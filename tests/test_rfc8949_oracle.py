import pytest

from oracle.rfc8949_oracle import encode_rfc8949


def test_shortest_int_zero():
    assert encode_rfc8949({"type": "int", "value": "0"}) == bytes.fromhex("00")


def test_shortest_int_boundary_23_24():
    assert encode_rfc8949({"type": "int", "value": "23"}) == bytes.fromhex("17")
    assert encode_rfc8949({"type": "int", "value": "24"}) == bytes.fromhex("1818")


def test_negative_int():
    assert encode_rfc8949({"type": "int", "value": "-1"}) == bytes.fromhex("20")


def test_map_keys_sorted_by_encoded_bytes():
    value = {
        "type": "map",
        "entries": [
            [{"type": "int", "value": "9"}, {"type": "int", "value": "1"}],
            [{"type": "int", "value": "10"}, {"type": "int", "value": "2"}],
        ],
    }
    # encoded key for 9 is 0x09 (1 byte), for 10 is 0x0a (1 byte) -> 0x09 < 0x0a, so 9 sorts first
    # even though this happens to match numeric order, the point is P2 sorts by bytes, not value
    assert encode_rfc8949(value) == bytes.fromhex("a2" "09" "01" "0a" "02")


def test_map_keys_pure_bytewise_no_length_presort():
    # -24 encodes to 1 byte (0x37); 1000 encodes to 3 bytes (0x1903e8).
    # cbor2's own canonical=True sorts by (len, bytes) and would put -24
    # first (shorter wins). RFC 8949 SS4.2.1 requires PURE bytewise order
    # of the encoded key with no length pre-sort: 0x19 < 0x37, so the
    # 3-byte encoding of 1000 must sort before the 1-byte encoding of -24.
    value = {
        "type": "map",
        "entries": [
            [{"type": "int", "value": "-24"}, {"type": "int", "value": "1"}],
            [{"type": "int", "value": "1000"}, {"type": "int", "value": "2"}],
        ],
    }
    assert encode_rfc8949(value) == bytes.fromhex("a2" "1903e8" "02" "37" "01")


def test_negative_bignum_offset_by_one():
    # RFC 8949 SS3.4.3: tag 3's payload encodes n = magnitude - 1 (the
    # represented value is -1-n). For magnitude 2^64, n = 2^64 - 1, whose
    # minimal big-endian encoding is 8 bytes of 0xff (not 9 bytes of a
    # raw 2^64 with a leading 0x01 byte).
    value = {"type": "bignum", "sign": "negative", "value": "18446744073709551616"}
    assert encode_rfc8949(value) == bytes.fromhex("c3" "48" + "ff" * 8)


def test_explicit_float_width_forces_encoding():
    # 2.5 is exactly representable at f16 (shortest-auto form is f94100),
    # but an explicit width="f64" must force the 8-byte double form.
    value = {"type": "float", "width": "f64", "value": "2.5"}
    assert encode_rfc8949(value) == bytes.fromhex("fb4004000000000000")


def test_explicit_float_width_raises_on_precision_loss():
    # 0.1 has no exact f16 representation; forcing width="f16" must raise
    # rather than silently truncate.
    value = {"type": "float", "width": "f16", "value": "0.1"}
    with pytest.raises(ValueError):
        encode_rfc8949(value)


def test_nan_canonical_payload():
    assert encode_rfc8949({"type": "float", "width": "auto", "value": "NaN"}) == bytes.fromhex("f97e00")


def test_negative_zero_preserved_distinct_from_zero():
    neg_zero = encode_rfc8949({"type": "float", "width": "auto", "value": "-0.0"})
    pos_zero = encode_rfc8949({"type": "float", "width": "auto", "value": "0.0"})
    assert neg_zero == bytes.fromhex("f98000")
    assert pos_zero == bytes.fromhex("f90000")
    assert neg_zero != pos_zero


def test_shortest_float_form_f16_exact():
    assert encode_rfc8949({"type": "float", "width": "auto", "value": "2.5"}) == bytes.fromhex("f94100")


def test_bignum_below_2_64_must_use_int_type_not_tag():
    # per SPEC.md's bignum rule, this is a contract on the caller, not the oracle:
    # the oracle only knows how to encode a bignum type as tag 2/3, it does not
    # downgrade a bignum LogicalValue to plain int -- that decision belongs to
    # whoever authors the vector. This test documents tag 2/3 encoding for a
    # magnitude that DOES require it (2^64).
    value = {"type": "bignum", "sign": "positive", "value": "18446744073709551616"}
    assert encode_rfc8949(value) == bytes.fromhex("c2" "49" "01" + "00" * 8)


def test_bignum_natively_representable_magnitude_rejected():
    # SPEC.md's bignum rule: magnitudes fitting the native 64-bit CBOR
    # integer range MUST use plain integers, never tag 2/3. The oracle must
    # enforce this rather than silently emitting a non-canonical tag-wrapped
    # value.
    positive = {"type": "bignum", "sign": "positive", "value": "5"}
    with pytest.raises(ValueError):
        encode_rfc8949(positive)

    negative = {"type": "bignum", "sign": "negative", "value": str(2**63)}
    with pytest.raises(ValueError):
        encode_rfc8949(negative)
