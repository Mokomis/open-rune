"""Real-map native navigation guards, incoming-link and pursuit regressions.

Requires owned assets. All fixtures use isolated saves and never edit map files.
LostCrypt checks a lift barrier rather than inventing a walk across floors.
"""
import argparse, hashlib, json, os, subprocess, time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('binary','game','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.binary=a.binary.resolve();a.game=a.game.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
cases=[('guards','DM-Hildir',['--navigation-safety'],'RUNE_NAV_SAFETY_OK cases=6 arrays_preserved=1')]
for m,n in [('AR-BladePit',4),('AR-Cathedral',13),('AR-Theatre',9),('DM-Sanctum',2),('DM-Team_ships',7)]:
 cases.append((m+'-incoming',m,['--navigation-integrity','--navigation-integrity-strict'],f'RUNE_NAV_INTEGRITY_RESULT passed={n} cases={n}'))
for m in ('DM-Alfheim','DM-Bothvar','DM-Thorstadt'):
 cases.append((m+'-pursuit',m,['--navigation-local','--navigation-all-markers'],'RUNE_NAVIGATION_RESULT passed=6 cases=6'))
cases.append(('LostCrypt-lift-barrier','DM-LostCrypt',['--navigation-all-markers','--navigation-lift-barrier'],'RUNE_NAV_LIFT_BARRIER_OK'))
rows=[]
for name,m,flags,marker in cases:
 out=a.output/name;out.mkdir();cmd=[str(a.binary),'--direct','--rune-tactics-probe=navigation','--navigation-map='+m+'.run','--probe-seed=73',*flags,'--save-directory='+str(out/'saves'),str(a.game)]
 start=time.monotonic()
 with(out/'game.log').open('w')as log:r=subprocess.run(cmd,env=dict(os.environ,ALSOFT_DRIVERS='null',SURREALENGINE_CONFIG_DIR=str(out/'config')),stdout=log,stderr=subprocess.STDOUT,timeout=150)
 text=(out/'game.log').read_text(errors='replace');row={'name':name,'passed':r.returncode==0 and marker in text,'command':cmd,'seconds':time.monotonic()-start,'markers':[l for l in text.splitlines() if l.startswith(('RUNE_NAV_','RUNE_NAVIGATION','NAV_REVERSE_CASE','NAV_PURSUIT'))]};rows.append(row)
 (a.output/'result.json').write_text(json.dumps({'passed':all(x['passed'] for x in rows),'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest(),'cases':rows},indent=2)+'\n');print(name,row['passed'],flush=True)
 if not row['passed']:raise SystemExit(1)
