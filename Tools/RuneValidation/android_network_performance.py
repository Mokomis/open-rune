"""Bounded Android LAN renderer profile with a fixed camera and isolated saves.

Restarts Open Rune. Run only when abandoning the current unsaved view is safe.
Uses private temporary invitations; never stores credentials in evidence.
The partner is a rendered desktop client capped at 20 FPS, not a second device.
"""
import argparse
import pathlib
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=pathlib.Path, required=True)
parser.add_argument('--game', type=pathlib.Path, required=True)
parser.add_argument('--adb', default='adb')
parser.add_argument('--device', required=True)
parser.add_argument('--host', required=True, help='Host LAN IPv4 address')
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--group-shadows', type=int, choices=(0, 1), default=0)
parser.add_argument('--reference-renderer', action='store_true',
                    help='Disable early opaque depth and use full-precision lighting for comparison')
parser.add_argument('--saved-resolution', action='store_true', help='Keep saved scaling; default forces native')
parser.add_argument('--view', default='-1714.423218,1713.299194,-178,57032,0')
parser.add_argument('--scripted-input', action='store_true', help='Arena gameplay fixture; use the Android Respawn button after death')
parser.add_argument('--prediction', type=int, choices=(0,1), default=1)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
adb = [args.adb, '-s', args.device]
processes, logs = [], []


def shell(*command):
    return subprocess.run(adb + ['shell', *command], check=True, capture_output=True,
                          text=True, timeout=20).stdout


def device_log():
    return shell('run-as', 'org.runeclassic.port', 'cat', 'files/native-run.log')


with tempfile.TemporaryDirectory(prefix='open-rune-performance-') as folder:
    root = pathlib.Path(folder)
    invites = root / 'invitations'

    def launch(name, options):
        saves = root / (name + '-saves')
        saves.mkdir()
        log = (args.output / (name + '.log')).open('w')
        logs.append(log)
        process = subprocess.Popen([str(args.binary.resolve()), '--direct',
            f'--save-directory={saves}', *options, str(args.game.resolve())],
            stdout=log, stderr=subprocess.STDOUT)
        processes.append(process)
        return process

    try:
        launch('server', [f'--rune-server={invites}', f'--rune-net-address={args.host}',
                         '--rune-net-seconds=120', '--probe-seed=1', *(['--rune-net-test-arena'] if args.scripted_input else [])])
        deadline = time.monotonic() + 20
        while not (invites / 'player2.rune-invite').exists():
            if processes[0].poll() is not None or time.monotonic() > deadline:
                raise RuntimeError('Server did not create invitations')
            time.sleep(.1)
        shell('run-as', 'org.runeclassic.port', 'mkdir', '-p', 'files/multiplayer')
        subprocess.run(adb + ['shell', 'run-as', 'org.runeclassic.port', 'sh', '-c',
            "'umask 077; cat > files/multiplayer/performance.rune-invite'"],
            input=(invites / 'player2.rune-invite').read_bytes(), check=True, timeout=20)
        launch('partner', [f'--rune-join={invites / "player1.rune-invite"}',
            '--rune-net-seconds=115', '--maxfps=20', '--rune-net-width=640',
            '--rune-net-height=480', '--render-msaa=2', *(['--rune-net-test-input'] if args.scripted_input else [])])
        shell('am', 'force-stop', 'org.runeclassic.port')
        time.sleep(1)
        shell('am', 'start', '-W', '-n', 'org.runeclassic.port/.MainActivity',
            '--es', 'saveTest', 'network-performance', '--es', 'networkInvite', 'performance.rune-invite',
            *([] if args.scripted_input else ['--es', 'probeView', args.view]),
            '--ei', 'networkPrediction', str(args.prediction),
            *(['--ez','networkTestInput','true','--ei','networkSeconds','55'] if args.scripted_input else []),
            *([] if args.saved_resolution else ['--ez', 'upscale', 'false']), '--ei', 'msaa', '2',
            '--ei', 'seed', '1', '--ei', 'profile', '900', '--ei', 'groupShadows', str(args.group_shadows),
            '--ei', 'opaqueEarlyDepth', '0' if args.reference_renderer else '1',
            '--ei', 'fullPrecisionLighting', '1' if args.reference_renderer else '0')
        deadline = time.monotonic() + 70
        while True:
            text = device_log()
            if 'OPEN_RUNE_CLIENT_PROFILE' in text:
                break
            if time.monotonic() > deadline:
                (args.output / 'android.log').write_text(text)
                raise RuntimeError('No joined-client profile; check screen, LAN and Android log')
            time.sleep(1)
        subprocess.run(['python3', str(pathlib.Path(__file__).with_name('android_present.py')),
            '--adb', args.adb, '--device', args.device, '--samples', '24', '--oppo-clocks',
            '--output', str(args.output / 'display')], check=True, timeout=45)
        (args.output / 'android.log').write_text(device_log())
        with (args.output / 'view.png').open('wb') as image:
            subprocess.run(adb + ['exec-out', 'screencap', '-p'], stdout=image, check=True, timeout=20)
        if args.scripted_input:
            deadline=time.monotonic()+65
            while 'OPEN_RUNE_CLIENT_STOP' not in text:
                if time.monotonic()>deadline: raise RuntimeError('Android gameplay fixture did not finish')
                time.sleep(1);text=device_log()
            (args.output/'android.log').write_text(text)
            assert 'death=1 respawn=1 life=2' in text, 'Use the Android Respawn button during this fixture'
            assert 'OPEN_RUNE_PREDICTION enabled=1' in text
            print('PASS Android movement, kick result, death and respawn to new life')
        for line in text.splitlines():
            if 'OPEN_RUNE_CLIENT_PROFILE' in line:
                print(line)
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        for log in logs:
            log.close()
        try:
            shell('am', 'force-stop', 'org.runeclassic.port')
            shell('run-as', 'org.runeclassic.port', 'rm', '-f', 'files/multiplayer/performance.rune-invite')
        except subprocess.SubprocessError as error:
            print('Device cleanup could not complete:', type(error).__name__)
