import unittest
import numpy as np
import production
import remaster as audio


class ProductionTests(unittest.TestCase):
    def test_spoken_and_music_stay_original(self):
        for name in ['VoiceOverSnd.Odin.hello','MusicalSnd.ME.cue01','SongSnd.Theme.track01','CreaturesSnd.Dwarves.word54','CreaturesSnd.Taunts.tauntsword','Addon.Arena.start01','OtherSnd.Silence.silence01','OtherSnd.Menu.click01']:
            self.assertIsNone(production.classify(name)[0],name)
        self.assertEqual(production.classify('WeaponsSnd.ImpFlesh.impfleshaxe01')[0],'flesh')

    def test_no_added_sound_before_authored_onset(self):
        rate=22050;x=np.zeros((rate//2,1));t=np.arange(2000)/rate;x[3000:5000,0]=.2*np.sin(2*np.pi*500*t)
        at=production.layered.onset(x)
        for kind in ['fire','ambient','shield','footstep:snow']:
            y=production.render(x,rate,kind,72,None);self.assertTrue(np.array_equal(y[:at],x[:at]),kind)

    def test_loops_stereo_shape_and_headroom(self):
        rate=22050;t=np.arange(rate//2)/rate;x=np.stack([.2*np.sin(2*np.pi*120*t),.12*np.sin(2*np.pi*330*t)],axis=1)
        for kind in ['fire','water','ambient','machine']:
            y=production.render(x,rate,kind,72,(0,len(x)-1));self.assertEqual(y.shape,x.shape)
            self.assertLessEqual(np.max(np.abs(y[-1]-y[0])),np.max(np.abs(x[-1]-x[0]))*1.3+.002)
            self.assertLess(audio.stats(y,rate)['true_peak_4x_dbfs'],-.94)
            self.assertTrue(np.array_equal(y,production.render(x,rate,kind,72,(0,len(x)-1))))


if __name__=='__main__':unittest.main()
