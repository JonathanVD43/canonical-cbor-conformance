#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dcbor.h"
#include "logical_value.h"
#include "test_framework.h"
#include "util.h"

static char *encode_line(const char *json) {
    char *err = NULL;
    LogicalValue *v = parse_logical_value_line(json, strlen(json), &err);
    if (!v) {
        fprintf(stderr, "parse failed for %s: %s\n", json, err ? err : "?");
        free(err);
        return NULL;
    }
    ByteBuf out;
    bytebuf_init(&out);
    bool ok = encode_dcbor(v, &out);
    logical_value_free(v);
    if (!ok) {
        bytebuf_free(&out);
        return NULL;
    }
    char *hex = hex_encode(out.data, out.len);
    bytebuf_free(&out);
    return hex;
}

void run_dcbor_tests(void) {
    char *hex;

    /* D7: 0, 0.0, and -0.0 all encode as the plain integer 0x00 in dcbor
     * (opposite of rfc8949's P7, which keeps -0.0 distinct). */
    hex = encode_line("{\"type\":\"float\",\"width\":\"auto\",\"value\":\"0.0\"}");
    CHECK_STREQ(hex, "00");
    free(hex);
    hex = encode_line("{\"type\":\"float\",\"width\":\"auto\",\"value\":\"-0.0\"}");
    CHECK_STREQ(hex, "00");
    free(hex);
    hex = encode_line("{\"type\":\"int\",\"value\":\"0\"}");
    CHECK_STREQ(hex, "00");
    free(hex);

    /* D5: numeric reduction -- a whole-number float reduces to a plain int. */
    hex = encode_line("{\"type\":\"float\",\"width\":\"auto\",\"value\":\"5.0\"}");
    CHECK_STREQ(hex, "05");
    free(hex);
    hex = encode_line("{\"type\":\"float\",\"width\":\"auto\",\"value\":\"-5.0\"}");
    CHECK_STREQ(hex, "24"); /* major1 arg4 = -5 */
    free(hex);

    /* Non-whole floats are unaffected by D5. */
    hex = encode_line("{\"type\":\"float\",\"width\":\"auto\",\"value\":\"2.5\"}");
    CHECK_STREQ(hex, "f94100");
    free(hex);

    /* D6: NaN pins to the canonical f16 payload. */
    hex = encode_line("{\"type\":\"float\",\"width\":\"f64\",\"value\":\"NaN\"}");
    CHECK_STREQ(hex, "f97e00");
    free(hex);

    /* D8: text is normalized to NFC. U+0065 U+0301 (e + combining acute
     * accent) normalizes to U+00E9 (e-acute), encoded as UTF-8 c3 a9. */
    hex = encode_line("{\"type\":\"text\",\"value\":\"e\\u0301\"}");
    CHECK_STREQ(hex, "62c3a9");
    free(hex);

    /* Map-key-collision dedup, last write wins -- order-dependent per
     * CONTRIBUTING.md: [0.0, 0] keeps 0's value; [0, 0.0] keeps 0.0's
     * value, even though both logical keys collapse to the same encoded
     * key byte 0x00. */
    hex = encode_line(
        "{\"type\":\"map\",\"entries\":["
        "[{\"type\":\"float\",\"width\":\"auto\",\"value\":\"0.0\"},{\"type\":\"int\",\"value\":\"10\"}],"
        "[{\"type\":\"int\",\"value\":\"0\"},{\"type\":\"int\",\"value\":\"20\"}]"
        "]}");
    CHECK_STREQ(hex, "a10014"); /* map1, key 0x00, value 0x14 = 20 (0's value, came last) */
    free(hex);

    hex = encode_line(
        "{\"type\":\"map\",\"entries\":["
        "[{\"type\":\"int\",\"value\":\"0\"},{\"type\":\"int\",\"value\":\"20\"}],"
        "[{\"type\":\"float\",\"width\":\"auto\",\"value\":\"0.0\"},{\"type\":\"int\",\"value\":\"10\"}]"
        "]}");
    CHECK_STREQ(hex, "a1000a"); /* value 0x0a = 10 (0.0's value, came last) */
    free(hex);

    /* Map-key sort is still pure bytewise (same rule as rfc8949, D3). */
    hex = encode_line(
        "{\"type\":\"map\",\"entries\":["
        "[{\"type\":\"int\",\"value\":\"-1\"},{\"type\":\"int\",\"value\":\"200\"}],"
        "[{\"type\":\"int\",\"value\":\"32\"},{\"type\":\"int\",\"value\":\"100\"}]"
        "]}");
    CHECK_STREQ(hex, "a2182018642018c8");
    free(hex);

    /* Bignum rule is shared with rfc8949 (no dcbor-specific variant). */
    hex = encode_line("{\"type\":\"bignum\",\"sign\":\"positive\",\"value\":\"5\"}");
    CHECK(hex == NULL);
    free(hex);
}
