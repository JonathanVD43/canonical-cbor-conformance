# Java adapter

Implements the `encode` and `decode-strict` modes (each with `--profile
rfc8949` and `--profile dcbor`) of the adapter contract
(`CONTRIBUTING.md`), as this project's sixth reference adapter and second
JVM-hosted one, deliberately built with a different approach than
`adapters/kotlin` rather than as a re-skin of it.

## Build prerequisite: JDK 21

`build.gradle.kts` pins a Java toolchain of 21 (via `java { toolchain {
languageVersion.set(JavaLanguageVersion.of(21)) } }`, Gradle's Java
equivalent of Kotlin's `jvmToolchain(21)`). Install a JDK 21 and put it on
`PATH`/`JAVA_HOME` before running Gradle.

## Library investigation: why this is still a hand-rolled CBOR engine

The ticket that produced this adapter asked for a real investigation of
whether an existing Java CBOR library could serve as the actual
encode/decode engine, not just a default to hand-rolling because Kotlin
already hand-rolled. Two candidates were checked against this project's
actual needs (SPEC.md's exact profile rules, not "CBOR" in general):

- **`com.upokecenter:cbor`** (peteroupc/CBOR-Java) is the most spec-complete
  CBOR library on the JVM and does ship a canonical-encoding mode
  (`CBOREncodeOptions.ctap2Canonical`/`DefaultCtap2Canonical`). But CTAP2
  canonical form sorts map keys **shortest-encoding-first, then
  bytewise** — exactly RFC 7049's older length-first rule that
  `CONTRIBUTING.md` calls out by name as the trap to avoid; both this
  project's `rfc8949-profile-1` (P2) and `dcbor` (D3) require pure bytewise
  lexicographic order with no length pre-sort. It also predates and has no
  concept of `draft-mcnally-deterministic-cbor` (no D5 numeric reduction, D7
  zero unification, or D8 NFC normalization), and its decoder surfaces
  malformed/non-canonical input as one generic `CBORException`, not the 11
  typed reason codes `decode-strict` must emit.
- **`jackson-dataformat-cbor`** is a generic Jackson streaming/tree-model
  binding for CBOR with no canonical/deterministic mode at all — using it
  would mean hand-rolling every one of P1-P9/D1-D9 on top of it anyway, for
  no reduction in the amount of hand-written canonicalization logic, plus an
  extra translation layer between Jackson's tree model and this project's
  `LogicalValue`.

Neither library cleanly supports the strict-reject granularity or the
exact non-length-first bytewise sort this project's two profiles need —
consistent with the Go and Python adapters' conclusions on the same
question (see `.scratch/vector-corpus-and-adapters/issues/02-go-adapter.md`
and `03-python-adapter.md`). So `Rfc8949.java` (P1-P9), `Dcbor.java`
(D1-D9), and `Decode.java`'s recursive-descent decode-strict parser are
hand-rolled here too, ported logic-for-logic from `adapters/kotlin`'s
Kotlin source (the closest available JVM ground truth, since both target
the same `java.math.BigInteger`/`BigDecimal` primitives) but written as
idiomatic modern Java: a `sealed interface LogicalValue` of `record`s (not
Kotlin's `sealed class` of `data class`es) exercised with Java 21 pattern
matching (`instanceof` patterns, pattern `switch`), rather than Kotlin
`when` expressions.

**Where this adapter *is* library-backed, unlike Kotlin's org.json use:**
the stdin JSON layer (`LogicalValue.parse`) uses Jackson's typed tree model
(`ObjectMapper`/`JsonNode`) with `USE_BIG_INTEGER_FOR_INTS` enabled, rather
than org.json's dynamic `JSONObject`/`JSONArray` — so a `tag` field up to
2^64-1 parses straight to `BigInteger` without a lossy `long` intermediate,
by construction rather than by a manual `Int`/`Long`/`BigInteger` type
check at each call site.

Half-precision float conversion (`Float16.java`) is hand-rolled for the
same reason as Kotlin's: no JVM stdlib or dependency-free library performs
f64<->f16 bit-level conversion.

## decode-strict mode

`Decode.java` is a from-scratch recursive-descent CBOR parser (not built on
a generic decoder, which would normalize away exactly the encoding-choice
metadata — additional-info width, raw NaN bit pattern, raw map-key byte
spans — that strict-decode rejection needs to inspect). It implements all
11 reason codes from `SPEC.md`: `NON_SHORTEST_INT`, `UNSORTED_MAP_KEYS`,
`INDEFINITE_LENGTH`, `DUPLICATE_KEY`, `NON_SHORTEST_FLOAT`,
`NAN_PAYLOAD_VARIANT`, `TRAILING_BYTES`, `MULTIPLE_TOP_LEVEL_ITEMS`,
`UNKNOWN_TAG` (both profiles), plus `NON_NFC_STRING` and
`UNREDUCED_NUMERIC` (dcbor-only). It returns the first, most specific
violation found during descent, matching the vector corpus's singular
`expected_reason` contract.

`UNKNOWN_TAG`'s allow-list (`ALLOWED_TAGS` in `Decode.java`) currently
covers only the bignum tags (2, 3), matching the other five adapters —
grow it only if the vector corpus adds vectors that need it.

**Known scope gap, replicated deliberately from every other adapter in this
repo:** a bignum (tag 2/3) whose magnitude fits the native 64-bit range is
not rejected by decode-strict, even though the encoder side requires it to
be a plain int. No documented reason code covers this case and no vector
exercises it; see the doc comment at the top of `Decode.java`.

Unsigned 64-bit argument/tag values in the decoder use Java's signed `long`
as a raw 64-bit bit pattern with `Long.compareUnsigned`/
`Long.toUnsignedString` (the standard JDK idiom, since Java — unlike
Kotlin's `ULong` — has no native unsigned integer type).

## Building

```bash
cd adapters/java
./gradlew installDist
```

Produces the launcher at `build/install/adapter/bin/adapter` (a POSIX shell
script on the `application` plugin's default layout, mirroring
`adapters/kotlin/build.gradle.kts`'s `installDist` output), matching the
harness's `[binary_path, mode, "--profile", profile]` argv contract
(`harness/adapters.json`'s `java` entry points here — note the distinct
binary path from `kotlin`'s entry).

## Testing

```bash
cd adapters/java
./gradlew test
```

80 JUnit 5 tests across `UtilTest`, `Float16Test`, `LogicalValueTest`,
`Rfc8949Test`, `DcborTest`, `DecodeTest`, covering the same regression
cases as `adapters/kotlin`'s suite: the map-key pure-bytewise-sort trap (no
length pre-sort), `-0.0` distinctness in rfc8949 vs. zero unification in
dcbor, the bignum native-range-rejection boundary, D5/D7/D8 numeric
reduction and map-key-collision dedup, an exhaustive subnormal-f16 decode
round-trip sweep, and the `tag` field surviving intact
at 2^64-1 without truncating through a signed 64-bit intermediate.
