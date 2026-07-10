# Canonical CBOR Conformance Suite

[![Conformance](https://github.com/JonathanVD43/canonical-cbor-conformance/actions/workflows/conformance.yml/badge.svg)](https://github.com/JonathanVD43/canonical-cbor-conformance/actions/workflows/conformance.yml)

If two or more independently-written implementations sign or hash a
serialized structure, they share a hidden requirement: **the encoders must
agree, byte-for-byte, on every possible input** — not just the happy-path
cases someone thought to test. CBOR (RFC 8949) defines a *canonical*
encoding profile meant to make this tractable, but in practice:

- Many CBOR libraries implement deterministic/canonical encoding only as
  an opt-in mode, and differ in edge-case behavior: integer boundary
  values, float shortest-form selection, map-key sort order,
  indefinite-length collapsing, duplicate-key handling, NaN payload
  normalization.
- Few CBOR libraries provide a strict validation mode that **rejects**
  well-formed-but-non-deterministic input instead of silently accepting
  and re-normalizing it. (RFC 8949 defines deterministic *encoding*; it
  does not itself require decoders to reject non-deterministic input —
  the strict-reject behavior this project tests is a project-level
  convention, not an RFC requirement.) This is the more dangerous gap: in
  systems that assume a unique serialized representation for a given
  logical value, a decoder that silently accepts an alternate valid
  encoding can create ambiguity — signature-verification bypass or
  cache/dedup-key confusion, depending on what the consuming protocol
  does with the decoded value. The risk is protocol-dependent, not
  inherent to CBOR itself; some protocols (e.g. those that hash raw
  bytes directly) aren't affected the same way.

This project turns "do my two encoders agree, and does my decoder reject
everything that isn't canonical" into a repeatable, automated conformance
check: a versioned rule spec, a large cross-checked test vector corpus, a
language-agnostic harness, and two reference adapters (Rust, Kotlin/JVM)
proving the contract actually works end to end.

**This is not a CBOR library.** It doesn't ship an encoder/decoder for you
to depend on, and the Rust/Kotlin adapters aren't endorsed production
libraries — they're reference implementations *of the test harness
contract*, one layer removed from the library you'd actually use:

```
your CBOR library → thin adapter (implements the CLI contract)
                        ↓
                  conformance harness (harness/run.py)
                        ↓
                  frozen vector corpus (vectors/v1/)
```

## What it tests

Two pinned canonicalization profiles, both rooted in RFC 8949 §4.2
deterministic encoding:

- **`rfc8949`** — bare RFC 8949 §4.2.1 deterministic encoding, plus a
  handful of explicitly-flagged project pins: a canonical NaN payload
  (RFC 8949-mandated), the encoding preserving the IEEE distinction
  between `-0.0` and `0.0` (a pin on the encoding, not a semantic claim
  the RFC doesn't already make), and — a **project-specific strictness
  rule, not an RFC 8949 requirement** — unknown tags are rejected during
  strict decoding rather than passed through.
- **`dcbor`** — the stricter `draft-mcnally-deterministic-cbor` (Blockchain
  Commons) superset: adds numeric reduction, NaN/zero unification, and
  NFC string normalization on top of the RFC 8949 base.

Every rule in both profiles (see `SPEC.md`) has: a hand-written vector file
targeting its known-hard edge cases, a fuzz-generated corpus of thousands
of additional cases cross-checked against an independent oracle (a
separate Python/Ruby implementation in `oracle/`, not the Rust/Kotlin
adapters under test), and — for every non-canonical-but-well-formed case —
a `strict-decode-reject/` vector the decoder side must reject with a
specific reason code.

## Repository layout

```
SPEC.md                # the pinned profile rules, full prose + rationale
ARCHITECTURE.md         # design rationale, adapter contract, corpus generation
vectors/v1/             # the frozen, versioned test vector corpus
oracle/                 # independent reference encoder, used only to author/check vectors
adapters/
  rust/                 # reference adapter #1
  kotlin/                # reference adapter #2 (JVM, no Android dependency)
harness/
  run.py                 # language-agnostic runner: feeds vectors to each adapter, diffs output
  adapters.json          # registered adapters + their binary paths
.github/workflows/
  conformance.yml         # CI: builds both adapters, runs the harness, fails on any mismatch
```

## Running the harness

```bash
pip install -r requirements.txt

# build the adapters under test
(cd adapters/rust && cargo build --release)
(cd adapters/kotlin && ./gradlew installDist)

python3 harness/run.py
```

Exit code is nonzero if any registered adapter fails any vector. A detailed
per-rule pass/fail report is written to `harness/report/latest.json`.

## Adding another language

See `CONTRIBUTING.md` — the adapter contract is a small, fixed CLI protocol
designed so a new language implementation is a translation exercise
against the frozen corpus, not an integration project.

## Status

Both reference adapters (Rust, Kotlin/JVM) pass 100% of the corpus across
both profiles and both modes (`encode`, `decode-strict`), checked on every
push/PR in CI (`.github/workflows/conformance.yml`).

## License

BSD-2-Clause-Patent — see `LICENSE`. Chosen to match the license of the
`dcbor` crate (Blockchain Commons), which the Rust adapter and oracle
depend on and which the `dcbor` profile's rules are modeled after.
