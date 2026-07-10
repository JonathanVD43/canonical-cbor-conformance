import { test } from "node:test";
import assert from "node:assert/strict";
import { encodeRfc8949, EncodeError } from "./rfc8949.ts";
import { hexEncode } from "./util.ts";
import type { LogicalValue } from "./logicalValue.ts";

function hex(v: LogicalValue): string {
  return hexEncode(encodeRfc8949(v));
}

function Int(value: string): LogicalValue {
  return { type: "int", value };
}
function Text(value: string): LogicalValue {
  return { type: "text", value };
}
function Bytes(value: string): LogicalValue {
  return { type: "bytes", value };
}
function Bool(value: boolean): LogicalValue {
  return { type: "bool", value };
}
const Null: LogicalValue = { type: "null" };
function Arr(items: LogicalValue[]): LogicalValue {
  return { type: "array", items };
}
function Map_(entries: [LogicalValue, LogicalValue][]): LogicalValue {
  return { type: "map", entries };
}
function Float(width: "auto" | "f16" | "f32" | "f64", value: string): LogicalValue {
  return { type: "float", width, value };
}
function Tag(tag: bigint, value: LogicalValue): LogicalValue {
  return { type: "tag", tag, value };
}
function Bignum(sign: "positive" | "negative", value: string): LogicalValue {
  return { type: "bignum", sign, value };
}

test("shortestIntZero", () => {
  assert.equal(hex(Int("0")), "00");
});

test("shortestIntBoundary23_24", () => {
  assert.equal(hex(Int("23")), "17");
  assert.equal(hex(Int("24")), "1818");
});

test("shortestIntBoundary255_256", () => {
  assert.equal(hex(Int("255")), "18ff");
  assert.equal(hex(Int("256")), "190100");
});

test("shortestIntBoundary65535_65536", () => {
  assert.equal(hex(Int("65535")), "19ffff");
  assert.equal(hex(Int("65536")), "1a00010000");
});

test("shortestIntBoundary4294967295_4294967296", () => {
  assert.equal(hex(Int("4294967295")), "1affffffff");
  assert.equal(hex(Int("4294967296")), "1b0000000100000000");
});

test("negativeInt", () => {
  assert.equal(hex(Int("-1")), "20");
});

test("negativeIntMinI64Boundary", () => {
  assert.equal(hex(Int("-9223372036854775808")), "3b7fffffffffffffff");
});

test("textAndBytes", () => {
  assert.deepEqual(encodeRfc8949(Text("a")), new Uint8Array([0x61, 0x61]));
  assert.equal(hex(Bytes("deadbeef")), "44deadbeef");
});

test("boolAndNull", () => {
  assert.deepEqual(encodeRfc8949(Bool(true)), new Uint8Array([0xf5]));
  assert.deepEqual(encodeRfc8949(Bool(false)), new Uint8Array([0xf4]));
  assert.deepEqual(encodeRfc8949(Null), new Uint8Array([0xf6]));
});

test("arrayOfInts", () => {
  assert.deepEqual(encodeRfc8949(Arr([Int("1"), Int("2")])), new Uint8Array([0x82, 0x01, 0x02]));
});

test("nanCanonicalPayload", () => {
  assert.equal(hex(Float("auto", "NaN")), "f97e00");
});

test("nanCanonicalPayloadUnderExplicitWidthForcing", () => {
  for (const width of ["f16", "f32", "f64"] as const) {
    assert.equal(hex(Float(width, "NaN")), "f97e00");
  }
});

test("negativeZeroPreservedDistinctFromZero", () => {
  const neg = hex(Float("auto", "-0.0"));
  const pos = hex(Float("auto", "0.0"));
  assert.equal(neg, "f98000");
  assert.equal(pos, "f90000");
  assert.notEqual(neg, pos);
});

test("shortestFloatFormF16Exact", () => {
  assert.equal(hex(Float("auto", "2.5")), "f94100");
});

test("explicitFloatWidthForcesEncoding", () => {
  assert.equal(hex(Float("f64", "2.5")), "fb4004000000000000");
});

test("explicitFloatWidthRaisesOnPrecisionLoss", () => {
  assert.throws(() => encodeRfc8949(Float("f16", "0.1")), EncodeError);
});

test("mapKeysSortedByEncodedBytes", () => {
  assert.equal(hex(Map_([[Int("9"), Int("1")], [Int("10"), Int("2")]])), "a209010a02");
});

test("mapKeysPureBytewiseNoLengthPresort", () => {
  // -24 encodes to 1 byte (0x37); 1000 encodes to 3 bytes (0x1903e8). Pure bytewise
  // order (no length pre-sort) means 0x19 < 0x37, so the LONGER encoding of 1000
  // sorts FIRST. A length-first (RFC 7049-style) comparator would wrongly sort
  // -24 first. This is the critical regression trap for this task.
  assert.equal(
    hex(Map_([[Int("-24"), Int("1")], [Int("1000"), Int("2")]])),
    "a21903e8023701",
  );
});

test("bignumAt2Pow64UsesTag2", () => {
  assert.equal(hex(Bignum("positive", "18446744073709551616")), "c249010000000000000000");
});

test("negativeBignumOffsetByOne", () => {
  assert.equal(hex(Bignum("negative", "18446744073709551616")), "c348ffffffffffffffff");
});

test("bignumBelow2Pow64IsRejected", () => {
  assert.throws(() => encodeRfc8949(Bignum("positive", "5")), EncodeError);
  assert.throws(() => encodeRfc8949(Bignum("negative", "9223372036854775808")), EncodeError);
});

test("tagWrapsInnerValue", () => {
  assert.deepEqual(encodeRfc8949(Tag(100n, Int("5"))), new Uint8Array([0xd8, 0x64, 0x05]));
});

test("tagAtULongMaxDoesNotTruncate", () => {
  // Regression: verifies the tag argument doesn't get silently truncated/wrapped
  // when it's the maximum 64-bit unsigned value.
  assert.equal(hex(Tag(18446744073709551615n, Int("5"))), "dbffffffffffffffff05");
});

test("mapDuplicateKeysAreNotDeduped", () => {
  // rfc8949 has NO key-collision dedup (unlike dcbor) — both entries for a
  // literal duplicate key must survive to the output, in original order.
  assert.equal(hex(Map_([[Int("5"), Int("1")], [Int("5"), Int("2")]])), "a205010502");
});
