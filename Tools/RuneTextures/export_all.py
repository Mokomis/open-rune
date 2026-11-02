"""Extract owned Rune textures with the engine's package decoder; never edit the game."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--engine', type=Path, required=True)
    ap.add_argument('--game', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    game, output = args.game.resolve(), args.output.resolve()
    if output == game or game in output.parents:
        ap.error('output must be outside the original game directory')
    output.mkdir(parents=True, exist_ok=True)
    sources = sorted(p for folder in ('Textures', 'System', 'Meshes', 'Maps')
                     for p in (game / folder).iterdir()
                     if p.suffix.lower() in ('.utx', '.u', '.ums', '.run'))
    before = {str(p.relative_to(game)): digest(p) for p in sources}
    records, failures = [], []
    names = set()
    for p in sources:
        if p.stem.lower() in names:
            raise ValueError(f'ambiguous package stem: {p}')
        names.add(p.stem.lower())
        dest = output / 'source' / p.stem
        manifest = dest / 'manifest.json'
        # Resume only complete exports of the same original package.
        if manifest.exists() and json.loads(manifest.read_text()).get('source_sha256') == before[str(p.relative_to(game))]:
            data = json.loads(manifest.read_text())
        else:
            if dest.exists():
                raise ValueError(f'incomplete or stale export requires a fresh output directory: {dest}')
            result = subprocess.run([str(args.engine.resolve()), '--rune-export-textures',
                                     '--texture-package=' + (str(p) if p.suffix.lower() == '.run' else p.stem),
                                     '--texture-output=' + str(dest),
                                     '--save-directory=' + str(output / 'isolated-saves'), str(game)],
                                    capture_output=True, text=True)
            if result.returncode:
                failures.append({'package': p.name, 'error': result.stderr[-3000:]})
                print('FAILED', p.name, result.stderr[-300:], flush=True)
                continue
            data = json.loads(manifest.read_text())
            data['source_sha256'] = before[str(p.relative_to(game))]
            for row in data['textures']:
                if row['status'] == 'exported':
                    row['sha256'] = digest(dest / row['file'])
                    row['mask_sha256'] = digest(dest / row['mask_file'])
            manifest.write_text(json.dumps(data, indent=2) + '\n')
        for row in data['textures']:
            records.append(dict(row, directory=str(dest.relative_to(output))))
        print(p.name, len(data['textures']), flush=True)
    after = {str(p.relative_to(game)): digest(p) for p in sources}
    assert before == after, 'original package changed during export'
    report = {'schema': 1, 'source_packages': before, 'original_packages_unchanged': True,
              'textures': records, 'failures': failures,
              'counts': dict(collections.Counter(x['status'] for x in records))}
    (output / 'inventory.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'counts': report['counts'], 'failures': len(failures)}, indent=2))
    raise SystemExit(bool(failures))


if __name__ == '__main__':
    main()
