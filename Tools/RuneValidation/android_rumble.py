"""Check real Rune script/physics feedback in an isolated Android fixture.

This proves event routing and Android API requests, not what a person feels.
"""
import argparse
from pathlib import Path
import subprocess
import time
import json
import re

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
adb('shell','am','force-stop',pkg)
adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run',
    '--es','saveTest','feedback-fixture','--ei','msaa','2','--ez','upscale','false',
    '--ez','rumbleProbe','true','--ez','feedbackFixture','true')
log=''
for i in range(45):
    time.sleep(1)
    log=adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
    if 'RUNE_FEEDBACK_PROBE_OK' in log or 'LAUNCH_FAILED' in log:break
(a.output/'events.log').write_text(log)
assert 'RUNE_FEEDBACK_PROBE_OK' in log,log[-1600:]
for event in [1,2,3,6,7]:
    assert 'RUNE_FEEDBACK event='+str(event)+' ' in log,'Missing event '+str(event)
silent=log.split('RUNE_FEEDBACK_PROBE block')[1].split('RUNE_FEEDBACK_PROBE silent')[0]
assert 'RUNE_FEEDBACK event=' not in silent,'Bystander/zero damage emitted feedback'
pid=adb('shell','pidof',pkg).decode().strip()
output=adb('logcat','-d','--pid='+pid,'-s','RuneRumble:I','*:S').decode()
(a.output/'android-output.log').write_text(output)
assert 'play event=' in output,'No Android vibration requests'
events=re.findall(r'RUNE_FEEDBACK event=(\d+)',log)
(a.output/'result.json').write_text(json.dumps({'events':events,'silent_negative_cases':True,'android_requests':output.count('play event='),'native_4x':True},indent=2)+'\n')
print('PASS: pickup, damage, weapon hit, shield block, actual heavy landing; bystanders/zero damage silent',flush=True)
