"""Generate the file manager's original folder icon."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
image = Image.new("RGBA", (256, 256), "#FFF3D8")
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((40, 54, 126, 119), radius=14, fill="#D99312")
draw.rounded_rectangle((38, 78, 220, 208), radius=18, fill="#EAA520")
draw.rounded_rectangle((70, 65, 183, 168), radius=8, fill="#FFFFFF")
draw.rounded_rectangle((86, 90, 164, 98), radius=4, fill="#6B97CB")
draw.rounded_rectangle((86, 111, 150, 119), radius=4, fill="#6B97CB")
draw.rounded_rectangle((32, 119, 225, 210), radius=18, fill="#FFC94A")
image = image.resize((64, 64), Image.Resampling.LANCZOS)
(root / "apps/explorer/icon.bin").write_bytes(b"OIC1"+struct.pack("<HH", 64, 64)+image.tobytes("raw", "BGRA"))
image.save(root / ".tools/explorer-icon.png")
assets = root / "apps/explorer/assets"
assets.mkdir(parents=True, exist_ok=True)
folder = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
draw = ImageDraw.Draw(folder)
draw.rounded_rectangle((6, 23, 58, 61), radius=7, fill="#D99312")
draw.rounded_rectangle((6, 43, 122, 107), radius=9, fill="#FFC94A")
document = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
draw = ImageDraw.Draw(document)
draw.rounded_rectangle((23, 8, 105, 120), radius=9, fill="#DDEAFF")
for vertical in (43, 62, 81):
    draw.rounded_rectangle((38, vertical, 90, vertical+7), radius=3, fill="#4978B8")
for name, source in [("folder", folder), ("document", document)]:
    source = source.resize((32, 32), Image.Resampling.LANCZOS)
    (assets / (name+".bin")).write_bytes(b"OIC1"+struct.pack("<HH", 32, 32)+source.tobytes("raw", "BGRA"))
