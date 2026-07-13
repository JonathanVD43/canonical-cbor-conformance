#ifndef ADAPTER_RFC8949_H
#define ADAPTER_RFC8949_H

#include <stdbool.h>

#include "logical_value.h"
#include "util.h"

/* Encodes a logical value per the rfc8949-profile-1 rules (SPEC.md P1-P9).
 * Returns false (and sets adapter_last_error) on any rejection (e.g. a
 * bignum magnitude that fits the native int range, or an explicit float
 * width that can't round-trip). */
bool encode_rfc8949(const LogicalValue *value, ByteBuf *out);

#endif
