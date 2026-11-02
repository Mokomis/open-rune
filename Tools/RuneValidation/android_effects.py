"""Compare cosmetic fire/combat effects; preserves settings and isolates game saves.

Fixed-step captures check combat at equal simulation frames. Live
samples measure displayed frames separately after warm-up, in ABBA order.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--device', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--samples', type=int, default=60)
p.add_argument('--fixed-only', action='store_true', help='Capture the two isolated previews only')
p.add_argument('--scene', choices=('combat', 'hall'), default='combat')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
adb = ['adb', '-s', a.device]
pkg = 'org.runeclassic.port'
def shell(*args):
    return subprocess.check_output(adb + ['shell', *args], timeout=25).decode()
def readlog():
    return shell('run-as', pkg, 'cat', 'files/native-run.log')
def savefile(remote, dest):
    dest.write_bytes(subprocess.check_output(adb + ['exec-out', 'run-as', pkg, 'cat', remote], timeout=25))
settings = subprocess.check_output(adb + ['exec-out', 'run-as', pkg, 'cat', 'shared_prefs/MainActivity.xml'])
results = {}
try:
    for label, driver, fixed in [('fixed-original', 'false', True), ('fixed-enhanced', 'true', True),
                                 ('live-original-1', 'false', False), ('live-enhanced-1', 'true', False),
                                 ('live-enhanced-2', 'true', False), ('live-original-2', 'false', False)]:
        if a.fixed_only and not fixed: continue
        shell('am', 'force-stop', pkg)
        shell('run-as', pkg, 'rm', '-f', 'files/native-run.log', 'files/frame.ppm')
        opts = ['--ei', 'frames', '735', '--ei', 'probeTickHz', '120'] if fixed else []
        if fixed: opts += ['--ez', 'effectsProbe', 'true']
        scene = (['--es', 'enemy', 'GoblinWarrior', '--ei', 'enemyFrame', '120',
                  '--es', 'shield', 'VikingShield', '--ei', 'fire', '60', '--ei', 'fireHold', '8',
                  '--ei', 'fireRepeat', '60', '--ei', 'block', '200', '--ei', 'blockHold', '100000',
                  '--ez', 'endurance', 'true', '--ez', 'traceCombat', 'true', '--ez', 'checkReach', 'true']
                 if a.scene == 'combat' else ['--es', 'probeStart', 'PlayerStart1'])
        shell('am', 'start', '-W', '-n', pkg+'/.MainActivity', '--es', 'saveTest', 'effects-compare',
              '--es', 'map', 'DM-Hildir.run', '--es', 'vulkanDriver', 'system', '--ez', 'fireEffects', driver,
              '--ez', 'upscale', 'false', '--ei', 'msaa', '2', '--ei', 'seed', '1',
              '--ei', 'profile', '600', '--ei', 'worldFeatures', '8103', '--ei', 'clarity', '7',
              '--ez', 'enhanced', 'true', *scene, *opts)
        marker = 'BOUNDED_RUN_COMPLETE' if fixed else 'RUNE_PROFILE frames=600'
        deadline = time.monotonic()+120
        while True:
            time.sleep(1)
            log = readlog()
            if marker in log: break
            if 'ANDROID_LAUNCH_FAILED' in log or time.monotonic() > deadline:
                (a.output/(label+'.log')).write_text(log)
                raise RuntimeError(label+' did not complete')
        assert 'size=2520x1680 scale=1.000000 samples=4' in log
        assert 'id=8 ' in log
        assert 'RUNE_EFFECTS_MODE enabled='+('1' if driver=='true' else '0') in log
        if a.scene == 'combat': assert 'RUNE_ENDURANCE_BEGIN' in log
        if fixed:
            savefile('files/frame.ppm', a.output/(label+'.ppm'))
        else:
            # Exclude launch/initial pipeline creation from actual presentation.
            subprocess.run([sys.executable, str(Path(__file__).with_name('android_present.py')),
                '--device', a.device, '--samples', str(a.samples), '--oppo-clocks',
                '--output', str(a.output/label)], check=True, timeout=a.samples*2+40)
            (a.output/(label+'.png')).write_bytes(subprocess.check_output(adb+['exec-out','screencap','-p'], timeout=25))
            log = readlog()
        (a.output/(label+'.log')).write_text(log)
        assert not any(e in log for e in ('ANDROID_LAUNCH_FAILED', 'Script error:', 'Unknown native'))
        results[label] = {'enhanced': driver=='true', 'fixed_step': fixed,
            'profiles': [l for l in log.splitlines() if l.startswith(('RUNE_PROFILE ', 'RUNE_VULKAN_PROFILE ', 'RUNE_VULKAN_DRIVER ', 'RUNE_ENDURANCE_ACTIVE '))]}
        if not fixed: results[label]['display'] = json.loads((a.output/(label+'.json')).read_text())
        (a.output/'results.json').write_text(json.dumps(results, indent=2)+'\n')
        print(label, results[label], flush=True)
    # Equal fixed-step gameplay observations, not wall-time-based replay equivalence.
    def encounters(label):
        return [l for l in (a.output/(label+'.log')).read_text().splitlines() if l.startswith(('RUNE_ENCOUNTER ', 'RUNE_ENDURANCE_ACTIVE '))]
    if a.scene == 'combat':
        results['fixed_combat_observations_match'] = bool(encounters('fixed-original')) and encounters('fixed-original') == encounters('fixed-enhanced')
        if not results['fixed_combat_observations_match']:
            results['comparison_limit'] = 'Combat diverged despite fixed timestep; do not claim identical combat workloads. Use the fixed-camera hall comparison to isolate rendering cost.'
finally:
    current = subprocess.check_output(adb+['exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml'])
    results['settings_unchanged'] = current == settings
    results['settings_sha256'] = hashlib.sha256(current).hexdigest()
    (a.output/'results.json').write_text(json.dumps(results, indent=2)+'\n')
    shell('am', 'force-stop', pkg)
    shell('am', 'start', '-W', '-n', pkg+'/.MainActivity')
