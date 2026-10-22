# Quake II on OusePocket (experimental)

This is a standalone, trusted ESP32-P4 ELF program. It uses the software
renderer at 320×240 and the host's display, SD card and serial input bridge.
Its output fills the 480×320 landscape panel with no surrounding app controls,
status bar or navigation bar. Nearest-neighbor scaling keeps the renderer's
existing memory budget. Leaving the game restores the previous orientation;
the system's edge swipe still returns Home. Reopening a paused game restarts it.
The port supports single-player with native sound effects.

Engine source: [alexkid77/ESP32_QUAKE2](https://github.com/alexkid77/ESP32_QUAKE2),
commit `fcbd56f` (see the engine files for original copyright notices).
The Quake II engine is GPL; its license is included in `LICENSE`. This directory
contains source, not the copyrighted game maps/textures. The upstream SIMD
memory implementation is replaced by standard C library calls in `compat/`.

Local changes to the engine: PSRAM section attributes become ordinary ELF BSS;
the 7 MiB model pool is carved from the engine arena instead of reserving a
second pool. `Com_sprintf` uses bounded formatting instead of a 64 KiB stack
scratch buffer. ESP heap/logging headers are replaced by host compatibility
headers. Networking is a local loopback implementation. The native entry sets
the base directory, enables sound, opens the menu, and starts the demo map
without its introductory cinematic.

Build the host firmware first, then:

```powershell
python tools/build-quake2.py
python tools/quake2.py install
```

The game ELF is saved separately from the native counter demo. Obtain the
original Quake II 3.14 demo yourself, extract `Install/Data/baseq2/pak0.pak`,
and put it on the SD card as `/Ouse/Downloads/quake2.pak`. To omit the unused
sounds and recorded demo (about 30 MB), prepare a separate local test copy:

```powershell
python tools/quake2-data.py path/to/pak0.pak dist/quake2-test.pak
```

For a local HTTP
server hosting that file, the device can fetch it with:

```powershell
python tools/quake2.py data http://YOUR_PC_LAN_IP:8765/pak0.pak
```

For an existing base package stripped of sounds, add a separate sound PAK:

```powershell
python tools/quake2-data.py path/to/pak0.pak dist/quake2-sound.pak --sounds-only
python tools/quake2.py data http://YOUR_PC_LAN_IP:8765/quake2-sound.pak --sounds
```

This writes `/Ouse/Downloads/quake2-sound.pak`, mounted as `baseq2/pak1.pak`.
A complete original base PAK already includes sounds and needs no extra PAK.
Game assets remain outside Git.

Sound uses the existing media service and sole ES8311 codec worker at
16 kHz, 16-bit mono. The game-owned 16 KiB sample ring and 8-bit resampled
effect cache use PSRAM. Cache allocation matches the converted sample width.
The shared engine arena can spill into remaining session-tracked PSRAM during
sound/texture registration, and texture allocation failures exit safely.
The upstream 8-bit mixer volume-table index is corrected to the 0..31 range.
The media service copies samples into its bounded buffer; the codec's actual
consumed sample count clocks the game mixer. Audio focus and alarm handling
follow regular media playback. Exit, cancellation and codec failure stop the
producer; parent cleanup awaits codec release before unloading the ELF.
The game can continue silently if audio is unavailable.

`DBG_QAUDIO` reports written samples, nonzero samples, peak and codec-consumed
samples. `python tools/test-quake2-device.py --audio` verifies firing samples,
codec progress, fullscreen, serial controls and cleanup on the board.

The download runs on the device and commits the final file only after
completion. The command reports progress and waits; `--no-wait` returns after
submission. The existing file service refuses to overwrite an existing file.
Game configuration/saves live in `/Ouse/Quake2/baseq2` on the SD card.

Open the desktop's 雷神之锤2 icon, or:

```powershell
python tools/quake2.py start
python tools/quake2.py play
```

Click the computer control window to capture the mouse. Windows Raw Input
relative counts go directly to serial; the hidden pointer is confined so
continuous turning has no window-edge limit. F8 releases/recaptures the mouse;
Esc remains the in-game menu key. W/S move forward/back, A/D strafe, Space
jumps, and the left mouse button fires. Enter confirms. Top-row and numpad
1–9/0 select the original Quake II weapon slots (weapons must be acquired).
The WASD/number layout is applied after saved configuration at game startup.
Focus loss/closing the window releases the mouse and keys; if serial input stops, held keys
expire after 750 ms. Only one program may own the serial port at a time.
Returning to the device desktop cooperatively stops the game and releases
its arena, files, executable mapping and SD lease.

`python tools/quake2.py status` queries the native state and frame/allocation
counters; `stop` asks the worker to exit. `tools/quake2-control.cmd` starts the
game and opens the computer controller. This is experimental native code with full device
privileges, using the separate ABI 2 game import profile rather than the
small ABI 1 counter interface.

The code/data image, allocation arena (including the model pool), 256 KiB
game task stack, 320×240 RGB565 pending buffer and 480×320 RGB565 display buffer use PSRAM.
Hardware DMA buffers and the small system control structures remain in
internal SRAM. A separate loader uses a 16 KiB internal stack because changing
MMU mappings briefly disables caches. It starts the game on its 256 KiB PSRAM
stack, waits for its return, deletes the game task, closes files/frees the arena,
then unmaps the image. Installation and ordinary ABI 1 programs retain a 12 KiB
internal stack. Game configuration is written only to the SD card.

The host keeps a 128-event FIFO in PSRAM. The engine consumes at most 32 key
edges per frame, staying below its own 64-entry input queue. This preserves
quick down/up taps and typed key order. Mouse deltas are accumulated separately.

## Device verification (2026-10-22)

COM10: firmware flashing and SHA256 verification passed. The 727,052-byte
module opened the menu and entered `demo1`; captured map views showed roughly
14–18 FPS in these scenes. Real serial key/mouse events changed the view.
Back-to-back Enter down/up opened the menu and started the map. Lost input
heartbeats released the keys. Returning Home stopped the game and reduced its
tracked allocations to zero; PSRAM free returned to about 24.3 MB, and the
heap integrity check passed. During a menu session PSRAM free was about 2.3 MB;
internal SRAM free was about 126 KB, including the loader's 16 KiB stack.

The computer controller's Tk callbacks were exercised against the real board
with its window hidden. Original ABI 1 hello still returned 100 afterward.
The installed test PAK was verified as 19,976,927 bytes, CRC32 `7d7fc071`.
This local copy excludes sound and recorded attract-demo playback; it retains
the three single-player maps. Game data is not part of this repository.

Evidence: `.tools/quake-check/` (screenshots/results),
`.tools/quake-game-test.log`, `.tools/quake-controller-test.log` and the UART
directories referenced by those results. Verification covers this demo map
and these inputs, not the complete game's levels, audio or multiplayer.

Sound was flashed and verified on the same board: the sound-only demo PAK is
29,835,832 bytes, CRC32 `58fb11d5`. During a firing test nonzero PCM samples
increased from 28,657 to 62,447 and codec-consumed samples from 106,432 to
140,992. Map input, landscape fullscreen and Home cleanup passed. Tracked
allocations returned to zero, PSRAM free returned to about 24.3 MB and heap
integrity passed. This verifies PCM delivery to the real codec; audible quality
still requires listening on the device. Evidence: `.tools/quake-audio-check/`,
`.tools/quake-audio-device.log`, `.tools/quake-audio-assets.log`.

The landscape fullscreen update was flashed and verified separately:
`.tools/quake-fullscreen-check/` contains 480×320 menu/map captures with only
game content. Serial movement/input expiry and heap checks passed again.
An actual bottom-edge swipe restored the 320×480 desktop; reopening the cached
app restarted the game in fullscreen. Closing it while foreground also restored
the original orientation. See `.tools/quake-fullscreen-lifecycle.log`.

The Windows controller uses foreground Raw Input rather than cursor-coordinate
differences. It sends movement at up to roughly 60 updates/second as serial
latency permits, keeping the original integer deltas and splitting large
updates into protocol-sized chunks. Focus loss, F8, window close and transport
errors release the cursor. Keyboard keepalives refresh at 200 ms intervals.

Controller checks: `python tools/test-quake2-controls.py` verifies the native
HWND hook, relative-packet decoding, cursor release, WASD/digit wire events,
large-delta preservation and focus/disconnect handling without taking mouse
focus. The real Windows WM_INPUT path also delivered an injected `(9,-4)`
relative movement unchanged during local verification.

The updated 727,740-byte module was installed on COM10. Engine console queries
confirmed `w/s/a/d` -> `+forward/+back/+moveleft/+moveright`, and weapon bindings
1/2/3/4/0. Live keys 2/3/4 correctly reported unavailable Shotgun/Super Shotgun/
Machinegun on a fresh player; no weapons were granted for testing. Movement,
mouse input, normal stop and heap integrity passed. See
`.tools/quake-controls-device-check.log` and `.tools/quake-control-check/`.
