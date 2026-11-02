"""Two bounded presentation samples, native 4x and 120 FPS target only.

All new effects enabled, using Approximate water. Isolated saves; no performance-setting matrix and no
claim of whole-campaign or sustained performance. Run without validation.
"""
import argparse,fcntl,json,pathlib,re,subprocess,sys,time
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True);p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
before=adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
results=[]
for case,mapname,marker,extra in [
 ('combat','DM-Hildir','RUNE_ENEMY_SPAWN',['--es','enemy','GoblinWarrior','--ei','enemyFrame','120','--ez','enemyClearLane','true','--ez','endurance','true']),
 ('ocean','SailingShip','RUNE_WATER_APPROXIMATE',[])]:
 dest=a.output/case;dest.mkdir(exist_ok=True)
 adb('shell','am','force-stop',pkg)
 adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map',mapname+'.run','--es','saveTest','materials-presentation',
     '--ei','seed','1','--ei','msaa','2','--ez','upscale','false','--ei','worldFeatures','8103','--ez','traceCombat','true',*extra)
 deadline=time.monotonic()+60
 while time.monotonic()<deadline:
  time.sleep(.5);log=adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode()
  assert 'LAUNCH_FAILED' not in log
  if marker in log:break
 else:raise AssertionError('Expected scene not reached: '+marker)
 time.sleep(3)
 subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('android_present.py')),'--device',a.device,'--samples','24','--output',str(dest/'display')],check=True,stdout=subprocess.DEVNULL)
 log=adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode();(dest/'engine.log').write_text(log)
 assert 'size=2520x1680 scale=1.000000 samples=4' in log
 assert 'RUNE_VULKAN_VALIDATION active=1' not in log
 assert not re.search(r'LAUNCH_FAILED|Script error:|Unknown native|\[vulkan (error|warning)\]',log)
 display=json.loads((dest/'display.json').read_text());assert any('cur:120' in x for x in display['hwc_fps_lines'])
 (dest/'screen.png').write_bytes(adb('exec-out','screencap','-p'))
 result={'case':case,'native':True,'msaa':4,'target_fps':120,'world_features':8103,'display':display}
 results.append(result);print(json.dumps({'case':case,'presented_fps':display['presented_fps']}),flush=True)
assert before==adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
