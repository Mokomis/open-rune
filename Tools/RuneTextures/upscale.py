"""Offline faithful AI texture pack builder. Original packages remain untouched.

Uses official Real-ESRGAN compact models through Spandrel. No generative prompts,
face restoration or inference at game runtime. Output is an external PNG pack.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time

import numpy as np
from PIL import Image
import torch
from spandrel import ModelLoader
from scipy.ndimage import distance_transform_edt


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def atomic_png(image, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix('.tmp')
    image.save(temp, format='PNG', compress_level=4)
    temp.replace(path)


def exclusion(row):
    if row['status'] != 'exported':
        return row['status']
    if row['animated']:
        return 'animated-original'
    package = row['id'].split('.')[0].lower()
    if package in {'uwindowfonts', 'runeserveradmin', 'runemenus', 'uwindow', 'editor', 'engine',
                   'runefx', 'runefx2', 'runefx3', 'bloodfx', 'fluid', 'fogouts', 'palettes', 'sky'}:
        return 'ui-or-effect-original'
    if row['flags'] & (4 | 64):
        return 'translucent-or-modulated-original'
    if min(row['width'], row['height']) < 32:
        return 'small-utility-original'
    return None


def process(model, rgb, device, boundary, tile=256, halo=40):
    """Overlapping inference, with full receptive-field padding at every crop.

    Compact v3 has 34 3x3 convolutions: 40 input pixels exceed its receptive radius.
    Periodic padding retains repeat context; reflected padding is for skin atlases.
    """
    h, w = rgb.shape[:2]
    padded = np.pad(rgb, ((halo, halo), (halo, halo), (0, 0)), mode=boundary)
    out = np.empty((h * 4, w * 4, 3), np.uint8)
    for y in range(0, h, tile):
        for x in range(0, w, tile):
            th, tw = min(tile, h-y), min(tile, w-x)
            crop = padded[y:y+th+2*halo, x:x+tw+2*halo]
            inp = torch.from_numpy(crop.copy()).permute(2, 0, 1).unsqueeze(0).float().div_(255).to(device)
            with torch.inference_mode():
                result = model(inp)[0, :, halo*4:(halo+th)*4, halo*4:(halo+tw)*4]
                result = result.clamp(0, 1).mul(255).round().byte().permute(1, 2, 0).cpu().numpy()
            out[y*4:(y+th)*4, x*4:(x+tw)*4] = result
    return Image.fromarray(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--models', type=Path, required=True)
    ap.add_argument('--denoise', type=float, default=0.25)
    ap.add_argument('--only', nargs='*', help='Exact texture identities for sample builds')
    args = ap.parse_args()
    if not 0 <= args.denoise <= 1:
        ap.error('denoise must be in [0,1]')
    inv = json.loads((args.root/'inventory.json').read_text())
    if inv['failures']:
        ap.error('resolve export failures before building a pack')
    modelpaths = [args.models/'realesr-general-x4v3.pth', args.models/'realesr-general-wdn-x4v3.pth']
    weights = {p.name: sha(p) for p in modelpaths}
    expected = {'realesr-general-x4v3.pth': '8dc7edb9ac80ccdc30c3a5dca6616509367f05fbc184ad95b731f05bece96292',
                'realesr-general-wdn-x4v3.pth': '1641f8c4464b9f097c9fdda5589273713f67cf59f3d909e0bd688f0cee269dca'}
    if weights != expected:
        ap.error('model hashes differ from the verified official compact-v3 weights')
    recipe = {'version': 2, 'model': 'realesr-general-x4v3', 'denoise': args.denoise,
              'weights': weights, 'tile': 256, 'halo': 40,
              'scales': [2, 4], 'alpha': 'original palette-zero nearest sidecar; masked RGBA uses bilinear original coverage and nearest opaque RGB dilation',
              'selection': args.only, 'torch': torch.__version__}
    args.output.mkdir(parents=True, exist_ok=True)
    recipefile = args.output/'recipe.json'
    if recipefile.exists() and json.loads(recipefile.read_text()) != recipe:
        ap.error('output has a different recipe; use a new output folder')
    recipefile.write_text(json.dumps(recipe, indent=2)+'\n')
    sd, weak = [torch.load(p, map_location='cpu', weights_only=True)['params'] for p in modelpaths]
    sd = {k: args.denoise * sd[k] + (1-args.denoise)*weak[k] for k in sd}
    model = ModelLoader().load_from_state_dict(sd)
    assert model.scale == 4 and model.input_channels == model.output_channels == 3
    device = 'mps' if torch.backends.mps.is_available() else 'cuda' if torch.cuda.is_available() else 'cpu'
    model.to(device).eval()
    torch.set_num_threads(3)
    rows, skipped = [], []
    for row in inv['textures']:
        if args.only and row['id'] not in args.only:
            continue
        reason = exclusion(row)
        if reason:
            skipped.append({'id': row['id'], 'reason': reason})
        else:
            rows.append(row)
    done, reused = [], {}
    start = time.monotonic()
    for i, row in enumerate(rows):
        source = args.root/row['directory']/row['file']
        maskpath = args.root/row['directory']/row['mask_file']
        if sha(source) != row['sha256'] or sha(maskpath) != row['mask_sha256']:
            raise ValueError('source changed: '+row['id'])
        package = row['id'].split('.')[0]
        skin = package.lower().startswith(('players', 'creatures', 'weapons', 'objects', 'plants'))
        boundary = 'reflect' if skin else 'wrap'
        key = (row['sha256'], row['mask_sha256'], row['masked'], boundary)
        destination = {s: args.output/f'{s}x'/package/row['file'] for s in (2, 4)}
        receipt = args.output/'receipts'/(row['id']+'.json')
        if receipt.exists():
            record = json.loads(receipt.read_text())
            if record['source_sha256'] != row['sha256'] or record['source_mask_sha256'] != row['mask_sha256'] or any(not (args.output/f).exists() or sha(args.output/f) != h for f,h in record['outputs'].items()):
                raise ValueError('stale or damaged output: '+row['id'])
            done.append(record)
            reused[key] = record
            continue
        if key in reused:
            previous = reused[key]
            outputs = {}
            for filename, fingerprint in previous['outputs'].items():
                old = args.output / filename
                oldbase = previous['id'].split('.', 1)[1]
                newbase = row['file'][:-4]
                name = newbase + old.name[len(oldbase):]
                dest = args.output / Path(filename).parts[0] / package / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                if dest.exists():
                    if sha(dest) != fingerprint:
                        raise ValueError('conflicting duplicate output: '+str(dest))
                else:
                    os.link(old, dest)
                outputs[str(dest.relative_to(args.output))] = fingerprint
            record = dict(previous, id=row['id'], outputs=outputs, shared_with=previous['id'])
            receipt.parent.mkdir(exist_ok=True)
            receipt.write_text(json.dumps(record, indent=2)+'\n')
            done.append(record)
            continue
        rgb = np.asarray(Image.open(source).convert('RGB'))
        result = process(model, rgb, device, boundary)
        mask = Image.open(maskpath).convert('L')
        masked_result = None
        if row['masked']:
            visible = np.asarray(mask) != 0
            if visible.any() and not visible.all():
                nearest = distance_transform_edt(~visible, return_distances=False, return_indices=True)
                filled = rgb[nearest[0], nearest[1]]
                masked_result = process(model, filled, device, boundary)
            else:
                masked_result = result
        outputs = {}
        for scale, dest in destination.items():
            size = (row['width']*scale, row['height']*scale)
            atomic_png(result if scale == 4 else result.resize(size, Image.Resampling.LANCZOS), dest)
            maskdest = dest.with_name(dest.stem+'.mask.png')
            atomic_png(mask.resize(size, Image.Resampling.NEAREST), maskdest)
            for path in (dest, maskdest):
                outputs[str(path.relative_to(args.output))] = sha(path)
            if masked_result is not None:
                rgba = (masked_result if scale == 4 else masked_result.resize(size, Image.Resampling.LANCZOS)).convert('RGBA')
                rgba.putalpha(mask.resize(size, Image.Resampling.BILINEAR))
                masked_dest = dest.with_name(dest.stem+'.masked.png')
                atomic_png(rgba, masked_dest)
                outputs[str(masked_dest.relative_to(args.output))] = sha(masked_dest)
        record = {'id': row['id'], 'source_sha256': row['sha256'], 'source_mask_sha256': row['mask_sha256'],
                  'width': row['width'], 'height': row['height'], 'boundary': boundary, 'outputs': outputs}
        receipt.parent.mkdir(exist_ok=True)
        receipt.write_text(json.dumps(record, indent=2)+'\n')
        done.append(record)
        reused[key] = record
        if (i+1)%25 == 0 or i == 0 or i+1 == len(rows):
            print(json.dumps({'complete': i+1, 'total': len(rows), 'seconds': round(time.monotonic()-start, 1), 'last': row['id']}), flush=True)
    report = {'schema': 1, 'recipe': recipe, 'device': device, 'textures': done,
              'original_fallbacks': skipped, 'runtime_integrated': False}
    (args.output/'pack.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PACK COMPLETE', len(done), 'upscaled;', len(skipped), 'original fallbacks', flush=True)


if __name__ == '__main__':
    main()
