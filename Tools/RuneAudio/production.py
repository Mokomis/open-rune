"""Build an optional layered gameplay-effects pack from owned Rune exports.

All processing is offline. Every inventory row is classified; speech, score,
menu cues and unclassified material remain original. Never edits game packages.
"""
import argparse, collections, hashlib, json, math, os, platform, re, shutil, struct, time, warnings
from pathlib import Path
import numpy as np
from scipy import signal
from scipy.io import wavfile
import remaster as audio
import layered_examples as layered


def classify(identity):
    package,group,*parts=identity.split('.');p,g,n=package.lower(),group.lower(),'.'.join(parts).lower()
    if p in ['musicalsnd','songsnd','voiceoversnd']:return None,'Music or spoken dialogue'
    if p=='creaturessnd' or (p=='addon' and g not in ['ambient','doors']):
        if g in ['various','taunts','voice overs','musical','arena','headball'] or 'word' in n or any(k in n for k in ['taunt','mumble','chant','speech','dialog']):return None,'Dialogue/announcer/context-sensitive voice'
        combat=any(k in n for k in ['attack','hit','death','hurt','pain','roar','growl','breath','gasp','jump','throw','charge','drown','berzerk'])
        beast=g in ['mech','dangler','spiderbot','crab','fish','snowbeast','beetle','tubestriker','zombie','lizard','snakeman']
        if combat or (beast and 'ambient' in n):return ('beast' if beast else 'vocal'),'Creature combat vocalization'
        return None,'Unclassified creature voice retained'
    if p=='weaponssnd':
        if g=='shields' and n.startswith('shield'):return 'shield','Layered shield contact'
        if g=='impflesh':return 'flesh','Layered flesh contact'
        if g in ['impmetal','swords']:return 'metal','Metal contact/resonance'
        if g in ['impstone','impearth','impice','impwood','impcrashes']:return 'impact','Material impact/crash'
        if g in ['swings','throws']:return 'air','Weapon movement'
        if g=='powerups':return 'magic','Rune/weapon effect'
        return 'mechanical','Weapon handling/drop'
    if p=='footstepssnd':return 'footstep:'+g,'Surface-specific movement'
    if p=='doorssnd' or (p=='addon' and g=='doors'):return 'door:'+g,'Door material/movement'
    if p=='environmentalsnd' or (p=='addon' and g=='ambient'):
        if g=='fire':return 'fire','Fire/torch texture'
        if g in ['water','bubbles']:return 'water','Water movement'
        if g=='steam':return 'steam','Steam texture'
        if g=='birds':return 'natural','Natural ambience'
        if g in ['machines','scifi']:return 'machine','Mechanical ambience'
        if g=='rumbles':return 'rumble','Low environmental movement'
        return 'ambient','Environmental texture'
    if p=='murmursnd':
        if g in ['animals','birds','critters','wolves','insects']:return 'natural','Natural ambience'
        if g=='water':return 'water','Water movement'
        if g in ['windgust','steamburst']:return 'ambient','Wind/steam movement'
        if g in ['rocks','ice','eruptions','bumps','scuffle']:return 'impact','Environmental impact'
        return 'mechanical','Creak/movement'
    if p=='othersnd':
        if g in ['menu','silence','instruments']:return None,'Menu/silence/instrument cue retained'
        if g in ['regens','respawns','pickups']:return 'magic','Pickup/respawn cue'
        if g=='gibs':return 'flesh','Gore contact'
        if g=='explosions':return 'impact','Explosion/body'
        return 'mechanical','Object interaction'
    return None,'Unclassified sound retained'


def digest(path,kind='sha256'):return hashlib.new(kind,path.read_bytes()).hexdigest()


def render(x,rate,kind,seed,loop):
    frames,channels=x.shape;rng=np.random.default_rng(seed);t=np.arange(frames)/rate
    basekind=kind.split(':')[0]
    pad=min(frames,max(128,int(rate*.15)))
    magnitude=np.abs(x)
    # The texture follows the authored rhythm, rather than inventing new events.
    envelope=signal.lfilter([1-np.exp(-1/(rate*.003))],[1,-np.exp(-1/(rate*.003))],np.pad(magnitude,((pad,0),(0,0)),mode='wrap' if loop else 'reflect'),axis=0)[pad:]
    envelope/=max(float(np.max(envelope)),1e-12)
    noise=rng.standard_normal((frames,channels));noise=layered.band(np.pad(noise,((pad,0),(0,0)),mode='wrap'),rate,400,rate*.40)[pad:]
    onset=layered.onset(x);body=layered.fitted(layered.body(rate,frames,'weapon'),frames,onset)
    if channels>1:body=np.repeat(body,channels,axis=1)
    mix=.68*layered.normalize(x)
    def add(stem,weight):
        nonlocal mix
        if audio.rms(stem)>1e-8:mix+=weight*layered.normalize(stem)
    if basekind in ['impact','flesh','shield','metal','mechanical','door','footstep']:
        if loop or basekind in ['footstep','mechanical','door']:
            f=190 if basekind=='footstep' else 230
            body=(np.sin(2*np.pi*f*t)+.3*np.sin(2*np.pi*f*2*t))[:,None]*envelope
        if basekind=='flesh':
            add(body,.40);add(noise*envelope**.6,.28)
        elif basekind in ['shield','metal']:
            ring=layered.modal_ring(rate,frames)
            if channels>1:ring=np.repeat(ring,channels,axis=1)
            add(layered.fitted(ring,frames,onset) if not loop else ring*envelope,.52);add(body,.30);add(noise*envelope,.18)
        elif basekind=='footstep':
            material=kind.split(':')[-1];low,high=(100,1600) if material in ['snow','mud','earth','rope'] else (450,7000)
            texture=layered.band(noise,rate,low,high)*envelope
            add(body,.22 if material=='water' else .35);add(texture,.30)
        else:
            add(body,.35);add(noise*envelope,.27)
            if 'metal' in kind or 'grill' in kind:
                ring=np.sin(2*np.pi*650*t)[:,None]*envelope;add(ring,.12)
    elif basekind in ['fire','water','steam','ambient','machine','rumble']:
        lo,hi={'fire':(1400,8500),'water':(350,7000),'steam':(1500,8500),'ambient':(120,3500),'machine':(150,4500),'rumble':(55,900)}[basekind]
        texture=layered.band(noise,rate,lo,min(hi,rate*.4))*(.2+.8*envelope)
        if basekind=='fire':
            crackles=np.zeros((frames,channels))
            for at in rng.integers(0,frames,size=max(2,int(frames/rate*7))):
                n=min(frames-at,max(8,int(rate*.009)));crackles[at:at+n]+=noise[at:at+n]*np.exp(-np.arange(n)/(rate*.002))[:,None]
            add(crackles,.16);add(texture,.32)
        elif basekind=='machine':
            modes=(np.sin(2*np.pi*147*t)+.35*np.sin(2*np.pi*347*t))[:,None]*envelope
            add(modes,.14);add(texture,.27)
        else:add(texture,.32)
    elif basekind=='air':add(layered.band(noise,rate,600,8000)*envelope,.55)
    elif basekind=='magic':
        shimmer=(np.sin(2*np.pi*660*t)+.3*np.sin(2*np.pi*1047*t)+.15*np.sin(2*np.pi*1568*t))[:,None]*envelope
        add(shimmer,.20);add(noise*envelope,.24)
    elif basekind in ['beast','vocal','natural']:
        # Preserve the actor's actual utterance and pitch; add only gated texture/harmonics.
        harmonic=np.tanh(3*x)/3-x
        add(harmonic,.28 if basekind=='beast' else .17)
        add(layered.band(noise,rate,900,6000)*envelope,.20 if basekind=='beast' else .08)
    else:raise ValueError('Unknown profile: '+kind)
    mix/=max(float(np.max(np.abs(mix))),1e-12)
    mix=np.tanh(mix*1.15)/np.tanh(1.15)
    width=min(int(rate*.006),frames//6)
    if width:
        w=np.linspace(0,1,width)[:,None]
        # Normalize original into mix space before preserving boundary character.
        edge=x/max(float(np.max(np.abs(x))),1e-12)
        mix[:width]=edge[:width]*(1-w)+mix[:width]*w
        mix[-width:]=mix[-width:]*(1-w)+edge[-width:]*w
        if loop:
            for point in loop:
                lo=max(0,point-width);hi=min(frames,point+width+1);weight=np.minimum(1,np.abs(np.arange(lo,hi)-point)/max(width,1))[:,None]
                mix[lo:hi]=edge[lo:hi]*(1-weight)+mix[lo:hi]*weight
    mix*=audio.rms(x)/max(audio.rms(mix),1e-12)
    peak=float(np.max(np.abs(signal.resample_poly(mix,4,1,axis=0))));mix*=min(1,10**(-1/20)/max(peak,1e-12))
    if loop and width:
        # Restore the authored seam at its actual level, after mix normalization.
        for point in loop:
            lo=max(0,point-width);hi=min(frames,point+width+1);weight=np.minimum(1,np.abs(np.arange(lo,hi)-point)/width)[:,None]
            mix[lo:hi]=x[lo:hi]*(1-weight)+mix[lo:hi]*weight
        peak=float(np.max(np.abs(signal.resample_poly(mix,4,1,axis=0))));mix*=min(1,10**(-1/20)/max(peak,1e-12))
    if not loop and onset:
        # New ambience/noise layers must not anticipate an authored one-shot cue.
        mix[:onset]=x[:onset]
    return mix


def build(roots,output,approved):
    if output.exists():raise ValueError('Use a new production pack directory')
    output.mkdir(parents=True)
    if os.name=='nt':
        import ctypes
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(),0x4000)
    warnings.filterwarnings('ignore',category=wavfile.WavFileWarning)
    inventory=[];source_hashes={}
    for root in roots:
        for path in sorted(root.glob('*/manifest.json')):
            m=json.loads(path.read_text());source_hashes[m['package']]=m['source_sha256']
            inventory.extend((path.parent,row) for row in m['sounds'])
    approved_rows={r['id']:r for r in json.loads((approved/'manifest.json').read_text())['assets']}
    report=dict(schema=1,pack='OpenRune-Layered-v1',method='offline layered effects',host=platform.node(),numpy=np.__version__,source_packages=source_hashes,assets=[],preserved=[],held=[],suspect=[],listening_review='User approved two layered examples; bulk collection requires listening review.',runtime_installed=False)
    runtime=[];preview_rows=[];preview_count=collections.Counter();started=time.monotonic()
    for script in [Path(__file__),Path(audio.__file__),Path(layered.__file__)]:shutil.copy2(script,output/script.name)
    for number,(folder,row) in enumerate(inventory,1):
        identity=row['id'];kind,reason=classify(identity)
        if not kind:report['preserved'].append(dict(id=identity,reason=reason));continue
        try:
            if row['status']!='exported':raise ValueError(row.get('reason','Export failed'))
            raw=folder/row['raw_file'];decoded=folder/row['decoded_file']
            if digest(raw)!=row['raw_file_sha256'] or digest(decoded)!=row['decoded_file_sha256']:raise ValueError('Source hash mismatch')
            rate,x=wavfile.read(decoded);x=x.astype(np.float64)
            if x.ndim==1:x=x[:,None]
            if audio.rms(x)<1e-6:report['preserved'].append(dict(id=identity,reason='Silent/placeholder sound'));continue
            if x.shape!=(row['frames'],row['channels']) or not np.isfinite(x).all():raise ValueError('Invalid decoded dimensions')
            loop=(row['loop_start'],row['loop_end']) if row['looped'] else None
            if loop and not 0<=loop[0]<loop[1]<len(x):raise ValueError('Invalid loop range')
            seed=int(hashlib.sha256(identity.encode()).hexdigest()[:8],16)
            metadata=list(audio.chunks(raw.read_bytes())) if row['format']=='wav' else []
            dest=output/'replacements'/(identity+'.wav')
            if identity in approved_rows:
                candidate=approved/approved_rows[identity]['replacement'];assert digest(candidate)==approved_rows[identity]['replacement_sha256']
                dest.parent.mkdir(exist_ok=True);shutil.copy2(candidate,dest)
            else:
                y=render(x,rate,kind,seed,loop);audio.write_wav(dest,rate,y,metadata,seed)
            sr,actual=wavfile.read(dest);actual=actual.astype(float)/32768
            if actual.ndim==1:actual=actual[:,None]
            if sr!=rate or actual.shape!=x.shape:raise ValueError('Replacement timing/channels changed')
            before,after=audio.stats(x,rate),audio.stats(actual,rate)
            if after['clipped_samples'] or after['true_peak_4x_dbfs']>-.94:raise ValueError('Clipped or unsafe true peak')
            if row['looped']:
                smpl=[v for k,v in metadata if k==b'smpl'];new_smpl=[v for k,v in audio.chunks(dest.read_bytes()) if k==b'smpl']
                if smpl!=new_smpl:raise ValueError('Loop metadata changed')
                first,last=loop;oldstep=float(np.max(np.abs(x[last]-x[first])));newstep=float(np.max(np.abs(actual[last]-actual[first])))
                if newstep>oldstep*1.3+.002:raise ValueError('Loop seam worsened')
            notes=[]
            if abs(after['rms_dbfs']-before['rms_dbfs'])>2:notes.append('Peak protection changes average level by over 2 dB.')
            corr=float(np.corrcoef(x.ravel(),actual.ravel())[0,1])
            if corr<.4:notes.append('Large waveform change; check material/voice character.')
            entry=dict(id=identity,profile=kind,reason=reason,frames=len(x),sample_rate=rate,channels=x.shape[1],looped=row['looped'],loop_start=row['loop_start'],loop_end=row['loop_end'],replacement=str(dest.relative_to(output)).replace('\\','/'),replacement_sha256=digest(dest),raw_sha256=digest(raw),before=before,after=after,correlation=corr,suspect=notes)
            report['assets'].append(entry)
            if notes:report['suspect'].append(dict(id=identity,notes=notes))
            runtime.append(dict(id=identity,source_sha1=digest(raw,'sha1'),file_sha1=digest(dest,'sha1'),file=entry['replacement'],frames=len(x),sample_rate=rate,channels=x.shape[1],looped=row['looped'],loop_start=row['loop_start'],loop_end=row['loop_end']))
            preview_group=kind.split(':')[0]
            if preview_count[preview_group]<2:
                common=min(audio.rms(x),audio.rms(actual))*.8;a=x*common/audio.rms(x);b=actual*common/audio.rms(actual)
                peak=max(np.max(np.abs(signal.resample_poly(a,4,1,axis=0))),np.max(np.abs(signal.resample_poly(b,4,1,axis=0))));gain=min(1,.87/max(peak,1e-12));a*=gain;b*=gain
                pa=output/'comparisons'/(identity+'.original.wav');pb=output/'comparisons'/(identity+'.layered.wav');audio.write_wav(pa,rate,a,metadata,seed);audio.write_wav(pb,rate,b,metadata,seed)
                preview_rows.append(dict(entry,label=kind.replace(':',' · ').title()+' — '+identity.split('.')[-1],original_preview=str(pa.relative_to(output)).replace('\\','/'),remastered_preview=str(pb.relative_to(output)).replace('\\','/')));preview_count[preview_group]+=1
        except Exception as error:
            report['held'].append(dict(id=identity,reason=str(error)))
        if number%100==0:
            print(json.dumps(dict(visited=number,total=len(inventory),processed=len(report['assets']),held=len(report['held']))),flush=True)
            (output/'progress.json').write_text(json.dumps(dict(visited=number,total=len(inventory),processed=len(report['assets']),held=report['held']),indent=2))
    report.update(total_inventory=len(inventory),elapsed_seconds=time.monotonic()-started,profile_counts=dict(collections.Counter(r['profile'] for r in report['assets'])))
    (output/'manifest.json').write_text(json.dumps(report,indent=2));(output/'runtime.json').write_text(json.dumps(dict(schema=1,name='Open Rune layered sounds',assets=runtime),separators=(',',':')))
    (output/'suspect-review.json').write_text(json.dumps(report['suspect'],indent=2));(output/'preview-manifest.json').write_text(json.dumps(dict(assets=preview_rows),indent=2))
    audio.make_page(output,dict(assets=preview_rows));page=output/'comparison.html';page.write_text(page.read_text(encoding='utf-8').replace('Play remastered','Play layered').replace('Sound remaster pilot','Layered sounds · collection samples').replace('This is EQ, DC cleanup and transient shaping, not generative AI restoration.','These collection samples add source-aware impact, texture or resonance layers. Dialogue and music are retained.'),encoding='utf-8')
    print(json.dumps(dict(processed=len(runtime),preserved=len(report['preserved']),held=report['held'],suspect=len(report['suspect']),seconds=report['elapsed_seconds'])),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exports',type=Path,nargs='+',required=True);p.add_argument('--approved',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();build(a.exports,a.output,a.approved)
