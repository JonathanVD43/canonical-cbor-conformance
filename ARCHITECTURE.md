# Canonical CBOR Cross-Language Conformance Suite — Design & Architecture

## 0. Origin note

This project is a standalone spin-out of a narrow problem run into in a real system: a Rust core and a Kotlin (Android) client had to both produce and verify *byte-identical* canonical CBOR encodings of the same logical event, because signatures are computed over those bytes. No production code, domain content, or proprietary design from that original system is reused here — only the general problem.

This doc is self-contained.

## 1. The problem

Any system where two or more independently-written implementations sign or hash a serialized structure has a hidden shared-fate requirement: **the encoders must agree, byte-for-byte, on every possible input** — not just on the happy-path test cases someone thought to check. CBOR (RFC 8949) has a defined *canonical* encoding profile intended to make this tractable, but:

1. Most CBOR libraries implement canonical *encoding* as an opt-in mode, inconsistently, across a long tail of edge cases (integer boundary values, float shortest-form selection, map key sort order, indefinite-length collapsing, duplicate-key handling, NaN payload normalization).
2. Almost no CBOR libraries implement canonical *decoding* — i.e., a strict decoder that **rejects** well-formed-but-non-canonical input rather than silently accepting and re-normalizing it. This second gap is the more dangerous one: if a verifier's decoder accepts a non-canonical encoding of the same logical value, an attacker who can produce an alternate valid encoding may be able to construct signature-verification ambiguity or cache/dedup-key confusion, depending on what the consuming system does with the decoded value. This is a real, previously-documented class of malleability issue in canonical-serialization-over-signatures designs generally (not specific to CBOR).

Nobody should have to rediscover this by shipping the bug. This project exists to make "do my two encoders agree, and does my decoder correctly reject everything that isn't canonical" a solved, reusable, testable problem.

## 2. Goals / non-goals

**Goals**
- A precise, versioned, unambiguous canonicalization profile spec (built on RFC 8949 §4.2, "Deterministically Encoded CBOR" — not inventing a new profile from scratch).
- A large, versioned corpus of test vectors covering the profile's edge cases, generated both by hand (known-hard cases) and by property-based fuzzing, cross-checked against an independent reference implementation.
- A conformance harness that any language implementation can plug into via a small, fixed adapter contract, producing a pass/fail conformance report.
- **Strict-decode conformance as a first-class category**, not an afterthought: vectors that are valid CBOR but non-canonical, which a conformant decoder MUST reject.
- Two reference adapters shipped and CI-tested from day one: Rust and Kotlin/JVM (matching the motivating problem — a native core plus an Android client is a common pairing).
- Something a third party can adopt for their own two-(or more-)language signing system without reading any of this project's history.

**Non-goals**
- Not a general-purpose CBOR codec library. This project does not implement CBOR encoding/decoding for consumers to depend on directly — it *tests* existing codecs (or thin wrappers around them).
- Not a decoder-hardening/fuzzing project against adversarial malformed CBOR in general (buffer overflows, resource exhaustion, malformed length prefixes). That's a legitimate, different, well-covered problem (e.g., existing CBOR fuzz corpora) — this project only cares about the *canonical-vs-non-canonical* distinction on otherwise well-formed input.
- Not tied to any particular signing scheme, hash function, or wire protocol. Signing is the motivating use case; this project stops at "do the bytes match."
- No language bindings beyond Rust and Kotlin at v1. The adapter contract (§5) is designed so others can add a third language without touching the harness, but this project won't build/maintain them.

## 3. Canonicalization profile

Base: **RFC 8949 §4.2.1, "Core Deterministic Encoding Requirements."** This project does not invent new canonicalization rules; it pins a specific, versioned interpretation of the RFC profile (the RFC leaves a few application-defined choices) and tests conformance to that pinned interpretation.

Profile decisions (each numbered, each gets its own test vector category):

| # | Rule | RFC basis | Notes |
|---|---|---|---|
| P1 | Integers use the shortest possible encoding (major type 0/1, smallest additional-info form that represents the value) | §4.2.1 (1) | Includes the negative-integer offset-by-one boundary cases (`-1` vs `-2^64`) |
| P2 | Map keys sorted by their own canonical-encoded byte string, bytewise lexicographic | §4.2.1 (3) | The classic "sort by encoded form, not by logical value" trap — e.g. integer key `10` sorts before `9` under naive numeric comparison but the RFC requires byte-order comparison of the encoded key |
| P3 | No indefinite-length arrays, maps, byte strings, or text strings — all definite-length | §4.2.1 (2) | A canonical encoder must never emit the indefinite-length "break" byte (0xFF) |
| P4 | No duplicate map keys | §4.2.1 (implied by determinism) | A decoder must reject; an encoder must never emit |
| P5 | Floating point uses the shortest of {f16, f32, f64} that round-trips the value exactly, per RFC 8949 §4.2.2 | §4.2.2 | This is the single hardest rule to implement correctly — this project's vector corpus weights this rule heaviest |
| P6 | NaN is encoded as the single canonical NaN payload (f16 `0x7e00`), never a payload-preserving NaN | §4.2.2 (project-pinned choice; RFC allows either, this project picks the stricter one) | Explicitly flagged as a project-level pinning decision, not a bare RFC requirement, because RFC 8949 itself permits NaN payloads under some deterministic-encoding profiles |
| P7 | `-0.0` is preserved as distinct from `0.0` (not collapsed) | project-pinned choice | Also flagged as pinned, not RFC-mandated, because this affects semantics for any consumer treating floats as IEEE-754-exact |
| P8 | Tag usage: only tags explicitly listed in an implementation's schema are permitted; unknown tags are a hard decode error, not passed through | project-pinned choice | Motivated by the signing use case — silently accepting unknown tags is itself a malleability surface |
| P9 | Byte strings and text strings: no canonicalization *within* string content (no Unicode normalization) — canonical form applies only to the CBOR framing, not string payload interpretation | §4.2.1 note | Explicit non-goal callout so nobody assumes NFC/NFKC normalization is in scope |

Each pinned (non-bare-RFC) rule (P6, P7, P8) is documented with its rationale in `SPEC.md`, versioned independently, so an adopter who disagrees with a specific pin can fork the profile without forking the whole harness.

**Profile versioning:** `SPEC.md` carries a semver-independent `profile-version` (e.g. `ccbor-profile-1`). Vector corpus releases are tagged against a profile version. Changing a pinned rule (P6–P9) is a new profile version, not a patch — implementations conformant to `ccbor-profile-1` are not silently expected to track a rule change.

## 4. Repository layout

```
canonical-cbor-conformance/
  SPEC.md                     # the pinned profile, §3 above, expanded to full prose + rationale
  vectors/
    v1/
      manifest.json           # profile-version, vector-corpus-version, vector count, generation method per file
      hand-written/
        integers.json
        floats.json
        maps-key-order.json
        nested-structures.json
        tags.json
        strict-decode-reject/ # non-canonical-but-well-formed inputs a decoder MUST reject
          non-shortest-int.json
          unsorted-map-keys.json
          indefinite-length.json
          duplicate-keys.json
          non-shortest-float.json
          nan-payload-variant.json
      fuzz-generated/
        seed-corpus/           # committed seeds for reproducibility
        generation-report.md   # how these were produced, §6
  oracle/
    # independent reference encoder/decoder used ONLY to help author and
    # cross-check vectors during authoring — not itself "one of the two
    # implementations under test," and not shipped as part of conformance
    # claims. Python, using a well-established canonical-CBOR-capable
    # library, chosen specifically because it's a *third* implementation
    # lineage from Rust and Kotlin.
    oracle.py
  adapters/
    rust/
      # thin CLI: reads a vector's logical-value JSON description on
      # stdin, writes canonical CBOR bytes (or a decode-and-reject
      # verdict, for strict-decode vectors) to stdout
      src/main.rs
      Cargo.toml
    kotlin/
      # same contract, JVM CLI (buildable/runnable via Gradle, no
      # Android SDK dependency — Android-specific concerns are out of
      # scope for this project, JVM is the shared substrate)
      src/main/kotlin/Main.kt
      build.gradle.kts
  harness/
    # language-agnostic runner: iterates vectors/, invokes each
    # registered adapter as a subprocess per the contract in §5,
    # diffs output, produces a conformance report
    run.py
    report/
      schema.json
  .github/workflows/
    conformance.yml           # CI matrix: build both adapters, run harness, fail on any mismatch
  CONTRIBUTING.md              # how to add a third language adapter
  README.md
```

## 5. Adapter contract

Every language implementation under test exposes a single CLI binary conforming to this contract — deliberately minimal so adding a new language is a translation exercise, not an integration project.

**Invocation:** `<adapter-binary> encode` or `<adapter-binary> decode-strict`

**`encode` mode:**
- stdin: one JSON object per line, each describing a logical CBOR value in the project's neutral logical-value format (`{"type": "map", "entries": [...]}`, `{"type": "float", "value": ..., "bits": 64}`, etc. — full grammar in `SPEC.md` §2)
- stdout: one line per input line, hex-encoded canonical CBOR bytes
- exit code 0 if every line encoded successfully; nonzero + stderr detail on any encoder-internal failure (not a conformance failure by itself — the harness separately checks byte equality)

**`decode-strict` mode:**
- stdin: one hex-encoded CBOR byte string per line (these are the `strict-decode-reject/` vectors plus the canonical vectors, mixed)
- stdout: one line per input — either `ACCEPT <hex of re-encoded canonical form>` or `REJECT <reason>`
- A conformant implementation must `REJECT` every vector under `strict-decode-reject/` and `ACCEPT` every vector under the canonical corpus, with the accepted re-encoding byte-identical to the original input

**Why a subprocess/CLI contract instead of FFI/bindings:** keeps the harness itself language-agnostic and keeps each adapter's dependency footprint isolated (no requirement that the Rust adapter and Kotlin adapter share a build system, a process, or even run on the same machine in CI). Cost is subprocess overhead, irrelevant at this corpus size (thousands, not billions, of vectors).

## 6. Vector generation

Two sources, both required, kept in separate directories so provenance is always visible:

**Hand-written (`hand-written/`).** Deliberately targets known-hard cases from §3's rule table: integer boundary values (`2^63-1`, `2^64-1`, `-2^63`), the map-key-sort trap (P2), float shortest-form edge cases (subnormals, exactly-representable-in-f16-but-not-obviously-so values), and one full file per strict-decode-reject rule (§4's `strict-decode-reject/`). These are authored with an explanation comment per vector (in `manifest.json`, not inline in the vector file, to keep vector files themselves clean machine-readable JSON) — this corpus is the one a new contributor reads first to understand *why* each case is hard.

**Fuzz-generated (`fuzz-generated/`).** A property-based generator (Rust `proptest`, run once during corpus authoring, not per-CI-run) builds random logical-value trees — nested maps/arrays to a bounded depth, randomized integer/float/string/byte-string leaves — encodes each through the oracle (§7), and keeps the ones that exercise a code path the hand-written corpus doesn't (tracked via oracle-side coverage instrumentation during generation). Output is **committed**, not regenerated per CI run — reproducibility matters more than corpus freshness, and a wider corpus is a new vector-corpus-version (§3's versioning), reviewed like any other change.

## 7. The oracle's role (and its limits)

`oracle/oracle.py` exists for exactly two purposes: (a) helping a human author hand-written vectors without doing canonical-CBOR-by-hand arithmetic, and (b) filtering fuzz-generated candidates during corpus authoring. It is explicitly **not**:
- a third implementation whose agreement with Rust and Kotlin is itself the conformance bar (that would just be "three implementations must agree," which has the same blind-spot risk as two — an oracle bug shared by construction with one of the adapters wouldn't be caught)
- shipped or version-pinned as part of the conformance report

The actual conformance bar is: **every adapter's output for every vector matches the vector file's committed expected bytes exactly.** The expected bytes in committed vector files are the source of truth, authored once (oracle-assisted) and then frozen; adapters are graded against the frozen corpus, not against a live oracle run. This avoids the oracle becoming a silent third dependency of every CI run.

## 8. Harness & CI

`harness/run.py`:
1. Loads `vectors/v1/manifest.json`, resolves the full vector set.
2. For each registered adapter (initially: rust, kotlin), spawns the adapter process, feeds `encode`-mode input for the canonical corpus and `decode-strict`-mode input for the full corpus (canonical + reject vectors).
3. Diffs each adapter's output against the vector file's committed expected bytes / expected verdict.
4. Emits a report (`report/schema.json` shape) — per-adapter, per-rule-category (P1–P9) pass/fail counts, not just an aggregate pass/fail, so a contributor can see *which rule* an implementation gets wrong.
5. Nonzero exit if any mismatch — this is the CI gate.

`.github/workflows/conformance.yml`: matrix job builds the Rust adapter (`cargo build --release`) and the Kotlin adapter (`./gradlew build`, JVM only, no Android toolchain needed) on every push/PR, then runs the harness. A conformance badge in `README.md` reflects the latest main-branch run.

## 9. What "done" looks like (v1 definition of done)

- `SPEC.md` complete, all nine profile rules (§3) documented with rationale.
- Vector corpus: full hand-written coverage of P1–P9 including every `strict-decode-reject/` category, plus a fuzz-generated corpus of at least a few thousand vectors with documented generation provenance.
- Rust and Kotlin adapters both pass 100% of the corpus in CI.
- `CONTRIBUTING.md` describes the adapter contract precisely enough that a third-party language implementation could be added without asking a question.
- README states the problem (§1) in a way that's legible to someone who has never heard of this project's origin — this is the portfolio/OSS-facing document, written to stand alone.

## 10. Relationship to the originating problem

This project is deliberately decoupled: no shared repo, no shared build, no dependency in either direction with the original system that surfaced this problem. If that system later adopts this suite as a CI dependency, that's a downstream integration decision made in that repo, not something this repo's design assumes or accommodates specially.

## 11. Open questions (flag, don't silently resolve — same convention as the originating project)

- **P6 (NaN payload) and P7 (`-0.0`) are opinionated pins beyond bare RFC 8949.** Worth a second look once real adopters exist — an adopter with a different requirement here forks the profile version, but it's worth knowing if the pin is actually the right default before this gets wide adoption.
- **Third adapter language** — TypeScript/JS is the most likely next request (browser/Node signing verifiers are common); not scoped for v1, but the adapter contract (§5) should be sanity-checked against JS's lack of a native 64-bit integer type before assuming it'll be a clean fit.
- **Fuzz corpus regeneration cadence** — currently "committed once, grows via reviewed PRs." If coverage-guided fuzzing later finds a real gap, worth deciding whether that becomes a scheduled regeneration job or stays manual.
