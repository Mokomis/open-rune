"""Isolated Cinematic/Chaos physics, pose replication and real-save checks.

Runs the actual native engine against the user's existing game installation;
never touches its normal save slots. No public host or tablet is started.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--game',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--maps',nargs='+',default=['DM-Hildir.run'])
a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
a.binary=a.binary.resolve();a.game=a.game.resolve();cases=[]
for name,flags in [(f'{Path(m).stem}-{mode}',['--probe-map='+m,'--rune-physics='+str(mode)]) for m in a.maps for mode in (1,2)]+[(f'interaction-{mode}',['--rune-physics-interaction-probe','--rune-physics='+str(mode)]) for mode in (1,2)]+[(f'interaction-campaign-{mode}',['--rune-physics-interaction-probe','--rune-physics-campaign','--rune-physics='+str(mode)]) for mode in (1,2)]+[(f'environment-{kind}-{mode}',['--rune-physics-environment-probe','--rune-physics='+str(mode)]+(['--rune-physics-campaign'] if kind=='campaign' else [])) for kind in ('multi','campaign') for mode in (1,2)]+[(f'death-{kind}-{mode}',['--rune-physics-death-probe','--rune-physics='+str(max(1,mode))]+(['--rune-physics-campaign'] if kind=='campaign' else [])+(['--rune-physics-death-original'] if mode==0 else [])) for kind in ('multi','campaign') for mode in (0,1,2)]+[(f'kick-{kind}-{mode}',['--rune-physics-kick-probe','--rune-physics='+str(mode)]+(['--rune-physics-campaign'] if kind=='campaign' else [])) for kind in ('multi','campaign') for mode in (0,1,2)]+[('save',['--rune-physics-save-probe'])]:
    folder=a.output/name;folder.mkdir();args=[str(a.binary),'--rune-probe','--rune-physics-probe',*flags,'--save-directory='+str(folder/'saves'),str(a.game)]
    start=time.monotonic()
    with(folder/'game.log').open('w')as log:
        result=subprocess.run(args,env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(folder/'config'),ALSOFT_DRIVERS='null'),stdout=log,stderr=subprocess.STDOUT,timeout=180)
    text=(folder/'game.log').read_text(errors='replace');marker='RUNE_PHYSICS_SAVE_OK' if name=='save' else 'RUNE_PHYSICS_KICK_OK' if name.startswith('kick-') else 'RUNE_PHYSICS_INTERACTION_OK' if name.startswith('interaction-') else 'RUNE_PHYSICS_ENVIRONMENT_OK' if name.startswith('environment-') else 'RUNE_PHYSICS_DEATH_OK' if name.startswith('death-') else 'RUNE_PHYSICS_PROBE_OK'
    item=dict(name=name,passed=result.returncode==0 and marker in text,returncode=result.returncode,seconds=time.monotonic()-start,command=args,markers=[x for x in text.splitlines()if x.startswith((marker,'RUNE_PHYSICS_PROP_THROW','RUNE_KICK_OUTCOMES'))]);cases.append(item)
    report=dict(passed=all(c['passed']for c in cases),binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),cases=cases)
    (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(item),flush=True)
    if not item['passed']:
        print('\n'.join(text.splitlines()[-12:]));raise SystemExit(1)
