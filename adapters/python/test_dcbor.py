import pytest

from logical_value import ArrayValue, BignumValue, BytesValue, FloatValue, IntValue, MapEntry, MapValue, TagValue, TextValue
from dcbor import encode_dcbor


def float_v(value):
    return FloatValue("auto", value)


def enc(v):
    return encode_dcbor(v).hex()


def test_d5_float_whole_becomes_int():
    assert enc(float_v("2.0")) == "02"


def test_d5_float_fraction_stays_float():
    assert enc(float_v("2.5")) == "f94100"


def test_d6_nan_canonical_payload():
    assert enc(float_v("NaN")) == "f97e00"


def test_d7_zero_as_int():
    assert enc(IntValue("0")) == "00"


def test_d7_zero_as_pos_float_unifies_with_int():
    assert enc(float_v("0.0")) == "00"


def test_d7_zero_as_neg_float_unifies_with_int():
    # Opposite of rfc8949's P7: -0.0 must also collapse to plain int 0.
    assert enc(float_v("-0.0")) == "00"


def test_d8_nfc_combining_accent_normalizes():
    v = TextValue("café")  # "e" + combining acute accent (decomposed)
    assert enc(v) == "65636166c3a9"


def test_bignum_below_native_range_is_rejected():
    with pytest.raises(Exception):
        encode_dcbor(BignumValue("positive", "5"))


def test_bignum_at_2pow64_uses_tag2():
    value = BignumValue("positive", "18446744073709551616")
    assert enc(value) == "c249010000000000000000"


def test_tag_wraps_inner_value():
    value = TagValue(100, IntValue("5"))
    assert enc(value) == "d86405"


def test_reserved_bignum_tag_via_generic_tag_arm_is_a_raw_passthrough():
    value = TagValue(2, BytesValue("01"))
    assert enc(value) == "c24101"


def test_explicit_width_is_ignored_whole_float_still_reduces():
    v = FloatValue("f64", "2.0")
    assert enc(v) == "02"


def test_explicit_width_is_ignored_nan_still_canonical():
    v = FloatValue("f32", "NaN")
    assert enc(v) == "f97e00"


def test_tag_at_uint64_max_does_not_truncate():
    value = TagValue(18446744073709551615, IntValue("5"))
    assert enc(value) == "dbffffffffffffffff05"


def test_map_key_collision_dedupes_last_write_wins():
    # D7: 0, 0.0, and -0.0 all canonicalize to encoded key 0x00, so these
    # three logical entries collapse to one, keeping only the last value
    # written (3).
    value = MapValue([
        MapEntry(IntValue("0"), IntValue("1")),
        MapEntry(float_v("0.0"), IntValue("2")),
        MapEntry(float_v("-0.0"), IntValue("3")),
    ])
    assert enc(value) == "a10003"


def test_map_key_collision_via_nfc_dedupes_last_write_wins():
    # D8: "café" (precomposed) and "café" (NFC-decomposed) both normalize to
    # identical encoded key bytes, so they must also collapse, last write wins.
    decomposed = "café"
    precomposed = "café"
    value = MapValue([
        MapEntry(TextValue(decomposed), IntValue("1")),
        MapEntry(TextValue(precomposed), IntValue("2")),
    ])
    assert enc(value) == "a165636166c3a902"


def test_array_and_map_round_trip():
    arr = ArrayValue([IntValue("1"), IntValue("2")])
    assert enc(arr) == "820102"

    m = MapValue([MapEntry(TextValue("a"), IntValue("1"))])
    assert enc(m) == "a1616101"
