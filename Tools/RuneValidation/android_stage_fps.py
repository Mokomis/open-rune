"""Measure actual presentation during selected Rune stages, without policy changes.

Combat is a synthetic sustained encounter: health/limb health is replenished so
the sample cannot silently become an idle corpse scene. Transitions invoke real
exit scripts using an isolated save fixture. Other stages run original map scripts.
"""
import argparse
import fcntl
import json
from pathlib import Path
import re
import subprocess
import sys
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--stage',choices=['combat','village','underwater','transitions'],required=True)
p.add_argument('--upscale',action='store_true')
p.add_argument('--samples',type=int,default=60)
a=p.parse_args()
a.output.parent.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w'); fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
package='org.runeclassic.port'
def adb(*args): return subprocess.run(['adb','-s',a.device,*args],check=True,capture_output=True,timeout=25).stdout
maps={'combat':'DM-Hildir.run','village':'RagnarVillage.run','underwater':'SinkingShip2.run','transitions':'SinkingShip2.run'}
extras=[]
if a.stage=='combat': extras=['--es','enemy','GoblinWarrior','--ei','fire','60','--ei','fireHold','8','--ei','fireRepeat','60','--ez','endurance','true']
if a.stage=='transitions': extras=['--ez','saveFlow','true']
adb('shell','am','force-stop',package)
adb('shell','run-as',package,'rm','-f','files/native-run.log')
adb('shell','am','start','-n',package+'/.MainActivity','--es','map',maps[a.stage],
    '--es','saveTest','fps-'+a.stage,'--ei','seed','1','--ez','cameraProbe','true',
    '--ez','enhanced','false','--ez','upscale',str(a.upscale).lower(),'--ei','profile','1200',*extras)
deadline=time.monotonic()+60
log=''
while time.monotonic()<deadline:
    time.sleep(.25)
    try: log=adb('shell','run-as',package,'cat','files/native-run.log').decode()
    except subprocess.CalledProcessError: continue
    if re.search(r'LAUNCH_FAILED|Script error:',log): raise RuntimeError(log[-3000:])
    if a.stage=='transitions':
        if 'ANDROID_PACING active=1' in log: break
    elif re.search(r'RUNE_CAMERA frame=(?:[6-9]\d\d|\d{4,}) ',log): break
else: raise RuntimeError('No rendered warmup; tablet must be unlocked')
subprocess.run([sys.executable,str(Path(__file__).with_name('android_present.py')),
    '--device',a.device,'--samples',str(a.samples),'--output',str(a.output)],check=True)
log=adb('shell','run-as',package,'cat','files/native-run.log').decode()
Path(str(a.output)+'-engine.log').write_text(log)
assert not re.search(r'LAUNCH_FAILED|Script error:|Unknown native',log), 'Runtime error'
if a.stage=='combat':
    damage=[int(x) for x in re.findall(r'RUNE_ENDURANCE_ACTIVE.*enemy_damage=(\d+)',log)]
    assert damage and max(damage)>0, 'No active combat during measurement'
if a.stage=='transitions': assert 'RUNE_SAVE_FLOW_OK' in log, 'Transitions did not finish'
Path(str(a.output)+'.png').write_bytes(adb('exec-out','screencap','-p'))
result=json.loads(Path(str(a.output)+'.json').read_text())
result.update(stage=a.stage,upscale=a.upscale,enhanced=False,synthetic_combat=a.stage=='combat')
Path(str(a.output)+'.json').write_text(json.dumps(result,indent=2)+'\n')
print('STAGE_OK',a.stage,'upscale='+str(a.upscale),flush=True)
# Leave the game visible; the next test owns any stop/relaunch.
