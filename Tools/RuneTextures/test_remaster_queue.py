"""Check resume metadata and source integrity for the private art queue."""
import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image

import remaster_queue as queue


class QueueTest(unittest.TestCase):
    def test_duplicate_sources_keep_distinct_aliases_and_review_gate(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            source = base / 'source'
            (source / 'A').mkdir(parents=True)
            (source / 'B').mkdir()
            pixels = Image.new('RGB', (64, 64), (55, 43, 32))
            pixels.save(source / 'A' / 'first.png')
            pixels.save(source / 'B' / 'second.png')
            digest = queue.sha(source / 'A' / 'first.png')
            rows = []
            for directory, name, identity in (('source/A', 'first.png', 'A.First'),
                                               ('source/B', 'second.png', 'B.Second')):
                rows.append({'id': identity, 'status': 'exported', 'directory': directory,
                             'file': name, 'width': 64, 'height': 64,
                             'sha256': digest, 'animated': False, 'flags': 0})
            base.joinpath('inventory.json').write_text(json.dumps({
                'failures': [], 'original_packages_unchanged': True, 'textures': rows}))
            out = base / 'queue'
            queue.create(type('Args', (), {'source': base, 'output': out})())
            plan = json.loads((out / 'plan.json').read_text())
            self.assertEqual(plan['unique_generation_jobs'], 1)
            job = json.loads((out / 'jobs' / (digest + '.json')).read_text())
            self.assertEqual(len(job['aliases']), 2)

            wrong_shape = base / 'wrong-shape.png'
            Image.new('RGB', (256, 128), (56, 44, 34)).save(wrong_shape)
            queue.record_failed(type('Args', (), {'queue': out, 'job': digest,
                                                  'image': wrong_shape,
                                                  'generator': 'built-in image_gen',
                                                  'prompt_file': None,
                                                  'reason': 'Wrong aspect ratio'})())
            self.assertEqual(json.loads((out / 'jobs' / (digest + '.json')).read_text())['status'], 'pending')
            self.assertTrue((out / 'rejected' / digest / (queue.sha(wrong_shape) + '.json')).exists())

            originals = base / 'original-materials.json'
            queue.export_recipe(type('Args', (), {'queue': out, 'materials': None,
                                                  'output': originals, 'include_original': True})())
            original_assets = json.loads(originals.read_text())['assets']
            self.assertEqual(len(original_assets), 2)
            self.assertTrue(all('master' not in a for a in original_assets))
            self.assertEqual(json.loads((out / 'jobs' / (digest + '.json')).read_text())['status'], 'pending')

            generated = base / 'generated.png'
            Image.new('RGB', (256, 256), (56, 44, 34)).save(generated)
            prompt = base / 'prompt.txt'; prompt.write_text('Preserve the original.\n')
            args = type('Args', (), {'queue': out, 'job': digest, 'image': generated,
                                     'generator': 'built-in image_gen',
                                     'prompt_file': prompt, 'inspected': True})()
            queue.ingest(args)
            with self.assertRaises(ValueError):
                queue.decide(type('Args', (), {'queue': out, 'job': digest,
                                               'decision': 'approve', 'note': 'early'})())
            queue.audit(type('Args', (), {'queue': out, 'job': digest})())
            revised = base / 'revised.png'
            Image.new('RGB', (256, 256), (57, 45, 35)).save(revised)
            prior_hash = queue.sha(out / 'masters' / (digest + '.png'))
            queue.revise(type('Args', (), {'queue': out, 'job': digest,
                                           'image': revised, 'generator': 'built-in image_gen',
                                           'prompt_file': prompt, 'inspected': True})())
            archive = out / 'revisions' / digest / prior_hash
            self.assertEqual(queue.sha(archive / 'master.png'), prior_hash)
            self.assertTrue((archive / 'audit.json').exists())
            with self.assertRaises(ValueError):
                queue.decide(type('Args', (), {'queue': out, 'job': digest,
                                               'decision': 'approve', 'note': 'stale audit'})())
            queue.audit(type('Args', (), {'queue': out, 'job': digest})())
            queue.decide(type('Args', (), {'queue': out, 'job': digest,
                                           'decision': 'approve', 'note': 'inspected'})())
            finished = json.loads((out / 'jobs' / (digest + '.json')).read_text())
            self.assertEqual(finished['status'], 'approved')
            self.assertEqual(finished['generation_prompt'], 'Preserve the original.')
            self.assertEqual(finished['master_sha256'], queue.sha(out / finished['master']))
            exported = base / 'approved.json'
            queue.export_recipe(type('Args', (), {'queue': out, 'materials': None,
                                                  'output': exported})())
            packaged = json.loads(exported.read_text())['assets']
            self.assertEqual([a['identity'] for a in packaged], ['A.First', 'B.Second'])
            self.assertEqual([a['source'] for a in packaged], ['A/first.png', 'B/second.png'])
            self.assertTrue(all(a['master_sha256'] == finished['master_sha256'] for a in packaged))


if __name__ == '__main__':
    unittest.main()
