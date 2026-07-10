# Oracle

`rfc8949_oracle.py` wraps `cbor2` (PyPI) — see `requirements.txt` for the
pinned version.

`dcbor_oracle.py` / `dcbor_oracle.rb` wrap the `cbor-dcbor` Ruby gem (Roman
Gonzalez / cbor.io's Ruby implementation of Blockchain Commons' dCBOR
profile) — see `Gemfile.lock` for the pinned version. This library is
independent of all three of this project's v1 adapters (Rust, Kotlin,
TypeScript): it shares no code with any of them, which is why it's trusted
to author expected vector output for the `dcbor` profile.

Note: the gem's public API is `Object#to_dcbor` (not `DCBOR.encode`) and
`CBOR::Tagged.new(tag, value)` (from its `cbor` dependency, not
`DCBOR::Tagged`). The gem also does not perform Unicode NFC normalization of
text strings on its own, so `dcbor_oracle.rb` normalizes text values to NFC
before handing them to the gem, per the dCBOR spec's text string
requirement.

Neither oracle is a conformance judge. See SPEC.md and design spec §9 for
the oracle's actual role (authoring aid only).

## Setup

```bash
cd oracle && bundle install
```

Requires Ruby with the `cbor-dcbor` gem installed (see `Gemfile.lock` for
the exact pinned version).
