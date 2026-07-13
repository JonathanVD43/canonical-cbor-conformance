import math

from float16 import double_to_f16_bits, f16_bits_to_double, try_f16, try_f32


def bits_from_hex(s: str) -> int:
    b = bytes.fromhex(s)
    assert len(b) == 2
    return (b[0] << 8) | b[1]


def test_f16_zero_and_negative_zero():
    assert double_to_f16_bits(0.0) == bits_from_hex("0000")
    assert double_to_f16_bits(math.copysign(0.0, -1.0)) == bits_from_hex("8000")
    assert f16_bits_to_double(bits_from_hex("0000")) == 0.0
    back = f16_bits_to_double(bits_from_hex("8000"))
    assert math.copysign(1.0, back) == -1.0
    assert back == 0.0


def test_f16_canonical_nan_payload():
    assert double_to_f16_bits(math.nan) == bits_from_hex("7e00")


def test_f16_infinity():
    assert double_to_f16_bits(math.inf) == bits_from_hex("7c00")
    assert double_to_f16_bits(-math.inf) == bits_from_hex("fc00")
    assert f16_bits_to_double(bits_from_hex("7c00")) == math.inf
    assert f16_bits_to_double(bits_from_hex("fc00")) == -math.inf


def test_f16_exactly_representable_value_round_trips():
    b = try_f16(2.5)
    assert b is not None
    assert b.hex() == "4100"


def test_f16_non_exact_value_rejected():
    assert try_f16(0.1) is None


def test_f16_smallest_normal_and_subnormal_boundary():
    smallest_normal = 2.0 ** -14.0
    assert double_to_f16_bits(smallest_normal) == bits_from_hex("0400")
    smallest_subnormal = 2.0 ** -24.0
    assert double_to_f16_bits(smallest_subnormal) == bits_from_hex("0001")


def test_f16_max_finite_half_round_trips():
    assert double_to_f16_bits(65504.0) == bits_from_hex("7bff")
    assert f16_bits_to_double(bits_from_hex("7bff")) == 65504.0


def test_f16_overflow_rounds_to_infinity():
    assert double_to_f16_bits(65520.0) == bits_from_hex("7c00")


def test_f16_subnormal_exhaustive_round_trip():
    # Regression test for a found-and-fixed bug: f16_bits_to_double's
    # subnormal branch used to start exp_adj at -1 instead of 1,
    # undercounting the normalization shift by 2 and silently quartering
    # the magnitude of every subnormal f16 value on decode. No existing
    # vector exercised this (only the encode direction was covered by
    # test_f16_smallest_normal_and_subnormal_boundary above); caught by an
    # independent QA pass after the same bug was found and fixed in the C
    # and Go adapters first.
    for bits in range(0, 0x0400):
        got = f16_bits_to_double(bits)
        want = bits * 2.0**-24
        assert got == want, f"bits=0x{bits:04x} got={got!r} want={want!r}"


def test_f32_round_trip():
    b = try_f32(2.5)
    assert b is not None
    assert b.hex() == "40200000"
    assert try_f32(0.1) is None


def test_f32_magnitude_beyond_range_rejected_not_raised():
    # Regression: struct.pack('>f', ...) raises OverflowError for a
    # magnitude beyond float32 range (~3.4e38), unlike Go's float32(f) cast
    # which silently saturates to +/-Inf. try_f32 must treat this as
    # "doesn't fit" (None), not propagate the exception.
    huge = -294449594579902470000000000000000000000000000000.0
    assert try_f32(huge) is None
