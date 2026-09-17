# Open-source AD07 replacement firmware — early skeleton

**Status: compiles and links cleanly (`-Wall -Wextra`, zero warnings),
not yet tested on real hardware.**
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
- **Key edge detection → velocity-sensed Note On/Off** (`src/keys.c`):
  the physical (column, row-bit) → key index mapping is **confirmed**
  (the real indexing formula and its 28-byte lookup table, reproduced
  exactly from the verified firmware binary). The original's dual-
  switch velocity-sensing mechanism is also now fully implemented — a
  3-state per-key machine (idle/armed/fired) driven by the row-bit pair
  sharing each key index, with velocity computed as `127 -
  clamp(delta, 0, 126)` (confirmed a plain linear inversion, not a
  curve). **This is the intended mirror tap point for the ESP32-C3 BLE
  MIDI project** — see the TODO comment in `send_note()`. One caveat:
  the delta is clocked against a free-running counter incremented once
  per `keys_process()` call, matching the original's own timing
  *category* (confirmed to be a call-rate counter, not the SysTick
  hardware — see `keys.c`'s header) but not independently calibrated to
  real time, since this firmware's main-loop rate isn't guaranteed to
  match the original's.
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
- **Millisecond timebase** (`src/systick.c`): standard Cortex-M3 SysTick
  setup, 1ms ticks off the 48 MHz SYSCLK. Not derived from the
  original's disassembly — the original *does* configure SysTick
  hardware, but its handler is a confirmed no-op stub, so whatever it's
  for isn't interrupt-driven timekeeping (see `FIRMWARE_ANALYSIS.md`'s
  dual-switch key velocity section). Gives this firmware its first real
  wall-clock reference, used by `stuck_note.c`'s timeout and `arp.c`'s
  step rate.

- **Knob → MIDI CC** (`src/adc.c`, `src/knobs.c`): ADC1 in continuous
  scan mode over 16 channels, DMA1 channel 1 keeping `adc_raw[]` fresh
  autonomously, 4x oversampling, and CC messages sent on meaningful
  change. Confirmed via a real reverse-engineering find this session
  (`FIRMWARE_ANALYSIS.md`'s ADC/DMA section) that the original firmware
  really does use the internal ADC + DMA for this — a search that
  initially came up empty because the peripheral addresses are stored as
  data and dereferenced at runtime, not embedded as literal instruction
  operands. Which physical GPIOA pin (ADC channel) each knob is wired to
  still isn't confirmed (using channels 0-7 as a reasonable default).
- **Pad velocity sensing → MIDI Note On/Off/CC/Program Change**
  (`src/pads.c`): reimplements the confirmed shape of the original's
  pad-velocity handler — attack/release threshold hysteresis on each
  pad's ADC reading (channels 8-15, the other half of the 16-channel ADC
  scan), with the original's documented velocity-scaling formula. Also
  reimplements all three confirmed pad output modes (Note/CC/Program
  Change), not just Note. Which ADC channel maps to which physical pad
  still isn't confirmed.
- **Octave up/down buttons** (`src/buttons.c`): reimplements the
  toggle-between-two-states pattern found in the original's button
  handler as an octave up/down offset (±4), applied to `keys.c`'s note
  output. Input source is now **confirmed** — matrix column 8 (see
  `buttons.c`'s header for how this was traced).
- **Stuck-note safety net** (`src/stuck_note.c`): reimplements the
  original's 8-slot timeout mechanism — force-sends a Note Off for any
  key/pad note that's been held too long without a matching release.
  Wired into both `keys.c` and `pads.c`. Placeholder: the timeout
  duration/timebase isn't confirmed (counts main-loop iterations).
- **Per-program configuration record** (`src/program.c`): the original's
  own 101-byte per-program record, decoded field-by-field from
  `FIRMWARE_ANALYSIS.md`'s "Major new finding" section and cross-checked
  against the functions that actually consume it. `knobs.c`, `pads.c`,
  and `arp.c` now read CC numbers, note/PC/CC assignments, MIDI channel,
  and arp on/off/clock-division/tempo from here instead of standalone
  placeholder tables — the *architecture* now matches the original
  (per-program-configurable). Default field *values*: the header
  fields (channel, arp settings, tempo, and three still-unidentified
  bytes) and each pad's first sub-record byte now use the original's
  own confirmed factory-reset defaults (read out of the SysEx `j`
  bootstrap handler — see `program.c`'s header). The knob CC region and
  the rest of each pad's sub-record have no confirmed factory data, so
  those stay reasonable placeholders. Real values for *any* field can
  also now be loaded via SysEx — see below.
- **SysEx editor protocol — receive and send program configs, query
  status** (`src/sysex.c`): reassembles incoming USB-MIDI SysEx packets
  and implements `a` (write program), `b` (select program), `c`
  (read/dump program), and `d` (status query — replies with the current
  program number) against `program.c`'s record store, including the
  exact byte-reorder table the `a`/`c` wire format uses (confirmed by
  reading the original's `FUN_08002eac` in full — an earlier pass of
  this project's analysis had incorrectly guessed this was a 7-bit
  nibble-packing scheme; it's actually a pure reorder of the same 101
  bytes, now corrected in `FIRMWARE_ANALYSIS.md`). The USB-MIDI SysEx
  packing algorithm itself (`pack_and_send()`) is independently
  confirmed against the original's own packing function
  (`FUN_08005ea4`), not just assumed. This means a real program dump
  sent by AKAI's official editor software (or a replacement config
  tool) can now actually be received and applied, not just a
  placeholder default. **Not yet tested against a real SysEx sender** —
  the message framing and reorder table are verified by construction
  (the encode/decode tables are checked to be exact inverse
  permutations, and the packer matches the original's own algorithm)
  but not against a real captured dump from the official editor.
- **Arpeggiator** (`src/arp.c`): all 6 confirmed direction modes are
  implemented — Up, Down (the confirmed factory default), Up-Down,
  Down-Up, Random (using the original's own confirmed RNG constants),
  and a 6th mode reimplemented the same as Up pending more confidence
  in what distinguishes it — plus 0-3 octave range repeats, all read
  from the program record (previously undecoded fields, now resolved —
  see `FIRMWARE_ANALYSIS.md`'s "Arpeggiator engine fully traced"
  section). Gated on the program's real arp on/off flag and rate-scaled
  by its clock-division and tempo fields, with genuinely calibrated
  real-time step timing via `systick.c` (a step at 120 BPM with the
  default clock division is exactly 500ms — a standard "1/4 note" arp
  rate). Still partial: gate length and latch parameters aren't
  decoded. Still not wired to key/pad input.

## What's stubbed / not yet implemented

- **Knob and pad ADC-channel mappings** (see caveats above).
- **SysEx editor protocol commands `` ` `` (raw dump capture) and `j`
  (device identification/bootstrap/factory-reset)** — not implemented
  (see `sysex.c`'s header for why). `a`, `b`, `c`, and `d` *are*
  implemented — see above.
- Incoming MIDI (EP1 OUT) is now consumed by `sysex.c` for SysEx
  program management; non-SysEx incoming MIDI (e.g. a DAW driving this
  device's pads as a control surface) still isn't consumed by anything.
- There is no pitch-bend/mod-wheel/joystick handling because there is no
  such control on this hardware — confirmed against a photo of the real
  device (see `FIRMWARE_ANALYSIS.md`'s joystick section). Nothing to
  implement here.

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
