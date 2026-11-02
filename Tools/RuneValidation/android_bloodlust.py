"""Isolated visible Android touch gestures for bloodlust; preserves campaign saves."""
import argparse,subprocess,time,re,json,sys
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--device',required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--present',action='store_true');args=parser.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=True);pkg='org.runeclassic.port'
def adb(*a):return subprocess.check_output(['adb','-s',args.device,*a],timeout=40)
def log():return adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode()
def shot(name):(out/(name+'.png')).write_bytes(adb('exec-out','screencap','-p'))
results={}
for gesture in ['leap','charge']:
 adb('shell','am','force-stop',pkg)
 adb('shell','run-as',pkg,'rm','-f','files/native-run.log')
 adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run','--es','saveTest','bloodlust-'+gesture,'--ez','bloodlustProbe','true','--es','bloodlustGesture',gesture,'--ez','touchProbe','true','--ez','upscale','false','--ei','msaa','2','--ez','cameraProbe','true','--ez','traceCombat','true')
 deadline=time.monotonic()+35
 while time.monotonic()<deadline:
  try:
   if 'RUNE_BLOOD_FIXTURE_READY' in log():break
  except subprocess.CalledProcessError:pass
  time.sleep(.2)
 else:raise AssertionError('No full-bloodlust fixture')
 fps=None
 if gesture=='charge' and args.present:
  fps=subprocess.Popen([sys.executable,str(Path(__file__).with_name('android_present.py')),'--device',args.device,'--samples','40','--output',str(out/'charge-present')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 shot(gesture+'-ready-a');time.sleep(.35);shot(gesture+'-ready-b')
 time.sleep(6)
 if fps:
  report,_=fps.communicate(timeout=60);(out/'charge-present-command.log').write_bytes(report);assert fps.returncode==0,report.decode()
 text=log();(out/(gesture+'-touch.log')).write_text(text);shot(gesture+'-after')
 expected='RUNE_BLOOD_LEAP actor=Ragnar' if gesture=='leap' else 'RUNE_BLOOD_CHARGE actor=Ragnar'
 assert expected in text,gesture+' gesture failed: '+text[-3000:]
 assert re.search(r'RUNE_BLOOD_STATE meter=\d+ ready=0 .*'+('leaps=1' if gesture=='leap' else 'charges=1'),text), 'No spent/cooldown state'
 results[gesture]={'passed':True,'input':'real visible Android touch view dispatch','full_meter_once':True}
(out/'touch-result.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results))
