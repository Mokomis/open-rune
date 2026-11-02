"""Bounded native4x off/on world-lighting presentation comparison.

Uses an isolated living encounter; these are display samples, not a full campaign
benchmark or synchronized animation workload. Does not change saved settings.
"""
import argparse, json, pathlib, re, subprocess, sys, time, xml.etree.ElementTree as E
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--remastered',action='store_true');p.add_argument('--device',required=True);p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);pkg='org.runeclassic.port'
def adb(*x):return subprocess.check_output(['adb','-s',a.device,*x],timeout=30)
before=adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
world=next((int(n.get('value')) for n in E.fromstring(before) if n.get('name')=='worldFeatures'),1)
results=[]
for mode in [0,768 if a.remastered else 256]:
 dest=a.output/('on' if mode else 'off');dest.mkdir(exist_ok=True)
 adb('shell','am','force-stop',pkg)
 adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run','--es','saveTest','world-lighting',
  '--ei','seed','1','--ei','msaa','2','--ez','upscale','false','--ei','worldFeatures',str((world&~768)|mode),
  '--ez','traceCombat','true','--ez','enemyClearLane','true','--es','enemy','GoblinWarrior','--ei','enemyFrame','120','--ez','endurance','true')
 deadline=time.monotonic()+30
 while time.monotonic()<deadline:
  time.sleep(.5);log=adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode()
  assert 'LAUNCH_FAILED' not in log
  if 'RUNE_ENEMY_SPAWN' in log:break
 assert 'RUNE_ENEMY_SPAWN' in log
 subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('android_present.py')),'--device',a.device,'--samples','24','--output',str(dest/'display')],check=True,stdout=subprocess.DEVNULL)
 log=adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode();(dest/'engine.log').write_text(log)
 assert 'size=2520x1680 scale=1.000000 samples=4' in log
 assert not re.search('LAUNCH_FAILED|Script error:|Unknown native',log)
 if mode:assert 'RUNE_PIXEL_SURFACE kind=baked' in log
 if mode==768:assert 'RUNE_DYNAMIC_SHADOW light=' in log
 display=json.loads((dest/'display.json').read_text());assert any('cur:120' in x for x in display['hwc_fps_lines'])
 results.append({'lighting_mode':mode,'native':True,'msaa':4,'display':display})
 print(json.dumps({'lighting_mode':mode,'displayed_fps':display['presented_fps']}),flush=True)
assert before==adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
