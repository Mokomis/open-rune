"""Check original Android saves are visible on a cold normal launch.

Never loads, writes, restores or deletes a save. Leaves Load game open. Uses no
save-directory/test extras, so this also validates the diagnostic handoff boundary.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
adb=['adb','-s',a.device];pkg='org.runeclassic.port'
def call(*args):return subprocess.check_output(adb+list(args),timeout=30)
def shell(*args):return call('shell',*args)
def hashes():return sorted(call('exec-out','run-as',pkg,'find','files/game/Save','-type','f','-exec','sha256sum','{}','+').splitlines())
def tree():
    shell('uiautomator','dump','--compressed','/data/local/tmp/rune-normal-saves.xml')
    return ET.fromstring(call('exec-out','cat','/data/local/tmp/rune-normal-saves.xml'))
def tap(node):
    l,t,r,b=map(int,re.findall(r'\d+',node.get('bounds')))
    shell('input','tap',str((l+r)//2),str((t+b)//2))
original=hashes();result={'passed':False}
try:
    shell('am','force-stop',pkg);start=time.monotonic()
    shell('am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000')
    end=start+40
    while time.monotonic()<end:
        root=tree();buttons=[n for n in root.iter('node') if n.get('text')=='Load game' and n.get('clickable')=='true' and n.get('enabled')=='true']
        if buttons:break
        time.sleep(.1)
    else:raise AssertionError('Load game did not become available')
    result['load_opened_seconds_after_start']=time.monotonic()-start;tap(buttons[-1]);root=tree()
    (a.output/'save-list.xml').write_bytes(ET.tostring(root));(a.output/'save-list.png').write_bytes(call('exec-out','screencap','-p'))
    enabled={n.get('content-desc'):n.get('enabled')=='true' for n in root.iter('node') if n.get('content-desc','').startswith('Load ')}
    expected=[]
    for slot,name in [(9,'Quick save'),(10,'Checkpoint'),(0,'Manual save 1')]:
        if any(row.endswith(('/Save'+str(slot)+'.rsa').encode()) for row in original):
            expected.append(name);assert enabled.get('Load '+name),'Existing '+name+' is shown as missing'
    assert expected,'No original saves available to exercise the menu'
    result.update(passed=True,visible_original_slots=expected,save_load_buttons=enabled)
finally:
    result['original_save_hashes_unchanged']=hashes()==original
    (a.output/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
assert result['passed'] and result['original_save_hashes_unchanged']
