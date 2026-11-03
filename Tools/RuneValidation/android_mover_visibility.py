"""Matched native/4x LostCrypt comparison for the fixed-world visibility cache.

Uses isolated saves during the comparison, preserves normal saves/preferences,
and returns to LostCrypt with normal save access and the user's saved settings.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);adb=['adb','-s',a.device];pkg='org.runeclassic.port'
def call(*args):return subprocess.check_output(adb+list(args),timeout=30)
def shell(*args):return call('shell',*args).decode(errors='replace')
def saves():return sorted(call('exec-out','run-as',pkg,'find','files/game/Save','-type','f','-exec','sha256sum','{}','+').splitlines())
original_saves=saves();prefs={n:call('exec-out','run-as',pkg,'cat','shared_prefs/'+n) for n in ['MainActivity.xml','multiplayer.xml']}
values={n.get('name'):n.get('value',n.text) for n in ET.fromstring(prefs['MainActivity.xml'])}
world=int(values.get('worldFeatures',20399));texture=int(values.get('menu.texturePack',0));result={'passed':False,'cases':[]}
try:
    for cache in [0,1]:
        name='reference' if cache==0 else 'optimized'
        shell('am','force-stop',pkg);shell('run-as',pkg,'rm','-f','files/native-run.log','files/frame.ppm')
        shell('am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000','--es','map','DM-LostCrypt.run',
            '--es','saveTest','lostcrypt-visibility-'+name,'--ei','surfacePercent','100','--ei','msaa','2',
            '--ei','seed','1','--ei','probeTickHz','120','--ei','frames','360','--ei','profile','240',
            '--ei','fps','120','--ei','worldFeatures',str(world),'--ei','texturePack',str(texture),
            '--es','vulkanDriver','system','--ei','shadowLights','1','--ei','moverVisibilityCache',str(cache),
            '--ez','cameraProbe','true')
        deadline=time.monotonic()+120;log=''
        while time.monotonic()<deadline:
            time.sleep(1)
            try:log=call('exec-out','run-as',pkg,'cat','files/native-run.log').decode(errors='replace')
            except subprocess.CalledProcessError:continue
            if 'BOUNDED_RUN_COMPLETE' in log or 'ANDROID_LAUNCH_FAILED' in log:break
        (a.output/(name+'.log')).write_text(log)
        assert 'BOUNDED_RUN_COMPLETE' in log and 'ANDROID_LAUNCH_FAILED' not in log,name+' failed'
        assert 'size=2520x1680 scale=1.000000 samples=4' in log,'Not native OPPO/4x'
        with (a.output/(name+'.ppm')).open('wb') as f:subprocess.run(adb+['exec-out','run-as',pkg,'cat','files/frame.ppm'],stdout=f,check=True,timeout=30)
        row={'name':name,'cache':cache,'profiles':[l for l in log.splitlines() if l.startswith(('RUNE_PROFILE ','RUNE_RENDER_PROFILE ','RUNE_VULKAN_PROFILE '))],'trajectory':[l for l in log.splitlines() if l.startswith('RUNE_CAMERA ')]}
        result['cases'].append(row);print(name,*row['profiles'],flush=True)
    # PPM headers and pixels use the same capture path and dimensions.
    result['captures_byte_identical']=(a.output/'reference.ppm').read_bytes()==(a.output/'optimized.ppm').read_bytes()
    result['trajectories_match']=result['cases'][0]['trajectory']==result['cases'][1]['trajectory']
    assert result['captures_byte_identical'] and result['trajectories_match'],'Comparison differs; inspect captures and input'
    result['passed']=True
finally:
    shell('am','force-stop',pkg)
    result['preferences_unchanged']=all(call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in prefs.items())
    if not result['preferences_unchanged']:
        for n,b in prefs.items():
            file=a.output/('restore-'+n);file.write_bytes(b);tmp='/data/local/tmp/rune-visibility-'+n
            call('push',str(file),tmp);shell('run-as',pkg,'cp',tmp,'shared_prefs/'+n);shell('rm',tmp)
        result['preferences_restored']=all(call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in prefs.items())
    result['original_save_hashes_unchanged']=saves()==original_saves
    shell('am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000','--es','map','DM-LostCrypt.run','--ei','seed','1')
    result['normal_save_access_restored']=True
    (a.output/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
assert result['passed'] and result['original_save_hashes_unchanged']
