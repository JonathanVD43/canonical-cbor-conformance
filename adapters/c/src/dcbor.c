#include "dcbor.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "adapter_error.h"
#include "bignum128.h"
#include "cbor_common.h"
#include "float16.h"
#include "nfc.h"

/* D5/D7: a finite float with no fractional part (including -0.0, per D7's
 * zero unification) whose reduced integer value is in [-2^63, 2^64-1]
 * encodes as the equivalent plain CBOR integer instead of a float. The
 * literal bounds below intentionally mirror adapters/go/decode.go's
 * isDcborReducible bounds exactly (including the fact that
 * 18446744073709551615.0 the double literal rounds to 2^64, same
 * inexactness Go's literal has) so encode/decode-strict agree on the
 * boundary. */
static bool try_numeric_reduction(double f, ByteBuf *out) {
    if (!isfinite(f)) return false;
    if (f != floor(f)) return false;
    u128 two64 = u128_two_pow_64();
    if (f >= 0.0) {
        if (f < 18446744073709551616.0) { /* 2^64 */
            u128 whole = (u128)f;
            if (whole < two64) {
                cbor_encode_head(0, (uint64_t)whole, out);
                return true;
            }
        }
    } else {
        if (f >= -9223372036854775808.0) { /* -2^63 */
            u128 mag = (u128)(-f);
            u128 arg = mag - 1;
            if (arg < two64) {
                cbor_encode_head(1, (uint64_t)arg, out);
                return true;
            }
        }
    }
    /* Falls through to shortest-float form if it doesn't fit the native
     * int range (no dcbor vector in the corpus exercises this edge --
     * matches adapters/go/dcbor.go's comment on the same fallthrough). */
    return false;
}

static bool encode_float_dcbor(const char *literal, ByteBuf *out) {
    double f;
    if (!cbor_parse_float_literal(literal, &f)) return false;
    if (isnan(f)) {
        /* D6: NaN always uses the single canonical f16 payload. */
        bytebuf_push(out, 0xf9);
        bytebuf_push(out, 0x7e);
        bytebuf_push(out, 0x00);
        return true;
    }
    if (try_numeric_reduction(f, out)) return true;
    uint8_t b2[2];
    if (try_f16(f, b2)) {
        bytebuf_push(out, 0xf9);
        bytebuf_append(out, b2, 2);
        return true;
    }
    uint8_t b4[4];
    if (try_f32(f, b4)) {
        bytebuf_push(out, 0xfa);
        bytebuf_append(out, b4, 4);
        return true;
    }
    uint8_t b8[8];
    cbor_double_to_be_bytes(f, b8);
    bytebuf_push(out, 0xfb);
    bytebuf_append(out, b8, 8);
    return true;
}

typedef struct {
    ByteBuf key;
    ByteBuf val;
} DedupEntry;

static int compare_dedup(size_t a, size_t b, void *ctx) {
    DedupEntry *entries = (DedupEntry *)ctx;
    return compare_bytes_unsigned(entries[a].key.data, entries[a].key.len, entries[b].key.data, entries[b].key.len);
}

static bool encode_map_dcbor(const LogicalValue *value, ByteBuf *out) {
    size_t n = value->as.mapv.count;
    DedupEntry *dedup = n > 0 ? malloc(n * sizeof(DedupEntry)) : NULL;
    if (n > 0 && !dedup) abort();
    size_t dedup_count = 0;

    for (size_t i = 0; i < n; i++) {
        ByteBuf ekey, eval;
        bytebuf_init(&ekey);
        bytebuf_init(&eval);
        if (!encode_dcbor(value->as.mapv.entries[i].key, &ekey) ||
            !encode_dcbor(value->as.mapv.entries[i].val, &eval)) {
            bytebuf_free(&ekey);
            bytebuf_free(&eval);
            for (size_t j = 0; j < dedup_count; j++) {
                bytebuf_free(&dedup[j].key);
                bytebuf_free(&dedup[j].val);
            }
            free(dedup);
            return false;
        }
        /* dCBOR's reference encoder stores entries in a real map keyed by
         * canonical-encoded bytes, so two logical entries that canonicalize
         * to the same key (e.g. via D7 zero unification or D8 NFC
         * normalization) collapse to one, last write wins -- the position
         * in `dedup` is fixed at first occurrence, but the value is
         * overwritten on every subsequent occurrence of the same key. */
        size_t found = SIZE_MAX;
        for (size_t j = 0; j < dedup_count; j++) {
            if (compare_bytes_unsigned(dedup[j].key.data, dedup[j].key.len, ekey.data, ekey.len) == 0) {
                found = j;
                break;
            }
        }
        if (found == SIZE_MAX) {
            dedup[dedup_count].key = ekey;
            dedup[dedup_count].val = eval;
            dedup_count++;
        } else {
            bytebuf_free(&ekey); /* identical to dedup[found].key; discard the duplicate copy */
            bytebuf_free(&dedup[found].val);
            dedup[found].val = eval;
        }
    }

    size_t *order = dedup_count > 0 ? malloc(dedup_count * sizeof(size_t)) : NULL;
    if (dedup_count > 0 && !order) abort();
    stable_sort_indices(dedup_count, order, compare_dedup, dedup);

    cbor_encode_head(5, dedup_count, out);
    for (size_t i = 0; i < dedup_count; i++) {
        DedupEntry *e = &dedup[order[i]];
        bytebuf_append(out, e->key.data, e->key.len);
        bytebuf_append(out, e->val.data, e->val.len);
    }
    free(order);
    for (size_t i = 0; i < dedup_count; i++) {
        bytebuf_free(&dedup[i].key);
        bytebuf_free(&dedup[i].val);
    }
    free(dedup);
    return true;
}

bool encode_dcbor(const LogicalValue *value, ByteBuf *out) {
    switch (value->type) {
        case LV_INT:
            return cbor_encode_int(value->as.intv.value, out);
        case LV_FLOAT:
            return encode_float_dcbor(value->as.floatv.value, out);
        case LV_TEXT: {
            /* D8: normalize to NFC before UTF-8 encoding. */
            size_t nlen;
            uint8_t *normalized = nfc_normalize_utf8(value->as.textv.bytes, value->as.textv.len, &nlen);
            cbor_encode_head(3, nlen, out);
            bytebuf_append(out, normalized, nlen);
            free(normalized);
            return true;
        }
        case LV_BYTES: {
            uint8_t *decoded;
            size_t decoded_len;
            if (!hex_decode((const char *)value->as.bytesv.bytes, value->as.bytesv.len, &decoded, &decoded_len)) {
                adapter_set_error("bytes: invalid hex string");
                return false;
            }
            cbor_encode_head(2, decoded_len, out);
            bytebuf_append(out, decoded, decoded_len);
            free(decoded);
            return true;
        }
        case LV_BOOL:
            bytebuf_push(out, value->as.boolv ? 0xf5 : 0xf4);
            return true;
        case LV_NULL:
            bytebuf_push(out, 0xf6);
            return true;
        case LV_ARRAY: {
            cbor_encode_head(4, value->as.arrayv.count, out);
            for (size_t i = 0; i < value->as.arrayv.count; i++) {
                if (!encode_dcbor(value->as.arrayv.items[i], out)) return false;
            }
            return true;
        }
        case LV_MAP:
            return encode_map_dcbor(value, out);
        case LV_TAG: {
            cbor_encode_head(6, value->as.tagv.tag, out);
            return encode_dcbor(value->as.tagv.value, out);
        }
        case LV_BIGNUM:
            return cbor_encode_bignum(value->as.bignumv.sign, value->as.bignumv.value, out);
        default:
            adapter_set_error("unhandled logical value type");
            return false;
    }
}
