"""Focused HUD editing checks; not a settings-combination matrix."""
import subprocess,xml.etree.ElementTree as ET,re,time,json
from pathlib import Path
import argparse,shlex,fcntl
parser=argparse.ArgumentParser(description="Touch/controller HUD editor check on an unlocked OPPO; restores preferences and uses isolated saves.")
parser.add_argument('--device',required=True)
parser.add_argument('--output',type=Path,required=True)
a=parser.parse_args();out=a.output;out.mkdir(parents=True,exist_ok=True)
pkg='org.runeclassic.port';dev=a.device
lock=open(out.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
def adb(*a):return subprocess.check_output(['adb','-s',dev,*a],timeout=30)
def tree():
 adb('shell','uiautomator','dump','--compressed','/data/local/tmp/rune-hud.xml')
 return ET.fromstring(adb('shell','cat','/data/local/tmp/rune-hud.xml'))
def tap(text):
 ns=[n for n in tree().iter('node') if n.get('text')==text and n.get('enabled')=='true'];assert ns,text
 x,y,r,b=map(int,re.findall(r'\d+',ns[-1].get('bounds')));adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def xy(x,y):return str(round(x*2520/1536)),str(round(y*1680/1024))
def swipe(x,y,a,b):adb('shell','input','swipe',*xy(x,y),*xy(a,b),'650')
def prefs():
 time.sleep(.35)
 return {n.get('name'):n.get('value',n.text) for n in ET.fromstring(adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml'))}
def shot(name):out.joinpath(name+'.png').write_bytes(adb('exec-out','screencap','-p'))
def check(v,m):assert v,m;print('PASS:',m,flush=True)
backup=adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
(out/'preferences-before.xml').write_bytes(backup)
try:
 adb('shell','am','force-stop',pkg)
 adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run','--es','saveTest','hud-ui','--ez','upscale','false','--ei','msaa','2','--ei','seed','1','--es','shield','VikingShield')
 time.sleep(3)
 adb('shell','input','keyevent','4')
 assert tree().get('rotation')=='1','This touch fixture expects landscape rotation 90'
 tap('Edit HUD');shot('editor-vertical')
 tap('Vertical preset');swipe(80,200,300,330)
 # Selected Health moved from (38.7,62.4) to (258.7,192.4); corner is (338.7,447.4).
 swipe(339,447,355,498);tap('Orientation: Vertical');tap('+');tap('Save layout')
 p=prefs();check(float(p['hud.health.x'])>.1,'Touch drag saved Health position')
 check(float(p['hud.health.size'])>1.15,'Corner resize and size buttons saved')
 check(p['hud.health.horizontal']=='true','Per-meter horizontal saved')
 check(p['hud.shield.size']=='1.0' and p['hud.shield.horizontal']=='false','Other elements unchanged')
 tap('Edit HUD');tap('−');tap('Cancel');check(prefs()==p,'Cancel leaves saved preferences unchanged')
 tap('Edit HUD');tap('Horizontal preset');shot('editor-horizontal');tap('Save layout');tap('Resume');shot('horizontal-game')
 adb('shell','input','keyevent','4');tap('Edit HUD');tap('Vertical preset')
 # Exercise Android's actual key dispatch with gamepad L3 held while nudging cursor.
 adb('shell','input','keyevent',*(['21']*32+['19']*10))
 adb('shell','input','keycombination','-t','150','106','22')
 adb('shell','input','keyevent','108') # Start saves editor
 p=prefs();check(float(p['hud.health.x'])>.02,'Gamepad L3 + D-pad drag saved')
 tap('Edit HUD');tap('Vertical preset');tap('Save layout');tap('Resume');shot('vertical-game')
 check('LAUNCH_FAILED' not in adb('shell','run-as',pkg,'cat','files/native-run.log').decode(),'No engine failure during editing')
 out.joinpath('ui-checks.json').write_text(json.dumps({'touch_move':True,'corner_resize':True,'individual_orientation_size':True,'cancel':True,'presets':True,'controller_L3_dpad':True},indent=2))
finally:
 adb('shell','am','force-stop',pkg)
 # stdin avoids interpolating preference contents into shell commands.
 subprocess.run(['adb','-s',dev,'shell','run-as '+pkg+' sh -c '+shlex.quote('cat > shared_prefs/MainActivity.xml')],input=backup,check=True,timeout=30)
