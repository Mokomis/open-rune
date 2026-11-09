"""Isolated original-map enemy navigation and obstacle recovery checks."""
import argparse,hashlib,json,os,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('binary','game','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.binary=a.binary.resolve();a.game=a.game.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
cases=[]
for name,flags,marker in [('walls',['--navigation-walls'],'RUNE_WALL_RESULT passed=12 cases=12'),('pursuit',[],'RUNE_NAVIGATION_RESULT passed=5 cases=5'),('stalled-plan',['--navigation-stall'],'RUNE_STALL_RESULT walking_intent=1 replanned=1'),('local-pursuit',['--navigation-local'],'RUNE_NAVIGATION_RESULT passed=6 cases=6'),('save',['--navigation-local','--navigation-save'],'RUNE_NAV_SAVE_OK resumed=1 marker_cleanup=1')]:
 folder=a.output/name;folder.mkdir()
 cmd=[str(a.binary),'--direct','--rune-tactics-probe=navigation','--probe-seed=73',*flags,'--save-directory='+str(folder/'saves'),str(a.game)]
 start=time.monotonic()
 with(folder/'game.log').open('w') as log:r=subprocess.run(cmd,env=dict(os.environ,ALSOFT_DRIVERS='null',SURREALENGINE_CONFIG_DIR=str(folder/'config')),stdout=log,stderr=subprocess.STDOUT,timeout=120)
 text=(folder/'game.log').read_text(errors='replace');case=dict(name=name,passed=r.returncode==0 and marker in text,command=cmd,seconds=time.monotonic()-start,markers=[x for x in text.splitlines() if x.startswith(('NAV_WALL','NAV_PURSUIT','RUNE_WALL','RUNE_NAVIGATION','RUNE_STALL'))]);cases.append(case)
 report=dict(passed=all(c['passed'] for c in cases),binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),cases=cases)
 (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(case),flush=True)
 if not case['passed']:raise SystemExit(1)
