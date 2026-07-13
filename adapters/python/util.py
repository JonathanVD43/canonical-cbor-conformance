"""Hex helpers. Python's stdlib bytes.hex()/bytes.fromhex() already lowercase
on encode and reject odd-length/non-hex input on decode, so no hand-rolled
hex codec is needed here (same rationale as the Go adapter's util.go, which
notes the Kotlin/TS ports needed one because their runtimes had no
equivalent stdlib helper)."""
from errors import EncodeError


def hex_encode(b: bytes) -> str:
    return b.hex()


def hex_decode_strict(s: str) -> bytes:
    try:
        return bytes.fromhex(s)
    except ValueError as e:
        raise EncodeError(f"invalid hex string {s!r}: {e}") from e
