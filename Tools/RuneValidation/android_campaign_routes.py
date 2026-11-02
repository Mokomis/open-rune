"""Exercise original exit scripts and isolated save continuity across chapters.

These fixtures call the placed exit's Touch event directly. They do not solve
puzzles, navigate to the exit, or establish full campaign completion.
"""
import argparse, fcntl, json, re, subprocess, sys, time
from pathlib import Path

ROUTES = {
    'DeepUnder3': ('Teleporter1', 'Hel1/Start'),
    'Hel1': ('Teleporter0', 'Hel1a/start'),
    'Hel1a': ('Teleporter1', 'Hel1a2/start'),
    'Hel3a': ('Teleporter0', 'HelLift/start'),
    'HelLift': ('Teleporter1', 'Goblin2/start'),
    'Thor1': ('Teleporter3', 'Thormap3/start'),
    'DwarfTrans': ('Teleporter0', 'Dwarf1wwheel/start'),
    'Dwarf1wwheel': ('Teleporter1', 'DwarfMap2/Start'),
    'Loki1': ('Teleporter2', 'loki1A/start'),
    'Loki2': ('Teleporter2', 'loki3a/start'),
}
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--maps',nargs='+',choices=ROUTES,default=list(ROUTES))
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
package='org.runeclassic.port'
def adb(*args):
    return subprocess.run(['adb','-s',a.device,*args],check=True,capture_output=True,timeout=30).stdout
results=[]
for name in a.maps:
    out=a.output/name;out.mkdir(exist_ok=True)
    actor,url=ROUTES[name];log='';error=''
    adb('shell','am','force-stop',package)
    adb('shell','run-as',package,'rm','-f','files/native-run.log')
    adb('shell','am','start','-n',package+'/.MainActivity','--es','map',name+'.run',
        '--es','saveTest','campaign-route-'+name,'--es','travelExit',actor,'--es','travelURL',url,
        '--ei','msaa','2','--ez','upscale','false','--ez','enhanced','false','--ez','cameraProbe','true','--ei','seed','1')
    deadline=time.monotonic()+190
    try:
        while time.monotonic()<deadline:
            time.sleep(.5)
            try: log=adb('shell','run-as',package,'cat','files/native-run.log').decode()
            except subprocess.CalledProcessError: continue
            if re.search(r'LAUNCH_FAILED|Script error:|Unknown native|RUNE_SAVE_PROBE_FAIL',log):
                error='runtime or assertion failure';break
            if 'RUNE_ROUTE_FLOW_OK' in log:break
        else:error='route timeout'
        if not error:
            assert 'size=2520x1680 scale=1.000000 samples=4' in log,'native4x not active'
            subprocess.run([sys.executable,str(Path(__file__).with_name('android_present.py')),
                '--device',a.device,'--samples','20','--output',str(out/'native-fps')],check=True,stdout=subprocess.DEVNULL)
            log=adb('shell','run-as',package,'cat','files/native-run.log').decode()
            assert not re.search(r'LAUNCH_FAILED|Script error:|Unknown native|RUNE_SAVE_PROBE_FAIL',log),'runtime error during observation'
            (out/'screen.png').write_bytes(adb('exec-out','screencap','-p'))
    except Exception as e:error=str(e)
    (out/'engine.log').write_text(log)
    gated='original exit absent/disabled:' in log
    result={'status':'gated_or_missing_exit' if gated else ('failed' if error else 'passed'),'source':name,'destination':url.split('/')[0],'exit':actor,'passed':not error,'error':error,
        'roundtrips':re.findall(r'RUNE_SAVE_ROUNDTRIP_OK[^\n]*',log),
        'travel':re.findall(r'RUNE_ROUTE_(?:TRAVEL|FLOW)_OK[^\n]*',log),
        'native_4x':'size=2520x1680 scale=1.000000 samples=4' in log,
        'fps':json.loads((out/'native-fps.json').read_text()) if (out/'native-fps.json').exists() and not error else None,
        'coverage':'direct original exit event and save continuity; not puzzle or traversal completion'}
    results.append(result);(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(result),flush=True)
    if error and not gated:break
assert all(r['passed'] or r['status']=='gated_or_missing_exit' for r in results),'Route fixture failed; see retained evidence'
