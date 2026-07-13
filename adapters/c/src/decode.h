#ifndef ADAPTER_DECODE_H
#define ADAPTER_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util.h"

/* Strict canonical-CBOR decoder (SPEC.md's `decode-strict` mode). Parses
 * raw CBOR bytes into a byte-range-aware intermediate form and rejects any
 * well-formed-but-non-canonical input with the single most specific
 * decode-strict reason code (P1-P9 / D1-D9). Hand-rolled (not delegated to
 * a generic CBOR library) for the same reason every other adapter's port
 * hand-rolls it: a generic decode normalizes away exactly the information
 * needed to tell *why* something is non-canonical (which additional-info
 * width was used, raw NaN payload bits, raw map-key byte order). Ported
 * from adapters/go/decode.go.
 *
 * Known scope gap, matching every other reference adapter: a tag-2/3
 * (bignum) item whose magnitude fits the native 64-bit range is not
 * rejected here -- none of the 11 documented decode-strict reason codes
 * covers it and no vector in the corpus exercises it. */

typedef enum { PROFILE_RFC8949, PROFILE_DCBOR } Profile;

typedef struct {
    bool accept;
    ByteBuf bytes;    /* valid iff accept; caller must bytebuf_free */
    const char *reason; /* valid iff !accept; a static string, not owned */
} Verdict;

/* Decodes a single hex-decoded input line. Returns true with *verdict
 * populated on a determinate outcome (accept or reject). Returns false
 * (verdict undefined) only on a genuine internal failure that isn't
 * covered by any reason code (e.g. truly malformed/empty input) --
 * callers should treat that like the other adapters do: print a
 * diagnostic and emit an empty output line, not crash the batch loop. */
bool decode_strict(const uint8_t *input, size_t len, Profile profile, Verdict *verdict);

#endif
