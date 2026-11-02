"""Bounded waypoint aid for a paused, isolated Open Rune Android session.

This uses normal camera/movement input. It cannot plan around walls, climb a
ledge, or establish puzzle completion; inspect its screenshot and stop reason.
Start with Rune visible and paused in its original Escape menu. The tool
unpauses for input and pauses again before returning.
"""
import argparse
import json
import math
import pathlib
import re
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('x', type=float)
p.add_argument('y', type=float)
p.add_argument('z', type=float)
p.add_argument('--device', required=True)
p.add_argument('--output', type=pathlib.Path, required=True,
               help='Output prefix for PNG and JSON evidence')
p.add_argument('--steps', type=int, default=8)
a = p.parse_args()
a.output.parent.mkdir(parents=True, exist_ok=True)
adb = ['adb', '-s', a.device]
camera = re.compile(r'RUNE_CAMERA .*? actor_yaw=(-?\d+) .*? x=([-\d.]+) y=([-\d.]+) z=([-\d.]+) health=(-?\d+)')

def run(*args):
    return subprocess.run(adb + list(map(str, args)), check=True, capture_output=True, timeout=45).stdout

def state():
    lines = run('shell', 'run-as', 'org.runeclassic.port', 'tail', '-200', 'files/native-run.log').decode().splitlines()
    for line in reversed(lines):
        match = camera.search(line)
        if match:
            yaw, x, y, z, health = match.groups()
            return dict(yaw=int(yaw) % 65536, x=float(x), y=float(y), z=float(z), health=int(health))
    raise RuntimeError('No recent RUNE_CAMERA line')

def delta_yaw(target, current):
    return (target - current + 32768) % 65536 - 32768

trace = []
run('shell', 'input', 'keyevent', '111')
try:
    prior = state()
    for step in range(a.steps):
        dx, dy = a.x - prior['x'], a.y - prior['y']
        distance = math.hypot(dx, dy)
        if distance < 65 and abs(a.z - prior['z']) < 90:
            trace.append({'step': step, 'status': 'arrived', 'state': prior})
            break
        desired = round(math.atan2(dy, dx) * 65536 / (2 * math.pi)) % 65536
        for _ in range(6):
            yaw_error = delta_yaw(desired, prior['yaw'])
            if abs(yaw_error) < 2200: break
            pixels = max(130, min(800, round(abs(yaw_error) / 11)))
            start = 1350 if yaw_error > 0 else 2350
            run('shell', 'input', 'swipe', start, 750, start + (pixels if yaw_error > 0 else -pixels), 750, 400)
            time.sleep(0.15)
            prior = state()
        duration = min(1200, max(140, round(distance / 290 * 1000)))
        buttons = ['51']
        if a.z > prior['z'] + 50: buttons.append('62')
        elif a.z < prior['z'] - 70: buttons.append('31')
        procs = [subprocess.Popen(adb + ['shell', 'input', 'keyevent', '--duration', str(duration), b], stdout=subprocess.DEVNULL) for b in buttons]
        for proc in procs: proc.wait(timeout=45)
        time.sleep(0.65)  # RUNE_CAMERA samples every 60 frames.
        current = state()
        actual = math.hypot(current['x'] - prior['x'], current['y'] - prior['y'])
        trace.append({'step': step, 'desired_yaw': desired, 'duration_ms': duration, 'moved_xy': round(actual, 1), 'state': current})
        if current['health'] <= 0:
            trace[-1]['status'] = 'dead'
            break
        if actual < 25:
            trace[-1]['status'] = 'no_xy_progress'
            break
        prior = current
    else:
        trace.append({'status': 'step_limit'})
    pathlib.Path(str(a.output) + '.png').write_bytes(run('exec-out', 'screencap', '-p'))
finally:
    run('shell', 'input', 'keyevent', '111')
pathlib.Path(str(a.output) + '.json').write_text(json.dumps(trace, indent=2) + '\n')
print(json.dumps(trace, indent=2))
