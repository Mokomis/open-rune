"""Actual touch navigation into the isolated physics playground.

Restarts Open Rune and restores preferences afterwards. The normal Save folder
is hashed before/after; the test world has its own save directory.
"""
import argparse,json,re,subprocess,time,xml.etree.ElementTree as ET
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--device',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
adb=['adb','-s',a.device];pkg='org.runeclassic.port'
def call(*args):return subprocess.check_output(adb+list(args),timeout=45)
def shell(*args):return call('shell',*args).decode(errors='replace')
def tree():shell('uiautomator','dump','--compressed','/data/local/tmp/rune-physics-ui.xml');return ET.fromstring(shell('cat','/data/local/tmp/rune-physics-ui.xml'))
def tap(text):
    rows=[n for n in tree().iter('node')if n.get('text')==text and n.get('clickable')=='true' and n.get('enabled')=='true'];assert rows,'Missing UI button '+text
    x,y,r,b=map(int,re.findall(r'\d+',rows[-1].get('bounds')));shell('input','tap',str((x+r)//2),str((y+b)//2));time.sleep(.25)
def saves():return sorted(call('exec-out','run-as',pkg,'find','files/game/Save','-type','f','-exec','sha256sum','{}','+').splitlines())
prefs={n:call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)for n in ['MainActivity.xml','multiplayer.xml']};before=saves();result={'passed':False}
for n,b in prefs.items():(a.output/('before-'+n)).write_bytes(b)
try:
    shell('input','keyevent','KEYCODE_WAKEUP');shell('wm','dismiss-keyguard');shell('am','force-stop',pkg)
    shell('am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000','--es','saveTest','physics-menu-20261104','--ei','surfacePercent','100','--ei','msaa','2','--ei','fps','120')
    time.sleep(4);tap('Settings');tap('Gameplay');tap('Cinematic physics')
    (a.output/'menu.png').write_bytes(call('exec-out','screencap','-p'));(a.output/'menu.xml').write_bytes(ET.tostring(tree()))
    # Use the ordinary choice menu, not a native setting injection.
    text=[n.get('text')for n in tree().iter('node')if n.get('clickable')=='true']
    tap('Original' if 'Original' in text else 'Chaos' if 'Chaos' in text else 'Cinematic')
    tap('Cinematic')
    tap('Physics playground');tap('Enter');deadline=time.monotonic()+30;log=''
    while time.monotonic()<deadline:
        time.sleep(1)
        try:log=call('exec-out','run-as',pkg,'cat','files/native-run.log').decode(errors='replace')
        except subprocess.CalledProcessError:continue
        if 'RUNE_PHYSICS_PLAYGROUND_READY' in log:break
    assert 'RUNE_NEW_FIGHT arena=DM-Hildir.run' in log and 'RUNE_PHYSICS_PLAYGROUND_READY' in log,'UI action did not start isolated arena'
    assert not any(x in log for x in ['DIRECT_LAUNCH_FAILED','ANDROID_LAUNCH_FAILED','Unknown native','Script error:']),'Runtime error'
    (a.output/'arena.png').write_bytes(call('exec-out','screencap','-p'));(a.output/'game.log').write_text(log)
    result.update(passed=True,actual_touch_navigation=True,arena_started=True)
finally:
    shell('am','force-stop',pkg)
    for n,b in prefs.items():
        f=a.output/('restore-'+n);f.write_bytes(b);tmp='/data/local/tmp/rune-physics-'+n;call('push',str(f),tmp);shell('run-as',pkg,'cp',tmp,'shared_prefs/'+n);shell('rm',tmp)
    result['preferences_restored']=all(call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in prefs.items());result['normal_saves_unchanged']=saves()==before
    (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    shell('am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000')
assert result['passed'] and result['preferences_restored'] and result['normal_saves_unchanged'];print(json.dumps(result),flush=True)
