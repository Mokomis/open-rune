"""Laptop-only production server/replica checks. No Android or normal saves.

Use --map-audit for startup/spawn capture on every shipped multiplayer map.
Use --mode and --maps for authenticated bots and complete match/travel checks.
A passing startup is deliberately not reported as objective or match completion.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile
import time
from pathlib import Path
from network_proxy import ImpairedLink


def run(args, mode, maps, label, audit=False):
    output=args.output/label
    output.mkdir(parents=True,exist_ok=False)
    processes=[];logs=[];links=[];endpoints={}
    result={'mode':mode,'maps':maps,'players':args.players,'audit_only':audit,'abrupt':args.abrupt,'late_join_seconds':args.late_join,'outage_seconds':args.outage_seconds,'passed':False,'binary_sha256':hashlib.sha256(args.binary.read_bytes()).hexdigest(),'server_options':args.server_option,'characters':args.characters,'client_seconds':args.seconds,'time_limit_seconds':args.time_limit,'stress':args.stress,'tactical_bots':args.tactical_bots,'bot_options':args.bot_option,'require_markers':args.require_marker,'impairment':dict(one_way_ms=75,jitter_ms=15,loss_percent=3,duplicate_percent=2) if args.impaired else None}
    with tempfile.TemporaryDirectory(prefix='multiplayer-',dir=args.scratch) as tmp:
        root=Path(tmp)
        def launch(name,options):
            saves=root/name/'saves';saves.mkdir(parents=True)
            config=root/name/'config';(config/'SurrealEngine').mkdir(parents=True)
            env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(config))
            log=(output/(name+'.log')).open('w');logs.append(log)
            cmd=[str(args.binary),'--direct',f'--save-directory={saves}',*options,str(args.game)]
            p=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,env=env);processes.append(p);return p
        def character_options(i):
            if not args.characters:return []
            character,skin=args.characters[(i-1)%len(args.characters)].split(':')
            return [f'--character={character}',f'--character-skin={skin}']
        try:
            duration=1 if audit else args.seconds
            began=time.monotonic()
            server=launch('server',[f'--rune-server={root/"invites"}',f'--rune-net-mode={mode}',
                f'--rune-net-maps={",".join(maps)}',f'--rune-net-players={args.players}',
                f'--rune-net-seconds={duration+(40 if args.reconnect else 10) if not audit else duration}',
                '--rune-net-intermission=2',f'--rune-net-time-limit={args.time_limit}',
                '--rune-net-frag-limit=0',*args.server_option])
            deadline=time.monotonic()+30
            while not (root/'invites'/f'player{args.players}.rune-invite').exists():
                if server.poll() is not None:raise RuntimeError('server startup failed')
                if time.monotonic()>deadline:raise RuntimeError('server startup timeout')
                time.sleep(.05)
            if not audit:
                clients=[]
                for i in range(1,args.players+1):
                    if args.late_join and i==args.players:time.sleep(args.late_join)
                    invitation=root/'invites'/f'player{i}.rune-invite'
                    if args.impaired:
                        lines=invitation.read_text().splitlines();address,port=lines[1].split();endpoints[i]=(address,int(port))
                        link=ImpairedLink((address,int(port)),uplink_ms=75,downlink_ms=75,jitter_ms=15,loss=3,duplicate=2,seed=891+i,outage_after=15 if args.outage_seconds else 0,outage_seconds=args.outage_seconds)
                        links.append(link);lines[1]=f'127.0.0.1 {link.port}';invitation.write_text('\n'.join(lines)+'\n')
                    clients.append(launch(f'client{i}',[f'--rune-join={invitation}','--rune-net-bot',
                        f'--rune-net-seconds={duration}',*character_options(i),*args.bot_option,*(['--rune-net-bot-idle'] if not args.tactical_bots and not args.stress else []),*(['--rune-net-bot-stress'] if args.stress else []),*(['--rune-net-leave-after=6'] if args.reconnect and not args.abrupt and i==1 else []),*(['--rune-net-test-headball-input'] if mode=='headball' and not args.tactical_bots else [])]))
                if args.reconnect:
                    if args.abrupt:
                        deadline=time.monotonic()+30
                        while 'OPEN_RUNE_BOT_JOINED' not in (output/'client1.log').read_text():
                            if clients[0].poll() is not None or time.monotonic()>deadline:raise RuntimeError('abrupt client never joined')
                            time.sleep(.1)
                        time.sleep(6);clients[0].kill();clients[0].wait()
                    elif clients[0].wait(timeout=duration+5)!=0:raise RuntimeError('departing client failed')
                    deadline=time.monotonic()+45
                    markers=['OPEN_RUNE_PLAYER_DISCONNECTED id=1'] if args.abrupt else ['OPEN_RUNE_PLAYER_LEFT id=1','OPEN_RUNE_PLAYER_DISCONNECTED id=1']
                    while not any(marker in (output/'server.log').read_text() for marker in markers):
                        if server.poll() is not None or time.monotonic()>deadline:raise RuntimeError('server did not release departing slot')
                        time.sleep(.1)
                    result['departure_observed']=next(marker for marker in markers if marker in (output/'server.log').read_text())
                    time.sleep(1)
                    if server.poll() is not None:raise RuntimeError('host stopped on player leave')
                    if args.impaired:
                        invitation=root/'invites'/'player1.rune-invite'
                        link=ImpairedLink(endpoints[1],uplink_ms=75,downlink_ms=75,jitter_ms=15,loss=3,duplicate=2,seed=1892)
                        links.append(link);lines=invitation.read_text().splitlines();lines[1]=f'127.0.0.1 {link.port}';invitation.write_text('\n'.join(lines)+'\n')
                    clients.append(launch('reconnected',[f'--rune-join={root/"invites"/"player1.rune-invite"}',
                        '--rune-net-bot',*character_options(1),*args.bot_option,*(['--rune-net-bot-idle'] if not args.tactical_bots and not args.stress else []),f'--rune-net-seconds={max(30,int(duration-(time.monotonic()-began)))}',*(['--rune-net-test-headball-input'] if mode=='headball' and not args.tactical_bots else []),*(['--rune-net-bot-stress'] if args.stress else [])]))
                for p in clients:
                    if args.abrupt and p==clients[0]:continue
                    if p.wait(timeout=duration+35)!=0:raise RuntimeError('client failed')
            if server.wait(timeout=50 if args.reconnect else 35)!=0:raise RuntimeError('server failed')
            server_text=(output/'server.log').read_text()
            if 'OPEN_RUNE_SERVER_READY' not in server_text:raise RuntimeError('no ready marker')
            if not audit:
                for i in range(1,args.players+1):
                    if args.abrupt and i==1:continue
                    t=(output/f'client{i}.log').read_text()
                    if 'OPEN_RUNE_BOT_OK' not in t:raise RuntimeError('no bot success')
                    if args.expect_travel and not (args.reconnect and i==1) and 'OPEN_RUNE_BOT_TRAVEL' not in t:raise RuntimeError('no client travel')
                if args.expect_travel and 'OPEN_RUNE_MATCH_ENDED' not in server_text:raise RuntimeError('no match result')
            if args.reconnect:
                text=(output/'reconnected.log').read_text()
                if 'OPEN_RUNE_BOT_JOINED' not in text or 'OPEN_RUNE_BOT_OK' not in text:raise RuntimeError('reconnect did not install a world')
                if server_text.count('OPEN_RUNE_PLAYER_JOINED id=1 ')<2:raise RuntimeError('slot not rejoined')
            if args.all_maps:
                for name in maps:
                    if f'map={name}\n' not in server_text:raise RuntimeError('map never started: '+name)
                    sections=[section for section in server_text.split('OPEN_RUNE_MAP_CHECK ')[1:] if section.startswith('map='+name+' ')]
                    for marker in args.require_marker:
                        if not any(marker in section for section in sections):raise RuntimeError(f'{name} missing objective: {marker}')
                    for i in range(1,args.players+1):
                        t=(output/f'client{i}.log').read_text()
                        if f'map={name} entities=' not in t:raise RuntimeError(f'client {i} did not install map {name}')
                if server_text.count('OPEN_RUNE_MATCH_ENDED')<len(maps):raise RuntimeError('not every map completed a match')
            for marker in args.require_marker:
                if marker not in server_text:raise RuntimeError('missing server evidence: '+marker)
            result['map_checks']=re.findall(r'OPEN_RUNE_MAP_CHECK[^\n]+',server_text)
            result['matches']=re.findall(r'OPEN_RUNE_MATCH_ENDED[^\n]+',server_text)
            result['elapsed_seconds']=round(time.monotonic()-began,2)
            result['passed']=True
        except Exception as e:result['error']=str(e)
        finally:
            for p in processes:
                if p.poll() is None:p.terminate()
            for p in processes:
                try:p.wait(timeout=5)
                except subprocess.TimeoutExpired:p.kill();p.wait()
            for f in logs:f.close()
            for link in links:link.close()
            result['returncodes']=[p.returncode for p in processes]
            result['links']=[link.stats for link in links]
            (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result),flush=True)
    return result


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--game',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--scratch',type=Path,required=True)
    p.add_argument('--players',type=int,default=4);p.add_argument('--seconds',type=int,default=22)
    p.add_argument('--time-limit',type=int,default=6);p.add_argument('--mode',default='dm')
    p.add_argument('--maps',default='DM-Hildir,DM-Thorstadt');p.add_argument('--map-audit',action='store_true')
    p.add_argument('--expect-travel',action='store_true');p.add_argument('--impaired',action='store_true')
    p.add_argument('--reconnect',action='store_true')
    p.add_argument('--abrupt',action='store_true')
    p.add_argument('--late-join',type=int,default=0)
    p.add_argument('--outage-seconds',type=int,default=0)
    p.add_argument('--stress',action='store_true')
    p.add_argument('--tactical-bots',action='store_true',help='Use real bot decisions, including HeadBall objectives instead of fixture inputs')
    p.add_argument('--bot-option',action='append',default=[])
    p.add_argument('--all-maps',action='store_true',help='Require a joined world and complete match for every rotation map')
    p.add_argument('--characters',nargs='+',help='Character ID:skin choices, repeated across clients')
    p.add_argument('--server-option',action='append',default=[])
    p.add_argument('--require-marker',action='append',default=[])
    a=p.parse_args()
    if a.characters and any(not re.fullmatch(r'\d+:\d+',c) for c in a.characters):p.error('characters must be ID:skin')
    if a.abrupt and not a.reconnect:p.error('--abrupt requires --reconnect')
    if a.outage_seconds and not a.impaired:p.error('--outage-seconds requires --impaired')
    for key in ('binary','game','output','scratch'):setattr(a,key,getattr(a,key).resolve())
    a.output.mkdir(parents=True,exist_ok=True);a.scratch.mkdir(parents=True,exist_ok=True)
    if a.map_audit:
        results=[]
        for path in sorted((a.game/'Maps').glob('*.run')):
            mode=next((m for prefix,m in [('DM-','dm'),('AR-','arena'),('HB-','headball')] if path.stem.upper().startswith(prefix)),None)
            if mode:results.append(run(a,mode,[path.stem],path.stem,True))
        (a.output/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
    else:results=[run(a,a.mode,a.maps.split(','),'match')]
    return 0 if results and all(r['passed'] for r in results) else 1
if __name__=='__main__':raise SystemExit(main())
