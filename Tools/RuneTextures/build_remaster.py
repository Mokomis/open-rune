"""Prepare reviewed generative masters; never overwrite owned packages or masters.

Color is stored in the engine's existing display-encoded UNORM convention. Mips
are filtered in linear light. Normal/roughness/response maps are linear data.
The maps estimate surface response; they are not recovered physical measurements.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import gaussian_filter, distance_transform_edt

MAGIC = b'\xabKTX 11\xbb\r\n\x1a\n'
MATERIALS = {  # roughness, specular strength, metal tint, relief amplitude
    'stone': (.88, .06, 0, .8), 'wood': (.78, .08, 0, .6),
    'metal': (.42, .27, .7, .35), 'skin': (.7, .08, 0, .18),
    'hair': (.86, .05, 0, .35), 'cloth': (.92, .03, 0, .25),
    'neutral': (.82, .06, 0, .3),
}


def linear(rgb):
    return np.where(rgb <= .04045, rgb / 12.92, ((rgb + .055) / 1.055) ** 2.4)


def encoded(rgb):
    rgb = np.maximum(rgb, 0)
    return np.where(rgb <= .0031308, rgb * 12.92, 1.055 * rgb ** (1 / 2.4) - .055)


def resize_float(a, size):
    if a.ndim == 2:
        return np.asarray(Image.fromarray(a.astype('float32')).resize(size, Image.Resampling.BOX))
    return np.stack([resize_float(a[..., i], size) for i in range(a.shape[2])], -1)


def region_map(recipe, size):
    names = list(MATERIALS)
    image = Image.new('L', size, names.index(recipe['material']))
    draw = ImageDraw.Draw(image)
    for region in recipe.get('regions', []):
        draw.polygon([(round(x * size[0]), round(y * size[1])) for x, y in region['polygon']], fill=names.index(region['material']))
    return np.asarray(image), names


def correct_color(original, generated, regions, mode):
    """Transfer broad original color separately within each material region.

    Local offsets retain generated fine detail; material-aware weights prevent
    a pale stone border from pushing the dark wooden door toward grey.
    """
    reference = np.asarray(original.resize((generated.shape[1], generated.shape[0]), Image.Resampling.BICUBIC), dtype=np.float32) / 255
    sigma = max(generated.shape[:2]) / 32
    corrected = generated.copy()
    for region in np.unique(regions):
        mask = (regions == region).astype(np.float32)
        weight = np.maximum(gaussian_filter(mask, sigma, mode=mode), 1e-5)
        for c in range(3):
            delta = gaussian_filter((reference[..., c] - generated[..., c]) * mask, sigma, mode=mode) / weight
            corrected[..., c] += delta * mask
    return np.clip(corrected, 0, 1)


def soften_stone_grain(rgb, original, regions, names, gain, mode):
    """Reduce generated stone grain while retaining the source's carved detail."""
    if not 0 <= gain <= 1:
        raise ValueError('stone_detail_gain must be between 0 and 1')
    if gain == 1 or 'stone' not in names:
        return rgb
    mask = regions == names.index('stone')
    if not np.any(mask):
        return rgb
    sigma = max(rgb.shape[:2]) / 32
    reference = np.asarray(original.resize((rgb.shape[1], rgb.shape[0]), Image.Resampling.BICUBIC), dtype=np.float32) / 255
    generated_detail = rgb - gaussian_filter(rgb, (sigma, sigma, 0), mode=mode)
    source_detail = reference - gaussian_filter(reference, (sigma, sigma, 0), mode=mode)
    # Preserve pixels at material boundaries and the source's own cracks.
    # Only added contrast is reduced, including medium-size generated pebbles.
    interior = np.clip((distance_transform_edt(mask) - 1) / max(sigma, 1), 0, 1)
    strength = (1 - gain) * interior[..., None]
    return np.clip(rgb - (generated_detail - source_detail) * strength, 0, 1)


def blend_source_structure(rgb, original, amount):
    """Restore a share of the source where generated medium detail is too busy."""
    if not 0 <= amount <= 1:
        raise ValueError('source_blend must be between 0 and 1')
    if amount == 0:
        return rgb
    reference = np.asarray(original.resize((rgb.shape[1], rgb.shape[0]), Image.Resampling.BICUBIC), dtype=np.float32) / 255
    return np.clip(rgb * (1 - amount) + reference * amount, 0, 1)


def wrap_edges(rgb, width=8):
    # Periodic masters are approved separately; force matching boundary samples
    # without changing the broad color. Blend only a narrow edge band.
    out = rgb.copy()
    for axis in (0, 1):
        a = np.moveaxis(out, axis, 0)
        average = (a[0].copy() + a[-1].copy()) * .5
        left, right = average - a[0], average - a[-1]
        for i in range(min(width, len(a) // 2)):
            t = (1 - i / width) ** 2
            a[i] += left * t
            a[-1-i] += right * t
    return np.clip(out, 0, 1)


def fill_transparent(rgb, alpha):
    if np.any(alpha > .5) and np.any(alpha <= .5):
        nearest = distance_transform_edt(alpha <= .5, return_distances=False, return_indices=True)
        rgb = np.where((alpha > .5)[..., None], rgb, rgb[tuple(nearest)])
    return rgb


def byte_image(a):
    return np.uint8(np.clip(np.rint(a * 255), 0, 255))


def color_mips(rgb, alpha):
    coverage = np.mean(alpha >= .5)
    color = linear(rgb)
    levels = []
    while True:
        levels.append(byte_image(np.dstack((encoded(fill_transparent(color, alpha)), alpha))))
        h, w = alpha.shape
        if w == h == 1:
            return levels
        size = max(1, w // 2), max(1, h // 2)
        a = resize_float(alpha, size)
        color = resize_float(color * alpha[..., None], size) / np.maximum(a[..., None], 1e-6)
        # Preserve alpha-test coverage as closely as this mip's pixel grid allows.
        if 0 < coverage < 1:
            lo, hi = 0., 8.
            for _ in range(18):
                mid = (lo + hi) / 2
                if np.mean(a * mid >= .5) < coverage:
                    lo = mid
                else:
                    hi = mid
            options = [np.clip(a * lo, 0, 1), np.clip(a * hi, 0, 1)]
            a = min(options, key=lambda v: abs(np.mean(v >= .5) - coverage))
        alpha = a


def material_mips(rgb, regions, names, mode):
    lum = rgb @ np.array([.2126, .7152, .0722])
    detail = lum - gaussian_filter(lum, max(rgb.shape[:2]) / 128, mode=mode)
    rough = np.zeros_like(lum); strength = rough.copy(); metal = rough.copy(); relief = rough.copy()
    for i, name in enumerate(names):
        r, s, m, h = MATERIALS[name]
        mask = regions == i
        rough[mask], strength[mask], metal[mask], relief[mask] = r, s, m, h
    # Derivatives wrap only for tileable environment art; atlas edges clamp.
    gx = (np.roll(detail, -1, 1) - np.roll(detail, 1, 1)) * .5
    gy = (np.roll(detail, -1, 0) - np.roll(detail, 1, 0)) * .5
    if mode != 'wrap':
        gx[:, [0, -1]] = 0; gy[[0, -1], :] = 0
    n = np.dstack((-gx * relief * 8, -gy * relief * 8, np.ones_like(lum)))
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    response = np.dstack((strength, metal, np.zeros_like(lum), np.ones_like(lum)))
    normals, responses = [], []
    while True:
        normals.append(byte_image(np.dstack((n * .5 + .5, rough))))
        responses.append(byte_image(response))
        h, w = rough.shape
        if w == h == 1:
            return normals, responses
        size = max(1, w // 2), max(1, h // 2)
        n = resize_float(n, size)
        length = np.linalg.norm(n, axis=2)
        rough = np.sqrt(np.clip(resize_float(rough ** 2, size) + 1 - length, 0, 1))
        n /= np.maximum(length[..., None], 1e-6)
        response = resize_float(response, size)


def ktx(path, levels, encoder=None, block=6):
    blobs = []
    with tempfile.TemporaryDirectory() as temp:
        for i, pixels in enumerate(levels):
            if encoder:
                source, target = Path(temp) / f'{i}.png', Path(temp) / f'{i}.astc'
                Image.fromarray(pixels).save(source)
                subprocess.run([str(encoder), '-cl', str(source), str(target), f'{block}x{block}', '-medium', '-silent', '-j', '2'], check=True, capture_output=True)
                data = target.read_bytes()
                assert data[:4] == b'\x13\xab\xa1\x5c'
                blobs.append(data[16:])
            else:
                blobs.append(pixels.tobytes())
    h, w = levels[0].shape[:2]
    fmt = (0x93b4 if block == 6 else 0x93b0) if encoder else 0x8058
    header = struct.pack('<13I', 0x04030201, 0 if encoder else 0x1401, 1, 0 if encoder else 0x1908, fmt, 0x1908, w, h, 0, 0, 1, len(blobs), 0)
    path.write_bytes(MAGIC + header + b''.join(struct.pack('<I', len(b)) + b for b in blobs))


def fingerprint(rgb):
    h = 14695981039346656037
    for b in rgb.tobytes():
        h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
    return f'{h:016x}'


def existing_tiers(root, size, formats):
    """Recover complete output after an interrupted batch without regenerating it."""
    tiers = {}
    for scale in (4, 2):
        factor = scale
        while max(size) * factor > 2048:
            factor //= 2
        expected = tuple(n * factor for n in size)
        files = {}
        mips = None
        for channel in ('color', 'masked', 'normal', 'response'):
            for compressed in formats:
                name = f'{channel}{".astc" if compressed else ""}.ktx'
                path = root / f'{scale}x' / name
                data = path.read_bytes()
                if data[:12] != MAGIC or len(data) < 68 or tuple(struct.unpack_from('<2I', data, 36)) != expected:
                    raise ValueError(f'Incomplete or invalid prior output: {path}')
                count = struct.unpack_from('<I', data, 56)[0]
                if count != max(expected).bit_length() or (mips is not None and count != mips):
                    raise ValueError(f'Unexpected mip count: {path}')
                mips = count
                files[name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        tiers[str(scale)] = {'size': expected, 'files': files, 'mips': mips}
    return tiers


def publish_progress(output, recipe, rows, receipts):
    for name, content in (('index.tsv', '\n'.join(rows) + '\n'),
                          ('recipe.json', json.dumps(recipe, indent=2)),
                          ('receipts.json', json.dumps(receipts, indent=2))):
        dest = output / name
        temp = dest.with_suffix(dest.suffix + '.tmp')
        temp.write_text(content)
        temp.replace(dest)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--recipe', type=Path, required=True)
    ap.add_argument('--samples', type=Path, required=True)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--astcenc', type=Path)
    ap.add_argument('--formats', choices=('both', 'astc', 'rgba'), default='both')
    ap.add_argument('--resume', action='store_true')
    args = ap.parse_args()
    recipe = json.loads(args.recipe.read_text())
    if args.formats == 'astc' and not args.astcenc:
        ap.error('--formats astc needs --astcenc')
    formats = (False, True) if args.formats == 'both' and args.astcenc else \
              (True,) if args.formats == 'astc' else (False,)
    args.output.mkdir(parents=True, exist_ok=args.resume)
    (args.output / 'review').mkdir(exist_ok=args.resume)
    if (args.output / 'recipe.json').exists() and json.loads((args.output / 'recipe.json').read_text()) != recipe:
        raise ValueError('Cannot resume with a different recipe')
    if (args.output / 'receipts.json').exists() and not args.resume:
        raise ValueError('Output already has receipts')
    rows, receipts = ['OPENRUNE_TEXTURES_1'], []
    for asset in recipe['assets']:
        identity = asset['identity'].lower()
        assert identity.replace('.', '').replace('_', '').replace('-', '').isalnum()
        source = args.source / asset['source']
        source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        master_file = args.samples / asset['master']
        master_hash = hashlib.sha256(master_file.read_bytes()).hexdigest()
        if asset.get('source_sha256', source_hash) != source_hash or asset.get('master_sha256', master_hash) != master_hash:
            raise ValueError('Source/master changed since review: ' + identity)
        original = Image.open(source).convert('RGB')
        master = Image.open(master_file).convert('RGB')
        source_pixels = np.asarray(original)
        rows.append(f'{identity}\t{original.width}\t{original.height}\t{fingerprint(source_pixels)}')
        receipt = {'id': identity, 'source_sha256': source_hash, 'master_sha256': master_hash, 'tiers': {}}
        final = args.output / identity
        if final.exists():
            if not args.resume:
                raise ValueError('Refusing to overwrite prior asset: ' + identity)
            receipt['tiers'] = existing_tiers(final, original.size, formats)
            receipts.append(receipt)
            publish_progress(args.output, recipe, rows, receipts)
            print(identity, 'reused complete files', flush=True)
            continue
        staging = args.output / '.building' / identity
        if staging.exists():
            shutil.rmtree(staging)  # Only uncommitted outputs from this builder.
        staging.mkdir(parents=True)
        master_channels = None
        master_size = None
        for scale in (4, 2):
            factor = scale
            while max(original.size) * factor > 2048:
                factor //= 2
            size = tuple(n * factor for n in original.size)
            regions, names = region_map(asset, size)
            rgb = np.asarray(master.resize(size, Image.Resampling.LANCZOS), dtype=np.float32) / 255
            mode = 'wrap' if asset.get('wrap') else 'nearest'
            rgb = correct_color(original, rgb, regions, mode)
            rgb = blend_source_structure(rgb, original, asset.get('source_blend', 0))
            rgb = soften_stone_grain(rgb, original, regions, names, asset.get('stone_detail_gain', 1), mode)
            if asset.get('wrap'):
                rgb = wrap_edges(rgb)
            alpha = np.ones(rgb.shape[:2], dtype=np.float32)
            mask_path = source.with_suffix('.mask.png')
            if mask_path.exists():
                alpha = np.asarray(Image.open(mask_path).convert('L').resize(size, Image.Resampling.NEAREST), dtype=np.float32) / 255
            if asset.get('preserve_black'):
                if not mask_path.exists():
                    raise ValueError('preserve_black requires source mask: ' + identity)
                rgb = np.where((alpha > .5)[..., None], rgb, 0)
            normal, response = material_mips(rgb, regions, names, mode)
            channels = {'color': color_mips(rgb, np.ones_like(alpha)), 'masked': color_mips(rgb, alpha), 'normal': normal, 'response': response}
            if master_channels is None:
                master_channels, master_size = channels, size
            else:
                # Both quality tiers share one calibrated master and filtering
                # history; switching tiers cannot change material strength/color.
                first = (master_size[0] // size[0]).bit_length() - 1
                channels = {name: levels[first:] for name, levels in master_channels.items()}
                normal = channels['normal']
            dest = staging / f'{scale}x'; dest.mkdir(parents=True)
            files = {}
            for name, mips in channels.items():
                for compressed in formats:
                    path = dest / f'{name}{".astc" if compressed else ""}.ktx'
                    ktx(path, mips, args.astcenc if compressed else None, 6 if name in ('color', 'masked') else 4)
                    files[path.name] = {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
            receipt['tiers'][str(scale)] = {'size': size, 'files': files, 'mips': len(normal)}
            if scale == 4:
                Image.fromarray(byte_image(rgb)).save(args.output / 'review' / f'{identity}-color.png')
                Image.fromarray(normal[0]).save(args.output / 'review' / f'{identity}-normal-roughness.png')
                Image.fromarray(response[0]).save(args.output / 'review' / f'{identity}-response.png')
                receipt['mean_rgb_original'] = (source_pixels.mean((0, 1)) / 255).tolist()
                receipt['mean_rgb_corrected'] = rgb.mean((0, 1)).tolist()
            print(identity, scale, 'ready', flush=True)
        final.parent.mkdir(parents=True, exist_ok=True)
        staging.replace(final)
        receipts.append(receipt)
        publish_progress(args.output, recipe, rows, receipts)
    (args.output / 'README.txt').write_text('Private derivatives of owned Rune artwork. '+str(len(receipts))+' reviewed textures. Color-calibrated generative color; estimated normal/roughness and material-region response. Not full PBR. Original packages are required. See repository Docs/RunePort/TEXTURE-PIPELINE.md.\n')


if __name__ == '__main__':
    main()
