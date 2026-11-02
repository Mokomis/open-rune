"""Focused live scaling UI checks. Isolated map; preferences restored on exit.

Tests Native, system 75/50, GSR 50/75, shared pages and persistence. Does not
assert every Android device uses hardware composition; retain HWC evidence.
"""
import argparse,fcntl,json,pathlib,re,subprocess,time,xml.etree.ElementTree as E
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--device',required=True);p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=40)
def tree():
 adb('shell','uiautomator','dump','--compressed','/sdcard/rune-scaling-menu.xml')
 return E.fromstring(adb('exec-out','cat','/sdcard/rune-scaling-menu.xml'))
def click(n):
 x,y,r,b=map(int,re.findall(r'\d+',n.get('bounds')));adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def tap(text):
 nodes=[n for n in tree().iter('node') if n.get('text')==text and n.get('enabled')=='true'];assert nodes,text;click(nodes[-1])
def choose(name,value):
 tap(name);nodes=[n for n in tree().iter('node') if n.get('text','').replace('   ✓','')==value and n.get('class')=='android.widget.Button'];assert nodes,value;click(nodes[0])
def expect(name,value):assert any(n.get('content-desc')==name+': '+value for n in tree().iter('node')),(name,value)
def status(expected):
 for _ in range(4):
  t=tree()
  if any(n.get('text')==expected for n in t.iter('node')):return
  time.sleep(.5)
 raise AssertionError((expected,E.tostring(t).decode()))
def log():return adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode()
def launch():
 adb('shell','am','force-stop',pkg)
 adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run','--es','saveTest','scaling-menu','--ei','msaa','2','--ei','seed','1','--ez','traceCombat','true')
 time.sleep(5);tap('Menu');tap('Settings');tap('Resolution & scaling')
def capture(name):
 (a.output/(name+'.png')).write_bytes(adb('exec-out','screencap','-p'))
 (a.output/(name+'.xml')).write_bytes(E.tostring(tree()))
def scene(name,measure=False):
 tap('×  Close');time.sleep(2)
 s=adb('shell','dumpsys','SurfaceFlinger').decode(errors='replace')
 start=s.index('HWC layers:');lines=s[start:].splitlines();out=lines[:5]
 for i,l in enumerate(lines):
  if pkg in l and i+1<len(lines) and re.search(r'\|\s*(DEVICE|CLIENT)\s*\|',lines[i+1]):out.extend(lines[i:i+2])
 if ' Table for DPU - 0' in s:
  start=s.index(' Table for DPU - 0');end=s.find('------------APOLLO',start);out.append(s[start:end if end>=0 else start+3500])
 out.extend(l for l in s.splitlines() if 'FPS min:' in l)
 (a.output/(name+'-composition.txt')).write_text('\n'.join(out)+'\n')
 if measure:subprocess.run(['python3',str(pathlib.Path(__file__).with_name('android_present.py')),'--device',a.device,'--samples','12','--oppo-clocks','--output',str(a.output/(name+'-display'))],check=True,stdout=subprocess.DEVNULL)
 (a.output/(name+'-scene.png')).write_bytes(adb('exec-out','screencap','-p'))
 tap('Menu');tap('Settings');tap('Resolution & scaling')
backup=adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml');(a.output/'preferences-before.xml').write_bytes(backup)
try:
 launch();choose('Rendering resolution','Native (100%)');status('Native · 2520×1680 → 2520×1680 display');scene('native',True)
 choose('Scaling method','Hardware Scaling');choose('Rendering resolution','75%');status('Hardware Scaling · 1890×1260 → 2520×1680 display');capture('system75-menu');scene('system75',True)
 choose('Rendering resolution','50%');status('Hardware Scaling · 1260×840 → 2520×1680 display');scene('system50')
 adb('shell','input','keyevent','KEYCODE_BACK');tap('Enhancements');tap('Resolution & scaling');expect('Rendering resolution','50%');expect('Scaling method','Hardware Scaling')
 choose('Scaling method','Snapdragon GSR');status('GSR · 1260×840 → 2520×1680 display');scene('gsr50')
 choose('Rendering resolution','75%');status('GSR · 1890×1260 → 2520×1680 display');capture('gsr75-menu');scene('gsr75',True)
 choose('Rendering resolution','Native (100%)');status('Native · 2520×1680 → 2520×1680 display')
 choose('Rendering resolution','50%');choose('Scaling method','Hardware Scaling')
 (a.output/'switching.log').write_text(log());assert 'LAUNCH_FAILED' not in log()
 launch();expect('Rendering resolution','50%');expect('Scaling method','Hardware Scaling');status('Hardware Scaling · 1260×840 → 2520×1680 display')
 (a.output/'persisted.log').write_text(log())
 # Return through real controls, then restore exact pre-test preference bytes.
 choose('Rendering resolution','Native (100%)');status('Native · 2520×1680 → 2520×1680 display')
 result={'native_system75_system50_gsr50_gsr75_live':True,'actual_render_dimensions':True,'shared_video_enhancements':True,'restart_system50':True,'native_roundtrip':True,'msaa':4}
 (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
finally:
 adb('shell','am','force-stop',pkg)
 subprocess.run(['adb','-s',a.device,'shell','run-as',pkg,'sh','-c',"'cat > shared_prefs/MainActivity.xml'"],input=backup,check=True,timeout=30)
 assert adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')==backup
