"""Exercise the actual Open Rune Android menu, shared settings and isolated saves.

Requires the unlocked tablet and the new menu APK. Saves go only to
validation-saves/menu-ui. Preferences are backed up and restored in finally.
"""
import argparse
import base64
import fcntl
import json
from pathlib import Path
import re
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--validation',action='store_true')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
def log():return adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
def tree():
    adb('shell','uiautomator','dump','--compressed','/data/local/tmp/rune-menu-test.xml')
    xml=adb('shell','cat','/data/local/tmp/rune-menu-test.xml')
    return ET.fromstring(xml)
def find(text,description=False):
    field='content-desc' if description else 'text'
    found=[n for n in tree().iter('node') if n.get(field)==text and n.get('enabled')=='true']
    assert found,'Missing visible control: '+text
    return found[-1]
def bounds(node):return list(map(int,re.findall(r'\d+',node.get('bounds'))))
def tap(text,description=False):
    x,y,r,b=bounds(find(text,description));adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def choose(name,value):
    tap(name)
    options=[n for n in tree().iter('node') if n.get('text','').replace('   ✓','')==value and n.get('class')=='android.widget.Button']
    assert options,'Missing option '+value
    x,y,r,b=bounds(options[0]);adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def shot(name):
    (a.output/(name+'.png')).write_bytes(adb('exec-out','screencap','-p'))
    (a.output/(name+'.xml')).write_bytes(ET.tostring(tree()))
def expect_value(name,value):find(name+': '+value,True)
def wait_for(pattern,seconds=15):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        text=log()
        assert 'LAUNCH_FAILED' not in text,text[-2000:]
        if re.search(pattern,text):return text
        time.sleep(.25)
    raise AssertionError('Timed out: '+pattern)
def launch():
    adb('shell','am','force-stop',pkg)
    adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','SinkingShip2.run',
        '--es','saveTest','menu-ui','--ez','upscale','false','--ei','msaa','2','--ez','enhanced','true',
        '--ez','validation',str(a.validation).lower())
    wait_for('RUNE_SCENE_TARGET.*samples=4|RUNE_VULKAN_SCENE.*samples=4|RUNE_MENU_AUDIO',30)
    tap('Menu')

def report(phase):print('PASS: '+phase,flush=True)
backup=adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
(a.output/'preferences-before.xml').write_bytes(backup)
results={}
try:
    launch();shot('pause')
    tap('Video')
    tap('Resolution & scaling');expect_value('Rendering resolution','Native (100%)');adb('shell','input','keyevent','KEYCODE_BACK');expect_value('Anti-aliasing','4× MSAA')
    for name in ('Adaptive sharpening','Shadow / midtone lift','Surface detail','Surface depth'):
        choose(name,'High');expect_value(name,'High')
    choose('Texture filtering','16×')
    tap('Enhancements')
    for name in ('Adaptive sharpening','Shadow / midtone lift','Surface detail','Surface depth'):expect_value(name,'High')
    wait_for('RUNE_CLARITY code=31');wait_for('RUNE_WORLD_FEATURES code=24[67]|RUNE_WORLD_FEATURES code=25[45]')
    shot('enhancements-high');report('Video and Enhancements share real High settings')
    before=adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
    tap('Compare original effects');wait_for('RUNE_CLARITY code=0');wait_for('RUNE_WORLD_FEATURES code=0')
    find('Resolution & scaling');find('Frame rate & refresh')
    after=adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
    assert before==after,'Comparison changed saved preferences'
    tap('Restore my effects');time.sleep(.3)
    text=log();assert re.findall(r'RUNE_CLARITY code=(\d+)',text)[-1]=='31'
    assert re.findall(r'RUNE_WORLD_FEATURES code=(\d+)',text)[-1] in ('247','255','246','254')
    report('Original comparison restores effects without changing performance or preferences')
    tap('Audio');choose('Headphone spatial audio','On')
    tap('Enhancements');expect_value('Headphone spatial audio','On')
    tap('Audio');choose('Headphone spatial audio','Off')
    n=find('Master volume',True);x,y,r,b=bounds(n);adb('shell','input','swipe',str((x+r)//2),str((y+b)//2),str(x+20),str((y+b)//2),'300')
    time.sleep(.3);text=log();master=int(re.findall(r'RUNE_MENU_AUDIO master=(\d+)',text)[-1]);assert master<15,master
    shot('audio');report('Audio aliases and master gain reach the audio device')
    tap('Video');tap('Frame rate & refresh');choose('Frame rate target','60 FPS');wait_for('RUNE_MENU pacing=1 target_fps=60')
    choose('Frame pacing','Off');wait_for('RUNE_MENU pacing=0 target_fps=60')
    choose('Frame pacing','On');choose('Frame rate target','120 FPS');wait_for('RUNE_MENU pacing=1 target_fps=120')
    adb('shell','input','keyevent','KEYCODE_BACK')
    choose('Anti-aliasing','2× MSAA');wait_for('RUNE_MENU msaa=1')
    choose('Anti-aliasing','4× MSAA');wait_for('RUNE_MENU msaa=2')
    shot('video');report('Live frame target, pacing and MSAA switches')
    tap('Controls');shot('controls');choose('Touch controls','Off');tap('×  Close')
    root=tree();assert not any(n.get('text')=='Attack' for n in root.iter('node'))
    tap('Menu');tap('Controls');choose('Touch controls','On');tap('Done')
    tap('Resume');tap('Menu')
    text=log();pauses=re.findall(r'RUNE_MENU paused=([01]) level_time=([\d.]+) x=([-\d.]+) y=([-\d.]+) z=([-\d.]+)',text)
    pairs=[(x,y) for x,y in zip(pauses,pauses[1:]) if x[0]=='1' and y[0]=='0']
    assert pairs and all(x[1:]==y[1:] for x,y in pairs),'World advanced while menu was open: '+str(pairs)
    report('Paused world time and location; hidden touch controls retain Menu')
    tap('Save game');shot('save-slots');tap('Save to Manual save 3',True)
    # A previous run may already occupy this isolated slot.
    time.sleep(.25)
    if any(n.get('text')=='Replace this save?' for n in tree().iter('node')):tap('Save')
    wait_for('RUNE_SESSION_SAVE_OK slot=2')
    tap('Menu');tap('Load game');tap('Load Manual save 3',True);tap('Load')
    wait_for('RUNE_SESSION_ACTION action=7 map=')
    report('Manual save and load through actual touch controls')
    tap('Menu');tap('Save game');tap('Save to Quick save',True)
    time.sleep(.25)
    if any(n.get('text')=='Replace this save?' for n in tree().iter('node')):tap('Save')
    wait_for('RUNE_SESSION_SAVE_OK slot=9');report('Quick save through actual touch controls')
    text=log();(a.output/'native-run.log').write_text(text)
    assert not re.search(r'LAUNCH_FAILED|Unknown native|\[vulkan (?:error|warning)\]',text)
    assert 'size=2520x1680 scale=1.000000 samples=4' in text
    if a.validation:assert 'RUNE_VULKAN_VALIDATION active=1 synchronization=1' in text
    launch();tap('Video')
    for name in ('Adaptive sharpening','Shadow / midtone lift','Surface detail','Surface depth'):expect_value(name,'High')
    report('Settings survive a full process restart')
    results={'passed':True,'native_4x':True,'high_settings':True,'aliases':True,'original_comparison':True,'world_pause':True,'volume_gain':True,'live_msaa_and_pacing':True,'manual_save_load':True,'quick_save':True,'persistence':True,'vulkan_validation':a.validation}
finally:
    try:(a.output/'last-run.log').write_text(log())
    except Exception:pass
    adb('shell','am','force-stop',pkg)
    encoded=base64.b64encode(backup).decode()
    command='printf %s '+shlex.quote(encoded)+' | base64 -d > shared_prefs/MainActivity.xml'
    adb('shell','run-as',pkg,'sh','-c',shlex.quote(command))
    restored=adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
    assert restored==backup,'Failed to restore test preferences'
    (a.output/'result.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
