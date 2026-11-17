import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from prepare_release import prepare, identity

class ReleaseTests(unittest.TestCase):
    def test_matching_identity(self):
        value=identity()
        self.assertGreater(value['compatibility']['network_protocol'],0)
        self.assertGreater(value['compatibility']['gameplay_rules'],0)
        self.assertEqual(prepare('v'+value['version'],[],True)['version'],value['version'])
    def test_mismatched_or_development_tag(self):
        with self.assertRaises(ValueError):prepare('v999.0.0',[])
        with patch('prepare_release.identity', return_value={**identity(), 'version': '1.0.0-rc.1'}):
            with self.assertRaises(ValueError):
                prepare('v1.0.0-rc.1', [])
        with patch('prepare_release.identity', return_value={**identity(), 'version': '1.0.0'}):
            self.assertEqual(prepare('v1.0.0', [])['channel'], 'stable')
    def test_asset_hash_and_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'client.zip';path.write_bytes(b'abc')
            value=prepare('v'+identity()['version'],[path],True)
            self.assertEqual(value['assets'][0]['sha256'],'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
            with self.assertRaises(ValueError):prepare('v'+identity()['version'],[path,path],True)
if __name__=='__main__':unittest.main()
