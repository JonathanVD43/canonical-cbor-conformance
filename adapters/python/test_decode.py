import pytest

from decode import decode_strict


def accept(input_hex, profile):
    v = decode_strict(bytes.fromhex(input_hex), profile)
    assert v.accept, f"expected Accept, got Reject({v.reason})"
    return v.bytes_out.hex()


def reject(input_hex, profile):
    v = decode_strict(bytes.fromhex(input_hex), profile)
    assert not v.accept, f"expected Reject, got Accept({v.bytes_out.hex()})"
    return v.reason


def test_accepts_canonical_int_and_round_trips():
    assert accept("05", "rfc8949") == "05"
    assert accept("05", "dcbor") == "05"


def test_accepts_canonical_map_and_array():
    assert accept("820102", "rfc8949") == "820102"
    assert accept("a10102", "rfc8949") == "a10102"


def test_non_shortest_int():
    assert reject("1805", "rfc8949") == "NON_SHORTEST_INT"


def test_unsorted_map_keys():
    assert reject("a20a010902", "rfc8949") == "UNSORTED_MAP_KEYS"


def test_indefinite_length():
    assert reject("9fff", "rfc8949") == "INDEFINITE_LENGTH"


def test_duplicate_key():
    assert reject("a201010102", "rfc8949") == "DUPLICATE_KEY"


def test_non_shortest_float():
    assert reject("fa40200000", "rfc8949") == "NON_SHORTEST_FLOAT"
    assert reject("fa40200000", "dcbor") == "NON_SHORTEST_FLOAT"


def test_nan_payload_variant():
    assert reject("fa7fc00000", "rfc8949") == "NAN_PAYLOAD_VARIANT"


def test_trailing_bytes():
    assert reject("05ff", "rfc8949") == "TRAILING_BYTES"


def test_multiple_top_level_items():
    assert reject("0506", "rfc8949") == "MULTIPLE_TOP_LEVEL_ITEMS"


def test_unknown_tag():
    assert reject("d86405", "rfc8949") == "UNKNOWN_TAG"


def test_non_nfc_string_dcbor_only():
    # "cafe" + combining acute accent (U+0301), not normalized to NFC.
    b = "6663616665cc81"
    assert reject(b, "dcbor") == "NON_NFC_STRING"
    assert accept(b, "rfc8949") == "6663616665cc81"


def test_unreduced_numeric_dcbor_only():
    # 2.0 encoded at its shortest float width (f16) -- still must be a plain
    # int under dcbor's D5/D7 rules.
    b = "f94000"
    assert reject(b, "dcbor") == "UNREDUCED_NUMERIC"
    assert accept(b, "rfc8949") == "f94000"
