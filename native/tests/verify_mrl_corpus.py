#!/usr/bin/env python3
"""Usage: python3 native/tests/verify_mrl_corpus.py BUILD-DIRECTORY.
Run the output helper on bundled original IndependentMDX fixtures, then compare
all log bytes against SHA-256 fingerprints from the unchanged upstream writer.
"""
from pathlib import Path
import sys, json, hashlib, subprocess, tempfile
root = Path(__file__).resolve().parent
fixture = root / "fixtures" / "IndependentMDX_0.33.5"
expected = json.loads((fixture / "expected_mrl.json").read_text())
exe = Path(sys.argv[1]).resolve() / ("mrl-export-tests.exe" if sys.platform == "win32" else "mrl-export-tests")
with tempfile.TemporaryDirectory() as temporary:
    out = Path(temporary)
    subprocess.run([str(exe), str(fixture), str(out)], check=True)
    for name, info in expected["files"].items():
        data = (out / name).read_bytes()
        assert len(data) == info["bytes"] and hashlib.sha256(data).hexdigest() == info["sha256"], name
    assert len(list(out.rglob("*.mrl"))) == len(expected["files"])
print(f'{len(expected["files"])} / {len(expected["files"])} upstream MRL files byte-identical')
