import json
from pathlib import Path

import jsonschema
import pytest

from oracle.rfc8949_oracle import encode_rfc8949

VECTOR_DIR = Path(__file__).parent.parent / "vectors/v1/cbor/rfc8949/hand-written"
SCHEMA = json.loads((Path(__file__).parent.parent / "logical-value.schema.json").read_text())
VALIDATOR = jsonschema.Draft202012Validator(SCHEMA)

VECTOR_FILES = ["integers.json", "floats.json", "maps-key-order.json", "bignums.json"]


@pytest.mark.parametrize("filename", VECTOR_FILES)
def test_vector_file_matches_oracle_and_schema(filename):
    vectors = json.loads((VECTOR_DIR / filename).read_text())
    assert len(vectors) >= 1
    for v in vectors:
        assert v["rationale"], f"{v['id']} missing rationale"
        VALIDATOR.validate(v["logical_value"])
        assert encode_rfc8949(v["logical_value"]).hex() == v["expected_hex"], v["id"]


def test_integers_file_covers_shortest_int_boundaries():
    vectors = json.loads((VECTOR_DIR / "integers.json").read_text())
    ids = {v["id"] for v in vectors}
    for expected_id in ["int-zero", "int-23", "int-24", "int-255", "int-256", "int-neg-1", "int-min-i64"]:
        assert expected_id in ids
