# Open-source AD07 replacement firmware — early skeleton

**Status: compiles and links cleanly, not yet tested on real hardware.**
This is a genuine, from-scratch reimplementation (not decompiled/copied
code — see `FIRMWARE_ANALYSIS.md` for the analysis this is based on and
each source file's header comments for what's confirmed-behavior vs.
still-unknown), built up incrementally as the reverse-engineering findings
land.

## What's real right now

- **Matrix scanner** (`src/matrix.c`): full reimplementation of the
  9×7-column key/pad scan, debounce included. Column-select wiring
  (PC7–PC15) reused from the original firmware's data table since it's a
  hardware-wiring fact about this specific board, not creative content.
- **MIDI ring buffer** (`src/midi_ring.c`): standard circular buffer,
  matching the original's confirmed 240-byte design.
- **Key edge detection → Note On/Off** (`src/keys.c`): diffs
  `matrix_state[]` against the previous scan and pushes MIDI events for
  every transition. **This is the intended mirror tap point for the
  ESP32-C3 BLE MIDI project** — see the TODO comment in `send_note()` in
  `keys.c`. **Important caveat**: the physical (column, row-bit) → key
  index mapping (`key_index_table` in `keys.c`) is a placeholder, all
  "no key" — the original firmware's lookup table wasn't traced precisely
  enough to be confident of the exact mapping, so this was left honest
  rather than guessed. Needs filling in from real hardware testing before
  keys actually produce correct notes.
- **USB descriptors** (`src/usb_descriptors.c`): byte-verified against the
  real device, needed for class-compliant enumeration.
- **USB device stack** (`src/usb.c`): minimal STM32F1 USB peripheral
  driver written from the public ST reference manual, not from the
  original firmware's disassembly. Handles enough of EP0 (GET_DESCRIPTOR,
  SET_ADDRESS, SET_CONFIGURATION) to enumerate, and EP1 IN for sending
  MIDI. **Not yet tested against real hardware or a real USB host** — the
  control-transfer state machine in particular is exactly the kind of code
  that looks right on paper and has an off-by-one until it's actually
  plugged in.
- Startup code, vector table, linker script: standard Cortex-M3/STM32F1
  boilerplate, the same shape as any STM32F1 project.

## What's stubbed / not yet implemented

- **The key-index lookup table** (see caveat above) — the single biggest
  remaining gap now that the edge-detection logic itself exists.
- **Clock configuration.** Currently runs on the default 8 MHz HSI reset
  clock. USB needs a precise 48 MHz (HSE + PLL) — not yet configured, so
  USB will not actually work correctly yet despite the driver code being
  present.
- **Knobs/CC**, **SysEx editor protocol**, **pedal/joystick handling**,
  **EP1 OUT (incoming MIDI) processing** — all still open per
  `FIRMWARE_ANALYSIS.md`'s "not yet analyzed" section.
- Multi-packet EP0 control transfers (the 101-byte config descriptor gets
  truncated to 16 bytes right now — `ep0_send()`'s TODO).

## Building

```bash
cd firmware
make
```

Needs `arm-none-eabi-gcc` (confirmed working with the version already used
elsewhere in this project). Produces `build/mpk-mini-open.elf` and
`build/mpk-mini-open.bin`.

## Flashing

Not yet attempted. Once clock init and key→MIDI are in place, this should
be flashable the same way `FULL_DUMP_PICO.md`'s RDP-removal section
flashed the restored original firmware:

```bash
openocd -f interface/cmsis-dap.cfg -c "adapter serial DAP_Himimoni" \
  -c "transport select swd" -c "adapter speed 50" \
  -c "set FLASH_SIZE 0x20000" -f target/stm32f1x.cfg \
  -c "init" -c "reset halt" \
  -c "program build/mpk-mini-open.bin 0x08000000 verify reset" \
  -c "shutdown"
```

**Do not flash this to the research unit without a way back** — keep
`mpk-mini-full-pass2.bin` (the verified original-firmware backup) ready to
reflash, and confirm SWD access still works before committing to testing
this on real hardware given how incomplete it still is.
