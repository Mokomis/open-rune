"""Actual Android touch layout regression; isolated saves, restores preferences."""
import argparse,subprocess,time,re,json,shlex,xml.etree.ElementTree as E
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--device',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
pkg='org.runeclassic.port';prefix=['adb','-s',a.device]
def adb(*args):return subprocess.check_output(prefix+list(args),timeout=40)
def tree():
 adb('shell','uiautomator','dump','/data/local/tmp/touch-layout.xml');return E.fromstring(adb('exec-out','cat','/data/local/tmp/touch-layout.xml'))
def bounds(n):return list(map(int,re.findall(r'\d+',n.get('bounds'))))
def node(text,desc=False):
 ns=[n for n in tree().iter('node') if n.get('content-desc' if desc else 'text')==text and n.get('enabled')=='true'];assert ns,text;return ns[-1]
def tap(text):
 x,y,r,b=bounds(node(text));adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def swipe(x,y,r,b):adb('shell','input','swipe',str(round(x)),str(round(y)),str(round(r)),str(round(b)),'450')
def prefs():
 time.sleep(.15);return {n.get('name'):n.get('value',n.text) for n in E.fromstring(adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml'))}
def shot(name):a.output.joinpath(name+'.png').write_bytes(adb('exec-out','screencap','-p'))
def launch():
 adb('shell','am','force-stop',pkg);adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run','--es','saveTest','touch-layout','--ei','surfacePercent','100','--ei','msaa','2','--ez','upscale','false','--ez','traceCombat','true');time.sleep(4)
def edit():
 tap('Menu');tap('Settings');tap('Controls');tap('Edit touch controls')
def report(s):print('PASS: '+s,flush=True)
backup=adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml');(a.output/'preferences-before.xml').write_bytes(backup)
try:
 launch();shot('gameplay-native');edit();shot('editor-native')
 # Move/resize Attack, independently adjust opacity, save, then check disk.
 x,y,r,b=bounds(node('Attack'));swipe((x+r)/2,(y+b)/2,(x+r)/2-170,(y+b)/2-70)
 x,y,r,b=bounds(node('Attack'));swipe(r-6,b-6,r+65,b+65)
 x,y,r,b=bounds(node('Selected control opacity',True));adb('shell','input','tap',str(round(x+(r-x)*.4)),str((y+b)//2))
 tap('Done');saved=prefs();assert float(saved['touchLayout.attack.size'])>1.15;assert float(saved['touchLayout.attack.x'])<.985;assert float(saved['touchLayout.attack.opacity'])<.65
 assert saved['touchLayout.block.size']=='1.0';report('Touch drag, corner resize, independent opacity and save')
 tap('Edit touch controls');tap('Reset layout');tap('Cancel');assert prefs()==saved;report('Cancel discards reset')
 # Move and resize right stick, Menu and look area via selector to avoid overlap.
 tap('Edit touch controls');tap('Reset layout');tap('Done');tap('Edit touch controls')
 for label in ['Camera','Menu','Use','Block','Attack','Kick','Jump','Throw','Dive','Move','Look area']:
  for _ in range(11):
   if any(n.get('text')=='Selected: '+label for n in tree().iter('node')):break
   tap('Control ›')
  else:raise AssertionError(label)
  x,y,r,b=bounds(node('Selected control size',True));adb('shell','input','tap',str(round(x+(r-x)*.52)),str((y+b)//2))
 tap('Done');all_sizes=prefs();assert all(float(all_sizes['touchLayout.'+k+'.size'])>1.1 for k in ['camera','menu','use','block','attack','kick','jump','throw','dive','move','look']);report('Every control, both sticks, Menu and look area independently resizable')
 launch();edit();assert prefs()==all_sizes;report('Layout survives activity/process restart')
 # Reset final test layout and save so gameplay checks have predictable controls.
 tap('Reset layout');tap('Done');adb('shell','input','keyevent','4');adb('shell','input','keyevent','4');shot('gameplay-reset')
 log=adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode();assert 'LAUNCH_FAILED' not in log
 (a.output/'result.json').write_text(json.dumps({'drag':True,'corner_resize':True,'individual_opacity':True,'all_controls_resize':True,'cancel':True,'restart':True,'normal_saves':'isolated'},indent=2))
finally:
 adb('shell','am','force-stop',pkg)
 subprocess.run(prefix+['shell','run-as '+pkg+' sh -c '+shlex.quote('cat > shared_prefs/MainActivity.xml')],input=backup,check=True,timeout=30)
