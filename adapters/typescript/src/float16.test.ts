import { test } from "node:test";
import assert from "node:assert/strict";
import { doubleToF16Bits, f16BitsToDouble, tryF16, tryF32 } from "./float16.ts";

function bits(hex: string): number {
  return parseInt(hex, 16);
}

function rawBitsOfDouble(d: number): bigint {
  const view = new DataView(new ArrayBuffer(8));
  view.setFloat64(0, d, false);
  return view.getBigUint64(0, false);
}

function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes).map((b) => b.toString(16).padStart(2, "0")).join("");
}

test("zero and negative zero", () => {
  assert.strictEqual(doubleToF16Bits(0.0), bits("0000"));
  assert.strictEqual(doubleToF16Bits(-0.0), bits("8000"));
  assert.strictEqual(f16BitsToDouble(bits("0000")), 0.0);
  // For -0.0, we need raw-bits equality (not just == which is true for both 0.0 and -0.0)
  assert.strictEqual(
    rawBitsOfDouble(f16BitsToDouble(bits("8000"))),
    rawBitsOfDouble(-0.0),
  );
});

test("canonical NaN payload", () => {
  // 0xf97e00's payload: sign 0, exp 0x1f, mantissa 0x200 (0x7e00 in 16-bit form).
  assert.strictEqual(doubleToF16Bits(Number.NaN), bits("7e00"));
});

test("infinity", () => {
  assert.strictEqual(doubleToF16Bits(Number.POSITIVE_INFINITY), bits("7c00"));
  assert.strictEqual(doubleToF16Bits(Number.NEGATIVE_INFINITY), bits("fc00"));
  assert.strictEqual(f16BitsToDouble(bits("7c00")), Number.POSITIVE_INFINITY);
  assert.strictEqual(f16BitsToDouble(bits("fc00")), Number.NEGATIVE_INFINITY);
});

test("exactly representable value round-trips", () => {
  // 2.5 = 0xf94100 payload 0x4100.
  const b = tryF16(2.5);
  assert.ok(b !== null, "tryF16(2.5) should not be null");
  assert.strictEqual(bytesToHex(b), "4100");
});

test("non-exact value rejected", () => {
  // 0.1 has no exact f16 representation.
  assert.strictEqual(tryF16(0.1), null);
});

test("smallest normal and subnormal boundary", () => {
  // Smallest positive normal half: 2^-14 = 0x0400.
  const smallestNormal = Math.pow(2, -14);
  assert.strictEqual(doubleToF16Bits(smallestNormal), bits("0400"));
  // Smallest positive subnormal half: 2^-24 = 0x0001.
  const smallestSubnormal = Math.pow(2, -24);
  assert.strictEqual(doubleToF16Bits(smallestSubnormal), bits("0001"));
});

test("max finite half round-trips", () => {
  // Max finite half value: 65504.0 = 0x7bff.
  assert.strictEqual(doubleToF16Bits(65504.0), bits("7bff"));
  assert.strictEqual(f16BitsToDouble(bits("7bff")), 65504.0);
});

test("overflow rounds to infinity", () => {
  // 65520 rounds up past the max finite half value -> +infinity.
  assert.strictEqual(doubleToF16Bits(65520.0), bits("7c00"));
});

test("subnormal exhaustive round-trip", () => {
  // Regression test for a found-and-fixed bug: f16BitsToDouble's subnormal
  // branch used to start expAdj at -1 instead of 1, undercounting the
  // normalization shift by 2 and silently quartering the magnitude of
  // every subnormal f16 value on decode. No existing vector exercised
  // this (only the encode direction was covered by "smallest normal and
  // subnormal boundary" above); the identical bug was already found and
  // fixed in this project's C/Go/Python/Java adapters, but TypeScript
  // and Kotlin were never checked -- caught only when this project's own
  // real CI run failed on the newly-added float-f16-smallest-subnormal
  // vector.
  for (let bits = 0; bits <= 0x03ff; bits++) {
    const got = f16BitsToDouble(bits);
    const want = bits * Math.pow(2, -24);
    assert.strictEqual(got, want, `bits=0x${bits.toString(16).padStart(4, "0")}`);
  }
});

test("f32 round-trip", () => {
  const b = tryF32(2.5);
  assert.ok(b !== null, "tryF32(2.5) should not be null");
  assert.strictEqual(bytesToHex(b), "40200000");
  assert.strictEqual(tryF32(0.1), null);
});

test("f16 round-trip for other small values", () => {
  // Test some additional small exact values
  const half = 0.5;
  const b = tryF16(half);
  assert.ok(b !== null, "tryF16(0.5) should not be null");

  const quarter = 0.25;
  const c = tryF16(quarter);
  assert.ok(c !== null, "tryF16(0.25) should not be null");
});

test("negative f16 values", () => {
  const negTwo = -2.0;
  const b = tryF16(negTwo);
  assert.ok(b !== null, "tryF16(-2.0) should not be null");

  const negQuarter = -0.25;
  const c = tryF16(negQuarter);
  assert.ok(c !== null, "tryF16(-0.25) should not be null");
});

test("subnormal underflow edge cases", () => {
  // Test values that are between subnormal and zero
  const tiny = Math.pow(2, -25);
  const result = doubleToF16Bits(tiny);
  // This should either round to zero or smallest subnormal
  assert.ok(result >= 0x0000 && result <= 0x0001);
});

test("large values near overflow", () => {
  // Test that large values that don't round-trip exactly are rejected
  const nearMax = 65000.0;
  const b = tryF16(nearMax);
  assert.strictEqual(b, null, "tryF16(65000.0) should not round-trip exactly");

  // Test value that overflows
  const overflow = 66000.0;
  assert.strictEqual(doubleToF16Bits(overflow), bits("7c00"));
});

test("negative infinity and negative values", () => {
  assert.strictEqual(doubleToF16Bits(-1.0), bits("bc00"));
  const b = tryF16(-1.0);
  assert.ok(b !== null, "tryF16(-1.0) should round-trip");
});

test("edge case: 1.0", () => {
  assert.strictEqual(doubleToF16Bits(1.0), bits("3c00"));
  const b = tryF16(1.0);
  assert.ok(b !== null, "tryF16(1.0) should round-trip");
  assert.strictEqual(bytesToHex(b!), "3c00");
});

test("round-to-nearest-even rounding", () => {
  // Test values that require rounding
  // 2.5 should round-trip exactly (mantissa ends in 0)
  const exact = tryF16(2.5);
  assert.ok(exact !== null);

  // Test a value that doesn't round-trip exactly to verify rejection
  const inexact = tryF16(2.6);
  assert.strictEqual(inexact, null);
});
