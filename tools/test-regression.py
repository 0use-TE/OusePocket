"""Run the offline kernel regression suite; never accesses a device or flashes firmware."""
from pathlib import Path
import argparse
import json
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
CASES = {
    'ui': [sys.executable, 'tools/test-ouse-core.py'],
    'kernel': [sys.executable, 'tools/test-ouse-kernel.py'],
    'validation': [sys.executable, 'tools/test-ouse-validation.py'],
    'system': [sys.executable, 'tools/test-ouse-system.py'],
    'protocol': [sys.executable, 'tools/test-protocol-boundaries.py'],
    'services': [sys.executable, 'tools/test-ouse-services.py'],
    'files': [sys.executable, 'tools/test-ouse-files.py'],
    'sd-board': [sys.executable, 'tools/test-sd-board-recovery.py'],
    'launcher-storage': [sys.executable, 'tools/test-launcher-storage.py'],
    'power-off': [sys.executable, 'tools/test-power-off.py'],
    'ble-probe': [sys.executable, 'tools/test-ble-probe.py'],
    'ble-service': [sys.executable, 'tools/test-ble-service.py'],
    'audio': [sys.executable, 'tools/test-ouse-audio.py'],
    'mixing': [sys.executable, 'tools/test-ouse-audio.py', '--mixing'],
    'capture': [sys.executable, 'tools/test-ouse-audio.py', '--recording-format'],
    'sd-recording': [sys.executable, 'tools/test-ouse-audio.py', '--sd-recording'],
    'permissions': [sys.executable, 'tools/test-ouse-permissions.py'],
    'channels': [sys.executable, 'tools/test-network-channels.py'],
    'countdown': ['node', 'tools/test-countdown-app.cjs'],
    'network-app': ['node', 'tools/test-network-app.cjs'],
    'recorder-app': ['node', 'tools/test-recorder-app.cjs'],
    'netease-app': ['node', 'tools/test-netease-app.cjs'],
    'mouse-app': ['node', 'tools/test-mouse-app.cjs'],
    'weather-app': ['node', 'tools/test-weather-app.cjs'],
    'store-app': ['node', 'tools/test-store-app.cjs'],
    'file-open-apps': ['node', 'tools/test-file-open-apps.cjs'],
    'package-requests': [sys.executable, 'tools/test-package-requests.py'],
    'packages': [sys.executable, 'tools/test-ouse-app-packages.py', '--apps', 'countdown', 'network', 'camera', 'gallery', 'music', 'recorder', 'store', 'explorer'],
    'image-updates': [sys.executable, 'tools/test-image-updates.py'],
    'runtime-errors': [sys.executable, 'tools/test-runtime-errors.py'],
    'bluetooth-runtime': [sys.executable, 'tools/test-bluetooth-runtime.py'],
    'bluetooth-ownership': [sys.executable, 'tools/test-bluetooth-ownership.py'],
    'wifi': [sys.executable, 'tools/test-ouse-wifi.py'],
    'sd-write': [sys.executable, 'tools/test-sd-write-verification.py'],
    'camera-pipeline': [sys.executable, 'tools/test-camera-pipeline.py'],
    'camera-preview': [sys.executable, 'tools/test-camera-preview.py'],
    'camera-apps': ['node', 'tools/test-camera-apps.cjs'],
    'survivor': ['node', 'tools/test-survivor.cjs'],
    'game2d': [sys.executable, 'tools/test-game2d.py'],
    'native-elf': [sys.executable, 'tools/test-native-elf.py'],
    'nes-controls': [sys.executable, 'tools/test-nes-controls.py'],
    'quake2-controls': [sys.executable, 'tools/test-quake2-controls.py'],
    'hosted-memory': [sys.executable, 'tools/test-hosted-memory.py'],
    'display-flush': [sys.executable, 'tools/test-display-flush-wake.py'],
    'lcd-async': [sys.executable, 'tools/test-lcd-async.py'],
    'lcd-window': [sys.executable, 'tools/test-lcd-frame-window.py'],
    'game-session': [sys.executable, 'tools/test-game-session.py'],
}
DEPENDENCIES = {
    'power-off': ['system'],
    'image-updates': ['ui', 'packages'],
    'package-requests': ['kernel'],
    'validation': ['kernel'],
    'services': ['kernel'],
    'files': ['system'],
    'sd-board': ['system'],
    'launcher-storage': ['system'],
    'ble-probe': ['system'],
    'ble-service': ['kernel'],
    'audio': ['ui'],
    'mixing': ['ui'],
    'capture': ['ui'],
    'sd-recording': ['ui'],
    'permissions': ['kernel'],
    'channels': ['kernel'],
    'packages': ['ui', 'kernel'],
    'runtime-errors': ['ui', 'kernel', 'packages'],
    'bluetooth-runtime': ['kernel', 'packages'],
    'bluetooth-ownership': ['kernel'],
    'wifi': ['ui', 'kernel'],
    'sd-write': ['ui'],
    'camera-pipeline': ['ui'],
    'camera-preview': ['ui'],
    'game2d': ['ui', 'packages'],
    'hosted-memory': ['system'],
    'display-flush': ['system'],
}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--only', nargs='+', choices=CASES)
args = parser.parse_args()
output = ROOT / '.tools/regression'
output.mkdir(parents=True, exist_ok=True)
results = []
selected = set(args.only or CASES)
pending = list(selected)
while pending:
    for dependency in DEPENDENCIES.get(pending.pop(), []):
        if dependency not in selected:
            selected.add(dependency)
            pending.append(dependency)
for name in CASES:
    if name not in selected:
        continue
    started = time.monotonic()
    print('RUN ' + name, flush=True)
    try:
        with (output / (name + '.log')).open('w', encoding='utf-8') as log:
            result = subprocess.run(CASES[name], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=300)
        code = result.returncode
    except (OSError, subprocess.TimeoutExpired) as error:
        code = -1
        (output / (name + '.log')).write_text(str(error), encoding='utf-8')
    results.append(dict(name=name, passed=code == 0, code=code, seconds=round(time.monotonic()-started, 2)))
    print(('PASS ' if code == 0 else 'FAIL ') + name, flush=True)
(output / 'report.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
raise SystemExit(0 if all(result['passed'] for result in results) else 1)
