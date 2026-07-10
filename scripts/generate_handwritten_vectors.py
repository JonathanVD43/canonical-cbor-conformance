"""The only script that writes into vectors/v1/cbor/**. Re-running it
regenerates identical files from the SOURCE_VECTORS definitions below --
idempotent by construction."""
import json
from pathlib import Path

from oracle.dcbor_oracle import encode_dcbor
from oracle.rfc8949_oracle import encode_rfc8949

ROOT = Path(__file__).parent.parent

RFC8949_INTEGERS = [
    ("int-zero", "P1", "zero is the base case for shortest-int encoding", {"type": "int", "value": "0"}),
    ("int-23", "P1", "largest value encodable in the 1-byte immediate form", {"type": "int", "value": "23"}),
    ("int-24", "P1", "smallest value requiring a 1-byte length extension", {"type": "int", "value": "24"}),
    ("int-255", "P1", "largest value fitting the 1-byte extension", {"type": "int", "value": "255"}),
    ("int-256", "P1", "smallest value requiring a 2-byte extension", {"type": "int", "value": "256"}),
    ("int-neg-1", "P1", "smallest-magnitude negative integer, major type 1 offset-by-one", {"type": "int", "value": "-1"}),
    ("int-min-i64", "P1", "most negative 64-bit integer boundary", {"type": "int", "value": "-9223372036854775808"}),
]

RFC8949_FLOATS = [
    ("float-nan", "P6", "NaN must use the single canonical f16 payload 0x7e00", {"type": "float", "width": "auto", "value": "NaN"}),
    ("float-neg-zero", "P7", "-0.0 must be preserved distinct from 0.0", {"type": "float", "width": "auto", "value": "-0.0"}),
    ("float-pos-zero", "P7", "0.0 is the counterpart -0.0 must stay distinct from", {"type": "float", "width": "auto", "value": "0.0"}),
    ("float-2-5-f16-exact", "P5", "2.5 round-trips exactly in f16, the shortest form", {"type": "float", "width": "auto", "value": "2.5"}),
]

RFC8949_MAPS = [
    (
        "map-int-keys-byte-sort",
        "P2",
        "keys 9 and 10 sort by their 1-byte encoded form (0x09 < 0x0a), not numeric value -- here they coincide, exercised further by mixed-type cases in a later milestone",
        {
            "type": "map",
            "entries": [
                [{"type": "int", "value": "9"}, {"type": "int", "value": "1"}],
                [{"type": "int", "value": "10"}, {"type": "int", "value": "2"}],
            ],
        },
    ),
]

RFC8949_BIGNUMS = [
    (
        "bignum-2-pow-64",
        "bignum-rule",
        "smallest magnitude that must use tag 2 rather than a plain integer, per SPEC.md's bignum rule",
        {"type": "bignum", "sign": "positive", "value": "18446744073709551616"},
    ),
]


def _build(entries, encode_fn):
    return [
        {
            "id": vid,
            "rule": rule,
            "rationale": rationale,
            "logical_value": lv,
            "expected_hex": encode_fn(lv).hex(),
        }
        for vid, rule, rationale, lv in entries
    ]


DCBOR_NUMERIC_REDUCTION = [
    ("float-whole-becomes-int", "D5", "2.0 has zero fractional part, must reduce to integer 2", {"type": "float", "width": "auto", "value": "2.0"}),
    ("float-fraction-stays-float", "D5", "2.5 has a fractional part, must stay a float in shortest form", {"type": "float", "width": "auto", "value": "2.5"}),
    ("float-nan-canonical", "D6", "NaN must use the single canonical f16 payload 0x7e00, same bit pattern as P6", {"type": "float", "width": "auto", "value": "NaN"}),
]

DCBOR_ZERO_UNIFICATION = [
    ("zero-as-int", "D7", "plain integer 0 is the baseline unification target", {"type": "int", "value": "0"}),
    ("zero-as-pos-float", "D7", "0.0 must unify with integer 0, unlike rfc8949's P7", {"type": "float", "width": "auto", "value": "0.0"}),
    ("zero-as-neg-float", "D7", "-0.0 must also unify with integer 0, the opposite of rfc8949's P7", {"type": "float", "width": "auto", "value": "-0.0"}),
]

DCBOR_NFC_NORMALIZATION = [
    (
        "nfc-combining-accent",
        "D8",
        "'cafe' + combining acute accent (U+0301) must normalize to precomposed 'café' (U+00E9)",
        {"type": "text", "value": "caf" + "e" + "́"},  # e (U+0065) + combining acute accent (U+0301), NOT precomposed
    ),
]


def _build_dcbor(entries):
    return _build(entries, encode_dcbor)


def _write_manifest():
    rfc8949_files = [
        "vectors/v1/cbor/rfc8949/hand-written/integers.json",
        "vectors/v1/cbor/rfc8949/hand-written/floats.json",
        "vectors/v1/cbor/rfc8949/hand-written/maps-key-order.json",
        "vectors/v1/cbor/rfc8949/hand-written/bignums.json",
    ]
    dcbor_files = [
        "vectors/v1/cbor/dcbor/hand-written/numeric-reduction.json",
        "vectors/v1/cbor/dcbor/hand-written/zero-unification.json",
        "vectors/v1/cbor/dcbor/hand-written/nfc-normalization.json",
    ]
    rfc8949_count = sum(len(json.loads((ROOT / f).read_text())) for f in rfc8949_files)
    dcbor_count = sum(len(json.loads((ROOT / f).read_text())) for f in dcbor_files)
    manifest = {
        "profile_versions": {
            "rfc8949": "rfc8949-profile-1",
            "dcbor": "dcbor-profile-draft-04",
        },
        "hand_written_files": {
            "rfc8949": rfc8949_files,
            "dcbor": dcbor_files,
        },
        "corpus_stats": {
            "rfc8949_hand_written_count": rfc8949_count,
            "dcbor_hand_written_count": dcbor_count,
        },
    }
    (ROOT / "vectors/v1/manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def main():
    out_dir = ROOT / "vectors/v1/cbor/rfc8949/hand-written"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "integers.json").write_text(json.dumps(_build(RFC8949_INTEGERS, encode_rfc8949), indent=2) + "\n")
    (out_dir / "floats.json").write_text(json.dumps(_build(RFC8949_FLOATS, encode_rfc8949), indent=2) + "\n")
    (out_dir / "maps-key-order.json").write_text(json.dumps(_build(RFC8949_MAPS, encode_rfc8949), indent=2) + "\n")
    (out_dir / "bignums.json").write_text(json.dumps(_build(RFC8949_BIGNUMS, encode_rfc8949), indent=2) + "\n")

    dcbor_out_dir = ROOT / "vectors/v1/cbor/dcbor/hand-written"
    dcbor_out_dir.mkdir(parents=True, exist_ok=True)
    (dcbor_out_dir / "numeric-reduction.json").write_text(json.dumps(_build_dcbor(DCBOR_NUMERIC_REDUCTION), indent=2) + "\n")
    (dcbor_out_dir / "zero-unification.json").write_text(json.dumps(_build_dcbor(DCBOR_ZERO_UNIFICATION), indent=2) + "\n")
    (dcbor_out_dir / "nfc-normalization.json").write_text(json.dumps(_build_dcbor(DCBOR_NFC_NORMALIZATION), indent=2) + "\n")

    _write_manifest()


if __name__ == "__main__":
    main()
