import { test } from "node:test";
import assert from "node:assert/strict";
import { encodeDcbor, EncodeError } from "./dcbor.ts";
import { hexEncode } from "./util.ts";
import type { LogicalValue } from "./logicalValue.ts";

// Built via String.fromCharCode (not literal accented characters in source)
// so the exact Unicode codepoint sequence is unambiguous.
// "cafe" + combining acute accent (U+0301) -- NFD-decomposed form.
const CAFE_DECOMPOSED = "cafe" + String.fromCharCode(0x0301);
// precomposed form using a single U+00E9 character -- NFC form.
const CAFE_PRECOMPOSED = "caf" + String.fromCharCode(0x00e9);

function hex(v: LogicalValue): string {
  return hexEncode(encodeDcbor(v));
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

test("d5FloatWholeBecomesInt", () => {
  assert.equal(hex(Float("auto", "2.0")), "02");
});

test("d5FloatFractionStaysFloat", () => {
  assert.equal(hex(Float("auto", "2.5")), "f94100");
});

test("d6NanCanonicalPayload", () => {
  assert.equal(hex(Float("auto", "NaN")), "f97e00");
});

test("d7ZeroAsInt", () => {
  assert.equal(hex(Int("0")), "00");
});

test("d7ZeroAsPosFloatUnifiesWithInt", () => {
  assert.equal(hex(Float("auto", "0.0")), "00");
});

test("d7ZeroAsNegFloatUnifiesWithInt", () => {
  // Opposite of rfc8949's P7: -0.0 must ALSO collapse to plain int 0 here.
  assert.equal(hex(Float("auto", "-0.0")), "00");
});

test("d8NfcCombiningAccentNormalizes", () => {
  // "cafe" + combining acute accent (U+0301) normalizes to precomposed form (U+00E9).
  assert.equal(hex(Text(CAFE_DECOMPOSED)), "65636166c3a9");
});

test("bignumBelowNativeRangeIsRejected", () => {
  assert.throws(() => encodeDcbor(Bignum("positive", "5")), EncodeError);
});

test("bignumAt2Pow64UsesTag2", () => {
  assert.equal(
    hex(Bignum("positive", "18446744073709551616")),
    "c249010000000000000000",
  );
});

test("tagWrapsInnerValue", () => {
  assert.deepEqual(encodeDcbor(Tag(100n, Int("5"))), new Uint8Array([0xd8, 0x64, 0x05]));
});

test("reservedBignumTagViaGenericTagArmIsARawPassthrough", () => {
  // Tag 2 supplied via the generic Tag variant (not the Bignum variant) is just
  // a raw pass-through tag wrap around its inner value -- the bignum native-range
  // rejection rule does NOT apply here.
  assert.deepEqual(
    encodeDcbor(Tag(2n, Bytes("01"))),
    new Uint8Array([0xc2, 0x41, 0x01]),
  );
});

test("explicitWidthIsIgnoredWholeFloatStillReduces", () => {
  assert.equal(hex(Float("f64", "2.0")), "02");
});

test("explicitWidthIsIgnoredNanStillCanonical", () => {
  assert.equal(hex(Float("f32", "NaN")), "f97e00");
});

test("tagAtULongMaxDoesNotTruncate", () => {
  assert.equal(hex(Tag(18446744073709551615n, Int("5"))), "dbffffffffffffffff05");
});

test("mapKeyCollisionDedupesLastWriteWins", () => {
  // 0, 0.0, and -0.0 all D7-unify to encoded key 0x00, so all three entries
  // collapse to ONE entry keyed 0x00 with value 3 (the last one written).
  assert.equal(
    hex(
      Map_([
        [Int("0"), Int("1")],
        [Float("auto", "0.0"), Int("2")],
        [Float("auto", "-0.0"), Int("3")],
      ]),
    ),
    "a10003",
  );
});

test("mapKeyCollisionViaNfcDedupesLastWriteWins", () => {
  // both D8-normalize to identical encoded key bytes, so they collapse to
  // ONE entry with value 2 (the last one written).
  assert.equal(
    hex(
      Map_([
        [Text(CAFE_DECOMPOSED), Int("1")],
        [Text(CAFE_PRECOMPOSED), Int("2")],
      ]),
    ),
    "a165636166c3a902",
  );
});

test("arrayAndMapRoundTrip", () => {
  assert.deepEqual(encodeDcbor(Arr([Int("1"), Int("2")])), new Uint8Array([0x82, 0x01, 0x02]));
  assert.equal(hex(Map_([[Text("a"), Int("1")]])), "a1616101");
});

// Cross-check against vectors/v1/cbor/dcbor/hand-written/numeric-reduction.json
test("vector numeric-reduction: float-whole-becomes-int", () => {
  assert.equal(hex(Float("auto", "2.0")), "02");
});
test("vector numeric-reduction: float-fraction-stays-float", () => {
  assert.equal(hex(Float("auto", "2.5")), "f94100");
});
test("vector numeric-reduction: float-nan-canonical", () => {
  assert.equal(hex(Float("auto", "NaN")), "f97e00");
});

// Cross-check against vectors/v1/cbor/dcbor/hand-written/zero-unification.json
test("vector zero-unification: zero-as-int", () => {
  assert.equal(hex(Int("0")), "00");
});
test("vector zero-unification: zero-as-pos-float", () => {
  assert.equal(hex(Float("auto", "0.0")), "00");
});
test("vector zero-unification: zero-as-neg-float", () => {
  assert.equal(hex(Float("auto", "-0.0")), "00");
});

// Cross-check against vectors/v1/cbor/dcbor/hand-written/nfc-normalization.json
test("vector nfc-normalization: nfc-combining-accent", () => {
  assert.equal(hex(Text(CAFE_DECOMPOSED)), "65636166c3a9");
});

// Cross-check against vectors/v1/cbor/dcbor/hand-written/maps-key-order.json
test("vector maps-key-order: dcbor-map-int-keys-byte-sort", () => {
  assert.equal(
    hex(
      Map_([
        [Int("9"), Int("1")],
        [Int("10"), Int("2")],
      ]),
    ),
    "a209010a02",
  );
});
test("vector maps-key-order: dcbor-map-int-keys-pure-bytewise-no-length-presort", () => {
  assert.equal(
    hex(
      Map_([
        [Int("-24"), Int("1")],
        [Int("1000"), Int("2")],
      ]),
    ),
    "a21903e8023701",
  );
});
