#!/usr/bin/env python3
"""Start a private Open Rune host or join with a private invitation.

Uses a fresh session directory for logs, configuration, saves and invitations.
The engine and game assets are read from the supplied paths. No game installation
or normal saves/settings are modified. Windows/macOS/Linux; Python standard library only.
"""
import argparse
import ipaddress
import json
import shutil
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    action=p.add_mutually_exclusive_group(required=True)
    action.add_argument('--host',action='store_true',help='Host and play in one command')
    action.add_argument('--headless',action='store_true',help='Host without a game window')
    action.add_argument('--join',type=Path,metavar='INVITATION',help='Join using your private .rune-invite file')
    action.add_argument('--join-key',help='Join a public HyperDHT host by its 64-character identity')
    p.add_argument('--public',action='store_true',help='List the hosted match in Find games (requires Node.js and npm ci in Tools/RuneDiscovery)')
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--game',type=Path,required=True)
    p.add_argument('--session',type=Path,required=True,help='New directory for this session; must not exist')
    p.add_argument('--address',default='127.0.0.1',help='Host interface reachable by friends; default is laptop-only')
    p.add_argument('--mode',choices=('dm','tdm','arena','headball'),default='dm')
    p.add_argument('--maps',help='Comma-separated map rotation')
    p.add_argument('--players',type=int,default=4,help='Number of private invitation slots')
    p.add_argument('--teams',type=int,choices=(2,3,4),default=2)
    p.add_argument('--arena-team-size',type=int,choices=range(1,5),default=1)
    p.add_argument('--limbs-score',action='store_true')
    p.add_argument('--score-limit',type=int,default=20,help='Frags, team points, or Arena rounds; 0 disables')
    p.add_argument('--time-limit',type=int,default=600,help='Match seconds; 0 disables')
    p.add_argument('--intermission',type=int,default=8)
    p.add_argument('--seconds',type=int,default=0,help='Optional session duration; 0 runs until stopped')
    p.add_argument('--bots',type=int,default=0,help='Tactical bots with randomized installed characters/skins; reserves invitation slots')
    p.add_argument('--bot-difficulty',type=int,choices=range(4),default=1,help='0 Easy, 1 Medium, 2 Hard, 3 Very Hard')
    p.add_argument('--diagnostics',action='store_true',help='Record host tick/network diagnostics and show the optional client overlay')
    p.add_argument('--client-bot',action='store_true',help=argparse.SUPPRESS)
    a=p.parse_args()
    if a.bots and (a.join or a.join_key):p.error('--bots is for hosting')
    if not 0<=a.bots<a.players:p.error('Leave at least one invitation slot for a human')
    if a.public and (a.join or a.join_key):p.error('--public is for hosting')
    if a.join_key and (len(a.join_key)!=64 or any(c not in '0123456789abcdef' for c in a.join_key)):p.error('Invalid host identity')
    if a.public:a.address='127.0.0.1'
    a.binary=a.binary.resolve();a.game=a.game.resolve();a.session=a.session.resolve()
    if not a.binary.is_file() or not os.access(a.binary,os.X_OK):p.error('Engine executable is missing or not executable')
    if not (a.game/'System'/'RuneI.u').is_file():p.error('Game folder must contain System/RuneI.u')
    if a.join and not a.join.is_file():p.error('Invitation file does not exist')
    if not 2<=a.players<=16:p.error('Player slots must be between 2 and 16')
    if a.seconds<0 or a.seconds>86400 or (a.client_bot and not a.seconds):p.error('Invalid duration (automated clients need a finite duration)')
    if not 0<=a.score_limit<=10000 or not 0<=a.time_limit<=86400 or not (a.score_limit or a.time_limit):p.error('Enable a valid time or score limit')
    if not 2<=a.intermission<=60:p.error('Intermission must be 2..60 seconds')
    if a.limbs_score and a.mode!='headball':p.error('Limb scoring is a HeadBall rule')
    try:
        address=ipaddress.IPv4Address(a.address)
        if address.is_unspecified or address.is_multicast:raise ValueError()
    except ValueError:p.error('Use a specific local IPv4 address')
    if a.session.exists():p.error('Choose a new session directory to preserve previous sessions')
    a.session.mkdir(parents=True,mode=0o700)
    children=[];logs=[];stopping=False
    def stop(_sig=None,_frame=None):
        nonlocal stopping
        stopping=True
    previous={s:signal.signal(s,stop) for s in (signal.SIGINT,signal.SIGTERM)}
    def launch(name,options):
        root=a.session/name;root.mkdir(mode=0o700)
        saves=root/'saves';saves.mkdir(mode=0o700)
        config=root/'config';(config/'SurrealEngine').mkdir(parents=True,mode=0o700)
        log=(root/'session.log').open('x');logs.append(log)
        process=subprocess.Popen([str(a.binary),'--direct',f'--save-directory={saves}',*options,*(['--rune-net-diagnostics'] if a.diagnostics else []),str(a.game)],
            env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(config)),stdout=log,stderr=subprocess.STDOUT)
        children.append(process);return process
    discovery=None
    def discover(directory=None,key=None):
        if not shutil.which('node'):raise RuntimeError('Node.js is required for desktop discovery')
        root=a.session/'discovery';root.mkdir(mode=0o700)
        config={'control':str(root)}
        if directory:config.update(directory=str(directory),firstSlot=(1 if a.headless else 2)+a.bots)
        (root/'config.json').write_text(json.dumps(config))
        log=(root/'runtime.log').open('x');logs.append(log)
        process=subprocess.Popen(['node',str(Path(__file__).resolve().parents[1]/'RuneDiscovery/runtime.cjs'),str(root/'config.json')],stdout=log,stderr=subprocess.STDOUT)
        children.append(process)
        if key:(root/'command.json').write_text(json.dumps({'id':1,'action':'join','key':key}))
        deadline=time.monotonic()+30
        while True:
            if stopping:raise RuntimeError('Discovery cancelled')
            if process.poll() is not None:raise RuntimeError('Discovery stopped; see discovery/runtime.log')
            try:state=json.loads((root/'state.json').read_text())
            except (OSError,ValueError):state={}
            if state.get('phase') in ('hosting','joined'):
                print('HyperDHT '+state['phase']+'. Host identity: '+state['key'],flush=True);return process
            if state.get('phase')=='error':raise RuntimeError(state.get('message','Discovery failed'))
            if time.monotonic()>deadline:raise RuntimeError('Discovery startup timed out')
            time.sleep(.1)
    server=None;client=None;bots=[]
    try:
        invite=a.join.resolve() if a.join else a.session/'invitations'/'player1.rune-invite'
        if not (a.join or a.join_key):
            maps=a.maps or dict(dm='DM-Hildir,DM-Thorstadt',tdm='DM-Team_ships',arena='AR-Champions',headball='HB-Agora')[a.mode]
            server=launch('host',[f'--rune-server={a.session/"invitations"}',f'--rune-net-address={a.address}',
                f'--rune-net-mode={a.mode}',f'--rune-net-maps={maps}',f'--rune-net-players={a.players}',
                f'--rune-net-teams={a.teams}',f'--rune-net-arena-team-size={a.arena_team_size}',
                *(['--rune-net-discovery'] if a.public else []),*(['--rune-net-limbs-score'] if a.limbs_score else []),f'--rune-net-frag-limit={a.score_limit}',
                f'--rune-net-time-limit={a.time_limit}',f'--rune-net-intermission={a.intermission}',f'--rune-net-seconds={a.seconds}'])
            deadline=time.monotonic()+45
            while not (a.session/'invitations'/f'player{a.players}.rune-invite').exists():
                if stopping:return 0
                if server.poll() is not None:raise RuntimeError('Host did not start; see host/session.log')
                if time.monotonic()>deadline:raise RuntimeError('Host startup timed out; see host/session.log')
                time.sleep(.05)
            for slot in range(1 if a.headless else 2,(1 if a.headless else 2)+a.bots):
                bots.append(launch(f'bot{slot}',[f'--rune-join={a.session/"invitations"/f"player{slot}.rune-invite"}',
                    '--rune-net-bot',f'--rune-bot-difficulty={a.bot_difficulty}',f'--rune-net-seconds={a.seconds}']))
            if a.bots:print(f'{a.bots} tactical bots started; remaining invitation slots are for human players.',flush=True)
            if a.public:discovery=discover(a.session/'invitations')
            else:print(f'Private host ready. Invitations: {a.session/"invitations"}',flush=True)
            if not a.public:print('Give each friend a different invitation file. It contains a private access key.',flush=True)
            if a.host:print('Player 1 is reserved for your local game window.',flush=True)
        if a.join_key:
            discovery=discover(key=a.join_key);invite=a.session/'discovery/joined.rune-invite'
        if not a.headless:
            client=launch('player',[f'--rune-join={invite}',f'--rune-net-seconds={a.seconds}',*(['--rune-net-bot'] if a.client_bot else [])])
            print(f'Joining. Session log: {a.session/"player"/"session.log"}',flush=True)
        while not stopping:
            for bot in bots:
                if bot.poll() is not None and bot.returncode:raise RuntimeError('A bot disconnected with an error; see its session.log')
            if discovery is not None and discovery.poll() is not None:raise RuntimeError('Discovery transport stopped; see discovery/runtime.log')
            if client is not None and client.poll() is not None:
                if client.returncode:raise RuntimeError('Player session failed; see player/session.log')
                break
            if server is not None and server.poll() is not None:
                if server.returncode:raise RuntimeError('Host stopped with an error; see host/session.log')
                if client is not None:
                    try:client.wait(timeout=5)
                    except subprocess.TimeoutExpired:pass
                break
            time.sleep(.1)
        return 0
    except (OSError,RuntimeError) as e:
        print(str(e),file=sys.stderr);return 1
    finally:
        # Engine exits normally after Leave or the bounded session duration.
        # On interruption, terminate only the processes owned by this launcher.
        for child in reversed(children):
            if child.poll() is None:child.terminate()
        for child in children:
            try:child.wait(timeout=5)
            except subprocess.TimeoutExpired:child.kill();child.wait()
        for log in logs:log.close()
        for sig,handler in previous.items():signal.signal(sig,handler)

if __name__=='__main__':raise SystemExit(main())
