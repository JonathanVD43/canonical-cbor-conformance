"""dcbor-profile-<draft-version> encoder: D5 (numeric reduction), D6 (NaN
pin), D7 (zero unification), and D8 (NFC normalization) are implemented
directly here. D3 (map key sort) and bignum handling reuse the exact same
rules as rfc8949 (see rfc8949.py).

Unlike the Go adapter, D8 needs no hand-rolled implementation: Python's
stdlib `unicodedata.normalize("NFC", ...)` covers it natively."""
import math
import unicodedata

from errors import EncodeError
from float16 import try_f16, try_f32
from logical_value import (
    ArrayValue, BignumValue, BoolValue, BytesValue, FloatValue, IntValue,
    MapValue, NullValue, TagValue, TextValue,
)
from rfc8949 import (
    TWO_POW_64,
    double_to_be_bytes,
    encode_bignum,
    encode_head,
    encode_int,
    parse_float_literal,
)
from util import hex_decode_strict


def encode_float_dcbor(literal: str) -> bytes:
    f = parse_float_literal(literal)
    if math.isnan(f):
        # D6: NaN always uses the single canonical f16 payload.
        return bytes.fromhex("f97e00")

    if not math.isinf(f) and f == math.floor(f):
        # D5/D7: a float with no fractional part (including -0.0) reduces to
        # the shortest-int form, unifying with the plain integer encoding.
        # Python's `int(f)` is an exact conversion for any whole-valued
        # finite float, no big-integer helper needed.
        whole = int(f)
        if whole >= 0:
            if whole < TWO_POW_64:
                return encode_head(0, whole)
        else:
            arg = -1 - whole
            if arg < TWO_POW_64:
                return encode_head(1, arg)
        # Falls through to shortest-float form below if it doesn't fit the
        # native int range (no dcbor vector exercises this edge).

    b = try_f16(f)
    if b is not None:
        return b"\xf9" + b
    b = try_f32(f)
    if b is not None:
        return b"\xfa" + b
    return b"\xfb" + double_to_be_bytes(f)


def encode_bignum_dcbor(sign: str, value: str) -> bytes:
    return encode_bignum(sign, value)


def encode_dcbor(value) -> bytes:
    if isinstance(value, IntValue):
        return encode_int(value.value)
    if isinstance(value, FloatValue):
        return encode_float_dcbor(value.value)
    if isinstance(value, TextValue):
        # D8: normalize to NFC before UTF-8 encoding.
        normalized = unicodedata.normalize("NFC", value.value)
        b = normalized.encode("utf-8")
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
            out += encode_dcbor(item)
        return out
    if isinstance(value, MapValue):
        # dCBOR's reference encoder stores entries in a real map keyed by
        # canonical-encoded bytes, so two logical entries that canonicalize
        # to the same key (e.g. via D7 zero unification or D8 NFC
        # normalization) collapse to one, last write wins.
        dedup: dict[bytes, bytes] = {}
        order: list[bytes] = []
        for e in value.entries:
            encoded_key = encode_dcbor(e.key)
            encoded_val = encode_dcbor(e.val)
            if encoded_key not in dedup:
                order.append(encoded_key)
            dedup[encoded_key] = encoded_val
        entries = [(k, dedup[k]) for k in order]
        entries.sort(key=lambda kv: kv[0])
        out = encode_head(5, len(entries))
        for k, v in entries:
            out += k + v
        return out
    if isinstance(value, TagValue):
        return encode_head(6, value.tag) + encode_dcbor(value.value)
    if isinstance(value, BignumValue):
        return encode_bignum_dcbor(value.sign, value.value)
    raise EncodeError(f"unhandled logical value type {type(value)}")
