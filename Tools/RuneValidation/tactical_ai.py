#!/usr/bin/env python3
"""Bounded laptop AI regressions using owned assets and isolated save slots."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--game", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--seeds", type=int, nargs="+", default=[73])
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
binary, game = args.binary.resolve(), args.game.resolve()
summary = {"binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
           "map": "DM-Hildir", "simulation_hz": 120, "display_fps_measured": False, "cases": []}
with tempfile.TemporaryDirectory(prefix="open-rune-tactics-") as temporary:
    for seed in args.seeds:
        for actor in ("DarkViking", "GoblinWarrior", "Dwarf"):
            name = f"{actor}-{seed}"
            saves = Path(temporary) / name
            saves.mkdir()
            config = Path(temporary) / (name + '-config')
            (config / 'SurrealEngine').mkdir(parents=True)
            started = time.monotonic()
            with (args.output / f"{name}.log").open("w") as log:
                result = subprocess.run([str(binary), "--direct", f"--rune-tactics-probe={actor}",
                    f"--probe-seed={seed}", f"--save-directory={saves}", str(game)],
                    env=dict(os.environ, SURREALENGINE_CONFIG_DIR=str(config)),
                    stdout=log, stderr=subprocess.STDOUT, timeout=90)
            text = (args.output / f"{name}.log").read_text()
            passed = result.returncode == 0 and f"TACTICS_RUNTIME_OK class={actor}" in text
            if actor == "DarkViking":
                passed = passed and all(marker in text for marker in
                    ("TACTICS_KICK", "TACTICS_BASH", "TACTICS_DEFENSE", "TACTICS_SAVE", "TACTICS_GROUP participants=3", "TACTICS_SPECIAL_GROUP_OK"))
            summary["cases"].append({"name": name, "passed": passed,
                "wall_seconds": round(time.monotonic()-started, 3),
                "results": [line for line in text.splitlines() if line.startswith("TACTICS_")]})
            (args.output / "summary.json").write_text(json.dumps(summary, indent=2)+"\n")
            print(f"{'PASS' if passed else 'FAIL'} {name}", flush=True)
            if not passed:
                raise SystemExit(f"Inspect {args.output / (name+'.log')}")
