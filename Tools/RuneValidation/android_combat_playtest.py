"""Private human blocking practice, native resolution/4x AA, isolated saves.

The tablet owns player 1; only the desktop opponent is automated. Run after
the user authorizes replacing the current session. Stop via Leave match or
terminate this launcher. Invitations are temporary bearer credentials.
"""
import argparse
import json
import pathlib
import signal
import subprocess
import tempfile
import time
from network_proxy import ImpairedLink

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', type=pathlib.Path, required=True)
p.add_argument('--game', type=pathlib.Path, required=True)
p.add_argument('--output', type=pathlib.Path, required=True)
p.add_argument('--adb', default='adb')
p.add_argument('--device', default='99cc517a')
p.add_argument('--host', required=True)
p.add_argument('--tablet', required=True)
p.add_argument('--duration', type=int, default=1800)
p.add_argument('--delay-ms', type=int, default=50, help='Added delay each way')
p.add_argument('--jitter-ms', type=int, default=5)
p.add_argument('--desktop-check', action='store_true')
p.add_argument('--automated-defender', action='store_true', help='Bounded sustained rendering fixture; takes control of tablet')
p.add_argument('--cull-lighting', type=int, choices=(0,1), default=1)
p.add_argument('--profile', type=int, default=0)
p.add_argument('--packed-hdr', type=int, choices=(0,1), default=1,
               help='Compare scene-buffer formats without changing resolution or effects')
p.add_argument('--uniform-shadow-data', type=int, choices=(0,1), default=1,
               help='Compare frame-wide lighting buffer access without changing effects')
a = p.parse_args()
if not 20 <= a.duration <= 3600 or not 0 <= a.delay_ms <= 250 or not 0 <= a.jitter_ms <= 100:
    p.error('Invalid duration or impairment bounds')
a.output.mkdir(parents=True, exist_ok=False)
adb = [a.adb, '-s', a.device]
processes, logs = [], []
link = None
uploaded = False
stopping = False

def stop(_signal, _frame):
    global stopping
    stopping = True

signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, stop)

def shell(*args):
    return subprocess.run(adb + ['shell', *args], check=True, capture_output=True,
                          text=True, timeout=15).stdout

with tempfile.TemporaryDirectory(prefix='open-rune-human-combat-') as folder:
    root = pathlib.Path(folder)
    invitations = root / 'invitations'

    def launch(name, options):
        saves = root / (name + '-saves')
        saves.mkdir()
        log = (a.output / (name + '.log')).open('w')
        logs.append(log)
        process = subprocess.Popen([str(a.binary.resolve()), '--direct',
            f'--save-directory={saves}', f'--rune-net-seconds={a.duration}',
            *options, str(a.game.resolve())], stdout=log, stderr=subprocess.STDOUT)
        processes.append(process)
        return process

    try:
        server = launch('server', [f'--rune-server={invitations}',
            f'--rune-net-address={a.host}', '--rune-net-sparring-arena', '--probe-seed=1'])
        deadline = time.monotonic() + 25
        while not (invitations / 'player2.rune-invite').exists():
            if server.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError('Server did not become ready')
            time.sleep(.1)
        path = invitations / 'player1.rune-invite'
        lines = path.read_text().splitlines()
        address, port = lines[1].split()
        link = ImpairedLink((address, int(port)), bind_address=a.host,
            client_address=a.host if a.desktop_check else a.tablet,
            uplink_ms=a.delay_ms, downlink_ms=a.delay_ms,
            jitter_ms=a.jitter_ms, seed=921)
        lines[1] = f'{a.host} {link.port}'
        path.write_text('\n'.join(lines) + '\n')
        opponent = launch('opponent', [f'--rune-join={invitations / "player2.rune-invite"}',
            '--rune-net-sparring-opponent', '--maxfps=60', '--rune-net-width=640',
            '--rune-net-height=480', '--render-msaa=2'])
        if a.desktop_check:
            launch('defender', [f'--rune-join={path}', '--maxfps=60',
                *(['--rune-net-sparring-defender'] if a.automated_defender else []),
                '--rune-net-width=640', '--rune-net-height=480', '--render-msaa=2'])
        else:
            shell('run-as', 'org.runeclassic.port', 'mkdir', '-p', 'files/multiplayer')
            subprocess.run(adb + ['shell', 'run-as', 'org.runeclassic.port', 'sh', '-c',
                "'umask 077; cat > files/multiplayer/combat-playtest.rune-invite'"],
                input=path.read_bytes(), check=True, timeout=15)
            uploaded = True
            shell('am', 'force-stop', 'org.runeclassic.port')
            time.sleep(1)
            shell('am', 'start', '-W', '-n', 'org.runeclassic.port/.MainActivity',
                '--es', 'saveTest', 'network-human-combat',
                '--es', 'networkInvite', 'combat-playtest.rune-invite',
                '--ez', 'upscale', 'false', '--ei', 'msaa', '2', '--ei', 'seed', '1',
                '--ei', 'surfacePercent', '100', '--ei', 'packedHdr', str(a.packed_hdr), '--ei', 'uniformShadowData', str(a.uniform_shadow_data),
                '--ei', 'cullLighting', str(a.cull_lighting), '--ei', 'profile', str(a.profile),
                '--ez', 'networkSparringDefender', str(a.automated_defender).lower())
        print(f'Session launched: added RTT {a.delay_ms * 2} ms, jitter +/-{a.jitter_ms} ms each way; duration {a.duration}s.', flush=True)
        deadline = time.monotonic() + a.duration + 5
        while not stopping and time.monotonic() < deadline and server.poll() is None and opponent.poll() is None:
            time.sleep(.5)
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
        for process in processes:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        for log in logs:
            log.close()
        if link:
            link.close()
            (a.output / 'link.json').write_text(json.dumps({'added_rtt_ms': a.delay_ms * 2,
                'jitter_ms_each_way': a.jitter_ms, 'stats': link.stats}, indent=2) + '\n')
        if uploaded:
            try:
                (a.output / 'android.log').write_text(shell('run-as', 'org.runeclassic.port', 'cat', 'files/native-run.log'))
                shell('run-as', 'org.runeclassic.port', 'rm', '-f', 'files/multiplayer/combat-playtest.rune-invite')
            except (subprocess.SubprocessError, OSError):
                print('Device unavailable for log retrieval/invitation cleanup; server credentials have expired.', flush=True)
