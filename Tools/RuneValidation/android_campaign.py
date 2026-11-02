"""Bounded camera/combat/save checks using the installed Android prototype.

Save tests use a separate directory. Limb/weapon/save fixtures assert their own
results in the engine; they are diagnostic setups, not a full campaign playthrough.
Run cases sequentially; do not install or run another device test concurrently.
"""
import argparse
import fcntl
import json
from pathlib import Path
import re
import subprocess
import time

CASES = {
    'rope-deepunder3': (600, ['--es','map','DeepUnder3.run','--es','saveTest','campaign-rope-deepunder3','--ez','ropeProbe','true'], 'RUNE_ROPE_PROBE_OK'),
    'limb-head': (360, ['--es','enemy','GoblinWarrior','--ei','limbJoint','20'], 'RUNE_LIMB_OK'),
    'limb-left': (360, ['--es','enemy','GoblinWarrior','--ei','limbJoint','9'], 'RUNE_LIMB_OK'),
    'limb-right': (360, ['--es','enemy','GoblinWarrior','--ei','limbJoint','15'], 'RUNE_LIMB_OK'),
    'throw': (1080, ['--es','weaponProbe','throw'], 'RUNE_WEAPON_OK'),
    'drop': (1080, ['--es','weaponProbe','drop'], 'RUNE_WEAPON_OK'),
    'save-flow': (1500, ['--es','map','SinkingShip2.run','--es','saveTest','campaign-validation','--ez','saveFlow','true'], 'RUNE_SAVE_FLOW_OK'),
    'save-resume': (600, ['--es','saveTest','campaign-validation','--ei','resumeSlot','90'], 'RUNE_SAVE_RESUME_OK'),
    'dark-viking': (900, ['--es','enemy','DarkViking','--ei','forward','180','--ei','fire','150','--ei','fireHold','8','--ei','fireRepeat','60'], 'enemy_state=Pain'),
    'endurance': (2400, ['--es','enemy','GoblinWarrior','--ei','fire','60','--ei','fireHold','8','--ei','fireRepeat','60','--ez','endurance','true'], 'RUNE_ENDURANCE_ACTIVE'),
}
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--cases',nargs='+',choices=CASES,default=list(CASES))
p.add_argument('--capture',action='store_true',help='Save final engine PPM captures (large)')
a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w')
fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
package='org.runeclassic.port'
def adb(*args):
    return subprocess.run(['adb','-s',a.device,*args],check=True,capture_output=True,timeout=25).stdout

for case in a.cases:
    frames,extras,marker=CASES[case]
    adb('shell','am','force-stop',package)
    adb('shell','run-as',package,'rm','-f','files/native-run.log','files/frame.ppm')
    adb('shell','am','start','-n',package+'/.MainActivity','--es','map','DM-Hildir.run',
        '--es','saveTest','campaign-'+case,'--ei','msaa','2',
        '--ei','seed','1','--ei','frames',str(frames),'--ez','enhanced','false','--ez','upscale','false',*extras)
    log=''
    deadline=time.monotonic()+100
    try:
        while time.monotonic()<deadline:
            time.sleep(.5)
            try: log=adb('shell','run-as',package,'cat','files/native-run.log').decode()
            except subprocess.CalledProcessError: continue
            if 'BOUNDED_RUN_COMPLETE' in log or re.search(r'LAUNCH_FAILED|Script error:',log): break
    finally:
        (a.output/(case+'.log')).write_text(log)
    assert 'BOUNDED_RUN_COMPLETE' in log and marker in log, f'{case} failed; see log'
    assert not re.search(r'Unknown native|Script error:|LAUNCH_FAILED',log), f'{case} runtime error'
    if case=='endurance':
        damage=[int(x) for x in re.findall(r'RUNE_ENDURANCE_ACTIVE.*enemy_damage=(\d+)',log)]
        assert damage and max(damage)>0, 'Endurance must include active hits'
    if a.capture:
        (a.output/(case+'.ppm')).write_bytes(adb('exec-out','run-as',package,'cat','files/frame.ppm'))
    result={'case':case,'passed':True,'frames':frames,'assertion':marker}
    (a.output/(case+'.json')).write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result),flush=True)
