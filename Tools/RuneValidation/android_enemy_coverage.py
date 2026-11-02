"""Bounded original enemy AI encounters; no campaign traversal claim.

The fixture spawns an enemy in DM-Hildir, then uses ordinary attack input.
It records damage in both directions without restoring health or bypassing AI.
"""
import argparse
import fcntl
import json
from pathlib import Path
import re
import subprocess
import sys
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--device', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--enemies', nargs='+', default=['DwarfMech', 'SnowBeast', 'StoneGuard'])
p.add_argument('--frames', type=int, default=1800)
p.add_argument('--forward', type=int, default=0, help='Frames of ordinary forward input to approach a stationary enemy')
p.add_argument('--seed', type=int, default=1)
p.add_argument('--require-damage-both', action='store_true', help='Require both sides to take damage; omit for invulnerable scripted bosses')
p.add_argument('--fps-start', type=int, default=240)
p.add_argument('--fps-samples', type=int, default=5)
p.add_argument('--start', help='Placed PlayerStart name, resolved with normal world collision')
p.add_argument('--clarity', type=int, default=0, help='0=original, 13=Low/16x/Low')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
lock = open(a.output.parent / 'android-case.lock', 'w')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
package = 'org.runeclassic.port'

def adb(*args):
    return subprocess.check_output(['adb', '-s', a.device, *args], timeout=25)

results = []
for enemy in a.enemies:
    assert re.fullmatch(r'[A-Za-z0-9_]+', enemy)
    adb('shell', 'am', 'force-stop', package)
    adb('shell', 'run-as', package, 'rm', '-f', 'files/native-run.log')
    adb('shell', 'am', 'start', '-n', package + '/.MainActivity',
        '--es', 'map', 'DM-Hildir.run', '--es', 'saveTest', 'enemy-' + enemy,
        '--es', 'enemy', enemy, '--ei', 'seed', str(a.seed), '--ei', 'frames', str(a.frames),
        '--ei', 'msaa', '2', '--ez', 'upscale', 'false', '--ez', 'enhanced', 'false',
        '--ei', 'clarity', str(a.clarity),
        '--ei', 'fire', '60', '--ei', 'fireHold', '8', '--ei', 'fireRepeat', '60',
        '--ei', 'forward', str(a.forward),
        '--ez', 'traceCombat', 'true',
        *(['--es', 'probeStart', a.start] if a.start else []))
    log = ''
    captured = False
    measured = False
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        time.sleep(.5)
        try:
            log = adb('shell', 'run-as', package, 'cat', 'files/native-run.log').decode()
        except subprocess.CalledProcessError:
            continue
        encounter_frames = [int(x) for x in re.findall(r'RUNE_ENCOUNTER frame=(\d+)', log)]
        if not measured and encounter_frames and encounter_frames[-1] >= a.fps_start:
            subprocess.run([sys.executable, str(Path(__file__).with_name('android_present.py')),
                            '--device', a.device, '--samples', str(a.fps_samples),
                            '--output', str(a.output / (enemy + '-fps'))], check=True,
                           stdout=subprocess.DEVNULL)
            measured = True
        if not captured and 'enemy_state=Fighting' in log:
            (a.output / (enemy + '.png')).write_bytes(adb('exec-out', 'screencap', '-p'))
            captured = True
        if re.search(r'BOUNDED_RUN_COMPLETE|LAUNCH_FAILED|Script error:', log):
            break
    (a.output / (enemy + '.log')).write_text(log)
    rows = [dict(re.findall(r'(\w+)=(\S+)', line)) for line in log.splitlines() if 'RUNE_ENCOUNTER ' in line]
    initial = re.search(r'RUNE_ENEMY_SPAWN class=\S+ health=(-?\d+)', log)
    result = {'enemy': enemy, 'seed': a.seed, 'start': a.start, 'forward_frames': a.forward, 'completed': 'BOUNDED_RUN_COMPLETE' in log,
              'runtime_errors': re.findall(r'^.*(?:LAUNCH_FAILED|Script error:|Unknown native).*$', log, re.M),
              'states': sorted({r['enemy_state'] for r in rows}),
              'enemy_initial_health': int(initial[1]) if initial else None,
              'enemy_min_health': min((int(r['enemy_health']) for r in rows), default=None),
              'player_min_health': min((int(r['player_health']) for r in rows), default=None),
              'presented_fps': json.loads((a.output / (enemy + '-fps.json')).read_text())['presented_fps'] if measured else None,
              'native_4x': 'size=2520x1680 scale=1.000000 samples=4' in log,
              'clarity_settings': a.clarity,
              'coverage': 'spawned arena encounter with ordinary attack input; not campaign or every attack'}
    result['damage_both_directions'] = bool(rows and initial and result['player_min_health'] < 100
                                           and result['enemy_min_health'] < int(initial[1]))
    results.append(result)
    (a.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(result), flush=True)
    if result['runtime_errors']:
        break
assert all(r['completed'] and r['native_4x'] and not r['runtime_errors'] for r in results), 'Encounter failed; inspect logs'

if a.require_damage_both:
    assert all(r['damage_both_directions'] for r in results), 'Expected reciprocal damage did not occur'
