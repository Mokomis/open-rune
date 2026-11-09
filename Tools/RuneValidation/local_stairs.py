"""Offline Rune stair ascent/descent; isolated Mac saves/config, no live device."""
import argparse, csv, json, os, subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--game',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--captures',action='store_true')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
runs={};metrics={}
for name,enabled in [('original',0),('fixed',1)]:
    folder=out/name;folder.mkdir()
    args=[str(a.binary.resolve()),'--direct','--no-desktop-menu','--close-menu',
        '--url=DM-Hildir.run','--maxframes=1100','--render-msaa=2','--render-scale=1',
        '--render-vsync=0','--maxfps=120','--world-features=4864','--shadow-lights=3',
        '--probe-seed=1','--probe-tick-hz=120',f'--rune-local-stair-smoothing={enabled}',
        f'--save-directory={folder/"saves"}',f'--probe-local-stairs={folder/"movement.csv"}']
    if a.captures:args += [f'--probe-local-stair-captures={folder/"frame"}']
    args.append(str(a.game.resolve()));(folder/'args.json').write_text(json.dumps(args,indent=2)+'\n')
    with (folder/'game.log').open('w') as log:
        subprocess.run(args,env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(folder/'config'),ALSOFT_DRIVERS='null'),
            stdout=log,stderr=subprocess.STDOUT,timeout=120,check=True)
    assert 'BOUNDED_RUN_COMPLETE' in (folder/'game.log').read_text()
    runs[name]=rows=list(csv.DictReader((folder/'movement.csv').open()))
    assert len(rows)>1000
    metrics[name]={}
    for direction,start,end in [('ascent',200,420),('descent',680,900)]:
        section=[r for r in rows if start<int(r['frame'])<=end]
        height=[float(r['z']) for r in section]
        relative=[float(r['visual_z'])-float(r['camera_z']) for r in section]
        metrics[name][direction]={
            'height_range':max(height)-min(height),
            'max_physical_step':max(abs(b-a) for a,b in zip(height,height[1:])),
            'max_body_camera_jolt':max(abs(b-a) for a,b in zip(relative,relative[1:])),
            'max_visual_offset':max(abs(float(r['offset'])) for r in section)}
        assert metrics[name][direction]['height_range']>100
        if enabled:assert metrics[name][direction]['max_body_camera_jolt']<2,metrics

assert len(runs['original'])==len(runs['fixed'])
for original,fixed in zip(runs['original'],runs['fixed']):
    # The actual controller/collision/camera route must be unchanged.
    for key in ('frame','x','y','z','camera_z','anchor_z','grounded'):
        assert original[key]==fixed[key],(key,original,fixed)
metrics['simulation_and_camera_identical']=True
(out/'result.json').write_text(json.dumps(metrics,indent=2)+'\n')
print(json.dumps(metrics,indent=2))
