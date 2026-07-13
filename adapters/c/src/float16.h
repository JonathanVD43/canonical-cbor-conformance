#ifndef ADAPTER_FLOAT16_H
#define ADAPTER_FLOAT16_H

#include <stdbool.h>
#include <stdint.h>

/* Hand-rolled IEEE-754 binary16 (f16) <-> binary64 (f64) bit-level
 * conversion. C has no float16 type (and no stdlib support even if it did);
 * ported bit-for-bit from adapters/go/float16.go, itself cross-checked
 * against the Rust adapter's `half` crate and the Kotlin/TypeScript
 * adapters' hand-rolled Float16 modules. */

uint16_t double_to_f16_bits(double value);
double f16_bits_to_double(uint16_t bits);

/* True iff the two doubles have bit-identical representations (NaN never
 * equals NaN under raw-bits comparison -- callers must handle NaN before
 * reaching these helpers). */
bool same_bits(double a, double b);

/* Attempts a round-trip-exact f16/f32 encoding of a non-NaN double. On
 * success writes the 2 (f16) or 4 (f32) big-endian bytes into out and
 * returns true. */
bool try_f16(double f, uint8_t out[2]);
bool try_f32(double f, uint8_t out[4]);

#endif
