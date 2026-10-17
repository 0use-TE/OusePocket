"""Generate the two OIC1 timer icons from fixed geometric primitives."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
(ROOT / ".tools").mkdir(exist_ok=True)
for name, color in [("countdown", "#D98924"), ("stopwatch", "#148E88")]:
    image = Image.new("RGBA", (256, 256))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((4, 4, 252, 252), radius=58, fill=color)
    if name == "countdown":
        draw.line((76, 52, 180, 52), fill="white", width=14)
        draw.line((76, 204, 180, 204), fill="white", width=14)
        draw.line((86, 58, 86, 84, 128, 128, 170, 84, 170, 58), fill="white", width=10, joint="curve")
        draw.line((86, 198, 86, 172, 128, 128, 170, 172, 170, 198), fill="white", width=10, joint="curve")
        draw.polygon(((104, 88), (152, 88), (128, 113)), fill="white")
        draw.polygon(((98, 188), (158, 188), (128, 158)), fill="white")
    else:
        draw.rounded_rectangle((100, 34, 156, 48), radius=5, fill="white")
        draw.rectangle((121, 48, 135, 75), fill="white")
        draw.line((183, 69, 199, 85), fill="white", width=16)
        draw.ellipse((56, 76, 200, 220), outline="white", width=12)
        draw.line((128, 103, 128, 148, 156, 165), fill="white", width=10, joint="curve")
        draw.ellipse((120, 140, 136, 156), fill="white")
    image = image.resize((64, 64), Image.Resampling.LANCZOS)
    (ROOT / "apps" / name / "icon.bin").write_bytes(b"OIC1" + struct.pack("<HH", 64, 64) + image.tobytes("raw", "BGRA"))
    image.save(ROOT / ".tools" / (name + "-icon.png"))
