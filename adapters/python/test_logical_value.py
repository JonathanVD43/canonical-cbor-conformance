import pytest

from errors import ParseError
from logical_value import (
    ArrayValue,
    BignumValue,
    BoolValue,
    BytesValue,
    FloatValue,
    IntValue,
    MapEntry,
    MapValue,
    NullValue,
    TagValue,
    TextValue,
    parse_logical_value_line,
)


def test_parses_int():
    assert parse_logical_value_line('{"type":"int","value":"42"}') == IntValue("42")


def test_parses_float():
    assert parse_logical_value_line('{"type":"float","width":"auto","value":"2.5"}') == FloatValue("auto", "2.5")


def test_parses_text():
    assert parse_logical_value_line('{"type":"text","value":"café"}') == TextValue("café")


def test_parses_bytes():
    assert parse_logical_value_line('{"type":"bytes","value":"deadbeef"}') == BytesValue("deadbeef")


def test_parses_bool_and_null():
    assert parse_logical_value_line('{"type":"bool","value":true}') == BoolValue(True)
    assert parse_logical_value_line('{"type":"null"}') == NullValue()


def test_parses_array():
    got = parse_logical_value_line('{"type":"array","items":[{"type":"int","value":"1"},{"type":"int","value":"2"}]}')
    assert got == ArrayValue([IntValue("1"), IntValue("2")])


def test_parses_map():
    got = parse_logical_value_line('{"type":"map","entries":[[{"type":"text","value":"a"},{"type":"int","value":"1"}]]}')
    assert got == MapValue([MapEntry(TextValue("a"), IntValue("1"))])


def test_parses_tag():
    got = parse_logical_value_line('{"type":"tag","tag":100,"value":{"type":"int","value":"5"}}')
    assert got == TagValue(100, IntValue("5"))


def test_parses_tag_at_uint64_max():
    # Regression: a naive parse through a signed 64-bit intermediate would
    # silently truncate this. 18446744073709551615 == 2**64 - 1.
    got = parse_logical_value_line('{"type":"tag","tag":18446744073709551615,"value":{"type":"int","value":"5"}}')
    assert got == TagValue(18446744073709551615, IntValue("5"))


def test_rejects_tag_above_uint64_max():
    with pytest.raises(ParseError):
        parse_logical_value_line('{"type":"tag","tag":18446744073709551616,"value":{"type":"int","value":"5"}}')


def test_rejects_negative_tag():
    with pytest.raises(ParseError):
        parse_logical_value_line('{"type":"tag","tag":-1,"value":{"type":"int","value":"5"}}')


def test_parses_bignum():
    got = parse_logical_value_line('{"type":"bignum","sign":"positive","value":"18446744073709551616"}')
    assert got == BignumValue("positive", "18446744073709551616")


def test_rejects_unknown_type():
    with pytest.raises(ParseError):
        parse_logical_value_line('{"type":"nonsense"}')


def test_rejects_missing_type():
    with pytest.raises(ParseError):
        parse_logical_value_line("{}")


def test_rejects_malformed_json():
    with pytest.raises(ParseError):
        parse_logical_value_line("not json")
