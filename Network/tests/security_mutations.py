"""Check security/timing guards using temporary copies and a Release build.
Usage: python3 Network/tests/security_mutations.py build-net-release [clang++]
"""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()
compiler = sys.argv[2] if len(sys.argv) > 2 else "clang++"
cache = (build / "CMakeCache.txt").read_text()
source_line = next(line for line in cache.splitlines() if line.startswith("FETCHCONTENT_SOURCE_DIR_MBEDTLS:PATH="))
source = Path(source_line.split("=", 1)[1]) if source_line.split("=", 1)[1] else build / "_deps/mbedtls-src"
libraries = [build / "libopenrune_net.a"] + [build / "_deps/mbedtls-build/library" / f"lib{name}.a"
                                           for name in ("mbedtls", "mbedx509", "mbedcrypto")]
cases = [
    ("clock_future_guard", "ClockSync.cpp", "if (result > now && result-now > error) return {};", ""),
    ("clock_request_identity", "ClockSync.cpp", " || pending->id != id", ""),
    ("dtls_replay", "SecureChannel.cpp", "MBEDTLS_SSL_ANTI_REPLAY_ENABLED", "MBEDTLS_SSL_ANTI_REPLAY_DISABLED"),
    ("stale_clock_gate", "SecureSession.cpp", " && clock.ready(now)", ""),
]
results = []
with tempfile.TemporaryDirectory(prefix="open-rune-auth-mutations-") as directory:
    for name, filename, old, new in cases:
        text = (root / "src" / filename).read_text()
        assert text.count(old) == 1, name
        mutated = Path(directory) / filename
        mutated.write_text(text.replace(old, new))
        executable = Path(directory) / name
        subprocess.run([compiler, "-std=c++20", "-O1", "-pthread", "-I", str(root / "include"),
                        "-I", str(source / "include"), str(mutated), str(root / "tests/SecurityTests.cpp"),
                        *(str(p) for p in libraries), "-o", str(executable)], check=True, capture_output=True)
        run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        assert run.returncode == 1 and "FAIL check failed at line" in run.stderr, (name, run.stdout, run.stderr)
        results.append({"mutation": name, "detected": True, "failure": run.stderr.strip()})
print(json.dumps(results, indent=2))
