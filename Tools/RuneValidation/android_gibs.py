"""Measure original gib/limb physics at controlled 30/60/120 simulation steps.

Runs the opt-in synchronous fixture inside the real Android game/world. It is
not a display FPS benchmark. Other actors stay frozen during each eight-second
simulation; actor contacts are excluded. Normal saves/preferences are untouched.
"""
import argparse, json, pathlib, re, subprocess, time, fcntl
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
adb('shell','am','force-stop',pkg)
adb('shell','run-as',pkg,'rm','-f','files/native-run.log')
adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run','--es','saveTest','gib-physics','--ez','gibProbe','true','--ei','frames','300','--ei','msaa','2','--ez','upscale','false','--ez','enhanced','false')
log='';deadline=time.monotonic()+65
try:
 while time.monotonic()<deadline:
  time.sleep(.4)
  try:log=adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
  except subprocess.CalledProcessError:continue
  if re.search(r'BOUNDED_RUN_COMPLETE|LAUNCH_FAILED|Script error:',log):break
finally:(a.output/'native-run.log').write_text(log)
assert 'RUNE_GIB_COMPLETE cases=24' in log and 'BOUNDED_RUN_COMPLETE' in log,log[-4000:]
assert not re.search(r'LAUNCH_FAILED|Script error:|Unknown native',log)
rows=[dict(re.findall(r'(\w+)=(\S+)',s)) for s in log.splitlines() if s.startswith('RUNE_GIB_RESULT ')]
assert len(rows)==24
(a.output/'results.json').write_text(json.dumps({'controlled_timestep':True,'physics_changed':False,'cases':rows},indent=2)+'\n')
for r in rows:print(json.dumps(r),flush=True)
