# ILI9341_T4 differential engine / ESP32-P4 port

Upstream: https://github.com/vindar/ILI9341_T4
Pinned commit: bee2957aefe6aa443f21da96b0856b0e1746e696
Copyright (c) 2020 Arvind Singh. LGPL-2.1-or-later; see LICENSE.

DiffBuffTemplate.h and StatsVar.h are unmodified upstream files. Arduino.h
is a local minimal build shim for elapsedMicros and unused statistics output.
board_lcd_diff.cpp instantiates the upstream engine for 320x480, a 6-pixel
merge gap, 16-line guard, and at most 20 physical rows per DMA operation.
The 8 KiB diff log has upstream's conservative full-remainder overflow fallback.
No approximate color comparison is enabled.

board_lcd_sync.c ports the driver's scanline-trailing asynchronous scheduling
to an ESP-IDF task, using spacing=2, strict initial resynchronization, physical
scan-period calibration and estimated scan position between register reads.
It uses the original readDiff() wait/continue protocol, not the retired fixed
24..40 scanline entry window. Hardware-specific Teensy FIFO/IRQ programming,
touchscreen and ILI9341 register initialization are replaced by ESP-IDF DMA,
the existing ST7796 BSP and FT6336 touch driver.

The canonical framebuffer is portrait, like upstream. Landscape images use
the upstream rotation=1 diff and are packed into logical vertical strips;
the panel's MADCTL is never changed temporarily during transmission.
Unknown front/back porch split is handled conservatively: waits assume all
extra scanlines precede active display; deadlines assume none precede it.

Original compose/latest-pending/sending ownership remains for UI isolation.
A fourth 307200-byte PSRAM image is the upstream canonical mirror, plus an
8192-byte PSRAM diff log. It is private to the sender and cannot be recycled
while DMA is active. Failed transfers invalidate it and force a full repaint.
Failed calibration/invalid reads keep differential asynchronous transmission
but increment unsynced; missed deadlines increment late. Neither is reported
as a successful tear-free frame. The upstream refresh-rate adjustment is
adapted to ST7796 FRMCTR1 (FRS=9, DIVA=0, RTNA=31, nominally near 50Hz), then
calibrated from actual reads. ILI9341 register values/formulas are not copied.

Sources and firmware remain rebuildable in this repository. The existing MIT
license of independent board/UI files is unchanged. Port/adapters derived from
the upstream engine/scheduler are marked LGPL-2.1-or-later.
