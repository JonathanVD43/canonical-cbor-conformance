#ifndef ADAPTER_DCBOR_H
#define ADAPTER_DCBOR_H

#include <stdbool.h>

#include "logical_value.h"
#include "util.h"

/* Encodes a logical value per the dcbor-profile-<draft-version> rules
 * (SPEC.md D1-D9): numeric reduction (D5), NaN pin (D6), zero unification
 * (D7), NFC string normalization (D8), and map-key-collision
 * last-write-wins dedup. D3 (map key sort) and the bignum rule reuse the
 * exact same code as rfc8949 (see cbor_common.c) -- no dcbor-specific
 * variant of either exists. */
bool encode_dcbor(const LogicalValue *value, ByteBuf *out);

#endif
