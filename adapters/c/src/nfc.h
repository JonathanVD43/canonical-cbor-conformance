#ifndef ADAPTER_NFC_H
#define ADAPTER_NFC_H

#include <stddef.h>
#include <stdint.h>

/* Hand-rolled Unicode Normalization Form C (NFC), stdlib-only -- ported
 * algorithm-for-algorithm from adapters/go/nfc.go (itself Go's answer to
 * having no normalization API in its stdlib either). Operates on decoded
 * Unicode codepoints (uint32_t), not raw UTF-8 bytes.
 *
 * nfc_normalize_utf8 takes and returns UTF-8-encoded byte spans (matching
 * this project's text logical-value representation) and does the
 * UTF-8<->codepoint conversion internally. The returned buffer is
 * newly malloc'd (out_len bytes); caller frees it. */
uint8_t *nfc_normalize_utf8(const uint8_t *utf8, size_t len, size_t *out_len);

#endif
