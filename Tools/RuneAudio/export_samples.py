"""Run on the processing host. Export owned sound packages without playing the game."""
import argparse, hashlib, json, os, platform, subprocess
from pathlib import Path


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda:stream.read(1048576),b''):h.update(data)
    return h.hexdigest()


def main():
    p=argparse.ArgumentParser();p.add_argument('--engine',type=Path,required=True);p.add_argument('--game',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--packages',nargs='+',default=['WeaponsSnd','FootstepsSnd','EnvironmentalSnd','CreaturesSnd','OtherSnd']);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True)
    if os.name=='nt':
        import ctypes
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(),0x4000)
    packages=a.packages
    if any(not name.replace('_','').isalnum() for name in packages):raise ValueError('Invalid package name')
    def snapshot():
        watched=[a.game/'Sounds'/(name+'.uax') for name in packages]
        watched += [p for folder in ['Save','System'] for p in (a.game/folder).glob('*') if p.is_file() and (folder=='Save' or p.suffix.lower()=='.ini')]
        return {str(p):sha(p) for p in watched}
    before=snapshot()
    result={'host':platform.node(),'platform':platform.platform(),'engine_sha256':sha(a.engine),'before':before,'packages':{}}
    (a.output/'source-before.json').write_text(json.dumps(result,indent=2))
    env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str((a.output/'config').resolve()))
    for name in packages:
        target=a.output/'exports'/name
        if not (target/'manifest.json').exists():
            with (a.output/(name+'.log')).open('wb') as log:
                subprocess.run([str(a.engine.resolve()),'--rune-export-sounds','--sound-package='+name,'--sound-output='+str(target.resolve()),'--save-directory='+str((a.output/'saves').resolve()),str(a.game.resolve())],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
        manifest=json.loads((target/'manifest.json').read_text())
        for row in manifest['sounds']:
            for field in ['raw_file','decoded_file']:
                if field in row:row[field+'_sha256']=sha(target/row[field])
        manifest['source_sha256']=before[str(a.game/'Sounds'/(name+'.uax'))]
        (target/'manifest.json').write_text(json.dumps(manifest,indent=2))
        result['packages'][name]={'exported':sum(r['status']=='exported' for r in manifest['sounds']),'held':sum(r['status']!='exported' for r in manifest['sounds'])}
        print(name,result['packages'][name],flush=True)
    after=snapshot();assert before==after,'Original game data or saves changed'
    result.update(after=after,originals_unchanged=True)
    (a.output/'export-report.json').write_text(json.dumps(result,indent=2))


if __name__=='__main__':main()
