#include "cbor_common.h"

#include <stdlib.h>
#include <string.h>

#include "adapter_error.h"
#include "bignum128.h"

void cbor_encode_head(int major_type, uint64_t arg, ByteBuf *out) {
    uint8_t mt = (uint8_t)(major_type << 5);
    if (arg < 24) {
        bytebuf_push(out, (uint8_t)(mt | (uint8_t)arg));
    } else if (arg <= 0xffULL) {
        bytebuf_push(out, (uint8_t)(mt | 24));
        bytebuf_push(out, (uint8_t)arg);
    } else if (arg <= 0xffffULL) {
        bytebuf_push(out, (uint8_t)(mt | 25));
        bytebuf_append_be(out, arg, 2);
    } else if (arg <= 0xffffffffULL) {
        bytebuf_push(out, (uint8_t)(mt | 26));
        bytebuf_append_be(out, arg, 4);
    } else {
        bytebuf_push(out, (uint8_t)(mt | 27));
        bytebuf_append_be(out, arg, 8);
    }
}

bool cbor_encode_int(const char *value, ByteBuf *out) {
    size_t len = strlen(value);
    if (len == 0) {
        adapter_set_error("int: empty literal");
        return false;
    }
    bool negative = false;
    size_t start = 0;
    if (value[0] == '-') {
        negative = true;
        start = 1;
    }
    if (len - start == 0) {
        adapter_set_error("int: invalid integer literal \"%s\"", value);
        return false;
    }
    u128 magnitude;
    if (!u128_parse_decimal(value + start, len - start, &magnitude)) {
        adapter_set_error("int: invalid integer literal \"%s\"", value);
        return false;
    }
    /* "-0" is the same integer as "0"; a native bignum type would fold this
     * automatically (as Go's math/big.Int does -- Sign() reports 0, not
     * negative), so replicate that normalization explicitly here rather
     * than encoding a spurious major-type-1 zero. */
    if (magnitude == 0) negative = false;

    u128 two64 = u128_two_pow_64();
    if (!negative) {
        if (magnitude >= two64) {
            adapter_set_error("int: %s exceeds native range", value);
            return false;
        }
        cbor_encode_head(0, (uint64_t)magnitude, out);
        return true;
    }
    u128 arg = magnitude - 1;
    if (arg >= two64) {
        adapter_set_error("int: %s exceeds native range", value);
        return false;
    }
    cbor_encode_head(1, (uint64_t)arg, out);
    return true;
}

bool cbor_parse_float_literal(const char *literal, double *out) {
    if (!literal || literal[0] == '\0') {
        adapter_set_error("float: invalid literal \"%s\"", literal ? literal : "");
        return false;
    }
    char *endptr = NULL;
    double v = strtod(literal, &endptr);
    if (endptr == literal || *endptr != '\0') {
        adapter_set_error("float: invalid literal \"%s\"", literal);
        return false;
    }
    *out = v;
    return true;
}

void cbor_double_to_be_bytes(double f, uint8_t out[8]) {
    uint64_t bits;
    memcpy(&bits, &f, sizeof bits);
    for (int i = 0; i < 8; i++) {
        out[i] = (uint8_t)(bits >> (size_t)((7 - i) * 8));
    }
}

bool cbor_bignum_tag_and_bytes(const char *sign, const char *value, int *tag_out, ByteBuf *bytes_out) {
    u128 magnitude;
    if (!u128_parse_decimal(value, strlen(value), &magnitude)) {
        adapter_set_error("bignum: invalid magnitude \"%s\"", value);
        return false;
    }
    int tag = (strcmp(sign, "positive") == 0) ? 2 : 3;
    if (magnitude < u128_two_pow_64()) {
        adapter_set_error(
            "bignum magnitude %s fits in a native CBOR integer and must not be encoded as tag %d (SPEC.md bignum rule)",
            value, tag);
        return false;
    }
    u128 raw_int;
    if (strcmp(sign, "positive") == 0) {
        raw_int = magnitude;
    } else if (strcmp(sign, "negative") == 0) {
        raw_int = magnitude - 1;
    } else {
        adapter_set_error("bignum: unknown sign \"%s\"", sign);
        return false;
    }
    u128_to_minimal_be_bytes(raw_int, bytes_out);
    *tag_out = tag;
    return true;
}

bool cbor_encode_bignum(const char *sign, const char *value, ByteBuf *out) {
    int tag;
    ByteBuf magbytes;
    bytebuf_init(&magbytes);
    if (!cbor_bignum_tag_and_bytes(sign, value, &tag, &magbytes)) {
        bytebuf_free(&magbytes);
        return false;
    }
    cbor_encode_head(6, (uint64_t)tag, out);
    cbor_encode_head(2, magbytes.len, out);
    bytebuf_append(out, magbytes.data, magbytes.len);
    bytebuf_free(&magbytes);
    return true;
}
