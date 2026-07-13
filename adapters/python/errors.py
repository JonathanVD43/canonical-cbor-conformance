"""Shared exception types used across the adapter's modules."""


class ParseError(Exception):
    """Raised for any malformed logical-value JSON line (logical_value.py)."""


class EncodeError(Exception):
    """Raised when a logical value cannot be encoded under the requested
    profile (rfc8949.py / dcbor.py), e.g. a bignum magnitude that fits the
    native 64-bit range, or an explicit float width that can't round-trip."""


class DecodeSignal(Exception):
    """Raised during decode-strict parsing (decode.py). The message is
    either one of SPEC.md's reason codes (e.g. "UNSORTED_MAP_KEYS") or an
    "internal: ..." diagnostic for malformed/truncated input not covered by
    any reason code. decode_strict() inspects the message to tell the two
    apart."""
