# Akai MPK Mini (AD07 / Gen 1) — firmware research

Reverse-engineering and firmware-preservation work on the original Akai MPK
Mini's mainboard (revision AD07, STM32F102R8T6), with the eventual goal of
building open-source replacement/enhanced firmware that adds BLE MIDI via an
external ESP32-C3 while preserving the original USB MIDI functionality.

## Status

- **Complete, twice-verified firmware backup obtained** via a power-glitch
  RDP1 bypass (Raspberry Pi Pico + Flipper Zero as SWD probe). See
  `FULL_DUMP_PICO.md` for the full attack log, including 12 attempts across
  two sessions and the debugging trail that got there.
- RDP has since been removed on the research unit and the original firmware
  reflashed and verified working (USB MIDI enumeration, ALSA MIDI port,
  live MIDI events from key/pad presses all confirmed).
- Reverse engineering of the extracted firmware is in progress (Ghidra,
  ARM:LE:32:Cortex) — see `FIRMWARE_ANALYSIS.md` for documented findings
  (raw decompiler output itself is not published here — see "What's not in
  this repo").
- **An open-source replacement firmware has been started** (`firmware/`) —
  compiles cleanly as of this writing, with the matrix scanner, MIDI ring
  buffer, and a minimal USB device stack implemented. Not yet
  feature-complete or hardware-tested — see `firmware/README.md` for exact
  status.

## Repo layout

- `FINDINGS.md` — SWD/hardware bring-up findings, board pinout, the
  exception-based partial-extraction method (CVE-2020-8004) that got the
  first 89.1% before the full dump.
- `FULL_DUMP_PICO.md` — the full power-glitch extraction method (CVE-2020-13466,
  the [stm32f1-picopwner](https://github.com/CTXz/stm32f1-picopwner) technique),
  every attempt logged including failures and what they ruled out, and the
  final successful RDP-removal + reflash procedure.
- `RAIDEN_PICO_NEXT.md` — notes on [raiden-pico](https://github.com/AdamLaurie/raiden-pico),
  a more capable ADC-gated glitching platform, prepared as a next step if the
  simpler tool's technique needs deeper diagnosis on similar hardware in future.
- `dump-flipper.py` — CMSIS-DAP/Flipper Zero adaptation of the picopwner
  dump script (patched from upstream, which is hardcoded for ST-Link).
- `connect-readonly.cfg`, `extractor-openocd.cfg` — OpenOCD configs used
  during the read-only SWD bring-up and the exception-based extraction pass.
- `verify-full-dump.py` — cross-checks two full-dump passes against each
  other and against the earlier partial dump.
- `FIRMWARE_ANALYSIS.md` — documented reverse-engineering findings: the
  confirmed key/pad matrix scanner, the complete USB MIDI TX pipeline and
  its mirror tap point, USB descriptor tree, main loop, and more, all in
  original prose cross-checked against raw binary bytes.
- `firmware/` — the open-source replacement firmware itself. See
  `firmware/README.md` for build instructions and exact status.
- `ghidra-scripts/` — headless Ghidra scripts used for the analysis:
  bulk-decompiling every function, and cross-referencing peripheral/call
  usage to map which functions do what.
- `ad07-schematic-page7.jpg`, `PXL_*.jpg` — the AD07 service-manual schematic
  page used throughout, and board photos.
- `pa9-test2/` — standalone SRAM test firmware (bypasses the whole glitch
  mechanism) used to independently verify the PA9/USART1 wiring and chip
  pin identification during debugging.

## What's not in this repo

- **The extracted firmware binary itself.** It's AKAI's copyrighted compiled
  code; this repo covers the extraction *method*, not a redistribution of
  the result. `FULL_DUMP_PICO.md` documents the exact procedure to reproduce
  a personal backup from your own hardware.
- **Raw Ghidra decompiler output.** Same reasoning — unprocessed decompilation
  is a direct derivative of the original binary. Findings will be published
  here once they're transformed into original, documented analysis (function
  purposes, protocol notes, etc. in our own words) rather than raw decompiler
  text.
- **Third-party tools cloned during this work** (`stm32f1-picopwner`,
  `stm32f1-firmware-extractor`, `raiden-pico`, the Pico SDK) — each has its
  own upstream repo and license. See the clone commands referenced in
  `FULL_DUMP_PICO.md` and `RAIDEN_PICO_NEXT.md`.

## Hardware used

Raspberry Pi Pico, Flipper Zero (as CMSIS-DAP SWD probe via DAP Link),
USB-UART adapter, logic analyser, multimeter, basic through-hole resistors
(220 Ω / 560 Ω / 10 kΩ) and a BC547B NPN transistor.
