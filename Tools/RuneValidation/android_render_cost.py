"""Compare reference/optimized rendering at native resolution and 4x MSAA.

Restarts Open Rune and uses isolated saves. Fixed-step mode captures matching
simulation frames; its FPS is a throughput measurement, not gameplay cadence.
--real-time measures actual SurfaceFlinger presentation instead. Keep the
screen unlocked and do not operate the controls during either comparison.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--adb', default='adb')
parser.add_argument('--device', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--cases', nargs='+', choices=('hildir', 'water', 'combat'),
                    default=['hildir', 'water', 'combat'])
parser.add_argument('--real-time', action='store_true')
parser.add_argument('--compare-lighting-cull', action='store_true', help='Compare the sustained-lighting optimization with early depth/relaxed precision held on')
parser.add_argument('--compare-shadow-receivers', action='store_true', help='Compare conservative shadow-receiver culling with prior optimizations held on')
parser.add_argument('--compare-uniform-shadow-data', action='store_true', help='Compare uniform versus storage buffers for frame-wide lighting data')
parser.add_argument('--world-features', type=int, default=8103)
parser.add_argument('--oppo-clocks', action='store_true', help='Read rooted OPPO GPU clocks during fixed-frame measurements')
args = parser.parse_args()
if sum((args.compare_lighting_cull, args.compare_shadow_receivers, args.compare_uniform_shadow_data)) > 1: parser.error('Select one comparison at a time')
args.output.mkdir(parents=True, exist_ok=False)
adb = [args.adb, '-s', args.device]
package = 'org.runeclassic.port'
cases = {
    'hildir': ('DM-Hildir.run', ['--es', 'probeStart', 'PlayerStart1', '--es', 'probeView',
                               '-1714.423218,1713.299194,-178,57032,0']),
    'water': ('Dwarf1wwheel.run', ['--es', 'probeView', '-1450,-2000,-650,52200,-3500']),
    'combat': ('DM-Hildir.run', ['--es', 'enemy', 'GoblinWarrior', '--ei', 'enemyFrame', '120',
        '--es', 'shield', 'VikingShield', '--ei', 'fire', '60', '--ei', 'fireHold', '8',
        '--ei', 'fireRepeat', '60', '--ei', 'block', '200', '--ei', 'blockHold', '1600',
        '--ez', 'traceCombat', 'true', '--ez', 'checkReach', 'true']),
}


def shell(*command):
    return subprocess.run(adb + ['shell', *command], capture_output=True,
                          text=True, check=True, timeout=25).stdout


def read_ppm(path):
    with path.open('rb') as stream:
        assert stream.readline() == b'P6\n'
        width, height = map(int, stream.readline().split())
        assert stream.readline() == b'255\n'
        pixels = stream.read()
    assert len(pixels) == width * height * 3
    return (width, height), pixels


def compare_pixels(reference, optimized):
    size, a = read_ppm(reference)
    other_size, b = read_ppm(optimized)
    assert size == other_size
    errors = bytes(abs(x - y) for x, y in zip(a, b))
    histogram = Counter(errors)
    cumulative, p99 = 0, 255
    for value in sorted(histogram):
        cumulative += histogram[value]
        if cumulative >= len(errors) * .99:
            p99 = value
            break
    pixel_errors = [max(errors[i:i+3]) for i in range(0, len(errors), 3)]
    result = {'mean_absolute_8bit': sum(errors) / len(errors),
              'p99_absolute_8bit': p99, 'max_absolute_8bit': max(errors),
              'pixels_any_gt3': sum(value > 3 for value in pixel_errors),
              'pixels_any_gt10': sum(value > 10 for value in pixel_errors),
              'pixel_count': size[0] * size[1]}
    return result


results = {}
try:
    for name in args.cases:
        map_name, options = cases[name]
        rows, encounter_logs = {}, {}
        for label, early_depth, full_precision in [('reference', 0, 1), ('optimized', 1, 0)]:
            if args.compare_lighting_cull or args.compare_shadow_receivers or args.compare_uniform_shadow_data:
                early_depth, full_precision = 1, 0
            prefix = args.output / (name + '-' + label)
            shell('am', 'force-stop', package)
            time.sleep(1)
            shell('run-as', package, 'rm', '-f', 'files/native-run.log', 'files/frame.ppm')
            fixed = [] if args.real_time else ['--ei', 'frames', '760', '--ei', 'probeTickHz', '120']
            shell('am', 'start', '-W', '-n', package + '/.MainActivity',
                '--es', 'saveTest', 'renderer-cost', '--es', 'map', map_name,
                '--ei', 'surfacePercent', '100', '--ei', 'msaa', '2', '--ei', 'seed', '1',
                '--ei', 'profile', '600', '--ei', 'worldFeatures', str(args.world_features),
                '--ez', 'enhanced', 'true', '--ei', 'clarity', '7',
                '--ei', 'opaqueEarlyDepth', str(early_depth),
                '--ei', 'fullPrecisionLighting', str(full_precision),
                *(['--ei', 'cullLighting', '0' if label=='reference' else '1'] if args.compare_lighting_cull else []), *(['--ei', 'shadowReceivers', '0' if label=='reference' else '1'] if args.compare_shadow_receivers else []), *(['--ei', 'uniformShadowData', '0' if label=='reference' else '1'] if args.compare_uniform_shadow_data else []), *fixed, *options)
            marker = 'RUNE_PROFILE frames=600' if args.real_time else 'BOUNDED_RUN_COMPLETE'
            deadline = time.monotonic() + 120
            log = ''
            clocks = []
            while time.monotonic() < deadline:
                time.sleep(1)
                log = shell('run-as', package, 'cat', 'files/native-run.log')
                if args.oppo_clocks:
                    # Read-only OPPO clock context; never change clock policy.
                    clocks.append(shell('su', '-c', "'cat /sys/class/kgsl/kgsl-3d0/gpuclk /sys/class/kgsl/kgsl-3d0/max_gpuclk /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage'").strip().splitlines())
                if marker in log or 'ANDROID_LAUNCH_FAILED' in log:
                    break
            prefix.with_suffix('.log').write_text(log)
            assert marker in log, f'{name}/{label}: no completion; inspect log and device'
            assert 'size=2520x1680 scale=1.000000 samples=4' in log, 'Expected OPPO native 4x target'
            assert f'RUNE_OPAQUE_EARLY_DEPTH enabled={early_depth}' in log
            assert f'RUNE_LIGHTING_PRECISION relaxed={1-full_precision}' in log
            if args.compare_lighting_cull:
                assert f'RUNE_LIGHTING_CULL enabled={int(label=="optimized")}' in log
            if args.compare_shadow_receivers:
                assert f'RUNE_SHADOW_RECEIVERS enabled={int(label=="optimized")}' in log
            if args.compare_uniform_shadow_data:
                assert f'RUNE_UNIFORM_SHADOW_DATA enabled={int(label=="optimized")}' in log
            assert f'RUNE_WORLD_FEATURES code={args.world_features}' in log
            assert not any(error in log for error in ('ANDROID_LAUNCH_FAILED', 'Script error:', 'Unknown native'))
            rows[label] = {'profiles': [line for line in log.splitlines() if line.startswith(
                ('RUNE_PROFILE ', 'RUNE_VULKAN_PROFILE ', 'RUNE_RENDER_PROFILE '))], 'clocks': clocks}
            encounter_logs[label] = [line for line in log.splitlines() if line.startswith('RUNE_ENCOUNTER ')]
            if args.real_time:
                subprocess.run([sys.executable, str(Path(__file__).with_name('android_present.py')),
                    '--adb', args.adb, '--device', args.device, '--samples', '24', '--oppo-clocks',
                    '--output', str(prefix)], check=True)
                rows[label]['display'] = json.loads(prefix.with_suffix('.json').read_text())
                image = subprocess.run(adb + ['exec-out', 'screencap', '-p'],
                                       capture_output=True, check=True, timeout=25).stdout
                prefix.with_suffix('.png').write_bytes(image)
            else:
                image = subprocess.run(adb + ['exec-out', 'run-as', package, 'cat', 'files/frame.ppm'],
                                       capture_output=True, check=True, timeout=25).stdout
                prefix.with_suffix('.ppm').write_bytes(image)
            print(name, label, rows[label]['profiles'], flush=True)
        if not args.real_time:
            rows['pixels'] = compare_pixels(args.output / (name + '-reference.ppm'),
                                            args.output / (name + '-optimized.ppm'))
            if name == 'combat':
                assert encounter_logs['reference'], 'No combat observations'
                assert encounter_logs['reference'] == encounter_logs['optimized'], 'Combat trajectory changed'
                rows['matching_encounter_observations'] = len(encounter_logs['reference'])
        results[name] = rows
        (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
finally:
    shell('am', 'force-stop', package)
