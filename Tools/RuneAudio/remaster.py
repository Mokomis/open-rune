"""Deterministic offline sample remaster; run on Windows, no paid/AI service.

Keeps native rates, channels, frame counts and authored loop metadata. Emits
uninstalled replacement candidates plus level-matched listening comparisons.
"""
import os
os.environ.setdefault('OPENBLAS_NUM_THREADS','2')
os.environ.setdefault('OMP_NUM_THREADS','2')
import argparse, base64, hashlib, html, json, math, platform, struct, time
from pathlib import Path
import numpy as np
import scipy
from scipy import signal
from scipy.io import wavfile

PROFILES={
    'impact':dict(highpass=30,body=2.5,body_hz=160,presence=1.8,presence_hz=2700,transient=.16),
    'shield':dict(highpass=35,body=2.0,body_hz=190,presence=1.7,presence_hz=1900,transient=.14),
    'swing':dict(highpass=45,body=1.0,body_hz=180,presence=1.3,presence_hz=2400,transient=.08),
    'footstep':dict(highpass=40,body=1.5,body_hz=180,presence=1.1,presence_hz=1800,transient=.10),
    'fire':dict(highpass=30,body=1.2,body_hz=180,presence=1.0,presence_hz=2200,transient=0),
    'creature':dict(highpass=40,body=1.0,body_hz=180,presence=1.2,presence_hz=2300,transient=0),
}


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def db(value):return float(20*np.log10(max(float(value),1e-12)))
def rms(x):return float(np.sqrt(np.mean(np.square(x,dtype=np.float64))))


def chunks(data):
    if data[:4]!=b'RIFF' or data[8:12]!=b'WAVE':raise ValueError('Not a RIFF/WAVE sound')
    at=12
    while at+8<=len(data):
        key=data[at:at+4];size=struct.unpack_from('<I',data,at+4)[0];end=at+8+size
        if end>len(data):raise ValueError('Truncated WAV metadata')
        yield key,data[at+8:end];at=end+(size&1)


def write_wav(path,rate,x,metadata=(),seed=0):
    x=np.asarray(x,dtype=np.float64)
    if x.ndim==1:x=x[:,None]
    if not np.isfinite(x).all() or np.max(np.abs(x))>=1:raise ValueError('Unsafe output samples')
    rng=np.random.default_rng(seed)
    dither=(rng.random(x.shape)-rng.random(x.shape))/65536
    pcm=np.round(np.clip(x+dither,-.99994,.99994)*32768).astype('<i2')
    parts=[(b'fmt ',struct.pack('<HHIIHH',1,x.shape[1],rate,rate*x.shape[1]*2,x.shape[1]*2,16)),(b'data',pcm.tobytes())]
    parts.extend((key,value) for key,value in metadata if key in [b'smpl',b'cue ',b'LIST'])
    body=b'WAVE'+b''.join(key+struct.pack('<I',len(value))+value+(b'\0' if len(value)&1 else b'') for key,value in parts)
    path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'RIFF'+struct.pack('<I',len(body))+body)


def peak_eq(hz,gain,rate,q=.75):
    hz=min(hz,rate*.35);a=10**(gain/40);w=2*np.pi*hz/rate;alpha=np.sin(w)/(2*q);c=np.cos(w)
    b=np.array([1+alpha*a,-2*c,1-alpha*a]);d=np.array([1+alpha/a,-2*c,1-alpha/a]);return np.r_[b/d[0],d/d[0]][None,:]


def stats(x,rate):
    if x.ndim==1:x=x[:,None]
    peak=float(np.max(np.abs(x)));true=float(np.max(np.abs(signal.resample_poly(x,4,1,axis=0))))
    spectrum=np.abs(np.fft.rfft(x,axis=0))**2;frequencies=np.fft.rfftfreq(len(x),1/rate)
    energy=spectrum.sum()
    return dict(rms_dbfs=db(rms(x)),peak_dbfs=db(peak),true_peak_4x_dbfs=db(true),dc=float(np.max(np.abs(x.mean(axis=0)))),clipped_samples=int(np.count_nonzero(np.abs(x)>=.9999)),centroid_hz=float((spectrum*frequencies[:,None]).sum()/max(energy,1e-20)))


def process(x,rate,profile,loop=None):
    if x.ndim==1:x=x[:,None]
    if not len(x) or not np.isfinite(x).all():raise ValueError('Empty/nonfinite source')
    p=PROFILES[profile];original=x.astype(np.float64);y=original.copy()
    pad=min(len(y),max(128,int(rate*.25)))
    y=np.pad(y,((pad,0),(0,0)),mode='wrap' if loop else 'reflect')
    filters=[signal.butter(2,p['highpass'],btype='highpass',fs=rate,output='sos'),peak_eq(p['body_hz'],p['body'],rate),peak_eq(p['presence_hz'],p['presence'],rate)]
    for sos in filters:y=signal.sosfilt(sos,y,axis=0)
    y=y[pad:]
    if p['transient']:
        magnitude=np.max(np.abs(y),axis=1)
        def envelope(ms):
            a=np.exp(-1/(rate*ms*.001));return signal.lfilter([1-a],[1,-a],magnitude)
        fast,slow=envelope(2),envelope(18)
        attack=np.clip((fast-slow)/(slow+.025),0,1)
        y*=1+p['transient']*attack[:,None]
    # Keep original onset/loop seam samples; no trimming, timing shift or tail extension.
    width=min(int(rate*.004),len(y)//4)
    if width:
        w=np.linspace(0,1,width)[:,None];y[:width]=original[:width]*(1-w)+y[:width]*w
        y[-width:]=y[-width:]*(1-w)+original[-width:]*w
    if loop:
        start,end=loop
        for point in (start,end):
            lo=max(0,point-width);hi=min(len(y),point+width+1)
            weight=np.minimum(1,np.abs(np.arange(lo,hi)-point)/max(width,1))[:,None]
            y[lo:hi]=original[lo:hi]*(1-weight)+y[lo:hi]*weight
    source_rms=rms(original)
    if source_rms<1e-6:raise ValueError('Source is effectively silent')
    y*=source_rms/max(rms(y),1e-12)
    true_peak=float(np.max(np.abs(signal.resample_poly(y,4,1,axis=0))))
    y*=min(1,(10**(-1/20))/max(true_peak,1e-12))
    return y


def build(exports,selection,output):
    if output.exists():raise ValueError('Use a new versioned output folder')
    output.mkdir(parents=True)
    (output/'processing-source.py').write_bytes(Path(__file__).read_bytes())
    (output/'selection.json').write_text(json.dumps(selection,indent=2))
    if os.name=='nt':
        import ctypes
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(),0x4000)
    rows={}
    for path in exports.glob('*/manifest.json'):
        manifest=json.loads(path.read_text())
        for row in manifest['sounds']:rows[row['id']]=(path.parent,row,manifest['source_sha256'])
    result=dict(schema=1,kind='offline-dsp-pilot',host=platform.node(),platform=platform.platform(),numpy=np.__version__,scipy=scipy.__version__,processing_script_sha256=sha(__file__),profiles=PROFILES,assets=[],held=[],listening_review='pending',runtime_installed=False)
    started=time.monotonic()
    for item in selection:
        identity=item['id'];profile=item['profile']
        try:
            folder,row,package_hash=rows[identity]
            if row['status']!='exported':raise ValueError(row.get('reason','Not exported'))
            decoded,raw=folder/row['decoded_file'],folder/row['raw_file']
            if sha(decoded)!=row['decoded_file_sha256'] or sha(raw)!=row['raw_file_sha256']:raise ValueError('Source hash mismatch')
            rate,x=wavfile.read(decoded);x=np.asarray(x,dtype=np.float64)
            if x.ndim==1:x=x[:,None]
            assert len(x)==row['frames'] and rate==row['sample_rate'] and x.shape[1]==row['channels']
            metadata=list(chunks(raw.read_bytes())) if row['format']=='wav' else []
            loop=(row['loop_start'],row['loop_end']) if row['looped'] else None
            if loop and not 0<=loop[0]<loop[1]<len(x):raise ValueError('Invalid authored loop points')
            y=process(x,rate,profile,loop);assert y.shape==x.shape
            seed=int(hashlib.sha256(identity.encode()).hexdigest()[:8],16)
            replacement=output/'replacements'/(identity+'.wav');write_wav(replacement,rate,y,metadata,seed)
            # RMS-matched A/B at a common attenuation also safe for the original peaks.
            common=min(rms(x),rms(y))*.85
            ox=x*common/rms(x);oy=y*common/rms(y)
            peak=max(np.max(np.abs(signal.resample_poly(ox,4,1,axis=0))),np.max(np.abs(signal.resample_poly(oy,4,1,axis=0))))
            gain=min(1,.88/max(peak,1e-12));ox*=gain;oy*=gain
            original=output/'comparisons'/(identity+'.original.wav');remaster=output/'comparisons'/(identity+'.remastered.wav')
            write_wav(original,rate,ox,metadata,seed);write_wav(remaster,rate,oy,metadata,seed)
            preview_a=wavfile.read(original)[1].astype(float)/32768
            preview_b=wavfile.read(remaster)[1].astype(float)/32768
            ab_difference=abs(db(rms(preview_a))-db(rms(preview_b)))
            assert ab_difference<.1,'A/B playback levels differ'
            after_rate,actual=wavfile.read(replacement);actual=actual.astype(float)/32768
            if actual.ndim==1:actual=actual[:,None]
            assert after_rate==rate and actual.shape==x.shape
            before_stats,after_stats=stats(x,rate),stats(actual,rate)
            assert after_stats['clipped_samples']==0 and after_stats['true_peak_4x_dbfs']<-.95
            for key in [b'smpl',b'cue ',b'LIST']:
                assert [v for k,v in metadata if k==key]==[v for k,v in chunks(replacement.read_bytes()) if k==key]
            correlation=float(np.corrcoef(x.ravel(),actual.ravel())[0,1]);suspect=[]
            if before_stats['clipped_samples']:suspect.append('Original contains full-scale samples; this pass does not reconstruct clipping.')
            if correlation<.9:suspect.append('Larger waveform change: listen for altered character.')
            if abs(after_stats['rms_dbfs']-before_stats['rms_dbfs'])>1.5:suspect.append('Headroom protection reduced level by more than 1.5 dB.')
            if loop:
                seam_before=float(np.max(np.abs(x[loop[1]]-x[loop[0]])));seam_after=float(np.max(np.abs(actual[loop[1]]-actual[loop[0]])))
                assert seam_after<=seam_before*1.3+.002,'Loop seam worsened'
            entry=dict(id=identity,profile=profile,source_package_sha256=package_hash,raw_sha256=sha(raw),decoded_sha256=sha(decoded),replacement=str(replacement.relative_to(output)).replace('\\','/'),replacement_sha256=sha(replacement),original_preview=str(original.relative_to(output)).replace('\\','/'),remastered_preview=str(remaster.relative_to(output)).replace('\\','/'),frames=len(x),sample_rate=rate,channels=x.shape[1],looped=row['looped'],loop_start=row['loop_start'],loop_end=row['loop_end'],before=before_stats,after=after_stats,correlation=correlation,ab_rms_difference_db=ab_difference,suspect=suspect,review='technical checks passed; listening pending')
            result['assets'].append(entry)
        except Exception as error:result['held'].append(dict(id=identity,reason=str(error)))
    result['elapsed_seconds']=time.monotonic()-started
    for row in result['assets']:
        row['label']=next(item.get('label',row['id']) for item in selection if item['id']==row['id'])
    (output/'manifest.json').write_text(json.dumps(result,indent=2))
    make_page(output,result)
    print(json.dumps({'processed':len(result['assets']),'held':result['held'],'seconds':result['elapsed_seconds']}))
    return result


def make_page(output,result):
    cards=[]
    for row in result['assets']:
        name=html.escape(row.get('label',row['id']))
        # The desktop HTML preview may not grant access to sibling files.
        def embedded(field):
            return 'data:audio/wav;base64,'+base64.b64encode((output/row[field]).read_bytes()).decode('ascii')
        a,b=embedded('original_preview'),embedded('remastered_preview')
        loop=' loop' if row['looped'] and row['loop_start']==0 and row['loop_end']==row['frames']-1 else ''
        notes=html.escape(' '.join(row['suspect']) or 'No automatic review flags.')
        cards.append(f'<article><h2>{name}</h2><p>{row["profile"]} · {row["sample_rate"]} Hz · {row["channels"]} channel(s) · level matched</p><audio controls{loop} preload="none" src="{a}"></audio><div><button data-src="{a}">Play original</button><button data-src="{b}">Play remastered</button></div><p class="playback-status" role="status"></p><details><summary>Review notes</summary><p>{notes} Listening review remains pending; correlation flags can also reflect filter phase changes.</p></details></article>')
    page='''<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Open Rune · Sound comparison</title><style>body{background:#101314;color:#eee8db;font:17px system-ui;max-width:960px;margin:36px auto;padding:0 20px}h1{color:#edbd68}h2{font-size:18px;overflow-wrap:anywhere}article{border:1px solid #766342;padding:18px;margin:18px 0;background:#192022}p{color:#bbb9af;line-height:1.5}button{padding:12px 20px;background:#292e30;color:#eee8db;border:1px solid #ad8b4c;margin:12px 10px 0 0;cursor:pointer}button.active{background:#775927}audio{width:100%}</style><h1>Open Rune · Sound remaster pilot</h1><p>Press Play original or Play remastered. Switching during playback keeps your position. Audio is embedded in this page; no neighboring files or internet connection are needed. Previews have matched RMS levels. No sounds have been installed into the game. This is EQ, DC cleanup and transient shaping, not generative AI restoration.</p>'''+''.join(cards)+'''<script>
const players=[...document.querySelectorAll('audio')];
document.querySelectorAll('article').forEach(card=>{
 const audio=card.querySelector('audio'),status=card.querySelector('.playback-status'),buttons=[...card.querySelectorAll('button')];
 buttons[0].classList.add('active');
 audio.addEventListener('play',()=>players.forEach(other=>{if(other!==audio)other.pause();}));
 audio.addEventListener('error',()=>{status.textContent='This preview could not load the sound. Open this HTML file in Safari, Edge or Chrome.';});
 buttons.forEach(button=>button.onclick=()=>{
  const changed=audio.getAttribute('src')!==button.dataset.src;
  const at=!audio.paused&&!audio.ended?audio.currentTime:0;
  status.textContent='';
  if(changed){audio.src=button.dataset.src;audio.onloadedmetadata=()=>{if(Number.isFinite(audio.duration))audio.currentTime=Math.min(at,Math.max(0,audio.duration-.01));};}
  else if(audio.ended||audio.paused)audio.currentTime=0;
  buttons.forEach(b=>b.classList.toggle('active',b===button));
  // Call play directly in the click handler to retain the browser's user gesture.
  audio.play().catch(error=>{status.textContent='Playback was blocked. Try the audio player’s play control, or open this file in your browser.';});
 });
});
</script>'''
    (output/'comparison.html').write_text(page,encoding='utf-8')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exports',type=Path,required=True);p.add_argument('--selection',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    build(a.exports,json.loads(a.selection.read_text()),a.output)
