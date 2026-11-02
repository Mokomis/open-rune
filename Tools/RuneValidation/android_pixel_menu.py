"""Test the shared lighting-mode controls, comparison and process persistence.

Leaves Remastered lighting enabled; other saved settings must remain unchanged.
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
before=prefs();launch();choose('Remastered');expect('Remastered')
(a.output/'video.png').write_bytes(adb('exec-out','screencap','-p'))
tap('Enhancements');expect('Remastered');choose('Original');expect('Original');tap('Video');expect('Original')
choose('Remastered');tap('Enhancements');tap('Compare original effects')
log=adb('shell','run-as',pkg,'cat','files/native-run.log').decode();assert re.findall(r'RUNE_WORLD_FEATURES code=(\d+)',log)[-1]=='0'
tap('Restore my effects');expect('Remastered')
log=adb('shell','run-as',pkg,'cat','files/native-run.log').decode();assert int(re.findall(r'RUNE_WORLD_FEATURES code=(\d+)',log)[-1])&768==768
(a.output/'enhancements.png').write_bytes(adb('exec-out','screencap','-p'))
launch();expect('Remastered');tap('Enhancements');expect('Remastered')
after=prefs();assert all(after.get(k)==v for k,v in before.items() if k not in ('menu.pixelLighting','worldFeatures'))
old=int(before.get('worldFeatures',('int','1',None))[1]);assert int(after['worldFeatures'][1])==(old&~768)|768
result={'shared_video_enhancements':True,'original_roundtrip':True,'comparison_restores_saved_choice':True,'restart_persists':True,'unrelated_preferences_preserved':True,'enabled_at_end':True}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
