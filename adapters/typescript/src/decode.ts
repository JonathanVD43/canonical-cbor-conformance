/**
 * Strict canonical-CBOR decoder ("decode-strict" mode per SPEC.md).
 * Faithful port of adapters/kotlin/src/main/kotlin/Decode.kt.
 */

import { hexEncode } from "./util.ts";
import { f16BitsToDouble, doubleToF16Bits } from "./float16.ts";
import { encodeRfc8949, EncodeError } from "./rfc8949.ts";
import { encodeDcbor } from "./dcbor.ts";
import type { LogicalValue } from "./logicalValue.ts";

export type Profile = "rfc8949" | "dcbor";

export class DecodeException extends Error {
  reasonOrMessage: string;
  constructor(reasonOrMessage: string) {
    super(reasonOrMessage);
    this.name = "DecodeException";
    this.reasonOrMessage = reasonOrMessage;
  }
}

const REASON_CODES = new Set([
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
]);

// Placeholder allow-list for the UNKNOWN_TAG check: only the bignum tags are
// exercised by the current vector corpus.
const ALLOWED_TAGS = new Set([2n, 3n]);

export type Verdict = { kind: "accept"; bytes: Uint8Array } | { kind: "reject"; reason: string };

class Cursor {
  pos: number;
  constructor(pos: number) {
    this.pos = pos;
  }
}

export function decodeStrict(input: Uint8Array, profile: Profile): Verdict {
  if (input.length === 0) throw new DecodeException("internal: empty input line");
  const cursor = new Cursor(0);
  let item: Item;
  try {
    item = parseItem(input, cursor, profile);
  } catch (e) {
    if (e instanceof DecodeException && REASON_CODES.has(e.reasonOrMessage)) {
      return { kind: "reject", reason: e.reasonOrMessage };
    }
    throw e;
  }

  if (cursor.pos < input.length) {
    const cursor2 = new Cursor(cursor.pos);
    try {
      parseItem(input, cursor2, profile);
      if (cursor2.pos === input.length) {
        return { kind: "reject", reason: "MULTIPLE_TOP_LEVEL_ITEMS" };
      } else {
        return { kind: "reject", reason: "TRAILING_BYTES" };
      }
    } catch (e) {
      return { kind: "reject", reason: "TRAILING_BYTES" };
    }
  }

  const logical = itemToLogical(item);
  let reencoded: Uint8Array;
  try {
    reencoded = profile === "rfc8949" ? encodeRfc8949(logical) : encodeDcbor(logical);
  } catch (e) {
    if (e instanceof EncodeError) {
      throw new DecodeException(`internal: canonical input failed to re-encode: ${e.message}`);
    }
    throw e;
  }
  return { kind: "accept", bytes: reencoded };
}

type Item =
  | { kind: "intPos"; v: bigint }
  | { kind: "intNeg"; v: bigint }
  | { kind: "bytes"; v: Uint8Array }
  | { kind: "text"; v: string }
  | { kind: "arr"; items: Item[] }
  | { kind: "mapEntries"; entries: [Item, Item][] }
  | { kind: "taggedItem"; tag: bigint; inner: Item }
  | { kind: "boolItem"; v: boolean }
  | { kind: "nullItem" }
  | { kind: "floatItem"; v: number };

type ArgWidth = "DIRECT" | "ONE" | "TWO" | "FOUR" | "EIGHT";

interface Head {
  major: number;
  arg: bigint;
  width: ArgWidth;
  indefinite: boolean;
}

function readHead(input: Uint8Array, cursor: Cursor): Head {
  if (cursor.pos >= input.length) {
    throw new DecodeException("internal: truncated input (expected an item header)");
  }
  const b0 = input[cursor.pos] & 0xff;
  cursor.pos += 1;
  const major = (b0 >>> 5) & 0x7;
  const info = b0 & 0x1f;
  if (info === 31) return { major, arg: 0n, width: "DIRECT", indefinite: true };

  let arg: bigint;
  let width: ArgWidth;
  if (info <= 23) {
    arg = BigInt(info);
    width = "DIRECT";
  } else if (info === 24) {
    if (cursor.pos >= input.length) throw new DecodeException("internal: truncated 1-byte argument");
    const b = input[cursor.pos] & 0xff;
    cursor.pos += 1;
    arg = BigInt(b);
    width = "ONE";
  } else if (info === 25) {
    if (cursor.pos + 2 > input.length) throw new DecodeException("internal: truncated 2-byte argument");
    let v = 0n;
    for (let i = 0; i < 2; i++) v = (v << 8n) | BigInt(input[cursor.pos + i] & 0xff);
    cursor.pos += 2;
    arg = v;
    width = "TWO";
  } else if (info === 26) {
    if (cursor.pos + 4 > input.length) throw new DecodeException("internal: truncated 4-byte argument");
    let v = 0n;
    for (let i = 0; i < 4; i++) v = (v << 8n) | BigInt(input[cursor.pos + i] & 0xff);
    cursor.pos += 4;
    arg = v;
    width = "FOUR";
  } else if (info === 27) {
    if (cursor.pos + 8 > input.length) throw new DecodeException("internal: truncated 8-byte argument");
    let v = 0n;
    for (let i = 0; i < 8; i++) v = (v << 8n) | BigInt(input[cursor.pos + i] & 0xff);
    cursor.pos += 8;
    arg = v;
    width = "EIGHT";
  } else {
    throw new DecodeException("internal: reserved additional-info value (28-30)");
  }
  return { major, arg, width, indefinite: false };
}

function shortestWidthFor(arg: bigint): ArgWidth {
  if (arg < 24n) return "DIRECT";
  if (arg <= 0xffn) return "ONE";
  if (arg <= 0xffffn) return "TWO";
  if (arg <= 0xffffffffn) return "FOUR";
  return "EIGHT";
}

// P1/D2: every integer *argument* (ints themselves, and string/array/map
// lengths, and tag numbers) must use the shortest encoding for its value.
function checkShortestArg(head: Head): void {
  if (head.width !== shortestWidthFor(head.arg)) throw new DecodeException("NON_SHORTEST_INT");
}

function strictUtf8Decode(bytes: Uint8Array): string {
  try {
    return new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } catch (e) {
    throw new DecodeException("internal: invalid utf-8 in text string");
  }
}

function parseItem(input: Uint8Array, cursor: Cursor, profile: Profile): Item {
  const head = readHead(input, cursor);

  if (head.major === 7) {
    if (head.indefinite) {
      throw new DecodeException("internal: unexpected break byte outside indefinite-length container");
    }
    return parseMajor7(head.width, head.arg, profile);
  }

  if (head.indefinite) {
    // P3/D1: indefinite-length arrays/maps/byte/text strings are banned
    // outright, regardless of what they'd otherwise contain.
    throw new DecodeException("INDEFINITE_LENGTH");
  }
  checkShortestArg(head);

  switch (head.major) {
    case 0:
      return { kind: "intPos", v: head.arg };
    case 1:
      return { kind: "intNeg", v: head.arg };
    case 2: {
      const len = Number(head.arg);
      if (cursor.pos + len > input.length) throw new DecodeException("internal: truncated byte string");
      const bytes = input.slice(cursor.pos, cursor.pos + len);
      cursor.pos += len;
      return { kind: "bytes", v: bytes };
    }
    case 3: {
      const len = Number(head.arg);
      if (cursor.pos + len > input.length) throw new DecodeException("internal: truncated text string");
      const bytes = input.slice(cursor.pos, cursor.pos + len);
      const s = strictUtf8Decode(bytes);
      cursor.pos += len;
      // D8: dcbor requires NFC-normalized text; rfc8949 has no such
      // rule (P9 is an explicit non-goal).
      if (profile === "dcbor") {
        const normalized = s.normalize("NFC");
        if (normalized !== s) throw new DecodeException("NON_NFC_STRING");
      }
      return { kind: "text", v: s };
    }
    case 4: {
      const len = Number(head.arg);
      const items: Item[] = [];
      for (let i = 0; i < len; i++) items.push(parseItem(input, cursor, profile));
      return { kind: "arr", items };
    }
    case 5: {
      const len = Number(head.arg);
      const entries: [Item, Item][] = [];
      const keyRanges: [number, number][] = [];
      for (let i = 0; i < len; i++) {
        const keyStart = cursor.pos;
        const key = parseItem(input, cursor, profile);
        const keyEnd = cursor.pos;
        const value = parseItem(input, cursor, profile);
        keyRanges.push([keyStart, keyEnd]);
        entries.push([key, value]);
      }
      // P2/D3: keys must appear in strictly increasing bytewise order
      // of their own raw encoded bytes. A duplicate key is necessarily
      // adjacent to itself in properly sorted order, so one adjacent
      // pass catches both violations.
      for (let i = 0; i < keyRanges.length - 1; i++) {
        const a = input.slice(keyRanges[i][0], keyRanges[i][1]);
        const b = input.slice(keyRanges[i + 1][0], keyRanges[i + 1][1]);
        const cmp = compareBytesUnsignedLocal(a, b);
        if (cmp === 0) throw new DecodeException("DUPLICATE_KEY");
        if (cmp > 0) throw new DecodeException("UNSORTED_MAP_KEYS");
      }
      return { kind: "mapEntries", entries };
    }
    case 6: {
      const tag = head.arg;
      if (!ALLOWED_TAGS.has(tag)) throw new DecodeException("UNKNOWN_TAG");
      const inner = parseItem(input, cursor, profile);
      return { kind: "taggedItem", tag, inner };
    }
    default:
      throw new Error("major type is 3 bits, always 0-7");
  }
}

function compareBytesUnsignedLocal(a: Uint8Array, b: Uint8Array): number {
  const len = Math.min(a.length, b.length);
  for (let i = 0; i < len; i++) {
    if (a[i] !== b[i]) return a[i] - b[i];
  }
  return a.length - b.length;
}

type FloatWidth = "F16" | "F32" | "F64";

function parseMajor7(width: ArgWidth, arg: bigint, profile: Profile): Item {
  switch (width) {
    case "DIRECT":
      if (arg === 20n) return { kind: "boolItem", v: false };
      if (arg === 21n) return { kind: "boolItem", v: true };
      if (arg === 22n) return { kind: "nullItem" };
      throw new DecodeException(`internal: unsupported major-7 simple value ${arg}`);
    case "ONE":
      throw new DecodeException("internal: unsupported major-7 1-byte simple value");
    case "TWO":
      return checkFloat(arg, "F16", profile);
    case "FOUR":
      return checkFloat(arg, "F32", profile);
    case "EIGHT":
      return checkFloat(arg, "F64", profile);
  }
}

function checkFloat(bits: bigint, width: FloatWidth, profile: Profile): Item {
  let value: number;
  switch (width) {
    case "F16":
      value = f16BitsToDouble(Number(bits));
      break;
    case "F32": {
      const view = new DataView(new ArrayBuffer(4));
      view.setUint32(0, Number(bits), false);
      value = view.getFloat32(0, false);
      break;
    }
    case "F64": {
      const view = new DataView(new ArrayBuffer(8));
      view.setBigUint64(0, bits, false);
      value = view.getFloat64(0, false);
      break;
    }
  }

  if (Number.isNaN(value)) {
    // P6/D6: canonical NaN is exactly f16 with payload 0x7e00, in both profiles.
    if (width !== "F16" || bits !== 0x7e00n) throw new DecodeException("NAN_PAYLOAD_VARIANT");
    return { kind: "floatItem", v: NaN };
  }

  // D5/D7 (dcbor only): any whole-number float in [-2^63, 2^64-1] -
  // including +/-0.0, per D7 zero unification - must be a plain int
  // instead. This is checked before, and takes priority over, the general
  // shortest-float-width rule below.
  if (profile === "dcbor" && isDcborReducible(value)) throw new DecodeException("UNREDUCED_NUMERIC");

  // P5/D2: the width used must be the narrowest of f16/f32/f64 that
  // round-trips `value` exactly.
  const shortest = shortestFloatWidth(value);
  if (width !== shortest) throw new DecodeException("NON_SHORTEST_FLOAT");

  return { kind: "floatItem", v: value };
}

function isDcborReducible(value: number): boolean {
  if (!Number.isFinite(value)) return false;
  if (value !== Math.floor(value)) return false;
  return value >= -9_223_372_036_854_775_808.0 && value <= 18_446_744_073_709_551_615.0;
}

function doubleToRawBits(value: number): bigint {
  const view = new DataView(new ArrayBuffer(8));
  view.setFloat64(0, value, false);
  return view.getBigUint64(0, false);
}

function shortestFloatWidth(value: number): FloatWidth {
  const targetBits = doubleToRawBits(value);
  const f16Bits = doubleToF16Bits(value);
  if (doubleToRawBits(f16BitsToDouble(f16Bits)) === targetBits) return "F16";
  const f32 = Math.fround(value);
  if (doubleToRawBits(f32) === targetBits) return "F32";
  return "F64";
}

function formatFloat(value: number): string {
  return Number.isNaN(value) ? "NaN" : String(value);
}

function itemToLogical(item: Item): LogicalValue {
  switch (item.kind) {
    case "intPos":
      return { type: "int", value: item.v.toString() };
    case "intNeg":
      return { type: "int", value: (-1n - item.v).toString() };
    case "bytes":
      return { type: "bytes", value: hexEncode(item.v) };
    case "text":
      return { type: "text", value: item.v };
    case "arr":
      return { type: "array", items: item.items.map(itemToLogical) };
    case "mapEntries":
      return {
        type: "map",
        entries: item.entries.map(([k, v]) => [itemToLogical(k), itemToLogical(v)] as [LogicalValue, LogicalValue]),
      };
    case "taggedItem":
      return { type: "tag", tag: item.tag, value: itemToLogical(item.inner) };
    case "boolItem":
      return { type: "bool", value: item.v };
    case "nullItem":
      return { type: "null" };
    case "floatItem":
      return { type: "float", width: "auto", value: formatFloat(item.v) };
  }
}
