"""Build compact CC0 Pixel Frog atlases; pass the downloaded Pixel Adventure 1 ZIP.

The source archive is available free at https://pixelfrog-assets.itch.io/pixel-adventure-1.
Only the selected PNG originals and generated runtime assets belong to the app.
"""
from pathlib import Path
import argparse, hashlib, json, struct, zipfile
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'apps/跳跳庄园'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    source = zipfile.ZipFile(args.archive)
    used = {}
    def read(name):
        import io
        raw = source.read('Free/' + name)
        used[name] = hashlib.sha256(raw).hexdigest()
        return Image.open(io.BytesIO(raw)).convert('RGBA')
    def save(name, image):
        image.save(APP / 'assets' / (name + '.png'))
        (APP / 'assets' / (name + '.bin')).write_bytes(
            b'OIC1' + struct.pack('<HH', *image.size) + image.tobytes('raw', 'BGRA'))

    actors = Image.new('RGBA', (256, 192))
    animations = {}
    index = 0
    for name, frames in [('Idle', 6), ('Run', 8), ('Jump', 1), ('Fall', 1), ('Double Jump', 4), ('Hit', 2)]:
        strip = read('Main Characters/Ninja Frog/' + name + ' (32x32).png')
        animations[name] = {'first': index, 'count': frames}
        # Uniformly sample longer authored sequences while retaining their order.
        for n in range(frames):
            sample = n * (strip.width // 32) // frames
            cell = strip.crop((sample * 32, 0, sample * 32 + 32, 32))
            for face in range(2):
                k = index + face * 24
                actors.alpha_composite(ImageOps.mirror(cell) if face else cell, ((k % 8) * 32, (k // 8) * 32))
            index += 1
    save('actors', actors)
    objects = Image.new('RGBA', (256, 128))
    terrain = read('Terrain/Terrain (16x16).png')
    for skin, y in enumerate([0, 64, 128]):
        for depth in range(3):
            objects.alpha_composite(terrain.crop((112, y + depth*16, 128, y + depth*16 + 16)), (skin*48 + depth*16, 0))
    objects.alpha_composite(read('Traps/Spikes/Idle.png'), (144, 0))
    for i, name in enumerate(['Apple', 'Cherries', 'Strawberry']):
        strip = read('Items/Fruits/' + name + '.png')
        for n in range(4):
            frame = n * (strip.width // 32) // 4
            objects.alpha_composite(strip.crop((frame*32, 0, frame*32+32, 32)), (n*32, 32+i*32))
    for x, name in [(160, 'Checkpoint/Checkpoint (No Flag).png'),
                    (192, 'Checkpoint/Checkpoint (Flag Idle)(64x64).png'),
                    (224, 'End/End (Idle).png')]:
        cell = read('Items/Checkpoints/' + name).crop((0, 0, 64, 64)).resize((32, 32), Image.Resampling.NEAREST)
        objects.alpha_composite(cell, (x, 32))
    save('objects', objects)
    icon = Image.new('RGBA', (48, 48), '#7bb8b0')
    icon.alpha_composite(actors.crop((0, 0, 32, 32)).resize((48, 48), Image.Resampling.NEAREST))
    (APP/'icon.bin').write_bytes(b'OIC1'+struct.pack('<HH',48,48)+icon.tobytes('raw','BGRA'))
    icon.save(APP/'icon.png')
    (APP/'assets/animations.json').write_text(json.dumps(animations,indent=2)+'\n')
    (APP/'provenance.json').write_text(json.dumps({
        'author':'Pixel Frog', 'source':'https://pixelfrog-assets.itch.io/pixel-adventure-1',
        'license':'CC0-1.0','archiveSha256':hashlib.sha256(args.archive.read_bytes()).hexdigest(),
        'sourceFiles':used,'changes':'Selected animation frames, mirrored hero, compact atlases and reduced checkpoint sprites. Original levels and game code by OusePocket.'
    },indent=2)+'\n')
    print('Built Pixel Adventure atlases: 327680 decoded bytes; no SD resources required')

if __name__ == '__main__':
    main()
