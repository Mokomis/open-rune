"""Bounded Android recovery checks against an existing private host.

Requires USB debugging, unlocked tablet, and a disposable invitation supplied by
an existing host (with another player for the short-Wi-Fi case). Restarts Open
Rune using isolated saves and native/4x overrides. Never starts/stops a remote
host, prints invitations, or modifies saved graphics preferences. Wi-Fi is
restored in finally; use join-timeout only with a deliberately unavailable host.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time
import uuid

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--adb', default='adb')
p.add_argument('--device', required=True)
p.add_argument('--invitation', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--case', choices=('short-wifi', 'join-timeout'), required=True)
a = p.parse_args()
if ':' in a.device:
    p.error('Use USB debugging so the Wi-Fi check cannot cut off recovery access')
if a.output.exists():
    p.error('Choose a new evidence directory')
data = a.invitation.read_bytes()
if len(data) > 1024 or not data.startswith(b'OPEN-RUNE-INVITE-'):
    p.error('Not a bounded Open Rune invitation')
a.output.mkdir(parents=True, mode=0o700)
adb = [a.adb, '-s', a.device]
pkg = 'org.runeclassic.port'
name = 'recovery-' + uuid.uuid4().hex + '.rune-invite'

def command(*args, **kwargs):
    return subprocess.check_output(adb + list(args), timeout=30, **kwargs)

def shell(*args):
    return command('shell', *args).decode()

def log():
    try:
        return command('exec-out', 'run-as', pkg, 'cat', 'files/native-run.log').decode()
    except subprocess.CalledProcessError:
        return ''

def wait(predicate, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = log()
        if predicate(value):
            return value
        time.sleep(.5)
    raise RuntimeError('Client did not reach the expected recovery state')

wifi_changed = False
try:
    if a.case == 'short-wifi' and shell('settings', 'get', 'global', 'wifi_on').strip() != '1':
        raise RuntimeError('Connect Wi-Fi before starting the interruption check')
    shell('am', 'force-stop', pkg)
    shell('run-as', pkg, 'rm', '-f', 'files/native-run.log')
    shell('run-as', pkg, 'mkdir', '-p', 'files/multiplayer')
    subprocess.run(adb + ['shell', 'run-as', pkg, 'sh', '-c',
                   '"cat > files/multiplayer/' + name + '"'], input=data, check=True, timeout=30)
    shell('input', 'keyevent', 'KEYCODE_WAKEUP')
    shell('wm', 'dismiss-keyguard')
    began = time.monotonic()
    shell('am', 'start', '-n', pkg + '/.MainActivity', '--es', 'saveTest', 'connection-recovery',
          '--es', 'networkInvite', name, '--ei', 'surfacePercent', '100', '--ei', 'msaa', '2')
    if a.case == 'short-wifi':
        wait(lambda s: 'OPEN_RUNE_CLIENT_JOINED' in s, 60)
        time.sleep(2)
        wifi_changed = True
        shell('svc', 'wifi', 'disable')
        try:
            time.sleep(3)
            # No accessibility dump here: waiting for an idle UI would prolong
            # the actual outage and invalidate the short-interruption case.
            (a.output / 'interrupted.log').write_text(log())
        finally:
            shell('svc', 'wifi', 'enable')
            wifi_changed = False
        value = wait(lambda s: 'OPEN_RUNE_CONNECTION waiting=0' in s, 30)
        assert 'OPEN_RUNE_CONNECTION waiting=1' in value
        assert 'OPEN_RUNE_CLIENT_STOP' not in value
        assert value.count('OPEN_RUNE_CLIENT_JOINED') == 1
    else:
        value = wait(lambda s: 'OPEN_RUNE_CLIENT_STOP' in s, 65)
        assert 'Could not reach the match. Check the invitation and host.' in value
        assert 'OPEN_RUNE_CLIENT_JOINED' not in value
        assert 'handshake_retry=started' in value
    assert 'size=2520x1680 scale=1.000000 samples=4' in value or 'scale=1.000000 samples=4' in value
    (a.output / 'client.log').write_text(value)
    (a.output / 'screen.png').write_bytes(command('exec-out', 'screencap', '-p'))
    (a.output / 'result.json').write_text(json.dumps({
        'case': a.case, 'passed': True, 'elapsed_seconds': time.monotonic() - began,
        'native_resolution': True, 'msaa': 4,
    }, indent=2) + '\n')
    print('PASS:', a.case)
finally:
    if wifi_changed:
        shell('svc', 'wifi', 'enable')
    (a.output / 'last-client.log').write_text(log())
    # This uniquely named test credential is no longer needed on the tablet.
    shell('run-as', pkg, 'rm', '-f', 'files/multiplayer/' + name)
