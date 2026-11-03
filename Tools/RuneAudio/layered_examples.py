"""Two intentionally re-authored impact examples, processed locally on Windows.

Combines owned Rune recordings with original procedural percussion. No external
recording, paid service, model or runtime processing is required.
"""
import argparse, hashlib, json, math, os, platform
from pathlib import Path
import numpy as np
from scipy import signal
from scipy.io import wavfile
import remaster as audio

RECIPES=[
    dict(id='WeaponsSnd.ImpFlesh.impfleshaxe01',label='Heavy axe hit',kind='weapon',description='Original axe contact, a low impact thump, a short wood crack, a slowed flesh layer and a brief textured splatter.',
         layers=[('original','WeaponsSnd.ImpFlesh.impfleshaxe01',.45),('flesh','WeaponsSnd.ImpFlesh.impfleshsword07',.55),('crack','WeaponsSnd.ImpWood.impactwood01',.40),('body',None,.50),('texture',None,.20)]),
    dict(id='WeaponsSnd.Shields.shield01',label='Shield impact',kind='shield',description='Original shield contact, a wooden knock, a metal strike, a short low thump and a newly synthesized shield resonance.',
         layers=[('original','WeaponsSnd.Shields.shield01',.30),('wood','WeaponsSnd.ImpWood.impactwood06',.55),('metal','WeaponsSnd.ImpMetal.impactmetal02',.35),('resonance',None,.60),('body',None,.30),('texture',None,.16)]),
]


def normalize(x):return x/max(audio.rms(x),1e-10)
def band(x,rate,low=None,high=None):
    if low:x=signal.sosfilt(signal.butter(2,low,btype='highpass',fs=rate,output='sos'),x,axis=0)
    if high:x=signal.sosfilt(signal.butter(2,min(high,rate*.43),btype='lowpass',fs=rate,output='sos'),x,axis=0)
    return x


def onset(x):
    v=np.max(np.abs(x),axis=1);where=np.flatnonzero(v>=max(.002,float(v.max())*.025))
    return int(where[0]) if len(where) else 0


def fitted(x,frames,start):
    out=np.zeros((frames,1));n=min(len(x),frames-start)
    if n>0:out[start:start+n]=x[:n]
    return out


def body(rate,frames,kind):
    t=np.arange(frames)/rate
    begin,end,tau=(145,60,.035) if kind=='weapon' else (210,115,.025)
    phase=2*np.pi*(end*t+(begin-end)*tau*(1-np.exp(-t/tau)))
    envelope=(1-np.exp(-t/.0008))*np.exp(-t/(.045 if kind=='weapon' else .032))
    # Harmonics keep the impact audible on small speakers as well as headphones.
    return ((np.sin(phase)+.35*np.sin(2*phase)+.12*np.sin(3*phase))*envelope)[:,None]


def modal_ring(rate,frames):
    t=np.arange(frames)/rate;y=np.zeros(frames)
    for hz,level,decay in [(410,.36,.15),(665,.22,.11),(1020,.14,.09),(1760,.11,.065),(2600,.08,.045),(4300,.04,.025)]:
        y+=level*np.sin(2*np.pi*hz*t)*np.exp(-t/decay)
    return (y*(1-np.exp(-t/.0006)))[:,None]


def render(recipe,load):
    original,rate,source=load(recipe['id']);frames=len(original);start=onset(original)
    rng=np.random.default_rng(48291 if recipe['kind']=='weapon' else 81613)
    stems=[];weights=[]
    for kind,identity,weight in recipe['layers']:
        if kind=='original':stem=original.copy()
        elif identity:
            x,sr,_=load(identity);x=x[onset(x):]
            divisor=math.gcd(sr,rate);x=signal.resample_poly(x,rate//divisor,sr//divisor,axis=0)
            if kind=='flesh':
                # Slowing this secondary donor adds weight without changing the target event.
                x=signal.resample_poly(x,5,4,axis=0);x=band(x,rate,90,3600)
                t=np.arange(len(x))/rate;x*=np.exp(-t/.16)[:,None]
            elif kind=='crack':
                x=band(x,rate,900,8500);t=np.arange(len(x))/rate;x*=np.exp(-t/.035)[:,None]
            elif kind=='wood':
                x=band(x,rate,100,2800);t=np.arange(len(x))/rate;x*=np.exp(-t/.075)[:,None]
            elif kind=='metal':
                x=band(x,rate,1400,8500);t=np.arange(len(x))/rate;x*=np.exp(-t/.10)[:,None]
            stem=fitted(x,frames,start)
        else:
            if kind=='body':x=body(rate,frames,recipe['kind'])
            elif kind=='resonance':x=modal_ring(rate,frames)
            else:
                x=band(rng.standard_normal((frames,1)),rate,1500,8500);t=np.arange(frames)/rate
                duration=.055 if recipe['kind']=='weapon' else .018
                x*=((1-np.exp(-t/.0005))*np.exp(-t/duration))[:,None]
                if recipe['kind']=='weapon':x*=.3+np.sin(2*np.pi*83*t)[:,None]**4
            stem=fitted(x,frames,start)
        # Every added layer is tied to the original event onset, not a new delay.
        stem=normalize(stem);stems.append(stem);weights.append(weight)
    mixed=sum(s*w for s,w in zip(stems,weights));mixed/=max(float(np.max(np.abs(mixed))),1e-10)
    mixed=np.tanh(mixed*1.3)/np.tanh(1.3)
    # Finish inside the existing clip; preserve quiet leading samples and avoid a cut tail.
    fade=min(int(rate*.012),frames//10);mixed[-fade:]*=np.linspace(1,0,fade)[:,None]
    mixed*=audio.rms(original)/max(audio.rms(mixed),1e-12)
    ceiling=10**(-1/20);peak=float(np.max(np.abs(signal.resample_poly(mixed,4,1,axis=0))))
    mixed*=min(1,ceiling/max(peak,1e-12))
    return original,mixed,rate,source,stems,start


def build(exports,output):
    if output.exists():raise ValueError('Use a new output folder')
    output.mkdir(parents=True)
    if os.name=='nt':
        import ctypes
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(),0x4000)
    manifest=json.loads((exports/'WeaponsSnd/manifest.json').read_text());rows={r['id']:r for r in manifest['sounds']}
    donors={}
    def load(identity):
        row=rows[identity];p=exports/'WeaponsSnd'/row['decoded_file']
        assert audio.sha(p)==row['decoded_file_sha256']
        sr,x=wavfile.read(p);x=x.astype(float)
        if x.ndim==1:x=x[:,None]
        assert x.shape[1]==1 and np.isfinite(x).all()
        donors[identity]=dict(decoded_sha256=audio.sha(p),raw_sha256=row['raw_file_sha256'])
        return x,sr,row
    result=dict(schema=1,kind='layered-impact-examples',host=platform.node(),platform=platform.platform(),processing_script_sha256=audio.sha(__file__),helpers_sha256=audio.sha(audio.__file__),source_package_sha256=manifest['source_sha256'],assets=[],held=[],recipes=RECIPES,donors=donors,listening_review='pending',runtime_installed=False)
    (output/'processing-source.py').write_bytes(Path(__file__).read_bytes());(output/'remaster.py').write_bytes(Path(audio.__file__).read_bytes())
    for recipe in RECIPES:
        original,y,rate,source,stems,start=render(recipe,load);identity=recipe['id'];seed=38
        replacement=output/'replacements'/(identity+'.wav');audio.write_wav(replacement,rate,y,seed=seed)
        peak=max(float(np.max(np.abs(signal.resample_poly(original,4,1,axis=0)))),float(np.max(np.abs(signal.resample_poly(y,4,1,axis=0)))))
        common=min(audio.rms(original),audio.rms(y))*.85
        a=original*common/audio.rms(original);b=y*common/audio.rms(y)
        peak=max(float(np.max(np.abs(signal.resample_poly(a,4,1,axis=0)))),float(np.max(np.abs(signal.resample_poly(b,4,1,axis=0)))))
        gain=min(1,.88/max(peak,1e-12));a*=gain;b*=gain
        pa=output/'comparisons'/(identity+'.original.wav');pb=output/'comparisons'/(identity+'.layered.wav')
        audio.write_wav(pa,rate,a,seed=seed);audio.write_wav(pb,rate,b,seed=seed)
        qa=wavfile.read(pa)[1].astype(float)/32768;qb=wavfile.read(pb)[1].astype(float)/32768
        actual=wavfile.read(replacement)[1].astype(float)/32768
        before,after=audio.stats(original,rate),audio.stats(actual,rate)
        delta=abs(audio.db(audio.rms(qa))-audio.db(audio.rms(qb)));assert delta<.1
        correlation=float(np.corrcoef(qa.ravel(),qb.ravel())[0,1]);assert abs(correlation)<.85,'Example is too close to the original waveform'
        assert len(actual)==len(original) and after['clipped_samples']==0 and after['true_peak_4x_dbfs']<-.95
        stem_notes=[]
        for (kind,donor,weight),stem in zip(recipe['layers'],stems):
            p=output/'stems'/identity/(kind+'.wav');s=stem*.75/max(np.max(np.abs(stem)),1e-12);audio.write_wav(p,rate,s,seed=seed)
            stem_notes.append(dict(kind=kind,donor=donor,weight=weight,file=str(p.relative_to(output)).replace('\\','/'),sha256=audio.sha(p)))
        sequence=np.concatenate([a,np.zeros((int(rate*.35),1)),b]);audio.write_wav(output/'comparisons'/(identity+'.original-then-layered.wav'),rate,sequence,seed=seed)
        result['assets'].append(dict(id=identity,label=recipe['label'],description=recipe['description'],profile='layered '+recipe['kind'],replacement=str(replacement.relative_to(output)).replace('\\','/'),replacement_sha256=audio.sha(replacement),original_preview=str(pa.relative_to(output)).replace('\\','/'),remastered_preview=str(pb.relative_to(output)).replace('\\','/'),frames=len(original),sample_rate=rate,channels=1,looped=False,loop_start=0,loop_end=0,event_onset_sample=start,before=before,after=after,ab_rms_difference_db=delta,correlation=correlation,stems=stem_notes,suspect=[],review='Technical checks passed; listening pending.'))
    (output/'manifest.json').write_text(json.dumps(result,indent=2));audio.make_page(output,result)
    p=output/'comparison.html';text=p.read_text(encoding='utf-8').replace('Play remastered','Play layered').replace('Sound remaster pilot','Layered impact examples').replace('This is EQ, DC cleanup and transient shaping, not generative AI restoration.','These two examples combine original Rune recordings with newly synthesized impact and resonance layers. No external recordings or generative AI were used.')
    # Describe the actual construction without exposing internal asset names in the main cards.
    for row in result['assets']:text=text.replace('<h2>'+row['label']+'</h2>','<h2>'+row['label']+'</h2><p>'+row['description']+'</p>')
    p.write_text(text,encoding='utf-8')
    print(json.dumps({'examples':len(result['assets']),'correlations':[r['correlation'] for r in result['assets']],'level_differences_db':[r['ab_rms_difference_db'] for r in result['assets']]}))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exports',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();build(a.exports,a.output)
