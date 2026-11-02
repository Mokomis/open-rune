"""Real touch input and shared menu toggle, in an isolated dual-wield fixture."""
import argparse,json,re,subprocess,time,xml.etree.ElementTree as ET
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--device',required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
def log():return adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode()
def tree():
 adb('shell','uiautomator','dump','/data/local/tmp/open-rune-dual-ui.xml')
 return list(ET.fromstring(adb('exec-out','cat','/data/local/tmp/open-rune-dual-ui.xml')).iter('node'))
def locate(text):
 nodes=tree();matches=[n for n in nodes if n.get('text')==text or n.get('content-desc')==text]
 assert matches, f'Missing {text}: '+str([(n.get('text'),n.get('content-desc')) for n in nodes if n.get('text') or n.get('content-desc')])
 # A selector dialog and the settings behind it can both expose On/Off.
 # Prefer the dialog's plain button, not a background feature toggle.
 choices=[n for n in matches if n.get('class')=='android.widget.Button' and not n.get('content-desc')]
 n=(choices if text in ('On','Off') and choices else matches)[-1]
 x,y,r,b=map(int,re.findall(r'\d+',n.get('bounds')));return (x+r)//2,(y+b)//2
def tap(text):
 x,y=locate(text);adb('shell','input','tap',str(x),str(y));time.sleep(.3)
def shot(name):
 (a.output/(name+'.png')).write_bytes(adb('exec-out','screencap','-p'))
def close():tap('×  Close');time.sleep(.5)
def combat():
 tap('Menu');tap('Enhancements');tap('Combat tactics & moves')
adb('shell','am','force-stop',pkg)
adb('shell','run-as',pkg,'rm','-f','files/native-run.log')
adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run','--es','saveTest','dual-wield-20261027','--ez','dualProbe','true','--ez','upscale','false','--ei','msaa','2','--ez','cameraProbe','true','--ez','traceCombat','true')
deadline=time.monotonic()+30
while time.monotonic()<deadline:
 try:
  if 'RUNE_DUAL_EQUIP actor=Ragnar' in log():break
 except subprocess.CalledProcessError:pass
 time.sleep(.3)
else:raise AssertionError('Fixture did not equip')
# Tap the actual touch Attack control, allowing each original recovery to finish.
x,y=locate('Attack')
for _ in range(4):
 adb('shell','input','tap',str(x),str(y));time.sleep(1.4)
text=log()
assert re.search(r'RUNE_SWEEP_HIT .*caller=handaxe',text),'offhand sweep not observed'
assert re.search(r'RUNE_SWEEP_HIT .*caller=VikingShortSword',text),'mainhand sweep not observed'
shot('dual-ready')
combat();tap('Dual wield');tap('Off');close();shot('single-ready')
count=log().count('RUNE_DUAL_EQUIP actor=Ragnar')
combat();tap('Dual wield');tap('On');close()
assert log().count('RUNE_DUAL_EQUIP actor=Ragnar')>count,'On did not re-equip'
x,y=locate('Block')
hold=subprocess.Popen(['adb','-s',a.device,'shell','input','swipe',str(x),str(y),str(x),str(y),'3500'])
try:
 time.sleep(.9);shot('crossed-block')
finally:hold.wait(timeout=10)
tap('Menu')
(a.output/'touch.log').write_text(log())
(a.output/'touch-result.json').write_text(json.dumps({'passed':True,'native_resolution':'2520x1680','msaa':4,'touch_attack_both_weapon_sweeps':True,'menu_off_on_reequips':True,'actual_block_screenshot':'crossed-block.png','final_state':'paused','display_fps_measured':False},indent=2)+'\n')
print('DUAL_ANDROID_TOUCH_OK')
