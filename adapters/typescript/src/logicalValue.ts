/**
 * LogicalValue grammar parser for canonical CBOR conformance test suite.
 */

/**
 * Represents a logical value in the conformance test grammar.
 * This is a recursive union type matching logical-value.schema.json.
 */
export type LogicalValue =
  | IntValue
  | FloatValue
  | TextValue
  | BytesValue
  | BoolValue
  | NullValue
  | ArrayValue
  | MapValue
  | TagValue
  | BignumValue;

interface IntValue {
  type: "int";
  value: string;
}

interface FloatValue {
  type: "float";
  width: "auto" | "f16" | "f32" | "f64";
  value: string;
}

interface TextValue {
  type: "text";
  value: string;
}

interface BytesValue {
  type: "bytes";
  value: string;
}

interface BoolValue {
  type: "bool";
  value: boolean;
}

interface NullValue {
  type: "null";
}

interface ArrayValue {
  type: "array";
  items: LogicalValue[];
}

interface MapValue {
  type: "map";
  entries: [LogicalValue, LogicalValue][];
}

interface TagValue {
  type: "tag";
  tag: bigint;
  value: LogicalValue;
}

interface BignumValue {
  type: "bignum";
  sign: "positive" | "negative";
  value: string;
}

/**
 * Parse error with explanatory message.
 */
export class ParseError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ParseError";
  }
}

/**
 * Parse a single line of logical-value JSON grammar.
 *
 * Handles the JSON number precision hazard for tag values:
 * - Rewrites "tag":<digits> to "tag":"<digits>" before parsing
 * - Converts tag string back to bigint after parsing
 * This ensures tag values >= 2^53 are preserved exactly.
 */
export function parseLogicalValueLine(line: string): LogicalValue {
  // Apply the tag-precision regex fix: rewrite "tag":<digits> to "tag":"<digits>"
  // This pattern is safe against false positives because:
  // - additionalProperties: false in tagValue schema
  // - unescaped colons can only appear as field separators
  // - escaped quotes in strings are always \"
  const fixedLine = line.replace(/"tag"\s*:\s*(\d+)/g, '"tag":"$1"');

  let json: any;
  try {
    json = JSON.parse(fixedLine);
  } catch (e) {
    throw new ParseError(`JSON parse error: ${e instanceof Error ? e.message : String(e)}`);
  }

  return parseLogicalValue(json);
}

/**
 * Parse a JSON object into a LogicalValue.
 * Assumes input is already parsed (not a string).
 */
function parseLogicalValue(json: any): LogicalValue {
  if (typeof json !== "object" || json === null || Array.isArray(json)) {
    throw new ParseError("expected a JSON object");
  }

  const type = json.type;
  if (typeof type !== "string") {
    throw new ParseError('missing or invalid "type" field');
  }

  switch (type) {
    case "int":
      return parseIntValue(json);
    case "float":
      return parseFloatValue(json);
    case "text":
      return parseText(json);
    case "bytes":
      return parseBytes(json);
    case "bool":
      return parseBool(json);
    case "null":
      return parseNull(json);
    case "array":
      return parseArray(json);
    case "map":
      return parseMap(json);
    case "tag":
      return parseTag(json);
    case "bignum":
      return parseBignum(json);
    default:
      throw new ParseError(`unknown logical-value type: ${JSON.stringify(type)}`);
  }
}

function parseIntValue(json: any): IntValue {
  const value = json.value;
  if (typeof value !== "string") {
    throw new ParseError('int: missing or invalid "value"');
  }
  return { type: "int", value };
}

function parseFloatValue(json: any): FloatValue {
  const width = json.width;
  if (!["auto", "f16", "f32", "f64"].includes(width)) {
    throw new ParseError('float: invalid or missing "width"');
  }
  const value = json.value;
  if (typeof value !== "string") {
    throw new ParseError('float: missing or invalid "value"');
  }
  return { type: "float", width: width as "auto" | "f16" | "f32" | "f64", value };
}

function parseText(json: any): TextValue {
  const value = json.value;
  if (typeof value !== "string") {
    throw new ParseError('text: missing or invalid "value"');
  }
  return { type: "text", value };
}

function parseBytes(json: any): BytesValue {
  const value = json.value;
  if (typeof value !== "string") {
    throw new ParseError('bytes: missing or invalid "value"');
  }
  return { type: "bytes", value };
}

function parseBool(json: any): BoolValue {
  const value = json.value;
  if (typeof value !== "boolean") {
    throw new ParseError('bool: missing or invalid "value"');
  }
  return { type: "bool", value };
}

function parseNull(json: any): NullValue {
  return { type: "null" };
}

function parseArray(json: any): ArrayValue {
  const items = json.items;
  if (!Array.isArray(items)) {
    throw new ParseError('array: missing or invalid "items"');
  }
  return { type: "array", items: items.map(parseLogicalValue) };
}

function parseMap(json: any): MapValue {
  const entries = json.entries;
  if (!Array.isArray(entries)) {
    throw new ParseError('map: missing or invalid "entries"');
  }
  const parsed: [LogicalValue, LogicalValue][] = [];
  for (const entry of entries) {
    if (!Array.isArray(entry) || entry.length !== 2) {
      throw new ParseError("map entry must be a 2-element array");
    }
    parsed.push([parseLogicalValue(entry[0]), parseLogicalValue(entry[1])]);
  }
  return { type: "map", entries: parsed };
}

function parseTag(json: any): TagValue {
  const tag = json.tag;
  if (typeof tag !== "bigint" && typeof tag !== "string") {
    throw new ParseError('tag: missing or invalid "tag" field');
  }
  let tagValue: bigint;
  if (typeof tag === "string") {
    try {
      tagValue = BigInt(tag);
    } catch (e) {
      throw new ParseError('tag: "tag" field is not a valid bigint string');
    }
  } else {
    tagValue = tag as bigint;
  }
  const value = json.value;
  if (!value) {
    throw new ParseError('tag: missing "value"');
  }
  return { type: "tag", tag: tagValue, value: parseLogicalValue(value) };
}

function parseBignum(json: any): BignumValue {
  const sign = json.sign;
  if (!["positive", "negative"].includes(sign)) {
    throw new ParseError('bignum: invalid or missing "sign"');
  }
  const value = json.value;
  if (typeof value !== "string") {
    throw new ParseError('bignum: missing or invalid "value"');
  }
  return { type: "bignum", sign: sign as "positive" | "negative", value };
}
