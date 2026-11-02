"""Mac-only original roster join, respawn and travel regression; isolated files.

Uses four authenticated headless clients at a time, all on DM-Hildir. This is
network lifecycle coverage, not rendering or campaign playthrough evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--game',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--choices',nargs='*',help='Optional character:skin subset')
a=p.parse_args()
a.binary=a.binary.resolve();a.game=a.game.resolve();a.output=a.output.resolve()
a.output.mkdir(parents=True,exist_ok=False)
header=Path(__file__).resolve().parents[2]/'Network/include/openrune/CharacterRoster.h'
roster=re.findall(r'\{"([^\"]+)", "([^\"]+)", (\d+)\}',header.read_text())
assert len(roster)==36
choices=[tuple(map(int,c.split(':'))) for c in a.choices] if a.choices else [(i,s) for i,r in enumerate(roster) for s in range(int(r[2]))]
binary_hash=hashlib.sha256(a.binary.read_bytes()).hexdigest()
results=[]
for batch in range(0,len(choices),4):
    assert hashlib.sha256(a.binary.read_bytes()).hexdigest()==binary_hash,"binary changed during validation"
    selected=choices[batch:batch+4]
    if len(selected)==1:selected=selected+[(0,0)]
    out=a.output/f'batch-{batch//4+1:02d}';out.mkdir()
    processes=[];logs=[]
    result={'choices':selected,'passed':False}
    with tempfile.TemporaryDirectory(prefix='open-rune-roster-') as temp:
        root=Path(temp)
        def launch(name,options):
            saves=root/name/'saves';saves.mkdir(parents=True)
            config=root/name/'config';(config/'SurrealEngine').mkdir(parents=True)
            log=(out/(name+'.log')).open('w');logs.append(log)
            process=subprocess.Popen([str(a.binary),'--direct',f'--save-directory={saves}',*options,str(a.game)],stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(config)))
            processes.append(process);return process
        try:
            server=launch('server',[f'--rune-server={root/"invitations"}',f'--rune-net-players={len(selected)}','--rune-net-seconds=29','--rune-net-time-limit=8','--rune-net-intermission=2','--rune-net-maps=DM-Hildir','--rune-net-frag-limit=0','--rune-net-character-fixture'])
            until=time.monotonic()+30
            while not (root/'invitations'/f'player{len(selected)}.rune-invite').is_file():
                if server.poll() is not None or time.monotonic()>until:raise RuntimeError('host failed before invitations')
                time.sleep(.1)
            for i,(character,skin) in enumerate(selected,1):
                launch(f'client{i}',[f'--rune-join={root/"invitations"/f"player{i}.rune-invite"}','--rune-net-bot','--rune-net-seconds=24','--rune-net-bot-stress',f'--character={character}',f'--character-skin={skin}'])
            for process in processes[1:]:
                if process.wait(timeout=45)!=0:raise RuntimeError('client failed')
            if server.wait(timeout=15)!=0:raise RuntimeError('server failed')
            server_text=(out/'server.log').read_text()
            for i,(character,skin) in enumerate(selected,1):
                text=(out/f'client{i}.log').read_text()
                assert 'OPEN_RUNE_BOT_OK' in text and 'OPEN_RUNE_BOT_TRAVEL' in text
                assert f'OPEN_RUNE_CHARACTER_VERIFIED class={roster[character][0].split(".")[1]} skin={skin}' in text
                assert f'OPEN_RUNE_CHARACTER_RESPAWN player={i} character={character} skin={skin}' in server_text
            result['passed']=True
        except Exception as error:
            result['error']=str(error)
        finally:
            for process in processes:
                if process.poll() is None:process.terminate()
            for process in processes:
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait()
            for log in logs:log.close()
    results.append(result)
    report={'binary_sha256':binary_hash,'batches':results,'passed':all(r['passed'] for r in results)}
    (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(result),flush=True)
if not report['passed']:raise SystemExit(1)
