import { strict as assert } from "node:assert";
import { test } from "node:test";
import { parseLogicalValueLine, ParseError, type LogicalValue } from "./logicalValue.ts";

// Test every grammar variant from logical-value.schema.json

test("parse int value", () => {
  const result = parseLogicalValueLine('{"type": "int", "value": "0"}');
  assert.deepEqual(result, { type: "int", value: "0" });
});

test("parse int value with negative", () => {
  const result = parseLogicalValueLine('{"type": "int", "value": "-42"}');
  assert.deepEqual(result, { type: "int", value: "-42" });
});

test("parse float auto", () => {
  const result = parseLogicalValueLine('{"type": "float", "width": "auto", "value": "2.5"}');
  assert.deepEqual(result, { type: "float", width: "auto", value: "2.5" });
});

test("parse float NaN", () => {
  const result = parseLogicalValueLine('{"type": "float", "width": "auto", "value": "NaN"}');
  assert.deepEqual(result, { type: "float", width: "auto", value: "NaN" });
});

test("parse float -0.0", () => {
  const result = parseLogicalValueLine('{"type": "float", "width": "auto", "value": "-0.0"}');
  assert.deepEqual(result, { type: "float", width: "auto", value: "-0.0" });
});

test("parse float f32", () => {
  const result = parseLogicalValueLine('{"type": "float", "width": "f32", "value": "1.5"}');
  assert.deepEqual(result, { type: "float", width: "f32", value: "1.5" });
});

test("parse float f64", () => {
  const result = parseLogicalValueLine('{"type": "float", "width": "f64", "value": "1.5"}');
  assert.deepEqual(result, { type: "float", width: "f64", value: "1.5" });
});

test("parse float f16", () => {
  const result = parseLogicalValueLine('{"type": "float", "width": "f16", "value": "1.5"}');
  assert.deepEqual(result, { type: "float", width: "f16", value: "1.5" });
});

test("parse text value", () => {
  const result = parseLogicalValueLine('{"type": "text", "value": "café"}');
  assert.deepEqual(result, { type: "text", value: "café" });
});

test("parse bytes value", () => {
  const result = parseLogicalValueLine('{"type": "bytes", "value": "deadbeef"}');
  assert.deepEqual(result, { type: "bytes", value: "deadbeef" });
});

test("parse bool true", () => {
  const result = parseLogicalValueLine('{"type": "bool", "value": true}');
  assert.deepEqual(result, { type: "bool", value: true });
});

test("parse bool false", () => {
  const result = parseLogicalValueLine('{"type": "bool", "value": false}');
  assert.deepEqual(result, { type: "bool", value: false });
});

test("parse null", () => {
  const result = parseLogicalValueLine('{"type": "null"}');
  assert.deepEqual(result, { type: "null" });
});

test("parse empty array", () => {
  const result = parseLogicalValueLine('{"type": "array", "items": []}');
  assert.deepEqual(result, { type: "array", items: [] });
});

test("parse array with single int", () => {
  const result = parseLogicalValueLine('{"type": "array", "items": [{"type": "int", "value": "1"}]}');
  assert.deepEqual(result, {
    type: "array",
    items: [{ type: "int", value: "1" }],
  });
});

test("parse array with multiple items", () => {
  const result = parseLogicalValueLine(
    '{"type": "array", "items": [{"type": "int", "value": "1"}, {"type": "int", "value": "2"}]}'
  );
  assert.deepEqual(result, {
    type: "array",
    items: [
      { type: "int", value: "1" },
      { type: "int", value: "2" },
    ],
  });
});

test("parse nested array", () => {
  const result = parseLogicalValueLine(
    '{"type": "array", "items": [{"type": "array", "items": [{"type": "int", "value": "1"}]}]}'
  );
  assert.deepEqual(result, {
    type: "array",
    items: [
      {
        type: "array",
        items: [{ type: "int", value: "1" }],
      },
    ],
  });
});

test("parse empty map", () => {
  const result = parseLogicalValueLine('{"type": "map", "entries": []}');
  assert.deepEqual(result, { type: "map", entries: [] });
});

test("parse map with single entry", () => {
  const result = parseLogicalValueLine(
    '{"type": "map", "entries": [[{"type": "text", "value": "a"}, {"type": "int", "value": "1"}]]}'
  );
  assert.deepEqual(result, {
    type: "map",
    entries: [
      [
        { type: "text", value: "a" },
        { type: "int", value: "1" },
      ],
    ],
  });
});

test("parse map with multiple entries", () => {
  const result = parseLogicalValueLine(
    '{"type": "map", "entries": [[{"type": "text", "value": "a"}, {"type": "int", "value": "1"}], [{"type": "text", "value": "b"}, {"type": "int", "value": "2"}]]}'
  );
  assert.deepEqual(result, {
    type: "map",
    entries: [
      [
        { type: "text", value: "a" },
        { type: "int", value: "1" },
      ],
      [
        { type: "text", value: "b" },
        { type: "int", value: "2" },
      ],
    ],
  });
});

test("parse tag with small value", () => {
  const result = parseLogicalValueLine(
    '{"type": "tag", "tag": 100, "value": {"type": "int", "value": "5"}}'
  );
  assert.deepEqual(result, {
    type: "tag",
    tag: 100n,
    value: { type: "int", value: "5" },
  });
});

test("parse bignum positive", () => {
  const result = parseLogicalValueLine(
    '{"type": "bignum", "sign": "positive", "value": "18446744073709551616"}'
  );
  assert.deepEqual(result, {
    type: "bignum",
    sign: "positive",
    value: "18446744073709551616",
  });
});

test("parse bignum negative", () => {
  const result = parseLogicalValueLine(
    '{"type": "bignum", "sign": "negative", "value": "18446744073709551616"}'
  );
  assert.deepEqual(result, {
    type: "bignum",
    sign: "negative",
    value: "18446744073709551616",
  });
});

// CRITICAL: Test tag precision fix for values > 2^53
test("tag precision: value > 2^53 round-trips exactly as bigint", () => {
  // 2^53 = 9007199254740992
  // 2^64 - 1 = 18446744073709551615
  // This is the maximum valid CBOR tag value
  const maxTag = "18446744073709551615";
  const jsonLine = `{"type": "tag", "tag": ${maxTag}, "value": {"type": "int", "value": "0"}}`;

  const result = parseLogicalValueLine(jsonLine);
  assert.equal(result.type, "tag");
  if (result.type === "tag") {
    assert.equal(typeof result.tag, "bigint");
    assert.equal(result.tag, BigInt(maxTag));
    // Verify no precision loss by converting back to string
    assert.equal(result.tag.toString(), maxTag);
  }
});

test("tag precision: various large tag values preserve exactly", () => {
  const largeValues = [
    "9007199254740993", // 2^53 + 1
    "281474976710656", // 2^48
    "1099511627776", // 2^40
    "18446744073709551615", // 2^64 - 1
  ];

  for (const tagValue of largeValues) {
    const jsonLine = `{"type": "tag", "tag": ${tagValue}, "value": {"type": "null"}}`;
    const result = parseLogicalValueLine(jsonLine);
    assert.equal(result.type, "tag");
    if (result.type === "tag") {
      assert.equal(result.tag.toString(), tagValue, `Failed for tag value ${tagValue}`);
    }
  }
});

// Error cases
test("reject malformed JSON", () => {
  assert.throws(
    () => parseLogicalValueLine("{not valid json}"),
    ParseError
  );
});

test("reject missing type field", () => {
  assert.throws(
    () => parseLogicalValueLine('{"value": "test"}'),
    ParseError
  );
});

test("reject unknown type", () => {
  assert.throws(
    () => parseLogicalValueLine('{"type": "unknown"}'),
    ParseError
  );
});

test("reject int missing value", () => {
  assert.throws(
    () => parseLogicalValueLine('{"type": "int"}'),
    ParseError
  );
});

test("reject float missing width", () => {
  assert.throws(
    () => parseLogicalValueLine('{"type": "float", "value": "1.0"}'),
    ParseError
  );
});

test("reject float missing value", () => {
  assert.throws(
    () => parseLogicalValueLine('{"type": "float", "width": "auto"}'),
    ParseError
  );
});

test("reject array missing items", () => {
  assert.throws(
    () => parseLogicalValueLine('{"type": "array"}'),
    ParseError
  );
});

test("reject map missing entries", () => {
  assert.throws(
    () => parseLogicalValueLine('{"type": "map"}'),
    ParseError
  );
});

test("reject tag missing tag", () => {
  assert.throws(
    () => parseLogicalValueLine('{"type": "tag", "value": {"type": "null"}}'),
    ParseError
  );
});

test("reject tag missing value", () => {
  assert.throws(
    () => parseLogicalValueLine('{"type": "tag", "tag": 100}'),
    ParseError
  );
});

test("reject map entry not array", () => {
  assert.throws(
    () => parseLogicalValueLine('{"type": "map", "entries": [{"type": "int", "value": "1"}]}'),
    ParseError
  );
});

test("reject map entry wrong size", () => {
  assert.throws(
    () => parseLogicalValueLine('{"type": "map", "entries": [[{"type": "int", "value": "1"}]]}'),
    ParseError
  );
});
