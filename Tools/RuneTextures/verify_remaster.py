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
from build_remaster import ART_CHANNELS, MATERIAL_CHANNELS, MAGIC, PIPELINE_VERSION, identity_key, fingerprint, channel_formats


def validate_ktx(data, tier):
    """Check every mip payload as well as the receipt and top-level size."""
    assert data[:12] == MAGIC and len(data) >= 68
    words = struct.unpack_from('<13I', data, 12)
    endian, typ, type_size, fmt, internal, base, w, h, depth, arrays, faces, count, extra = words
    assert endian == 0x04030201 and type_size == 1 and base == 0x1908
    assert not depth and not arrays and faces == 1 and not extra
    assert [w, h] == tier['size'] and count == tier['mips'] == max(w, h).bit_length()
    assert w > 0 and h > 0 and w <= 2048 and h <= 2048 and not (w & (w-1)) and not (h & (h-1))
    compressed = internal in (0x93b0, 0x93b4)
    assert (typ == fmt == 0) if compressed else (internal == 0x8058 and typ == 0x1401 and fmt == 0x1908)
    pos = 64
    for _ in range(count):
        length, = struct.unpack_from('<I', data, pos)
        block = 6 if internal == 0x93b4 else 4
        expected = ((w+block-1)//block) * ((h+block-1)//block) * 16 if compressed else w*h*4
        assert length == expected and pos + 4 + length <= len(data)
        pos += 4 + length
        w, h = max(1, w//2), max(1, h//2)
    assert pos == len(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pack', type=Path, required=True)
    ap.add_argument('--astcenc', type=Path)
    ap.add_argument('--samples', type=Path, required=True)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--keep-going', action='store_true', help='Report all compression-quality failures, retaining the error threshold')
    args = ap.parse_args()
    recipe = json.loads((args.pack / 'recipe.json').read_text())
    receipts = json.loads((args.pack / 'receipts.json').read_text())
    profile = json.loads((args.pack / 'pipeline.json').read_text()) if (args.pack / 'pipeline.json').exists() else {'version': 1}
    assert profile['version'] in (1, PIPELINE_VERSION)
    index_lines = (args.pack / 'index.tsv').read_text().splitlines()
    assert index_lines[0] == 'OPENRUNE_TEXTURES_1'
    index = {}
    for line in index_lines[1:]:
        name, width, height, digest = line.split('\t')
        assert name not in index
        index[name] = (int(width), int(height), digest)
    assert set(index) == {identity_key(a['identity']) for a in recipe['assets']}
    result = {'files': 0, 'payload_bytes': {'rgba8': 0, 'astc': 0}, 'decode_checks': []}
    for asset, receipt in zip(recipe['assets'], receipts, strict=True):
        assert receipt['id'] == identity_key(asset['identity'])
        original = Image.open(args.source / asset['source']).convert('RGB')
        assert index[receipt['id']] == (original.width, original.height, fingerprint(np.asarray(original)))
        assert receipt['source_sha256'] == hashlib.sha256((args.source / asset['source']).read_bytes()).hexdigest()
        if asset.get('master'):
            assert receipt['master_sha256'] == hashlib.sha256((args.samples / asset['master']).read_bytes()).hexdigest()
        else:
            assert receipt['master_sha256'] is None
        if profile['version'] == PIPELINE_VERSION:
            assert set(receipt['tiers']) == ({'4', '2', '1'} if asset.get('master') else {'1'})
        for scale, tier in receipt['tiers'].items():
            root = args.pack / receipt['id'] / (scale + 'x')
            if profile['version'] == PIPELINE_VERSION:
                channels = MATERIAL_CHANNELS if scale == '1' else ART_CHANNELS
                expected_files = {c + ('.astc.ktx' if compressed else '.ktx') for c in channels for compressed in channel_formats(asset, scale, c, profile['formats'])}
                assert set(tier['files']) == expected_files
            for name, expected in tier['files'].items():
                data = (root / name).read_bytes()
                assert len(data) == expected['bytes']
                assert hashlib.sha256(data).hexdigest() == expected['sha256']
                validate_ktx(data, tier)
                result['files'] += 1
                result['payload_bytes']['astc' if '.astc.' in name else 'rgba8'] += len(data) - 64 - 4*tier['mips']
        decode_cases = [(scale, channel) for scale in ('4', '1') if scale in receipt['tiers']
                        for channel in (MATERIAL_CHANNELS if scale == '1' else
                                        ('color', 'normal', 'response', 'relief') if profile['version'] == PIPELINE_VERSION else ('color', 'normal'))]
        for scale, channel in decode_cases:
            root = args.pack / receipt['id'] / (scale + 'x')
            if not (root / f'{channel}.astc.ktx').exists():
                continue
            if (root / f'{channel}.ktx').exists():
                rgba = (root / f'{channel}.ktx').read_bytes()
                w, h = struct.unpack_from('<2I', rgba, 36)
                reference = np.frombuffer(rgba, np.uint8, count=w*h*4, offset=68).reshape(h, w, 4).astype(float)
                comparison = 'rgba8'
            else:
                label = 'normal-roughness' if channel == 'normal' else channel
                prefix = receipt['id'] + ('-original' if scale == '1' else '')
                reference = np.asarray(Image.open(args.pack / 'review' / f"{prefix}-{label}.png"), dtype=float)
                comparison = 'prepared-review-image'
            with tempfile.TemporaryDirectory() as tmp:
                if not args.astcenc:
                    raise ValueError('Compressed pack verification requires --astcenc')
                target = Path(tmp) / 'decoded.png'
                subprocess.run([str(args.astcenc), '-dl', str(root / f'{channel}.astc.ktx'), str(target), '-silent'], capture_output=True, check=True)
                decoded = np.asarray(Image.open(target).convert('RGBA'), dtype=float)
            if reference.shape[2] == 3:
                decoded = decoded[..., :3]
            rmse = float(np.sqrt(np.mean((reference-decoded)**2)))
            if not args.keep_going:
                assert rmse < 15, (receipt['id'], scale, channel, rmse)
            result['decode_checks'].append({'id': receipt['id'], 'tier': scale, 'channel': channel,
                                            'reference': comparison, 'rmse_255': rmse})
    result['failed_decode_checks'] = [c for c in result['decode_checks'] if c['rmse_255'] >= 15]
    print(json.dumps(result, indent=2))
    if result['failed_decode_checks']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
