"""OPPO rumble UI checks: shared settings, off/zero, unsupported device, lifecycle.

Preserves the user's initial rumble values and unrelated preferences. This device
fixture expects a supported controller and no built-in vibrator. It uses no saves.
"""
import argparse,json,re,subprocess,time
from pathlib import Path
import xml.etree.ElementTree as ET
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
def prefs():return ET.fromstring(adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml'))
before={n.get('name'):(n.tag,n.get('value'),n.text) for n in prefs()}
# This validation is run after the user's 100% hardware check; don't alter a
# different user's preference with an approximate slider gesture.
assert before.get('menu.rumbleStrength',('',None,''))[1]=='100','Fixture expects the current 100% choice'
def tree():
    adb('shell','uiautomator','dump','/sdcard/rumble-check.xml')
    return ET.fromstring(adb('exec-out','cat','/sdcard/rumble-check.xml'))
def node(text,desc=False):
    found=[n for n in tree().iter('node') if n.get('content-desc' if desc else 'text')==text]
    assert found,text
    return found[-1]
def tap(text,desc=False):
    x,y,r,b=map(int,re.findall(r'\d+',node(text,desc).get('bounds')))
    adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def choose(value):
    n=next(n for n in tree().iter('node') if n.get('content-desc','').startswith('Vibration output:'))
    tap(n.get('content-desc'),True)
    options=[n for n in tree().iter('node') if n.get('text','').replace('   ✓','')==value]
    assert options,value
    x,y,r,b=map(int,re.findall(r'\d+',options[-1].get('bounds')))
    adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def output():return adb('logcat','-d','--pid='+pid,'-s','RuneRumble:I','*:S').decode()
def plays():return output().count('play event=')
def slider(maximum):
    x,y,r,b=map(int,re.findall(r'\d+',node('Rumble intensity',True).get('bounds')))
    adb('shell','input','tap',str(r-1 if maximum else x+1),str((y+b)//2))

adb('shell','am','force-stop',pkg)
adb('shell','am','start','-n',pkg+'/.MainActivity','--es','saveTest','rumble-ui','--ei','msaa','2','--ez','upscale','false','--ez','rumbleProbe','true')
time.sleep(5);pid=adb('shell','pidof',pkg).decode().strip()
tap('Controls');tap('Rumble & vibration')
tap('Rumble: On',True);count=plays();tap('Test rumble');assert plays()==count
tap('Rumble: Off',True);tap('Test rumble');assert plays()>count
slider(False);node('0%');count=plays();tap('Test rumble');assert plays()==count
slider(True);node('100%')
choose('Device');node('No supported vibration output');count=plays();tap('Test rumble');assert plays()==count
choose('Controller');assert any('variable strength' in n.get('text','') for n in tree().iter('node'))
adb('shell','input','gamepad','keyevent','KEYCODE_BUTTON_B');tap('Enhancements');tap('Rumble & vibration')
node('Vibration output: Controller',True);node('100%')
choose('Automatic')
tap('Restore defaults');tap('Restore');node('Rumble & vibration');node('65%')
slider(True);node('100%')
# Preview must cancel on background rather than waiting its full 400ms.
n=node('Test rumble');x,y,r,b=map(int,re.findall(r'\d+',n.get('bounds')))
adb('shell',f'input tap {(x+r)//2} {(y+b)//2}; input keyevent KEYCODE_HOME')
state=adb('shell','dumpsys','input').decode();assert 'Vibrating: true' not in state
adb('shell','am','start','-n',pkg+'/.MainActivity');time.sleep(.5)
(a.output/'menu.png').write_bytes(adb('exec-out','screencap','-p'))
(a.output/'android-output.log').write_text(output())
after={n.get('name'):(n.tag,n.get('value'),n.text) for n in prefs()}
assert all(after.get(k)==v for k,v in before.items()),'A pre-existing preference changed'
assert set(after)-set(before)<= {'menu.rumble','menu.rumbleOutput','menu.rumbleStrength'}
(a.output/'result.json').write_text(json.dumps({'off_and_zero_silent':True,'unsupported_device_silent':True,'aliases_share_settings':True,'restore_defaults_page_correct':True,'background_stopped':True,'existing_preferences_preserved':True,'intensity':100},indent=2)+'\n')
print('PASS: shared rumble controls, off/zero, unsupported output, restore defaults, background stop and preferences',flush=True)
