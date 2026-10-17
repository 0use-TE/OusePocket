"""Generate an original record illustration for the native BPK image format."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
image = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
draw.ellipse((12, 12, 500, 500), fill="#DDE3F3")
draw.ellipse((26, 26, 486, 486), fill="#212944")
for inset in range(42, 190, 12):
    draw.ellipse((inset, inset, 512-inset, 512-inset), outline="#354160", width=3)
draw.arc((54, 54, 458, 458), 210, 285, fill="#8D9BB9", width=9)
draw.ellipse((178, 178, 334, 334), fill="#4078EF")
draw.ellipse((238, 238, 274, 274), fill="#F8FAFF")
image = image.resize((128, 128), Image.Resampling.LANCZOS)
destination = root / "apps/music/assets/record.bin"
destination.write_bytes(b"OIC1"+struct.pack("<HH", 128, 128)+image.tobytes("raw", "BGRA"))
image.save(root / ".tools/music-record.png")
