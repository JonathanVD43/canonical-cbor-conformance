#include <stdlib.h>
#include <string.h>

#include "nfc.h"
#include "test_framework.h"

static void check_norm(const uint8_t *in, size_t in_len, const uint8_t *expected, size_t expected_len) {
    size_t out_len;
    uint8_t *out = nfc_normalize_utf8(in, in_len, &out_len);
    CHECK(out_len == expected_len);
    CHECK(out_len == expected_len && memcmp(out, expected, expected_len) == 0);
    free(out);
}

void run_nfc_tests(void) {
    /* "e" + combining acute accent (U+0065 U+0301) -> precomposed U+00E9. */
    uint8_t decomposed[] = {0x65, 0xcc, 0x81};
    uint8_t composed[] = {0xc3, 0xa9};
    check_norm(decomposed, sizeof decomposed, composed, sizeof composed);

    /* Already-NFC text is a no-op. */
    check_norm(composed, sizeof composed, composed, sizeof composed);

    /* ASCII is always already NFC. */
    uint8_t ascii[] = "hello world";
    check_norm(ascii, strlen((char *)ascii), ascii, strlen((char *)ascii));

    /* Hangul jamo L+V compose to the syllable block algorithmically
     * (U+1100 + U+1161 -> U+AC00, UTF-8 ea b0 80). */
    uint8_t jamo[] = {0xe1, 0x84, 0x80, 0xe1, 0x85, 0xa1}; /* U+1100, U+1161 */
    uint8_t syllable[] = {0xea, 0xb0, 0x80};                /* U+AC00 */
    check_norm(jamo, sizeof jamo, syllable, sizeof syllable);

    /* Empty input. */
    size_t out_len;
    uint8_t *out = nfc_normalize_utf8((const uint8_t *)"", 0, &out_len);
    CHECK(out_len == 0);
    free(out);
}
