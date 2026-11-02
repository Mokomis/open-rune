"""Four rendered clients: original scoring, match limits, two-map rotation.

Uses private disposable invitations/saves and deterministic server death events;
this is a match-lifecycle fixture, not proof of human combat fairness.
"""
import argparse
import json
import os
import hashlib
import re
import subprocess
import tempfile
import time
from pathlib import Path
from network_proxy import ImpairedLink

parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--game', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--native',action='store_true',help='Use the native desktop window resolution with 4x AA')
parser.add_argument('--maps',help='Override rotation; one map may repeat across rounds')
parser.add_argument('--team',action='store_true')
parser.add_argument('--arena',action='store_true')
parser.add_argument('--headball',action='store_true')
parser.add_argument('--time-limit', action='store_true', help='No scripted kills; test a tied time-limit finish')
parser.add_argument('--impaired', action='store_true')
parser.add_argument('--seconds', type=int, default=70)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
processes, logs, links = [], [], []
passed=False
started=time.monotonic()
rotation=args.maps.split(",") if args.maps else ["DM-Team_ships"] if args.team else ["DM-Hildir","DM-Thorstadt"]
with tempfile.TemporaryDirectory(prefix='open-rune-match-') as temporary:
    root = Path(temporary)
    invites = root / 'invitations'
    def launch(name, options):
        saves = root / (name + '-saves'); saves.mkdir()
        log = (args.output / (name + '.log')).open('w'); logs.append(log)
        config=root/(name+'-config');(config/'SurrealEngine').mkdir(parents=True)
        process = subprocess.Popen([str(args.binary.resolve()), '--direct', f'--save-directory={saves}',
                                    *options, str(args.game.resolve())], stdout=log, stderr=subprocess.STDOUT,env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(config)))
        processes.append(process)
        return process
    try:
        options = [f'--rune-server={invites}', '--rune-net-players=4', '--rune-net-maps=DM-Team_ships' if args.team else '--rune-net-maps=DM-Hildir,DM-Thorstadt',
                   '--rune-net-mode=tdm' if args.team else '--rune-net-mode=dm',
                   f'--rune-net-seconds={args.seconds+25}', '--rune-net-intermission=3',
                   '--rune-net-time-limit=6' if args.time_limit else '--rune-net-time-limit=30',
                   '--rune-net-frag-limit=0' if args.time_limit else '--rune-net-frag-limit=1']
        if args.arena:
            options = [f'--rune-server={invites}', '--rune-net-players=4', '--rune-net-mode=arena', '--rune-net-maps=AR-Champions', '--rune-net-frag-limit=0', '--rune-net-time-limit=120', f'--rune-net-seconds={args.seconds+25}', '--rune-net-test-arena-mode','--rune-net-test-moving-brush']
        if args.headball:
            options=[f'--rune-server={invites}','--rune-net-players=4','--rune-net-mode=headball','--rune-net-maps=HB-Agora','--rune-net-frag-limit=0','--rune-net-time-limit=20',f'--rune-net-seconds={args.seconds+25}','--rune-net-intermission=3','--rune-net-test-headball']
        if not args.arena and not args.headball and not args.time_limit: options.append('--rune-net-test-team' if args.team else '--rune-net-test-match')
        if args.maps:
            options=[opt for opt in options if not opt.startswith('--rune-net-maps=')]+['--rune-net-maps='+args.maps]
        server = launch('server', options)
        deadline = time.monotonic() + 20
        while not (invites / 'player4.rune-invite').exists():
            if server.poll() is not None or time.monotonic() > deadline: raise RuntimeError('Server startup failed')
            time.sleep(.05)
        clients = []
        for i in range(1, 5):
            invitation = invites / f'player{i}.rune-invite'
            assert invitation.stat().st_mode & 0o077 == 0
            if args.impaired:
                lines = invitation.read_text().splitlines(); address, port = lines[1].split()
                link = ImpairedLink((address,int(port)), uplink_ms=75,downlink_ms=75,jitter_ms=15,loss=3,duplicate=2,seed=891+i)
                links.append(link);lines[1] = f'127.0.0.1 {link.port}';invitation.write_text('\n'.join(lines)+'\n')
            clients.append(launch(f'client{i}', [f'--rune-join={invitation}',f'--rune-net-seconds={args.seconds}',
                *(['--render-scale=1'] if args.native else ['--rune-net-width=640','--rune-net-height=480']),'--render-msaa=2','--render-omit-hit=1','--maxfps=60',*(['--rune-net-test-headball-input'] if args.headball else ['--rune-net-test-arena-input'] if args.arena else ['--rune-net-test-team-input'] if args.team else []),
                f'--capture-frame={(args.output/f"client{i}.ppm").resolve()}']))
        deadline=time.monotonic()+args.seconds+30
        while any(client.poll() is None for client in clients):
            assert server.poll() in (None,0), 'Server failed'
            assert all(client.poll() in (None,0) for client in clients), 'Client failed'
            assert time.monotonic()<deadline, 'Client timed out'
            time.sleep(.2)
        assert all(client.returncode==0 for client in clients), 'Client failed'
        assert server.wait(timeout=40) == 0, 'Server failed'
        text = (args.output/'server.log').read_text()
        if args.headball:
            for marker in ['OPEN_RUNE_HEADBALL_PUMP team=0','OPEN_RUNE_HEADBALL_PUMP team=1','kind=6 accepted=1','kind=7 accepted=1','OPEN_RUNE_HEADBALL_STARTED','OPEN_RUNE_HEADBALL_GOAL','OPEN_RUNE_HEADBALL_KILL no_points=1','kind=9 accepted=1','OPEN_RUNE_MATCH_STARTED round=2 map=HB-Agora']:
                assert marker in text, marker
            points=int(re.search(r'OPEN_RUNE_HEADBALL_GOAL points=(\d+)',text)[1]);assert points>0
            for i in range(1,5):
                client=(args.output/f'client{i}.log').read_text()
                assert f'OPEN_RUNE_CLIENT_SCORE round=1 phase=3 winner=0 team=0 p1={points},0 p2=0,1 p3=0,0 p4=0,0 hb=1,0,1,1 red={points} blue=0' in client
                assert 'OPEN_RUNE_CLIENT_TRAVEL round=2 map=HB-Agora' in client
                assert 'Exception' not in client and 'Error' not in client
            print('PASS HeadBall: pumps, countdown, equipped/thrown head, original goal scoring, kill rule, respawn and match restart')
        elif args.arena:
            assert 'OPEN_RUNE_ARENA_QUEUED players=4' in text
            assert 'OPEN_RUNE_ARENA_DEFEAT round=2' in text and 'OPEN_RUNE_ARENA_DEFEAT round=3' in text
            assert re.search(r'p[1-4]=2,0',text), 'Champion did not receive two wins'
            assert 'OPEN_RUNE_ARENA_SCRIPT_RESPAWN player=4' in text and 'OPEN_RUNE_PLAYER_LIFE player=4 life=2 health=200' in text, 'No original-script respawn life transition'
            assert 'p1=3,0 p2=0,1 p3=0,1 p4=0,1' in text, 'Queue did not advance through each challenger'
            for i in range(1,5):
                client=(args.output/f'client{i}.log').read_text()
                assert f'OPEN_RUNE_CLIENT_JOINED owned={i}' in client and 'OPEN_RUNE_CLIENT_STOP' in client
                for phase in (1,2,3):assert f'arena={phase},' in client, 'Missing Arena phase'
                brushes=re.search(r'OPEN_RUNE_BRUSH_CHECKS authored=(\d+) moved=(\d+) checks=(\d+)',client)
                assert brushes and int(brushes[1])>0 and int(brushes[2])>0 and int(brushes[3])>0, 'Authored brush validation missing'
                assert 'Exception' not in client and 'Error' not in client
            print('PASS Arena on AR-Champions: four players, queue, countdown, fights, wins/losses and respawn')
        else:
            assert ('OPEN_RUNE_MATCH_STARTED round=3 map='+rotation[2%len(rotation)]) in text, 'Did not rotate back'
            reason = 'time-limit' if args.time_limit else 'frag-limit'
            winner = 0 if args.time_limit or args.team else 3
            team = 0 if args.team and not args.time_limit else 255
            scores = 'p1=-1,1 p2=0,1 p3=2,0 p4=0,1' if args.team else 'p1=-1,1 p2=0,1 p3=1,0 p4=0,0'
            if args.team and not args.time_limit:
                assert text.count('OPEN_RUNE_TEAM_FRIENDLY_OK')>=2
                assert 'kind=9 accepted=1' in text
                assert 'OPEN_RUNE_CONTACT move=1 contact=1' in text and 'OPEN_RUNE_CONTACT move=2 contact=1' in text
            for r in (1,2):
                assert f'OPEN_RUNE_MATCH_ENDED round={r} reason={reason} winner={winner} team={team}' in text
                if not args.time_limit:
                    assert f'OPEN_RUNE_SCORE round={r} phase=3 {scores}' in text, 'Original kill/suicide scoring differs'
            for i in range(1,5):
                client = (args.output/f'client{i}.log').read_text()
                sessions = re.findall(r'OPEN_RUNE_CLIENT_TRAVEL round=(\d+) map=([\w-]+) session=(\d+)',client)
                assert len(sessions)>=2 and sessions[0][:2]==('2',rotation[1%len(rotation)]) and sessions[1][:2]==('3',rotation[2%len(rotation)]), 'Client did not travel twice'
                assert len({s[2] for s in sessions})==1, 'Connection changed during travel'
                assert client.count(f'OPEN_RUNE_CLIENT_JOINED owned={i}')>=3, 'Player not installed after rotation'
                assert 'OPEN_RUNE_CLIENT_STOP' in client
                for r in (1,2):
                    assert f'OPEN_RUNE_CLIENT_SCORE round={r} phase=3 winner={winner}' in client, 'Client missed match result'
                    if not args.time_limit:
                        assert f'OPEN_RUNE_CLIENT_SCORE round={r} phase=3 winner={winner} team={team} {scores}' in client
                assert 'OPEN_RUNE_CLIENT_SCORE round=3 phase=1 winner=0 team=255 p1=0,0 p2=0,0 p3=0,0 p4=0,0' in client, 'Scores not reset'
                assert 'Error' not in client and 'Exception' not in client
            print('PASS four rendered players, scores, '+reason+', intermission, '+('two map changes' if len(set(rotation))>1 else 'two same-map restarts')+', authenticated connections preserved')
        passed=True
    finally:
        for process in processes:
            if process.poll() is None: process.kill()
            process.wait()
        for log in logs: log.close()
        for link in links: link.close()
        (args.output/'results.json').write_text(json.dumps({'passed':passed,'binary_sha256':hashlib.sha256(args.binary.read_bytes()).hexdigest(),'elapsed_seconds':time.monotonic()-started,'mode':'headball' if args.headball else 'arena' if args.arena else 'tdm' if args.team else 'dm','native_4x':args.native,'time_limit':args.time_limit,'impaired':args.impaired,'links':[l.stats for l in links]},indent=2)+'\n')
