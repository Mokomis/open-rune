"""Exercise combat kicks through actual Android touch/controller input.

Fixture positions a DarkViking and pauses its ordinary AI for controlled player
and enemy impacts. Mob kick starts use production Start; player starts/blocks
use real screen controls. Isolated saves, native resolution and 4x MSAA.
"""
import argparse,subprocess,pathlib,time,re,json,xml.etree.ElementTree as ET,fcntl
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True);p.add_argument('--output',type=pathlib.Path,required=True)
p.add_argument('--cases',nargs='+',default=['player-unblocked','player-blocked','mob-unblocked','mob-blocked'])
p.add_argument('--controller',action='store_true')
p.add_argument('--rumble-probe',action='store_true',help='Trace game feedback for integration checks')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
def button(label):
 adb('shell','uiautomator','dump','--compressed','/data/local/tmp/rune-kick-ui.xml')
 root=ET.fromstring(adb('shell','cat','/data/local/tmp/rune-kick-ui.xml'))
 for n in root.iter('node'):
  if n.get('text')==label and n.get('enabled')=='true':
   x,y,r,b=map(int,re.findall(r'\d+',n.get('bounds')));return (str((x+r)//2),str((y+b)//2))
 raise AssertionError('Missing '+label)
for case in a.cases:
 adb('shell','am','force-stop',pkg);adb('shell','run-as',pkg,'rm','-f','files/native-run.log')
 adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run','--es','saveTest','kick-'+case,'--es','kickProbe',case,'--ez','rumbleProbe',str(a.rumble_probe).lower(),'--ei','frames','1600','--ei','msaa','2','--ez','upscale','false','--ez','enhanced','false')
 log='';triggered=False;hold=None;shot=False;deadline=time.monotonic()+55
 try:
  while time.monotonic()<deadline:
   time.sleep(.2)
   try:log=adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
   except subprocess.CalledProcessError:continue
   if re.search(r'LAUNCH_FAILED|Script error:|Unknown native',log):break
   if 'RUNE_KICK_PROBE_READY' in log and not triggered:
    if case.startswith('player-'):
     if a.controller:adb('shell','input','gamepad','keyevent','107')
     else:adb('shell','input','tap',*button('Kick'))
    elif case=='mob-blocked':
     x,y=button('Block');hold=subprocess.Popen(['adb','-s',a.device,'shell','input','swipe',x,y,x,y,'3000'],stdout=subprocess.DEVNULL)
    triggered=True
   if 'RUNE_KICK_START' in log and not shot:
    (a.output/(case+'.png')).write_bytes(adb('exec-out','screencap','-p'));shot=True
   if 'BOUNDED_RUN_COMPLETE' in log:break
 finally:
  if hold:hold.wait(timeout=10)
  (a.output/(case+'.log')).write_text(log)
 assert 'RUNE_KICK_PROBE_OK' in log and 'BOUNDED_RUN_COMPLETE' in log,log[-3000:]
 assert not re.search(r'LAUNCH_FAILED|Script error:|Unknown native',log)
 if a.rumble_probe:
  expected=4 if case.startswith('player-') else 2 if case=='mob-blocked' else 3
  assert 'RUNE_FEEDBACK event='+str(expected)+' ' in log,'Missing expected rumble event '+str(expected)
 line=next(x for x in log.splitlines() if 'RUNE_KICK_PROBE_OK' in x)
 result=dict(re.findall(r'(\w+)=(\S+)',line));result.update(controller=a.controller,passed=True)
 (a.output/(case+'.json')).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
