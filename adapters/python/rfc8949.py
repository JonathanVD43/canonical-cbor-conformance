"""rfc8949-profile-1 encoder (SPEC.md's P1-P9). A from-scratch
reimplementation, not a library wrapper -- same rationale as the
Kotlin/TypeScript/Go adapters: this project needs exact control over
canonical-form choices (shortest-int width, bytewise map-key sort, the
single canonical NaN payload) that a generic CBOR library's own "canonical
mode" may not expose or may implement slightly differently.

Python's arbitrary-precision native `int` makes the bignum/shortest-int
arithmetic notably simpler here than in the Rust/Kotlin/TypeScript/Go ports,
which all needed an explicit big-integer type."""
import math
import re
import struct

from errors import EncodeError
from float16 import try_f16, try_f32
from logical_value import (
    ArrayValue, BignumValue, BoolValue, BytesValue, FloatValue, IntValue,
    MapValue, NullValue, TagValue, TextValue,
)
from util import hex_decode_strict

TWO_POW_64 = 1 << 64

_INT_LITERAL_RE = re.compile(r"^-?[0-9]+$")
_MAGNITUDE_LITERAL_RE = re.compile(r"^[0-9]+$")


def encode_head(major_type: int, arg: int) -> bytes:
    """Writes a CBOR item head (major type + argument) using the shortest
    encoding for arg, per P1/D2."""
    mt = major_type << 5
    if arg < 24:
        return bytes([mt | arg])
    if arg <= 0xFF:
        return bytes([mt | 24, arg])
    if arg <= 0xFFFF:
        return bytes([mt | 25]) + arg.to_bytes(2, "big")
    if arg <= 0xFFFFFFFF:
        return bytes([mt | 26]) + arg.to_bytes(4, "big")
    return bytes([mt | 27]) + arg.to_bytes(8, "big")


def encode_int(value: str) -> bytes:
    """Reused by dcbor.py (same shortest-int rule applies to both
    profiles)."""
    if not _INT_LITERAL_RE.match(value):
        raise EncodeError(f"int: invalid integer literal {value!r}")
    n = int(value)
    if n >= 0:
        if n >= TWO_POW_64:
            raise EncodeError(f"int: {n} exceeds native range")
        return encode_head(0, n)
    arg = -1 - n
    if arg >= TWO_POW_64:
        raise EncodeError(f"int: {n} exceeds native range")
    return encode_head(1, arg)


def double_to_be_bytes(f: float) -> bytes:
    """Reused by dcbor.py."""
    return struct.pack(">d", f)


def parse_float_literal(literal: str) -> float:
    try:
        return float(literal)
    except ValueError as e:
        raise EncodeError(f"float: invalid literal {literal!r}") from e


def encode_float(width: str, literal: str) -> bytes:
    f = parse_float_literal(literal)
    if math.isnan(f):
        # P6: NaN always uses the single canonical f16 payload, regardless
        # of width.
        return bytes.fromhex("f97e00")

    if width == "auto":
        b = try_f16(f)
        if b is not None:
            return b"\xf9" + b
        b = try_f32(f)
        if b is not None:
            return b"\xfa" + b
        return b"\xfb" + double_to_be_bytes(f)

    if width == "f16":
        b = try_f16(f)
        if b is None:
            raise EncodeError(f"float: {f} cannot round-trip at requested width f16")
        return b"\xf9" + b
    if width == "f32":
        b = try_f32(f)
        if b is None:
            raise EncodeError(f"float: {f} cannot round-trip at requested width f32")
        return b"\xfa" + b
    if width == "f64":
        return b"\xfb" + double_to_be_bytes(f)
    raise EncodeError(f"float: unknown width {width!r}")


def _strip_leading_zeros(b: bytes) -> bytes:
    i = 0
    while i < len(b) - 1 and b[i] == 0:
        i += 1
    return b[i:]


def bignum_tag_and_bytes(sign: str, value: str):
    """Shared with the dcbor profile: bignum tag+magnitude rules are
    identical in both profiles."""
    if sign not in ("positive", "negative"):
        raise EncodeError(f"bignum: unknown sign {sign!r}")
    if not _MAGNITUDE_LITERAL_RE.match(value):
        raise EncodeError(f"bignum: invalid magnitude {value!r}")
    magnitude = int(value)
    tag = 2 if sign == "positive" else 3
    if magnitude < TWO_POW_64:
        raise EncodeError(
            f"bignum magnitude {magnitude} fits in a native CBOR integer and "
            f"must not be encoded as tag {tag} (SPEC.md bignum rule)"
        )
    raw_int = magnitude if sign == "positive" else magnitude - 1
    nbytes = max(1, (raw_int.bit_length() + 7) // 8)
    return tag, _strip_leading_zeros(raw_int.to_bytes(nbytes, "big"))


def encode_bignum(sign: str, value: str) -> bytes:
    tag, raw_bytes = bignum_tag_and_bytes(sign, value)
    return encode_head(6, tag) + encode_head(2, len(raw_bytes)) + raw_bytes


def compare_bytes_unsigned(a: bytes, b: bytes) -> int:
    """Reused by dcbor.py (same bytewise map-key sort rule applies to both
    profiles). Bytewise-lexicographic, shorter-is-less on a shared prefix --
    Python's native bytes comparison already has exactly this semantics."""
    if a < b:
        return -1
    if a > b:
        return 1
    return 0


def encode_rfc8949(value) -> bytes:
    if isinstance(value, IntValue):
        return encode_int(value.value)
    if isinstance(value, TextValue):
        b = value.value.encode("utf-8")
        return encode_head(3, len(b)) + b
    if isinstance(value, BytesValue):
        b = hex_decode_strict(value.value)
        return encode_head(2, len(b)) + b
    if isinstance(value, BoolValue):
        return b"\xf5" if value.value else b"\xf4"
    if isinstance(value, NullValue):
        return b"\xf6"
    if isinstance(value, ArrayValue):
        out = encode_head(4, len(value.items))
        for item in value.items:
            out += encode_rfc8949(item)
        return out
    if isinstance(value, FloatValue):
        return encode_float(value.width, value.value)
    if isinstance(value, MapValue):
        encoded = [(encode_rfc8949(e.key), encode_rfc8949(e.val)) for e in value.entries]
        # list.sort is stable (Timsort); sorting bytes objects with the
        # default comparator is already bytewise-lexicographic, no
        # length-first pre-sort trap.
        encoded.sort(key=lambda kv: kv[0])
        out = encode_head(5, len(value.entries))
        for k, v in encoded:
            out += k + v
        return out
    if isinstance(value, TagValue):
        return encode_head(6, value.tag) + encode_rfc8949(value.value)
    if isinstance(value, BignumValue):
        return encode_bignum(value.sign, value.value)
    raise EncodeError(f"unhandled logical value type {type(value)}")
