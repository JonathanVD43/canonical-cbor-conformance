# tests/test_logical_value_schema.py
import json
from pathlib import Path

import jsonschema
import pytest

SCHEMA_PATH = Path(__file__).parent.parent / "logical-value.schema.json"


@pytest.fixture(scope="module")
def schema():
    return json.loads(SCHEMA_PATH.read_text())


@pytest.fixture(scope="module")
def validator(schema):
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


GOOD_VALUES = [
    {"type": "int", "value": "0"},
    {"type": "int", "value": "-9223372036854775808"},
    {"type": "float", "width": "auto", "value": "2.5"},
    {"type": "float", "width": "auto", "value": "NaN"},
    {"type": "float", "width": "auto", "value": "-0.0"},
    {"type": "text", "value": "café"},
    {"type": "bytes", "value": "deadbeef"},
    {"type": "bool", "value": True},
    {"type": "null"},
    {"type": "array", "items": [{"type": "int", "value": "1"}, {"type": "int", "value": "2"}]},
    {
        "type": "map",
        "entries": [
            [{"type": "text", "value": "a"}, {"type": "int", "value": "1"}],
            [{"type": "int", "value": "9"}, {"type": "int", "value": "2"}],
        ],
    },
    {"type": "tag", "tag": 100, "value": {"type": "int", "value": "5"}},
    {"type": "bignum", "sign": "positive", "value": "18446744073709551616"},
]

BAD_VALUES = [
    {"type": "int"},  # missing value
    {"type": "float", "value": "2.5"},  # missing width
    {"type": "bytes", "value": "not-hex"},
    {"type": "bignum", "sign": "sideways", "value": "1"},
    {"type": "nonsense"},
]


def test_schema_is_valid_draft202012(validator):
    assert validator is not None


@pytest.mark.parametrize("value", GOOD_VALUES)
def test_accepts_good_values(validator, value):
    validator.validate(value)


@pytest.mark.parametrize("value", BAD_VALUES)
def test_rejects_bad_values(validator, value):
    with pytest.raises(jsonschema.ValidationError):
        validator.validate(value)
