"""Real lift scripts + bot inputs in independent server/client engine processes.

Uses actual actor/snapshot/input codecs over bounded local file exchange; this
isolates replicated geometry from secure-socket timing tests. Own game files are
required. Original assets/saves are not edited; each role has isolated state.
"""
import argparse, hashlib, json, os, re, subprocess, time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('binary','game','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args()
for key in ('binary','game','output'):setattr(a,key,getattr(a,key).resolve())
a.output.mkdir(parents=True,exist_ok=False);rows=[]
cases=[('stand-up','DM-LostCrypt','Mover8',0,False),('stand-tall','DM-LostCrypt','Mover10',0,False),('stand-down','DM-LostCrypt','Mover8',1,True),('stand-wait','DM-LostCrypt','Mover8',0,True),('bump','AR-Cathedral','Mover5',0,False),('relay','HB-Aqueduct','Mover0',0,False)]
for name,map_name,lift,stop,away in cases:
 out=a.output/name;out.mkdir();exchange=out/'exchange';exchange.mkdir();processes=[];logs=[];commands=[];started=time.monotonic();error=None;codes=[]
 try:
  for role in ('server','client'):
   folder=out/role;folder.mkdir();cmd=[str(a.binary),'--rune-network-probe','--rune-net-native-combat','--rune-bot-traversal-probe','--traversal-process='+role,'--traversal-exchange='+str(exchange),'--traversal-map='+map_name+'.run','--traversal-lift='+lift,'--traversal-from='+str(stop),*(['--traversal-away']if away else []),'--save-directory='+str(folder/'saves'),str(a.game)];commands.append(cmd);f=(folder/'game.log').open('w');logs.append(f);processes.append(subprocess.Popen(cmd,stdout=f,stderr=subprocess.STDOUT,env=dict(os.environ,ALSOFT_DRIVERS='null',SURREALENGINE_CONFIG_DIR=str(folder/'config'))))
  codes=[proc.wait(timeout=100)for proc in processes]
 except subprocess.TimeoutExpired:error='100-second child deadline'
 finally:
  for proc in processes:
   if proc.poll()is None:proc.terminate();proc.wait(timeout=5)
  for f in logs:f.close()
 texts=[(out/role/'game.log').read_text(errors='replace')for role in ('server','client')]
 row={'name':name,'map':map_name,'lift':lift,'passed':codes==[0,0]and'BOT_LIFT_PROCESS_SERVER_OK'in texts[0]and'BOT_LIFT_PROCESS_CLIENT_OK'in texts[1],'codes':codes,'error':error,'seconds':time.monotonic()-started,'commands':commands,'markers':[[l for l in text.splitlines()if'PROCESS_'in l or'LIFT_SERVER_COMPLETE'in l]for text in texts]};rows.append(row)
 (a.output/'result.json').write_text(json.dumps({'passed':all(x['passed']for x in rows),'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest(),'scope':'independent engine worlds, 60Hz authoritative physics, 30Hz encoded snapshots, 10Hz bot decisions; local file exchange, no socket/latency claim','cases':rows},indent=2)+'\n');print(name,row['passed'],flush=True)
 if not row['passed']:raise SystemExit(1)
