#include "decode.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapter_error.h"
#include "bignum128.h"
#include "cbor_common.h"
#include "dcbor.h"
#include "float16.h"
#include "logical_value.h"
#include "nfc.h"
#include "rfc8949.h"

/* --- intermediate parse-tree ("Item") type: mirrors adapters/go/decode.go's
 * sealed `item` interface. Bytes/text spans borrow directly from the input
 * buffer (which outlives the whole decode_strict call), so freeing an Item
 * tree only ever frees the tree's own structural allocations, never the
 * borrowed spans. --- */

typedef enum {
    ITEM_INT_POS,
    ITEM_INT_NEG,
    ITEM_BYTES,
    ITEM_TEXT,
    ITEM_ARR,
    ITEM_MAP,
    ITEM_TAGGED,
    ITEM_BOOL,
    ITEM_NULL,
    ITEM_FLOAT
} ItemType;

typedef struct Item Item;

typedef struct {
    Item *k;
    Item *v;
} ItemMapEntry;

struct Item {
    ItemType type;
    union {
        uint64_t int_pos;
        uint64_t int_neg_arg; /* actual value is -1 - (u128)arg */
        struct {
            const uint8_t *bytes;
            size_t len;
        } bytesv;
        struct {
            const uint8_t *bytes;
            size_t len;
        } textv;
        struct {
            Item **items;
            size_t count;
        } arr;
        struct {
            ItemMapEntry *entries;
            size_t count;
        } map;
        struct {
            uint64_t tag;
            Item *inner;
        } tagged;
        bool boolv;
        double floatv;
    } as;
};

static Item *alloc_item(ItemType t) {
    Item *it = calloc(1, sizeof(Item));
    if (!it) abort();
    it->type = t;
    return it;
}

static void free_item(Item *it) {
    if (!it) return;
    switch (it->type) {
        case ITEM_ARR:
            for (size_t i = 0; i < it->as.arr.count; i++) free_item(it->as.arr.items[i]);
            free(it->as.arr.items);
            break;
        case ITEM_MAP:
            for (size_t i = 0; i < it->as.map.count; i++) {
                free_item(it->as.map.entries[i].k);
                free_item(it->as.map.entries[i].v);
            }
            free(it->as.map.entries);
            break;
        case ITEM_TAGGED:
            free_item(it->as.tagged.inner);
            break;
        default:
            break;
    }
    free(it);
}

/* --- reader cursor + head parsing --- */

typedef struct {
    const uint8_t *input;
    size_t len;
    size_t pos;
} Cursor;

typedef enum { WIDTH_DIRECT, WIDTH_ONE, WIDTH_TWO, WIDTH_FOUR, WIDTH_EIGHT } ArgWidth;

typedef struct {
    int major;
    uint64_t arg;
    ArgWidth width;
    bool indefinite;
} Head;

static bool read_head(Cursor *c, Head *out) {
    if (c->pos >= c->len) {
        adapter_set_error("internal: truncated input (expected an item header)");
        return false;
    }
    uint8_t b0 = c->input[c->pos];
    c->pos++;
    int major = (b0 >> 5) & 0x7;
    uint8_t info = b0 & 0x1f;
    if (info == 31) {
        out->major = major;
        out->indefinite = true;
        out->arg = 0;
        out->width = WIDTH_DIRECT;
        return true;
    }
    if (info <= 23) {
        out->major = major;
        out->arg = info;
        out->width = WIDTH_DIRECT;
        out->indefinite = false;
        return true;
    }
    if (info == 24) {
        if (c->pos >= c->len) {
            adapter_set_error("internal: truncated 1-byte argument");
            return false;
        }
        out->major = major;
        out->arg = c->input[c->pos];
        out->width = WIDTH_ONE;
        out->indefinite = false;
        c->pos += 1;
        return true;
    }
    if (info == 25) {
        if (c->pos + 2 > c->len) {
            adapter_set_error("internal: truncated 2-byte argument");
            return false;
        }
        uint64_t v = 0;
        for (int i = 0; i < 2; i++) v = (v << 8) | c->input[c->pos + i];
        out->major = major;
        out->arg = v;
        out->width = WIDTH_TWO;
        out->indefinite = false;
        c->pos += 2;
        return true;
    }
    if (info == 26) {
        if (c->pos + 4 > c->len) {
            adapter_set_error("internal: truncated 4-byte argument");
            return false;
        }
        uint64_t v = 0;
        for (int i = 0; i < 4; i++) v = (v << 8) | c->input[c->pos + i];
        out->major = major;
        out->arg = v;
        out->width = WIDTH_FOUR;
        out->indefinite = false;
        c->pos += 4;
        return true;
    }
    if (info == 27) {
        if (c->pos + 8 > c->len) {
            adapter_set_error("internal: truncated 8-byte argument");
            return false;
        }
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | c->input[c->pos + i];
        out->major = major;
        out->arg = v;
        out->width = WIDTH_EIGHT;
        out->indefinite = false;
        c->pos += 8;
        return true;
    }
    adapter_set_error("internal: reserved additional-info value (28-30)");
    return false;
}

static ArgWidth shortest_width_for(uint64_t arg) {
    if (arg < 24) return WIDTH_DIRECT;
    if (arg <= 0xff) return WIDTH_ONE;
    if (arg <= 0xffff) return WIDTH_TWO;
    if (arg <= 0xffffffffULL) return WIDTH_FOUR;
    return WIDTH_EIGHT;
}

/* P1/D2: every integer *argument* (ints themselves, and string/array/map
 * lengths, and tag numbers) must use the shortest encoding for its value. */
static const char *check_shortest_arg(Head h) {
    if (h.width != shortest_width_for(h.arg)) return "NON_SHORTEST_INT";
    return NULL;
}

static bool utf8_validate(const uint8_t *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t b0 = s[i];
        if (b0 < 0x80) {
            i += 1;
            continue;
        }
        size_t need;
        uint32_t cp, min_cp;
        if ((b0 & 0xe0) == 0xc0) {
            need = 1;
            cp = (uint32_t)(b0 & 0x1f);
            min_cp = 0x80;
        } else if ((b0 & 0xf0) == 0xe0) {
            need = 2;
            cp = (uint32_t)(b0 & 0x0f);
            min_cp = 0x800;
        } else if ((b0 & 0xf8) == 0xf0) {
            need = 3;
            cp = (uint32_t)(b0 & 0x07);
            min_cp = 0x10000;
        } else {
            return false;
        }
        if (i + 1 + need > len) return false;
        for (size_t k = 1; k <= need; k++) {
            uint8_t bk = s[i + k];
            if ((bk & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (uint32_t)(bk & 0x3f);
        }
        if (cp < min_cp) return false; /* overlong encoding */
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false; /* surrogate half */
        i += 1 + need;
    }
    return true;
}

static Item *parse_item(Cursor *c, Profile profile, const char **reject_reason);

static bool is_dcbor_reducible(double value) {
    if (isinf(value)) return false;
    if (value != floor(value)) return false;
    return value >= -9223372036854775808.0 && value <= 18446744073709551615.0;
}

typedef enum { FLOAT_F16, FLOAT_F32, FLOAT_F64 } FloatWidth;

static FloatWidth shortest_float_width(double value) {
    uint64_t target;
    memcpy(&target, &value, sizeof target);
    uint16_t f16_bits = double_to_f16_bits(value);
    double back16 = f16_bits_to_double(f16_bits);
    uint64_t back16_bits;
    memcpy(&back16_bits, &back16, sizeof back16_bits);
    if (back16_bits == target) return FLOAT_F16;
    float f32 = (float)value;
    double back32 = (double)f32;
    uint64_t back32_bits;
    memcpy(&back32_bits, &back32, sizeof back32_bits);
    if (back32_bits == target) return FLOAT_F32;
    return FLOAT_F64;
}

static Item *check_float(uint64_t bits, FloatWidth width, Profile profile, const char **reject_reason) {
    double value;
    switch (width) {
        case FLOAT_F16: {
            double v = f16_bits_to_double((uint16_t)bits);
            value = v;
            break;
        }
        case FLOAT_F32: {
            uint32_t b32 = (uint32_t)bits;
            float f;
            memcpy(&f, &b32, sizeof f);
            value = (double)f;
            break;
        }
        case FLOAT_F64:
        default: {
            memcpy(&value, &bits, sizeof value);
            break;
        }
    }

    if (isnan(value)) {
        /* P6/D6: canonical NaN is exactly f16 with payload 0x7e00, in both profiles. */
        if (width != FLOAT_F16 || bits != 0x7e00) {
            *reject_reason = "NAN_PAYLOAD_VARIANT";
            return NULL;
        }
        Item *it = alloc_item(ITEM_FLOAT);
        it->as.floatv = nan("");
        return it;
    }

    /* D5/D7 (dcbor only): any whole-number float in [-2^63, 2^64-1] --
     * including +/-0.0, per D7 zero unification -- must be a plain int
     * instead. This is checked before, and takes priority over, the
     * general shortest-float-width rule below. */
    if (profile == PROFILE_DCBOR && is_dcbor_reducible(value)) {
        *reject_reason = "UNREDUCED_NUMERIC";
        return NULL;
    }

    FloatWidth shortest = shortest_float_width(value);
    if (width != shortest) {
        *reject_reason = "NON_SHORTEST_FLOAT";
        return NULL;
    }

    Item *it = alloc_item(ITEM_FLOAT);
    it->as.floatv = value;
    return it;
}

static Item *parse_major7(ArgWidth width, uint64_t arg, Profile profile, const char **reject_reason) {
    switch (width) {
        case WIDTH_DIRECT:
            switch (arg) {
                case 20: {
                    Item *it = alloc_item(ITEM_BOOL);
                    it->as.boolv = false;
                    return it;
                }
                case 21: {
                    Item *it = alloc_item(ITEM_BOOL);
                    it->as.boolv = true;
                    return it;
                }
                case 22:
                    return alloc_item(ITEM_NULL);
                default:
                    adapter_set_error("internal: unsupported major-7 simple value %llu", (unsigned long long)arg);
                    return NULL;
            }
        case WIDTH_ONE:
            adapter_set_error("internal: unsupported major-7 1-byte simple value");
            return NULL;
        case WIDTH_TWO:
            return check_float(arg, FLOAT_F16, profile, reject_reason);
        case WIDTH_FOUR:
            return check_float(arg, FLOAT_F32, profile, reject_reason);
        case WIDTH_EIGHT:
            return check_float(arg, FLOAT_F64, profile, reject_reason);
        default:
            adapter_set_error("internal: unreachable float width");
            return NULL;
    }
}

static bool allowed_tag(uint64_t tag) {
    /* UNKNOWN_TAG allow-list: only the bignum tags are exercised by the
     * current vector corpus (CONTRIBUTING.md: grow only if the corpus
     * requires it). */
    return tag == 2 || tag == 3;
}

static Item *parse_item(Cursor *c, Profile profile, const char **reject_reason) {
    Head h;
    if (!read_head(c, &h)) return NULL;

    if (h.major == 7) {
        if (h.indefinite) {
            adapter_set_error("internal: unexpected break byte outside indefinite-length container");
            return NULL;
        }
        return parse_major7(h.width, h.arg, profile, reject_reason);
    }

    if (h.indefinite) {
        /* P3/D1: indefinite-length arrays/maps/byte/text strings are banned
         * outright, regardless of what they'd otherwise contain. */
        *reject_reason = "INDEFINITE_LENGTH";
        return NULL;
    }
    {
        const char *r = check_shortest_arg(h);
        if (r) {
            *reject_reason = r;
            return NULL;
        }
    }

    switch (h.major) {
        case 0: {
            Item *it = alloc_item(ITEM_INT_POS);
            it->as.int_pos = h.arg;
            return it;
        }
        case 1: {
            Item *it = alloc_item(ITEM_INT_NEG);
            it->as.int_neg_arg = h.arg;
            return it;
        }
        case 2: {
            size_t length = (size_t)h.arg;
            if (c->pos + length > c->len) {
                adapter_set_error("internal: truncated byte string");
                return NULL;
            }
            Item *it = alloc_item(ITEM_BYTES);
            it->as.bytesv.bytes = c->input + c->pos;
            it->as.bytesv.len = length;
            c->pos += length;
            return it;
        }
        case 3: {
            size_t length = (size_t)h.arg;
            if (c->pos + length > c->len) {
                adapter_set_error("internal: truncated text string");
                return NULL;
            }
            const uint8_t *b = c->input + c->pos;
            if (!utf8_validate(b, length)) {
                adapter_set_error("internal: invalid utf-8 in text string");
                return NULL;
            }
            c->pos += length;
            /* D8: dcbor requires NFC-normalized text; rfc8949 has no such
             * rule (P9 is an explicit non-goal). */
            if (profile == PROFILE_DCBOR) {
                size_t nlen;
                uint8_t *normalized = nfc_normalize_utf8(b, length, &nlen);
                bool same = nlen == length && (length == 0 || memcmp(normalized, b, length) == 0);
                free(normalized);
                if (!same) {
                    *reject_reason = "NON_NFC_STRING";
                    return NULL;
                }
            }
            Item *it = alloc_item(ITEM_TEXT);
            it->as.textv.bytes = b;
            it->as.textv.len = length;
            return it;
        }
        case 4: {
            size_t length = (size_t)h.arg;
            Item *it = alloc_item(ITEM_ARR);
            Item **items = length > 0 ? malloc(length * sizeof(Item *)) : NULL;
            if (length > 0 && !items) abort();
            for (size_t i = 0; i < length; i++) {
                Item *sub = parse_item(c, profile, reject_reason);
                if (!sub) {
                    for (size_t j = 0; j < i; j++) free_item(items[j]);
                    free(items);
                    free(it);
                    return NULL;
                }
                items[i] = sub;
            }
            it->as.arr.items = items;
            it->as.arr.count = length;
            return it;
        }
        case 5: {
            size_t length = (size_t)h.arg;
            Item *it = alloc_item(ITEM_MAP);
            ItemMapEntry *entries = length > 0 ? malloc(length * sizeof(ItemMapEntry)) : NULL;
            if (length > 0 && !entries) abort();
            size_t *key_start = length > 0 ? malloc(length * sizeof(size_t)) : NULL;
            size_t *key_end = length > 0 ? malloc(length * sizeof(size_t)) : NULL;
            if (length > 0 && (!key_start || !key_end)) abort();
            size_t built = 0;
            for (size_t i = 0; i < length; i++) {
                size_t ks = c->pos;
                Item *k = parse_item(c, profile, reject_reason);
                if (!k) goto map_fail;
                size_t ke = c->pos;
                Item *v = parse_item(c, profile, reject_reason);
                if (!v) {
                    free_item(k);
                    goto map_fail;
                }
                key_start[i] = ks;
                key_end[i] = ke;
                entries[i].k = k;
                entries[i].v = v;
                built = i + 1;
                continue;
            map_fail:
                for (size_t j = 0; j < built; j++) {
                    free_item(entries[j].k);
                    free_item(entries[j].v);
                }
                free(entries);
                free(key_start);
                free(key_end);
                free(it);
                return NULL;
            }
            /* P2/D3: keys must appear in strictly increasing bytewise order
             * of their own raw encoded bytes. A duplicate key is
             * necessarily adjacent to itself in properly sorted order, so
             * one adjacent pass catches both violations. */
            for (size_t i = 0; i + 1 < length; i++) {
                const uint8_t *a = c->input + key_start[i];
                size_t a_len = key_end[i] - key_start[i];
                const uint8_t *b = c->input + key_start[i + 1];
                size_t b_len = key_end[i + 1] - key_start[i + 1];
                int cmp = compare_bytes_unsigned(a, a_len, b, b_len);
                if (cmp == 0) {
                    for (size_t j = 0; j < length; j++) {
                        free_item(entries[j].k);
                        free_item(entries[j].v);
                    }
                    free(entries);
                    free(key_start);
                    free(key_end);
                    free(it);
                    *reject_reason = "DUPLICATE_KEY";
                    return NULL;
                }
                if (cmp > 0) {
                    for (size_t j = 0; j < length; j++) {
                        free_item(entries[j].k);
                        free_item(entries[j].v);
                    }
                    free(entries);
                    free(key_start);
                    free(key_end);
                    free(it);
                    *reject_reason = "UNSORTED_MAP_KEYS";
                    return NULL;
                }
            }
            free(key_start);
            free(key_end);
            it->as.map.entries = entries;
            it->as.map.count = length;
            return it;
        }
        case 6: {
            uint64_t tag = h.arg;
            if (!allowed_tag(tag)) {
                *reject_reason = "UNKNOWN_TAG";
                return NULL;
            }
            Item *inner = parse_item(c, profile, reject_reason);
            if (!inner) return NULL;
            Item *it = alloc_item(ITEM_TAGGED);
            it->as.tagged.tag = tag;
            it->as.tagged.inner = inner;
            return it;
        }
        default:
            adapter_set_error("internal: major type is 3 bits, always 0-7");
            return NULL;
    }
}

/* --- Item -> LogicalValue conversion (for ACCEPT re-encoding) --- */

static char *format_float_literal(double v) {
    char buf[64];
    if (isnan(v)) {
        snprintf(buf, sizeof buf, "NaN");
    } else {
        /* Not necessarily the *shortest* round-tripping decimal (unlike
         * Go's strconv.FormatFloat with prec=-1), but 17 significant
         * digits is sufficient precision to round-trip any IEEE double
         * exactly (C99), which is all this needs: the string is fed
         * straight back into cbor_parse_float_literal by our own encoder
         * during re-encoding, never emitted on stdout, so "shortest" isn't
         * required, only "round-trips to the identical bit pattern." */
        snprintf(buf, sizeof buf, "%.17g", v);
    }
    size_t l = strlen(buf);
    char *out = malloc(l + 1);
    if (!out) abort();
    memcpy(out, buf, l + 1);
    return out;
}

static LogicalValue *alloc_lv(LVType t) {
    LogicalValue *v = calloc(1, sizeof(LogicalValue));
    if (!v) abort();
    v->type = t;
    return v;
}

static LogicalValue *item_to_logical(const Item *it) {
    switch (it->type) {
        case ITEM_INT_POS: {
            char buf[40];
            u128_to_decimal_string((u128)it->as.int_pos, buf, sizeof buf);
            LogicalValue *v = alloc_lv(LV_INT);
            size_t l = strlen(buf);
            v->as.intv.value = malloc(l + 1);
            memcpy(v->as.intv.value, buf, l + 1);
            return v;
        }
        case ITEM_INT_NEG: {
            /* value = -1 - arg, computed exactly via u128 to avoid any
             * overflow at the arg == UINT64_MAX boundary (representing the
             * most negative plain-int value, -2^64). */
            u128 magnitude = (u128)it->as.int_neg_arg + 1;
            char digits[40];
            u128_to_decimal_string(magnitude, digits, sizeof digits);
            LogicalValue *v = alloc_lv(LV_INT);
            size_t l = strlen(digits);
            char *out = malloc(l + 2);
            out[0] = '-';
            memcpy(out + 1, digits, l + 1);
            v->as.intv.value = out;
            return v;
        }
        case ITEM_BYTES: {
            LogicalValue *v = alloc_lv(LV_BYTES);
            char *hex = hex_encode(it->as.bytesv.bytes, it->as.bytesv.len);
            v->as.bytesv.bytes = (uint8_t *)hex;
            v->as.bytesv.len = strlen(hex);
            return v;
        }
        case ITEM_TEXT: {
            LogicalValue *v = alloc_lv(LV_TEXT);
            uint8_t *copy = it->as.textv.len > 0 ? malloc(it->as.textv.len) : malloc(1);
            if (!copy) abort();
            memcpy(copy, it->as.textv.bytes, it->as.textv.len);
            v->as.textv.bytes = copy;
            v->as.textv.len = it->as.textv.len;
            return v;
        }
        case ITEM_ARR: {
            LogicalValue *v = alloc_lv(LV_ARRAY);
            size_t n = it->as.arr.count;
            LogicalValue **items = n > 0 ? malloc(n * sizeof(LogicalValue *)) : NULL;
            for (size_t i = 0; i < n; i++) items[i] = item_to_logical(it->as.arr.items[i]);
            v->as.arrayv.items = items;
            v->as.arrayv.count = n;
            return v;
        }
        case ITEM_MAP: {
            LogicalValue *v = alloc_lv(LV_MAP);
            size_t n = it->as.map.count;
            LVMapEntry *entries = n > 0 ? malloc(n * sizeof(LVMapEntry)) : NULL;
            for (size_t i = 0; i < n; i++) {
                entries[i].key = item_to_logical(it->as.map.entries[i].k);
                entries[i].val = item_to_logical(it->as.map.entries[i].v);
            }
            v->as.mapv.entries = entries;
            v->as.mapv.count = n;
            return v;
        }
        case ITEM_TAGGED: {
            LogicalValue *v = alloc_lv(LV_TAG);
            v->as.tagv.tag = it->as.tagged.tag;
            v->as.tagv.value = item_to_logical(it->as.tagged.inner);
            return v;
        }
        case ITEM_BOOL: {
            LogicalValue *v = alloc_lv(LV_BOOL);
            v->as.boolv = it->as.boolv;
            return v;
        }
        case ITEM_NULL:
            return alloc_lv(LV_NULL);
        case ITEM_FLOAT: {
            LogicalValue *v = alloc_lv(LV_FLOAT);
            char *width = malloc(5);
            if (!width) abort();
            memcpy(width, "auto", 5);
            v->as.floatv.width = width;
            v->as.floatv.value = format_float_literal(it->as.floatv);
            return v;
        }
        default:
            abort();
    }
}

bool decode_strict(const uint8_t *input, size_t len, Profile profile, Verdict *verdict) {
    if (len == 0) {
        adapter_set_error("internal: empty input line");
        return false;
    }
    Cursor c = {input, len, 0};
    const char *reject = NULL;
    Item *item = parse_item(&c, profile, &reject);
    if (!item) {
        if (reject) {
            verdict->accept = false;
            verdict->reason = reject;
            bytebuf_init(&verdict->bytes);
            return true;
        }
        return false;
    }

    if (c.pos < len) {
        Cursor c2 = {input, len, c.pos};
        const char *reject2 = NULL;
        Item *extra = parse_item(&c2, profile, &reject2);
        bool multiple = extra != NULL && c2.pos == len;
        if (extra) free_item(extra);
        free_item(item);
        verdict->accept = false;
        verdict->reason = multiple ? "MULTIPLE_TOP_LEVEL_ITEMS" : "TRAILING_BYTES";
        bytebuf_init(&verdict->bytes);
        return true;
    }

    LogicalValue *logical = item_to_logical(item);
    free_item(item);
    ByteBuf reenc;
    bytebuf_init(&reenc);
    bool enc_ok = (profile == PROFILE_RFC8949) ? encode_rfc8949(logical, &reenc) : encode_dcbor(logical, &reenc);
    logical_value_free(logical);
    if (!enc_ok) {
        bytebuf_free(&reenc);
        adapter_set_error("internal: canonical input failed to re-encode: %s", adapter_last_error());
        return false;
    }
    verdict->accept = true;
    verdict->bytes = reenc;
    verdict->reason = NULL;
    return true;
}
