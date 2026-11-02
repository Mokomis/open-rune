"""Read campaign map actors and links through the engine's headless package loader."""
import argparse,concurrent.futures,json,os,shlex,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--engine',type=Path,required=True)
p.add_argument('--game',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
maps=sorted(x for x in (a.game/'Maps').glob('*.run') if not x.name.lower().startswith(('dm-','ar-','hb-')) and x.stem.lower() not in ('entry','intro','autoplay'))
def scan(path):
 r=subprocess.run([str(a.engine.resolve()),'--rune-probe','--probe-map='+path.name,'--probe-map-inventory',str(a.game.resolve())],capture_output=True,text=True,timeout=60)
 (a.output/(path.stem+'.log')).write_text(r.stdout+r.stderr)
 assert r.returncode==0,path.name+' failed'
 actors=[]
 for line in r.stdout.splitlines():
  if line.startswith('RUNE_MAP_ACTOR '):
   row=shlex.split(line)[1:]
   actors.append(dict(zip(['name','class','tag','event','x','y','z','url','next_order','next_tag','orders','order_tag','other_tag','attach_tag','initial_state','physics','wait_for_trigger','destroyable','position','rate_modifier','pause_time','instant','spline_through','end_path','skip_next','start_speed','end_speed','draw_type','particle_type','particle_count','connection_points','system_lifespan','system_oneshot','system_ticks','event_death','event_init','event_system_tick','event_particle_tick','spewer_damage','spewer_length','spewer_radius','auto_start','msg_location'],row)))
 return {'map':path.name,'actors':len(actors),'links':[x for x in actors if x['url']],'classes':sorted(set(x['class'] for x in actors))}
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:rows=list(pool.map(scan,maps))
(a.output/'catalog.json').write_text(json.dumps(rows,indent=2)+'\n')
for row in rows:print(row['map'],row['actors'],[(x['class'],x['url']) for x in row['links']])
