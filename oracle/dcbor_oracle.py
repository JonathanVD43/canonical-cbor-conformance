"""Thin Python-side subprocess wrapper around oracle/dcbor_oracle.rb."""
import json
import subprocess
from pathlib import Path

_RB_SCRIPT = Path(__file__).parent / "dcbor_oracle.rb"


def encode_dcbor(value: dict) -> bytes:
    try:
        result = subprocess.run(
            ["ruby", str(_RB_SCRIPT)],
            input=json.dumps(value),
            capture_output=True,
            text=True,
            cwd=str(_RB_SCRIPT.parent),
            check=True,
        )
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"dcbor_oracle.rb failed: {e.stderr}") from e
    return bytes.fromhex(result.stdout.strip())
