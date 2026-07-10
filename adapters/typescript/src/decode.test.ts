import { test } from "node:test";
import assert from "node:assert/strict";
import { decodeStrict } from "./decode.ts";
import { hexDecode, hexEncode } from "./util.ts";
import type { Verdict } from "./decode.ts";

function accept(v: Verdict, expectedHex: string): void {
  assert.equal(v.kind, "accept");
  if (v.kind === "accept") assert.equal(hexEncode(v.bytes), expectedHex);
}

function reject(v: Verdict, expectedReason: string): void {
  assert.equal(v.kind, "reject");
  if (v.kind === "reject") assert.equal(v.reason, expectedReason);
}

test("acceptsCanonicalIntAndRoundTrips", () => {
  accept(decodeStrict(hexDecode("05"), "rfc8949"), "05");
  accept(decodeStrict(hexDecode("05"), "dcbor"), "05");
});

test("acceptsCanonicalMapAndArray", () => {
  accept(decodeStrict(hexDecode("820102"), "rfc8949"), "820102");
  accept(decodeStrict(hexDecode("a10102"), "rfc8949"), "a10102");
});

test("nonShortestInt", () => {
  // 5 encoded with a redundant 1-byte-argument form instead of the direct form
  reject(decodeStrict(hexDecode("1805"), "rfc8949"), "NON_SHORTEST_INT");
});

test("unsortedMapKeys", () => {
  // keys 10 then 9, descending raw-byte order
  reject(decodeStrict(hexDecode("a20a010902"), "rfc8949"), "UNSORTED_MAP_KEYS");
});

test("indefiniteLength", () => {
  reject(decodeStrict(hexDecode("9fff"), "rfc8949"), "INDEFINITE_LENGTH");
});

test("duplicateKey", () => {
  reject(decodeStrict(hexDecode("a201010102"), "rfc8949"), "DUPLICATE_KEY");
});

test("nonShortestFloat", () => {
  // 2.5 round-trips exactly through f16, so this f32 encoding is non-shortest
  reject(decodeStrict(hexDecode("fa40200000"), "rfc8949"), "NON_SHORTEST_FLOAT");
  reject(decodeStrict(hexDecode("fa40200000"), "dcbor"), "NON_SHORTEST_FLOAT");
});

test("nanPayloadVariant", () => {
  reject(decodeStrict(hexDecode("fa7fc00000"), "rfc8949"), "NAN_PAYLOAD_VARIANT");
});

test("trailingBytes", () => {
  reject(decodeStrict(hexDecode("05ff"), "rfc8949"), "TRAILING_BYTES");
});

test("multipleTopLevelItems", () => {
  reject(decodeStrict(hexDecode("0506"), "rfc8949"), "MULTIPLE_TOP_LEVEL_ITEMS");
});

test("unknownTag", () => {
  reject(decodeStrict(hexDecode("d86405"), "rfc8949"), "UNKNOWN_TAG");
});

test("nonNfcStringDcborOnly", () => {
  // "cafe" + combining acute accent (U+0301), not NFC-normalized
  reject(decodeStrict(hexDecode("6663616665cc81"), "dcbor"), "NON_NFC_STRING");
  accept(decodeStrict(hexDecode("6663616665cc81"), "rfc8949"), "6663616665cc81");
});

test("unreducedNumericDcborOnly", () => {
  // 2.0 at its shortest float width (f16) - still must be a plain int under dcbor's D5/D7
  reject(decodeStrict(hexDecode("f94000"), "dcbor"), "UNREDUCED_NUMERIC");
  accept(decodeStrict(hexDecode("f94000"), "rfc8949"), "f94000");
});

test("negativeZeroFloatPreservesSign", () => {
  // -0.0 as f16 (P7): formatFloat must emit "-0", not String(-0) === "0", or the
  // decode -> re-encode round trip would silently collapse -0.0 into +0.0 and
  // produce f90000 instead of the original f98000. (dcbor rejects all
  // integral-valued floats via UNREDUCED_NUMERIC, -0.0 included, so this is
  // rfc8949-only; see unreducedNumericDcborOnly above for that rule.)
  accept(decodeStrict(hexDecode("f98000"), "rfc8949"), "f98000");
  reject(decodeStrict(hexDecode("f98000"), "dcbor"), "UNREDUCED_NUMERIC");
});

test("positiveZeroFloatRoundTrips", () => {
  // Adjacent case: legitimate +0.0 must not be affected by the -0.0 sign fix.
  accept(decodeStrict(hexDecode("f90000"), "rfc8949"), "f90000");
});

// --- Vector-file-derived cases (verbatim hex/reason from vectors/v1/cbor/{rfc8949,dcbor}/hand-written/strict-decode-reject/*.json) ---

test("vector rfc8949 duplicate-keys-same-key-twice", () => {
  reject(decodeStrict(hexDecode("a200000001"), "rfc8949"), "DUPLICATE_KEY");
});

test("vector rfc8949 indefinite-length-array", () => {
  reject(decodeStrict(hexDecode("9f00ff"), "rfc8949"), "INDEFINITE_LENGTH");
});

test("vector rfc8949 multiple-top-level-items-two-ints", () => {
  reject(decodeStrict(hexDecode("0001"), "rfc8949"), "MULTIPLE_TOP_LEVEL_ITEMS");
});

test("vector rfc8949 nan-payload-variant-f16", () => {
  reject(decodeStrict(hexDecode("f97e01"), "rfc8949"), "NAN_PAYLOAD_VARIANT");
});

test("vector rfc8949 non-shortest-float-2-5-as-f64", () => {
  reject(decodeStrict(hexDecode("fb4004000000000000"), "rfc8949"), "NON_SHORTEST_FLOAT");
});

test("vector rfc8949 non-shortest-int-zero", () => {
  reject(decodeStrict(hexDecode("1800"), "rfc8949"), "NON_SHORTEST_INT");
});

test("vector rfc8949 trailing-bytes-incomplete-head", () => {
  reject(decodeStrict(hexDecode("0018"), "rfc8949"), "TRAILING_BYTES");
});

test("vector rfc8949 unknown-tag-999", () => {
  reject(decodeStrict(hexDecode("d903e700"), "rfc8949"), "UNKNOWN_TAG");
});

test("vector rfc8949 unsorted-map-keys-1-before-0", () => {
  reject(decodeStrict(hexDecode("a201000000"), "rfc8949"), "UNSORTED_MAP_KEYS");
});

test("vector dcbor duplicate-keys-same-key-twice", () => {
  reject(decodeStrict(hexDecode("a200000001"), "dcbor"), "DUPLICATE_KEY");
});

test("vector dcbor indefinite-length-array", () => {
  reject(decodeStrict(hexDecode("9f00ff"), "dcbor"), "INDEFINITE_LENGTH");
});

test("vector dcbor multiple-top-level-items-two-ints", () => {
  reject(decodeStrict(hexDecode("0001"), "dcbor"), "MULTIPLE_TOP_LEVEL_ITEMS");
});

test("vector dcbor nan-payload-variant-f16", () => {
  reject(decodeStrict(hexDecode("f97e01"), "dcbor"), "NAN_PAYLOAD_VARIANT");
});

test("vector dcbor non-nfc-string-cafe-combining-accent", () => {
  reject(decodeStrict(hexDecode("6663616665cc81"), "dcbor"), "NON_NFC_STRING");
});

test("vector dcbor non-shortest-float-2-5-as-f64", () => {
  reject(decodeStrict(hexDecode("fb4004000000000000"), "dcbor"), "NON_SHORTEST_FLOAT");
});

test("vector dcbor non-shortest-int-zero", () => {
  reject(decodeStrict(hexDecode("1800"), "dcbor"), "NON_SHORTEST_INT");
});

test("vector dcbor trailing-bytes-incomplete-head", () => {
  reject(decodeStrict(hexDecode("0018"), "dcbor"), "TRAILING_BYTES");
});

test("vector dcbor unknown-tag-999", () => {
  reject(decodeStrict(hexDecode("d903e700"), "dcbor"), "UNKNOWN_TAG");
});

test("vector dcbor unreduced-numeric-2-0-as-float", () => {
  reject(decodeStrict(hexDecode("f94000"), "dcbor"), "UNREDUCED_NUMERIC");
});

test("vector dcbor unsorted-map-keys-1-before-0", () => {
  reject(decodeStrict(hexDecode("a201000000"), "dcbor"), "UNSORTED_MAP_KEYS");
});
