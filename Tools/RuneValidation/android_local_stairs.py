"""Bounded native/4x OPPO offline stair comparison; isolated saves, no live host.

Restarts Open Rune and finishes at its normal title screen. Does not load or
write normal saves. Requires the localStairProbe development intent.
"""
import argparse,csv,io,json,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
adb=['adb','-s',a.device];pkg='org.runeclassic.port'
def call(*args):return subprocess.check_output(adb+list(args),timeout=45)
def shell(*args):return call('shell',*args).decode(errors='replace')
def saves():return sorted(call('exec-out','run-as',pkg,'find','files/game/Save','-type','f','-exec','sha256sum','{}','+').splitlines())
original=saves();prefs={n:call('exec-out','run-as',pkg,'cat','shared_prefs/'+n) for n in ['MainActivity.xml','multiplayer.xml']}
for n,b in prefs.items():(a.output/('before-'+n)).write_bytes(b)
result={'passed':False,'runs':{}};runs={}
try:
 for name,enabled in [('original',False),('fixed',True)]:
  folder=a.output/name;folder.mkdir();shell('am','force-stop',pkg)
  shell('run-as',pkg,'rm','-f','files/native-run.log','files/local-stairs.csv')
  args=['am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000',
      '--es','map','DM-Hildir.run','--es','saveTest','local-stairs-'+name,
      '--es','vulkanDriver','system','--ei','surfacePercent','100','--ei','msaa','2',
      '--ei','frames','1100','--ei','fps','120','--ei','probeTickHz','120','--ei','seed','1',
      '--ei','worldFeatures','4864','--ei','shadowLights','3','--ei','texturePack','0',
      '--ez','localStairProbe','true','--ez','localStairSmoothing',str(enabled).lower()]
  (folder/'args.json').write_text(json.dumps(args,indent=2)+'\n');shell(*args)
  deadline=time.monotonic()+150;log=''
  while time.monotonic()<deadline:
   time.sleep(1)
   try:log=call('exec-out','run-as',pkg,'cat','files/native-run.log').decode(errors='replace')
   except subprocess.CalledProcessError:continue
   if 'BOUNDED_RUN_COMPLETE' in log or 'ANDROID_LAUNCH_FAILED' in log:break
  (folder/'game.log').write_text(log)
  assert 'BOUNDED_RUN_COMPLETE' in log and 'size=2520x1680 scale=1.000000 samples=4' in log,name+' failed/native settings missing'
  assert not any(x in log for x in ['ANDROID_LAUNCH_FAILED','Script error:','Unknown native']),name+' runtime error'
  data=call('exec-out','run-as',pkg,'cat','files/local-stairs.csv');(folder/'movement.csv').write_bytes(data)
  rows=list(csv.DictReader(io.StringIO(data.decode())));runs[name]=rows;assert len(rows)>1000
  result['runs'][name]={}
  for direction,begin,end in [('ascent',200,420),('descent',680,900)]:
   section=[r for r in rows if begin<int(r['frame'])<=end]
   z=[float(r['z']) for r in section];relative=[float(r['visual_z'])-float(r['camera_z']) for r in section]
   metrics={'height_range':max(z)-min(z),'max_physical_step':max(abs(b-a) for a,b in zip(z,z[1:])),
       'max_body_camera_jolt':max(abs(b-a) for a,b in zip(relative,relative[1:])),
       'max_offset':max(abs(float(r['offset'])) for r in section)}
   result['runs'][name][direction]=metrics;assert metrics['height_range']>100
   if enabled:assert metrics['max_body_camera_jolt']<2,metrics
  for frame in [270,760]:
   (folder/f'frame-{frame}.ppm').write_bytes(call('exec-out','run-as',pkg,'cat',f'files/local-stairs-{frame}.ppm'))
  print(name,result['runs'][name],flush=True)
 assert len(runs['original'])==len(runs['fixed'])
 for old,new in zip(runs['original'],runs['fixed']):
  for key in ['frame','x','y','z','camera_z','anchor_z','grounded']:assert old[key]==new[key],(key,old,new)
 result['simulation_and_camera_identical']=True;result['passed']=True
finally:
 shell('am','force-stop',pkg)
 result['preferences_unchanged']=all(call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in prefs.items())
 if not result['preferences_unchanged']:
  for n,b in prefs.items():
   file=a.output/('restore-'+n);file.write_bytes(b);tmp='/data/local/tmp/rune-stairs-'+n
   call('push',str(file),tmp);shell('run-as',pkg,'cp',tmp,'shared_prefs/'+n);shell('rm',tmp)
  result['preferences_restored']=all(call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in prefs.items())
 result['normal_saves_unchanged']=saves()==original
 shell('am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000')
 result['normal_title_restored']=True
 (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
assert result['passed'] and result['normal_saves_unchanged']
print(json.dumps(result,indent=2),flush=True)
