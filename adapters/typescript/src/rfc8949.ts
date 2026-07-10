/**
 * RFC 8949 §4.2.1 deterministic-encoding profile ("P" rules in SPEC.md).
 * Faithful port of adapters/kotlin/src/main/kotlin/Rfc8949.kt.
 */

import { hexDecode } from "./util.ts";
import { tryF16, tryF32 } from "./float16.ts";
import type { LogicalValue } from "./logicalValue.ts";

export class EncodeError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "EncodeError";
  }
}

export const TWO_POW_64 = 1n << 64n;

/** Convert a non-negative bigint magnitude to a big-endian byte array of exactly `width` bytes. */
function bigIntToBytes(v: bigint, width: number): Uint8Array {
  const out = new Uint8Array(width);
  let n = v;
  for (let i = width - 1; i >= 0; i--) {
    out[i] = Number(n & 0xffn);
    n >>= 8n;
  }
  return out;
}

/** Convert a non-negative bigint to the minimal big-endian byte array (at least 1 byte). */
function bigIntToMinimalBytes(v: bigint): Uint8Array {
  if (v === 0n) return new Uint8Array([0]);
  let hex = v.toString(16);
  if (hex.length % 2 !== 0) hex = "0" + hex;
  return hexDecode(hex);
}

export function concatBytes(...parts: Uint8Array[]): Uint8Array {
  const total = parts.reduce((sum, p) => sum + p.length, 0);
  const out = new Uint8Array(total);
  let offset = 0;
  for (const p of parts) {
    out.set(p, offset);
    offset += p.length;
  }
  return out;
}

export function encodeHead(majorType: number, arg: bigint): Uint8Array {
  const mt = majorType << 5;
  if (arg < 24n) {
    return new Uint8Array([mt | Number(arg)]);
  } else if (arg <= 0xffn) {
    return concatBytes(new Uint8Array([mt | 24]), bigIntToBytes(arg, 1));
  } else if (arg <= 0xffffn) {
    return concatBytes(new Uint8Array([mt | 25]), bigIntToBytes(arg, 2));
  } else if (arg <= 0xffffffffn) {
    return concatBytes(new Uint8Array([mt | 26]), bigIntToBytes(arg, 4));
  } else {
    return concatBytes(new Uint8Array([mt | 27]), bigIntToBytes(arg, 8));
  }
}

/** Not exported-private: reused by Dcbor.ts (same shortest-int rule applies to both profiles). */
export function encodeInt(value: string): Uint8Array {
  let n: bigint;
  try {
    n = BigInt(value);
  } catch (e) {
    throw new EncodeError(
      `int: invalid integer literal "${value}": ${e instanceof Error ? e.message : String(e)}`,
    );
  }
  if (n >= 0n) {
    if (n >= TWO_POW_64) throw new EncodeError(`int: ${n} exceeds native range`);
    return encodeHead(0, n);
  } else {
    const arg = -1n - n;
    if (arg >= TWO_POW_64) throw new EncodeError(`int: ${n} exceeds native range`);
    return encodeHead(1, arg);
  }
}

/** Reused by Dcbor.ts. */
export function doubleToBeBytes(f: number): Uint8Array {
  const view = new DataView(new ArrayBuffer(8));
  view.setFloat64(0, f, false);
  return new Uint8Array(view.buffer);
}

function parseFloatLiteral(literal: string): number {
  const f = Number(literal);
  if (Number.isNaN(f) && literal.trim().toLowerCase() !== "nan") {
    throw new EncodeError(`float: invalid literal "${literal}"`);
  }
  return f;
}

function encodeFloat(width: "auto" | "f16" | "f32" | "f64", literal: string): Uint8Array {
  const f = parseFloatLiteral(literal);
  if (Number.isNaN(f)) {
    // P6: NaN always uses the single canonical f16 payload, regardless of width.
    return new Uint8Array([0xf9, 0x7e, 0x00]);
  }
  if (width === "auto") {
    const f16 = tryF16(f);
    if (f16) return concatBytes(new Uint8Array([0xf9]), f16);
    const f32 = tryF32(f);
    if (f32) return concatBytes(new Uint8Array([0xfa]), f32);
    return concatBytes(new Uint8Array([0xfb]), doubleToBeBytes(f));
  }
  switch (width) {
    case "f16": {
      const bytes = tryF16(f);
      if (!bytes) throw new EncodeError(`float: ${f} cannot round-trip at requested width f16`);
      return concatBytes(new Uint8Array([0xf9]), bytes);
    }
    case "f32": {
      const bytes = tryF32(f);
      if (!bytes) throw new EncodeError(`float: ${f} cannot round-trip at requested width f32`);
      return concatBytes(new Uint8Array([0xfa]), bytes);
    }
    case "f64":
      return concatBytes(new Uint8Array([0xfb]), doubleToBeBytes(f));
    default:
      throw new EncodeError(`float: unknown width "${width}"`);
  }
}

/**
 * Shared with the dcbor profile: bignum tag+magnitude rules are identical in
 * both profiles, so both encoders build on this single validated computation.
 */
export function bignumTagAndBytes(
  sign: "positive" | "negative",
  value: string,
): { tag: number; bytes: Uint8Array } {
  let magnitude: bigint;
  try {
    magnitude = BigInt(value);
  } catch (e) {
    throw new EncodeError(
      `bignum: invalid magnitude "${value}": ${e instanceof Error ? e.message : String(e)}`,
    );
  }
  const tag = sign === "positive" ? 2 : 3;
  if (magnitude < TWO_POW_64) {
    throw new EncodeError(
      `bignum magnitude ${magnitude} fits in a native CBOR integer and must not be encoded as tag ${tag} (SPEC.md bignum rule)`,
    );
  }
  let rawInt: bigint;
  if (sign === "positive") {
    rawInt = magnitude;
  } else if (sign === "negative") {
    rawInt = magnitude - 1n;
  } else {
    throw new EncodeError(`bignum: unknown sign "${sign}"`);
  }
  const bytes = bigIntToMinimalBytes(rawInt);
  return { tag, bytes };
}

function encodeBignum(sign: "positive" | "negative", value: string): Uint8Array {
  const { tag, bytes } = bignumTagAndBytes(sign, value);
  return concatBytes(
    encodeHead(6, BigInt(tag)),
    encodeHead(2, BigInt(bytes.length)),
    bytes,
  );
}

/** Reused by Dcbor.ts (same bytewise map-key sort rule applies to both profiles). */
export function compareBytesUnsigned(a: Uint8Array, b: Uint8Array): number {
  const len = Math.min(a.length, b.length);
  for (let i = 0; i < len; i++) {
    const ai = a[i];
    const bi = b[i];
    if (ai !== bi) return ai - bi;
  }
  return a.length - b.length;
}

export function encodeRfc8949(value: LogicalValue): Uint8Array {
  switch (value.type) {
    case "int":
      return encodeInt(value.value);
    case "text": {
      const bytes = new TextEncoder().encode(value.value);
      return concatBytes(encodeHead(3, BigInt(bytes.length)), bytes);
    }
    case "bytes": {
      const bytes = hexDecode(value.value);
      return concatBytes(encodeHead(2, BigInt(bytes.length)), bytes);
    }
    case "bool":
      return new Uint8Array([value.value ? 0xf5 : 0xf4]);
    case "null":
      return new Uint8Array([0xf6]);
    case "array": {
      const parts = [encodeHead(4, BigInt(value.items.length))];
      for (const item of value.items) parts.push(encodeRfc8949(item));
      return concatBytes(...parts);
    }
    case "float":
      return encodeFloat(value.width, value.value);
    case "map": {
      const encoded = value.entries.map(
        ([k, v]) => [encodeRfc8949(k), encodeRfc8949(v)] as [Uint8Array, Uint8Array],
      );
      const sorted = encoded
        .map((pair, index) => ({ pair, index }))
        .sort((a, b) => {
          const cmp = compareBytesUnsigned(a.pair[0], b.pair[0]);
          return cmp !== 0 ? cmp : a.index - b.index;
        })
        .map((x) => x.pair);
      const parts = [encodeHead(5, BigInt(value.entries.length))];
      for (const [k, v] of sorted) {
        parts.push(k);
        parts.push(v);
      }
      return concatBytes(...parts);
    }
    case "tag":
      return concatBytes(encodeHead(6, value.tag), encodeRfc8949(value.value));
    case "bignum":
      return encodeBignum(value.sign, value.value);
  }
}
