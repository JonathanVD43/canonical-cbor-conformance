import json
from pathlib import Path

import jsonschema
import pytest

from oracle.dcbor_oracle import encode_dcbor

VECTOR_DIR = Path(__file__).parent.parent / "vectors/v1/cbor/dcbor/hand-written"
SCHEMA = json.loads((Path(__file__).parent.parent / "logical-value.schema.json").read_text())
VALIDATOR = jsonschema.Draft202012Validator(SCHEMA)

VECTOR_FILES = ["numeric-reduction.json", "zero-unification.json", "nfc-normalization.json"]


@pytest.mark.parametrize("filename", VECTOR_FILES)
def test_vector_file_matches_oracle_and_schema(filename):
    vectors = json.loads((VECTOR_DIR / filename).read_text())
    assert len(vectors) >= 1
    for v in vectors:
        assert v["rationale"]
        VALIDATOR.validate(v["logical_value"])
        assert encode_dcbor(v["logical_value"]).hex() == v["expected_hex"], v["id"]


def test_zero_unification_all_three_forms_produce_same_bytes():
    vectors = json.loads((VECTOR_DIR / "zero-unification.json").read_text())
    hexes = {v["expected_hex"] for v in vectors}
    assert hexes == {"00"}
