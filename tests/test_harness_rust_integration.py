import json
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))
from harness.run import (
    build_report,
    load_adapters,
    load_manifest,
    load_schema_validator,
    run_decode_strict_profile,
    run_encode_mode,
    run_profile,
)

ROOT = Path(__file__).parent.parent


@pytest.fixture(scope="session", autouse=True)
def built_rust_adapter():
    subprocess.run(
        ["cargo", "build", "--release", "--manifest-path", str(ROOT / "adapters/rust/Cargo.toml")],
        check=True,
    )


def test_rust_adapter_passes_all_rfc8949_hand_written_vectors():
    manifest = load_manifest(ROOT / "vectors/v1/manifest.json")
    adapters = load_adapters(ROOT / "harness/adapters.json")
    validator = load_schema_validator(ROOT / "logical-value.schema.json")

    rust_adapter = next(a for a in adapters["adapters"] if a["name"] == "rust")
    binary_path = ROOT / rust_adapter["binary"]
    vector_files = manifest["hand_written_files"]["rfc8949"]

    results = run_profile(binary_path, "rfc8949", vector_files, validator)

    failures = [r for r in results if not r["passed"]]
    assert failures == [], f"unexpected mismatches: {failures}"
    assert len(results) == manifest["corpus_stats"]["rfc8949_hand_written_count"]


def test_rust_adapter_passes_all_dcbor_hand_written_vectors():
    manifest = load_manifest(ROOT / "vectors/v1/manifest.json")
    adapters = load_adapters(ROOT / "harness/adapters.json")
    validator = load_schema_validator(ROOT / "logical-value.schema.json")

    rust_adapter = next(a for a in adapters["adapters"] if a["name"] == "rust")
    binary_path = ROOT / rust_adapter["binary"]
    vector_files = manifest["hand_written_files"]["dcbor"]

    results = run_profile(binary_path, "dcbor", vector_files, validator)

    failures = [r for r in results if not r["passed"]]
    assert failures == [], f"unexpected mismatches: {failures}"
    assert len(results) == manifest["corpus_stats"]["dcbor_hand_written_count"]


def test_rejected_vector_mid_batch_does_not_shift_subsequent_results():
    # Regression test for the adapter/harness line-alignment contract: the
    # real Rust adapter must emit exactly one stdout line per input line,
    # even when a line is rejected (e.g. a profile-rule violation, not just
    # malformed JSON). If it ever regresses to skipping the print on
    # failure, every vector after the failure shifts by one and this test
    # will fail even though it doesn't merely re-assert the adapter's own
    # unit-tested stdout behavior -- it goes through run_encode_mode exactly
    # as the harness does.
    adapters = load_adapters(ROOT / "harness/adapters.json")
    rust_adapter = next(a for a in adapters["adapters"] if a["name"] == "rust")
    binary_path = ROOT / rust_adapter["binary"]
    cmd = [str(binary_path), "encode", "--profile", "rfc8949"]

    vectors = [
        {"id": "before", "rule": "R", "expected_hex": "00", "logical_value": {"type": "int", "value": "0"}},
        {
            # Bignum magnitude fits the native 64-bit range: rfc8949.rs's
            # encode_bignum rejects this with an Err (a well-formed, schema-
            # valid input rejected by encoding policy, not malformed JSON).
            "id": "rejected",
            "rule": "P-bignum-native-range",
            "expected_hex": "irrelevant",
            "logical_value": {"type": "bignum", "sign": "positive", "value": "100"},
        },
        {"id": "after", "rule": "R", "expected_hex": "17", "logical_value": {"type": "int", "value": "23"}},
    ]

    results = run_encode_mode(cmd, vectors)

    assert results[0]["id"] == "before"
    assert results[0]["actual_hex"] == "00"
    assert results[0]["passed"] is True

    assert results[1]["id"] == "rejected"
    assert results[1]["passed"] is False

    # This is the crux of the regression: "after" must still be compared
    # against its own expected_hex, not against the rejected vector's
    # leftover stdout.
    assert results[2]["id"] == "after"
    assert results[2]["actual_hex"] == "17"
    assert results[2]["passed"] is True


def test_rust_adapter_passes_all_decode_strict_vectors():
    manifest = load_manifest(ROOT / "vectors/v1/manifest.json")
    adapters = load_adapters(ROOT / "harness/adapters.json")

    rust_adapter = next(a for a in adapters["adapters"] if a["name"] == "rust")
    binary_path = ROOT / rust_adapter["binary"]

    for profile in ["rfc8949", "dcbor"]:
        results = run_decode_strict_profile(binary_path, profile, manifest)
        failures = [r for r in results if not r["passed"]]
        assert failures == [], f"unexpected mismatches for {profile}: {failures}"
        expected_count = (
            manifest["corpus_stats"][f"{profile}_hand_written_count"]
            + manifest["corpus_stats"][f"{profile}_strict_decode_reject_count"]
        )
        assert len(results) == expected_count


def test_full_main_run_produces_report():
    from harness.run import main

    exit_code = main()
    assert exit_code == 0

    report = json.loads((ROOT / "harness/report/latest.json").read_text())
    for profile in ["rfc8949", "dcbor"]:
        encode_report = report["adapters"]["rust"][profile]["encode"]
        assert encode_report["total"] == encode_report["passed"]
        assert encode_report["total"] > 0

        decode_report = report["adapters"]["rust"][profile]["decode_strict"]
        assert decode_report["total"] == decode_report["passed"]
        assert decode_report["total"] > 0
