#!/usr/bin/env python3
# tests/fixtures/stub_adapter.py
"""Trivial stand-in adapter for unit-testing harness logic without needing
a Rust build. For `encode --profile <anything>`, echoes back a fixed hex
string per input line: even-numbered lines (0-indexed) succeed with "aa",
odd-numbered lines fail with a stderr message and an empty stdout line
(to keep output line count aligned with input line count)."""
import sys


def main():
    lines = [line for line in sys.stdin.read().split("\n") if line.strip()]
    had_error = False
    for i, _line in enumerate(lines):
        if i % 2 == 0:
            print("aa")
        else:
            print("")  # empty line to stdout to maintain alignment
            print(f"stub failure on line {i}", file=sys.stderr)
            had_error = True
    sys.exit(1 if had_error else 0)


if __name__ == "__main__":
    main()
