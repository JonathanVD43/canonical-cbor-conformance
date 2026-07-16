#include <stdlib.h>
#include <string.h>

#include "decode.h"
#include "test_framework.h"
#include "util.h"

static void expect_reject(const char *hex, Profile profile, const char *reason) {
    uint8_t *bytes;
    size_t len;
    CHECK(hex_decode(hex, strlen(hex), &bytes, &len));
    Verdict v;
    CHECK(decode_strict(bytes, len, profile, &v));
    CHECK(!v.accept);
    if (!v.accept) {
        CHECK_STREQ(v.reason, reason);
    } else {
        bytebuf_free(&v.bytes);
    }
    free(bytes);
}

static void expect_accept(const char *hex, Profile profile) {
    uint8_t *bytes;
    size_t len;
    CHECK(hex_decode(hex, strlen(hex), &bytes, &len));
    Verdict v;
    CHECK(decode_strict(bytes, len, profile, &v));
    CHECK(v.accept);
    if (v.accept) {
        char *got = hex_encode(v.bytes.data, v.bytes.len);
        CHECK_STREQ(got, hex); /* ACCEPT re-encodes byte-identical to the input */
        free(got);
        bytebuf_free(&v.bytes);
    }
    free(bytes);
}

void run_decode_tests(void) {
    expect_accept("00", PROFILE_RFC8949);
    expect_accept("00", PROFILE_DCBOR);

    /* P1: 0x1800 is a non-shortest encoding of integer 0. */
    expect_reject("1800", PROFILE_RFC8949, "NON_SHORTEST_INT");

    /* P3/D1: indefinite-length array (0x9f ... 0xff). */
    expect_reject("9f01ff", PROFILE_RFC8949, "INDEFINITE_LENGTH");
    expect_reject("9f01ff", PROFILE_DCBOR, "INDEFINITE_LENGTH");

    /* P2/D3: map keys out of bytewise order (key 0x01 before key 0x00). */
    expect_reject("a2010a000b", PROFILE_RFC8949, "UNSORTED_MAP_KEYS");

    /* P4/D4: duplicate map key. */
    expect_reject("a200010002", PROFILE_RFC8949, "DUPLICATE_KEY");

    /* Two complete top-level items exactly filling the input. */
    expect_reject("0000", PROFILE_RFC8949, "MULTIPLE_TOP_LEVEL_ITEMS");

    /* One valid item followed by bytes that don't form a second complete item. */
    expect_reject("0aff", PROFILE_RFC8949, "TRAILING_BYTES");

    /* P8: unknown tags are always a hard error, even well-known ones like
     * tag 1 (epoch date/time) -- only tags 2/3 (bignum) are allow-listed. */
    expect_reject("c100", PROFILE_RFC8949, "UNKNOWN_TAG");
    expect_reject("c100", PROFILE_DCBOR, "UNKNOWN_TAG");

    /* P6/D6: non-canonical NaN payload (f16, exponent all-1s, nonzero
     * mantissa != 0x7e00). */
    expect_reject("f97c01", PROFILE_RFC8949, "NAN_PAYLOAD_VARIANT");

    /* dcbor-only: unnormalized text ("e" + combining acute accent, not
     * pre-composed to U+00E9) is rejected under dcbor (D8) but accepted
     * under rfc8949 (P9 explicitly does not normalize). */
    expect_reject("6365cc81", PROFILE_DCBOR, "NON_NFC_STRING");
    expect_accept("6365cc81", PROFILE_RFC8949);

    /* dcbor-only: a whole-number float in-range must be reduced to a plain
     * int (D5); rfc8949 has no such rule. Uses f16 5.0 = 0x4500 (verified
     * against this adapter's own float16.c round-trip). */
    expect_reject("f94500", PROFILE_DCBOR, "UNREDUCED_NUMERIC");
    expect_accept("f94500", PROFILE_RFC8949);

    /* Bignum accept round-trip: 2^64 via tag 2. */
    expect_accept("c249010000000000000000", PROFILE_RFC8949);
    expect_accept("c249010000000000000000", PROFILE_DCBOR);

    /* NON_CANONICAL_BIGNUM: (a) magnitude fits native range -- tag 2
     * wrapping magnitude 1. */
    expect_reject("c24101", PROFILE_RFC8949, "NON_CANONICAL_BIGNUM");
    expect_reject("c24101", PROFILE_DCBOR, "NON_CANONICAL_BIGNUM");
    /* (a) exact boundary: 2^64-1 (8-byte all-ones). */
    expect_reject("c248ffffffffffffffff", PROFILE_RFC8949, "NON_CANONICAL_BIGNUM");
    /* (b) non-minimal length: 2^64 with a leading zero byte. */
    expect_reject("c24a00010000000000000000", PROFILE_RFC8949, "NON_CANONICAL_BIGNUM");
    /* tag 3 negative equivalents. */
    expect_reject("c34101", PROFILE_DCBOR, "NON_CANONICAL_BIGNUM");
    expect_reject("c34a00010000000000000000", PROFILE_RFC8949, "NON_CANONICAL_BIGNUM");
    /* Genuinely canonical negative bignum still ACCEPTs. */
    expect_accept("c349010000000000000000", PROFILE_RFC8949);
}
