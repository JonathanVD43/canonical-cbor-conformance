"""IEEE-754 binary16 (f16) <-> binary64 (f64) bit-level conversion.

Python's stdlib `struct` module has no float16 pack/unpack format, so this
is hand-rolled -- ported from the same round-to-nearest-even algorithm the
Rust adapter's `half` crate and the Kotlin/TypeScript/Go adapters' Float16
modules use."""
import struct


def double_to_f16_bits(value: float) -> int:
    x = int.from_bytes(struct.pack(">d", value), "big")
    sign = (x >> 63) & 1
    exp = (x >> 52) & 0x7FF
    man = x & 0xFFFFFFFFFFFFF

    if exp == 0x7FF:
        # Infinity or NaN.
        new_man = man >> 42
        nan_bit = (1 << 9) if man != 0 else 0
        return (sign << 15 | 0x7C00 | new_man | nan_bit) & 0xFFFF

    half_sign_bits = sign << 15
    half_exp = exp - 1023 + 15

    if half_exp >= 0x1F:
        return (half_sign_bits | 0x7C00) & 0xFFFF

    if half_exp <= 0:
        if 10 - half_exp > 21:
            # Underflows even a subnormal half: signed zero.
            return half_sign_bits & 0xFFFF
        man_with_hidden = man | 0x10000000000000
        half_man = man_with_hidden >> (43 - half_exp)
        round_bit = 1 << (42 - half_exp)
        if man_with_hidden & round_bit != 0 and man_with_hidden & (3 * round_bit - 1) != 0:
            half_man += 1
        return (half_sign_bits | half_man) & 0xFFFF

    half_exp_bits = half_exp << 10
    half_man = man >> 42
    round_bit = 1 << 41
    combined = half_sign_bits | half_exp_bits | half_man
    round_up = man & round_bit != 0 and man & (3 * round_bit - 1) != 0
    if round_up:
        combined += 1
    return combined & 0xFFFF


def f16_bits_to_double(bits: int) -> float:
    i = bits & 0xFFFF
    sign = (i >> 15) & 1
    exp = (i >> 10) & 0x1F
    man = i & 0x3FF
    sign64 = sign << 63

    if exp == 0 and man == 0:
        return struct.unpack(">d", sign64.to_bytes(8, "big"))[0]
    if exp == 0:
        # Normalize the 10-bit subnormal significand by left-shifting until
        # its leading set bit reaches position 10 (the implicit-bit
        # position), counting shifts via exp_adj. exp_adj MUST start at 1,
        # not -1: for the smallest subnormal (man=1, true value 2^-24),
        # exactly 10 shifts are needed to reach man_adj=0x400, and the final
        # double exponent must come out to 1023-24=999. That only holds if
        # exp_adj lands on 1-10=-9 (giving 1008+(-9)=999); a -1 starting
        # point yields -11, undercounting by 2 and silently quartering the
        # reconstructed magnitude of every subnormal f16 value.
        exp_adj = 1
        man_adj = man
        while man_adj & 0x400 == 0:
            man_adj <<= 1
            exp_adj -= 1
        man_adj &= 0x3FF
        exp64 = 1023 - 15 + exp_adj
        man64 = man_adj << 42
        bits64 = (sign64 | (exp64 << 52) | man64) & 0xFFFFFFFFFFFFFFFF
        return struct.unpack(">d", bits64.to_bytes(8, "big"))[0]
    if exp == 0x1F:
        if man == 0:
            bits64 = sign64 | 0x7FF0000000000000
        else:
            bits64 = sign64 | 0x7FF0000000000000 | (man << 42)
        return struct.unpack(">d", bits64.to_bytes(8, "big"))[0]
    exp64 = exp - 15 + 1023
    man64 = man << 42
    bits64 = sign64 | (exp64 << 52) | man64
    return struct.unpack(">d", bits64.to_bytes(8, "big"))[0]


def _same_bits(a: float, b: float) -> bool:
    return struct.pack(">d", a) == struct.pack(">d", b)


# Callers must handle NaN before reaching these helpers -- bit-pattern
# comparison never matches NaN to NaN.
def try_f16(f: float):
    bits = double_to_f16_bits(f)
    back = f16_bits_to_double(bits)
    if not _same_bits(back, f):
        return None
    return bytes([(bits >> 8) & 0xFF, bits & 0xFF])


def try_f32(f: float):
    try:
        packed = struct.pack(">f", f)
    except OverflowError:
        # Magnitude beyond float32 range. Go's float32(f) cast silently
        # saturates to +/-Inf instead of raising; either way the round-trip
        # back to f won't match, so this is just another "doesn't fit" case.
        return None
    back = struct.unpack(">f", packed)[0]
    if not _same_bits(float(back), f):
        return None
    return packed
