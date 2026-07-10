from pathlib import Path

SPEC = (Path(__file__).parent.parent / "SPEC.md").read_text()

RULE_IDS = [f"P{i}" for i in range(1, 10)] + [f"D{i}" for i in range(1, 10)]

REASON_CODES = [
    "NON_SHORTEST_INT",
    "UNSORTED_MAP_KEYS",
    "INDEFINITE_LENGTH",
    "DUPLICATE_KEY",
    "NON_SHORTEST_FLOAT",
    "NAN_PAYLOAD_VARIANT",
    "TRAILING_BYTES",
    "MULTIPLE_TOP_LEVEL_ITEMS",
    "UNKNOWN_TAG",
    "NON_NFC_STRING",
    "UNREDUCED_NUMERIC",
]


def test_all_rule_ids_present():
    for rule_id in RULE_IDS:
        assert rule_id in SPEC, f"missing rule id {rule_id}"


def test_all_reason_codes_present():
    for code in REASON_CODES:
        assert code in SPEC, f"missing reason code {code}"


def test_bignum_rule_pinned():
    assert "2^64" in SPEC or "2**64" in SPEC
