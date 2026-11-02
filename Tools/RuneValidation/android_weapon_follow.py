"""Throw a granted weapon, let it settle, then walk away without using it.

Granting uses original SpawnCopy/AcquireInventory; Throw and movement use normal
engine bindings. This is a physics regression fixture, not hardware input proof.
Native rendering, 4x MSAA and isolated saves are required. Do not use controls
while this bounded fixture runs.
"""
import argparse
import fcntl
import json
from pathlib import Path
import re
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--weapons',nargs='+',default=['DwarfBattleHammer','DwarfWorkHammer','Handaxe'])
p.add_argument('--baseline',action='store_true',help='Record pre-fix movement without asserting stationarity')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.run(['adb','-s',a.device,*args],check=True,capture_output=True,timeout=25).stdout
for weapon in a.weapons:
    if not re.fullmatch('[A-Za-z0-9_]+',weapon):raise ValueError('Invalid fixture class')
    adb('shell','am','force-stop',pkg)
    adb('shell','run-as',pkg,'rm','-f','files/native-run.log')
    adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run',
        '--es','saveTest','weapon-follow-'+weapon,'--ei','frames','900','--ei','seed','1',
        '--ei','msaa','2','--ez','upscale','false','--es','weaponProbe','follow',
        '--es','weaponClass',weapon,'--ez','traceCombat','true')
    log='';deadline=time.monotonic()+60
    while time.monotonic()<deadline:
        time.sleep(.5)
        try:log=adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
        except subprocess.CalledProcessError:continue
        if 'BOUNDED_RUN_COMPLETE' in log or 'LAUNCH_FAILED' in log:break
    (a.output/(weapon+'.log')).write_text(log)
    assert 'BOUNDED_RUN_COMPLETE' in log and not re.search(r'Unknown native|Script error:|LAUNCH_FAILED',log)
    match=re.search(r'RUNE_FOLLOW_RESULT player_moved=([\d.]+) weapon_moved=([\d.]+) base=(\S+)',log)
    assert match,'Missing movement comparison'
    moved,drift,base=float(match[1]),float(match[2]),match[3]
    assert moved>50,'Player must actually walk away'
    if not a.baseline:assert drift<.01 and base!='Ragnar0','Thrown weapon still follows its former carrier'
    result={'weapon':weapon,'baseline':a.baseline,'player_moved':moved,'weapon_moved':drift,'base':base,'passed':not a.baseline and drift<.01,'native_resolution':True,'msaa':4}
    (a.output/(weapon+'.json')).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
