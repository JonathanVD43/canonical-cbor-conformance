// Hand-rolled IEEE-754 binary16 (f16) <-> binary64 (f64) bit-level conversion.
// No dependency-free library does this in JS; ported from the Kotlin adapter.
// Intermediate 64-bit bit manipulation uses bigint to avoid precision loss.

/**
 * Extract raw 64-bit bits from a double without any rounding/truncation.
 * Uses DataView to access the underlying binary representation.
 */
function doubleToBigUint64(value: number): bigint {
  const view = new DataView(new ArrayBuffer(8));
  view.setFloat64(0, value, false); // false = big-endian
  return view.getBigUint64(0, false);
}

/**
 * Reconstruct a double from raw 64-bit bits.
 */
function bigUint64ToDouble(bits: bigint): number {
  const view = new DataView(new ArrayBuffer(8));
  view.setBigUint64(0, bits, false); // false = big-endian
  return view.getFloat64(0, false);
}

/**
 * Compare two doubles for raw-bit equality (handles NaN and -0.0 correctly).
 * NaN raw bits never equal each other (mirrors Rust behavior).
 */
function sameBits(a: number, b: number): boolean {
  return doubleToBigUint64(a) === doubleToBigUint64(b);
}

/**
 * Convert a 64-bit double to 16-bit float bits (0-65535).
 * Implements round-to-nearest-even semantics.
 */
export function doubleToF16Bits(value: number): number {
  const x = doubleToBigUint64(value);
  const sign = (x >> 63n) & 1n;
  const exp = (x >> 52n) & 0x7ffn;
  const man = x & 0xfffffffffffffn;

  if (exp === 0x7ffn) {
    // Infinity or NaN.
    const newMan = man >> 42n;
    const nanBit = man !== 0n ? 1n << 9n : 0n;
    return Number(((sign << 15n) | 0x7c00n | newMan | nanBit) & 0xffffn);
  }

  const halfSignBits = sign << 15n;
  const halfExp = exp - 1023n + 15n;

  if (halfExp >= 0x1fn) {
    return Number((halfSignBits | 0x7c00n) & 0xffffn);
  }

  if (halfExp <= 0n) {
    if (10n - halfExp > 21n) {
      // Underflows even a subnormal half: signed zero.
      return Number(halfSignBits & 0xffffn);
    }
    const manWithHidden = man | 0x10000000000000n;
    let halfMan = manWithHidden >> (43n - halfExp);
    const roundBit = 1n << (42n - halfExp);
    if ((manWithHidden & roundBit) !== 0n && (manWithHidden & (3n * roundBit - 1n)) !== 0n) {
      halfMan += 1n;
    }
    return Number((halfSignBits | halfMan) & 0xffffn);
  }

  const halfExpBits = halfExp << 10n;
  const halfMan = man >> 42n;
  const roundBit = 1n << 41n;
  const combined = halfSignBits | halfExpBits | halfMan;
  const roundUp = (man & roundBit) !== 0n && (man & (3n * roundBit - 1n)) !== 0n;
  return Number((roundUp ? combined + 1n : combined) & 0xffffn);
}

/**
 * Convert 16-bit float bits back to a 64-bit double.
 */
export function f16BitsToDouble(bits: number): number {
  const i = BigInt(bits) & 0xffffn;
  const sign = (i >> 15n) & 1n;
  const exp = (i >> 10n) & 0x1fn;
  const man = i & 0x3ffn;
  const sign64 = sign << 63n;

  if (exp === 0n && man === 0n) {
    // Zero (with sign).
    return bigUint64ToDouble(sign64);
  }

  if (exp === 0n) {
    // Subnormal.
    let expAdj = -1n;
    let manAdj = man;
    while ((manAdj & 0x400n) === 0n) {
      manAdj = manAdj << 1n;
      expAdj -= 1n;
    }
    manAdj = manAdj & 0x3ffn;
    const exp64 = 1023n - 15n + expAdj;
    const man64 = manAdj << 42n;
    return bigUint64ToDouble(sign64 | (exp64 << 52n) | man64);
  }

  if (exp === 0x1fn) {
    // Infinity or NaN.
    if (man === 0n) {
      return bigUint64ToDouble(sign64 | 0x7ff0000000000000n);
    } else {
      return bigUint64ToDouble(sign64 | 0x7ff0000000000000n | (man << 42n));
    }
  }

  // Normal.
  const exp64 = exp - 15n + 1023n;
  const man64 = man << 42n;
  return bigUint64ToDouble(sign64 | (exp64 << 52n) | man64);
}

/**
 * Attempt to encode a double as f16, returning bytes only if it round-trips exactly.
 * Returns 2-byte big-endian Uint8Array or null.
 * Callers must handle NaN before reaching this (sameBits never matches NaN to NaN).
 */
export function tryF16(f: number): Uint8Array | null {
  const bits = doubleToF16Bits(f);
  const back = f16BitsToDouble(bits);
  if (!sameBits(back, f)) return null;
  return new Uint8Array([(bits >>> 8) & 0xff, bits & 0xff]);
}

/**
 * Attempt to encode a double as f32, returning bytes only if it round-trips exactly.
 * Returns 4-byte big-endian Uint8Array or null.
 */
export function tryF32(f: number): Uint8Array | null {
  const v = Math.fround(f); // Truncate to float32
  const back = v; // fround result, when coerced to double, is the f32 round-trip
  if (!sameBits(back, f)) return null;

  // Get raw 32-bit bits
  const view = new DataView(new ArrayBuffer(4));
  view.setFloat32(0, v, false); // false = big-endian
  const bits = view.getUint32(0, false);

  return new Uint8Array([
    (bits >>> 24) & 0xff,
    (bits >>> 16) & 0xff,
    (bits >>> 8) & 0xff,
    bits & 0xff,
  ]);
}
