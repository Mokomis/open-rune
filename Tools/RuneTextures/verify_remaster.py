"""Verify receipts and an independent ASTC decode of each pilot color/normal map."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pack', type=Path, required=True)
    ap.add_argument('--astcenc', type=Path, required=True)
    ap.add_argument('--samples', type=Path, required=True)
    ap.add_argument('--source', type=Path, required=True)
    args = ap.parse_args()
    recipe = json.loads((args.pack / 'recipe.json').read_text())
    receipts = json.loads((args.pack / 'receipts.json').read_text())
    result = {'files': 0, 'payload_bytes': {'rgba8': 0, 'astc': 0}, 'decode_checks': []}
    for asset, receipt in zip(recipe['assets'], receipts, strict=True):
        assert receipt['source_sha256'] == hashlib.sha256((args.source / asset['source']).read_bytes()).hexdigest()
        assert receipt['master_sha256'] == hashlib.sha256((args.samples / asset['master']).read_bytes()).hexdigest()
        for scale, tier in receipt['tiers'].items():
            root = args.pack / receipt['id'] / (scale + 'x')
            for name, expected in tier['files'].items():
                data = (root / name).read_bytes()
                assert len(data) == expected['bytes']
                assert hashlib.sha256(data).hexdigest() == expected['sha256']
                assert list(struct.unpack_from('<2I', data, 36)) == tier['size']
                assert struct.unpack_from('<I', data, 56)[0] == tier['mips']
                result['files'] += 1
                result['payload_bytes']['astc' if '.astc.' in name else 'rgba8'] += len(data) - 64 - 4*tier['mips']
        for channel in ('color', 'normal'):
            root = args.pack / receipt['id'] / '4x'
            if not (root / f'{channel}.astc.ktx').exists():
                continue
            if (root / f'{channel}.ktx').exists():
                rgba = (root / f'{channel}.ktx').read_bytes()
                w, h = struct.unpack_from('<2I', rgba, 36)
                reference = np.frombuffer(rgba, np.uint8, count=w*h*4, offset=68).reshape(h, w, 4).astype(float)
                comparison = 'rgba8'
            else:
                label = 'color' if channel == 'color' else 'normal-roughness'
                reference = np.asarray(Image.open(args.pack / 'review' / f"{receipt['id']}-{label}.png"), dtype=float)
                comparison = 'prepared-review-image'
            with tempfile.TemporaryDirectory() as tmp:
                target = Path(tmp) / 'decoded.png'
                subprocess.run([str(args.astcenc), '-dl', str(root / f'{channel}.astc.ktx'), str(target), '-silent'], capture_output=True, check=True)
                decoded = np.asarray(Image.open(target).convert('RGBA'), dtype=float)
            if reference.shape[2] == 3:
                decoded = decoded[..., :3]
            rmse = float(np.sqrt(np.mean((reference-decoded)**2)))
            assert rmse < 15, (receipt['id'], channel, rmse)
            result['decode_checks'].append({'id': receipt['id'], 'channel': channel,
                                            'reference': comparison, 'rmse_255': rmse})
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
