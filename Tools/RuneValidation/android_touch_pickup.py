"""Exercise the actual Android Throw/Use buttons against weapon and food fixtures.

The engine only positions the player by the landed axe and spawns nearby food.
This driver injects Android screen taps, never engine Use or pickup commands.
Normal saves and INI bindings are unchanged; all slots are isolated.
"""
import argparse
import fcntl
import json
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--surface-percent',type=int,choices=(100,75,50),default=100,help='Android system surface scaling for real touch regression')
p.add_argument('--controller-use',action='store_true',help='Inject Android L3 for Use; Throw still uses the screen button')
p.add_argument('--rumble-probe',action='store_true',help='Trace game feedback for integration checks')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w')
fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
package='org.runeclassic.port'
def adb(*args):
    return subprocess.run(['adb','-s',a.device,*args],check=True,capture_output=True,timeout=25).stdout
adb('shell','am','force-stop',package)
adb('shell','run-as',package,'rm','-f','files/native-run.log')
adb('shell','am','start','-n',package+'/.MainActivity','--es','map','DM-Hildir.run',
    '--es','saveTest','touch-pickup','--ei','seed','1','--ei','frames','3700',
    '--ei','surfacePercent',str(a.surface_percent),'--ei','msaa','2','--ez','upscale','false','--ez','enhanced','false','--ez','traceCombat','true',
    '--ez','rumbleProbe',str(a.rumble_probe).lower(),'--es','weaponProbe','throw','--ez','weaponTouch','true')
# Locate real buttons from the Android accessibility tree, not fixed coordinates.
adb('shell','uiautomator','dump','/data/local/tmp/rune-touch-pickup.xml')
xml=adb('shell','cat','/data/local/tmp/rune-touch-pickup.xml')
(a.output/'controls.xml').write_bytes(xml)
buttons={}
for n in ET.fromstring(xml).iter('node'):
    if n.get('text') in ('Use','Throw'):
        x1,y1,x2,y2=map(int,re.findall(r'\d+',n.get('bounds')))
        buttons[n.get('text')]=((x1+x2)//2,(y1+y2)//2)
assert len(buttons)==2,'Unlock tablet, disconnect gamepads, enable Touch controls and leave Rune visible'
phases=[('RUNE_WEAPON_BEGIN','Throw'),('RUNE_WEAPON_TOUCH_READY','Use'),('RUNE_FOOD_TOUCH_READY','Use')]
phase=0;log='';deadline=time.monotonic()+70
try:
    while time.monotonic()<deadline:
        time.sleep(.15)
        log=adb('shell','run-as',package,'cat','files/native-run.log').decode()
        if re.search(r'LAUNCH_FAILED|Script error:|Unknown native',log):break
        if phase<len(phases) and phases[phase][0] in log:
            label=phases[phase][1]
            if a.controller_use and label=='Use':
                adb('shell','input','gamepad','keyevent','KEYCODE_BUTTON_THUMBL')
                print('Injected Android L3 for '+phases[phase][0],flush=True)
            else:
                adb('shell','input','tap',*map(str,buttons[label]))
                print('Tapped '+label+' for '+phases[phase][0],flush=True)
            phase+=1
        if 'BOUNDED_RUN_COMPLETE' in log:break
finally:
    (a.output/'touch-pickup.log').write_text(log)
assert phase==3 and 'BOUNDED_RUN_COMPLETE' in log
assert 'RUNE_WEAPON_OK' in log and 'touch=1' in log and 'RUNE_FOOD_TOUCH_OK' in log
assert log.count('RUNE_INPUT key=LControl binding=Use')==2
assert 'RUNE_INPUT key=Q binding=Throw' in log
(a.output/'frame.ppm').write_bytes(adb('exec-out','run-as',package,'cat','files/frame.ppm'))
result={'passed':True,'android_throw_taps':1,'android_use_taps':0 if a.controller_use else 2,'android_l3_injections':2 if a.controller_use else 0,'weapon_recovered':True,
        'food_consumed':True,'native_resolution':a.surface_percent==100,'surface_percent':a.surface_percent,'positioning':'fixture positions player beside landed weapon; no engine Use injection'}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
