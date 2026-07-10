#!/usr/bin/env python3
"""Convert cargo-fuzz corpus files into oracle-validated fuzz-generated vectors.

Pipeline per profile (rfc8949, dcbor):
  1. Run the `replay` binary over the raw corpus dir to reconstruct each raw
     fuzz file's LogicalValue as JSON (adapters/rust/fuzz/src/bin/replay.rs).
  2. Dedupe candidates by canonical JSON, preferring coverage-guided files
     (sha1-named) over random padding files (rand_*.bin) on collision.
  3. Validate every unique candidate through the independent oracle
     (oracle/rfc8949_oracle.py or oracle/dcbor_oracle_batch.rb), discarding
     anything the oracle rejects.
  4. Cross-check survivors against the Rust reference adapter's own encoder
     (batch mode, one subprocess for the whole set): keep only candidates the
     adapter also accepts and encodes to the identical bytes. This is a
     consistency filter, never the grader -- the oracle's bytes are always
     what gets committed as expected_hex. Candidates the adapter rejects
     (e.g. plain ints outside native range, which the generator deliberately
     produces to exercise the encoder's rejection path) are dropped, and any
     adapter/oracle byte mismatch is reported rather than silently dropped.
  5. Cap the kept set at 5000, prioritizing coverage-guided candidates.
  6. Write vector file(s) + generation-report.md.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "oracle"))
import rfc8949_oracle  # noqa: E402

REPLAY_BIN = ROOT / "adapters/rust/fuzz/target/release/replay"
ADAPTER_BIN = ROOT / "adapters/rust/target/release/adapter"
DCBOR_BATCH_RB = ROOT / "oracle/dcbor_oracle_batch.rb"
CAP = 5000


def find_replay_bin() -> Path:
    for candidate in [
        REPLAY_BIN,
        ROOT / "adapters/rust/fuzz/target/x86_64-apple-darwin/release/replay",
        ROOT / "adapters/rust/fuzz/target/aarch64-apple-darwin/release/replay",
    ]:
        if candidate.exists():
            return candidate
    raise SystemExit(f"replay binary not found (looked at {REPLAY_BIN} and siblings)")


def run_replay(corpus_dir: Path) -> list[dict]:
    replay_bin = find_replay_bin()
    result = subprocess.run(
        [str(replay_bin), str(corpus_dir)],
        capture_output=True, text=True, check=True,
    )
    records = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if line:
            records.append(json.loads(line))
    return records


def dedupe(records: list[dict]) -> list[dict]:
    """Keep one entry per unique logical_value, preferring coverage-guided
    (non rand_*) source files when the same value appears from both."""
    records = sorted(records, key=lambda r: r["file"].startswith("rand_"))
    seen: dict[str, dict] = {}
    for r in records:
        key = json.dumps(r["logical_value"], sort_keys=True)
        if key not in seen:
            seen[key] = r
    return list(seen.values())


def oracle_validate_rfc8949(candidates: list[dict]) -> list[tuple[dict, str]]:
    kept = []
    for r in candidates:
        try:
            expected = rfc8949_oracle.encode_rfc8949(r["logical_value"])
        except Exception:
            continue
        kept.append((r, expected.hex()))
    return kept


def oracle_validate_dcbor(candidates: list[dict]) -> list[tuple[dict, str]]:
    input_lines = "\n".join(
        json.dumps(r["logical_value"]) for r in candidates
    ) + "\n"
    result = subprocess.run(
        ["ruby", str(DCBOR_BATCH_RB)],
        input=input_lines, capture_output=True, text=True,
        cwd=str(DCBOR_BATCH_RB.parent), check=True,
    )
    out_lines = result.stdout.splitlines()
    if len(out_lines) != len(candidates):
        raise RuntimeError(
            f"dcbor_oracle_batch.rb line-count mismatch: "
            f"{len(candidates)} in, {len(out_lines)} out"
        )
    kept = []
    for r, hex_line in zip(candidates, out_lines):
        hex_line = hex_line.strip()
        if hex_line:
            kept.append((r, hex_line))
    return kept


def adapter_cross_check(profile: str, kept: list[tuple[dict, str]]) -> tuple[list[tuple[dict, str]], list[dict]]:
    """Run the reference Rust adapter over every oracle-validated candidate in
    one batch subprocess. Keep only candidates the adapter also accepts and
    matches byte-for-byte; return (kept, mismatches) separately."""
    input_lines = "\n".join(
        json.dumps(r["logical_value"]) for r, _ in kept
    ) + "\n"
    result = subprocess.run(
        [str(ADAPTER_BIN), "encode", "--profile", profile],
        input=input_lines, capture_output=True, text=True,
    )
    out_lines = result.stdout.splitlines()
    if len(out_lines) != len(kept):
        raise RuntimeError(
            f"adapter output line-count mismatch for {profile}: "
            f"{len(kept)} in, {len(out_lines)} out"
        )
    final = []
    mismatches = []
    for (r, expected_hex), actual_hex in zip(kept, out_lines):
        actual_hex = actual_hex.strip()
        if not actual_hex:
            continue  # adapter rejected -- excluded, not an error
        if actual_hex != expected_hex:
            mismatches.append({
                "file": r["file"],
                "logical_value": r["logical_value"],
                "oracle_hex": expected_hex,
                "adapter_hex": actual_hex,
            })
            continue
        final.append((r, expected_hex))
    return final, mismatches


def cap_preferring_coverage(kept: list[tuple[dict, str]], cap: int) -> list[tuple[dict, str]]:
    coverage = [x for x in kept if not x[0]["file"].startswith("rand_")]
    padding = [x for x in kept if x[0]["file"].startswith("rand_")]
    if len(coverage) >= cap:
        return coverage[:cap]
    return coverage + padding[: cap - len(coverage)]


def write_vectors(profile: str, final: list[tuple[dict, str]]) -> Path:
    out_dir = ROOT / f"vectors/v1/cbor/{profile}/fuzz-generated"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "vectors.json"
    vectors = []
    for i, (r, hex_str) in enumerate(final):
        vectors.append({
            "id": f"fuzz-{profile}-{i:05d}",
            "rule": "fuzz-generated",
            "source_file": r["file"],
            "logical_value": r["logical_value"],
            "expected_hex": hex_str,
        })
    out_path.write_text(json.dumps(vectors, indent=2) + "\n")
    return out_path


def process_profile(profile: str, corpus_dir: Path) -> dict:
    raw = run_replay(corpus_dir)
    coverage_raw = [r for r in raw if not r["file"].startswith("rand_")]
    padding_raw = [r for r in raw if r["file"].startswith("rand_")]
    unique = dedupe(raw)

    if profile == "rfc8949":
        oracle_kept = oracle_validate_rfc8949(unique)
    else:
        oracle_kept = oracle_validate_dcbor(unique)

    adapter_kept, mismatches = adapter_cross_check(profile, oracle_kept)
    final = cap_preferring_coverage(adapter_kept, CAP)
    out_path = write_vectors(profile, final)

    final_coverage = sum(1 for r, _ in final if not r["file"].startswith("rand_"))
    final_padding = len(final) - final_coverage

    return {
        "profile": profile,
        "raw_total": len(raw),
        "raw_coverage": len(coverage_raw),
        "raw_padding": len(padding_raw),
        "unique_candidates": len(unique),
        "oracle_validated": len(oracle_kept),
        "adapter_confirmed": len(adapter_kept),
        "adapter_mismatches": mismatches,
        "final_count": len(final),
        "final_coverage": final_coverage,
        "final_padding": final_padding,
        "out_path": str(out_path.relative_to(ROOT)),
    }


def write_report(stats: list[dict]) -> None:
    report_path = ROOT / "vectors/v1/generation-report.md"
    lines = [
        "# Fuzz-generated vector corpus: generation report",
        "",
        "Generation method: cargo-fuzz (libFuzzer + `arbitrary` crate) used as a "
        "coverage-guided **input selector** against the Rust adapter's own "
        "encoders (`adapter::rfc8949::encode` / `adapter::dcbor::encode`, "
        "instrumented via SanitizerCoverage). The encoder is never the grader: "
        "every surviving candidate's expected bytes come from the independent "
        "oracle (`oracle/rfc8949_oracle.py` wrapping `cbor2`; "
        "`oracle/dcbor_oracle_batch.rb` wrapping the `cbor-dcbor` Ruby gem).",
        "",
        "Because coverage-guided fuzzing naturally saturates once all reachable "
        "coverage edges are hit (converging around 645 rfc8949 / 1170 dcbor "
        "corpus files after ~3M runs each), the raw corpus was padded with "
        "additional random-byte files (`rand_*.bin`, decoded through the same "
        "shared `generate()` function) to reach a larger candidate pool before "
        "oracle validation and capping at 5000/profile.",
        "",
        "Toolchain: cargo-fuzz (nightly rustc via `cargo +nightly fuzz`), "
        "`arbitrary` 1.x with the `derive` feature.",
        "",
    ]
    for s in stats:
        lines += [
            f"## {s['profile']}",
            "",
            f"- Raw corpus files replayed: {s['raw_total']} "
            f"({s['raw_coverage']} coverage-guided + {s['raw_padding']} random padding)",
            f"- Unique candidates after dedup: {s['unique_candidates']}",
            f"- Oracle-validated: {s['oracle_validated']}",
            f"- Adapter-confirmed (also encodes, byte-identical to oracle): {s['adapter_confirmed']}",
            f"- Adapter/oracle mismatches (excluded, flagged for follow-up): {len(s['adapter_mismatches'])}",
            f"- Final committed vector count: {s['final_count']} "
            f"({s['final_coverage']} coverage-guided + {s['final_padding']} random padding)",
            f"- Vector file: `{s['out_path']}`",
            "",
        ]
        if s["adapter_mismatches"]:
            lines.append("### Adapter/oracle mismatches (excluded from corpus)")
            lines.append("")
            for m in s["adapter_mismatches"][:20]:
                lines.append(
                    f"- `{m['file']}`: oracle=`{m['oracle_hex']}` "
                    f"adapter=`{m['adapter_hex']}`"
                )
            lines.append("")
    report_path.write_text("\n".join(lines))
    print(f"wrote {report_path.relative_to(ROOT)}")


def main() -> None:
    stats = []
    for profile, corpus_dir in [
        ("rfc8949", ROOT / "adapters/rust/fuzz/corpus/rfc8949"),
        ("dcbor", ROOT / "adapters/rust/fuzz/corpus/dcbor"),
    ]:
        print(f"processing {profile}...", file=sys.stderr)
        s = process_profile(profile, corpus_dir)
        stats.append(s)
        print(
            f"{profile}: {s['raw_total']} raw -> {s['unique_candidates']} unique -> "
            f"{s['oracle_validated']} oracle-valid -> {s['adapter_confirmed']} adapter-confirmed "
            f"-> {s['final_count']} final ({len(s['adapter_mismatches'])} mismatches)",
            file=sys.stderr,
        )
    write_report(stats)


if __name__ == "__main__":
    main()
