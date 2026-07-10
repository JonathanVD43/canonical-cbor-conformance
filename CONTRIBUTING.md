# Contributing

## Adding a third language adapter

An adapter is a single CLI binary that implements two modes against a
fixed stdin/stdout batch protocol. There is no FFI, no shared build system,
no requirement that your adapter run on the same machine as the existing
ones, and no language-specific integration required — the harness only
ever talks to it as a subprocess.

An adapter may wrap an existing CBOR library or implement encoding/decoding
directly; the harness only observes the CLI's stdin/stdout behavior, not
what's underneath it.

Use `adapters/rust/` and `adapters/kotlin/` as reference implementations —
they demonstrate one way to satisfy the contract below, not the only way.
Every rule referenced below is defined, with rationale, in `SPEC.md`.

### 1. Invocation

```
<your-binary> encode --profile <rfc8949|dcbor>
<your-binary> decode-strict --profile <rfc8949|dcbor>
```

Exit codes on bad arguments (verified by each adapter's own test suite,
not by the conformance harness):
- `2` — missing mode, missing `--profile`, or an internal I/O failure
  (failed stdin read / stdout write)
- `3` — unrecognized `--profile` value

### 2. `encode` mode

**stdin:** one JSON object per line, each a "logical value" in this
project's Neutral Logical Value Grammar (schema: `logical-value.schema.json`
at the repo root). Example lines your adapter must handle:

```json
{"type": "int", "value": "0"}
{"type": "float", "width": "auto", "value": "2.5"}
{"type": "float", "width": "auto", "value": "NaN"}
{"type": "float", "width": "auto", "value": "-0.0"}
{"type": "text", "value": "café"}
{"type": "bytes", "value": "deadbeef"}
{"type": "bool", "value": true}
{"type": "null"}
{"type": "array", "items": [{"type": "int", "value": "1"}]}
{"type": "map", "entries": [[{"type": "text", "value": "a"}, {"type": "int", "value": "1"}]]}
{"type": "tag", "tag": 100, "value": {"type": "int", "value": "5"}}
{"type": "bignum", "sign": "positive", "value": "18446744073709551616"}
```

Notes that trip up a naive port:
- `int.value`, `float.value`, and `bignum.value` are **decimal strings**,
  not native JSON numbers — magnitudes go well outside a native 64-bit
  signed integer's range (bignums by definition; tag values up to
  2^64-1). Do not parse these through a native floating-point or
  fixed-width integer type; use an arbitrary-precision integer type where
  appropriate.
- `tag.tag` is a **JSON number** in `[0, 2^64-1]` — this exceeds a signed
  64-bit type's range in the upper half. If your language's JSON library
  parses large integer literals as an arbitrary-precision type (as
  `org.json` does via `BigInteger`, and `serde_json` does via `as_u64()`),
  make sure your tag field's runtime type actually preserves that,
  rather than narrowing through a signed 64-bit intermediate.
- `float.width` is one of `"auto"`, `"f16"`, `"f32"`, `"f64"`. `"auto"`
  means "pick the shortest width that round-trips exactly" (P5/D2) — i.e.
  decoding the candidate encoding must yield the exact same logical value
  back, not merely something IEEE-close to it; an explicit width is a
  request to encode at that exact width, failing if the value can't
  round-trip there.

**stdout:** one line per input line, in order:
- success: the hex-encoded bytes in canonical form for the selected
  profile
- failure (e.g. a bignum magnitude that fits the native int range, or an
  explicit float width that can't round-trip): **exactly one empty line**,
  so line-alignment with the input is preserved; a diagnostic goes to
  stderr, not stdout

**Bignum rule (both profiles):** a magnitude that fits the native 64-bit
range (unsigned 0..2^64-1, negative -1..-2^64) must reject as a bignum
input — it's only ever valid as a plain `int`. See `SPEC.md`'s "Bignum
rule" section for the exact tag-2/3 encoding once a magnitude is ≥ 2^64.

**Profile divergence you must implement, not share:**
- **Map key sort** (P2/D3): both profiles sort entries by their own
  canonical-encoded key bytes, bytewise lexicographic — *not* by logical
  value, and *not* length-first (RFC 7049's older rule, a common library
  default to watch for).
- **Map key collisions**: `dcbor`'s numeric reduction (D5), zero
  unification (D7), and NFC normalization (D8) mean two distinct logical
  map entries can canonicalize to identical encoded key bytes (e.g.
  `0`, `0.0`, and `-0.0` all encode as `0x00`). When multiple logical keys
  collapse to identical encoded key bytes, keep the value from whichever
  entry appears last in the *input* — "last write wins" — not the one
  with the numerically/lexically greatest key. E.g. for input entries in
  the order `0.0`, `0`, the collapsed key `0` keeps `0`'s value, because
  it came last; reordering the input to `0`, `0.0` flips which value
  wins. Don't sort or otherwise reorder entries before collapsing.
  **`rfc8949` has no such rule** (no D5/D7/D8 equivalent, and P9
  explicitly rules out string normalization) — a literal duplicate key
  there passes through as two separate encoded entries; do not add dedup
  logic to that profile.
- NaN payload (P6/D6): both pin to the single canonical half-precision
  payload `0xf97e00`, regardless of requested width.
- `-0.0` (P7 vs D7): `rfc8949` preserves it distinct from `0.0`; `dcbor`
  unifies both to the plain integer `0x00`. Opposite conventions — don't
  share this code path between profiles.

### 3. `decode-strict` mode

Note: RFC 8949 defines deterministic *encoding*; it does not itself
mandate that decoders reject non-deterministic input. `decode-strict`'s
reject behavior is a project-level convention adopted to harden protocols
against re-encoding ambiguity (see the README's security rationale) — your
adapter must implement it exactly as specified below, even though it goes
beyond what RFC 8949 requires.

**stdin:** one hex-encoded raw CBOR byte string per line — a mix of
canonical-corpus vectors (must be accepted) and
`strict-decode-reject/` vectors (must be rejected).

**stdout:** one line per input line:
- `ACCEPT <hex>` — the byte string was already in canonical form; `<hex>`
  is your re-encoding of the decoded value, which the harness checks is
  **byte-identical to the original input**. `<hex>` MUST be lowercase
  hexadecimal with no whitespace.
- `REJECT <REASON_CODE>` — one of the symbolic reason codes defined in
  `SPEC.md` (e.g. `NON_SHORTEST_INT`, `UNSORTED_MAP_KEYS`,
  `UNKNOWN_TAG`, `NON_NFC_STRING`/`UNREDUCED_NUMERIC` for dcbor only).
  Treat `SPEC.md` as the authoritative list — it may grow without a
  corresponding CONTRIBUTING.md update.

Return the **first, most specific violation found during descent** — the
vector corpus's `expected_reason` is singular, not a list, so don't try to
enumerate every violation in a malformed input. (This also keeps the
harness's pass/fail comparison deterministic: one vector, one expected
reason.)

**Unknown tags:** decode-strict allow-lists specific tags per profile (at
the time of writing, just the bignum tags 2 and 3 — see each adapter's
`ALLOWED_TAGS`/equivalent). Tags 2/3 are allow-listed specifically because
they're the bignum tags this project's grammar defines; **every other tag
number is `UNKNOWN_TAG`, regardless of whether it's a well-known tag
elsewhere in the CBOR ecosystem** (e.g. tag 1, epoch-based date/time, is
still rejected here). Grow the allow-list only if the vector corpus adds
vectors that need it — check `vectors/v1/manifest.json` and the relevant
`strict-decode-reject/unknown-tag.json` file before assuming a wider set
is required.

### 4. Register your adapter

Add an entry to `harness/adapters.json`:

```json
{
  "name": "yourlang",
  "binary": "adapters/yourlang/path/to/built/binary",
  "profiles": ["rfc8949", "dcbor"]
}
```

`binary` is invoked directly as argv[0] (no shell, no interpreter wrapper)
with `[binary_path, mode, "--profile", profile]` — if your build produces
a script or launcher rather than a raw executable, that's fine as long as
it's directly executable and matches this argv contract (see
`adapters/kotlin/build.gradle.kts`'s `installDist` output for an example
of a non-JAR launcher script satisfying this).

### 5. Verify

```bash
pip install -r requirements.txt
python3 harness/run.py
```

Exit code 0 means every registered adapter passed every vector. A
per-rule breakdown (so you can see exactly which rule your port gets
wrong, not just an aggregate pass/fail) is written to
`harness/report/latest.json`.

Write unit tests alongside your adapter's source mirroring
`adapters/rust`'s and `adapters/kotlin`'s test suites — in particular the
regression cases that are easy to get right by accident and wrong under a
slightly different input: the map-key pure-bytewise sort trap (no
length pre-sort), `-0.0` distinctness in `rfc8949`, bignum
native-range rejection, and (for `dcbor`) the D5/D7/D8 numeric-reduction
and map-key-collision-dedup rules described above.

### 6. Wire into CI

Add a build step for your language to `.github/workflows/conformance.yml`
alongside the existing Rust and Kotlin steps, before the harness run step.

## General contribution notes

- Vector corpus changes (adding/editing files under `vectors/v1/`) are
  reviewed like any other change — this is a versioned corpus, not
  scratch data. `oracle/` exists only to help author and cross-check
  vectors during authoring; it is not itself a conformance bar (see
  `ARCHITECTURE.md` §7).
- Don't regenerate the fuzz-generated corpus as part of an unrelated PR —
  see `ARCHITECTURE.md` §11 on regeneration cadence.
- Profile rule changes (P6–P9, D-series) are a new profile version, not a
  patch — see `SPEC.md` and `ARCHITECTURE.md` §3.
