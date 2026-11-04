"""Mac/native corpse pickup, throw, guard, bot and snapshot regression.

Uses a fresh config and save folder for each case; never accesses Android or
starts a public host. Render checks are separate --probe-corpse bounded runs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--game', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--mode', choices=('single', 'network', 'team', 'all'), default='all')
    a = ap.parse_args()
    a.binary = a.binary.resolve(); a.game = a.game.resolve(); a.output = a.output.resolve()
    a.output.mkdir(parents=True, exist_ok=False)
    results = []
    for mode in ('single', 'network', 'team') if a.mode == 'all' else (a.mode,):
        case = a.output / mode; case.mkdir()
        args = [str(a.binary), '--rune-network-probe', '--rune-corpse-probe=' + mode,
                '--save-directory=' + str(case / 'saves'), str(a.game)]
        env = dict(os.environ, SURREALENGINE_CONFIG_DIR=str(case / 'config'), ALSOFT_DRIVERS='null')
        start = time.monotonic()
        with (case / 'game.log').open('w') as log:
            process = subprocess.run(args, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        log = (case / 'game.log').read_text(errors='replace')
        marker = re.search(r'RUNE_CORPSE_OK mode=(\w+) checks=(\d+) render_checks=(\d+) hits=(\d+)', log)
        result = dict(mode=mode, passed=process.returncode == 0 and bool(marker), returncode=process.returncode,
                      seconds=time.monotonic() - start, args=args)
        if marker:
            result.update(checks=int(marker[2]), transform_frames=int(marker[3]), contacts=int(marker[4]))
        results.append(result)
        print(json.dumps(result), flush=True)
        if not result['passed']:
            print('\n'.join(log.splitlines()[-20:]))
            break
    report = dict(binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(), cases=results,
                  passed=bool(results) and all(x['passed'] for x in results))
    (a.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
