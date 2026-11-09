import tempfile
import unittest
import hashlib
import json
import subprocess
import sys
import struct
from pathlib import Path

import numpy as np
from PIL import Image
from build_remaster import crop_master, repair_mask_edges, blend_source_structure, color_mips, correct_color, material_mips, relief_mips, soften_stone_grain, wrap_edges, ktx, identity_key, channel_formats
from verify_remaster import validate_ktx


class RemasterTests(unittest.TestCase):
    def test_reviewed_padding_crop_keeps_the_uv_strip_and_raw_master(self):
        raw = Image.new('RGB', (32, 64), (0, 0, 0))
        raw.paste((90, 60, 30), (8, 0, 24, 64))
        strip = crop_master(raw, [8, 0, 24, 64])
        self.assertEqual(strip.size, (16, 64))
        np.testing.assert_array_equal(np.asarray(strip), np.tile([90, 60, 30], (64, 16, 1)))
        self.assertEqual(raw.getpixel((0, 0)), (0, 0, 0))
        for box in ([0, 0, 33, 64], [-1, 0, 32, 64], [8, 0, 8, 64], [0, 0, 16.5, 64], [False, 0, 16, 64]):
            with self.assertRaises(ValueError):crop_master(raw, box)
        with self.assertRaises(ValueError):crop_master(None, [0, 0, 16, 64])

    def test_shifted_mask_key_does_not_leak_into_foreground(self):
        source = np.full((16, 16, 3), [0, 0, 255], np.uint8)
        source[:, :8] = [90, 60, 30]
        mask = np.zeros((16, 16), np.uint8); mask[:, :8] = 255
        generated = np.asarray(Image.fromarray(source).resize((64, 64), Image.Resampling.NEAREST), dtype=float) / 255
        generated[:, 28:32] = [0, 0, 1]  # shifted artwork leaves blue inside the source silhouette
        reference, cleaned = repair_mask_edges(Image.fromarray(source), generated, Image.fromarray(mask))
        self.assertTrue(np.all(cleaned[:, :32, 2] < .2))
        self.assertTrue(np.all(np.asarray(reference)[..., 2] == 30))
        levels = color_mips(cleaned, np.asarray(Image.fromarray(mask).resize((64, 64), Image.Resampling.NEAREST)) / 255)
        self.assertTrue(all(not np.any((m[..., 2] > 160) & (m[..., 3] >= 128)) for m in levels))
        self.assertEqual(np.mean(levels[0][..., 3] >= 128), .5)

    def test_mask_repair_retains_genuine_blue_foreground(self):
        source = np.full((16, 16, 3), [0, 0, 255], np.uint8); source[:, :8] = [90, 60, 30]
        source[4:8, 2:6] = [0, 0, 255]
        mask = np.zeros((16, 16), np.uint8); mask[:, :8] = 255
        rgb = source.astype(float) / 255
        _, cleaned = repair_mask_edges(Image.fromarray(source), rgb, Image.fromarray(mask))
        np.testing.assert_array_equal(cleaned[4:8, 2:6], rgb[4:8, 2:6])
        _, opaque = repair_mask_edges(Image.fromarray(source), rgb, Image.new('L', (16, 16), 255))
        np.testing.assert_array_equal(opaque, rgb)

    def test_lossless_exception_retains_portable_fallback(self):
        asset = {'rgba_only': {'1': ['relief']}}
        self.assertEqual(channel_formats(asset, 1, 'relief', (False, True)), (False,))
        self.assertEqual(channel_formats(asset, 4, 'relief', (False, True)), (False, True))
        with self.assertRaises(ValueError):
            channel_formats(asset, 1, 'relief', (True,))
        with self.assertRaises(ValueError):
            channel_formats({'rgba_only': {'3': ['relief']}}, 1, 'relief', (False, True))
    def test_original_names_are_encoded_without_collisions(self):
        self.assertEqual(identity_key('Galleon.Inside ship'), 'galleon.inside~20ship')
        self.assertNotEqual(identity_key('Galleon.Inside ship'), identity_key('Galleon.Inside~20ship'))
        for name in ('../bad', 'a/b', '.a', 'a.', 'a\\b'):
            with self.assertRaises(ValueError):
                identity_key(name)
    def test_linear_light_mips(self):
        rgb = np.zeros((2, 2, 3)); rgb[:, 0] = 1
        levels = color_mips(rgb, np.ones((2, 2)))
        self.assertTrue(np.all(np.abs(levels[-1][0, 0, :3].astype(int) - 188) <= 1))

    def test_mask_no_white_halo(self):
        rgb = np.ones((16, 16, 3)); rgb[:, :8] = [1, 0, 0]
        alpha = np.zeros((16, 16)); alpha[:, :8] = 1
        levels = color_mips(rgb, alpha)
        for level in levels:
            self.assertEqual(int(level[..., 1:3].max()), 0)
        for level in levels[:-1]:
            self.assertAlmostEqual(np.mean(level[..., 3] >= 128), .5)

    def test_empty_mask_and_rectangular_chain(self):
        levels = color_mips(np.ones((8, 32, 3)), np.zeros((8, 32)))
        self.assertEqual([x.shape[:2] for x in levels], [(8, 32), (4, 16), (2, 8), (1, 4), (1, 2), (1, 1)])
        self.assertTrue(all(not np.any(x[..., 3]) for x in levels))

    def test_regional_color_and_detail(self):
        rgb = np.full((64, 64, 3), .5); rgb[::2] += .05
        original = np.full((64, 64, 3), 30, np.uint8); original[:, 32:] = 180
        regions = np.zeros((64, 64), int); regions[:, 32:] = 1
        corrected = correct_color(Image.fromarray(original), rgb, regions, 'nearest')
        self.assertLess(np.abs(corrected[:, :32].mean() - 30/255), .005)
        self.assertLess(np.abs(corrected[:, 32:].mean() - 180/255), .005)
        self.assertGreater(corrected[::2, :32].mean() - corrected[1::2, :32].mean(), .03)

    def test_periodic_edges(self):
        rgb = wrap_edges(np.random.default_rng(2).random((64, 64, 3)))
        np.testing.assert_allclose(rgb[0], rgb[-1], atol=1e-6)
        np.testing.assert_allclose(rgb[:, 0], rgb[:, -1], atol=1e-6)

    def test_stone_grain_gain_keeps_boundaries_and_other_materials(self):
        rgb = np.full((128, 128, 3), .45)
        rgb[::2] += .08
        rgb[:, 64:] += .2
        regions = np.zeros((128, 128), int); regions[:, 64:] = 1
        source = np.full((128, 128, 3), 115, np.uint8); source[:, 64:] = 166
        result = soften_stone_grain(rgb, Image.fromarray(source), regions, ['stone', 'wood'], .7, 'nearest')
        self.assertLess(np.std(result[32:96, 16:48]), np.std(rgb[32:96, 16:48]) * .8)
        np.testing.assert_allclose(result[:, 64:], rgb[:, 64:])
        np.testing.assert_allclose(result[:, 63], rgb[:, 63])

    def test_source_structure_blend(self):
        generated = np.full((8, 8, 3), .8)
        source = Image.fromarray(np.full((8, 8, 3), 51, np.uint8))
        result = blend_source_structure(generated, source, .5)
        np.testing.assert_allclose(result, .5)

    def test_normal_lengths_and_material_regions(self):
        rgb = np.random.default_rng(3).random((64, 64, 3))
        regions = np.zeros((64, 64), int); regions[:, 32:] = 1
        normals, response = material_mips(rgb, regions, ['wood', 'metal'], 'nearest')
        for pixels in normals:
            n = pixels[..., :3].astype(float)/127.5-1
            self.assertLess(np.abs(np.linalg.norm(n, axis=2)-1).max(), .014)
        self.assertGreater(response[0][:, 32:, 1].mean(), response[0][:, :32, 1].mean()+100)
        self.assertLess(normals[0][:, 32:, 3].mean(), normals[0][:, :32, 3].mean())

    def test_depth_ignores_flat_painted_brightness_and_preserves_neutral(self):
        regions = np.zeros((32, 64), int); regions[:, 32:] = 1
        outputs = []
        for brightness in (.1, .8):
            rgb = np.full((32, 64, 3), brightness)
            normals, _ = material_mips(rgb, regions, ['stone', 'skin'], 'nearest')
            levels = relief_mips(rgb, regions, ['stone', 'skin'], 'nearest', normals)
            outputs.append(levels)
            self.assertTrue(np.all(levels[0][:, 32:, 3] == 255))
            self.assertLess(levels[0][:, :32, 3].mean(), 255)
            self.assertEqual(levels[-1].shape, (1, 1, 4))
        for a, b in zip(*outputs):
            np.testing.assert_array_equal(a, b)

    def test_depth_off_and_transparent_areas_are_flat(self):
        rgb = np.random.default_rng(23).random((64, 32, 3))
        regions = np.zeros((64, 32), int)
        normals, _ = material_mips(rgb, regions, ['wood'], 'wrap')
        levels = relief_mips(rgb, regions, ['wood'], 'wrap', normals, gain=0)
        self.assertTrue(all(np.all(m[..., 3] == 255) for m in levels))
        alpha = np.ones((64, 32)); alpha[:, :16] = 0
        first = relief_mips(rgb, regions, ['wood'], 'wrap', normals, alpha)[0]
        self.assertTrue(np.all(first[:, :16, 3] == 255))
        with self.assertRaises(ValueError):
            relief_mips(rgb, regions, ['wood'], 'wrap', normals, gain=2)

    def test_complete_pack_original_remaster_tiers_resume_and_corruption(self):
        tool = Path(__file__).parent
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = root / 'source'; source.mkdir()
            samples = root / 'masters'; samples.mkdir()
            rgb = np.random.default_rng(7).integers(35, 180, (32, 64, 3), dtype=np.uint8)
            Image.fromarray(rgb).save(source / 'stone.png')
            Image.fromarray(rgb).resize((256, 128)).save(samples / 'master.png')
            before = hashlib.sha256((source / 'stone.png').read_bytes()).hexdigest()
            recipe = {'version': 1, 'assets': [
                {'identity': 'Stone.Test', 'source': 'stone.png', 'master': 'master.png', 'material': 'stone', 'wrap': True},
                {'identity': 'Cloth.Original', 'source': 'stone.png', 'material': 'cloth'}]}
            (root / 'recipe.json').write_text(json.dumps(recipe))
            out = root / 'pack'
            cmd = [sys.executable, str(tool / 'build_remaster.py'), '--recipe', str(root / 'recipe.json'),
                   '--source', str(source), '--samples', str(samples), '--output', str(out), '--formats', 'rgba']
            subprocess.run(cmd, check=True, capture_output=True)
            subprocess.run([sys.executable, str(tool / 'verify_remaster.py'), '--pack', str(out),
                            '--source', str(source), '--samples', str(samples)], check=True, capture_output=True)
            index = out / 'index.tsv'; saved_index = index.read_text()
            index.write_text('\n'.join(saved_index.splitlines()[:-1])+'\n')
            self.assertNotEqual(subprocess.run([sys.executable, str(tool / 'verify_remaster.py'),
                '--pack', str(out), '--source', str(source), '--samples', str(samples)], capture_output=True).returncode, 0)
            index.write_text(saved_index)
            self.assertFalse((out / 'cloth.original/2x').exists())
            self.assertFalse((out / 'stone.test/1x/color.ktx').exists())
            self.assertEqual(hashlib.sha256((source / 'stone.png').read_bytes()).hexdigest(), before)
            # The lower remaster tier must be the same mip sequence, not a new
            # independent material estimate with different depth/contrast.
            for channel in ('normal', 'response', 'relief', 'color', 'masked'):
                hi = (out / 'stone.test/4x' / (channel + '.ktx')).read_bytes()
                lo = (out / 'stone.test/2x' / (channel + '.ktx')).read_bytes()
                first_length, = struct.unpack_from('<I', hi, 64)
                self.assertEqual(hi[68+first_length:], lo[64:])
            subprocess.run(cmd + ['--resume'], check=True, capture_output=True)
            profile = json.loads((out / 'pipeline.json').read_text()); profile['version'] = 0
            (out / 'pipeline.json').write_text(json.dumps(profile))
            self.assertNotEqual(subprocess.run(cmd + ['--resume'], capture_output=True).returncode, 0)
            broken = bytearray((out / 'stone.test/1x/relief.ktx').read_bytes())
            broken[-1:] = b''
            with self.assertRaises(AssertionError):
                validate_ktx(broken, {'size': [64, 32], 'mips': 7})


if __name__ == '__main__':
    unittest.main()
