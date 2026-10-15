"""Generate the original twenty-second melody embedded in Pocket firmware.

Uses only the Python standard library; no downloaded recordings or dependencies.
Run manually after editing the melody. Firmware builds use the checked-in WAV.
"""
from array import array
import math
from pathlib import Path
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "components/pocket_platform/assets/pocket-test.wav"
# This synthetic melody's harmonics fit below 4 kHz. Keep its full duration
# while leaving room for the firmware in the existing 8 MiB application slot.
SAMPLE_RATE = 8000
DURATION_SECONDS = 20
PHRASE = (60, 64, 67, 72, 69, 65, 62, 67, 64, 69, 71, 67, 62, 65, 64, 60)
NOTE_COUNT = (SAMPLE_RATE * DURATION_SECONDS - int(0.3 * SAMPLE_RATE)) // int(0.32 * SAMPLE_RATE)
NOTES = tuple(PHRASE[index % len(PHRASE)] for index in range(NOTE_COUNT))


def main():
    samples = array("h", [0]) * (SAMPLE_RATE * DURATION_SECONDS)
    start = int(0.3 * SAMPLE_RATE)
    step = int(0.32 * SAMPLE_RATE)
    sounding = int(0.28 * SAMPLE_RATE)
    for index, note in enumerate(NOTES):
        frequency = 440 * 2 ** ((note - 69) / 12)
        for offset in range(sounding):
            time = offset / SAMPLE_RATE
            # Fade both edges to avoid clicks; leave headroom at system volume.
            envelope = min(1.0, time / 0.015, (sounding - 1 - offset) / (0.08 * SAMPLE_RATE))
            phase = 2 * math.pi * frequency * time
            value = (0.8 * math.sin(phase) + 0.17 * math.sin(2 * phase)
                     + 0.03 * math.sin(3 * phase))
            samples[start + index * step + offset] = round(32767 * 0.3 * envelope * value)
    if sys.byteorder != "little":
        samples.byteswap()
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(TARGET), "wb") as output:
        output.setparams((1, 2, SAMPLE_RATE, len(samples), "NONE", "not compressed"))
        output.writeframes(samples.tobytes())
    print(f"Generated {TARGET}: {DURATION_SECONDS}s, {SAMPLE_RATE} Hz, mono PCM16, {TARGET.stat().st_size} bytes")


if __name__ == "__main__":
    main()
