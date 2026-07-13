# Python adapter

Implements the `encode` and `decode-strict` modes (each with `--profile
rfc8949` and `--profile dcbor`) of the adapter contract (`CONTRIBUTING.md`),
ported from `adapters/go`/`adapters/kotlin` to serve as this project's fifth
reference adapter. Stdlib only -- no `pip install` beyond what's already in
the repo root's `requirements.txt` (which is for the harness/oracle, not an
adapter dependency).

## Layout

Flat module list, no package/`__init__.py`, mirroring the Go adapter's flat
`package main` structure -- each module has a `test_*.py` sibling in the
same directory:

- `logical_value.py` -- JSON stdin line -> internal `LogicalValue`
  dataclasses. Python's `json` module already parses JSON integers as
  arbitrary-precision `int`, so (unlike the Go/Kotlin/TypeScript/Rust ports)
  no special "preserve big numbers through a lossy intermediate" trick is
  needed for the `tag` field.
- `float16.py` -- hand-rolled IEEE-754 binary16 <-> binary64 bit-level
  conversion (Python's `struct` module has no f16 pack/unpack format).
- `rfc8949.py` -- P1-P9 encoder.
- `dcbor.py` -- D1-D9 encoder (numeric reduction, zero unification, NFC
  normalization, map-key-collision last-write-wins dedup). D8 uses Python's
  stdlib `unicodedata.normalize("NFC", ...)` directly -- unlike Go, which
  had to hand-roll NFC because Go's stdlib has no normalization API.
- `decode.py` -- from-scratch recursive-descent decode-strict parser
  implementing all 11 `SPEC.md` reason codes. Uses real exceptions
  (`DecodeSignal`) to unwind out of nested parse calls, rather than the
  per-call `if err != nil` propagation the Go port needs.
- `main.py` -- CLI entry point + batch stdin/stdout protocol. Has a
  `#!/usr/bin/env python3` shebang and is checked in with the executable
  bit set (`git ls-files -s` shows mode `100755`) so it's directly
  executable on Linux CI per `harness/adapters.json`'s
  `[binary_path, mode, "--profile", profile]` argv contract.
- `errors.py`, `util.py` -- shared exception types and hex helpers.

## Bignum rule

Python's native `int` is already arbitrary-precision, so the bignum
magnitude arithmetic needed no big-integer library the way the
Rust/Kotlin/TypeScript/Go ports did. The rule itself is unchanged: a
magnitude that fits the native 64-bit range (unsigned 0..2^64-1, negative
-1..-2^64) must reject as a bignum input -- it's only ever valid as a plain
`int` (`rfc8949.py`'s `bignum_tag_and_bytes`, reused by `dcbor.py`).

## Testing

```bash
cd adapters/python
python -m pytest -q
```

## Running directly

```bash
cd adapters/python
python main.py encode --profile rfc8949
python main.py decode-strict --profile dcbor
```

On Linux/macOS with the executable bit set, `./main.py encode --profile
rfc8949` also works directly via the shebang -- this is how the harness
invokes it (`harness/adapters.json`'s `python` entry points at
`adapters/python/main.py` with no interpreter wrapper).

**Known Windows dev-box limitation** (same category as the Kotlin/TypeScript
launcher scripts, unrelated to this adapter specifically): Windows'
`CreateProcess` cannot directly execute a `.py` text file via shebang the
way Linux can -- unlike Go's compiled binary, which is a real PE executable
Windows can run directly regardless of extension. Invoke via `python
main.py ...` for local testing on Windows; CI's `ubuntu-latest` runs it
unmodified per the harness contract.

## Known scope gap

Replicated deliberately from the Rust/Kotlin/TypeScript/Go adapters: a
bignum (tag 2/3) whose magnitude fits the native 64-bit range is not
rejected by `decode-strict`, even though the encoder side requires it to be
a plain int. No documented reason code covers this case and no vector
exercises it.
