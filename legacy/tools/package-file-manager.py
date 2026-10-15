"""Build an unsigned development BPK (standard ZIP, accepted by the product firmware)."""
from pathlib import Path
import json
import zipfile
root = Path(__file__).resolve().parents[1]
source = root / 'examples/file-manager/src'
manifest = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
package = manifest['package']
output = root / 'examples/file-manager/dist' / f"{package['id']}-{package['version']}.bpk"
output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(source.rglob('*')):
        if path.is_file(): archive.write(path, path.relative_to(source).as_posix())
with zipfile.ZipFile(output) as archive:
    assert archive.testzip() is None
    assert 'res/profile.json' in archive.namelist()
print(output)
