# Rust adapter

Implements the `encode` and `decode-strict` modes (each with `--profile
rfc8949` and `--profile dcbor`) of the adapter contract (design spec §7).

## rfc8949 profile: no external CBOR crate

This adapter's `rfc8949` encoder (`src/rfc8949.rs`) is a from-scratch
implementation, not a wrapper around an existing Rust CBOR crate. No
crates.io CBOR crate surveyed at the time of writing exposes this project's
exact pinned rule set as a single "canonical" mode — specifically the P6
NaN payload pin, the P7 `-0.0`-preservation pin, and RFC 8949 SS4.2.1's pure
bytewise (no length pre-sort) map-key ordering, which most "canonical CBOR"
implementations get wrong by defaulting to RFC 7049's length-first order
instead. `half` (crates.io, v2.4) is used only for IEEE-754 half-precision
float conversion, not CBOR framing.

This mirrors `oracle/rfc8949_oracle.py`'s approach: that oracle also needed
a hand-written float-pinning patch layer on top of `cbor2`, because no
existing library's canonical mode alone implements every one of this
project's pins. The expected bytes for every hand-written vector in
`vectors/v1/cbor/rfc8949/hand-written/` were authored via that oracle and
are the ground truth this adapter is tested against
(`tests/test_harness_rust_integration.py`, plus this crate's own
`cargo test`).

## dcbor profile: wraps the `dcbor` crate

Unlike `rfc8949`, this profile (`src/dcbor.rs`) wraps the existing `dcbor`
crate (Blockchain Commons, crates.io, v0.25.2, BSD-2-Clause-Patent) instead
of reimplementing deterministic-encoding rules from scratch, per the design
spec's §7 "reuse is legitimate" note. `LogicalValue` is converted into the
crate's `CBOR` type via its `From` impls, and `CBOR::to_cbor_data()` is the
sole encoding step.

A throwaway spike (see the Task 9 note in
`docs/superpowers/plans/2026-07-09-harness-rust-rfc8949.md`'s follow-on
milestone) confirmed no patch layer is needed: `to_cbor_data()` already
gives D5 (numeric reduction — whole floats collapse to shortest int form),
D6 (NaN's canonical f16 payload, same bit pattern as `rfc8949`'s P6), D7
(zero unification — int `0`, float `0.0`, and float `-0.0` all collapse to
the same encoding, the opposite convention from `rfc8949`'s P7), and D8
(NFC Unicode normalization for text strings) out of the box.

Bignums have no dcbor-specific rule, so `dcbor.rs` reuses `rfc8949.rs`'s
validated `bignum_tag_and_bytes` helper (native-range rejection plus
shortest-form magnitude bytes) rather than pulling in the crate's optional
`num-bigint` feature.

## decode-strict mode

`src/decode.rs` is a from-scratch recursive-descent CBOR parser (not built
on the `dcbor` crate's own decoder, which normalizes away exactly the
encoding-choice metadata — additional-info width, raw NaN bit pattern, raw
map-key byte spans — that strict-decode rejection needs to inspect). It
implements all 11 reason codes from the design spec: `NON_SHORTEST_INT`,
`UNSORTED_MAP_KEYS`, `INDEFINITE_LENGTH`, `DUPLICATE_KEY`,
`NON_SHORTEST_FLOAT`, `NAN_PAYLOAD_VARIANT`, `TRAILING_BYTES`,
`MULTIPLE_TOP_LEVEL_ITEMS`, `UNKNOWN_TAG` (both profiles), plus
`NON_NFC_STRING` and `UNREDUCED_NUMERIC` (dcbor-only, since rfc8949 has no
NFC or numeric-reduction rule). The parser returns the first, most specific
violation found during descent rather than enumerating every violation,
matching the vector corpus's singular `expected_reason` contract.

Each stdin line is one hex-encoded raw CBOR byte string; each stdout line
is `ACCEPT <hex>` (the re-encoded canonical form, which must be
byte-identical to the input) or `REJECT <REASON_CODE>`.

`UNKNOWN_TAG`'s allow-list (`ALLOWED_TAGS` in `decode.rs`) currently covers
only the bignum tags (2, 3), since that's all the vector corpus exercises
today — grow it if the corpus adds other tags.

**Known scope gap:** a bignum (tag 2/3) whose magnitude fits the native
64-bit range is not rejected by decode-strict, even though the encoder side
requires it to be a plain int. No documented reason code covers this case
and no vector exercises it; see the doc comment at the top of `decode.rs`.

## Fuzz-generated vector corpus

`fuzz/` is a cargo-fuzz project (`adapter-fuzz` crate) that supplements the
hand-written vectors with 5,000 oracle-validated vectors per profile under
`vectors/v1/cbor/{rfc8949,dcbor}/fuzz-generated/`. See
`vectors/v1/generation-report.md` for full methodology and stats.

cargo-fuzz (libFuzzer + the `arbitrary` crate) is used only as a
**coverage-guided input selector** against this crate's own encoders
(`SanitizerCoverage`-instrumented `adapter::rfc8949::encode` /
`adapter::dcbor::encode`) — the encoder is never the grader. Every committed
vector's `expected_hex` comes from the independent oracle
(`oracle/rfc8949_oracle.py`, `oracle/dcbor_oracle_batch.rb`), cross-checked
against this adapter's own output as a consistency filter (candidates the
adapter rejects, or where its output disagrees with the oracle, are excluded
and logged in the generation report rather than committed).

To regenerate:

```bash
# 1. Build the shared generator + libFuzzer targets (requires nightly).
cd adapters/rust/fuzz
cargo +nightly fuzz run rfc8949 -- -max_total_time=180
cargo +nightly fuzz run dcbor -- -max_total_time=180

# 2. Build the plain replay tool (stable toolchain).
cargo build --release --bin replay

# 3. Convert the raw corpus through the independent oracle (from repo root).
cd ../../..
source .venv/bin/activate
python3 scripts/generate-fuzz-corpus.py
```

`vectors/v1/cbor/{rfc8949,dcbor}/fuzz-generated/seed-corpus/` holds the
coverage-guided corpus files (not the random padding) for reproducibility.

Two discovered adapter/oracle disagreement classes are excluded from the
corpus rather than silently papered over — see "Adapter/oracle mismatches"
in `vectors/v1/generation-report.md`:
- rfc8949 maps with duplicate keys: the oracle's `dict()`-based conversion
  silently collapses duplicates (last write wins) while this adapter emits
  every entry as-is — a genuine grammar-handling divergence, not yet
  resolved.
- Negative integers/bignums at exactly magnitude 2^64: native CBOR major
  type 1 can represent this magnitude directly, but this adapter's
  bignum-boundary logic always routes it through the tag-3 bignum path.

Fuzz-generated vectors are intentionally **not** wired into decode-strict's
ACCEPT-side checks: the generator explores arbitrary tag numbers to exercise
the encoders' unconstrained tag handling, which is wider than decode-strict's
allow-listed tag set (`ALLOWED_TAGS = [2, 3]` above).

## Building

```bash
cargo build --release --manifest-path adapters/rust/Cargo.toml
```

## Testing

```bash
cd adapters/rust && cargo test
```
