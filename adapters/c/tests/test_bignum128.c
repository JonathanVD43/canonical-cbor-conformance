#include <string.h>

#include "bignum128.h"
#include "test_framework.h"

void run_bignum128_tests(void) {
    u128 v;
    CHECK(u128_parse_decimal("0", strlen("0"), &v) && v == 0);
    CHECK(u128_parse_decimal("18446744073709551616", strlen("18446744073709551616"), &v)); /* 2^64 */
    CHECK(v == u128_two_pow_64());
    CHECK(u128_parse_decimal("18446744073709551615", strlen("18446744073709551615"), &v)); /* 2^64 - 1 */
    CHECK(v == u128_two_pow_64() - 1);

    /* Empty / non-digit input rejected. */
    CHECK(!u128_parse_decimal("", 0, &v));
    CHECK(!u128_parse_decimal("12a3", strlen("12a3"), &v));

    /* Overflow beyond 128 bits is detected, not silently wrapped. */
    {
        const char *huge = "999999999999999999999999999999999999999999";
        CHECK(!u128_parse_decimal(huge, strlen(huge), &v));
    }

    /* Minimal big-endian byte extraction, no leading zero byte. */
    ByteBuf buf;
    bytebuf_init(&buf);
    u128_to_minimal_be_bytes(u128_two_pow_64(), &buf); /* 2^64 -> 01 00...00 (9 bytes) */
    CHECK(buf.len == 9);
    CHECK(buf.data[0] == 0x01);
    for (size_t i = 1; i < buf.len; i++) CHECK(buf.data[i] == 0x00);
    bytebuf_free(&buf);

    bytebuf_init(&buf);
    u128_to_minimal_be_bytes(0, &buf); /* zero still yields one byte, not zero bytes */
    CHECK(buf.len == 1 && buf.data[0] == 0x00);
    bytebuf_free(&buf);

    char digits[40];
    u128_to_decimal_string(u128_two_pow_64(), digits, sizeof digits);
    CHECK(strcmp(digits, "18446744073709551616") == 0);
    u128_to_decimal_string(0, digits, sizeof digits);
    CHECK(strcmp(digits, "0") == 0);
}
