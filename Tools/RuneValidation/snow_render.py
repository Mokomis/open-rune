"""Compare snow modes in an isolated native-resolution, 4x AA Mac view. No tablet access."""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True);p.add_argument('--game',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--map',default='RagnarVillage');p.add_argument('--view',default='1112,1063,-180,16384,0')
p.add_argument('--frames',type=int,default=960);p.add_argument('--repeats',type=int,default=2)
p.add_argument('--presets',nargs='+',choices=['off','light','heavy','blizzard'],default=['off','light','heavy'])
a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False);results=[]
for repeat in range(a.repeats):
 for preset in (a.presets if repeat%2==0 else list(reversed(a.presets))):
  mode={"off":0,"light":1,"heavy":2,"blizzard":2}[preset]
  root=a.output/f'{repeat}-{preset}';(root/'saves').mkdir(parents=True);(root/'config'/'SurrealEngine').mkdir(parents=True)
  cmd=[str(a.binary.resolve()),'--direct',f'--url={a.map}.run','--close-menu',f'--save-directory={root/"saves"}',f'--probe-view={a.view}',f'--maxframes={a.frames}',f'--profile-frames={a.frames-60}','--probe-tick-hz=60','--probe-seed=1','--render-msaa=2','--render-scale=1','--render-vsync=0',f'--snow={mode}','--snow-profile',f'--capture-frame={root/"frame.ppm"}',str(a.game.resolve())]
  if preset=='blizzard':cmd.insert(-1,'--snow-blizzard')
  with (root/'run.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(root/'config')),check=True,timeout=120)
  text=(root/'run.log').read_text();assert 'DIRECT_LAUNCH_FAILED' not in text and (root/'frame.ppm').exists(),text[-2000:]
  def values(prefix):
   rows=[dict((k,float(v)) for k,v in re.findall(r'(\w+)=([\d.]+)',line)) for line in text.splitlines() if line.startswith(prefix)]
   return rows
  profile=values('RUNE_PROFILE ');assert profile,'missing frame profile'
  item={'preset':preset,'repeat':repeat,'mode':mode,'frame':profile[-1],'gpu':values('RUNE_VULKAN_PROFILE '),'snow':values('RUNE_SNOW ')}
  results.append(item);(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(item),flush=True)
