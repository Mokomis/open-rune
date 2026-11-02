"""Focused Android camera UI and app-only wide viewport checks.

Requires an unlocked device. Restarts Open Rune using isolated saves; leaves
FOV 90/Automatic in a 21:9 preview. Relaunch normally to remove the preview.
Does not change Android display settings or load/write normal save slots.
"""
import argparse,subprocess,time,xml.etree.ElementTree as E,re,json,math
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--device',required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
A=['adb','-s',args.device];out=args.output;out.mkdir(parents=True,exist_ok=True)
def run(*a):return subprocess.check_output(A+list(a),timeout=35)
def tree():
 run('shell','uiautomator','dump','/sdcard/rune-fov.xml');return E.fromstring(run('exec-out','cat','/sdcard/rune-fov.xml'))
def tap(text,desc=False):
 nodes=[n for n in tree().iter('node') if n.get('content-desc' if desc else 'text','').replace('   ✓','')==text and n.get('enabled')=='true'];assert nodes,text
 x,y,r,b=map(int,re.findall(r'\d+',nodes[-1].get('bounds')));run('shell','input','tap',str((x+r)//2),str((y+b)//2))
def shot(name):
 (out/(name+'.png')).write_bytes(run('exec-out','screencap','-p'));(out/(name+'.xml')).write_bytes(E.tostring(tree()))
def launch(aspect=None):
 run('shell','am','force-stop','org.runeclassic.port')
 args=['shell','am','start','-n','org.runeclassic.port/.MainActivity','--es','map','DM-Hildir.run','--es','saveTest','camera-fov','--ez','upscale','false','--ei','msaa','2','--ei','seed','1','--es','probeView','-1423,1235,-160,0,0']
 if aspect:args+=['--ef','aspectPreview',str(aspect)]
 run(*args)
 for _ in range(30):
  time.sleep(1)
  log=run('exec-out','run-as','org.runeclassic.port','cat','files/native-run.log').decode(errors='replace')
  if 'DIRECT_LAUNCH_FAILED' in log:raise RuntimeError(log[-1000:])
  if 'ANDROID_PRESENT images=' in log:break
 else:raise RuntimeError('Game did not finish loading; check screen lock and native log')
 time.sleep(1)
def status():
 t=tree();s=next(n.get('text') for n in t.iter('node') if n.get('text','').startswith('Current camera:'))
 return s
launch();shot('3x2-world');tap('Menu');tap('Settings');tap('Camera & field of view');print(status(),flush=True);shot('3x2-menu')
# Move the actual on-screen slider to its high endpoint.
n=next(n for n in tree().iter('node') if n.get('class')=='android.widget.SeekBar');x,y,r,b=map(int,re.findall(r'\d+',n.get('bounds')))
run('shell','input','swipe',str((x+r)//2),str((y+b)//2),str(r-2),str((y+b)//2),'300')
time.sleep(.5);assert any(n.get('text')=='120°' for n in tree().iter('node'));print('Slider120: '+status(),flush=True)
run('shell','input','keyevent','KEYCODE_BACK');tap('Enhancements');tap('Camera & field of view');assert any(n.get('text')=='120°' for n in tree().iter('node'));tap('Reset FOV')
tap('Widescreen framing');tap('Original');print('Legacy: '+status(),flush=True)
tap('Widescreen framing');tap('Automatic');print('Automatic: '+status(),flush=True)
for label,aspect in [('16x9',16/9),('21x9',21/9)]:
 launch(aspect);shot(label+'-world');tap('Menu');tap('Settings');tap('Camera & field of view');s=status();print(label+': '+s,flush=True);shot(label+'-menu')
 h,v=map(float,re.findall(r'([\d.]+)°',s));expected=math.degrees(2*math.atan(aspect/(4/3)));assert abs(h-expected)<.2,(h,expected);assert abs(v-73.74)<.2,v
 assert any(n.get('text')=='90°' for n in tree().iter('node'))
 (out/(label+'-engine.log')).write_bytes(run('exec-out','run-as','org.runeclassic.port','cat','files/native-run.log'))
print('PASS: touch slider, shared tabs, reset, legacy/automatic, restart, app-only 16:9 and 21:9 native4x',flush=True)
