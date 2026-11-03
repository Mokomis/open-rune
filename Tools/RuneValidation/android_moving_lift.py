"""Compare moving-lift visibility backends at native OPPO resolution and 4x AA.

Uses bounded original lift interpolation, isolated saves and exact capture/pose
comparisons. Returns to normal LostCrypt with saved settings and save access.
"""
import argparse,json,subprocess,time,xml.etree.ElementTree as ET
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--device',required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--maps',nargs='+',default=['DM-LostCrypt.run','HelLift.run'])
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);adb=['adb','-s',a.device];pkg='org.runeclassic.port'
def call(*args):return subprocess.check_output(adb+list(args),timeout=30)
def shell(*args):return call('shell',*args).decode(errors='replace')
def saves():return sorted(call('exec-out','run-as',pkg,'find','files/game/Save','-type','f','-exec','sha256sum','{}','+').splitlines())
original=saves();prefs={n:call('exec-out','run-as',pkg,'cat','shared_prefs/'+n) for n in ['MainActivity.xml','multiplayer.xml']}
v={n.get('name'):n.get('value',n.text) for n in ET.fromstring(prefs['MainActivity.xml'])};world=int(v.get('worldFeatures',20399))|768;texture=int(v.get('menu.texturePack',0));result={'passed':False,'maps':[]}
try:
 for mapname in a.maps:
  assert mapname in ['DM-LostCrypt.run','HelLift.run']
  rows=[]
  for fast in [0,1]:
   name=mapname+'-'+str(fast);shell('am','force-stop',pkg);shell('run-as',pkg,'rm','-f','files/native-run.log','files/frame.ppm')
   shell('am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000','--es','map',mapname,
    '--es','saveTest','moving-lift-'+mapname[:-4]+'-'+str(fast),'--ei','surfacePercent','100','--ei','msaa','2',
    '--ei','seed','1','--ei','probeTickHz','120','--ei','frames','360','--ei','profile','240','--ei','fps','120',
    '--ei','worldFeatures',str(world),'--ei','texturePack',str(texture),'--es','vulkanDriver','system',
    '--ei','shadowLights','1','--ei','fastWorldVisibility',str(fast),'--es','liftProbe','auto')
   deadline=time.monotonic()+120;log=''
   while time.monotonic()<deadline:
    time.sleep(1)
    try:log=call('exec-out','run-as',pkg,'cat','files/native-run.log').decode(errors='replace')
    except subprocess.CalledProcessError:continue
    if 'BOUNDED_RUN_COMPLETE' in log or 'ANDROID_LAUNCH_FAILED' in log:break
   (a.output/(name+'.log')).write_text(log)
   assert 'BOUNDED_RUN_COMPLETE' in log and 'RUNE_LIFT_FIXTURE_OK' in log,name+' failed'
   assert 'size=2520x1680 scale=1.000000 samples=4' in log,'Not native OPPO/4x'
   with (a.output/(name+'.ppm')).open('wb') as f:subprocess.run(adb+['exec-out','run-as',pkg,'cat','files/frame.ppm'],stdout=f,check=True,timeout=30)
   row={'map':mapname,'fast':fast,'profiles':[l for l in log.splitlines() if l.startswith(('RUNE_PROFILE ','RUNE_RENDER_PROFILE ','RUNE_VULKAN_PROFILE '))],'motion':[l for l in log.splitlines() if l.startswith(('RUNE_LIFT_FIXTURE ','RUNE_LIFT_FRAME ','RUNE_LIFT_FIXTURE_OK'))]};rows.append(row)
   print(mapname,fast,*row['profiles'],flush=True)
  captures=(a.output/(mapname+'-0.ppm')).read_bytes()==(a.output/(mapname+'-1.ppm')).read_bytes()
  motion=rows[0]['motion']==rows[1]['motion'];assert captures and motion,'Moving lift captures/poses differ: '+mapname
  result['maps'].append({'map':mapname,'cases':rows,'captures_byte_identical':captures,'motion_matches':motion});(a.output/'result.json').write_text(json.dumps(result,indent=2))
 result['passed']=True
finally:
 shell('am','force-stop',pkg)
 result['preferences_unchanged']=all(call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in prefs.items())
 if not result['preferences_unchanged']:
  for n,b in prefs.items():
   f=a.output/('restore-'+n);f.write_bytes(b);tmp='/data/local/tmp/rune-lift-'+n;call('push',str(f),tmp);shell('run-as',pkg,'cp',tmp,'shared_prefs/'+n);shell('rm',tmp)
  result['preferences_restored']=all(call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in prefs.items())
 result['save_hashes_unchanged']=saves()==original
 shell('am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000','--es','map','DM-LostCrypt.run','--ei','seed','1')
 result['normal_save_access_restored']=True;(a.output/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
assert result['passed'] and result['save_hashes_unchanged']
