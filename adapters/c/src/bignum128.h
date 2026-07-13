#ifndef ADAPTER_BIGNUM128_H
#define ADAPTER_BIGNUM128_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util.h"

/* This adapter's answer to "how do you handle bignums in a language with no
 * arbitrary-precision integer type": CBOR's tag-2/3 bignum encoding is a
 * plain big-endian byte string, so no *arithmetic* library is needed --
 * only decimal-string-to-magnitude parsing, a magnitude comparison against
 * 2^64, and a byte-string extraction. GCC/Clang's `unsigned __int128`
 * (128 bits, exact, no floating-point rounding) is more than sufficient
 * range: the widest bignum magnitude anywhere in vectors/v1/ is 39 decimal
 * digits (~2^128), and the existing Rust reference adapter
 * (adapters/rust/src/rfc8949.rs::bignum_tag_and_bytes) independently made
 * the same choice, parsing bignum magnitudes into a native u128 rather than
 * a real arbitrary-precision type. This type mirrors that precedent rather
 * than reaching for a hand-rolled bignum math library that nothing in the
 * corpus exercises.
 *
 * A magnitude with more than 39 significant decimal digits (support only
 * up to 128 bits) is rejected as a parse error, same as Rust's u128::from_str
 * failing -- not silently truncated. */

typedef unsigned __int128 u128;

#define U128_MAX (~(u128)0)

/* 2^64 as a u128 constant. */
u128 u128_two_pow_64(void);

/* Parses an unsigned decimal digit string (no sign, no leading '+') into a
 * u128. Returns false on empty input, a non-digit character, or overflow
 * beyond 128 bits. */
bool u128_parse_decimal(const char *digits, size_t len, u128 *out);

/* Writes `v`'s big-endian byte representation, minimal length (no leading
 * zero byte, but at least one byte even for v == 0), into `out`. */
void u128_to_minimal_be_bytes(u128 v, ByteBuf *out);

/* Formats v as an unsigned decimal literal into buf (caller-provided,
 * must be at least 40 bytes -- u128's max value is 39 decimal digits plus
 * a NUL terminator). Used to reconstruct a decimal-string logical "int"
 * value from a decoded CBOR integer whose magnitude may exceed a native
 * 64-bit type's range only in the boundary case (arg == UINT64_MAX for a
 * negative integer's offset-by-one magnitude). */
void u128_to_decimal_string(u128 v, char *buf, size_t buf_size);

#endif
