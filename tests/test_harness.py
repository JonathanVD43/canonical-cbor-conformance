# tests/test_harness.py
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))
from harness.run import (
    AdapterBinaryNotFoundError,
    build_report,
    load_manifest,
    load_schema_validator,
    run_encode_mode,
    run_profile,
    validate_vectors,
)

STUB_ADAPTER = [sys.executable, str(Path(__file__).parent / "fixtures" / "stub_adapter.py")]


def test_load_manifest_reads_real_manifest():
    manifest = load_manifest(Path(__file__).parent.parent / "vectors/v1/manifest.json")
    assert manifest["profile_versions"]["rfc8949"] == "rfc8949-profile-1"


def test_validate_vectors_accepts_real_rfc8949_vectors():
    validator = load_schema_validator(Path(__file__).parent.parent / "logical-value.schema.json")
    vectors = json.loads(
        (Path(__file__).parent.parent / "vectors/v1/cbor/rfc8949/hand-written/integers.json").read_text()
    )
    validate_vectors(vectors, validator)  # must not raise


def test_validate_vectors_rejects_bad_logical_value():
    validator = load_schema_validator(Path(__file__).parent.parent / "logical-value.schema.json")
    bad_vectors = [{"id": "bad", "rule": "P1", "rationale": "x", "logical_value": {"type": "nonsense"}, "expected_hex": ""}]
    with pytest.raises(Exception):
        validate_vectors(bad_vectors, validator)


def test_run_encode_mode_reports_pass_and_fail_against_stub():
    vectors = [
        {"id": "v0", "rule": "R", "expected_hex": "aa"},
        {"id": "v1", "rule": "R", "expected_hex": "bb"},
        {"id": "v2", "rule": "R", "expected_hex": "zz"},
    ]
    results = run_encode_mode(STUB_ADAPTER + ["encode", "--profile", "stub"], vectors)
    assert [r["passed"] for r in results] == [True, False, False]
    assert results[0]["actual_hex"] == "aa"
    assert results[1]["actual_hex"] == ""


def test_run_encode_mode_defaults_missing_output_line_to_empty_hex():
    # An adapter that dies partway through a batch writes fewer stdout lines
    # than vectors given. run_encode_mode must default the unmatched
    # vector(s) to actual_hex="" (always a failing comparison) rather than
    # raising an IndexError.
    truncating_adapter = [sys.executable, "-c", "import sys; sys.stdin.read(); print('aa')"]
    vectors = [
        {"id": "v0", "rule": "R", "expected_hex": "aa"},
        {"id": "v1", "rule": "R", "expected_hex": "bb"},
    ]
    results = run_encode_mode(truncating_adapter, vectors)
    assert [r["passed"] for r in results] == [True, False]
    assert results[1]["actual_hex"] == ""


def test_run_profile_raises_clean_error_when_binary_missing():
    validator = load_schema_validator(Path(__file__).parent.parent / "logical-value.schema.json")
    missing_binary = Path(__file__).parent / "fixtures" / "does_not_exist_adapter_binary"
    assert not missing_binary.exists()

    with pytest.raises(AdapterBinaryNotFoundError, match="cargo build --release"):
        run_profile(missing_binary, "rfc8949", [], validator)


def test_build_report_groups_by_rule():
    results = [
        {"id": "a", "rule": "P1", "passed": True, "expected_hex": "aa", "actual_hex": "aa"},
        {"id": "b", "rule": "P1", "passed": False, "expected_hex": "bb", "actual_hex": ""},
        {"id": "c", "rule": "P2", "passed": True, "expected_hex": "cc", "actual_hex": "cc"},
    ]
    report = build_report("rust", "rfc8949", results)
    by_rule = report["adapters"]["rust"]["rfc8949"]["encode"]["by_rule"]
    assert by_rule["P1"] == {"total": 2, "passed": 1, "failed_ids": ["b"]}
    assert by_rule["P2"] == {"total": 1, "passed": 1, "failed_ids": []}
    assert report["adapters"]["rust"]["rfc8949"]["encode"]["total"] == 3
    assert report["adapters"]["rust"]["rfc8949"]["encode"]["passed"] == 2
