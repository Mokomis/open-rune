"""Replay touch movement from RagnarVillage2's original start to its water edge.

No actor relocation, exit triggers, or direct engine input is used. The route is
specific to the original map/start; all saves use a separate test directory.
"""
import argparse, fcntl, json, re, subprocess, time
from pathlib import Path
import xml.etree.ElementTree as ET
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--expect-blocked',action='store_true')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
package='org.runeclassic.port'
def adb(*args):
    return subprocess.run(['adb','-s',a.device,*args],check=True,capture_output=True,timeout=25).stdout

def read():return adb('shell','run-as',package,'cat','files/native-run.log').decode()
adb('shell','am','force-stop',package)
adb('shell','run-as',package,'rm','-f','files/native-run.log')
adb('shell','am','start','-n',package+'/.MainActivity','--es','map','RagnarVillage2.run',
    '--es','saveTest','campaign-village-ledge','--ei','seed','1','--ei','msaa','2',
    '--ez','upscale','false','--ez','cameraProbe','true')
deadline=time.monotonic()+40
while time.monotonic()<deadline:
    time.sleep(.25)
    try: log=read()
    except subprocess.CalledProcessError:continue
    if re.search(r'RUNE_CAMERA .*state=PlayerWalking',log):break
else:raise RuntimeError('No controllable village start; unlock tablet and inspect log')
adb('shell','uiautomator','dump','/data/local/tmp/rune-village-controls.xml')
xml=adb('shell','cat','/data/local/tmp/rune-village-controls.xml');(a.output/'controls.xml').write_bytes(xml)
buttons={}
for node in ET.fromstring(xml).iter('node'):
    if node.get('text') in ('↑','→'):
        x1,y1,x2,y2=map(int,re.findall(r'\d+',node.get('bounds')))
        buttons[node.get('text')]=((x1+x2)//2,(y1+y2)//2)
assert len(buttons)==2,'Rune movement controls not visible'
actions=[]
try:
    for label,duration in [('→',1400),('↑',6000),('→',600),('↑',5000),('→',600),('↑',5000)]:
        x,y=map(str,buttons[label])
        adb('shell','input','touchscreen','swipe',x,y,x,y,str(duration))
        log=read()
        states=re.findall(r'RUNE_CAMERA .*',log)
        actions.append({'button':label,'hold_ms':duration,'last_state':states[-1] if states else None})
        print(json.dumps(actions[-1]),flush=True)
        if 'RUNE_LEDGE_GRAB' in log:break
    time.sleep(3)
    log=read()
    (a.output/'screen.png').write_bytes(adb('exec-out','screencap','-p'))
finally:
    (a.output/'engine.log').write_text(read())
    (a.output/'actions.json').write_text(json.dumps(actions,indent=2)+'\n')
errors=re.findall(r'^.*(?:LAUNCH_FAILED|Script error:|Unknown native).*$',log,re.M)
rows=re.findall(r'RUNE_CAMERA .*',log)
grabbed='RUNE_LEDGE_GRAB' in log or 'state=EdgeHanging' in log
after_grab=log.split('RUNE_LEDGE_GRAB',1)[-1] if 'RUNE_LEDGE_GRAB' in log else ''
climb_animated=bool(re.search(r'RUNE_CAMERA .*state=EdgeHanging physics=4',after_grab))
dry_exit=False
for row in re.findall(r'RUNE_CAMERA .*state=PlayerWalking physics=1 .*',after_grab):
    x,y,z=map(float,re.search(r' x=([^ ]+) y=([^ ]+) z=([^ ]+)',row).groups())
    if y>0 and z>-290 and ' water=0 head_water=0 ' in row:
        dry_exit=True
result={'grabbed':grabbed,'expect_blocked':a.expect_blocked,'errors':errors,
        'climb_animated':climb_animated,'returned_to_dry_walking':dry_exit,
        'last_state':rows[-1] if rows else None,
        'native_4x_verified':'size=2520x1680 scale=1.000000 samples=4' in log,
        'input':'actual Android arrow holds from original start; no fixture teleport',
        'coverage':'village water-edge traversal only, not full village completion'}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2),flush=True)
assert not errors and result['native_4x_verified']
if a.expect_blocked:
    last=rows[-1]
    x,y,z=map(float,re.search(r' x=([^ ]+) y=([^ ]+) z=([^ ]+)',last).groups())
    assert -1200<x<-980 and -400<y<-200 and -380<z<-300, 'Baseline did not reach the water edge'
assert grabbed != a.expect_blocked,'Unexpected ledge result; inspect retained position and image'
if not a.expect_blocked:
    assert climb_animated and dry_exit, 'Grab did not complete the original climb and return to dry walking'
