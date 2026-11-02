"""One rendered native/4x TDM client, selected characters and isolated LAN peers.

No device, public listing, normal saves/configuration, or long-lived host.
Invitations and credentials are temporary and are never copied to the report.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
from network_proxy import ImpairedLink


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--game',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--impaired',action='store_true')
    p.add_argument('--motion',action='store_true',help='Exercise rendered movement/jump and one moving scripted peer')
    p.add_argument('--smoothing',type=int,choices=(0,1),default=1)
    p.add_argument('--outage-seconds',type=float,default=0)
    a=p.parse_args();a.binary=a.binary.resolve();a.game=a.game.resolve();a.output=a.output.resolve()
    if not 0<=a.outage_seconds<=5 or (a.outage_seconds and not a.impaired):p.error('Outage needs --impaired and 0..5 seconds')
    a.output.mkdir(parents=True,exist_ok=False)
    processes=[];logs=[];links=[];result={'passed':False,'binary_sha256':hashlib.sha256(a.binary.read_bytes()).hexdigest(),'impaired':a.impaired,'motion':a.motion,'smoothing':a.smoothing,'outage_seconds':a.outage_seconds}
    try:
        with tempfile.TemporaryDirectory(prefix='rune-diagnostics-') as tmp:
            root=Path(tmp)
            def launch(name,flags):
                saves=root/name/'saves';saves.mkdir(parents=True)
                config=root/name/'config';(config/'SurrealEngine').mkdir(parents=True)
                log=(a.output/(name+'.log')).open('w');logs.append(log)
                process=subprocess.Popen([str(a.binary),'--direct',f'--save-directory={saves}',*flags,str(a.game)],
                    env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(config)),stdout=log,stderr=subprocess.STDOUT)
                processes.append(process);return process
            host=launch('host',[f'--rune-server={root/"invites"}','--rune-net-mode=tdm','--rune-net-maps=DM-Hildir','--rune-net-players=4',
                '--rune-net-seconds=45','--rune-net-tag-fixture','--rune-net-diagnostics','--rune-net-frag-limit=0','--rune-net-time-limit=120'])
            deadline=time.monotonic()+25
            while not (root/'invites'/'player4.rune-invite').is_file():
                if host.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Host startup failed')
                time.sleep(.05)
            invites=[]
            for i in range(1,5):
                invite=root/'invites'/f'player{i}.rune-invite'
                if a.impaired:
                    lines=invite.read_text().splitlines();address,port=lines[1].split()
                    link=ImpairedLink((address,int(port)),uplink_ms=75,downlink_ms=75,jitter_ms=15,loss=3,duplicate=2,seed=5560+i,outage_after=11 if a.outage_seconds else 0,outage_seconds=a.outage_seconds)
                    links.append(link);lines[1]=f'127.0.0.1 {link.port}';invite.write_text('\n'.join(lines)+'\n')
                invites.append(invite)
            for i in range(2,5):launch(f'bot{i}',[f'--rune-join={invites[i-1]}','--rune-net-bot','--rune-net-bot-motion' if a.motion and i==3 else '--rune-net-bot-idle','--rune-net-seconds=32',f'--character={i+2}'])
            client=launch('client',[f'--rune-join={invites[0]}','--character=11','--character-skin=1',f'--rune-net-seconds={22 if a.outage_seconds else 16}',
                '--render-msaa=2','--maxfps=120','--rune-net-diagnostics','--profile-frames=300',f'--rune-net-smoothing={a.smoothing}',
                *(['--rune-net-test-input',f'--rune-net-motion-trace={a.output/"motion.csv"}'] if a.motion else []),f'--capture-frame={a.output/"client.ppm"}'])
            if client.wait(timeout=40)!=0:raise RuntimeError('Rendered client failed')
            if host.poll() is not None:raise RuntimeError('Host exited during the rendered check')
            for log in logs:log.flush()
            text=(a.output/'client.log').read_text()
            if 'OPEN_RUNE_CONNECTION ended=Left match' not in text:raise RuntimeError('Rendered session ended unexpectedly')
            tags=re.findall(r'OPEN_RUNE_TEAM_TAG player=(\d+) pawn=(\d+) generation=(\d+) team=(\d+)',text)
            if not any(slot!=pawn for slot,pawn,_,_ in tags):raise RuntimeError('No rendered tag for a remapped character')
            if not re.search(r'RTT: \d+ ms / variation',text):raise RuntimeError('No acknowledged RTT measurement')
            if 'Transport KiB/s:' not in text or 'Frame ms:' not in text:raise RuntimeError('Diagnostics did not publish')
            if a.motion and not re.search(r'air_frames=[1-9]\d*',text):raise RuntimeError('No ordinary falling prediction frames')
            if a.outage_seconds and ('OPEN_RUNE_CONNECTION waiting=1' not in text or 'OPEN_RUNE_CONNECTION waiting=0' not in text):raise RuntimeError('Outage did not pause and recover the rendered client')
            if 'RUNE_CLIENT_PROFILE' not in text or 'msaa=2 scale=1.000000' not in text:raise RuntimeError('Missing native / 4x profile')
            if (a.output/'client.ppm').stat().st_size<10000:raise RuntimeError('Missing rendered capture')
            result.update(passed=True,tags=tags,client_returncode=client.returncode)
    except Exception as e:
        result['error']=str(e)
    finally:
        for proc in processes:
            if proc.poll() is None:proc.terminate()
        for proc in processes:
            try:proc.wait(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
        for log in logs:log.close()
        for link in links:link.close()
        result['links']=[link.stats for link in links]
        (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result));return 0 if result['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
