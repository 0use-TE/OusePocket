"""Package optional JSON/JS applications without adding them to the firmware."""
from pathlib import Path
import json,zipfile,hashlib
ROOT=Path(__file__).resolve().parents[1]
for name in ['music','log-viewer']:
    source=ROOT/'examples'/name/'src'
    package=json.loads((source/'manifest.json').read_text(encoding='utf-8'))['package']
    target=source.parent/'dist'/f"{package['id']}-{package['version']}.bpk"
    target.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(target,'w',compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source.rglob('*')):
            if path.is_file():archive.write(path,path.relative_to(source).as_posix())
    with zipfile.ZipFile(target) as archive:
        assert archive.testzip() is None
        assert {'manifest.json','app/main.js','res/profile.json'}<=set(archive.namelist())
    print(target,hashlib.sha256(target.read_bytes()).hexdigest())
