"""Read battery and thermal state during a fixed-duration, unplugged A/B run.

Does not launch the game or change device settings. Run android_present.py in
parallel for independent displayed FPS. Temperatures are sensor readings, not
surface-temperature or power measurements. Use matched scene/brightness/start
conditions and cooldown between modes.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--device', required=True)
p.add_argument('--seconds', type=int, default=180)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
if a.seconds < 10:
    p.error('--seconds must be at least 10')

def shell(command):
    return subprocess.check_output(['adb', '-s', a.device, 'shell', command], text=True, timeout=15)

start = time.monotonic()
rows = []
next_sample = start
while True:
    battery = shell('dumpsys battery')
    row = {'seconds': round(time.monotonic() - start, 2), 'battery_raw': battery}
    for name, pattern in [('battery_c', r'^  temperature: (\d+)'), ('phone_c', r'^  PhoneTemp: (\d+)'),
                          ('level', r'^  level: (\d+)'), ('voltage_mv', r'^  voltage: (\d+)'),
                          ('current_reported_ma', r'^  Battery current : (-?\d+)')]:
        match = re.search(pattern, battery, re.M)
        if match:
            row[name] = int(match[1]) / (10 if name.endswith('_c') else 1)
    row['charging'] = bool(re.search(r'(?:AC|USB|Wireless|Dock) powered: true', battery))
    row['brightness'] = shell('settings get system screen_brightness').strip()
    row['thermal_raw'] = shell('dumpsys thermalservice')
    rows.append(row)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(rows, indent=2) + '\n')
    print(json.dumps({k: v for k, v in row.items() if not k.endswith('_raw')}), flush=True)
    if row['charging']:
        raise SystemExit('Charging detected: comparison is confounded; stop and repeat unplugged.')
    if time.monotonic() - start >= a.seconds:
        break
    next_sample += 5
    time.sleep(max(0, min(next_sample, start + a.seconds) - time.monotonic()))
