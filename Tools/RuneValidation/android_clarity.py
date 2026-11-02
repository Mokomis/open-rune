"""Actual Android clarity controls plus native-4x A/B/A presentation samples.

Uses isolated saves. This is a short stationary comparison, not campaign or
sustained-performance validation. UI preferences finish at Low / 16x / Low.
"""
import argparse
import fcntl
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--device', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--samples', type=int, default=30)
p.add_argument('--map', default='Hel3a')
p.add_argument('--comparison-only', action='store_true', help='Skip already-verified individual setting controls')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
lock = open(a.output.parent / 'android-case.lock', 'w')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
package = 'org.runeclassic.port'

def adb(*args):
    return subprocess.check_output(['adb', '-s', a.device, *args], timeout=30)

def log():
    return adb('shell', 'run-as', package, 'cat', 'files/native-run.log').decode()

def wait_code(code):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        data = log()
        assert not re.search(r'LAUNCH_FAILED|Script error:|\[vulkan (?:error|warning)\]', data), data[-1500:]
        codes = re.findall(r'RUNE_CLARITY code=(\d+)', data)
        if codes and int(codes[-1]) == code and 'ANDROID_RENDER_TARGET' in data:
            return
        time.sleep(.3)
    raise AssertionError('Clarity setting was not applied: ' + str(code))

def tap(prefix):
    adb('shell', 'uiautomator', 'dump', '/data/local/tmp/rune-clarity.xml')
    xml = adb('shell', 'cat', '/data/local/tmp/rune-clarity.xml')
    for node in ET.fromstring(xml).iter('node'):
        if node.get('text', '').startswith(prefix):
            x1, y1, x2, y2 = map(int, re.findall(r'\d+', node.get('bounds')))
            adb('shell', 'input', 'tap', str((x1+x2)//2), str((y1+y2)//2))
            time.sleep(.25)
            return
    (a.output / 'missing-control.xml').write_bytes(xml)
    raise AssertionError('Visible control not found: ' + prefix)

def compare(code):
    tap('Clarity:')
    tap('Compare:')
    wait_code(code)

def setting(label, choice, code):
    tap('Clarity:')
    tap(label)
    tap(choice)
    wait_code(code)
    print('Applied ' + label + choice, flush=True)

def sample(name):
    time.sleep(3)
    subprocess.run([sys.executable, str(Path(__file__).with_name('android_present.py')),
        '--device', a.device, '--samples', str(a.samples), '--oppo-clocks',
        '--output', str(a.output / name)], check=True, stdout=subprocess.DEVNULL)
    (a.output / (name+'.png')).write_bytes(adb('exec-out', 'screencap', '-p'))
    result = json.loads((a.output / (name+'.json')).read_text())
    print(name + ': ' + str(result['presented_fps']) + ' displayed FPS', flush=True)
    return result['presented_fps']

adb('shell', 'am', 'force-stop', package)
adb('shell', 'am', 'start', '-n', package+'/.MainActivity',
    '--es', 'map', a.map+'.run', '--es', 'saveTest', 'clarity-comparison',
    '--ei', 'seed', '1', '--ei', 'msaa', '2', '--ei', 'profile', '240',
    '--ez', 'upscale', 'false', '--ez', 'enhanced', 'false', '--ei', 'clarity', '0')
time.sleep(4)
wait_code(0)
fps = {'original_a': sample('original-a')}
compare(13)
fps['adjusted'] = sample('adjusted')
compare(0)
fps['original_b'] = sample('original-b')
compare(13)
if not a.comparison_only:
    setting('Adaptive sharpening:', 'Medium', 14)
    setting('Adaptive sharpening:', 'Off', 12)
    setting('Adaptive sharpening:', 'Low', 13)
    setting('Shadow / midtone lift:', 'Medium', 21)
    setting('Shadow / midtone lift:', 'Off', 5)
    setting('Shadow / midtone lift:', 'Low', 13)
    setting('Texture filtering:', '8×', 9)
    setting('Texture filtering:', '16×', 13)
data = log()
(a.output / 'engine.log').write_text(data)
assert 'size=2520x1680 scale=1.000000 samples=4' in data
assert 'RUNE_TEXTURE_FILTER requested=16.000000 applied=16.000000' in data
assert 'ANDROID_PACING active=1' in data
(a.output / 'controls-final.png').write_bytes(adb('exec-out', 'screencap', '-p'))
(a.output / 'preferences-final.xml').write_bytes(adb('shell', 'run-as', package, 'cat', 'shared_prefs/MainActivity.xml'))
result = {'passed': True, 'map': a.map, 'native_4x_verified': True,
          'actual_android_settings': True, 'applied_anisotropy': 16,
          'individual_controls_tested': not a.comparison_only,
          'final_settings': {'sharp': 'low', 'filtering': 16, 'shadows': 'low'},
          'presented_fps': fps, 'coverage': 'short stationary A/B/A; device clocks can vary'}
(a.output / 'result.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result), flush=True)
