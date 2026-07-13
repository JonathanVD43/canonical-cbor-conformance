#ifndef ADAPTER_CBOR_COMMON_H
#define ADAPTER_CBOR_COMMON_H

#include <stdbool.h>
#include <stdint.h>

#include "util.h"

/* Encoding primitives shared verbatim between the rfc8949 and dcbor
 * profiles -- P1/D2's shortest-int-argument rule, the bignum rule (SPEC.md,
 * identical in both profiles), and float-literal parsing are not
 * profile-specific, so both profile encoders call into this module rather
 * than duplicating it (mirrors adapters/go/rfc8949.go's role as the shared
 * base that dcbor.go calls into). */

/* Writes a CBOR item head (major type 0-7, argument arg) using the
 * shortest encoding for arg, per P1/D2. */
void cbor_encode_head(int major_type, uint64_t arg, ByteBuf *out);

/* Encodes a decimal-string integer literal (may be arbitrarily long text,
 * schema pattern ^-?[0-9]+$) as a plain CBOR integer (major type 0 or 1).
 * Returns false (and sets adapter_last_error) if the magnitude doesn't fit
 * the native 64-bit range or the literal is malformed. */
bool cbor_encode_int(const char *value, ByteBuf *out);

/* Parses a logical-value float literal ("2.5", "NaN", "Infinity",
 * "-Infinity", "-0.0", ...) into a double. Returns false on a malformed
 * literal. */
bool cbor_parse_float_literal(const char *literal, double *out);

void cbor_double_to_be_bytes(double f, uint8_t out[8]);

/* Shared bignum tag+magnitude computation (SPEC.md's bignum rule -- see
 * bignum128.h for how the arbitrary-magnitude-without-arbitrary-precision
 * problem is resolved). Returns false if magnitude fits the native 64-bit
 * range (must be a plain int instead) or the input is malformed. */
bool cbor_bignum_tag_and_bytes(const char *sign, const char *value, int *tag_out, ByteBuf *bytes_out);

/* Encodes a bignum logical value directly to tag+bytestring CBOR bytes. */
bool cbor_encode_bignum(const char *sign, const char *value, ByteBuf *out);

#endif
