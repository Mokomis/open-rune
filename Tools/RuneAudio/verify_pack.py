"""Validate a remaster candidate without loading it into a running game."""
import argparse,hashlib,json,struct,wave
from pathlib import Path


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def verify(root):
    root=root.resolve();m=json.loads((root/'manifest.json').read_text());assert m['assets']
    assert sha(root/'processing-source.py')==m['processing_script_sha256']
    if 'helpers_sha256' in m:assert sha(root/'remaster.py')==m['helpers_sha256']
    ids=set()
    for row in m['assets']:
        assert row['id'] not in ids;ids.add(row['id'])
        for field in ['replacement','original_preview','remastered_preview']:
            path=(root/row[field]).resolve();assert path.is_relative_to(root)
            with wave.open(str(path),'rb') as f:
                assert f.getframerate()==row['sample_rate'] and f.getnchannels()==row['channels'] and f.getnframes()==row['frames'] and f.getsampwidth()==2
            if field=='replacement':assert sha(path)==row['replacement_sha256']
        assert row['after']['clipped_samples']==0 and row['after']['true_peak_4x_dbfs']<-.95
        assert row['ab_rms_difference_db']<.1
    return dict(passed=True,assets=len(ids),held=len(m['held']),loops=sum(r['looped'] for r in m['assets']),largest_ab_rms_difference_db=max(r['ab_rms_difference_db'] for r in m['assets']),listening_review=m['listening_review'])


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('pack',type=Path);a=p.parse_args();print(json.dumps(verify(a.pack),indent=2))
