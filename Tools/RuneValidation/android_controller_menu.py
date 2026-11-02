"""Focused menu-routing regression using injected gamepad keys, not hardware proof.

Uses isolated saves, does not change preferences, and leaves the title visible.
The tablet must be unlocked. Physical stick/button confirmation is separate.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--device', required=True)
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
pkg = 'org.runeclassic.port'

def adb(*args):
    return subprocess.check_output(['adb', '-s', a.device, *args], timeout=40)

def tree():
    adb('shell', 'uiautomator', 'dump', '/sdcard/rune-controller-menu.xml')
    return ET.fromstring(adb('exec-out', 'cat', '/sdcard/rune-controller-menu.xml'))

def key(name):
    adb('shell', 'input', 'gamepad', 'keyevent', 'KEYCODE_' + name)
    time.sleep(.15)

def focused():
    return [n for n in tree().iter('node') if n.get('focused') == 'true']

def expect_focus(text):
    nodes = focused()
    assert any(n.get('text') == text for n in nodes), [n.attrib for n in nodes]

def tap(text):
    nodes = [n for n in tree().iter('node') if n.get('text') == text]
    assert nodes, text
    x, y, r, b = map(int, re.findall(r'\d+', nodes[-1].get('bounds')))
    adb('shell', 'input', 'tap', str((x+r)//2), str((y+b)//2))

def launch(game=False):
    adb('shell', 'am', 'force-stop', pkg)
    args = ['shell', 'am', 'start', '-n', pkg+'/.MainActivity',
            '--es', 'saveTest', 'controller-menu', '--ei', 'msaa', '2',
            '--ez', 'upscale', 'false', '--ez', 'controllerProbe', 'true']
    if game:
        args += ['--es', 'map', 'DM-Hildir.run']
    adb(*args)
    time.sleep(7)

def report(text):
    print('PASS: '+text, flush=True)
    results.append(text)

before = adb('exec-out', 'run-as', pkg, 'cat', 'shared_prefs/MainActivity.xml')
results = []
launch()
key('DPAD_DOWN'); expect_focus('Continue')
for text in ['New campaign', 'Load game', 'Settings']:
    key('DPAD_DOWN'); expect_focus(text)
key('BUTTON_A')
assert any(n.get('text') == 'Resolution & scaling' for n in tree().iter('node'))
report('Cold title launch: D-pad focus and A selection')

# Touch enters a modal; controller focus must stay inside it, including at edges.
tap('Frame rate & refresh')
nodes = [n for n in tree().iter('node') if n.get('content-desc', '').startswith('Frame rate target:')]
assert nodes, 'Frame target control missing'
x, y, r, b = map(int, re.findall(r'\d+', nodes[-1].get('bounds')))
adb('shell', 'input', 'tap', str((x+r)//2), str((y+b)//2))
for i in range(7):
    key('DPAD_DOWN')
    nodes = focused()
    assert nodes and nodes[0].get('text', '').replace('   ✓', '') in ['30 FPS', '60 FPS', '120 FPS', '144 FPS', '165 FPS', 'Custom…', '×'], [n.attrib for n in nodes]
key('BUTTON_B')
assert not any(n.get('text') == '120 FPS   ✓' for n in tree().iter('node'))
report('Touch-to-controller choice dialog, focus containment and B return')

key('BUTTON_B')
adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
adb('shell', 'am', 'start', '-n', pkg+'/.MainActivity')
time.sleep(1)
key('DPAD_DOWN')
assert any(n.get('class') == 'android.widget.Button' for n in focused())
report('Menu focus recovers after background/foreground')

launch(True)
key('BUTTON_START')
key('DPAD_DOWN'); expect_focus('Resume')
key('BUTTON_A')
assert any(n.get('text') == 'Menu' for n in tree().iter('node'))
report('Gameplay Start opens pause menu; A resumes')
launch()
assert before == adb('exec-out', 'run-as', pkg, 'cat', 'shared_prefs/MainActivity.xml'), 'Preferences changed'
(a.output/'title.png').write_bytes(adb('exec-out', 'screencap', '-p'))
(a.output/'results.json').write_text(json.dumps({'passes': results, 'preferences_identical': True}, indent=2)+'\n')
