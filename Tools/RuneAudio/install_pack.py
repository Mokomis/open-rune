"""Install a verified optional pack beside game data without overwriting files."""
import argparse,hashlib,json,shutil,time,zipfile
from pathlib import Path
from verify_production import verify


def install(pack,game,archive=None):
    report=verify(pack);assert report['passed'] and not report['held']
    parent=game/'SoundPacks';parent.mkdir(exist_ok=True);dest=parent/'OpenRune-Layered-v1'
    files=['runtime.json','manifest.json','suspect-review.json']
    def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
    if dest.exists():
        if digest(dest/'runtime.json')!=digest(pack/'runtime.json'):raise ValueError('An existing different sound pack was left unchanged')
    else:
        stage=parent/('OpenRune-Layered-v1.importing-'+str(time.time_ns()));stage.mkdir()
        try:
            for name in files:shutil.copy2(pack/name,stage/name)
            shutil.copytree(pack/'replacements',stage/'replacements')
            for entry in json.loads((stage/'runtime.json').read_text())['assets']:
                path=stage/entry['file'];assert hashlib.sha1(path.read_bytes()).hexdigest()==entry['file_sha1']
            stage.rename(dest)
        except Exception:shutil.rmtree(stage);raise
    if archive:
        if archive.exists():raise ValueError('Archive already exists')
        with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=4) as z:
            for path in dest.rglob('*'):
                if path.is_file():z.write(path,Path(dest.name)/path.relative_to(dest))
    print(json.dumps(dict(installed=str(dest),replacements=report['replacements'],game_settings_changed=False,archive=str(archive) if archive else None)))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--pack',type=Path,required=True);p.add_argument('--game',type=Path,required=True);p.add_argument('--archive',type=Path);a=p.parse_args();install(a.pack,a.game,a.archive)
