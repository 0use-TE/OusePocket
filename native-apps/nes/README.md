# NES on OusePocket

The **红白机 / NES** desktop app (`ouse.nes`) loads Nofrendo as an ABI 2
ELF module, alongside Doom and Quake II. One native game runs at a time.
This integrates Retro-Go's NES core into OusePocket; it does not replace
the desktop with Retro-Go firmware.

`core/` comes from [Retro-Go](https://github.com/ducalex/retro-go), commit
`4ced120669750ca7228fd0414211430c1d923166`, directory
`retro-core/components/nofrendo`. Original source notices, `LICENSE`
(upstream COPYING), and `CREDITS` are retained. The local change in
`core/nes/utils.h` connects the portable CRC32 fallback to the adapter's
implementation, enabling cartridge database matching. The platform adapter
`main.c` is GPL-2.0-or-later.

`demo/` contains the public-domain [2048 NES game](https://github.com/mmuszkow/2048-nes)
at commit `d0b2841384b7881619e53863d5bb7a27263ccccd`, including its ROM,
source, and Unlicense dedication. No commercial game ROM is bundled.

## Build and install

Build the host firmware with ESP-IDF first. The firmware must include the
NES desktop registration and host imports; installing the ELF alone on an
older firmware is insufficient. Then run:

```powershell
python tools/build-nes.py
python tools/nes.py install
python tools/nes.py start
python tools/nes.py play
```

The CLI defaults to COM10; use `--port COMx` to override it. Installation
stores `nes.elf` in `/bpkfs/ouse-native/nes.elf` and the 2048 demo in
`/sdcard/Ouse/Downloads/2048.nes`. `tools/nes-control.cmd` starts the game
and opens the PC controller. Close other serial controllers first.

To upload and select your own compatible iNES ROM (up to 2 MiB):

```powershell
python tools/nes.py rom C:\Games\example.nes
python tools/nes.py start
python tools/nes.py play
```

Uploads use CRC-based filenames and verify the committed data. To select an
existing SD file, use `python tools/nes.py select Downloads/example.nes`.
Selection lasts until reboot; the default after reboot is 2048. NES 2.0,
FDS disks, NSF files, multiplayer and save-state UI are not provided.
Mapper support follows the vendored Nofrendo core; only the bundled NROM
game has been tested on this device. Other ROM compatibility is unverified.

## Controls and runtime

| PC input | NES input |
| --- | --- |
| Arrow keys / WASD | D-pad |
| Z / J / Space | A |
| X / K | B |
| Enter | Start |
| Tab / Shift | Select |
| R | Soft reset |
| Esc | Close PC controller and release inputs |

The PC window also provides clickable buttons. Losing focus releases all
buttons; held keys expire on the device after 750 ms without a heartbeat.
In 2048, press Enter to start, release direction keys to move, and use
Select to toggle music. Home stops the game and restores portrait mode;
reopening the desktop app restarts it. Battery-backed cartridges save SRAM
to `/sdcard/Ouse/NES/<cartridge-crc32>.sav` on a normal exit.

The NES image is 256×240, stretched to the full landscape 480×320 panel
without surrounding system UI. Emulation targets 60 Hz NTSC / 50 Hz PAL
and presents every other frame. Executable code, game stack and bulk
buffers use PSRAM. Audio uses the existing board service and ES8311 codec
at 16 kHz mono, including NES music and sound effects.

## Validation

`python tools/test-nes-device.py` checks the installed firmware hash,
2048 title/gameplay, serial Start and movement, nonzero audio and playback
rate, Home/reopen/close, malformed ROM rejection, allocation cleanup and
heap integrity. On the ESP32-P4 device, 2048 used 113,844 tracked bytes
(excluding ELF, host display buffers and stack), and audio consumption
measured about 16,075 samples/second. This is one game measurement, not a
compatibility or performance guarantee for every cartridge.

Host checks: `tools/test-game-session.py`, `tools/test-nes-controls.py`,
and `tools/test-native-elf.py` (production loader validation).
