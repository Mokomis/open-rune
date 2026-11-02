"""Test shared material/water/shadow controls, comparison and persistence.

Leaves all three new effects enabled; other saved settings must remain unchanged.
Uses an isolated title session and does not load or save campaign slots.
"""
import fcntl
import argparse,json,pathlib,re,subprocess,time,xml.etree.ElementTree as E
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True);p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);lock=open(a.output.parent/'android-case.lock','w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=30)
def tree():
 adb('shell','uiautomator','dump','/sdcard/rune-pixel-menu.xml')
 return E.fromstring(adb('exec-out','cat','/sdcard/rune-pixel-menu.xml'))
def tap(text,desc=False):
 nodes=[n for n in tree().iter('node') if n.get('content-desc' if desc else 'text')==text and n.get('enabled')=='true'];assert nodes,text
 x,y,r,b=map(int,re.findall(r'\d+',nodes[-1].get('bounds')));adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def expect(value):
 assert any(n.get('content-desc')=='Lighting mode: '+value for n in tree().iter('node')),value

def choose(value):
 tap('Lighting mode')
 nodes=[n for n in tree().iter('node') if n.get('text','').replace('   ✓','')==value and n.get('class')=='android.widget.Button'];assert nodes,value
 x,y,r,b=map(int,re.findall(r'\d+',nodes[0].get('bounds')));adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def launch():
 adb('shell','am','force-stop',pkg)
 adb('shell','am','start','-n',pkg+'/.MainActivity','--es','saveTest','pixel-menu','--ei','msaa','2','--ez','upscale','false')
 time.sleep(6);tap('Settings')
def prefs():return {n.get('name'):(n.tag,n.get('value'),n.text) for n in E.fromstring(adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml'))}
before=prefs();launch();tap('Textures, water & shadows')
def chooseFeature(name,value):
 tap(name)
 nodes=[n for n in tree().iter('node') if n.get('text','').replace('   ✓','')==value and n.get('class')=='android.widget.Button'];assert nodes,value
 x,y,r,b=map(int,re.findall(r'\d+',nodes[0].get('bounds')));adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def expectFeature(name,value):assert any(n.get('content-desc')==name+': '+value for n in tree().iter('node')),(name,value)
features=[('Material maps','Enhanced'),('Water reflections','Approximate'),('Shadow-casting lights','Three nearby')]
for name,value in features:chooseFeature(name,value);expectFeature(name,value)
(a.output/'materials.png').write_bytes(adb('exec-out','screencap','-p'))
adb('shell','input','keyevent','KEYCODE_BACK');tap('Enhancements');tap('Textures, water & shadows')
for name,value in features:expectFeature(name,value)
chooseFeature('Water reflections','Full scene');expectFeature('Water reflections','Full scene')
time.sleep(.3)
log=adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode();assert int(re.findall(r'RUNE_WORLD_FEATURES code=(\d+)',log)[-1])&10240==10240
chooseFeature('Water reflections','Original');expectFeature('Water reflections','Original')
adb('shell','input','keyevent','KEYCODE_BACK');tap('Video');tap('Textures, water & shadows');expectFeature('Water reflections','Original');chooseFeature('Water reflections','Approximate')
adb('shell','input','keyevent','KEYCODE_BACK');tap('Enhancements');tap('Compare original effects');time.sleep(.3)
log=adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode();assert re.findall(r'RUNE_WORLD_FEATURES code=(\d+)',log)[-1]=='0'
tap('Restore my effects');time.sleep(.3)
log=adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode();assert int(re.findall(r'RUNE_WORLD_FEATURES code=(\d+)',log)[-1])&7168==7168
launch();tap('Textures, water & shadows')
for name,value in features:expectFeature(name,value)
after=prefs();allowed={'menu.materialMaps','menu.water','menu.shadowLights','worldFeatures'}
assert all(after.get(k)==v for k,v in before.items() if k not in allowed)
assert int(after['worldFeatures'][1])==(int(before['worldFeatures'][1])|7168)&~8192
result={'video_enhancements_shared':True,'water_roundtrip':True,'comparison_restores':True,'restart_persists':True,'unrelated_preferences_preserved':True}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
