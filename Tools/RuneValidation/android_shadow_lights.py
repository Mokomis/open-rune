"""Bounded native/4x Android shadow-slot and authored-scene checks.

Restarts Open Rune, uses isolated saves, and keeps normal preferences intact.
The authored map may have fewer eligible lights than the requested maximum;
eight-slot correctness is established separately by actual GPU pixel readback.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--cases',nargs='+',choices=['eight-slot-gpu','full-vulkan','map-three','map-eight'],default=['eight-slot-gpu','full-vulkan','map-three','map-eight'])
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
adb=['adb','-s',a.device];pkg='org.runeclassic.port'
def call(*args):return subprocess.check_output(adb+list(args),timeout=30)
def shell(*args):return call('shell',*args).decode(errors='replace')
preferences={n:call('exec-out','run-as',pkg,'cat','shared_prefs/'+n) for n in ['MainActivity.xml','multiplayer.xml']}
for n,b in preferences.items():(a.output/('before-'+n)).write_bytes(b)
result={'passed':False,'cases':[]}
try:
    for name,budget,frames,extra,marker in [
        ('eight-slot-gpu',8,130,['--ez','shadowBudgetProbe','true'],'RUNE_SHADOW_BUDGET_GPU_OK capacity=8 cases=68'),
        ('full-vulkan',8,130,['--ez','vulkanProbe','true'],'RUNE_VULKAN_PROBE_OK'),
        ('map-three',3,360,['--ei','profile','240'],'BOUNDED_RUN_COMPLETE'),
        ('map-eight',8,360,['--ei','profile','240'],'BOUNDED_RUN_COMPLETE')]:
        if name not in a.cases:continue
        shell('am','force-stop',pkg)
        shell('run-as',pkg,'rm','-f','files/native-run.log','files/frame.ppm')
        args=['am','start','-W','-n',pkg+'/.MainActivity','--es','saveTest','shadow-lights-'+name,
            '--es','map','DM-Hildir.run','--es','probeStart','PlayerStart4','--es','vulkanDriver','system','--ei','surfacePercent','100',
            '--ei','msaa','2','--ei','frames',str(frames),'--ei','probeTickHz','120','--ei','seed','1',
            '--ei','worldFeatures','4864','--ei','shadowLights',str(budget),'--ei','fps','120',
            '--ez','enhanced','false','--ei','clarity','0','--ei','soundRemaster','0','--ei','texturePack','0',
            '--ez','traceShadows','true',*extra]
        shell(*args);deadline=time.monotonic()+150;log=''
        while time.monotonic()<deadline:
            time.sleep(1)
            try:log=call('exec-out','run-as',pkg,'cat','files/native-run.log').decode(errors='replace')
            except subprocess.CalledProcessError:continue
            if 'BOUNDED_RUN_COMPLETE' in log or 'ANDROID_LAUNCH_FAILED' in log:break
        (a.output/(name+'.log')).write_text(log)
        assert marker in log and 'BOUNDED_RUN_COMPLETE' in log,name+': missing successful completion'
        assert 'size=2520x1680 scale=1.000000 samples=4' in log,name+': not OPPO native/4x'
        assert 'RUNE_SHADOW_BUDGET requested='+str(budget) in log,name+': wrong active budget'
        if name=='map-eight':
            assert any('requested=8' in line and int(line.rsplit('active=',1)[1])>3 for line in log.splitlines() if line.startswith('RUNE_SHADOW_BUDGET requested=')), 'Authored scene did not exercise more than three lights'
        assert not any(e in log for e in ['ANDROID_LAUNCH_FAILED','Script error:','Unknown native']),name+': runtime error'
        with (a.output/(name+'.ppm')).open('wb') as f:
            subprocess.run(adb+['exec-out','run-as',pkg,'cat','files/frame.ppm'],stdout=f,check=True,timeout=30)
        result['cases'].append({'name':name,'budget':budget,'passed':True,'command':args,
            'profiles':[line for line in log.splitlines() if line.startswith(('RUNE_PROFILE ','RUNE_VULKAN_PROFILE ','RUNE_RENDER_PROFILE ','RUNE_SHADOW_BUDGET requested='))]})
        print('PASS',name,flush=True)
        (a.output/'result.json').write_text(json.dumps(result,indent=2))
    result['passed']=True
finally:
    shell('am','force-stop',pkg)
    result['preferences_unchanged']=all(call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in preferences.items())
    if not result['preferences_unchanged']:
        for n,b in preferences.items():
            file=a.output/('restore-'+n);file.write_bytes(b);tmp='/data/local/tmp/rune-shadow-'+n
            call('push',str(file),tmp);shell('run-as',pkg,'cp',tmp,'shared_prefs/'+n);shell('rm',tmp)
        result['preferences_restored']=all(call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in preferences.items())
    (a.output/'result.json').write_text(json.dumps(result,indent=2))
    shell('am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000')
    result['normal_launch_restored']=True
    (a.output/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result),flush=True)
