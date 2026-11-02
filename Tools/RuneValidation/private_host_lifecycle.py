"""Verify a private headless host survives an empty lobby, then reuses invites.
All state is in temporary test directories. No normal game configuration/saves.
"""
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True);p.add_argument('--game',type=Path,required=True)
p.add_argument('--scratch',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
for key in ('binary','game','scratch','output'):setattr(a,key,getattr(a,key).resolve())
a.output.mkdir(parents=True,exist_ok=False);a.scratch.mkdir(parents=True,exist_ok=True)
launcher=Path(__file__).resolve().parents[1]/'RuneMultiplayer/private_session.py'
children=[];files=[];result={'passed':False,'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest()}
with tempfile.TemporaryDirectory(prefix='empty-host-',dir=a.scratch) as folder:
 root=Path(folder)
 def launch(name,options):
  log=(a.output/(name+'.log')).open('w');files.append(log)
  child=subprocess.Popen([sys.executable,str(launcher),'--binary',str(a.binary),'--game',str(a.game),'--session',str(root/name),*options],stdout=log,stderr=subprocess.STDOUT);children.append(child);return child
 try:
  server=launch('host',['--headless','--players','2','--seconds','105','--time-limit','4','--score-limit','0','--intermission','2'])
  invites=root/'host/invitations';deadline=time.monotonic()+30
  while not (invites/'player2.rune-invite').exists():
   if server.poll() is not None or time.monotonic()>deadline:raise RuntimeError('host startup')
   time.sleep(.1)
  for round_no in (1,2):
   clients=[launch(f'round{round_no}-player{i}',['--join',str(invites/f'player{i}.rune-invite'),'--seconds','10','--client-bot']) for i in (1,2)]
   for child in clients:
    if child.wait(timeout=30):raise RuntimeError('client failed')
   if round_no==1:
    end=time.monotonic()+58
    while time.monotonic()<end:
     if server.poll() is not None:raise RuntimeError('empty host exited')
     time.sleep(.25)
  if server.wait(timeout=35):raise RuntimeError('host failed')
  for source in root.rglob('session.log'):
   target=a.output/('-'.join(source.relative_to(root).parts));target.write_bytes(source.read_bytes())
  text=(root/'host/host/session.log').read_text()
  for i in (1,2):
   if text.count(f'OPEN_RUNE_PLAYER_JOINED id={i} ')<2:raise RuntimeError('invitation slot not reused')
  if 'OPEN_RUNE_MAP_TRAVEL' not in text:raise RuntimeError('no map travel')
  if 'OPEN_RUNE_SERVER_STOP' not in text:raise RuntimeError('no clean host stop')
  result.update(passed=True,empty_lobby_seconds=58,join_rounds=2)
 except Exception as e:result['error']=str(e)
 finally:
  for child in children:
   if child.poll() is None:child.terminate()
  for child in children:
   try:child.wait(timeout=8)
   except subprocess.TimeoutExpired:child.kill();child.wait()
  for file in files:file.close()
  result['returncodes']=[child.returncode for child in children]
  (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result));raise SystemExit(0 if result['passed'] else 1)
