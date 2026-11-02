"""Bounded real-enemy tests against an installed prototype and its own assets.

Only stops/launches org.runeclassic.port. Leaves device policy and original game
data alone. Timing is frame based: results are gameplay checks, not benchmarks.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--device', required=True)
p.add_argument('--adb', default='adb')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--case', choices=['attack', 'block', 'unblocked', 'reach', 'stool-control', 'stool-attack'], required=True)
p.add_argument('--seed', type=int, default=1)
a = p.parse_args()
a.output.parent.mkdir(parents=True, exist_ok=True)
package = 'org.runeclassic.port'

def adb(*args):
    return subprocess.run([a.adb, '-s', a.device, *args], check=True, capture_output=True, timeout=20).stdout

extras = ['--es', 'enemy', 'GoblinWarrior', '--ei', 'enemyFrame', '240', '--es', 'shield', 'VikingShield']
frames = 1200
if a.case == 'attack':
    extras = ['--es', 'enemy', 'GoblinWarrior', '--ei', 'fire', '60', '--ei', 'fireHold', '8', '--ei', 'fireRepeat', '60']
elif a.case == 'block':
    extras += ['--ei', 'block', '200', '--ei', 'blockHold', '1600']
    frames = 1800  # Includes attacks after the shield breaks.
elif a.case == 'reach':
    extras += ['--ez', 'checkReach', 'true']
    frames = 360
elif a.case.startswith('stool'):
    extras = ['--es', 'target', 'Stool', '--ei', 'fire', '100' if a.case == 'stool-attack' else '0']
    frames = 300

adb('shell', 'am', 'force-stop', package)
adb('shell', 'run-as', package, 'rm', '-f', 'files/native-run.log')
adb('shell', 'am', 'start', '-n', package + '/.MainActivity', '--es', 'map', 'DM-Hildir.run',
    '--ei', 'seed', str(a.seed), '--ei', 'frames', str(frames), '--ez', 'traceCombat', 'true',
    '--ez', 'upscale', 'false', '--ez', 'enhanced', 'false', *extras)
deadline = time.monotonic() + 100
log = ''
captured = False
try:
    while time.monotonic() < deadline:
        time.sleep(.4)
        try:
            log = adb('shell', 'run-as', package, 'cat', 'files/native-run.log').decode()
        except subprocess.CalledProcessError:
            continue  # Activity may still be starting.
        # Capture a living encounter once the enemy begins fighting.
        if not captured and 'enemy_state=Fighting' in log:
            a.output.with_suffix('.png').write_bytes(adb('exec-out', 'screencap', '-p'))
            captured = True
        if 'BOUNDED_RUN_COMPLETE' in log or 'LAUNCH_FAILED' in log:
            break
finally:
    a.output.with_suffix('.log').write_text(log)

assert 'BOUNDED_RUN_COMPLETE' in log, 'Bounded run did not finish; see log'
assert not re.search(r'Unknown native|Script error:|LAUNCH_FAILED', log), 'Runtime error; see log'
rows = [dict(re.findall(r'(\w+)=(\S+)', line)) for line in log.splitlines() if 'RUNE_ENCOUNTER ' in line]
result = {'case': a.case, 'seed': a.seed, 'frames': frames, 'samples': len(rows)}
if rows:
    result.update(player_min_health=min(int(r['player_health']) for r in rows),
                  enemy_min_health=min(int(r['enemy_health']) for r in rows),
                  player_died=any(r['player_state'] == 'Dying' for r in rows),
                  enemy_died=any(r['enemy_state'] == 'Dying' or r['enemy_destroyed'] == '1' for r in rows))
if a.case == 'attack':
    assert result['enemy_died'] and result['player_min_health'] > 0, 'Expected a surviving player and defeated enemy'
elif a.case == 'block':
    protected = [r for r in rows if r['player_proxy'] == 'Defending' and r['player_shield'] == 'VikingShield']
    assert protected and all(int(r['player_health']) == 100 for r in protected), 'Raised shield failed to protect'
    assert any(int(r['shield_health']) < 70 for r in protected), 'Shield did not take damage'
    assert any(r['player_shield'] == 'None' and int(r['player_health']) < 100 for r in rows), 'No damage after shield break'
    assert 'RUNE_SWEEP_HIT actor=VikingShield' in log, 'No shield collision recorded'
elif a.case == 'unblocked':
    assert result['player_died'], 'Expected unblocked attacks to kill the player'
elif a.case == 'reach':
    assert 'RUNE_REACHABILITY_OK' in log, 'Reachability assertions did not run'
elif a.case == 'stool-attack':
    assert 'RUNE_COMBAT_TARGET_RESULT destroyed=1' in log, 'Attack failed to destroy stool'
elif a.case == 'stool-control':
    assert 'RUNE_COMBAT_TARGET_FINAL destroyed=0' in log, 'Control target did not survive'
result['passed'] = True
a.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
