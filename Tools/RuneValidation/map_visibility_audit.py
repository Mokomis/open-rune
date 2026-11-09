"""Audit every installed map's static render bounds without playing through it.

Runs the native isolated bounds fixture; no device, GUI, live host or LLM needed.
Outputs per-map logs plus a compact JSON report for later review.
"""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True);p.add_argument('--game',type=Path,required=True)
p.add_argument('--output',type=Path,required=True);p.add_argument('--maps',nargs='+')
a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
maps=sorted((a.game/'Maps').glob('*.run'))
if a.maps:maps=[m for m in maps if m.name in a.maps or m.stem in a.maps]
if not maps:p.error('No map files selected')
rows=[]
for m in maps:
 d=a.output/m.stem;d.mkdir(exist_ok=True)
 args=[str(a.binary.resolve()),'--rune-network-probe','--rune-shadow-culling-probe='+m.name,
     '--visibility-bound-audit','--visibility-audit-only','--save-directory='+str(d/'saves'),str(a.game.resolve())]
 (d/'args.json').write_text(json.dumps(args,indent=2)+'\n')
 with (d/'audit.log').open('w') as log:
  try:rc=subprocess.run(args,env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(d/'config')),
      stdout=log,stderr=subprocess.STDOUT,timeout=60).returncode
  except subprocess.TimeoutExpired:rc='timeout'
 text=(d/'audit.log').read_text(errors='replace')
 match=re.search(r'STATIC_BOUNDS_AUDIT map=(\S+) checked=(\d+) bad=(\d+) fixed_bad=(\d+)',text)
 row={'map':m.name,'rc':rc,'passed':bool(rc==0 and match and int(match[4])==0)}
 if match:row.update(checked=int(match[2]),original_mismatches=int(match[3]),corrected_mismatches=int(match[4]))
 else:row['error_tail']=text[-800:]
 rows.append(row);(a.output/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
 print(m.name,'PASS' if row['passed'] else 'FAIL',flush=True)
summary={'maps':len(rows),'passed':sum(r['passed'] for r in rows),'original_mismatch_maps':sum(r.get('original_mismatches',0)>0 for r in rows)}
(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
raise SystemExit(0 if summary['passed']==len(rows) else 1)
