# C adapter

Implements the `encode` and `decode-strict` modes (each with `--profile
rfc8949` and `--profile dcbor`) of the adapter contract (`CONTRIBUTING.md`),
this project's seventh and final planned reference adapter -- and its
hardest, since C has no GC, no native arbitrary-precision integer type, and
no stdlib JSON parser. Stdlib only (`stdio.h`, `stdlib.h`, `string.h`,
`stdint.h`, `math.h`) plus one deliberate, documented GCC/Clang extension
(`__int128`, see below) -- no third-party C library is linked.

Built with a plain `Makefile` (`make adapter`), matching the "no shared
build system, no language-specific integration required" spirit of
CONTRIBUTING.md more directly than a CMake project would.

## The bignum question

SPEC.md's bignum rule needs two things: (a) reject a bignum-tagged (tag 2/3)
input whose magnitude fits the native 64-bit range -- it's only ever valid
as a plain int -- and (b) encode/decode magnitudes >= 2^64 via tag 2/3 using
the minimal big-endian byte-string encoding of the magnitude. Neither of
these needs *bignum arithmetic* -- CBOR's tag-2/3 encoding is just a
big-endian byte string, so the only operations needed are decimal-string
parsing, a magnitude comparison against 2^64, and byte-array extraction.

`bignum128.c` resolves this with GCC/Clang's `unsigned __int128` (a 128-bit
integer extension, not part of standard C) rather than a hand-rolled
arbitrary-precision math library: `u128_parse_decimal` parses an
overflow-checked decimal string into a `u128`, and `u128_to_minimal_be_bytes`
extracts its minimal big-endian byte representation directly. This mirrors
a precedent already in this repo, not a new choice invented for this
adapter: `adapters/rust/src/rfc8949.rs::bignum_tag_and_bytes` independently
parses bignum magnitudes into a native `u128` via `value.parse::<u128>()`,
not a `num_bigint`-style arbitrary-precision type. The widest bignum
magnitude anywhere in `vectors/v1/` is 39 decimal digits (~2^128), so a
128-bit integer has ample headroom over anything the corpus actually
exercises; a magnitude requiring more than 128 bits is rejected as a parse
error (matching Rust's `u128::from_str` failure mode), not silently
truncated or wrapped. `cbor_common.c`'s plain-int encoder (`cbor_encode_int`)
and `dcbor.c`'s D5/D7 numeric-reduction path reuse the same `u128` type for
the same reason -- native `uint64_t`/`int64_t` unambiguously represent the
native range (unlike JS, C has no int/bignum representation-ambiguity
problem to resolve), but the *offset-by-one negative-bignum-magnitude* and
*whole-number-float-to-integer* computations both need one bit of headroom
beyond `uint64_t` at the exact 2^64 boundary, which `u128` provides for
free.

## Hand-rolled JSON parsing

`json.c` is a from-scratch recursive-descent JSON parser, scoped to exactly
what `logical-value.schema.json` needs: objects, arrays, strings (with full
backslash-u escape and surrogate-pair support), booleans, null, and
non-negative integer-literal numbers for the `tag` field -- no
fractional/exponent number support, since nothing in this project's grammar
uses it. Every parsed string carries an explicit byte length (not
NUL-terminated-reliant), since a backslash-u escape can embed a real NUL
byte in adapter text values, and the fuzz-generated vector corpus does
contain raw control-character escapes. `logical_value.c` is a second pass
over the parsed JSON tree, mirroring `adapters/go/logicalvalue.go`'s
two-stage `any` -> `LogicalValue` conversion; the `tag` field (a JSON
number up to 2^64-1) is parsed through the same overflow-checked `u128`
decimal parser as bignum magnitudes, not narrowed through a signed 64-bit
intermediate.

## rfc8949 and dcbor profiles: no stdlib CBOR library

`rfc8949.c` (P1-P9) and `dcbor.c` (D1-D9) are from-scratch
reimplementations against `SPEC.md`. `cbor_common.c` holds the pieces both
profiles share verbatim: `cbor_encode_head` (shortest-argument encoding,
P1/D2), `cbor_encode_int`, `cbor_parse_float_literal`,
`cbor_bignum_tag_and_bytes`/`cbor_encode_bignum` (the bignum rule, identical
in both profiles), and `util.c`'s `compare_bytes_unsigned` (the bytewise
map-key sort, P2/D3). Map-key sort uses a hand-rolled stable merge sort
(`util.c`'s `stable_sort_indices`) rather than `qsort()`, which C's stdlib
explicitly does not guarantee to be stable -- relevant because rfc8949
allows literal duplicate keys through unmodified (no dedup), so tie order
among equal-key entries must be encounter-order-preserving, not whatever
`qsort`'s unspecified partitioning happens to produce.

`dcbor.c`'s map-key-collision dedup (D5/D7/D8 causing two distinct logical
keys to canonicalize to identical encoded key bytes) does a linear scan
against already-built entries rather than a hash map -- the vector corpus's
largest map is 6 entries, so the O(n^2) scan is simpler and just as fast in
practice; "last write wins" is preserved by keeping the first-seen entry's
*position* but always overwriting its *value* bytes on a later collision,
mirroring `adapters/go/dcbor.go`'s real-map-based dedup semantics.

Half-precision float conversion (`float16.c`) is hand-rolled bit
manipulation, ported from `adapters/go/float16.go`'s
`doubleToF16Bits`/`f16BitsToDouble`. **One correctness note distinct from
the Go port it was translated from**: `f16_bits_to_double`'s subnormal
(`exp == 0`, `man != 0`) branch had an off-by-2 exponent bug in the ported
source (the normalizing-shift loop's `expAdj` accumulator needs to start at
`+1`, not `-1`, or every subnormal f16 value round-trips to exactly 1/4 of
its correct magnitude). Caught by this adapter's own exhaustive
round-trip test (`tests/test_float16.c` round-trips all 65536 f16 bit
patterns through `f16_bits_to_double` -> `double_to_f16_bits` and back),
which the ported Go source apparently isn't exercised against by any
existing vector (subnormal f16 values don't appear to be in the current
corpus). Fixed here only -- per this ticket's scope, the existing Go
adapter (or any other adapter) was not touched; flagging it in case it's
worth a fix upstream.

## D8 (NFC normalization): hand-rolled, not a stdlib API

C has no Unicode normalization API at all (unlike Python's `unicodedata`,
Java's `java.text.Normalizer`, JS's `String.prototype.normalize`, or Rust's
`unicode-normalization` crate). `nfc.c` reimplements Unicode Standard Annex
#15's canonical decomposition + canonical ordering + canonical composition
algorithm directly against data tables generated by
`scripts/gen_nfc_tables.py` into `nfc_tables.c` (2061 decomposition entries,
922 combining-class entries, 941 verified composition pairs, ground-truthed
against Python's own `unicodedata.normalize('NFC', ...)` output the same
way `adapters/go/scripts/gen_nfc_tables.py` grounds its own generated
tables -- so the official composition-exclusion list doesn't need
hand-transcribing). Hangul syllables are handled algorithmically (the
standard arithmetic Hangul decomposition/composition rule) rather than via
an 11172-entry table. Tables are emitted sorted by codepoint (or by
`(cp1, cp2)` for composition pairs) so `nfc.c` can binary-search them
instead of a linear scan or a hash map.

## decode-strict mode

`decode.c` is a from-scratch recursive-descent CBOR parser building a
lightweight intermediate `Item` tree (bytes/text spans borrow directly from
the input buffer rather than copying, since the whole decode call's input
buffer outlives the parse), converted to a `LogicalValue` tree only for the
final re-encode-and-compare-bytes step on ACCEPT. It implements all 11
reason codes from `SPEC.md`: `NON_SHORTEST_INT`, `UNSORTED_MAP_KEYS`,
`INDEFINITE_LENGTH`, `DUPLICATE_KEY`, `NON_SHORTEST_FLOAT`,
`NAN_PAYLOAD_VARIANT`, `TRAILING_BYTES`, `MULTIPLE_TOP_LEVEL_ITEMS`,
`UNKNOWN_TAG` (both profiles), plus `NON_NFC_STRING` and `UNREDUCED_NUMERIC`
(dcbor-only). Per CONTRIBUTING.md, only the first, most specific violation
found during descent is returned. The `UNKNOWN_TAG` allow-list covers only
tags 2 and 3 (the bignum tags) -- every other tag, including well-known
ones elsewhere in the CBOR ecosystem (e.g. tag 1), is rejected. Matching
every other reference adapter's documented scope gap: a tag-2/3 item whose
magnitude fits the native 64-bit range is not specially rejected (no
reason code covers it, and no vector exercises it).

## Memory safety

Every allocation has a matching free path (`logical_value_free`,
`json_free`, `free_item`, `bytebuf_free`/`strbuf_free`); the CLI frees each
line's parsed/encoded structures before moving to the next line rather than
accumulating an arena for the whole batch. `make test-asan` builds and runs
the unit test suite under `-fsanitize=address,undefined`; `make
adapter-asan` builds the CLI binary itself under the same sanitizers (used
to run the full harness vector corpus, not just the unit tests, through
ASan/UBSan -- see CI).

## Tests

C has no built-in test framework; `tests/test_framework.h` hand-rolls a
minimal `CHECK`-macro-based one (each failing check prints a diagnostic and
keeps going, rather than aborting at the first failure like a bare
`assert()` would). `tests/test_main.c` drives one `run_*_tests(void)` per
module. Coverage mirrors the regression cases CONTRIBUTING.md calls out
explicitly: the map-key pure-bytewise-sort trap (`test_rfc8949.c`,
`test_dcbor.c` -- keys `32` and `-1`, where bytewise order and
length-first order disagree), `-0.0` distinctness in rfc8949 vs.
unification in dcbor (`test_rfc8949.c`, `test_dcbor.c`), bignum
native-range rejection at both the exact 2^64 boundary and negative-side
`-2^64` boundary (`test_cbor_common.c`), and dcbor's D5/D7/D8 rules
including order-dependent map-key-collision last-write-wins
(`test_dcbor.c`).

Run `make test` (plain) or `make test-asan` (sanitizer-enabled).
