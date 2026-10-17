"""Prepare the existing example and three public-domain Mozart MIDI recordings."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request
import wave

ROOT = Path(__file__).resolve().parents[1]
TRACKS = [
    ("allegro", "https://upload.wikimedia.org/wikipedia/commons/0/03/K545_allegro.ogg"),
    ("andante", "https://upload.wikimedia.org/wikipedia/commons/b/b8/K545_andante.ogg"),
    ("rondo", "https://upload.wikimedia.org/wikipedia/commons/1/11/K545_rondo.ogg"),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / ".tools/sd-music")
    parser.add_argument("--ffmpeg")
    args = parser.parse_args()
    ffmpeg = args.ffmpeg or shutil.which("ffmpeg")
    if not ffmpeg:
        import imageio_ffmpeg
        ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    args.output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "apps/music/assets/fur-elise-30s.wav", args.output / "fur-elise-30s.wav")
    for movement, url in TRACKS:
        source = args.output / (movement + ".ogg")
        if not source.exists():
            request = urllib.request.Request(url, headers={"User-Agent": "OusePocket-music-samples/0.17"})
            with urllib.request.urlopen(request, timeout=60) as response:
                data = response.read(16 * 1024 * 1024 + 1)
            if len(data) > 16 * 1024 * 1024:
                raise ValueError("Source exceeds download budget")
            source.write_bytes(data)
        target = args.output / ("mozart-" + movement + ".wav")
        subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(source),
            "-ac", "1", "-ar", "16000", "-af", "volume=0.6,lowpass=f=7000,afade=t=in:d=0.05",
            "-c:a", "pcm_s16le", str(target)], check=True)
    license_text = (ROOT / "apps/music/AUDIO-LICENSE.md").read_text(encoding="utf-8")
    license_text += "\n\nAdditional SD recordings: Mozart, Piano Sonata No.16 K545, Allegro / Andante / Rondo.\n"
    license_text += "Public-domain MIDI renderings uploaded to Wikimedia Commons by Ryo xp in 2007.\n"
    license_text += "The file description pages identify these recordings with the Public Domain Mark 1.0.\n"
    for movement, url in TRACKS:
        license_text += f"{movement}: https://commons.wikimedia.org/wiki/File:K545_{movement}.ogg\nOriginal: {url}\n"
    license_text += "SD derivatives: full length, PCM16 WAV, mono 16000 Hz, reduced gain and short fade-in.\n"
    (args.output / "LICENSES.txt").write_text(license_text, encoding="utf-8")
    records = []
    for file in sorted(args.output.glob("*.wav")):
        with wave.open(str(file)) as audio:
            duration = audio.getnframes() / audio.getframerate()
        records.append({"file": file.name, "bytes": file.stat().st_size, "seconds": round(duration, 2),
                        "sha256": hashlib.sha256(file.read_bytes()).hexdigest()})
    (args.output / "manifest.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
    print(json.dumps(records, indent=2))


if __name__ == "__main__":
    main()
