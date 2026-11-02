"""Verify actual Android world-feature controls; leaves audio + surface detail on."""
import argparse,fcntl,json,re,subprocess,time
from pathlib import Path
import xml.etree.ElementTree as ET
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--device',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
def tap(label,prefix=False):
    adb('shell','uiautomator','dump','/data/local/tmp/rune-world-controls.xml')
    root=ET.fromstring(adb('shell','cat','/data/local/tmp/rune-world-controls.xml'))
    found=[n for n in root.iter('node') if (n.get('text','').casefold().startswith(label.casefold()) if prefix else n.get('text','').casefold()==label.casefold())]
    assert len(found)==1,(label,[(n.get('text')) for n in root.iter('node')])
    x0,y0,x1,y1=map(int,re.findall(r'\d+',found[0].get('bounds')))
    adb('shell','input','tap',str((x0+x1)//2),str((y0+y1)//2));time.sleep(.4)
def mode(expected):
    time.sleep(.5)
    log=adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
    states=re.findall(r'RUNE_WORLD_FEATURES code=(\d+)',log)
    assert states and int(states[-1])==expected,(expected,states)
    assert not re.search(r'LAUNCH_FAILED|Script error:|\[ALSOFT\] \(EE\)',log)
    return log

tap('Clarity:',True);tap('World detail / sound');tap('Original');mode(0)
tap('Clarity:',True);tap('World detail / sound')
for label,expected in [('Environmental sound',1),('Surface detail',3),('Surface depth',7),('Headphone spatial audio',15)]:tap(label);mode(expected)
(a.output/'all-options.png').write_bytes(adb('exec-out','screencap','-p'))
tap('Surface depth');mode(11);tap('Headphone spatial audio');log=mode(3)
(a.output/'selected-options.png').write_bytes(adb('exec-out','screencap','-p'))
tap('Close')
assert 'RUNE_AUDIO_HRTF requested=1 active=1 reset=1' in log and 'RUNE_AUDIO_HRTF requested=0 active=0 reset=1' in log
(a.output/'engine.log').write_text(log)
(a.output/'result.json').write_text(json.dumps({'passed':True,'actual_taps':True,'states':[0,1,3,7,15,11,3],'selected':3},indent=2)+'\n')
print('RUNE_WORLD_CONTROLS_OK states=0,1,3,7,15,11,3 headphone_active_and_disabled=1')
