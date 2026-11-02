"""Check every generated file against its receipt and original mask/dimensions."""
import argparse
import collections
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image


def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--pack', type=Path, required=True)
    args = ap.parse_args()
    inventory = json.loads((args.root/'inventory.json').read_text())
    original = {r['id']: r for r in inventory['textures']}
    pack = json.loads((args.pack/'pack.json').read_text())
    ids = [r['id'] for r in pack['textures']]
    assert len(ids) == len(set(ids)), 'duplicate output identity'
    files, masked, size, drift = 0, 0, collections.Counter(), []
    for record in pack['textures']:
        src = original[record['id']]
        sourcefile = args.root/src['directory']/src['file']
        maskfile = args.root/src['directory']/src['mask_file']
        assert sha(sourcefile) == record['source_sha256'] == src['sha256']
        assert sha(maskfile) == record['source_mask_sha256'] == src['mask_sha256']
        mask = Image.open(maskfile).convert('L')
        for filename, expected in record['outputs'].items():
            path = args.pack/filename
            assert path.resolve().is_relative_to(args.pack.resolve())
            assert sha(path) == expected, f'hash mismatch {filename}'
            scale = int(Path(filename).parts[0][:-1])
            assert scale in (2, 4)
            image = Image.open(path)
            assert image.size == (record['width']*scale, record['height']*scale)
            image.load()
            if filename.endswith('.mask.png') or filename.endswith('.masked.png'):
                alpha = image.getchannel('A') if filename.endswith('.masked.png') else image.convert('L')
                resample = Image.Resampling.BILINEAR if filename.endswith('.masked.png') else Image.Resampling.NEAREST
                assert np.array_equal(np.asarray(alpha), np.asarray(mask.resize(image.size, resample))), f'mask changed {filename}'
                masked += filename.endswith('.masked.png')
            elif scale == 4:
                restored = np.asarray(image.convert('RGB').resize((src['width'],src['height']),Image.Resampling.LANCZOS), dtype=np.float32)
                source = np.asarray(Image.open(sourcefile).convert('RGB'), dtype=np.float32)
                drift.append({'id': record['id'], 'rgb_mae_after_downsample': round(float(np.abs(restored-source).mean()),3),
                              'mean_rgb_shift': [round(float(x),3) for x in (restored-source).mean(axis=(0,1))]})
            files += 1
            size[f'{scale}x'] += path.stat().st_size
    report = {'textures': len(ids), 'files_verified': files, 'masked_variants_verified': masked,
              'bytes_by_tier': dict(size), 'hashes_dimensions_and_masks': 'passed',
              'fallbacks': dict(collections.Counter(x['reason'] for x in pack['original_fallbacks'])),
              'largest_pixel_changes_for_review': sorted(drift, key=lambda x:-x['rgb_mae_after_downsample'])[:25],
              'limitations': ['Pixel statistics do not prove artistic fidelity.', 'No in-game performance or rendering test.']}
    (args.pack/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='largest_pixel_changes_for_review'},indent=2))


if __name__ == '__main__':
    main()
