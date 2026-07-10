"""Wraps cbor2 (PyPI, independent third-party library) for rfc8949-profile
canonical encoding, with a thin patch layer for this project's float pins
(P6 NaN payload, P7 preserved -0.0) that cbor2's own canonical mode does not
control on its own."""
import struct
from decimal import Decimal

import cbor2


class PinnedFloat:
    """Carries pre-computed canonical CBOR bytes for a single float leaf,
    bypassing cbor2's own float-encoding choice."""

    def __init__(self, raw: bytes):
        self.raw = raw


def _pinned_default(encoder, value):
    if isinstance(value, PinnedFloat):
        encoder.write(value.raw)
        return
    raise cbor2.CBOREncodeTypeError(f"cannot encode {type(value)!r}")


def _same_bits(a: float, b: float) -> bool:
    """True if a and b have identical IEEE-754 double bit patterns --
    distinguishes -0.0 from 0.0, unlike Python's == operator."""
    return struct.pack(">d", a) == struct.pack(">d", b)


def _try_pack(fmt: str, f: float) -> bytes | None:
    try:
        packed = struct.pack(fmt, f)
    except (OverflowError, struct.error):
        return None
    back = struct.unpack(fmt, packed)[0]
    if back != back and f != f:  # both NaN
        return packed
    if _same_bits(float(back), f):
        return packed
    return None


def _float_to_pinned(f: float) -> PinnedFloat:
    if f != f:  # NaN
        return PinnedFloat(bytes.fromhex("f97e00"))  # P6
    packed = _try_pack(">e", f)  # f16
    if packed is not None:
        return PinnedFloat(b"\xf9" + packed)
    packed = _try_pack(">f", f)  # f32
    if packed is not None:
        return PinnedFloat(b"\xfa" + packed)
    return PinnedFloat(b"\xfb" + struct.pack(">d", f))  # f64, always round-trips


def _parse_float_literal(literal: str) -> float:
    if literal == "NaN":
        return float("nan")
    if literal == "Infinity":
        return float("inf")
    if literal == "-Infinity":
        return float("-inf")
    return float(Decimal(literal))


_WIDTH_FORMATS = {"f16": ">e", "f32": ">f", "f64": ">d"}
_WIDTH_PREFIXES = {"f16": b"\xf9", "f32": b"\xfa", "f64": b"\xfb"}


def _float_to_pinned_width(f: float, width: str) -> PinnedFloat:
    if f != f:  # NaN always uses the P6 pinned payload regardless of width
        return PinnedFloat(bytes.fromhex("f97e00"))
    fmt = _WIDTH_FORMATS[width]
    packed = _try_pack(fmt, f)
    if packed is None:
        raise ValueError(
            f"value {f!r} cannot round-trip at requested float width {width!r}"
        )
    return PinnedFloat(_WIDTH_PREFIXES[width] + packed)


def _key_sort_bytes(key_obj) -> bytes:
    # RFC 8949 SS4.2.1: map keys are ordered by pure bytewise lexicographic
    # comparison of each key's deterministic encoding -- no length pre-sort.
    # A single scalar key's encoding is already deterministic/minimal without
    # needing cbor2's canonical=True (which only changes map-key *ordering*,
    # not per-value encoding).
    return cbor2.dumps(key_obj, default=_pinned_default)


def _to_python(value: dict):
    t = value["type"]
    if t == "int":
        return int(value["value"])
    if t == "float":
        width = value.get("width", "auto")
        parsed = _parse_float_literal(value["value"])
        if width == "auto":
            return _float_to_pinned(parsed)
        return _float_to_pinned_width(parsed, width)
    if t == "text":
        return value["value"]
    if t == "bytes":
        return bytes.fromhex(value["value"])
    if t == "bool":
        return value["value"]
    if t == "null":
        return None
    if t == "array":
        return [_to_python(item) for item in value["items"]]
    if t == "map":
        pairs = [(_to_python(k), _to_python(v)) for k, v in value["entries"]]
        pairs.sort(key=lambda pair: _key_sort_bytes(pair[0]))
        return dict(pairs)
    if t == "tag":
        return cbor2.CBORTag(value["tag"], _to_python(value["value"]))
    if t == "bignum":
        magnitude = int(value["value"])
        if value["sign"] == "positive":
            # A native CBOR major-type-0 integer already covers magnitudes
            # up to 2**64 - 1; tag 2 is reserved for magnitude >= 2**64.
            if magnitude < 2**64:
                raise ValueError(
                    f"bignum magnitude {magnitude} fits in a native CBOR "
                    "integer and must not be encoded as tag 2 (SPEC.md "
                    "bignum rule)"
                )
            raw_int = magnitude
            tag = 2
        else:
            # RFC 8949 SS3.4.3: tag 3's payload encodes n = magnitude - 1
            # (the represented value is -1-n), not the raw magnitude. Native
            # major-type-1 integers already cover represented values down to
            # -2**64 (i.e. magnitude up to and including 2**64); tag 3 is
            # reserved for magnitude > 2**64 - 1, i.e. magnitude >= 2**64.
            if magnitude < 2**64:
                raise ValueError(
                    f"bignum magnitude {magnitude} (negative) fits in a "
                    "native CBOR integer and must not be encoded as tag 3 "
                    "(SPEC.md bignum rule)"
                )
            raw_int = magnitude - 1
            tag = 3
        raw = raw_int.to_bytes((raw_int.bit_length() + 7) // 8 or 1, "big")
        return cbor2.CBORTag(tag, raw)
    raise ValueError(f"unknown logical-value type: {t!r}")


def encode_rfc8949(value: dict) -> bytes:
    py_value = _to_python(value)
    # Map keys are pre-sorted (bytewise, per RFC 8949) by _to_python above, so
    # canonical=True must NOT be used here -- it would re-sort map keys using
    # cbor2's own length-first (RFC 7049) rule, undoing the correct order.
    return cbor2.dumps(py_value, default=_pinned_default)
