"""Opt-in native 4x Vulkan correctness fixture, not a performance measurement."""
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
p.add_argument('--discard-hit', type=int, choices=[0, 1], default=0)
p.add_argument('--omit-hit', type=int, choices=[0, 1], default=1)
p.add_argument('--resolve', type=int, choices=[0, 1], default=0)
p.add_argument('--map', default='Hel3a')
p.add_argument('--world-features', type=int, default=0)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
lock = open(a.output.parent / 'android-case.lock', 'w')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
package = 'org.runeclassic.port'

def adb(*args):
    return subprocess.check_output(['adb', '-s', a.device, *args], timeout=25)

adb('shell', 'am', 'force-stop', package)
adb('shell', 'run-as', package, 'rm', '-f', 'files/native-run.log')
adb('shell', 'am', 'start', '-n', package + '/.MainActivity',
    '--es', 'map', a.map + '.run', '--es', 'saveTest', 'renderer-validation',
    '--ez', 'upscale', 'false', '--ez', 'enhanced', 'false', '--ei', 'msaa', '2',
    '--ei', 'clarity', '0', '--ei', 'worldFeatures', str(a.world_features),
    '--ei', 'seed', '1', '--ei', 'frames', '240', '--ez', 'validation', 'true',
    '--ez', 'vulkanProbe', 'true', '--ei', 'discardHit', str(a.discard_hit),
    '--ei', 'inpassResolve', str(a.resolve), '--ei', 'omitHit', str(a.omit_hit))
log = ''
deadline = time.monotonic() + 150
while time.monotonic() < deadline:
    time.sleep(1)
    try:
        log = adb('shell', 'run-as', package, 'cat', 'files/native-run.log').decode()
    except subprocess.CalledProcessError:
        continue
    (a.output / 'engine.log').write_text(log)
    if 'BOUNDED_RUN_COMPLETE' in log or 'LAUNCH_FAILED' in log:
        break
errors = re.findall(r'^.*\[vulkan (?:error|warning)\].*$', log, re.M)
passed = ('RUNE_VULKAN_VALIDATION active=1 synchronization=1' in log
          and 'RUNE_VULKAN_PROBE_OK' in log and 'BOUNDED_RUN_COMPLETE' in log
          and 'RUNE_CLARITY_PROBE_OK' in log
          and 'RUNE_CAMERA_COVERAGE_OK' in log
          and 'RUNE_PIXEL_LIGHTING_OK' in log
          and 'RUNE_PIXEL_WORLD_OK' in log and 'RUNE_DYNAMIC_SHADOW_OK' in log
          and 'RUNE_WATER_OK' in log and 'RUNE_WATER_CACHE_OK' in log and 'RUNE_WATER_APPROXIMATE_OK' in log
          and 'RUNE_MATERIAL_MAPS_GPU_OK' in log and not errors)
result = {'passed': passed, 'validation_messages': errors,
          'discard_hit': a.discard_hit, 'omit_hit': a.omit_hit, 'inpass_resolve': a.resolve,
          'map': a.map, 'world_features': a.world_features, 'native': True, 'msaa_samples': 4,
          'clarity_pixels': 'RUNE_CLARITY_PROBE_OK' in log}
result['pixel_lighting_pixels'] = 'RUNE_PIXEL_LIGHTING_OK' in log
result['camera_coverage_pixels'] = 'RUNE_CAMERA_COVERAGE_OK' in log
result['water_approximate_pixels'] = 'RUNE_WATER_APPROXIMATE_OK' in log
result['water_cache_pixels'] = 'RUNE_WATER_CACHE_OK' in log
(a.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
if 'BOUNDED_RUN_COMPLETE' in log:
    (a.output / 'frame.ppm').write_bytes(adb('exec-out', 'run-as', package, 'cat', 'files/frame.ppm'))
print(json.dumps(result), flush=True)
assert passed, 'Renderer check failed; inspect engine.log'
