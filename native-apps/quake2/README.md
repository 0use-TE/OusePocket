# Quake II on OusePocket (experimental)

This is a standalone, trusted ESP32-P4 ELF program. It uses the software
renderer at 320×240 and the host's display, SD card and serial input bridge.
The first port supports single-player only, without sound.

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
the base directory, disables sound, opens the menu, and starts the demo map
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

The download runs on the device and commits the final file only after
completion. The command reports progress and waits; `--no-wait` returns after
submission. The existing file service refuses to overwrite an existing file.
Game configuration/saves live in `/Ouse/Quake2/baseq2` on the SD card.

Open the desktop's 雷神之锤2 icon, or:

```powershell
python tools/quake2.py start
python tools/quake2.py play
```

Focus the computer control window. Arrow keys move/turn, Enter confirms, Esc
opens the menu, Space jumps, and the left mouse button fires. Mouse motion
looks around. The game's own Options menu can change key bindings. Focus
loss/closing the window releases the keys; if serial input stops, held keys
expire after 750 ms. Only one program may own the serial port at a time.
Returning to the device desktop cooperatively stops the game and releases
its arena, files, executable mapping and SD lease.

`python tools/quake2.py status` queries the native state and frame/allocation
counters; `stop` asks the worker to exit. `tools/quake2-control.cmd` starts the
game and opens the computer controller. This is experimental native code with full device
privileges, using the separate ABI 2 game import profile rather than the
small ABI 1 counter interface.

The code/data image, allocation arena (including the model pool), 256 KiB
game task stack and both 320×240 RGB565 presentation buffers use PSRAM.
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
