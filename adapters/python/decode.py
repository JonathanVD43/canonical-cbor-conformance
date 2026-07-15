"""Strict canonical-CBOR decoder (SPEC.md's `decode-strict` mode). Parses raw
CBOR bytes into a byte-range-aware intermediate form and rejects any
well-formed-but-non-canonical input with the single most specific
decode-strict reason code (P1-P9 / D1-D9). Hand-rolled (not delegated to a
generic CBOR library, e.g. `cbor2` which is in requirements.txt for the
harness/oracle side only) because a generic decode normalizes away exactly
the information needed to tell *why* something is non-canonical: which
additional-info width was used, raw NaN payload bits, raw map-key byte
order.

Bignum rule (SPEC.md), matching the Rust/Kotlin/TypeScript/Go adapters: a
tag-2/3 (bignum) item is rejected with NON_CANONICAL_BIGNUM if its magnitude
fits the native 64-bit range or its byte-string payload is non-minimal (a
leading zero byte). A genuinely canonical bignum still ACCEPTs.

Unlike the Go port, this uses real exceptions to unwind out of nested
recursive-descent calls (DecodeSignal), rather than Go's per-call `if err !=
nil { return }` propagation -- functionally equivalent, just idiomatic for
the language."""
import math
import struct
import unicodedata
from dataclasses import dataclass, field
from typing import List, Optional

from errors import DecodeSignal, EncodeError
from float16 import double_to_f16_bits, f16_bits_to_double
from logical_value import (
    ArrayValue, BoolValue, FloatValue, IntValue, MapEntry, MapValue,
    NullValue, TagValue, TextValue, BytesValue,
)
from util import hex_encode
import rfc8949
import dcbor

REASON_CODES = {
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
    "NON_CANONICAL_BIGNUM",
}

# UNKNOWN_TAG allow-list: only the bignum tags are exercised by the current
# vector corpus.
ALLOWED_TAGS = {2, 3}


def _reject(reason: str) -> DecodeSignal:
    return DecodeSignal(reason)


def _internal(msg: str) -> DecodeSignal:
    return DecodeSignal(msg)


@dataclass
class Verdict:
    accept: bool
    bytes_out: bytes = b""
    reason: str = ""


class _Cursor:
    __slots__ = ("pos",)

    def __init__(self, pos: int = 0):
        self.pos = pos


# --- intermediate parse-tree "item" types (mirrors the Go/Kotlin adapters'
# sealed Item type) ---
@dataclass
class _ItemIntPos:
    v: int


@dataclass
class _ItemIntNeg:
    v: int


@dataclass
class _ItemBytes:
    v: bytes


@dataclass
class _ItemText:
    v: str


@dataclass
class _ItemArr:
    items: List[object] = field(default_factory=list)


@dataclass
class _ItemMapEntry:
    k: object
    v: object


@dataclass
class _ItemMap:
    entries: List[_ItemMapEntry] = field(default_factory=list)


@dataclass
class _ItemTagged:
    tag: int
    inner: object


@dataclass
class _ItemBool:
    v: bool


@dataclass
class _ItemNull:
    pass


@dataclass
class _ItemFloat:
    v: float


WIDTH_DIRECT, WIDTH_ONE, WIDTH_TWO, WIDTH_FOUR, WIDTH_EIGHT = range(5)
FLOAT_F16, FLOAT_F32, FLOAT_F64 = range(3)


@dataclass
class _Head:
    major: int
    arg: int = 0
    width: int = WIDTH_DIRECT
    indefinite: bool = False


def _read_head(data: bytes, c: _Cursor) -> _Head:
    if c.pos >= len(data):
        raise _internal("internal: truncated input (expected an item header)")
    b0 = data[c.pos]
    c.pos += 1
    major = (b0 >> 5) & 0x7
    info = b0 & 0x1F
    if info == 31:
        return _Head(major=major, indefinite=True)
    if info <= 23:
        return _Head(major=major, arg=info, width=WIDTH_DIRECT)
    if info == 24:
        if c.pos >= len(data):
            raise _internal("internal: truncated 1-byte argument")
        v = data[c.pos]
        c.pos += 1
        return _Head(major=major, arg=v, width=WIDTH_ONE)
    if info == 25:
        if c.pos + 2 > len(data):
            raise _internal("internal: truncated 2-byte argument")
        v = int.from_bytes(data[c.pos:c.pos + 2], "big")
        c.pos += 2
        return _Head(major=major, arg=v, width=WIDTH_TWO)
    if info == 26:
        if c.pos + 4 > len(data):
            raise _internal("internal: truncated 4-byte argument")
        v = int.from_bytes(data[c.pos:c.pos + 4], "big")
        c.pos += 4
        return _Head(major=major, arg=v, width=WIDTH_FOUR)
    if info == 27:
        if c.pos + 8 > len(data):
            raise _internal("internal: truncated 8-byte argument")
        v = int.from_bytes(data[c.pos:c.pos + 8], "big")
        c.pos += 8
        return _Head(major=major, arg=v, width=WIDTH_EIGHT)
    raise _internal("internal: reserved additional-info value (28-30)")


def _shortest_width_for(arg: int) -> int:
    if arg < 24:
        return WIDTH_DIRECT
    if arg <= 0xFF:
        return WIDTH_ONE
    if arg <= 0xFFFF:
        return WIDTH_TWO
    if arg <= 0xFFFFFFFF:
        return WIDTH_FOUR
    return WIDTH_EIGHT


def _check_shortest_arg(h: _Head) -> None:
    # P1/D2: every integer *argument* (ints themselves, and
    # string/array/map lengths, and tag numbers) must use the shortest
    # encoding for its value.
    if h.width != _shortest_width_for(h.arg):
        raise _reject("NON_SHORTEST_INT")


def _strict_utf8_decode(b: bytes) -> str:
    try:
        return b.decode("utf-8", errors="strict")
    except UnicodeDecodeError as e:
        raise _internal(f"internal: invalid utf-8 in text string: {e}") from e


def _parse_item(data: bytes, c: _Cursor, profile: str):
    h = _read_head(data, c)

    if h.major == 7:
        if h.indefinite:
            raise _internal("internal: unexpected break byte outside indefinite-length container")
        return _parse_major7(h.width, h.arg, profile)

    if h.indefinite:
        # P3/D1: indefinite-length arrays/maps/byte/text strings are banned
        # outright, regardless of what they'd otherwise contain.
        raise _reject("INDEFINITE_LENGTH")
    _check_shortest_arg(h)

    if h.major == 0:
        return _ItemIntPos(h.arg)
    if h.major == 1:
        return _ItemIntNeg(h.arg)
    if h.major == 2:
        length = h.arg
        if c.pos + length > len(data):
            raise _internal("internal: truncated byte string")
        b = data[c.pos:c.pos + length]
        c.pos += length
        return _ItemBytes(b)
    if h.major == 3:
        length = h.arg
        if c.pos + length > len(data):
            raise _internal("internal: truncated text string")
        b = data[c.pos:c.pos + length]
        s = _strict_utf8_decode(b)
        c.pos += length
        # D8: dcbor requires NFC-normalized text; rfc8949 has no such rule
        # (P9 is an explicit non-goal).
        if profile == "dcbor" and unicodedata.normalize("NFC", s) != s:
            raise _reject("NON_NFC_STRING")
        return _ItemText(s)
    if h.major == 4:
        length = h.arg
        items = [_parse_item(data, c, profile) for _ in range(length)]
        return _ItemArr(items)
    if h.major == 5:
        length = h.arg
        entries = []
        key_ranges = []
        for _ in range(length):
            key_start = c.pos
            k = _parse_item(data, c, profile)
            key_end = c.pos
            v = _parse_item(data, c, profile)
            key_ranges.append((key_start, key_end))
            entries.append(_ItemMapEntry(k, v))
        # P2/D3: keys must appear in strictly increasing bytewise order of
        # their own raw encoded bytes. A duplicate key is necessarily
        # adjacent to itself in properly sorted order, so one adjacent pass
        # catches both violations.
        for i in range(len(key_ranges) - 1):
            a = data[key_ranges[i][0]:key_ranges[i][1]]
            b = data[key_ranges[i + 1][0]:key_ranges[i + 1][1]]
            if a == b:
                raise _reject("DUPLICATE_KEY")
            if a > b:
                raise _reject("UNSORTED_MAP_KEYS")
        return _ItemMap(entries)
    if h.major == 6:
        tag = h.arg
        if tag not in ALLOWED_TAGS:
            raise _reject("UNKNOWN_TAG")
        inner = _parse_item(data, c, profile)
        # Bignum rule: a tag 2/3 payload must be the minimal big-endian
        # encoding of a magnitude >= 2^64. Reject if the magnitude fits the
        # native 64-bit range (a <= 8-byte payload always does, once the
        # leading-zero case is ruled out) or the payload is non-minimal
        # (non-empty with a leading zero byte).
        if tag in (2, 3) and isinstance(inner, _ItemBytes):
            b = inner.v
            if (len(b) >= 1 and b[0] == 0) or len(b) <= 8:
                raise _reject("NON_CANONICAL_BIGNUM")
        return _ItemTagged(tag, inner)
    raise _internal("internal: major type is 3 bits, always 0-7")


def _parse_major7(width: int, arg: int, profile: str):
    if width == WIDTH_DIRECT:
        if arg == 20:
            return _ItemBool(False)
        if arg == 21:
            return _ItemBool(True)
        if arg == 22:
            return _ItemNull()
        raise _internal(f"internal: unsupported major-7 simple value {arg}")
    if width == WIDTH_ONE:
        raise _internal("internal: unsupported major-7 1-byte simple value")
    if width == WIDTH_TWO:
        return _check_float(arg, FLOAT_F16, profile)
    if width == WIDTH_FOUR:
        return _check_float(arg, FLOAT_F32, profile)
    if width == WIDTH_EIGHT:
        return _check_float(arg, FLOAT_F64, profile)
    raise _internal("internal: unreachable float width")


def _check_float(bits: int, width: int, profile: str):
    if width == FLOAT_F16:
        value = f16_bits_to_double(bits)
    elif width == FLOAT_F32:
        value = struct.unpack(">f", bits.to_bytes(4, "big"))[0]
    else:
        value = struct.unpack(">d", bits.to_bytes(8, "big"))[0]

    if math.isnan(value):
        # P6/D6: canonical NaN is exactly f16 with payload 0x7e00, in both
        # profiles.
        if width != FLOAT_F16 or bits != 0x7E00:
            raise _reject("NAN_PAYLOAD_VARIANT")
        return _ItemFloat(math.nan)

    # D5/D7 (dcbor only): any whole-number float in [-2^63, 2^64-1] --
    # including +/-0.0, per D7 zero unification -- must be a plain int
    # instead. This is checked before, and takes priority over, the general
    # shortest-float-width rule below.
    if profile == "dcbor" and _is_dcbor_reducible(value):
        raise _reject("UNREDUCED_NUMERIC")

    # P5/D2: the width used must be the narrowest of f16/f32/f64 that
    # round-trips `value` exactly.
    shortest = _shortest_float_width(value)
    if width != shortest:
        raise _reject("NON_SHORTEST_FLOAT")

    return _ItemFloat(value)


def _is_dcbor_reducible(value: float) -> bool:
    if math.isinf(value):
        return False
    if value != math.floor(value):
        return False
    return -9223372036854775808.0 <= value <= 18446744073709551615.0


def _shortest_float_width(value: float) -> int:
    target = struct.pack(">d", value)
    f16_bits = double_to_f16_bits(value)
    if struct.pack(">d", f16_bits_to_double(f16_bits)) == target:
        return FLOAT_F16
    try:
        f32_back = struct.unpack(">f", struct.pack(">f", value))[0]
        if struct.pack(">d", f32_back) == target:
            return FLOAT_F32
    except OverflowError:
        # Magnitude beyond float32 range -- can't be represented at f32,
        # same as float16.try_f32's OverflowError handling.
        pass
    return FLOAT_F64


def _format_float(value: float) -> str:
    if math.isnan(value):
        return "NaN"
    if math.isinf(value):
        return "Infinity" if value > 0 else "-Infinity"
    return repr(value)


def _item_to_logical(it):
    if isinstance(it, _ItemIntPos):
        return IntValue(str(it.v))
    if isinstance(it, _ItemIntNeg):
        return IntValue(str(-1 - it.v))
    if isinstance(it, _ItemBytes):
        return BytesValue(hex_encode(it.v))
    if isinstance(it, _ItemText):
        return TextValue(it.v)
    if isinstance(it, _ItemArr):
        return ArrayValue([_item_to_logical(x) for x in it.items])
    if isinstance(it, _ItemMap):
        return MapValue([MapEntry(_item_to_logical(e.k), _item_to_logical(e.v)) for e in it.entries])
    if isinstance(it, _ItemTagged):
        return TagValue(it.tag, _item_to_logical(it.inner))
    if isinstance(it, _ItemBool):
        return BoolValue(it.v)
    if isinstance(it, _ItemNull):
        return NullValue()
    if isinstance(it, _ItemFloat):
        return FloatValue("auto", _format_float(it.v))
    raise _internal(f"unhandled item type {type(it)}")


def decode_strict(data: bytes, profile: str) -> Verdict:
    """profile: "rfc8949" or "dcbor". A non-DecodeSignal-reason-code
    exception (i.e. a DecodeSignal carrying an "internal: ..." message, or
    an EncodeError from a re-encode failure) propagates to the caller --
    callers should treat that the same way the reference adapters do: print
    a diagnostic and an empty output line, not crash the batch loop."""
    if len(data) == 0:
        raise _internal("internal: empty input line")

    c = _Cursor()
    try:
        item = _parse_item(data, c, profile)
    except DecodeSignal as e:
        reason = str(e)
        if reason in REASON_CODES:
            return Verdict(accept=False, reason=reason)
        raise

    if c.pos < len(data):
        c2 = _Cursor(c.pos)
        ok = False
        try:
            _parse_item(data, c2, profile)
            ok = c2.pos == len(data)
        except DecodeSignal:
            ok = False
        if ok:
            return Verdict(accept=False, reason="MULTIPLE_TOP_LEVEL_ITEMS")
        return Verdict(accept=False, reason="TRAILING_BYTES")

    logical = _item_to_logical(item)
    try:
        if profile == "rfc8949":
            reencoded = rfc8949.encode_rfc8949(logical)
        else:
            reencoded = dcbor.encode_dcbor(logical)
    except EncodeError as e:
        raise _internal(f"internal: canonical input failed to re-encode: {e}") from e
    return Verdict(accept=True, bytes_out=reencoded)
