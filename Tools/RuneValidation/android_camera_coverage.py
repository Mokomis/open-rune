"""Reproduce wall crowding and move using actual Android controls, native 4x."""
import argparse, fcntl, json, re, subprocess, sys, time
from pathlib import Path
import xml.etree.ElementTree as ET
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
adb('shell','am','force-stop',pkg)
adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run',
    '--es','saveTest','camera-coverage','--ei','seed','1','--ei','msaa','2',
    '--ez','upscale','false','--ei','clarity','13','--ez','enhanced','true','--ez','cameraProbe','true')
time.sleep(4)
def capture(name):
    time.sleep(1)
    data=adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
    (a.output/(name+'.log')).write_text(data)
    rows=[s for s in data.splitlines() if s.startswith('RUNE_CAMERA ')]
    assert rows and 'LAUNCH_FAILED' not in data
    row=dict(re.findall(r'(\w+)=(\S+)',rows[-1]))
    (a.output/(name+'.png')).write_bytes(adb('exec-out','screencap','-p'))
    print(name+': '+rows[-1],flush=True)
    return row
states={'wall':capture('wall')}
adb('shell','uiautomator','dump','/data/local/tmp/rune-camera-controls.xml')
xml=adb('shell','cat','/data/local/tmp/rune-camera-controls.xml')
buttons={}
for n in ET.fromstring(xml).iter('node'):
    if n.get('text') in ('↑','↓'):
        x0,y0,x1,y1=map(int,re.findall(r'\d+',n.get('bounds')))
        buttons[n.get('text')]=((x0+x1)//2,(y0+y1)//2)
assert len(buttons)==2
for label,name,duration in [('↓','backed-in',1200),('↑','moved-away',2400)]:
    x,y=buttons[label]
    adb('shell','input','swipe',str(x),str(y),str(x),str(y),str(duration))
    states[name]=capture(name)
subprocess.run([sys.executable,str(Path(__file__).with_name('android_present.py')),
    '--device',a.device,'--samples','20','--output',str(a.output/'native-fps')],check=True,stdout=subprocess.DEVNULL)
data=adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
(a.output/'engine.log').write_text(data)
assert 'size=2520x1680 scale=1.000000 samples=4' in data
assert not re.search(r'LAUNCH_FAILED|Script error:|Unknown native',data)
assert float(states['wall']['alpha'])<1
assert float(states['moved-away']['alpha'])>=.99
assert any(states['wall'][k]!=states['backed-in'][k] for k in ['x','y'])
result={'passed':True,'native_4x':True,'actual_android_movement':True,'states':states,
    'displayed_fps':json.loads((a.output/'native-fps.json').read_text())['presented_fps'],
    'coverage':'one wall case and actual backward/forward movement; screenshots require visual review'}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result),flush=True)
