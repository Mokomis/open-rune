import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image
from build_remaster import blend_source_structure, color_mips, correct_color, material_mips, soften_stone_grain, wrap_edges, ktx


class RemasterTests(unittest.TestCase):
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


if __name__ == '__main__':
    unittest.main()
