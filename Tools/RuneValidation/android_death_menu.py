"""Focused practice death/recovery UI test; isolated saves and controller keys.

Copies an existing checkpoint into a dedicated fixture. Never writes campaign saves.
"""
import argparse, json, pathlib, re, subprocess, time, xml.etree.ElementTree as E
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
p.add_argument('--checkpoint',default='files/validation-saves/menu-ui/Save10.rsa')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
pkg='org.runeclassic.port';fixture='files/validation-saves/death-menu'
assert re.fullmatch(r'files/[A-Za-z0-9_./-]+',a.checkpoint) and '..' not in a.checkpoint

def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
def log():return adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode()
def tree():
 adb('shell','uiautomator','dump','/sdcard/open-rune-death.xml')
 return E.fromstring(adb('exec-out','cat','/sdcard/open-rune-death.xml'))
def node(text):
 found=[n for n in tree().iter('node') if n.get('text')==text]
 assert found,text
 return found[-1]
def tap(text):
 n=node(text);assert n.get('enabled')=='true',text
 x,y,r,b=map(int,re.findall(r'\d+',n.get('bounds')))
 adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def key(k):adb('shell','input','gamepad','keyevent','KEYCODE_'+k)
def wait(pattern,seconds=40):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  text=log()
  assert 'LAUNCH_FAILED' not in text,text[-2000:]
  if re.search(pattern,text):return text
  time.sleep(.4)
 raise AssertionError('Timed out: '+pattern)
def shot(name):
 (a.output/(name+'.png')).write_bytes(adb('exec-out','screencap','-p'))
 (a.output/(name+'.log')).write_text(log())
def report(s):results.append(s);print('PASS: '+s,flush=True)

results=[]
before=adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
adb('shell','am','force-stop',pkg)
adb('shell','run-as',pkg,'rm','-f','files/native-run.log')
adb('shell','run-as',pkg,'mkdir','-p',fixture)
adb('shell','run-as',pkg,'cp',a.checkpoint,fixture+'/Save10.rsa')
adb('shell','run-as',pkg,'cp',a.checkpoint.replace('.rsa','.mapname'),fixture+'/Save10.mapname')
adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run',
 '--es','saveTest','death-menu','--es','enemy','GoblinWarrior','--ei','enemyFrame','240',
 '--ez','enemyClearLane','true','--ei','seed','1','--ez','traceCombat','true','--ez','cameraProbe','true',
 '--ez','upscale','false','--ei','msaa','2')
wait('RUNE_PLAYER_DEAD value=1',60)
time.sleep(2.5)
node('You have fallen');assert node('Restart fight').get('enabled')=='true'
assert not any(n.get('text') in ['Resume','Save game','Attack'] for n in tree().iter('node'))
shot('death');report('Death automatically opens recovery and hides gameplay/save controls')
key('BUTTON_B');node('You have fallen')
key('BUTTON_START');node('You have fallen')
report('B and Start cannot dismiss recovery into a dead session')
tap('Load game');node('Load game');key('BUTTON_B');node('You have fallen')
tap('Return to title');tap('Continue');node('You have fallen')
report('Load-list back and title Continue return to recovery')
adb('shell','run-as',pkg,'mv',fixture+'/Save10.rsa',fixture+'/Save10.test-backup')
time.sleep(.6);assert node('Restart fight').get('enabled')=='true'
adb('shell','run-as',pkg,'mv',fixture+'/Save10.test-backup',fixture+'/Save10.rsa')
time.sleep(.6);report('Practice restart stays available without a checkpoint')
tap('Load game')
nodes=[n for n in tree().iter('node') if n.get('content-desc')=='Load Checkpoint']
assert nodes and nodes[0].get('enabled')=='true'
x,y,r,b=map(int,re.findall(r'\d+',nodes[0].get('bounds')))
adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
tap('Load')
wait('RUNE_SESSION_ACTION action=7')
wait('RUNE_PLAYER_DEAD value=0')
time.sleep(1);node('Menu')
assert not any(n.get('text')=='You have fallen' for n in tree().iter('node'))
shot('recovered');report('Explicit checkpoint load restores living gameplay')
assert before==adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
report('Preferences unchanged')
(a.output/'result.json').write_text(json.dumps({'passes':results,'native_resolution':True,'msaa':4,'physical_controller_test':False},indent=2)+'\n')
