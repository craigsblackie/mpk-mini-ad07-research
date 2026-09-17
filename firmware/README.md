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
  original firmware's disassembly. Handles EP0 (GET_DESCRIPTOR with
  proper multi-packet IN support for descriptors over 16 bytes,
  SET_ADDRESS, SET_CONFIGURATION) to enumerate, EP1 IN for sending MIDI,
  and EP1 OUT for receiving it (dispatched via the weak
  `usb_midi_on_receive()` hook in `usb.h` — override it to do something
  with incoming MIDI; the default discards it). **Not yet tested against
  real hardware or a real USB host** — the control-transfer state machine
  in particular is exactly the kind of code that looks right on paper and
  has an off-by-one until it's actually plugged in.
- **Clock init** (`main.c`'s `clock_init()`): real HSE (8 MHz crystal,
  confirmed from this project's board photos) + PLL ×6 → 48 MHz
  SYSCLK/USBCLK, with correct flash wait-states. One flagged uncertainty:
  the USBPRE bit polarity was documented from memory, not re-verified
  against RM0008 this session.
- Startup code, vector table, linker script: standard Cortex-M3/STM32F1
  boilerplate, the same shape as any STM32F1 project.

## What's stubbed / not yet implemented

- **The key-index lookup table** (see caveat above) — the single biggest
  remaining gap now that the rest of the key→MIDI pipeline exists.
- **Knobs/CC**, **SysEx editor protocol**, **pedal/joystick handling** —
  all still open per `FIRMWARE_ANALYSIS.md`'s "not yet analyzed" section;
  tracked as tasks in this project's ongoing work.
- What incoming MIDI (EP1 OUT) actually *does* — the plumbing exists
  (`usb_midi_on_receive`) but nothing consumes it yet.

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
