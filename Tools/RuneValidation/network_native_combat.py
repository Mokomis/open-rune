"""Native combat through real encrypted windowed clients, optionally Android.

Uses isolated saves and private ephemeral invitations. --device restarts Open Rune;
run only when abandoning the current unsaved view is safe. No normal preferences
are changed. The fixture alone grants equipment/bloodlust; regular games do not.
"""
import argparse
import platform
import hashlib
import os
import json
import re
import subprocess
import tempfile
import time
from pathlib import Path
from network_proxy import ImpairedLink

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--game',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--fixture',choices=('dual','blood','bash'),required=True)
p.add_argument('--latency',type=int,default=0)
p.add_argument('--physics-mode',type=int,choices=(0,1,2),default=0)
p.add_argument('--device')
p.add_argument('--puppet-trace',action='store_true')
p.add_argument('--width',type=int,default=960)
p.add_argument('--height',type=int,default=600)
p.add_argument('--characters',nargs=2,default=['0:0','0:0'],help='Character ID:skin for the two desktop clients')
p.add_argument('--touch-gesture',choices=('leap','charge'))
p.add_argument('--host',default='127.0.0.1')
a=p.parse_args()
if not 0<=a.latency<=100:p.error('latency must be 0..100 ms each way')
if a.touch_gesture and (not a.device or a.fixture!='blood'):p.error('Touch gesture requires Android and blood fixture')
if a.device and a.host=='127.0.0.1':p.error('Android needs the reachable laptop LAN address')
a.output.mkdir(parents=True,exist_ok=False)
processes=[];logs=[];links=[]
adb=['adb','-s',a.device] if a.device else []
def device(*args):return subprocess.run(adb+['shell',*args],check=True,capture_output=True,text=True,timeout=20).stdout
with tempfile.TemporaryDirectory(prefix='open-rune-native-net-') as folder:
    root=Path(folder);invites=root/'invitations'
    def launch(name,args):
        saves=root/(name+'-saves');saves.mkdir();log=(a.output/(name+'.log')).open('w');logs.append(log)
        config=root/(name+'-config');(config/'SurrealEngine').mkdir(parents=True)
        proc=subprocess.Popen([str(a.binary.resolve()),'--direct',f'--save-directory={saves}',*(['--rune-puppet-trace']if a.puppet_trace else []),*args,str(a.game.resolve())],stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(config)))
        processes.append(proc);return proc
    try:
        host=launch('server',[f'--rune-server={invites}',f'--rune-net-address={a.host}','--rune-net-seconds=80',
            '--probe-seed=1',f'--rune-net-native-fixture={a.fixture}',f'--rune-physics={a.physics_mode}'])
        until=time.monotonic()+20
        while not (invites/'player2.rune-invite').exists():
            if host.poll() is not None or time.monotonic()>until:raise RuntimeError('No invitations; see server.log')
            time.sleep(.1)
        clients=[]
        for i in (1,2):
            invitation=invites/f'player{i}.rune-invite'
            if a.latency and not a.device:
                lines=invitation.read_text().splitlines();address,port=lines[1].split()
                link=ImpairedLink((address,int(port)),uplink_ms=a.latency,downlink_ms=a.latency,jitter_ms=8,loss=1,duplicate=1,seed=941+i)
                links.append(link);lines[1]=f'127.0.0.1 {link.port}';invitation.write_text('\n'.join(lines)+'\n')
            if a.device and i==2:
                # Keep earlier device captures intact, but never pull stale
                # masks from a different fixture into this run's evidence.
                probe=subprocess.run(adb+['shell','run-as','org.runeclassic.port','test','-d','files/native-combat-captures'],capture_output=True,timeout=20)
                if probe.returncode==0:
                    prior='files/prior-native-combat-'+str(time.time_ns())
                    device('run-as','org.runeclassic.port','mv','files/native-combat-captures',prior)
                elif probe.returncode!=1:raise RuntimeError('Cannot inspect isolated device captures')
                device('run-as','org.runeclassic.port','mkdir','-p','files/multiplayer')
                subprocess.run(adb+['shell','run-as','org.runeclassic.port','sh','-c',"'umask 077; cat > files/multiplayer/native-test.rune-invite'"],input=invitation.read_bytes(),check=True)
                if a.touch_gesture:
                    touchlog=(a.output/'touch-dispatch.log').open('w');logs.append(touchlog)
                    processes.append(subprocess.Popen(adb+['logcat','-T','1','-v','brief','-s','RuneTouchProbe:I'],stdout=touchlog,stderr=subprocess.STDOUT))
                device('am','force-stop','org.runeclassic.port')
                device('am','start','-W','-n','org.runeclassic.port/.MainActivity','--es','saveTest','network-native-combat',
                    '--es','networkInvite','native-test.rune-invite',
                    *(['--es','bloodlustGesture',a.touch_gesture,'--ez','touchProbe','true'] if a.touch_gesture else ['--es','networkNativeFixture',a.fixture]),'--ei','networkSeconds','25','--ei','physicsMode',str(a.physics_mode),'--ei','surfacePercent','100',*(['--ez','physicsTrace','true']if a.puppet_trace else []),
                    '--ez','upscale','false','--ei','msaa','2','--ei','profile','900','--ei','seed','1')
            else:
                captures=a.output/f'client{i}-frames';captures.mkdir()
                clients.append(launch(f'client{i}',[f'--rune-join={invitation}','--rune-net-seconds=45',f'--rune-net-native-fixture={a.fixture}',
                    f'--character={a.characters[i-1].split(":")[0]}',f'--character-skin={a.characters[i-1].split(":")[-1]}',f'--rune-net-native-captures={captures.resolve()}',f'--rune-physics={a.physics_mode}',f'--rune-net-width={a.width}',f'--rune-net-height={a.height}','--render-msaa=2','--maxfps=120','--profile-frames=900']))
        if a.device:
            until=time.monotonic()+55
            while True:
                text=device('run-as','org.runeclassic.port','cat','files/native-run.log')
                if ('OPEN_RUNE_CLIENT_STOP' if a.touch_gesture else 'OPEN_RUNE_NATIVE_CLIENT') in text:break
                if time.monotonic()>until:raise RuntimeError('Android native test did not finish')
                time.sleep(1)
            (a.output/'client2.log').write_text(text)
            names=[] if a.touch_gesture else device('run-as','org.runeclassic.port','ls','files/native-combat-captures').splitlines()
            captures=a.output/'client2-frames';captures.mkdir()
            for name in names:
                if re.fullmatch(r'native-\d+\.ppm',name):
                    with (captures/name).open('wb') as out:subprocess.run(adb+['exec-out','run-as','org.runeclassic.port','cat','files/native-combat-captures/'+name],stdout=out,check=True)
        for client in clients:assert client.wait(timeout=55)==0,'Client failure'
        assert host.wait(timeout=45)==0,'Host failure'
        for log in logs:log.flush()
        expected=(64|256|512) if a.physics_mode and a.fixture=='bash' else 192 if a.fixture=='bash' else 7 if a.fixture=='dual' else 40 if a.touch_gesture=='leap' else 56
        texts=[(a.output/f'client{i}.log').read_text() for i in (1,2)]
        for i,text in enumerate(texts,1):
            if a.touch_gesture and i==2:continue
            match=re.search(r'OPEN_RUNE_NATIVE_CLIENT fixture=\w+ mask=(\d+)',text)
            assert match and int(match[1])&expected==expected,f'Client {i} missed native states'
            assert 'DIRECT_LAUNCH_FAILED' not in text and 'native fatal:' not in text.lower()
        server=(a.output/'server.log').read_text()
        if a.puppet_trace and a.physics_mode and a.fixture=='dual':
            assert all('RUNE_PUPPET_VISIBLE' in text and 'replica=1' in text for text in texts),'No remote muscle reached the interactive renderer'
        if a.touch_gesture:
            kind=10 if a.touch_gesture=='leap' else 11
            assert re.search(fr'OPEN_RUNE_ACTION player=2 kind={kind} accepted=1',server),'Android touch move not accepted'
            assert 'BLOODLUST_TOUCH_DISPATCH '+a.touch_gesture in (a.output/'touch-dispatch.log').read_text(),'Missing real touch dispatch evidence'
        elif a.fixture=='bash':
            assert 'RUNE_BASH_HIT' in server and 'stagger=1' in server, 'Bash did not stagger target'
        elif a.fixture=='blood':
            for kind in (10,11):assert re.search(fr'OPEN_RUNE_ACTION player=\d+ kind={kind} accepted=1',server),f'Missing accepted action {kind}'
        result={'passed':True,'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest(),'returncodes':[proc.returncode for proc in [host,*clients]],'fixture':a.fixture,'physics_mode':a.physics_mode,'characters':a.characters,'touch_gesture':a.touch_gesture,'platform':('Android + '+platform.system()) if a.device else (platform.system()+' x2'),'latency_each_way_ms':a.latency,
            'expected_observed_mask':expected,'profiles':[re.findall(r'OPEN_RUNE_CLIENT_PROFILE[^\n]+',text) for text in texts]}
        (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    finally:
        for proc in processes:
            if proc.poll() is None:proc.terminate()
        for proc in processes:
            try:proc.wait(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
        for link in links:link.close()
        for log in logs:log.close()
        if a.device:device('run-as','org.runeclassic.port','rm','-f','files/multiplayer/native-test.rune-invite')
