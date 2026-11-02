"""Read actual SurfaceFlinger presentation timestamps without changing device settings."""
import argparse
import json
from pathlib import Path
import re
import shlex
import statistics
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--adb', default='adb')
parser.add_argument('--device', required=True)
parser.add_argument('--package', default='org.runeclassic.port')
parser.add_argument('--samples', type=int, default=60, help='Poll every half second')
parser.add_argument('--oppo-clocks', action='store_true', help='Read OPPO GPU/CPU clocks with existing root access; never change them')
parser.add_argument('--output', type=Path, required=True, help='Output filename prefix')
args = parser.parse_args()
if args.samples < 2:
    parser.error('--samples must be at least 2')

def shell(command):
    # Surface discovery precedes sampling, so a transient USB read can retry
    # without losing any timestamps. Fail a sampling read rather than invent FPS.
    attempts=5 if '--list' in command else 1
    for attempt in range(attempts):
        try:
            return subprocess.run([args.adb, '-s', args.device, 'shell', command],
                                  capture_output=True, text=True, check=True, timeout=15).stdout
        except subprocess.CalledProcessError:
            if attempt+1==attempts:raise
            time.sleep(.2)

candidates = []
for line in shell('dumpsys SurfaceFlinger --list').splitlines():
    if args.package in line and '(BLAST)' in line:
        name = re.sub(r'^RequestedLayerState\{', '', line).split(' parentId=')[0].rstrip('}')
        match = re.search(r'#(\d+)$', name)
        candidates.append((int(match.group(1)) if match else 0, name))
if not candidates:
    raise SystemExit('No game SurfaceView found. Launch the game and keep it visible.')
layer = max(candidates)[1]
rows = {}
clock_samples = []
clock_paths = {
    'gpu_hz': '/sys/class/kgsl/kgsl-3d0/gpuclk',
    'gpu_max_hz': '/sys/class/kgsl/kgsl-3d0/max_gpuclk',
    'gpu_busy': '/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage',
    'cpu0_khz': '/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq',
    'cpu0_max_khz': '/sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq',
    'cpu6_khz': '/sys/devices/system/cpu/cpufreq/policy6/scaling_cur_freq',
    'cpu6_max_khz': '/sys/devices/system/cpu/cpufreq/policy6/scaling_max_freq',
}
initial = None
for sample in range(args.samples):
    if args.oppo_clocks and sample % 4 == 0:
        command = 'cat ' + ' '.join(shlex.quote(path) for path in clock_paths.values())
        try:
            values = shell('su -c ' + shlex.quote(command)).splitlines()
            clock_samples.append({'sample': sample, **dict(zip(clock_paths, values))})
        except subprocess.SubprocessError as error:
            clock_samples.append({'sample': sample, 'error': str(error)})
    output = shell('dumpsys SurfaceFlinger --latency ' + shlex.quote(layer))
    lines = output.splitlines()
    refresh_period = lines[0] if lines else None
    current = []
    for line in lines[1:]:
        fields = line.split()
        if len(fields) == 3:
            values = list(map(int, fields))
            if 0 < values[1] < 2**63 - 1:
                current.append(values)
    if initial is None and current:
        initial = max(values[1] for values in current)
    for values in current:
        if initial is not None and values[1] >= initial:
            rows[values[1]] = values
    if sample + 1 < args.samples:
        time.sleep(0.5)

timestamps = sorted(rows)
if len(timestamps) < 2:
    raise SystemExit('No fresh presentation timestamps. No displayed-FPS result can be reported.')
intervals = [(b - a) / 1e6 for a, b in zip(timestamps, timestamps[1:])]
seconds = (timestamps[-1] - timestamps[0]) / 1e9
surface = shell('dumpsys SurfaceFlinger')
result = {
    'layer': layer, 'frames': len(timestamps), 'seconds': seconds,
    'presented_fps': (len(timestamps) - 1) / seconds,
    'median_ms': statistics.median(intervals),
    'p95_ms': sorted(intervals)[int((len(intervals) - 1) * 0.95)],
    'p99_ms': sorted(intervals)[int((len(intervals) - 1) * 0.99)],
    'max_ms': max(intervals),
    'intervals_over_50ms': sum(ms > 50 for ms in intervals),
    'surface_refresh_period_ns': refresh_period,
    'clock_samples': clock_samples,
    'hwc_fps_lines': [line.strip() for line in surface.splitlines() if 'FPS min:' in line],
}
if seconds >= 60:
    windows = []
    for start in range(0, int(seconds), 30):
        points = [t for t in timestamps if start <= (t-timestamps[0])/1e9 < start+30]
        if len(points)<2:
            continue
        elapsed=(points[-1]-points[0])/1e9
        gaps=sorted((b-a)/1e6 for a,b in zip(points,points[1:]))
        windows.append({'start_seconds':start, 'seconds':elapsed,
                        'presented_fps':(len(points)-1)/elapsed,
                        'p95_ms':gaps[int((len(gaps)-1)*.95)], 'max_ms':max(gaps)})
    result['windows_30s'] = windows
args.output.parent.mkdir(parents=True, exist_ok=True)
Path(str(args.output) + '.json').write_text(json.dumps(result, indent=2) + '\n')
Path(str(args.output) + '-timestamps.txt').write_text(
    '\n'.join('\t'.join(map(str, rows[timestamp])) for timestamp in timestamps) + '\n')
print(json.dumps(result, indent=2))
