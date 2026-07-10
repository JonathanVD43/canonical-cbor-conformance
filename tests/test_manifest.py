import json
from pathlib import Path

MANIFEST = json.loads((Path(__file__).parent.parent / "vectors/v1/manifest.json").read_text())


def test_has_profile_versions():
    assert MANIFEST["profile_versions"]["rfc8949"] == "rfc8949-profile-1"
    assert MANIFEST["profile_versions"]["dcbor"].startswith("dcbor-profile-")


def test_hand_written_files_listed_and_exist():
    root = Path(__file__).parent.parent
    for profile, files in MANIFEST["hand_written_files"].items():
        assert len(files) > 0
        for rel_path in files:
            assert (root / rel_path).exists(), rel_path


def test_corpus_stats_match_actual_vector_counts():
    root = Path(__file__).parent.parent
    for profile in ["rfc8949", "dcbor"]:
        total = 0
        for rel_path in MANIFEST["hand_written_files"][profile]:
            total += len(json.loads((root / rel_path).read_text()))
        assert MANIFEST["corpus_stats"][f"{profile}_hand_written_count"] == total
