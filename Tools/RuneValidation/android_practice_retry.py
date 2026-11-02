"""Actual death-menu taps restart the same isolated practice fight twice.

The second retry has a copied campaign checkpoint present to ensure practice
recovery cannot accidentally load the campaign. Normal saves are read only.
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
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
package = 'org.runeclassic.port'
fixture = 'files/validation-saves/practice-retry'

def adb(*args):
    return subprocess.check_output(['adb', '-s', a.device, *args], timeout=30)

def log():
    result=subprocess.run(['adb','-s',a.device,'exec-out','run-as',package,'cat','files/native-run.log'],capture_output=True,timeout=30)
    if result.returncode and b'No such file' in result.stderr+result.stdout:return ''
    result.check_returncode()
    return result.stdout.decode()

def tree():
    adb('shell', 'uiautomator', 'dump', '/data/local/tmp/open-rune-retry-test.xml')
    return ET.fromstring(adb('exec-out', 'cat', '/data/local/tmp/open-rune-retry-test.xml'))

def screen(label):
    # Death state precedes the delayed Android recovery UI; accessibility can
    # briefly report the preceding control tree during the transition.
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        nodes=list(tree().iter('node'))
        if any(n.get('text')==label for n in nodes):return nodes
        time.sleep(.3)
    raise AssertionError('Screen did not appear: '+label)

def tap(label):
    nodes = [n for n in screen(label) if n.get('text') == label]
    assert nodes and nodes[-1].get('enabled') == 'true', label
    x, y, r, b = map(int, re.findall(r'\d+', nodes[-1].get('bounds')))
    adb('shell', 'input', 'tap', str((x+r)//2), str((y+b)//2))

def wait_count(marker, count, seconds=60):
    deadline = time.monotonic()+seconds
    while time.monotonic() < deadline:
        text = log()
        assert 'LAUNCH_FAILED' not in text, text[-2000:]
        if text.count(marker) >= count:
            return text
        time.sleep(.4)
    raise AssertionError('Timed out: '+marker)

before = adb('exec-out', 'run-as', package, 'cat', 'shared_prefs/MainActivity.xml')
adb('shell', 'am', 'force-stop', package)
adb('shell', 'run-as', package, 'rm', '-f', 'files/native-run.log')
adb('shell', 'run-as', package, 'mkdir', '-p', fixture)
# Only this disposable fixture slot is cleared; campaign files are untouched.
adb('shell', 'run-as', package, 'rm', '-f', fixture+'/Save10.rsa')
adb('shell', 'am', 'start', '-n', package+'/.MainActivity',
    '--es', 'map', 'DM-Hildir.run', '--es', 'saveTest', 'practice-retry',
    '--es', 'enemy', 'DarkViking', '--ez', 'enemyClearLane', 'true',
    '--ei', 'seed', '73', '--ei', 'enemyFrame', '1200',
    '--ez', 'cameraProbe', 'true', '--ez', 'traceCombat', 'true',
    '--ez', 'upscale', 'false', '--ei', 'msaa', '2')

for attempt in (1, 2):
    wait_count('RUNE_PLAYER_DEAD value=1', attempt)
    nodes = screen('You have fallen')
    assert any(n.get('text') == 'You have fallen' for n in nodes)
    assert not any(n.get('text') == 'Retry checkpoint' for n in nodes)
    (a.output/f'death-{attempt}.png').write_bytes(adb('exec-out', 'screencap', '-p'))
    tap('Restart fight')
    wait_count('RUNE_RETRY_FIGHT arena=DM-Hildir.run enemy=DarkViking', attempt)
    wait_count('RUNE_PLAYER_DEAD value=0', attempt)
    # Exercise the real overlay after map replacement, before the delayed enemy.
    time.sleep(1)
    tap('Attack')
    deadline=time.monotonic()+4
    while time.monotonic()<deadline:
        after=log().rsplit('RUNE_RETRY_FIGHT',1)[1]
        if 'proxy=Attacking' in after or 'RUNE_SWEEP_HIT' in after:break
        time.sleep(.15)
    else:raise AssertionError('Quick touch reached Fire binding but did not start a weapon swing')
    controls={n.get('text'):n for n in tree().iter('node') if n.get('text')}
    for label,dx,dy in [('Move',0,-.3),('Camera',.3,0)]:
        x,y,r,b=map(int,re.findall(r'\d+',controls[label].get('bounds')))
        cx,cy=(x+r)//2,(y+b)//2
        adb('shell','input','swipe',str(cx),str(cy),str(int(cx+dx*(r-x))),str(int(cy+dy*(b-y))),'1200')
    tap('Jump')
    text=log()
    after=text.rsplit('RUNE_RETRY_FIGHT',1)[1]
    assert 'RUNE_INPUT key=Enter binding=Fire' in after
    assert 'RUNE_INPUT key=Space binding=Jump' in after
    assert 'ANDROID_RENDER_TARGET size=2520x1680 scale=1.000000 samples=4' in text
    if attempt == 1:
        adb('shell', 'run-as', package, 'cp', 'files/game/Save/Save10.rsa', fixture+'/Save10.rsa')
    print(f'PASS: practice retry {attempt}, campaign checkpoint present={attempt==2}', flush=True)

adb('shell', 'input', 'keyevent', '111')
text = log()
(a.output/'retry.log').write_text(text)
assert 'RUNE_SESSION_ACTION action=4' not in text
assert before == adb('exec-out', 'run-as', package, 'cat', 'shared_prefs/MainActivity.xml')
(a.output/'result.json').write_text(json.dumps({'passed': True, 'touch_restarts': 2,
    'same_map_and_enemy': True, 'campaign_checkpoint_not_loaded': True,
    'preferences_unchanged': True, 'touch_inputs_after_restart': True, 'quick_attack_taps_started_swings': True,
    'native_resolution': True, 'msaa_samples': 4}, indent=2)+'\n')
