import importlib.util
from pathlib import Path
import struct,tempfile,unittest
import numpy as np
from scipy.io import wavfile
import remaster as rem


class ProcessingTests(unittest.TestCase):
    def test_stereo_timing_headroom_and_separation(self):
        rate=22050;t=np.arange(rate)/rate
        x=np.stack([.8*np.sin(2*np.pi*200*t),.45*np.sin(2*np.pi*3100*t)],axis=1)
        y=rem.process(x,rate,'impact');self.assertEqual(x.shape,y.shape)
        self.assertLess(rem.stats(y,rate)['true_peak_4x_dbfs'],-.99)
        # Distinct input channels remain distinct rather than becoming a stereo mix.
        self.assertLess(abs(np.corrcoef(y[:,0],y[:,1])[0,1]),.05)

    def test_loop_metadata_and_deterministic_pcm(self):
        rate=11025;t=np.arange(rate)/rate;x=(.4*np.sin(2*np.pi*100*t))[:,None]
        y=rem.process(x,rate,'fire',(0,rate-1))
        smpl=struct.pack('<15I',0,0,round(1e9/rate),60,0,0,0,1,0,0,0,0,rate-1,0,0)
        with tempfile.TemporaryDirectory() as d:
            a,b=Path(d)/'a.wav',Path(d)/'b.wav'
            rem.write_wav(a,rate,y,[(b'smpl',smpl)],42);rem.write_wav(b,rate,y,[(b'smpl',smpl)],42)
            self.assertEqual(a.read_bytes(),b.read_bytes())
            self.assertEqual([v for k,v in rem.chunks(a.read_bytes()) if k==b'smpl'],[smpl])
            sr,out=wavfile.read(a);self.assertEqual(sr,rate);self.assertEqual(len(out),len(x));self.assertEqual(out.dtype,np.dtype('int16'))

    def test_no_resampling_or_extra_tail(self):
        for rate in [11025,22050,44100,48000]:
            x=np.zeros((rate//2,1));x[100:150]=.8
            for profile in rem.PROFILES:self.assertEqual(rem.process(x,rate,profile).shape,x.shape)

    def test_invalid_and_silent_inputs(self):
        for x in [np.zeros((64,1)),np.full((64,1),np.nan)]:
            with self.assertRaises(ValueError):rem.process(x,22050,'impact')

    def test_unsafe_pcm_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):rem.write_wav(Path(d)/'bad.wav',22050,np.ones((10,1)))

if __name__=='__main__':unittest.main()
