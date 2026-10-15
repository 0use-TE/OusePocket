"""Convert the product's images to raw pixels for the host layout check."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
output = root / 'examples/brookesia-minimal/build/host-shell-controls/raw'
output.mkdir(parents=True, exist_ok=True)
seen = set()
for directory in ['components/brookesia_app_settings/package/res/images',
                  'components/brookesia_system_super/resource/shell/images',
                  'examples/music/src/res/images', 'examples/log-viewer/src/res/images',
                  'examples/resource-monitor/src/res/images']:
    for path in (root / directory).rglob('*'):
        if path.suffix.lower() not in {'.png', '.jpg', '.jpeg'}:
            continue
        assert path.name not in seen, f'Duplicate fixture name: {path}'
        seen.add(path.name)
        with Image.open(path) as source:
            image = source.convert('RGBA')
            if path.name.endswith('_glyph.png'):
                assert source.mode=='RGBA' and source.size==(42,42),path
                assert image.getchannel('A').getextrema()==(0,255),f'Glyph lacks transparency: {path}'
                assert image.getpixel((0,0))[3]==0,f'Glyph has opaque corner: {path}'
            (output / (path.name + '.bgra')).write_bytes(image.tobytes('raw', 'BGRA'))
            (output / (path.name + '.size')).write_text(f'{image.width} {image.height}')
