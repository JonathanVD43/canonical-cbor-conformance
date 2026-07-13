#include "rfc8949.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "adapter_error.h"
#include "cbor_common.h"
#include "float16.h"

static bool encode_float_rfc8949(const char *width, const char *literal, ByteBuf *out) {
    double f;
    if (!cbor_parse_float_literal(literal, &f)) return false;
    if (isnan(f)) {
        /* P6: NaN always uses the single canonical f16 payload, regardless
         * of requested width. */
        bytebuf_push(out, 0xf9);
        bytebuf_push(out, 0x7e);
        bytebuf_push(out, 0x00);
        return true;
    }
    if (strcmp(width, "auto") == 0) {
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
    if (strcmp(width, "f16") == 0) {
        uint8_t b2[2];
        if (!try_f16(f, b2)) {
            adapter_set_error("float: %g cannot round-trip at requested width f16", f);
            return false;
        }
        bytebuf_push(out, 0xf9);
        bytebuf_append(out, b2, 2);
        return true;
    }
    if (strcmp(width, "f32") == 0) {
        uint8_t b4[4];
        if (!try_f32(f, b4)) {
            adapter_set_error("float: %g cannot round-trip at requested width f32", f);
            return false;
        }
        bytebuf_push(out, 0xfa);
        bytebuf_append(out, b4, 4);
        return true;
    }
    if (strcmp(width, "f64") == 0) {
        uint8_t b8[8];
        cbor_double_to_be_bytes(f, b8);
        bytebuf_push(out, 0xfb);
        bytebuf_append(out, b8, 8);
        return true;
    }
    adapter_set_error("float: unknown width \"%s\"", width);
    return false;
}

typedef struct {
    ByteBuf key;
    ByteBuf val;
} EncodedEntry;

static int compare_entries(size_t a, size_t b, void *ctx) {
    EncodedEntry *entries = (EncodedEntry *)ctx;
    return compare_bytes_unsigned(entries[a].key.data, entries[a].key.len, entries[b].key.data, entries[b].key.len);
}

bool encode_rfc8949(const LogicalValue *value, ByteBuf *out) {
    switch (value->type) {
        case LV_INT:
            return cbor_encode_int(value->as.intv.value, out);
        case LV_TEXT: {
            cbor_encode_head(3, value->as.textv.len, out);
            bytebuf_append(out, value->as.textv.bytes, value->as.textv.len);
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
                if (!encode_rfc8949(value->as.arrayv.items[i], out)) return false;
            }
            return true;
        }
        case LV_FLOAT:
            return encode_float_rfc8949(value->as.floatv.width, value->as.floatv.value, out);
        case LV_MAP: {
            size_t n = value->as.mapv.count;
            EncodedEntry *entries = n > 0 ? calloc(n, sizeof(EncodedEntry)) : NULL;
            if (n > 0 && !entries) abort();
            bool ok = true;
            for (size_t i = 0; i < n; i++) {
                bytebuf_init(&entries[i].key);
                bytebuf_init(&entries[i].val);
                if (!encode_rfc8949(value->as.mapv.entries[i].key, &entries[i].key)) {
                    ok = false;
                    n = i + 1; /* only free up to and including this partially-built entry */
                    break;
                }
                if (!encode_rfc8949(value->as.mapv.entries[i].val, &entries[i].val)) {
                    ok = false;
                    n = i + 1;
                    break;
                }
            }
            if (!ok) {
                for (size_t i = 0; i < n; i++) {
                    bytebuf_free(&entries[i].key);
                    bytebuf_free(&entries[i].val);
                }
                free(entries);
                return false;
            }
            n = value->as.mapv.count;
            size_t *order = n > 0 ? malloc(n * sizeof(size_t)) : NULL;
            if (n > 0 && !order) abort();
            stable_sort_indices(n, order, compare_entries, entries);
            cbor_encode_head(5, n, out);
            for (size_t i = 0; i < n; i++) {
                EncodedEntry *e = &entries[order[i]];
                bytebuf_append(out, e->key.data, e->key.len);
                bytebuf_append(out, e->val.data, e->val.len);
            }
            free(order);
            for (size_t i = 0; i < n; i++) {
                bytebuf_free(&entries[i].key);
                bytebuf_free(&entries[i].val);
            }
            free(entries);
            return true;
        }
        case LV_TAG: {
            cbor_encode_head(6, value->as.tagv.tag, out);
            return encode_rfc8949(value->as.tagv.value, out);
        }
        case LV_BIGNUM:
            return cbor_encode_bignum(value->as.bignumv.sign, value->as.bignumv.value, out);
        default:
            adapter_set_error("unhandled logical value type");
            return false;
    }
}
