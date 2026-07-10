import { strict as assert } from "node:assert";
import { test } from "node:test";
import { hexEncode, hexDecode } from "./util.ts";

test("hexEncode returns lowercase hex", () => {
  const buf = Buffer.from([0xde, 0xad, 0xbe, 0xef]);
  assert.equal(hexEncode(buf), "deadbeef");
});

test("hexDecode accepts valid hex", () => {
  const result = hexDecode("deadbeef");
  assert.deepEqual(result, Buffer.from([0xde, 0xad, 0xbe, 0xef]));
});

test("hexDecode rejects odd-length hex", () => {
  assert.throws(() => hexDecode("f"), /odd length/);
  assert.throws(() => hexDecode("fff"), /odd length/);
});

test("hexDecode rejects non-hex characters", () => {
  assert.throws(() => hexDecode("gg"), /non-hex characters/);
  assert.throws(() => hexDecode("zz"), /non-hex characters/);
  assert.throws(() => hexDecode("12xy"), /non-hex characters/);
});

test("hexEncode/hexDecode round-trip", () => {
  const original = Buffer.from([0x00, 0x01, 0x02, 0xff, 0xfe, 0xfd]);
  const hex = hexEncode(original);
  const decoded = hexDecode(hex);
  assert.deepEqual(decoded, original);
});

test("hexDecode accepts empty string", () => {
  const result = hexDecode("");
  assert.deepEqual(result, Buffer.alloc(0));
});

test("hexEncode/hexDecode preserves byte values exactly", () => {
  // Test all possible byte values
  const allBytes = Buffer.alloc(256);
  for (let i = 0; i < 256; i++) {
    allBytes[i] = i;
  }
  const hex = hexEncode(allBytes);
  const decoded = hexDecode(hex);
  assert.deepEqual(decoded, allBytes);
});
