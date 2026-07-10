# TypeScript adapter

Implements the `encode` and `decode-strict` modes (each with `--profile
rfc8949` and `--profile dcbor`) of the adapter contract (design spec §7),
ported from `adapters/kotlin` (itself ported from `adapters/rust`) to serve
as this project's third reference adapter (`ARCHITECTURE.md` §2/§9).

## No build step

There is no `dist/`, no bundler, no transpile-then-run pipeline. Node
(v22.6+/23+, the version already required by this project's `node --test
src/*.test.ts` unit tests) strips TypeScript types and runs `.ts` files
directly. `adapters/typescript/src/main.ts` carries a `#!/usr/bin/env node`
shebang and is executable (`chmod +x`), so the harness's
`[binary_path, mode, "--profile", profile]` argv contract
(`harness/adapters.json`'s `typescript` entry points straight at
`adapters/typescript/src/main.ts`) invokes it exactly like the Rust and
Kotlin binaries — no wrapper script needed.

The project uses ESM (`"type": "module"` in `package.json`) with explicit
`.ts` extensions on relative imports (`import { hexDecode } from
"./util.ts"`), matching `tsconfig.json`'s `NodeNext` module resolution.

## rfc8949 and dcbor profiles: no npm CBOR library

Like the Kotlin adapter, both `rfc8949.ts` (P1-P9) and `dcbor.ts` (D1-D9)
are from-scratch reimplementations against `SPEC.md`, not library wrappers.
No CBOR package on npm implements this project's exact pinned rule set as a
single "canonical" mode — specifically the NaN payload pin, `-0.0`
preservation for `rfc8949` vs. zero-unification for `dcbor`, and RFC 8949
§4.2.1's pure bytewise (no length pre-sort) map-key ordering. `dcbor.ts`
reuses `rfc8949.ts`'s `encodeHead`, `encodeInt`, `doubleToBeBytes`,
`compareBytesUnsigned`, and `bignumTagAndBytes` helpers directly.

Half-precision float conversion (`float16.ts`) is also hand-rolled: no
dependency-free JS/TS library performs f64<->f16 bit-level conversion, so
`f16BitsToDouble`/`doubleToF16Bits` implement the same round-to-nearest-even
algorithm as the Rust adapter's `half` crate and the Kotlin adapter's
`Float16.kt`.

## decode-strict mode

`decode.ts` is a from-scratch recursive-descent CBOR parser (not built on
any generic decoder, which would normalize away exactly the encoding-choice
metadata — additional-info width, raw NaN bit pattern, raw map-key byte
spans — that strict-decode rejection needs to inspect). It implements all
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

`UNKNOWN_TAG`'s allow-list (`ALLOWED_TAGS` in `decode.ts`) currently covers
only the bignum tags (2, 3), since that's all the vector corpus exercises
today — grow it if the corpus adds other tags.

**Known scope gap, replicated deliberately from the Rust and Kotlin
adapters:** a bignum (tag 2/3) whose magnitude fits the native 64-bit range
is not rejected by decode-strict, even though the encoder side requires it
to be a plain int. No documented reason code covers this case and no
vector exercises it; see `adapters/rust/src/decode.rs`'s and
`adapters/kotlin/src/main/kotlin/Decode.kt`'s top-of-file doc comments for
the original writeup of this gap.

## Building

```bash
cd adapters/typescript
npm install
```

## Testing

```bash
cd adapters/typescript
npm run typecheck
npm test          # node --test src/*.test.ts
```
