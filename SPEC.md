# Canonical CBOR Conformance — Specification

This document is the authoritative rule reference for both profiles this
project tests. "Canonical," in this project, means "conformant to one of
these two pinned profiles" — either bare RFC 8949 §4.2 deterministic
encoding, or the stricter dCBOR superset. "Deterministic" is used only when
quoting RFC 8949's own section title.

## Profile: `rfc8949-profile-1`

Base: RFC 8949 §4.2.1, "Core Deterministic Encoding Requirements."

| ID | Rule |
|---|---|
| P1 | Integers use shortest possible encoding |
| P2 | Map keys sorted by their own canonical-encoded byte string, bytewise lexicographic |
| P3 | No indefinite-length arrays, maps, byte strings, or text strings |
| P4 | No duplicate map keys |
| P5 | Floating point uses shortest of {f16, f32, f64} that round-trips exactly |
| P6 | NaN encoded as single canonical payload (f16 `0x7e00`) |
| P7 | `-0.0` preserved as distinct from `0.0` |
| P8 | Unknown tags are a hard decode error |
| P9 | No Unicode normalization of string content |

Rules P6, P7, P8 are project-pinned choices, not bare RFC requirements; P9 is
an explicit non-goal callout from RFC 8949 §4.2.1's own note.

## Profile: `dcbor-profile-<draft-version>`

Base: `draft-mcnally-deterministic-cbor` (Blockchain Commons).

| ID | Rule |
|---|---|
| D1 | Definite-length only |
| D2 | Preferred/shortest-form serialization for all types |
| D3 | Map keys in bytewise lexicographic order |
| D4 | No duplicate map keys |
| D5 | Numeric reduction: floats with zero fractional part in [-2^63, 2^64-1] convert to integers |
| D6 | All NaN values reduce to quiet NaN, half-precision encoding `0xf97e00` |
| D7 | Zero unification: `0`, `0.0`, `-0.0` all encode as integer `0x00` |
| D8 | Text strings normalized to Unicode NFC |
| D9 | Only `false`, `true`, `null`, and floating-point values permitted among major-type-7 simples |

## Bignum rule (both profiles)

Values whose magnitude fits within CBOR's native 64-bit integer range
(unsigned: 0 to 2^64-1; negative: -1 to -2^64) MUST be encoded as plain
integers (major type 0 or 1), never as a tag 2/3 bignum. Tag 2 (positive
bignum) / tag 3 (negative bignum) are reserved for magnitudes ≥ 2^64, and
MUST use the minimal big-endian byte-string encoding of the magnitude (no
leading zero byte). This resolves the ambiguity RFC 8949's and dCBOR's
"shortest possible encoding" language leaves open for bignums.

## `decode-strict` reason codes (both profiles)

A conformant `decode-strict` implementation rejects non-canonical but
well-formed CBOR with one of these reason codes:

`NON_SHORTEST_INT`, `UNSORTED_MAP_KEYS`, `INDEFINITE_LENGTH`,
`DUPLICATE_KEY`, `NON_SHORTEST_FLOAT`, `NAN_PAYLOAD_VARIANT`,
`TRAILING_BYTES`, `MULTIPLE_TOP_LEVEL_ITEMS`, `UNKNOWN_TAG`,
`NON_NFC_STRING`, `UNREDUCED_NUMERIC`.

## Logical-value grammar

See `logical-value.schema.json` at the repo root — the machine-checkable
grammar every vector file's logical values validate against.

## Relationship to dCBOR

This project does not reinvent dCBOR. `dcbor-profile-<draft-version>`
implements the published `draft-mcnally-deterministic-cbor` spec; this
project's contribution is the cross-language conformance harness, not a new
canonicalization profile.
