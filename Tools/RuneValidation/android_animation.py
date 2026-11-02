"""Bounded native4x combat presentation checks at 30/60/120 targets.

Compare the legacy sampler at 120 only. Requires the unlocked tablet. Uses an
isolated save directory and restores preferences byte-for-byte in finally.
Headless --rune-probe separately asserts pose continuity and event timing.
"""
import argparse, fcntl, subprocess, pathlib, time, re, json, shlex, xml.etree.ElementTree as ET
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[2]
out=a.output;out.mkdir(parents=True,exist_ok=True)
lock=open(out.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port';dev=a.device
def adb(*args):return subprocess.check_output(['adb','-s',dev,*args],timeout=30)
def prefs(data):
    subprocess.run(['adb','-s',dev,'shell','run-as '+pkg+' sh -c '+shlex.quote('cat > shared_prefs/MainActivity.xml')],input=data,check=True,timeout=30)
def log():return adb('shell','run-as',pkg,'cat','files/native-run.log').decode()
original=adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')
(out/'preferences-before.xml').write_bytes(original)
results=[]
try:
    for hz,smooth in [(30,1),(60,1),(120,1),(120,0)]:
        name=f'combat-{hz}-smooth{smooth}'
        adb('shell','am','force-stop',pkg)
        root=ET.fromstring(original)
        for key,value in [('menu.fps',{30:0,60:1,120:2}[hz]),('menu.pacing',1)]:
            e=next((n for n in root if n.get('name')==key),None)
            if e is None:e=ET.SubElement(root,'int',name=key)
            e.set('value',str(value))
        prefs(ET.tostring(root,encoding='utf-8',xml_declaration=True))
        adb('shell','run-as',pkg,'rm','-f','files/native-run.log')
        adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run','--es','saveTest','animation-fps','--ei','seed','1','--ez','cameraProbe','true','--ez','enhanced','false','--ez','upscale','false','--ei','msaa','2','--ei','animationSmooth',str(smooth),'--es','enemy','GoblinWarrior','--ei','fire','60','--ei','fireHold','8','--ei','fireRepeat','60','--ez','endurance','true')
        deadline=time.monotonic()+60
        while time.monotonic()<deadline:
            time.sleep(.4)
            try:s=log()
            except subprocess.CalledProcessError:continue
            assert not re.search('LAUNCH_FAILED|Script error:|Unknown native',s),s[-2000:]
            if re.search(r'RUNE_CAMERA frame=(?:[6-9]\d\d|\d{4,}) ',s):break
        else:raise RuntimeError('Warmup failed')
        assert 'size=2520x1680 scale=1.000000 samples=4' in s,'Expected native4x'
        assert f'RUNE_MENU pacing=1 target_fps={hz}' in s,'Target mismatch'
        subprocess.run(['python3',str(repo/'Tools/RuneValidation/android_present.py'),'--device',dev,'--samples','20','--output',str(out/name)],check=True,stdout=subprocess.DEVNULL)
        s=log();(out/(name+'-engine.log')).write_text(s)
        damage=[int(v) for v in re.findall(r'RUNE_ENDURANCE_ACTIVE.*enemy_damage=(\d+)',s)]
        assert damage and max(damage)>0,'No combat damage'
        (out/(name+'.png')).write_bytes(adb('exec-out','screencap','-p'))
        r=json.loads((out/(name+'.json')).read_text());r.update(target=hz,smooth=smooth,native4x=True,enemy_damage=max(damage));results.append(r)
        print(json.dumps({k:r[k] for k in ['target','smooth','presented_fps','median_ms','p95_ms','enemy_damage']}),flush=True)
finally:
    adb('shell','am','force-stop',pkg);prefs(original)
    assert adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml')==original
    (out/'fps-summary.json').write_text(json.dumps(results,indent=2))
