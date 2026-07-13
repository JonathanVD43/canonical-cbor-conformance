"""Parses this project's Neutral Logical Value Grammar (schema:
logical-value.schema.json at the repo root) from JSON stdin lines into a
closed set of dataclasses -- mirrors the Go adapter's LogicalValue
interface / Kotlin's sealed LogicalValue class.

Unlike the Go/Kotlin/TypeScript/Rust ports, no special "use an
arbitrary-precision JSON number type" trick is needed here: Python's `json`
module already parses JSON integer literals as native Python `int`, which is
arbitrary-precision, so the "tag" field (up to 2**64-1) is never narrowed
through a lossy fixed-width intermediate."""
import json
from dataclasses import dataclass, field
from typing import List, Union

from errors import ParseError

MAX_UINT64 = (1 << 64) - 1


@dataclass(frozen=True)
class IntValue:
    value: str


@dataclass(frozen=True)
class FloatValue:
    width: str
    value: str


@dataclass(frozen=True)
class TextValue:
    value: str


@dataclass(frozen=True)
class BytesValue:
    value: str


@dataclass(frozen=True)
class BoolValue:
    value: bool


@dataclass(frozen=True)
class NullValue:
    pass


@dataclass(frozen=True)
class ArrayValue:
    items: List["LogicalValue"] = field(default_factory=list)


@dataclass(frozen=True)
class MapEntry:
    key: "LogicalValue"
    val: "LogicalValue"


@dataclass(frozen=True)
class MapValue:
    entries: List[MapEntry] = field(default_factory=list)


@dataclass(frozen=True)
class TagValue:
    tag: int
    value: "LogicalValue"


@dataclass(frozen=True)
class BignumValue:
    sign: str
    value: str


LogicalValue = Union[
    IntValue, FloatValue, TextValue, BytesValue, BoolValue, NullValue,
    ArrayValue, MapValue, TagValue, BignumValue,
]


def parse_logical_value_line(line: str) -> LogicalValue:
    try:
        raw = json.loads(line)
    except json.JSONDecodeError as e:
        raise ParseError(f"malformed JSON: {e}") from e
    return parse_logical_value(raw)


def parse_logical_value(raw) -> LogicalValue:
    if not isinstance(raw, dict):
        raise ParseError("expected a JSON object")
    t = raw.get("type")
    if not isinstance(t, str):
        raise ParseError('missing "type" field')

    if t == "int":
        return IntValue(_required_string(raw, "value", "int"))
    if t == "float":
        return FloatValue(
            _required_string(raw, "width", "float"),
            _required_string(raw, "value", "float"),
        )
    if t == "text":
        return TextValue(_required_string(raw, "value", "text"))
    if t == "bytes":
        return BytesValue(_required_string(raw, "value", "bytes"))
    if t == "bool":
        v = raw.get("value")
        if not isinstance(v, bool):
            raise ParseError('bool: missing "value"')
        return BoolValue(v)
    if t == "null":
        return NullValue()
    if t == "array":
        items = raw.get("items")
        if not isinstance(items, list):
            raise ParseError('array: missing "items"')
        return ArrayValue([parse_logical_value(it) for it in items])
    if t == "map":
        entries = raw.get("entries")
        if not isinstance(entries, list):
            raise ParseError('map: missing "entries"')
        out = []
        for e in entries:
            if not isinstance(e, list) or len(e) != 2:
                raise ParseError("map entry must be a 2-element array")
            out.append(MapEntry(parse_logical_value(e[0]), parse_logical_value(e[1])))
        return MapValue(out)
    if t == "tag":
        raw_tag = raw.get("tag")
        # bool is a subclass of int in Python -- exclude it explicitly so a
        # stray JSON `true`/`false` isn't silently accepted as a tag number.
        if raw_tag is None or isinstance(raw_tag, bool) or not isinstance(raw_tag, int):
            raise ParseError('tag: missing "tag" number')
        if raw_tag < 0 or raw_tag > MAX_UINT64:
            raise ParseError('tag: missing "tag" number')
        if "value" not in raw or raw.get("value") is None:
            raise ParseError('tag: missing "value"')
        inner = parse_logical_value(raw["value"])
        return TagValue(raw_tag, inner)
    if t == "bignum":
        return BignumValue(
            _required_string(raw, "sign", "bignum"),
            _required_string(raw, "value", "bignum"),
        )
    raise ParseError(f"unknown logical-value type: {t!r}")


def _required_string(raw: dict, field_name: str, type_name: str) -> str:
    v = raw.get(field_name)
    if not isinstance(v, str):
        raise ParseError(f'{type_name}: missing "{field_name}"')
    return v
