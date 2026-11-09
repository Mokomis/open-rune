"""Installed native/4x slam integration through actual Android touch Views.

Restarts the app, uses isolated saves, and returns to the normal title screen.
Requires the explicit bounded slamTouchProbe development intent.
"""
import argparse,json,subprocess,time,re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--device',required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);adb=['adb','-s',a.device];pkg='org.runeclassic.port'
def call(*args):return subprocess.check_output(adb+list(args),timeout=45)
def shell(*args):return call('shell',*args).decode(errors='replace')
def saves():return sorted(call('exec-out','run-as',pkg,'find','files/game/Save','-type','f','-exec','sha256sum','{}','+').splitlines())
original=saves();prefs={n:call('exec-out','run-as',pkg,'cat','shared_prefs/'+n) for n in ['MainActivity.xml','multiplayer.xml']}
for n,b in prefs.items():(a.output/('before-'+n)).write_bytes(b)
result={'passed':False}
touch_output=(a.output/'touch.log').open('wb')
touch_capture=subprocess.Popen(adb+['logcat','-T','1','-s','RuneTouchProbe:I','AndroidRuntime:E','*:S'],stdout=touch_output,stderr=subprocess.STDOUT)
try:
 shell('am','force-stop',pkg);shell('run-as',pkg,'rm','-f','files/native-run.log')
 args=['am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000','--es','map','DM-Hildir.run','--es','saveTest','slam-touch-20261103',
     '--es','probeStart','PlayerStart4','--es','vulkanDriver','system','--ei','surfacePercent','100','--ei','msaa','2','--ei','fps','120',
     '--ei','frames','900','--ei','probeTickHz','120','--ei','seed','1','--ei','profile','600',
     '--ei','worldFeatures','4864','--ei','shadowLights','3','--ez','slamTouchProbe','true','--ez','slamSideView','true','--ez','traceShadowTransitions','true']
 (a.output/'args.json').write_text(json.dumps(args,indent=2)+'\n');shell(*args);deadline=time.monotonic()+150;log=''
 while time.monotonic()<deadline:
  time.sleep(1)
  try:log=call('exec-out','run-as',pkg,'cat','files/native-run.log').decode(errors='replace')
  except subprocess.CalledProcessError:continue
  if any(x in log for x in ['BOUNDED_RUN_COMPLETE','ANDROID_LAUNCH_FAILED','DIRECT_LAUNCH_FAILED']):break
 (a.output/'game.log').write_text(log)
 assert 'SLAM_TOUCH_DISPATCH jump-jump-attack' in (a.output/'touch.log').read_text(),'actual View dispatch not recorded'
 assert 'RUNE_SLAM_GESTURE_OK stages=4 heavy_hits=1' in log and 'BOUNDED_RUN_COMPLETE' in log,'slam/control/contact stages incomplete'
 assert 'RUNE_SLAM_FLIGHT_RENDER_OK' in log,'actual lift/dive/landing incomplete'
 assert 'RUNE_SLAM_LANDING_CONTACT' in log,'real landing contact was not observed'
 strengths=[float(x) for x in re.findall(r'RUNE_DYNAMIC_SHADOW[^\n]*hidden=1 strength=([0-9.]+)',log)]
 assert strengths and max(strengths)>.99,'hidden light never reached full shadow strength'
 assert 'size=2520x1680 scale=1.000000 samples=4' in log,'not OPPO native/4x'
 assert not any(x in log for x in ['ANDROID_LAUNCH_FAILED','Script error:','Unknown native']),'runtime error'
 for stage in range(4):(a.output/f'stage-{stage}.ppm').write_bytes(call('exec-out','run-as',pkg,'cat',f'files/slam-stage-{stage}.ppm'))
 result.update(passed=True,actual_touch=True,stages=4,native_resolution=[2520,1680],msaa=4,hidden_light_shadow_strength=max(strengths),flight=[l for l in log.splitlines() if l.startswith('RUNE_SLAM_FLIGHT_RENDER_OK')],profiles=[l for l in log.splitlines() if l.startswith(('RUNE_PROFILE ','RUNE_RENDER_PROFILE ','RUNE_VULKAN_PROFILE '))])
finally:
 touch_capture.terminate()
 try:touch_capture.wait(timeout=5)
 except subprocess.TimeoutExpired:touch_capture.kill();touch_capture.wait()
 touch_output.close()
 shell('am','force-stop',pkg)
 result['preferences_unchanged']=all(call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in prefs.items())
 if not result['preferences_unchanged']:
  for n,b in prefs.items():
   file=a.output/('restore-'+n);file.write_bytes(b);tmp='/data/local/tmp/rune-slam-'+n;call('push',str(file),tmp);shell('run-as',pkg,'cp',tmp,'shared_prefs/'+n);shell('rm',tmp)
  result['preferences_restored']=all(call('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in prefs.items())
 result['normal_saves_unchanged']=saves()==original
 shell('am','start','-W','-n',pkg+'/.MainActivity','-f','0x10008000');result['normal_title_restored']=True
 (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
assert result['passed'] and result['normal_saves_unchanged']
print(json.dumps(result,indent=2),flush=True)
