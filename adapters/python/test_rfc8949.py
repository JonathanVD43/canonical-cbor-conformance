import pytest

from logical_value import ArrayValue, BignumValue, BoolValue, BytesValue, FloatValue, IntValue, MapEntry, MapValue, NullValue, TagValue, TextValue
from rfc8949 import encode_rfc8949


def enc(v):
    return encode_rfc8949(v).hex()


def test_shortest_int_zero():
    assert enc(IntValue("0")) == "00"


def test_shortest_int_boundary_23_24():
    assert enc(IntValue("23")) == "17"
    assert enc(IntValue("24")) == "1818"


def test_shortest_int_boundary_255_256():
    assert enc(IntValue("255")) == "18ff"
    assert enc(IntValue("256")) == "190100"


def test_shortest_int_boundary_65535_65536():
    assert enc(IntValue("65535")) == "19ffff"
    assert enc(IntValue("65536")) == "1a00010000"


def test_shortest_int_boundary_4294967295_4294967296():
    assert enc(IntValue("4294967295")) == "1affffffff"
    assert enc(IntValue("4294967296")) == "1b0000000100000000"


def test_negative_int():
    assert enc(IntValue("-1")) == "20"


def test_negative_int_min_i64_boundary():
    assert enc(IntValue("-9223372036854775808")) == "3b7fffffffffffffff"


def test_text_and_bytes():
    assert enc(TextValue("a")) == "6161"
    assert enc(BytesValue("deadbeef")) == "44deadbeef"


def test_bool_and_null():
    assert enc(BoolValue(True)) == "f5"
    assert enc(BoolValue(False)) == "f4"
    assert enc(NullValue()) == "f6"


def test_array_of_ints():
    arr = ArrayValue([IntValue("1"), IntValue("2")])
    assert enc(arr) == "820102"


def test_nan_canonical_payload():
    assert enc(FloatValue("auto", "NaN")) == "f97e00"


def test_nan_canonical_payload_under_explicit_width_forcing():
    for width in ("f16", "f32", "f64"):
        assert enc(FloatValue(width, "NaN")) == "f97e00"


def test_negative_zero_preserved_distinct_from_zero():
    neg = enc(FloatValue("auto", "-0.0"))
    pos = enc(FloatValue("auto", "0.0"))
    assert neg == "f98000"
    assert pos == "f90000"
    assert neg != pos


def test_shortest_float_form_f16_exact():
    assert enc(FloatValue("auto", "2.5")) == "f94100"


def test_explicit_float_width_forces_encoding():
    assert enc(FloatValue("f64", "2.5")) == "fb4004000000000000"


def test_explicit_float_width_raises_on_precision_loss():
    with pytest.raises(Exception):
        encode_rfc8949(FloatValue("f16", "0.1"))


def test_map_keys_sorted_by_encoded_bytes():
    value = MapValue([MapEntry(IntValue("9"), IntValue("1")), MapEntry(IntValue("10"), IntValue("2"))])
    assert enc(value) == "a209010a02"


def test_map_keys_pure_bytewise_no_length_presort():
    value = MapValue([MapEntry(IntValue("-24"), IntValue("1")), MapEntry(IntValue("1000"), IntValue("2"))])
    assert enc(value) == "a21903e8023701"


def test_bignum_at_2pow64_uses_tag2():
    value = BignumValue("positive", "18446744073709551616")
    assert enc(value) == "c249010000000000000000"


def test_negative_bignum_offset_by_one():
    value = BignumValue("negative", "18446744073709551616")
    assert enc(value) == "c348ffffffffffffffff"


def test_bignum_below_2pow64_is_rejected():
    with pytest.raises(Exception):
        encode_rfc8949(BignumValue("positive", "5"))
    with pytest.raises(Exception):
        encode_rfc8949(BignumValue("negative", "9223372036854775808"))


def test_tag_wraps_inner_value():
    value = TagValue(100, IntValue("5"))
    assert enc(value) == "d86405"


def test_tag_at_uint64_max_does_not_truncate():
    value = TagValue(18446744073709551615, IntValue("5"))
    assert enc(value) == "dbffffffffffffffff05"


def test_map_duplicate_keys_are_not_deduped():
    # rfc8949 (P4/P9) has no key-collision dedup logic, unlike dcbor -- both
    # entries for a literal duplicate key must survive to the output.
    value = MapValue([MapEntry(IntValue("5"), IntValue("1")), MapEntry(IntValue("5"), IntValue("2"))])
    assert enc(value) == "a205010502"
