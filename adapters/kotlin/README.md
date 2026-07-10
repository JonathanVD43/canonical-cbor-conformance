# Kotlin/JVM adapter

Implements the `encode` and `decode-strict` modes (each with `--profile
rfc8949` and `--profile dcbor`) of the adapter contract (design spec §7),
ported from `adapters/rust` to serve as this project's second reference
adapter (`ARCHITECTURE.md` §2/§9).

## Build prerequisite: JDK 21

`build.gradle.kts` pins `kotlin { jvmToolchain(21) }`. Install a JDK 21
(e.g. `brew install openjdk@21`) and put it on `PATH`/`JAVA_HOME` before
running Gradle.

## rfc8949 and dcbor profiles: no JVM port of either reference library

The Rust adapter's `rfc8949` encoder is hand-written (no crate implements
this project's exact pinned rule set), and its `dcbor` encoder wraps the
`dcbor` crate (Blockchain Commons). **No JVM port of the `dcbor` crate
exists**, so unlike the Rust adapter, both `Rfc8949.kt` (P1-P9) and
`Dcbor.kt` (D1-D9) here are from-scratch reimplementations against
`SPEC.md`, not library wrappers. `Dcbor.kt` reuses `Rfc8949.kt`'s
`encodeInt`, `doubleToBeBytes`, `compareBytesUnsigned`, and
`bignumTagAndBytes` helpers directly (Kotlin has no `pub(crate)`-style
visibility, so these are exposed as ordinary top-level `fun`s rather than
`private`, annotated at each site noting they're intentionally shared).

Half-precision float conversion (`Float16.kt`) is also hand-rolled: no JVM
stdlib or dependency-free library performs f64<->f16 bit-level conversion,
so `doubleToF16Bits`/`f16BitsToDouble` implement the same round-to-nearest-
even algorithm as the Rust adapter's `half` crate dependency.

## decode-strict mode

`Decode.kt` is a from-scratch recursive-descent CBOR parser (not built on
any generic decoder, which would normalize away exactly the encoding-choice
metadata - additional-info width, raw NaN bit pattern, raw map-key byte
spans - that strict-decode rejection needs to inspect). It implements all
11 reason codes from the design spec: `NON_SHORTEST_INT`,
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

`UNKNOWN_TAG`'s allow-list (`ALLOWED_TAGS` in `Decode.kt`) currently covers
only the bignum tags (2, 3), since that's all the vector corpus exercises
today - grow it if the corpus adds other tags.

**Known scope gap, replicated deliberately from the Rust adapter:** a
bignum (tag 2/3) whose magnitude fits the native 64-bit range is not
rejected by decode-strict, even though the encoder side requires it to be a
plain int. No documented reason code covers this case and no vector
exercises it; see the doc comment at the top of `Decode.kt`.

## Building

```bash
cd adapters/kotlin
./gradlew installDist
```

Produces the launcher at `build/install/adapter/bin/adapter`, matching the
harness's `[binary_path, mode, "--profile", profile]` argv contract
(`harness/adapters.json`'s `kotlin` entry points here).

## Testing

```bash
cd adapters/kotlin
./gradlew test
```
