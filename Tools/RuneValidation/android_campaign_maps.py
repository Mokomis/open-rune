"""Native-resolution campaign entry and presentation checks, not full playthroughs.

Each map uses isolated saves. Optional --until-map observes natural progression;
it never invokes an exit, teleports the player, or skips a cinematic.
"""
import argparse,fcntl,json,re,subprocess,sys,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--maps',nargs='+',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--seconds',type=float,default=20)
p.add_argument('--samples',type=int,default=30)
p.add_argument('--until-map')
p.add_argument('--observe-running',action='store_true',help='Continue observing an already isolated run without restarting it; requires one source map')
p.add_argument('--oppo-clocks',action='store_true')
p.add_argument('--omit-hit',type=int,choices=[0,1])
p.add_argument('--resolve',type=int,choices=[0,1])
p.add_argument('--discard-hit',type=int,choices=[0,1])
p.add_argument('--msaa',type=int,choices=[0,1,2],help='Diagnostic override: 0=off, 1=2x, 2=4x; resolution stays native')
p.add_argument('--clarity',type=int,default=0,help='Explicit clarity settings; 0=original, 13=Low/16x/Low')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
assert not a.observe_running or (len(a.maps)==1 and a.until_map), 'Observation requires one source and one target map'
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
package='org.runeclassic.port'
def adb(*args):return subprocess.run(['adb','-s',a.device,*args],check=True,capture_output=True,timeout=25).stdout
results=[]
for name in a.maps:
    name=Path(name).stem
    folder=a.output/name;folder.mkdir(exist_ok=True)
    (folder/'battery-start.txt').write_bytes(adb('shell','dumpsys','battery'))
    if not a.observe_running:
        adb('shell','am','force-stop',package)
        adb('shell','run-as',package,'rm','-f','files/native-run.log')
        adb('shell','am','start','-n',package+'/.MainActivity','--es','map',name+'.run',
            '--es','saveTest','campaign-map-'+name,'--ez','upscale','false','--ez','enhanced','false',
            '--ei','seed','1','--ez','cameraProbe','true','--ei','profile','1800',
            '--ei','clarity',str(a.clarity),
            *(['--ei','msaa',str(a.msaa)] if a.msaa is not None else []),
            *(['--ei','discardHit',str(a.discard_hit)] if a.discard_hit is not None else []),
            *(['--ei','inpassResolve',str(a.resolve)] if a.resolve is not None else []),
            *(['--ei','omitHit',str(a.omit_hit)] if a.omit_hit is not None else []))
    start=time.monotonic();log='';measured=False;reached=False;error='';laststate=''
    try:
        while time.monotonic()-start<a.seconds+40:
            time.sleep(.5)
            read=subprocess.run(['adb','-s',a.device,'shell','run-as',package,'cat','files/native-run.log'],capture_output=True,timeout=25)
            if read.returncode:
                if time.monotonic()-start<30:continue
                error='native log unavailable after startup';break
            log=read.stdout.decode()
            if a.observe_running and not re.search(r'RUNE_CAMERA .*map='+re.escape(name)+r'\.run\b',log,re.I):
                error='running log does not match source map';break
            if re.search(r'LAUNCH_FAILED|Script error:|Unknown native',log):error='runtime error';break
            if re.search(r'RUNE_NEW_FIGHT|RUNE_SESSION_ACTION|RUNE_UPSCALE mode=sgsr|RUNE_VISUAL mode=enhanced',log):
                error='comparison interrupted by session or graphics change';break
            rows=re.findall(r'RUNE_CAMERA frame=(\d+) map=(\S+) state=(\S+)',log)
            if rows:
                current=rows[-1]
                state=current[1]+' '+current[2]
                if state!=laststate:print(name+': '+state,flush=True);laststate=state
                if a.until_map and any(x[1].lower().removesuffix('.run')==a.until_map.lower().removesuffix('.run') for x in rows):reached=True
                if not measured and int(current[0])>=600:
                    subprocess.run([sys.executable,str(Path(__file__).with_name('android_present.py')),'--device',a.device,
                        '--samples',str(a.samples),'--output',str(folder/'native-fps'),
                        *(['--oppo-clocks'] if a.oppo_clocks else [])],check=True,stdout=subprocess.DEVNULL)
                    measured=True
                    # Re-read state after the blocking measurement before accepting it.
                    continue
                if measured and ((a.until_map and reached) or (not a.until_map and time.monotonic()-start>=a.seconds)):break
            if time.monotonic()-start>15 and subprocess.run(['adb','-s',a.device,'shell','pidof',package],capture_output=True).returncode!=0:error='app exited';break
        else:error='progression timeout' if a.until_map else 'no rendered state'
        if measured and not error:
            (folder/'display.png').write_bytes(adb('exec-out','screencap','-p'))
    except Exception as e:error=str(e)
    finally:
        (folder/'engine.log').write_text(log)
        (folder/'battery-end.txt').write_bytes(adb('shell','dumpsys','battery'))
    result={'map':name,'passed_entry':bool(measured and not error),'observed_seconds':round(time.monotonic()-start,2),
        'compatibility_warnings':sorted(set(re.findall(r'^.*(?:Unimplemented:|RUNE_PARTICLE_UNSUPPORTED|Unknown command:).*$',log,re.M))),
        'native_4x_verified':'size=2520x1680 scale=1.000000 samples=4' in log,
        'states':sorted(set(re.findall(r'RUNE_CAMERA.*?map=(\S+) state=(\S+)',log))),
        'player_died': bool(re.search(r'RUNE_CAMERA.*?state=Dying',log)),
        'presented_fps': json.loads((folder/'native-fps.json').read_text())['presented_fps'] if measured else None,
        'natural_target':a.until_map,'reached_target':reached if a.until_map else None,'error':error,
        'observed_existing_run':a.observe_running,
        'msaa_override':a.msaa,'discard_hit_override':a.discard_hit,'inpass_resolve_override':a.resolve,'omit_hit_override':a.omit_hit,
        'clarity_settings':a.clarity,
        'coverage':'stationary entry and original scripts; not traversal or complete level validation'}
    (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n');results.append(result)
    (a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(result),flush=True)
    if error:break
assert all(x['passed_entry'] for x in results),'Campaign check failed; inspect retained evidence'
