# harness/run.py
"""Conformance harness: spawns registered language adapters (one process per
(mode, profile) combination — never per vector, per the adapter contract in
the design spec) and diffs their output against the committed vector corpus.
This module implements `encode` and `decode-strict` modes; `pipeline` mode is
a follow-on milestone."""
import json
import subprocess
import sys
from pathlib import Path

import jsonschema

ROOT = Path(__file__).parent.parent


class AdapterBinaryNotFoundError(Exception):
    """Raised by run_profile when the adapter binary hasn't been built yet."""


def load_manifest(path: Path) -> dict:
    return json.loads(path.read_text())


def load_adapters(path: Path) -> dict:
    return json.loads(path.read_text())


def load_schema_validator(schema_path: Path) -> jsonschema.Draft202012Validator:
    schema = json.loads(schema_path.read_text())
    return jsonschema.Draft202012Validator(schema)


def validate_vectors(vectors: list[dict], validator: jsonschema.Draft202012Validator) -> None:
    for vector in vectors:
        validator.validate(vector["logical_value"])


def run_encode_mode(cmd: list[str], vectors: list[dict]) -> list[dict]:
    """Feeds one JSON logical_value per line to `cmd`'s stdin (batch mode,
    single process for the whole vector list), returns one result dict per
    vector with pass/fail against that vector's committed expected_hex.
    A missing/short output line (e.g. an adapter that errored partway
    through) yields actual_hex="" for the unmatched vector, which always
    fails the comparison rather than raising."""
    input_text = "\n".join(json.dumps(v.get("logical_value", {})) for v in vectors)
    proc = subprocess.run(cmd, input=input_text, capture_output=True, text=True)
    output_lines = proc.stdout.rstrip("\n").split("\n") if proc.stdout.rstrip("\n") else []
    results = []
    for i, vector in enumerate(vectors):
        actual_hex = output_lines[i] if i < len(output_lines) else ""
        results.append(
            {
                "id": vector["id"],
                "rule": vector["rule"],
                "passed": actual_hex == vector["expected_hex"],
                "expected_hex": vector["expected_hex"],
                "actual_hex": actual_hex,
            }
        )
    return results


def run_decode_strict_mode(cmd: list[str], vectors: list[dict]) -> list[dict]:
    """Feeds one hex-encoded raw-CBOR input per line to `cmd`'s stdin (batch
    mode, single process for the whole vector list), returns one result dict
    per vector with pass/fail against that vector's expected output line
    (either "ACCEPT <hex>" or "REJECT <reason>"). Mirrors run_encode_mode's
    line-alignment contract: a missing output line always fails rather than
    raising."""
    input_text = "\n".join(v["input_hex"] for v in vectors)
    proc = subprocess.run(cmd, input=input_text, capture_output=True, text=True)
    output_lines = proc.stdout.rstrip("\n").split("\n") if proc.stdout.rstrip("\n") else []
    results = []
    for i, vector in enumerate(vectors):
        actual_line = output_lines[i] if i < len(output_lines) else ""
        results.append(
            {
                "id": vector["id"],
                "rule": vector["rule"],
                "passed": actual_line == vector["expected_line"],
                "expected_line": vector["expected_line"],
                "actual_line": actual_line,
            }
        )
    return results


def build_report(adapter_name: str, profile: str, results: list[dict], mode: str = "encode") -> dict:
    by_rule: dict[str, dict] = {}
    for r in results:
        bucket = by_rule.setdefault(r["rule"], {"total": 0, "passed": 0, "failed_ids": []})
        bucket["total"] += 1
        if r["passed"]:
            bucket["passed"] += 1
        else:
            bucket["failed_ids"].append(r["id"])
    total = len(results)
    passed = sum(1 for r in results if r["passed"])
    return {
        "adapters": {
            adapter_name: {
                profile: {
                    mode: {
                        "by_rule": by_rule,
                        "total": total,
                        "passed": passed,
                    }
                }
            }
        }
    }


def _merge_report(target: dict, source: dict) -> None:
    for adapter_name, profiles in source["adapters"].items():
        target_profiles = target["adapters"].setdefault(adapter_name, {})
        for profile, modes in profiles.items():
            target_profiles.setdefault(profile, {}).update(modes)


def run_profile(binary_path: Path, profile: str, vector_files: list[str], validator: jsonschema.Draft202012Validator) -> list[dict]:
    if not binary_path.exists():
        raise AdapterBinaryNotFoundError(
            f"adapter binary not found at {binary_path} — run: "
            f"cargo build --release --manifest-path adapters/rust/Cargo.toml"
        )
    vectors = []
    for rel_path in vector_files:
        vectors.extend(json.loads((ROOT / rel_path).read_text()))
    validate_vectors(vectors, validator)
    cmd = [str(binary_path), "encode", "--profile", profile]
    return run_encode_mode(cmd, vectors)


def build_decode_strict_vectors(manifest: dict, profile: str) -> list[dict]:
    """Every hand-written encode vector's expected_hex doubles as a
    decode-strict ACCEPT input (expected to re-encode byte-identical to
    itself); every strict-decode-reject vector is a REJECT input. See
    decode-vector.schema.json."""
    vectors = []
    # Fuzz-generated vectors are deliberately NOT included here: the fuzz
    # generator explores arbitrary tag numbers to exercise the encoders'
    # unconstrained tag handling (encode-side grammar), which is wider than
    # decode-strict's allow-listed tag set (decode.rs's ALLOWED_TAGS = [2, 3]).
    # Forcing every fuzz vector through the decode-strict ACCEPT path would
    # incorrectly treat legitimate decode-strict rejections (unknown tags) as
    # harness failures.
    for rel_path in manifest["hand_written_files"][profile]:
        for v in json.loads((ROOT / rel_path).read_text()):
            vectors.append(
                {
                    "id": v["id"],
                    "rule": v["rule"],
                    "input_hex": v["expected_hex"],
                    "expected_line": f"ACCEPT {v['expected_hex']}",
                }
            )
    for rel_path in manifest["strict_decode_reject_files"][profile]:
        for v in json.loads((ROOT / rel_path).read_text()):
            vectors.append(
                {
                    "id": v["id"],
                    "rule": v["rule"],
                    "input_hex": v["input_hex"],
                    "expected_line": f"REJECT {v['expected_reason']}",
                }
            )
    return vectors


def run_decode_strict_profile(binary_path: Path, profile: str, manifest: dict) -> list[dict]:
    if not binary_path.exists():
        raise AdapterBinaryNotFoundError(
            f"adapter binary not found at {binary_path} — run: "
            f"cargo build --release --manifest-path adapters/rust/Cargo.toml"
        )
    vectors = build_decode_strict_vectors(manifest, profile)
    cmd = [str(binary_path), "decode-strict", "--profile", profile]
    return run_decode_strict_mode(cmd, vectors)


def main() -> int:
    manifest = load_manifest(ROOT / "vectors/v1/manifest.json")
    adapters = load_adapters(ROOT / "harness/adapters.json")
    validator = load_schema_validator(ROOT / "logical-value.schema.json")

    report: dict = {"adapters": {}}
    exit_code = 0
    for adapter in adapters["adapters"]:
        name = adapter["name"]
        binary_path = ROOT / adapter["binary"]
        for profile in adapter["profiles"]:
            vector_files = manifest["hand_written_files"][profile] + manifest.get("fuzz_generated_files", {}).get(profile, [])
            try:
                results = run_profile(binary_path, profile, vector_files, validator)
            except AdapterBinaryNotFoundError as e:
                print(str(e), file=sys.stderr)
                return 1
            sub_report = build_report(name, profile, results)
            _merge_report(report, sub_report)
            if any(not r["passed"] for r in results):
                exit_code = 1

            decode_results = run_decode_strict_profile(binary_path, profile, manifest)
            decode_sub_report = build_report(name, profile, decode_results, mode="decode_strict")
            _merge_report(report, decode_sub_report)
            if any(not r["passed"] for r in decode_results):
                exit_code = 1

    report_dir = ROOT / "harness/report"
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / "latest.json").write_text(json.dumps(report, indent=2) + "\n")
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
