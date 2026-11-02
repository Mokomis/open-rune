"""Native4x world-feature screenshots and display samples, with isolated saves."""
import argparse,fcntl,json,re,subprocess,sys,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--map',default='DM-Hildir');p.add_argument('--modes',type=int,nargs='+',default=[0,3,7,0])
p.add_argument('--require-materials',action='store_true');p.add_argument('--validation',action='store_true');p.add_argument('--bounded',action='store_true')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
results=[]
for i,mode in enumerate(a.modes):
    dest=a.output/f'{i}-{mode}';dest.mkdir(exist_ok=True)
    adb('shell','am','force-stop',pkg)
    adb('shell','run-as',pkg,'rm','-f','files/native-run.log','files/frame.ppm')
    extras=['--ei','frames','300'] if a.bounded else []
    if a.validation: extras+=['--ez','validation','true']
    adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map',a.map+'.run','--es','saveTest','world-features',
        '--ei','seed','1','--ei','msaa','2','--ez','upscale','false','--ez','enhanced','true','--ei','clarity','22',
        '--ei','worldFeatures',str(mode),*extras)
    deadline=time.monotonic()+90;log=''
    while time.monotonic()<deadline:
        time.sleep(1)
        log=adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
        if 'LAUNCH_FAILED' in log:break
        if a.bounded and 'BOUNDED_RUN_COMPLETE' in log:break
        if not a.bounded and 'ANDROID_RENDER_TARGET' in log:time.sleep(3);break
    if a.bounded:
        assert 'BOUNDED_RUN_COMPLETE' in log,log[-2000:]
        (dest/'frame.ppm').write_bytes(adb('exec-out','run-as',pkg,'cat','files/frame.ppm'))
        subprocess.run(['sips','-s','format','png',str(dest/'frame.ppm'),'--out',str(dest/'frame.png')],check=True,stdout=subprocess.DEVNULL)
    else:
        (dest/'screen.png').write_bytes(adb('exec-out','screencap','-p'))
        subprocess.run([sys.executable,str(Path(__file__).with_name('android_present.py')),'--device',a.device,
            '--samples','20','--output',str(dest/'fps')],check=True,stdout=subprocess.DEVNULL)
        log=adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
    (dest/'engine.log').write_text(log)
    assert 'size=2520x1680 scale=1.000000 samples=4' in log
    assert not re.search(r'LAUNCH_FAILED|Script error:|Unknown native|\[vulkan (?:error|warning)\]|\[ALSOFT\] \(EE\)',log)
    if mode&6 and a.require_materials:assert 'RUNE_MATERIAL_GENERATED' in log
    if mode&1:assert 'RUNE_AUDIO_EFX available=1' in log and 'RUNE_AUDIO_ENV enabled=1' in log
    if mode&8:assert 'RUNE_AUDIO_HRTF requested=1 active=1 reset=1' in log
    result={'map':a.map,'mode':mode,'passed':True,'bounded':a.bounded,'generated_materials':len(re.findall('RUNE_MATERIAL_GENERATED',log))}
    if not a.bounded:result['fps']=json.loads((dest/'fps.json').read_text())['presented_fps']
    results.append(result);print(json.dumps(result),flush=True)
(a.output/'result.json').write_text(json.dumps(results,indent=2)+'\n')
