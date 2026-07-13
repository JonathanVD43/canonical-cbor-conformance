#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logical_value.h"
#include "rfc8949.h"
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
    bool ok = encode_rfc8949(v, &out);
    logical_value_free(v);
    if (!ok) {
        bytebuf_free(&out);
        return NULL;
    }
    char *hex = hex_encode(out.data, out.len);
    bytebuf_free(&out);
    return hex;
}

void run_rfc8949_tests(void) {
    char *hex;

    hex = encode_line("{\"type\":\"int\",\"value\":\"0\"}");
    CHECK_STREQ(hex, "00");
    free(hex);

    hex = encode_line("{\"type\":\"text\",\"value\":\"caf\\u00e9\"}");
    CHECK_STREQ(hex, "65636166c3a9"); /* head(major3,len5) + "caf\xc3\xa9" (5 UTF-8 bytes) */
    free(hex);

    hex = encode_line("{\"type\":\"bytes\",\"value\":\"deadbeef\"}");
    CHECK_STREQ(hex, "44deadbeef");
    free(hex);

    /* P2: map-key sort is pure bytewise-lexicographic on the *encoded* key
     * bytes, not length-first (RFC 7049's older rule). Encoded key 32 is
     * 0x1820 (2 bytes, starts with 0x18); encoded key -1 is 0x20 (1 byte,
     * starts with 0x20). Bytewise order puts 32 first (0x18 < 0x20) even
     * though its encoding is *longer* -- a length-first sort would get
     * this backwards. */
    hex = encode_line(
        "{\"type\":\"map\",\"entries\":["
        "[{\"type\":\"int\",\"value\":\"-1\"},{\"type\":\"int\",\"value\":\"200\"}],"
        "[{\"type\":\"int\",\"value\":\"32\"},{\"type\":\"int\",\"value\":\"100\"}]"
        "]}");
    CHECK_STREQ(hex, "a2182018642018c8");
    free(hex);

    /* P7: -0.0 preserved distinct from 0.0 in rfc8949 (unlike dcbor's D7). */
    hex = encode_line("{\"type\":\"float\",\"width\":\"auto\",\"value\":\"-0.0\"}");
    CHECK_STREQ(hex, "f98000");
    free(hex);
    hex = encode_line("{\"type\":\"float\",\"width\":\"auto\",\"value\":\"0.0\"}");
    CHECK_STREQ(hex, "f90000");
    free(hex);

    /* P6: NaN always pins to the canonical f16 payload regardless of width. */
    hex = encode_line("{\"type\":\"float\",\"width\":\"f64\",\"value\":\"NaN\"}");
    CHECK_STREQ(hex, "f97e00");
    free(hex);

    /* Bignum rule: fits-native-range magnitudes must be rejected as bignum. */
    hex = encode_line("{\"type\":\"bignum\",\"sign\":\"positive\",\"value\":\"5\"}");
    CHECK(hex == NULL);
    free(hex);

    hex = encode_line("{\"type\":\"bignum\",\"sign\":\"positive\",\"value\":\"18446744073709551616\"}");
    CHECK_STREQ(hex, "c249010000000000000000");
    free(hex);

    /* rfc8949 has no dedup rule: literal duplicate keys pass through as two
     * separate encoded entries (P4 duplicate-key rejection is a
     * decode-strict concern, not an encode-time one). */
    hex = encode_line(
        "{\"type\":\"map\",\"entries\":["
        "[{\"type\":\"int\",\"value\":\"0\"},{\"type\":\"int\",\"value\":\"1\"}],"
        "[{\"type\":\"int\",\"value\":\"0\"},{\"type\":\"int\",\"value\":\"2\"}]"
        "]}");
    CHECK_STREQ(hex, "a200010002");
    free(hex);

    /* Explicit float width that can't round-trip is rejected. */
    hex = encode_line("{\"type\":\"float\",\"width\":\"f16\",\"value\":\"0.1\"}");
    CHECK(hex == NULL);
    free(hex);

    /* auto width picks the shortest that round-trips: 1.5 fits f16. */
    hex = encode_line("{\"type\":\"float\",\"width\":\"auto\",\"value\":\"1.5\"}");
    CHECK_STREQ(hex, "f93e00");
    free(hex);

    /* tag round-trip */
    hex = encode_line("{\"type\":\"tag\",\"tag\":100,\"value\":{\"type\":\"int\",\"value\":\"5\"}}");
    CHECK_STREQ(hex, "d86405");
    free(hex);
}
