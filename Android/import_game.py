"""Import the user's existing Rune folder into a fresh, separate rooted prototype."""
import argparse
import re
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--device', required=True)
parser.add_argument('--source', required=True)
parser.add_argument('--adb', default='adb')
args = parser.parse_args()
adb = [args.adb, '-s', args.device]
package = 'org.runeclassic.port'
listing = subprocess.check_output(adb + ['shell', 'cmd package list packages -U ' + package], text=True)
match = re.search(r'^package:org\.runeclassic\.port uid:(\d+)\s*$', listing, re.MULTILINE)
if not match:
    raise SystemExit('Install Open Rune before importing assets.')
uid = match.group(1)
files = '/data/user/0/' + package + '/files'
target = files + '/game'
q = shlex.quote
command = (
    'set -eu; '
    'test -f ' + q(args.source.rstrip('/') + '/System/Rune.exe') + '; '
    'if test -e ' + q(target) + '; then echo "Game copy already exists; refusing to overwrite it." >&2; exit 1; fi; '
    'mkdir -p ' + q(target) + '; '
    'cp -R ' + q(args.source.rstrip('/') + '/.') + ' ' + q(target + '/') + '; '
    'chown -R ' + uid + ':' + uid + ' ' + q(files) + '; '
    'restorecon -RF ' + q(files)
)
subprocess.run(adb + ['shell', 'su -c ' + q(command)], check=True)
print('Imported game assets into the separate prototype. Original files were only read.')
