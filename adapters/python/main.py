#!/usr/bin/env python3
"""CLI entry point + batch stdin/stdout protocol (CONTRIBUTING.md's adapter
contract), ported from adapters/go/main.go (itself cross-checked against
adapters/kotlin, adapters/rust, adapters/typescript)."""
import sys

from decode import decode_strict
from dcbor import encode_dcbor
from errors import EncodeError, ParseError
from logical_value import parse_logical_value_line
from rfc8949 import encode_rfc8949
from util import hex_decode_strict, hex_encode


def run(args) -> int:
    if len(args) < 1:
        print("usage: adapter <mode> --profile <profile>", file=sys.stderr)
        return 2
    profile = _parse_profile_arg(args)

    if args[0] == "encode":
        return _run_encode(profile)
    if args[0] == "decode-strict":
        return _run_decode_strict(profile)
    print(f"unknown mode: {args[0]}", file=sys.stderr)
    return 2


def _parse_profile_arg(args):
    for i, a in enumerate(args):
        if a == "--profile" and i + 1 < len(args):
            return args[i + 1]
    return None


def _for_each_line(fn) -> bool:
    """Reads stdin line by line, skipping blank lines, calling fn for each
    non-blank stripped line. Returns True only on a genuine stdin read
    failure (mirrors the reference adapters' IOException handling -> exit
    2)."""
    try:
        for raw_line in sys.stdin:
            line = raw_line.strip()
            if not line:
                continue
            fn(line)
        return False
    except OSError:
        return True


class _StdoutWriter:
    """Tracks the first write error, the same way the Kotlin adapter checks
    PrintStream.checkError() after every write to detect a stdout failure
    and exit 2."""

    def __init__(self):
        self.had_error = False

    def println(self, line: str) -> None:
        if self.had_error:
            return
        try:
            sys.stdout.write(line)
            sys.stdout.write("\n")
            sys.stdout.flush()
        except OSError:
            self.had_error = True


def _run_encode(profile) -> int:
    if profile is None:
        print("--profile is required", file=sys.stderr)
        return 2
    if profile == "rfc8949":
        encoder = encode_rfc8949
    elif profile == "dcbor":
        encoder = encode_dcbor
    else:
        print(f"unsupported profile: {profile}", file=sys.stderr)
        return 3

    had_error = False
    out = _StdoutWriter()

    def handle(line: str) -> None:
        nonlocal had_error
        try:
            logical = parse_logical_value_line(line)
        except ParseError as e:
            print(f"malformed input line: {e}", file=sys.stderr)
            had_error = True
            out.println("")
            return
        try:
            encoded = encoder(logical)
        except EncodeError as e:
            print(f"encode rejected: {e}", file=sys.stderr)
            had_error = True
            out.println("")
            return
        out.println(hex_encode(encoded))

    read_failed = _for_each_line(handle)
    if out.had_error:
        print("internal adapter error: failed to write stdout", file=sys.stderr)
        return 2
    if read_failed:
        print("internal adapter error: failed to read stdin", file=sys.stderr)
        return 2
    return 1 if had_error else 0


def _run_decode_strict(profile) -> int:
    if profile is None:
        print("--profile is required", file=sys.stderr)
        return 2
    if profile not in ("rfc8949", "dcbor"):
        print(f"unsupported profile: {profile}", file=sys.stderr)
        return 3

    had_error = False
    out = _StdoutWriter()

    def handle(line: str) -> None:
        nonlocal had_error
        try:
            data = hex_decode_strict(line)
        except EncodeError as e:
            print(f"malformed input line: {e}", file=sys.stderr)
            had_error = True
            out.println("")
            return
        try:
            verdict = decode_strict(data, profile)
        except Exception as e:  # noqa: BLE001 - internal decode failure, not a crash
            print(f"decode-strict internal error: {e}", file=sys.stderr)
            had_error = True
            out.println("")
            return
        if verdict.accept:
            out.println("ACCEPT " + hex_encode(verdict.bytes_out))
        else:
            out.println("REJECT " + verdict.reason)

    read_failed = _for_each_line(handle)
    if out.had_error:
        print("internal adapter error: failed to write stdout", file=sys.stderr)
        return 2
    if read_failed:
        print("internal adapter error: failed to read stdin", file=sys.stderr)
        return 2
    return 1 if had_error else 0


def main() -> None:
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(run(sys.argv[1:]))


if __name__ == "__main__":
    main()
