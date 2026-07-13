#include <stdlib.h>
#include <string.h>

#include "cbor_common.h"
#include "test_framework.h"
#include "util.h"

static void check_hex(ByteBuf *b, const char *expected_hex) {
    char *got = hex_encode(b->data, b->len);
    CHECK_STREQ(got, expected_hex);
    free(got);
}

void run_cbor_common_tests(void) {
    ByteBuf b;

    /* P1: shortest-int-argument encoding at every width boundary. */
    bytebuf_init(&b);
    cbor_encode_head(0, 0, &b);
    check_hex(&b, "00");
    bytebuf_free(&b);

    bytebuf_init(&b);
    cbor_encode_head(0, 23, &b);
    check_hex(&b, "17");
    bytebuf_free(&b);

    bytebuf_init(&b);
    cbor_encode_head(0, 24, &b);
    check_hex(&b, "1818");
    bytebuf_free(&b);

    bytebuf_init(&b);
    cbor_encode_head(0, 256, &b);
    check_hex(&b, "190100");
    bytebuf_free(&b);

    bytebuf_init(&b);
    cbor_encode_head(0, 0x10000, &b);
    check_hex(&b, "1a00010000");
    bytebuf_free(&b);

    bytebuf_init(&b);
    cbor_encode_head(0, 0x100000000ULL, &b);
    check_hex(&b, "1b0000000100000000");
    bytebuf_free(&b);

    /* Plain int encode. */
    bytebuf_init(&b);
    CHECK(cbor_encode_int("0", &b));
    check_hex(&b, "00");
    bytebuf_free(&b);

    bytebuf_init(&b);
    CHECK(cbor_encode_int("-1", &b));
    check_hex(&b, "20");
    bytebuf_free(&b);

    /* "-0" normalizes to plain 0x00 (matches Go's big.Int Sign()==0 for -0). */
    bytebuf_init(&b);
    CHECK(cbor_encode_int("-0", &b));
    check_hex(&b, "00");
    bytebuf_free(&b);

    /* Native-range boundary: 2^64-1 fits; 2^64 does not. */
    bytebuf_init(&b);
    CHECK(cbor_encode_int("18446744073709551615", &b));
    check_hex(&b, "1bffffffffffffffff");
    bytebuf_free(&b);

    bytebuf_init(&b);
    CHECK(!cbor_encode_int("18446744073709551616", &b));
    bytebuf_free(&b);

    /* Most negative representable plain int is -2^64 (arg == 2^64-1). */
    bytebuf_init(&b);
    CHECK(cbor_encode_int("-18446744073709551616", &b));
    check_hex(&b, "3bffffffffffffffff");
    bytebuf_free(&b);

    bytebuf_init(&b);
    CHECK(!cbor_encode_int("-18446744073709551617", &b));
    bytebuf_free(&b);

    /* Bignum rule: a magnitude that fits the native 64-bit range must be
     * rejected as a bignum -- it's only ever valid as a plain int. */
    int tag;
    ByteBuf magbytes;
    bytebuf_init(&magbytes);
    CHECK(!cbor_bignum_tag_and_bytes("positive", "5", &tag, &magbytes));
    bytebuf_free(&magbytes);

    bytebuf_init(&magbytes);
    CHECK(!cbor_bignum_tag_and_bytes("negative", "9223372036854775808", &tag, &magbytes));
    bytebuf_free(&magbytes);

    /* 2^64 is the smallest magnitude that must use tag 2. */
    bytebuf_init(&b);
    CHECK(cbor_encode_bignum("positive", "18446744073709551616", &b));
    check_hex(&b, "c249010000000000000000");
    bytebuf_free(&b);

    /* Negative bignum offset-by-one, per SPEC.md's bignum rule. */
    bytebuf_init(&b);
    CHECK(cbor_encode_bignum("negative", "18446744073709551617", &b));
    /* magnitude - 1 = 2^64, minimal bytes 01 00...00 (9 bytes) */
    check_hex(&b, "c349010000000000000000");
    bytebuf_free(&b);

    /* Float literal parsing. */
    double f;
    CHECK(cbor_parse_float_literal("2.5", &f) && f == 2.5);
    CHECK(cbor_parse_float_literal("-0.0", &f) && f == 0.0 && 1.0 / f < 0);
    CHECK(cbor_parse_float_literal("NaN", &f));
    CHECK(cbor_parse_float_literal("Infinity", &f) && f > 0);
    CHECK(cbor_parse_float_literal("-Infinity", &f) && f < 0);
    CHECK(!cbor_parse_float_literal("not a number", &f));
}
