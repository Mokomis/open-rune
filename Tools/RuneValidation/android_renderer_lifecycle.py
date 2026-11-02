"""Check native 4x renderer background/resume without touching normal saves."""
import argparse
import fcntl
import json
from pathlib import Path
import re
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--device', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cycles', type=int, default=3)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
lock = open(a.output.parent / 'android-case.lock', 'w')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
package = 'org.runeclassic.port'

def adb(*args):
    return subprocess.check_output(['adb', '-s', a.device, *args], timeout=25)

def read_log():
    return adb('shell', 'run-as', package, 'cat', 'files/native-run.log').decode()

def frame(log):
    frames = re.findall(r'RUNE_CAMERA frame=(\d+)', log)
    return int(frames[-1]) if frames else 0

launch = ('shell', 'am', 'start', '-n', package + '/.MainActivity',
          '--es', 'map', 'Hel3a.run', '--es', 'saveTest', 'renderer-lifecycle',
          '--ez', 'upscale', 'false', '--ez', 'enhanced', 'false', '--ei', 'msaa', '2',
          '--ei', 'seed', '1', '--ez', 'validation', 'true', '--ez', 'cameraProbe', 'true',
          '--ei', 'omitHit', '1', '--ei', 'inpassResolve', '0')
adb('shell', 'am', 'force-stop', package)
adb('shell', 'run-as', package, 'rm', '-f', 'files/native-run.log')
adb(*launch)
log = ''
for _ in range(45):
    time.sleep(1)
    try:
        log = read_log()
    except subprocess.CalledProcessError:
        continue
    if frame(log) >= 120 or 'LAUNCH_FAILED' in log:
        break
assert frame(log) >= 120 and 'LAUNCH_FAILED' not in log, 'Launch failed'
assert 'RUNE_VULKAN_VALIDATION active=1 synchronization=1' in log
assert 'size=2520x1680 scale=1.000000 samples=4' in log
pid = adb('shell', 'pidof', package).strip()
results = []
for cycle in range(a.cycles):
    before = frame(read_log())
    adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
    time.sleep(3)
    background = read_log()
    (a.output / f'cycle-{cycle}-background.log').write_text(background)
    adb(*launch)
    time.sleep(5)
    log = read_log()
    (a.output / f'cycle-{cycle}-resumed.log').write_text(log)
    current_pid = subprocess.run(['adb', '-s', a.device, 'shell', 'pidof', package], capture_output=True).stdout.strip()
    passed = (current_pid == pid and frame(log) > before and 'LAUNCH_FAILED' not in log
              and 'LAUNCH_FAILED' not in background
              and not re.search(r'\[vulkan (?:error|warning)\]', log))
    results.append({'cycle': cycle, 'passed': passed, 'same_process': current_pid == pid,
                    'frame_before': before, 'frame_after': frame(log)})
    (a.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results[-1]), flush=True)
    if not passed:
        break
assert all(r['passed'] for r in results), 'Background/resume failed; inspect retained logs'
