"""Native 4x original/per-pixel/original comparisons in isolated living encounters.

Health is maintained by the existing endurance fixture. Animation/AI timing is
not frozen, so these are bounded display samples, not an exact GPU cost estimate.
"""
import fcntl
import argparse,json,pathlib,re,subprocess,sys,time
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True);p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
results=[]
for enemy in ['GoblinWarrior','DarkViking']:
 for index,mode in enumerate([0,256,0]):
  dest=a.output/(enemy+'-'+str(index)+'-'+str(mode));dest.mkdir(exist_ok=True)
  adb('shell','am','force-stop',pkg)
  adb('shell','run-as',pkg,'rm','-f','files/native-run.log')
  adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run',
   '--es','saveTest','pixel-lighting','--ei','seed','1','--ei','msaa','2',
   '--ez','upscale','false','--ez','enhanced','false','--ei','clarity','0',
   '--ei','worldFeatures',str(mode),'--ez','traceCombat','true','--ez','cameraProbe','true',
   '--ez','enemyClearLane','true','--es','enemy',enemy,'--ei','enemyFrame','240','--ez','endurance','true')
  deadline=time.monotonic()+40;log=''
  while time.monotonic()<deadline:
   time.sleep(.5)
   log=adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
   assert not re.search(r'LAUNCH_FAILED|Script error:|Unknown native',log),log[-1000:]
   if 'RUNE_ENEMY_SPAWN' in log:break
  assert 'RUNE_ENEMY_SPAWN' in log
  time.sleep(1)
  (dest/'before.png').write_bytes(adb('exec-out','screencap','-p'))
  subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('android_present.py')),
   '--device',a.device,'--samples','24','--output',str(dest/'display')],check=True,stdout=subprocess.DEVNULL)
  (dest/'after.png').write_bytes(adb('exec-out','screencap','-p'))
  log=adb('shell','run-as',pkg,'cat','files/native-run.log').decode();(dest/'engine.log').write_text(log)
  assert 'size=2520x1680 scale=1.000000 samples=4' in log
  assert 'RUNE_WORLD_FEATURES code='+str(mode) in log
  assert not re.search(r'LAUNCH_FAILED|Script error:|Unknown native|\[vulkan (?:error|warning)\]',log)
  meshes=re.findall(r'RUNE_PIXEL_MESH class=(\S+) lights=(\d+)',log)
  if mode:
   assert any(c==enemy for c,n in meshes),meshes
   assert any('Ragnar' in c for c,n in meshes),meshes
   assert {'Ragnar','handaxe',enemy,'goblinaxe' if enemy=='GoblinWarrior' else 'VikingAxe'} <= {c for c,n in meshes},meshes
  display=json.loads((dest/'display.json').read_text())
  assert any('cur:120' in line for line in display['hwc_fps_lines']),display
  result={'enemy':enemy,'mode':mode,'native':True,'msaa':4,'pixel_meshes':meshes,'display':display}
  results.append(result);print(json.dumps({'enemy':enemy,'mode':mode,'fps':display['presented_fps'],'meshes':meshes}),flush=True)
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
