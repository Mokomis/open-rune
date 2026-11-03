"""Native decode/fallback regression; no rendering, actors or live saves."""
import argparse,copy,json,os,subprocess,time
from pathlib import Path


def main():
    p=argparse.ArgumentParser();p.add_argument('--engine',type=Path,required=True);p.add_argument('--game',type=Path,required=True);p.add_argument('--pack',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    a.engine=a.engine.resolve();a.game=a.game.resolve();a.pack=a.pack.resolve();a.output=a.output.resolve()
    index=json.loads((a.pack/'runtime.json').read_text());mapped={r['id'].lower() for r in index['assets']};checks=[]
    def run(name,package,mode=0,pack=None,settings=False):
        root=a.output/name;root.mkdir(exist_ok=True);target=root/'result.json';config=root/'config';config.mkdir(exist_ok=True)
        if settings:
            (config/'SurrealEngine').mkdir(exist_ok=True);(config/'SurrealEngine/DesktopMenu.json').write_text('{"soundRemaster":1}')
        args=[str(a.engine),'--sound-pack-probe','--sound-probe-package='+package,'--sound-probe-output='+str(target),'--save-directory='+str(root/'saves'),'--sound-pack='+str(pack or a.pack)]
        if not settings:args.append('--sound-remaster='+str(mode))
        with (root/'native.log').open('wb') as log:subprocess.run(args+[str(a.game)],env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(config)),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
        return json.loads(target.read_text())
    baselines={}
    for package in ['WeaponsSnd','FootstepsSnd','EnvironmentalSnd','CreaturesSnd','DoorsSnd','MurmurSnd','Addon','OtherSnd','VoiceOverSnd','MusicalSnd','SongSnd']:
        original=run(package+'-original',package);layered=run(package+'-layered',package,1)
        original_rows={r['id'].lower():r for r in original['sounds']};new_rows={r['id'].lower():r for r in layered['sounds']};assert original_rows.keys()==new_rows.keys()
        changed=0
        for identity,row in original_rows.items():
            newer=new_rows[identity]
            for field in ['samples','rate','channels','duration','looped','start','end']:assert row[field]==newer[field],(identity,field)
            different=row['decoded_sha1']!=newer['decoded_sha1']
            if identity in mapped:assert different,identity;changed+=1
            else:assert not different,identity
        assert layered['replaced']==changed and layered['rejected']==0
        assert original['decoded_bytes']==layered['decoded_bytes']
        checks.append(dict(package=package,total=len(original_rows),replaced=changed,decoded_bytes=layered['decoded_bytes'],original_seconds=original['seconds'],layered_seconds=layered['seconds']))
        baselines[package]=original
    package='WeaponsSnd';original=baselines[package]
    def unchanged(result):
        assert [r['decoded_sha1'] for r in result['sounds']]==[r['decoded_sha1'] for r in original['sounds']]
        assert result['replaced']==0
    unchanged(run('missing-pack',package,1,a.output/'missing-pack'))
    one=copy.deepcopy(next(r for r in index['assets'] if r['id']=='WeaponsSnd.ImpFlesh.impfleshaxe01'))
    for name,mutate in [('source-mismatch',lambda r:r.update(source_sha1='0'*40)),('file-mismatch',lambda r:r.update(file_sha1='0'*40)),('timing-mismatch',lambda r:r.update(frames=r['frames']+1)),('missing-file',lambda r:r.update(file='replacements/missing.wav')),('path-escape',lambda r:r.update(file='../escape.wav'))]:
        fixture=a.output/name;fixture.mkdir(exist_ok=True);row=copy.deepcopy(one);mutate(row)
        if name not in ['path-escape','missing-file']:
            target=fixture/row['file'];target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes((a.pack/one['file']).read_bytes())
        (fixture/'runtime.json').write_text(json.dumps(dict(schema=1,assets=[row])))
        unchanged(run(name+'-run',package,1,fixture))
    if os.name=='nt':
        saved=run('saved-desktop-setting',package,pack=a.pack,settings=True);assert saved['replaced']>0
    result=dict(passed=True,packages=checks,total_sounds=sum(r['total'] for r in checks),total_replaced=sum(r['replaced'] for r in checks),fallbacks=['missing pack','source mismatch','file mismatch','timing mismatch','missing file','path escape'],windows_saved_setting_checked=os.name=='nt')
    (a.output/'summary.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))


if __name__=='__main__':main()
