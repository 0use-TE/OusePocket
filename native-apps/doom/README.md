# Doom on OusePocket

Standalone ESP32-P4 ELF port of
[DoomGeneric](https://github.com/ozkl/doomgeneric), commit
`e257a09527062bfb986b6e5e0780839b6cbd5b71`. The GPL-2.0 license is in
`LICENSE`; original copyright notices remain in the vendored engine files.
Game assets are not included in this repository.

The desktop entry is **毁灭战士 / Doom** (`ouse.doom`). Its program is installed
separately as `/bpkfs/ouse-native/doom.elf`; Quake II keeps its own program and
data. The native runtime runs one game at a time. Switching games stops the
previous worker before loading the next one.

The original 320×200 indexed image fills the landscape 480×320 screen, with
the game's own HUD and menus. Screen buffers, executable image, 8 MiB Doom
zone, sound cache and 256 KiB game stack use PSRAM. The loader retains its
internal stack because executable mapping briefly disables caches.

The port supplies 16-channel sound effects through the existing board media
service at 16 kHz mono. It retains DMX samples in the Doom zone and resamples
them while mixing. PCM is copied to the board consumer; no codec task calls
back into the ELF. Short writes retain the unsent PCM, and waiting for game
tics continues feeding audio. Rendering is limited to roughly 35 FPS.
The board audio worker runs above the LVGL task so continuous fullscreen
flushes cannot starve the codec. The real-device test checks consumption near
16,000 samples/second while firing with the display active.
Background music is currently disabled; this port does not provide a MIDI/OPL
music synthesizer or multiplayer.

Build the host firmware, then:

```powershell
python tools/build-doom.py
python tools/doom.py install
```

Place the original Doom 1.9 shareware IWAD at `/Ouse/Downloads/doom1.wad` on
the SD card. Its size is 4,196,020 bytes and MD5 is
`f0cefca49926d00903cf57551d901abe`. The device can download it from your own
HTTP server:

```powershell
python tools/doom.py data http://YOUR_PC_LAN_IP:8765/doom1.wad
python tools/doom.py start
python tools/doom.py play
```

`tools/doom-control.cmd` starts the game and opens the controller. Click the
controller to capture the mouse; F8 releases it. W/S move forward/backward,
A/D strafe, mouse X turns, left click fires, E/Space/right click uses doors,
Shift runs, 1–7 select acquired weapons, Enter confirms and Esc opens the
menu. Original Doom has no jumping or vertical mouse look. Raw mouse counts
use the existing serial transport and the engine's sensitivity setting.
Keys expire after 750 ms without a controller heartbeat; focus loss releases
all controls. Configuration and saves live under `/Ouse/Doom/`.

Press Home/swipe out to stop the game; reopening restarts it. Close and Home
restore the prior screen orientation, stop audio and free every tracked
allocation and file before unloading the ELF. The engine's error and quit
paths exit the game worker instead of exiting or resetting the firmware.

Local engine changes are confined to selecting the SD configuration path,
applying WASD after saved settings, posting serial mouse events, removing
desktop error-dialog dependencies and avoiding a TLS `errno` import for
file probing. The indexed output allocation matches `sizeof(pixel_t)` and
checks allocation failure. Platform and effect mixer code lives in `main.c`
and `audio.c`.

Validation commands:

```powershell
python tools/test-native-elf.py
python tools/test-quake2-controls.py
python tools/test-doom-device.py
python tools/test-quake2-device.py --audio
```

The device test enters shareware E1M1, captures the screen, checks firing PCM
and codec consumption, exercises movement/turning/weapons and input expiry,
then verifies Home, cached reopen, foreground close and heap integrity.
Local evidence is written under `.tools/doom-check/` and the referenced UART
capture directory. PCM delivery is measured; listening on the device is
needed to assess audible quality.

## Device verification (2026-10-22)

COM10: firmware write/hash verification passed. The installed ELF is 371,676
bytes. The original shareware WAD on SD was verified as 4,196,020 bytes,
CRC32 `162b696a`, matching the local MD5-verified source.

E1M1 movement and turning screenshots show only game content at 480×320.
During the firing test nonzero samples increased from 22,895 to 57,240,
and actual codec consumption averaged 16,061 samples/second with the display
active. Game-tracked allocations were 8,488,094 bytes; Home/reopen/close
returned them to zero and PSRAM free returned to about 24.3 MB. Heap integrity
passed. Switching between Doom and Quake II passed, and Quake II's menu/map,
audio, serial input and cleanup regression passed with the final firmware.

Evidence: `.tools/doom-check/result.json`, `.tools/doom-device.log`,
`.tools/doom-assets.log`, `.tools/doom-switch.log`,
`.tools/doom-quake-regression.log`, `.tools/doom-validator.log` and
`.tools/doom-board-audio-tests.log`. Validation covers this shareware map and
these inputs; the remaining levels and music are not covered.
