"""Rebuild the private multiplayer coverage ledger from retained passing runs."""
import csv
import json
import re
from pathlib import Path
root=Path(__file__).resolve().parents[2]
evidence=root/'Docs/RunePort/evidence/private-multiplayer'
runs=[('dm','default','rotation-dm'),('tdm','2 teams','rotation-tdm'),('tdm','3 teams','rotation-tdm-3-supported'),('tdm','4 teams','rotation-tdm-4-supported'),('arena','1v1','arena-final-all'),('arena','2v2','arena-2v2-fixed'),('arena','3v3','arena-3v3-all'),('arena','4v4','arena-4v4-all'),('headball','2 teams; limbs off','headball-complete'),('headball','2 teams; limbs on','headball-complete-limbs')]
for teams in (3,4):
 for limbs in (0,1):runs.append(('headball',f'{teams} teams; limbs '+('on' if limbs else 'off'),f'headball-{teams}-teams-limbs-{limbs}'))
rows=[]
for mode,variant,name in runs:
 path=evidence/name/'match';result=json.loads((path/'result.json').read_text())
 assert result['passed'],name
 text=(path/'server.log').read_text();sections=text.split('OPEN_RUNE_MAP_CHECK ')[1:]
 required={'dm':['OPEN_RUNE_TEST_DEATH'],'tdm':['OPEN_RUNE_TEST_DEATH'],'arena':['OPEN_RUNE_ARENA_QUEUED','OPEN_RUNE_ARENA_DEFEAT'],'headball':['OPEN_RUNE_HEADBALL_STARTED','OPEN_RUNE_HEADBALL_GOAL','OPEN_RUNE_HEADBALL_LIMB','OPEN_RUNE_HEADBALL_KILL']}[mode]
 for map_name in result['maps']:
  matching=[s for s in sections if s.startswith('map='+map_name+' ')]
  complete=any('OPEN_RUNE_MATCH_STARTED' in s and 'OPEN_RUNE_MATCH_ENDED' in s and all(m in s for m in required) for s in matching)
  assert complete,(name,map_name,'objective/match evidence')
  clients=[]
  for i in range(1,result['players']+1):
   client=(path/f'client{i}.log').read_text()
   assert f'map={map_name} entities=' in client,(name,map_name,i)
   assert 'OPEN_RUNE_BOT_TRAVEL' in client,(name,i,'travel')
  rows.append(dict(mode=mode,variant=variant,map=map_name,players=result['players'],load_spawn=True,objectives=True,match_complete=True,client_travel=True,evidence=f'{name}/match/result.json',binary_sha256=result['binary_sha256']))
with (evidence/'map-variant-matrix.csv').open('w',newline='') as f:
 writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
summary=dict(unique_maps=len({r['map'] for r in rows}),map_variant_combinations=len(rows),rulesets=len(runs),passed=True,coverage='Automated original-script objective fixtures and authenticated headless clients; not human playthroughs.',build_policy='Matrix accumulated across milestone builds. Each row records the exact executable hash. Final network/lifecycle regression runs are separate.')
(evidence/'coverage-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary)

# Keep whole-session metrics separate from active-play metrics. A loading barrier
# or intermission is not a missing combat update. Older runs lack active counters.
capacity=[]
for name,seconds in [('capacity-compressed-16-clean',90),('capacity-arena-16',80),('capacity-headball-16',115),('capacity-final-dm-16',100),('capacity-final-tdm-16',100),('active-metrics-tdm-16',80),('active-metrics-tdm-8',80),('active-metrics-tdm-4',80),('active-metrics-dm-16',80)]:
 path=evidence/name/'match';file=path/'result.json'
 if not file.exists():continue
 result=json.loads(file.read_text());assert result['passed'],name
 counts=[];gaps=[];bins=[0]*8;active=[0,0,0];active_bins=[0]*8;joins=[]
 for log in path.glob('client*.log'):
  text=log.read_text();match=re.search(r'BOT_OK worlds=(\d+) max_world_gap_ms=(\d+) gap_buckets=([\d,]+)',text)
  assert match,(name,log.name)
  counts.append(int(match[1]));gaps.append(int(match[2]))
  for i,v in enumerate(match[3].rstrip(',').split(',')):bins[i]+=int(v)
  first=re.search(r'BOT_JOINED.*join_ms=(\d+)',text)
  if first:joins.append(int(first[1]))
  match=re.search(r'active_updates=(\d+) active_ms=(\d+) active_max_gap_ms=(\d+) active_gap_buckets=([\d,]+)',text)
  if match:
   active[0]+=int(match[1]);active[1]+=int(match[2]);active[2]=max(active[2],int(match[3]))
   for i,v in enumerate(match[4].rstrip(',').split(',')):active_bins[i]+=int(v)
 row=dict(run=name,mode=result['mode'],players=result['players'],passed=True,impaired=bool(result['links']),client_seconds=seconds,binary_sha256=result['binary_sha256'],whole_session_updates_per_second=round(sum(counts)/len(counts)/seconds,2),whole_session_max_gap_ms=max(gaps),initial_join_range_ms=[min(joins),max(joins)],gap_buckets=bins)
 if active[1]:row.update(active_updates_per_second=round(active[0]*1000/active[1],2),active_max_gap_ms=active[2],active_gap_buckets=active_bins,active_percent_gaps_under_250ms=round(sum(active_bins[:2])/sum(active_bins)*100,2))
 capacity.append(row)
(evidence/'capacity-summary.json').write_text(json.dumps(capacity,indent=2)+'\n')
print('Capacity rows:',len(capacity))
