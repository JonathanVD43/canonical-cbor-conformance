# harness/report_markdown.py
"""Renders harness/report/latest.json (written by harness/run.py) into a
static interoperability dashboard: adapter x rule-category pass rate, per
profile and mode. Pure post-processing of the harness's own JSON output —
no new test execution, no new infrastructure (writes to GITHUB_STEP_SUMMARY
if set, otherwise stdout; also returns the markdown for the CI artifact
upload step)."""
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).parent.parent


def _cell(bucket: dict) -> str:
    total = bucket["total"]
    passed = bucket["passed"]
    pct = "n/a" if total == 0 else f"{100 * passed / total:.0f}%"
    return f"{passed}/{total} ({pct})"


def render_summary_table(report: dict) -> str:
    adapters = sorted(report["adapters"].keys())
    columns = [
        ("rfc8949", "encode"),
        ("rfc8949", "decode_strict"),
        ("dcbor", "encode"),
        ("dcbor", "decode_strict"),
    ]
    header = "| Adapter | " + " | ".join(f"{p} {m.replace('_', '-')}" for p, m in columns) + " | Overall |"
    sep = "|---" * (len(columns) + 2) + "|"
    lines = [header, sep]
    for name in adapters:
        adapter_report = report["adapters"][name]
        row = [name]
        total = passed = 0
        for profile, mode in columns:
            bucket = adapter_report.get(profile, {}).get(mode)
            if bucket is None:
                row.append("n/a")
                continue
            row.append(_cell(bucket))
            total += bucket["total"]
            passed += bucket["passed"]
        overall = "n/a" if total == 0 else f"{passed}/{total} ({100 * passed / total:.0f}%)"
        row.append(overall)
        lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines)


def render_rule_breakdown_table(report: dict, profile: str, mode: str) -> str:
    adapters = sorted(report["adapters"].keys())
    rules: list[str] = []
    for name in adapters:
        by_rule = report["adapters"][name].get(profile, {}).get(mode, {}).get("by_rule", {})
        for rule in by_rule:
            if rule not in rules:
                rules.append(rule)
    rules.sort()
    if not rules:
        return "_no data_"
    header = "| Rule | " + " | ".join(adapters) + " |"
    sep = "|---" * (len(adapters) + 1) + "|"
    lines = [header, sep]
    for rule in rules:
        row = [rule]
        for name in adapters:
            by_rule = report["adapters"][name].get(profile, {}).get(mode, {}).get("by_rule", {})
            bucket = by_rule.get(rule)
            row.append(_cell(bucket) if bucket else "n/a")
        lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines)


def render_markdown(report: dict) -> str:
    parts = [
        "# Conformance interoperability dashboard",
        "",
        "Adapter x rule-category pass rate, generated from the harness's own",
        "`harness/report/latest.json` - not a separate test run.",
        "",
        "## Summary",
        "",
        render_summary_table(report),
    ]
    for profile in ("rfc8949", "dcbor"):
        for mode in ("encode", "decode_strict"):
            parts.append("")
            parts.append(f"## {profile} - {mode.replace('_', '-')} by rule")
            parts.append("")
            parts.append(render_rule_breakdown_table(report, profile, mode))
    return "\n".join(parts) + "\n"


def main() -> int:
    report_path = ROOT / "harness/report/latest.json"
    if not report_path.exists():
        print(f"no report found at {report_path} - run harness/run.py first", file=sys.stderr)
        return 1
    report = json.loads(report_path.read_text())
    markdown = render_markdown(report)

    out_path = ROOT / "harness/report/dashboard.md"
    out_path.write_text(markdown, encoding="utf-8")

    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as f:
            f.write(markdown)
    else:
        print(markdown)
    return 0


if __name__ == "__main__":
    sys.exit(main())
