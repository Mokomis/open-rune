"""Verify every playable file and coverage classification in a production pack."""
import argparse,hashlib,json,wave
from pathlib import Path


def digest(p,kind):return hashlib.new(kind,p.read_bytes()).hexdigest()
def verify(root):
    root=root.resolve();m=json.loads((root/'manifest.json').read_text());runtime=json.loads((root/'runtime.json').read_text());entries={r['id']:r for r in runtime['assets']}
    assert len(entries)==len(runtime['assets'])==len(m['assets'])
    classified=[r['id'] for key in ['assets','preserved','held'] for r in m[key]];assert len(set(classified))==len(classified)==m['total_inventory']
    assert set(entries).isdisjoint(r['id'] for r in m['preserved'])
    for row in m['assets']:
        entry=entries[row['id']];p=(root/entry['file']).resolve();assert p.is_relative_to(root)
        assert digest(p,'sha1')==entry['file_sha1'] and digest(p,'sha256')==row['replacement_sha256']
        with wave.open(str(p),'rb') as w:
            assert w.getnframes()==entry['frames'] and w.getframerate()==entry['sample_rate'] and w.getnchannels()==entry['channels'] and w.getsampwidth()==2
        assert row['after']['clipped_samples']==0 and row['after']['true_peak_4x_dbfs']<-.94
    return dict(passed=True,total=m['total_inventory'],replacements=len(entries),preserved=len(m['preserved']),held=m['held'],suspect=len(m['suspect']),loops=sum(r['looped'] for r in runtime['assets']),replacement_bytes=sum((root/r['file']).stat().st_size for r in runtime['assets']),profiles=m['profile_counts'])


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('pack',type=Path);a=p.parse_args();print(json.dumps(verify(a.pack),indent=2))
