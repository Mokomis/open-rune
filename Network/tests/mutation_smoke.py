"""Confirm tests fail when four deliberate defects are introduced.

Only temporary copies are compiled. Repository sources are never modified.
Usage: python3 Network/tests/mutation_smoke.py [C++ compiler]
"""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = sys.argv[1] if len(sys.argv) > 1 else "clang++"
mutations = [
    ("invalid_magic", "Protocol.cpp", "magic != 0x4f524e31 || ", "", "contract"),
    ("duplicate_packet", "Protocol.cpp", " && !contains(n)", "", "contract"),
    ("expired_pending_state", "Transport.cpp", "lastNow-m.createdMs < m.lifetimeMs", "true", "contract"),
    ("reliable_starves_state", "Transport.cpp", "if (l && laneInFlight[l]", "if (false && laneInFlight[l]", "impairments"),
]
results = []
with tempfile.TemporaryDirectory(prefix="open-rune-net-mutations-") as directory:
    work = Path(directory)
    for name, filename, old, new, test in mutations:
        text = (root / "src" / filename).read_text()
        assert text.count(old) == 1, f"mutation anchor changed: {name}"
        mutated = work / filename
        mutated.write_text(text.replace(old, new))
        sources = [mutated if p.name == filename else p for p in
                   (root / "src" / "Protocol.cpp", root / "src" / "Transport.cpp", root / "src" / "ClockSync.cpp")]
        executable = work / name
        subprocess.run([compiler, "-std=c++20", "-O1", "-I", str(root / "include"),
                        *(str(p) for p in sources), str(root / "tests" / "NetTests.cpp"),
                        "-o", str(executable)], check=True, capture_output=True)
        run = subprocess.run([str(executable), test], capture_output=True, text=True)
        assert run.returncode == 1 and "FAIL line " in run.stderr, (name, run.stdout, run.stderr)
        results.append({"mutation": name, "detected": True, "failure": run.stderr.strip()})
print(json.dumps(results, indent=2))
