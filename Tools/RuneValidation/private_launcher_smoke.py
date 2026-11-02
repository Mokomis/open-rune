"""Player-host plus direct-join smoke through the user launcher, using real windows."""
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('binary','game','scratch','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
for name in ('binary','game','scratch','output'):setattr(a,name,getattr(a,name).resolve())
a.output.mkdir(parents=True,exist_ok=False)
launcher=Path(__file__).resolve().parents[1]/'RuneMultiplayer/private_session.py'
children=[];files=[];result={'passed':False,'rendered_clients':2,'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest()}
with tempfile.TemporaryDirectory(prefix='rendered-host-',dir=a.scratch) as folder:
 root=Path(folder)
 def launch(name,options):
  log=(a.output/(name+'.log')).open('w');files.append(log)
  child=subprocess.Popen([sys.executable,str(launcher),'--binary',str(a.binary),'--game',str(a.game),'--session',str(root/name),'--seconds','65',*options],stdout=log,stderr=subprocess.STDOUT);children.append(child);return child
 try:
  host=launch('host',['--host','--players','2','--score-limit','0','--time-limit','6','--intermission','2'])
  invitation=root/'host/invitations/player2.rune-invite';end=time.monotonic()+30
  while not invitation.exists():
   if host.poll() is not None or time.monotonic()>end:raise RuntimeError('host startup')
   time.sleep(.1)
  guest=launch('guest',['--join',str(invitation)])
  for child in children:
   if child.wait(timeout=85):raise RuntimeError('launcher failure')
  for source in root.rglob('session.log'):
   (a.output/('-'.join(source.relative_to(root).parts))).write_bytes(source.read_bytes())
  for name in ('host','guest'):
   text=(root/name/'player/session.log').read_text()
   if 'OPEN_RUNE_CLIENT_JOINED' not in text or 'OPEN_RUNE_CLIENT_STOP' not in text or 'OPEN_RUNE_CLIENT_TRAVEL' not in text:raise RuntimeError('rendered client did not join, travel and stop')
  result['passed']=True
 except Exception as e:result['error']=str(e)
 finally:
  for child in children:
   if child.poll() is None:child.terminate()
  for child in children:
   try:child.wait(timeout=8)
   except subprocess.TimeoutExpired:child.kill();child.wait()
  for f in files:f.close()
  result['returncodes']=[c.returncode for c in children]
  (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result));raise SystemExit(0 if result['passed'] else 1)
