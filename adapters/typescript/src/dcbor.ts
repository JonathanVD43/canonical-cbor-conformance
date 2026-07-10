/**
 * dCBOR deterministic-encoding profile ("D" rules in SPEC.md).
 * Faithful port of adapters/kotlin/src/main/kotlin/Dcbor.kt.
 */

import { hexDecode, hexEncode } from "./util.ts";
import { tryF16, tryF32 } from "./float16.ts";
import {
  encodeHead,
  encodeInt,
  compareBytesUnsigned,
  bignumTagAndBytes,
  doubleToBeBytes,
  EncodeError,
} from "./rfc8949.ts";
import type { LogicalValue } from "./logicalValue.ts";

export { EncodeError };

const TWO_POW_64 = 1n << 64n;

function concatBytes(...parts: Uint8Array[]): Uint8Array {
  const total = parts.reduce((sum, p) => sum + p.length, 0);
  const out = new Uint8Array(total);
  let offset = 0;
  for (const p of parts) {
    out.set(p, offset);
    offset += p.length;
  }
  return out;
}

function encodeFloatDcbor(literal: string): Uint8Array {
  const f = Number(literal);
  if (Number.isNaN(f) && literal.trim().toLowerCase() !== "nan") {
    throw new EncodeError(`float: invalid literal "${literal}"`);
  }
  if (Number.isNaN(f)) {
    // D6: NaN always uses the single canonical f16 payload.
    return new Uint8Array([0xf9, 0x7e, 0x00]);
  }
  if (Number.isFinite(f) && f === Math.floor(f)) {
    // D5/D7: a float with no fractional part (including -0.0) reduces to
    // the shortest-int form, unifying with the plain integer encoding.
    const big = BigInt(f); // truncation is safe: f === floor(f), and BigInt(-0) === 0n
    if (big >= 0n) {
      if (big < TWO_POW_64) return encodeHead(0, big);
    } else {
      const arg = -1n - big;
      if (arg < TWO_POW_64) return encodeHead(1, arg);
    }
    // Falls through to shortest-float form below if it doesn't fit the
    // native int range (no dcbor vector exercises this edge).
  }
  const f16 = tryF16(f);
  if (f16) return concatBytes(new Uint8Array([0xf9]), f16);
  const f32 = tryF32(f);
  if (f32) return concatBytes(new Uint8Array([0xfa]), f32);
  return concatBytes(new Uint8Array([0xfb]), doubleToBeBytes(f));
}

function encodeBignumDcbor(sign: "positive" | "negative", value: string): Uint8Array {
  const { tag, bytes } = bignumTagAndBytes(sign, value);
  return concatBytes(
    encodeHead(6, BigInt(tag)),
    encodeHead(2, BigInt(bytes.length)),
    bytes,
  );
}

export function encodeDcbor(value: LogicalValue): Uint8Array {
  switch (value.type) {
    case "int":
      return encodeInt(value.value);
    case "float":
      // dcbor's float encoding ignores `width` entirely; always the D5/D6 cascade.
      return encodeFloatDcbor(value.value);
    case "text": {
      // D8: normalize to NFC before UTF-8 encoding.
      const bytes = new TextEncoder().encode(value.value.normalize("NFC"));
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
      for (const item of value.items) parts.push(encodeDcbor(item));
      return concatBytes(...parts);
    }
    case "map": {
      // dCBOR's reference encoder stores entries in a real map keyed by
      // canonical-encoded bytes, so two logical entries that canonicalize to
      // the same key (e.g. via D7 zero unification or D8 NFC normalization)
      // collapse to one, last write wins. Mirror that with an
      // insertion-ordered map keyed by hex(encodedKey).
      const dedup = new Map<string, { keyBytes: Uint8Array; valueBytes: Uint8Array }>();
      for (const [k, v] of value.entries) {
        const encodedKey = encodeDcbor(k);
        dedup.set(hexEncode(encodedKey), { keyBytes: encodedKey, valueBytes: encodeDcbor(v) });
      }
      const sorted = [...dedup.values()].sort((a, b) =>
        compareBytesUnsigned(a.keyBytes, b.keyBytes),
      );
      const parts = [encodeHead(5, BigInt(sorted.length))];
      for (const { keyBytes, valueBytes } of sorted) {
        parts.push(keyBytes);
        parts.push(valueBytes);
      }
      return concatBytes(...parts);
    }
    case "tag":
      return concatBytes(encodeHead(6, value.tag), encodeDcbor(value.value));
    case "bignum":
      return encodeBignumDcbor(value.sign, value.value);
  }
}
