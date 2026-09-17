# AD07 firmware analysis

Findings from reverse-engineering the extracted, verified firmware image
(`mpk-mini-full-pass2.bin`, see `FULL_DUMP_PICO.md`). This is original
analysis in our own words — not raw decompiler output, which is intentionally
excluded from this repo (see `README.md`).

Method: Ghidra 12.1 headless, `ARM:LE:32:Cortex`, base address `0x08000000`,
default auto-analysis. Peripheral usage mapped by finding every code
reference to the address range of each STM32F1 peripheral, then reading the
containing functions' decompiled output and cross-checking against the raw
binary bytes directly (not just trusting the decompiler's rendering — see
"Method notes" below for why that mattered here).

Ghidra project and scripts used: `ghidra-scripts/ExportDecompiled.java`
(bulk-decompile every function) and `ghidra-scripts/MapPeripheralUsers.java`
(cross-reference known STM32F1 peripheral base addresses against every
function that touches them). Both run via `analyzeHeadless`, no GUI needed:

```bash
/opt/ghidra/support/analyzeHeadless /path/to/ghidra-project MPKMini \
  -import mpk-mini-full-pass2.bin -processor ARM:LE:32:Cortex \
  -loader BinaryLoader -loader-baseAddr 0x08000000 \
  -scriptPath ./ghidra-scripts \
  -postScript ExportDecompiled.java /path/to/decompiled_full.c

/opt/ghidra/support/analyzeHeadless /path/to/ghidra-project MPKMini \
  -process mpk-mini-full-pass2.bin -noanalysis \
  -scriptPath ./ghidra-scripts \
  -postScript MapPeripheralUsers.java /path/to/peripheral_map.txt
```

## Confirmed: key/pad matrix scanner — `FUN_080048f4` (flash `0x080048f4`)

A 9-column × 8-row matrix scan with debounce. Verified against raw binary
bytes at its data table (`0x08004980`), not just the decompiler's rendering:

```
0x08004980: 00 10 01 40   →  0x40011000  = GPIOC base (column driver)
0x08004984: a8 6d 00 08   →  0x08006da8  = flash: table of 9× 16-bit column-select patterns
0x08004988: d0 02 00 20   →  0x200002d0  = SRAM: per-column debounce-state byte array
0x0800498c: 08 0c 01 40   →  0x40010c08  = GPIOB_IDR (row input register, read directly)
```

Behavior per scan cycle (9 iterations, one per column):

1. Write `GPIOC_ODR = (GPIOC_ODR & 0xFFFF | 0xFF80) & ~column_mask[i]` — drives
   the column-select bits, clearing the active column's bit(s) low against a
   baseline of bits `[15:7]` held high.
2. Busy-wait delay (~40 loop iterations — settling time for the matrix).
3. Read `row_byte = GPIOB_IDR[15:8]` (upper byte of GPIOB).
4. Compare against the previous reading for this column
   (`last_state[i]` in the SRAM table). If unchanged, increment a per-column
   stability counter (capped at 0xF0); the **second** consecutive matching
   read (counter == 1) is what actually gets latched as the debounced
   result — a 1-sample debounce, i.e. requires the same electrical state on
   two consecutive scans before it's trusted.
5. On debounce-confirm: for columns 0–6, stores the inverted row byte into a
   result buffer at `debounced_result_base[i+3]`. Columns 7 and 8 are
   special-cased into two extra slots (`+2`, `+1`) — likely because those
   two columns carry fewer physical inputs (encoders/transport buttons
   rather than a full 8-key column) and get packed differently.
6. If the reading *changed* from last time: reset the stability counter to 0
   and update `last_state[i]` — restarts the debounce window.

This runs guarded behind a single-shot flag (`*guard_byte == 0` at entry,
set to 1 immediately) — the caller (not yet identified) presumably calls
this once per main-loop iteration or timer tick, and this flag is likely
cleared elsewhere to re-arm it; not yet confirmed.

**Confidence: high.** Every address in the parameter table was verified
against raw binary bytes, not just the decompiler's symbolic rendering (see
"Method notes" — the auto-decompile alone was unreliable for peripheral
addresses in this pass).

## Confirmed: bootloader/DFU entry at boot — `FUN_08001cf0` (high confidence)

Also manipulates GPIOB/GPIOC, but a *different* pattern from the key
scanner above, and does something much more consequential at the end.

- Drives GPIOC ODR bits (`|= 0xFF80`, `&= ~0x4000`) — the `0x4000` bit
  is **column 7** (`column_mask[7]` in this document's matrix scanner
  section) — and reads `(~GPIOB_IDR & 0x3FFF) >> 8` with its own
  debounce loop (up to 99 consecutive stable reads, longer than the key
  scanner's single-sample debounce).
- Calls `FUN_08000b68()` — **a checksum routine**: sums `0x77FE` (30,718)
  bytes starting from a flash pointer, and compares the sum plus a trailing
  stored value against zero (classic additive checksum-validates-to-zero
  pattern). Very likely validating the application image before it's
  considered safe to jump away from.
- If the checksum passes (`FUN_08000b68() == 0`) **and** the debounced
  column-7 row read isn't a specific sentinel value (`uVar3 != 8`, its
  exact meaning as an "idle" pattern not resolved), calls
  `FUN_08000728()` → `FUN_08000188()` (CMSIS `__set_MSP`-equivalent)
  then jumps through a function pointer Ghidra couldn't statically
  resolve. Set-MSP-then-jump is the textbook pattern for handing off to
  a different firmware image or the ST system bootloader.

**Confirmed this runs at the very start of reset**, before any of the
application's own peripheral init: `get_xrefs_to` traces
`FUN_08001cf0` → called unconditionally from `FUN_08001cd6` → called
from a computed-call thunk at flash `0x0800013a`, right after the
vector table. This is a genuine "hold a button in the column-7 cluster
while powering on, and if the firmware checksum is valid, jump into
the DFU/system bootloader" feature — a standard user-triggerable
firmware-update-mode pattern, running before `main()` even starts.

**Implemented in `firmware/bootloader.c`**, called from
`Reset_Handler` before `main()`, matching the original's timing. The
jump itself uses the standard, documented STM32F101/F102/F103
medium-density system-memory bootloader address (`0x1FFFF000`, per
ST's AN2606) rather than anything reverse-engineered — that part needed
no guessing. What's reimplemented rather than ported exactly: the
trigger condition (any button in the column-7 cluster held, debounced
with clean pull-ups this firmware configures itself, rather than
replicating the exact `!= 8` sentinel comparison against the original's
specific pull configuration) and the checksum gate is not reimplemented
at all (there's no analogous "is my own image corrupt" check that makes
sense for firmware already running and about to voluntarily jump away).

## USB device driver — extensive, not yet mapped in detail

References to the USB peripheral (`0x40005C00`+, packet buffer at
`0x40006000`) span roughly 30+ functions across `0x080002C4`–`0x08006398` —
this is clearly a full USB device stack (likely the ST Standard Peripheral
Library's USB-FS-Device driver, given the STM32F102 is the "USB access
line" part). Not yet individually traced. Next useful step here: locate the
USB descriptor tables (device/config/interface/endpoint descriptors) in
flash — these are usually identifiable as a contiguous run of small,
structured byte sequences (descriptor length byte + descriptor type byte
pattern) rather than code, and would immediately confirm the exact MIDI
endpoint numbers/sizes without needing to trace the driver logic.

## RCC / AFIO / clock setup — not detailed, low priority

Heavy but unsurprising RCC usage (peripheral clock enables) and a small
AFIO footprint (2 functions, GPIO remap — likely remapping the matrix
scan pins or USART pins off their default locations). Standard init
boilerplate; not prioritized for detailed analysis since it's not where
the interesting application logic lives.

## Method notes

**The default auto-decompile pass does not reliably show peripheral
addresses as literal hex in its C output** — searching the raw decompiled
text for `0x4001...`-style constants returns almost nothing, even in
functions that demonstrably do touch those registers. Peripheral base
addresses are instead loaded from small literal-pool constants in flash
(the `DAT_08xxxxxx` symbols), and Ghidra's quick auto-analysis doesn't
always fold those into clean literals in the decompiler's rendering.

**What actually worked**: Ghidra's *reference* database (which instruction
reads/writes which address) is populated correctly even when the
decompiler's text rendering isn't clean — cross-referencing known
peripheral base addresses against `getReferencesTo()` found the right
functions reliably. Confirming exact semantics then meant reading the raw
bytes at the flash addresses those functions load their pointers from
(as done above for the matrix scanner), rather than trusting the
decompiled C's variable names/structure at face value.

## Confirmed: complete USB MIDI TX pipeline, and the mirror tap point

This is the headline finding of this pass — a fully traced, high-confidence
path from key-press to USB packet, directly answering what the original
project objective (`FINDINGS.md`: mirror MIDI events via USART1/PA9 to the
ESP32-C3) needs to know.

### USB MIDI descriptors (byte-verified, not inferred)

Found the complete USB descriptor tree at flash `0x08001dc6` (a duplicate
copy also exists at `0x08007071`). Device descriptor's `idVendor`/`idProduct`
(`0x09E8`/`0x007C`) are a byte-perfect match for the real device as seen by
`lsusb` earlier in this project — strong independent confirmation this is
genuinely the right descriptor block, not a coincidental byte pattern.

Standard USB-MIDI (Audio Class 1.0 MIDIStreaming) with 2 bulk endpoints:

| Endpoint | Direction | Max packet | Purpose |
| --- | --- | --- | --- |
| `0x01` | OUT (host→device) | 64 | Incoming MIDI (not traced this pass) |
| `0x81` | IN (device→host) | 64 | **Outgoing MIDI — where key/pad/knob events leave the device** |

Strings confirmed: iManufacturer = `"AKAI PROFESSIONAL,LP"`, iProduct =
`"MPK mini"`, plus an unindexed-by-standard-fields version string
`"Ver00.1..."` (likely truncated in this read — worth re-checking, not
critical).

### The pipeline, traced end to end

```
FUN_080048f4  matrix scan (9 col × 8 row, debounced)
      │  writes column debounce-state bytes to SRAM 0x200002d0+
      ▼
FUN_08004990  edge detector — diffs new vs. previous debounce state,
              looks up MIDI note number per (column, bit) from a flash
              table, builds a 4-byte USB-MIDI event (Note On 0x90 /
              Note Off 0x80 + note + velocity)
      │  calls FUN_08006d54(event_bytes, 4)
      ▼
FUN_08006d54  ring-buffer PUSH — appends 4 bytes to a 240-byte circular
              buffer (SRAM 0x200001e0..0x200002d0; control struct at
              SRAM 0x20000004: byte 0 = count, +4 = write ptr, +8 = read
              ptr, wraps at base+0xF0). Guards against overflow
              (won't write if count already 0xF0).

  ... (called periodically, presumably from the main loop — not yet
       located) ...

FUN_08004c44  TX pump — if EP1 IN isn't busy (EP1R STAT_TX != VALID) and
              the ring buffer has ≥4 bytes queued, proceeds
      │  calls FUN_08006cf8(stack_buf, 64)
      ▼
FUN_08006cf8  ring-buffer DRAIN — pulls up to 64 bytes from the same
              circular buffer (mirror image of the push function — same
              wraparound logic, opposite direction) into a stack buffer;
              FUN_08004c44 then zero-pads to 64 bytes if fewer were
              available
      │  calls FUN_08006398(stack_buf, 64)
      ▼
FUN_08006398  low-level USB TX — copies into USB packet memory (PMA) via
              FUN_0800656c, sets COUNT_TX, and toggles EP1R's STAT_TX
              bits to VALID (`*EP1R = *EP1R & 0x8FBF ^ 0x30`, the
              standard STM32 EPnR toggle-bit idiom) — arms the endpoint,
              hardware sends it on the next USB IN token.
```

**The mirror tap point is `FUN_08006d54`.** Every call to it is exactly one
complete, already-formatted 4-byte USB-MIDI event (byte 0 = Cable Number +
Code Index Number, bytes 1–3 = the actual 1-3 byte MIDI message padded to
3). Mirroring MIDI to the ESP32-C3 means: at this exact point, also transmit
the same 4 bytes (or the meaningful 1-3 of them, per Code Index Number) out
USART1/PA9 — no need to touch USB timing, packetization, or the ring buffer
logic at all. This confirms the original plan's premise was sound and gives
the precise, minimal insertion point.

Not yet located: what calls `FUN_08004990` (main loop? confirmed not a
timer interrupt — see "not yet analyzed"), and whether there's a
symmetrical RX ring buffer for EP1 OUT (incoming MIDI) using the same
push/drain pattern — likely, given how systematically this buffer design is
used, but not confirmed.

**Correction (later in this document's own timeline): `FUN_08004990` was
re-read in full — it's actually only 157 lines, not ~470, and has no
`'b'`/`'c'` command branches at all.** That claim was a mixup with
`FUN_08002eac` (the SysEx handler, which genuinely does have `'b'`/`'c'`
branches — see its section below) — an error made earlier in this
session's own analysis, caught and corrected during the later search for
the joystick handler. `FUN_08004990` is now confirmed fully traced: it
builds both Note On and Note Off events directly (including a
velocity/aftertouch-like scaled secondary value in the Note On path),
with no hidden untraced content. Left here, struck through in spirit
rather than silently deleted, as a record of the correction.

## Confirmed: the main loop, and MIDI generation is not just keys

`FUN_08006860` is the main loop — a single `do { ... } while(...)` calling,
in order, every scan/process/pipeline function found so far, gated behind a
mode flag (`pcVar2[3] == 0`, meaning of the flag not yet determined —
possibly "not mid-SysEx" or similar):

```
FUN_08006860 (main loop)
  FUN_080060ac()                    // one-time init before the loop
  do {
    FUN_08004710()                  // unconditional, every iteration
    if (mode_flag == 0) {
      FUN_080048f4()                // matrix scan
      FUN_08004990()                // key edge detector -> Note On/Off
      FUN_08006988()                // unidentified -- calls ring_push 4x, likely pads
      FUN_08005734()                // unidentified -- not a ring_push caller
      FUN_08002588()                // unidentified -- calls ring_push 2x
      ... (timer/countdown init snippet) ...
      FUN_08002400()                // unidentified
      FUN_0800478c()                // unidentified -- calls ring_push 1x
      FUN_08003ab8()                // unidentified -- calls ring_push 2x
      FUN_080044fc()                // unidentified -- calls ring_push 1x
      FUN_08004c90()                // unidentified
      if (some_state_byte == 0) {
        FUN_08006d3c()              // ring-buffer housekeeping/status (not push/drain)
      } else {
        FUN_08002eac()              // unidentified -- calls ring_push 4x (most active)
        FUN_08004c44()              // TX pump (only runs in this branch)
      }
      ... (further unanalyzed logic) ...
    }
  } while (...)
```

**Important refinement to the tap-point finding above**: `FUN_08006d54`
(ring-buffer push) is called from **9 different functions**, not just the
key edge-detector — confirmed via Ghidra's call-reference analysis, not
just the one path traced initially. This means it's the universal
choke-point for *all* MIDI output this device generates (keys, and
whatever `FUN_08006988`, `FUN_08002588`, `FUN_0800478c`, `FUN_08003ab8`,
`FUN_080044fc`, and `FUN_08002eac` turn out to be — most likely pads,
knobs/CC, pitch bend, mod wheel, sustain pedal, and/or an arpeggiator,
not yet individually confirmed). This *strengthens* the tap-point
conclusion rather than changing it: hooking `FUN_08006d54` still catches
everything, from every source, with no need to separately hook each
generator.

Also notable: the TX pump (`FUN_08004c44`) only runs in one branch of a
runtime state check — the other branch calls only `FUN_08006d3c`
(presumably ring-buffer housekeeping, not push/drain) instead. Worth
understanding this gate before assuming the pump always runs every loop
iteration.

## Confirmed: knob sampling is ADC1 + DMA1, resolved via indirect pointers

Earlier in this document, the peripheral cross-reference method (checking
which functions directly reference known STM32F1 peripheral base
addresses as literal instruction operands) found **zero** references to
ADC1, ADC2, SPI, or I2C anywhere in the binary, leading to an open
question about how knobs are actually sampled.

**Resolved**: they are read via the STM32's internal ADC1, transferred via
DMA1 — the peripheral cross-reference method simply couldn't see it,
because the code doesn't embed the peripheral address as a literal
operand at the call site. Instead, a small set of flash constants store
the peripheral base *addresses themselves as data* (`DAT_08002480` =
`0x40012400` = ADC1 base; `DAT_0800383c` and `DAT_08003708` = `0x40020000`
= **DMA1** base — a peripheral never checked in the earlier searches,
since it wasn't among the "obvious" candidates for a device with no
apparent DMA-driven activity), and helper functions dereference those
stored pointers at runtime. A reference search for literal operands
matching the peripheral address will never find this pattern; only
resolving the pointer *values* and re-checking against those does.

Traced chain, confirmed against raw bytes:

- `FUN_08002566(0x40012400, 1)` sets ADC1_CR2 bits `0x500000` = **EXTTRIG
  (bit 20) | SWSTART (bit 22)** — software-triggers an ADC conversion
  sequence.
- `FUN_08003820(2)` / `FUN_080036f8(2)` poll and clear DMA1's global
  interrupt/transfer-complete flags for a specific channel (channel
  index encoded via the `param_1 << 3` sign trick, matching DMA1_ISR's
  4-bits-per-channel layout) — standard "wait for DMA transfer done,
  clear the flag" pattern.
- `FUN_08002400` (called every main-loop iteration) is the actual
  consumer: once DMA signals completion, it reads 16 raw 12-bit values
  (`& 0xFFF`, matching the STM32's ADC resolution exactly) from an SRAM
  buffer DMA wrote to (`0x200001c0`), accumulates them across 4 calls,
  then right-shifts by 4 (a 4x-oversample-and-average, a standard
  noise-reduction technique) into a second SRAM array — which is exactly
  the array `FUN_0800478c` (below) reads as each knob's "current value".

The interrupt vector table was also checked directly (not just the
call-graph-reachable functions) as part of this investigation — the
ADC1_2 interrupt vector points to a single `bx lr` (immediate return, does
nothing) stub, confirming ADC completion is **polled** (via the DMA
flag-check functions above, called from the main loop), not
interrupt-driven, consistent with this firmware's apparent all-polling
main-loop architecture (no TIM1-4 or EXTI activity was found either,
earlier in this document).

**Practical implication for the replacement firmware**: implementing real
knob support needs an ADC1 + DMA1 init (continuous or software-triggered
scan of the relevant channels into a 16-entry SRAM buffer) in addition to
the CC-generation logic below — not yet done in `firmware/`, tracked as a
task.

## Candidate: knob/CC handler — `FUN_0800478c` (medium confidence)

Gated behind a flag (`*DAT_080048d8 == 1`, cleared on entry — set by
`FUN_08002400` above once a new oversampled reading is ready, not an ADC
interrupt as originally guessed). Iterates a table of entries indexed by
`uVar11`, each entry checked for "enabled" (a non-zero byte at `+0x4D`),
then reads what look like a CC number and channel (`+0x4E`, `+0x4F`),
compares a current value against a previous-value array, and on change
presumably calls `ring_push` with a Control Change message (not fully
traced into the message-construction tail).

The indexing pattern (`*DAT_080048dc * 0x65 + base`, 0x65 = 101-byte
stride) recurs across several of these unidentified functions and looks
like a **per-program configuration record** — consistent with the MPK
Mini's stored program/bank feature (multiple selectable knob/pad/CC
mappings). Worth confirming: how many programs are stored, and the exact
101-byte record layout, since that would directly inform how a
replacement firmware represents its own mapping config.

## Confirmed: SysEx editor-protocol handler — `FUN_08002eac` (high confidence)

Not a physical-control MIDI generator, despite calling `ring_push` 4 times
(revising the earlier assumption in this document) — this is the receiver
for AKAI's editor-software protocol, used by the official "MPK mini
Editor" desktop app to read/write the device's stored program
configurations.

- Gated on a flag at `DAT_080032b0[5]` (set elsewhere — presumably when a
  SysEx message finishes arriving on the OUT endpoint), and a minimum
  length check (`puVar3[6] > 5`).
- `*DAT_080032b4 == -0x10` — `0xF0` as a signed byte — checks for the
  **SysEx start byte**.
- `DAT_080032b4[1] == 'G'` — a fixed signature byte following SysEx-start
  (likely part of AKAI's manufacturer ID sequence; the actual AKAI SysEx
  ID bytes weren't independently confirmed this pass).
- `DAT_080032b4[3] == '|'` (`0x7C`) — a fixed delimiter/sub-command marker.
- Command byte at offset 4 selects behavior: `` '`' `` (`0x60`) copies raw
  payload bytes into a local buffer (likely "dump current program" request
  handling); `'a'` (`0x61`) triggers a large structured byte-shuffle copy
  from the SysEx payload into a per-program record at
  `program_base + program_number*0x65 - 0x1F9` — **the same 101-byte
  per-program stride** seen in the knob/CC handler (`FUN_0800478c`) above,
  now confirmed as the format a full program config is transferred in over
  SysEx, not just an internal detail.
- A further flag (`puVar3[6] == 'n'`) selects between (at least) two
  different byte-layout variants for the copy — possibly different
  protocol/firmware versions, or different record sub-types (e.g. keys
  vs. pads vs. knobs sent as separate chunks).

**This is a second, independent confirmation of the 101-byte per-program
record concept**, and identifies the actual wire protocol (SysEx,
`F0 <?> 'G' <?> '|' <cmd> ...`) a replacement firmware would need to
either implement (for compatibility with the official editor) or
deliberately not implement (if replacing the editor entirely with, say, a
web/BLE config interface via the ESP32-C3).

### Follow-up pass: full command set and wire encoding

Reading the complete function (not just its opening) resolves the
command set further:

| Command byte | Meaning (confidence) |
| --- | --- |
| `` '`' `` (`0x60`) | Raw payload copy to a local buffer — likely a "dump current program" request. Medium confidence. |
| `'a'` (`0x61`) | **Write program**: copies from the SysEx payload into the internal 101-byte record. High confidence. |
| `'b'` (`0x62`) | **Select program**: just sets a current-program-index byte, no bulk copy. High confidence. |
| `'c'` (`0x63`) | **Read/dump program**: the mirror-image of `'a'` — copies from the internal 101-byte record back out into a SysEx reply buffer. High confidence. |

**Correction, with live Ghidra access restored: the wire encoding is NOT
7-bit-nibble-packed.** The guess above (an earlier pass of this document,
made from only a partial read of the byte-shuffle table) is disproven by
reading `FUN_08002eac` in full. Counting every destination byte in the
`'a'` handler's copy — the 14-byte direct-order header prefix, the
31-byte odd-record-offset run, the 32-byte even-record-offset run, and
the 24-byte direct-order knob-config tail — totals exactly **101 bytes**,
matching the record size exactly, with no expansion. **It's a pure
byte-for-byte reorder, not a bit-packing scheme.** Every wire byte is a
real 8-bit data byte in the clear — which also means, incidentally, this
protocol does *not* actually respect the usual SysEx 7-bit-data-byte
convention (nothing in the handler masks or reassembles high bits), so
it presumably relies on the specific byte values a program record can
hold never exceeding 0x7F in practice, or on this being carried inside
the message body rather than as strict MIDI SysEx data bytes.

**Full reorder table, read directly off the decompiled copy loop**
(`wire[n]` = byte `n` of the payload immediately following the 8-byte
message header below; `record[n]` = byte `n` of the internal 101-byte
record):

- `wire[0..13]` → `record[1,0,2,3,4,5,6,7,8,9,10,11,12,13]` (14 bytes;
  note the first two are swapped — `record[0]` comes from `wire[1]`,
  `record[1]` from `wire[0]`).
- `wire[14..44]` (31 bytes) → `record[15,17,19,...,75]` (every odd
  offset from 0xF to 0x4B).
- `wire[45..76]` (32 bytes) → `record[14,16,18,...,76]` (every even
  offset from 0xE to 0x4C).
- `wire[77..100]` (24 bytes) → `record[77..100]` directly (the knob
  config region, `record+0x4D..0x64` — unshuffled).

The `'c'` (dump) handler performs the exact inverse of this same table.
**Full message framing**, also read directly off the handler: `F0 47
<id> 7C <cmd> <len_hi> <len_lo> <program#> <101-byte payload> F7` — 110
bytes total for `'a'`/`'c'` (8-byte header + 101-byte payload + 1
trailing `F7`). This resolves what `puVar3[6] == 'n'` (`0x6E` = 110
decimal) was gating on: a completeness check that the full 110-byte
message has actually arrived before processing it — not a mysterious
sentinel, just "expected total byte count."

**One additional confirmed field**: near the end of the `'a'` handler,
`*DAT_080032c4 = 60000 / (record[-0x1ee] + record[-0x1ef]*0x80)` — a
BPM-to-milliseconds conversion (60,000 ms/min ÷ BPM), matching the
`record+0x0a`/`record+0x0b` tempo field independently confirmed via
`FUN_08005ac8` (see the "Major new finding" section below) — third,
independent cross-confirmation of that field.

**What's still open**: the semantic meaning of most individual byte
offsets within the header region (`record+0x00..0x0c`, beyond the
already-confirmed channel/arp-enable/clock-div/tempo fields) and the
pad-region bytes beyond note/PC/CC (`record+0x0d..0x4c`, beyond the
already-confirmed 3 fields per pad) — their *positions* in both the
record and the wire transfer are now fully confirmed, not their
complete meaning. With the reorder table above, `firmware/` can now
actually receive and apply real program dumps from AKAI's editor
software (see `program.c`'s SysEx receive support) even without knowing
every field's meaning — unrecognized bytes just round-trip unchanged.

## Revised: octave/program buttons + device-initiated SysEx — `FUN_080044fc` (medium-high confidence)

Full trace revises the earlier "sustain pedal?" guess — this doesn't match
that pattern at all on closer reading.

- Reads a status byte, edge-detects against the previous value (standard
  pattern throughout this firmware).
- Bit 0 = released/center (the "else" branch resets state on bit0=1).
- Bits 1, 2, 3 (tested via `(x << 30/29/28) < 0`, i.e. individual bit
  tests) each drive different behavior when bit 0 = 0:
  - Bit 1: sets a mode flag and a status byte to `0xFF`.
  - Bit 2 and bit 3: each **toggle a state byte between two specific
    values** (`3↔1` for bit 3, `2↔1` for bit 2) — a toggle-between-two-
    states pattern, not a simple increment/decrement. Strongly suggestive
    of **octave up/down buttons**, a real, distinctive physical control on
    this keyboard, rather than a pedal (pedals are typically simple
    binary on/off, not this three-way bit-tested structure).
- When a *different* trigger condition holds (`*DAT_080045c8 != 0`), the
  function instead builds and sends a **complete, device-initiated SysEx
  message** via `ring_push`: `F0 47 00 04 7C 6A 00 04 04 5B 00 07 <byte>
  F7`. **This independently confirms `0x47` immediately after `0xF0` is
  AKAI's manufacturer ID** — matching, byte-for-byte, the `'G'` (ASCII
  0x47) signature byte found in the editor-protocol handler
  (`FUN_08002eac`) above. Two unrelated functions agreeing on this byte is
  strong cross-confirmation it's genuinely the manufacturer ID, not a
  coincidental ASCII match. The message is very likely a status
  notification back to a connected editor (e.g. reporting an octave or
  program change), not user-facing MIDI.

**Update — the status byte's source, checked directly against the raw
binary**: disassembling `FUN_080044fc` (`arm-none-eabi-objdump` against
the verified firmware dump, since the live Ghidra analysis session
wasn't available this pass) resolves `DAT_080045c4` to SRAM address
`0x20000011` — a literal pool load, confirmed by reading the raw bytes
at its flash location. This is **not** part of the matrix scanner's own
result buffers (those live at `0x200002d0`+, per this document's matrix
scanner section) — so whatever sets this byte, it is confirmed *not* to
be a direct read of `matrix_state[]`. Searching the rest of the binary
for other references to this same address found exactly one other
reader, in a large function near the main loop (~`0x8006994`) that
constructs the *identical* `F0 47 00 04 7C 6A 00 04 04 5B 00 07 <byte>
F7` SysEx template — and only one writer anywhere in the binary, an
init routine that zeroes it at startup alongside a cluster of other
single-byte flags packed into the first ~50 bytes of SRAM. No store to
this address was found outside that one-time init, meaning whatever
sets it to a nonzero value during normal operation does so through a
different code path than a simple literal-pool-addressed write — most
likely computed through a pointer/offset this pass didn't resolve.

This left real uncertainty about whether `FUN_080044fc` fires from a
**physical button** at all, versus a command byte received over
USB/SysEx — resolved below once the live Ghidra session came back.

**Resolution**: with live Ghidra access restored, `get_xrefs_to` on
`0x20000011` immediately found the write this pass's manual binary scan
had missed — from `08004962`, inside `FUN_080048f4`, **the matrix
scanner itself**. The earlier scan's assumption (a literal-pool-loaded
direct store) was too narrow; the real write is a `strb` to a
register-plus-offset address computed from the scanner's own base
pointer, a pattern that doesn't show up when grepping the binary for
the target address as a 32-bit literal.

Reading the scanner's disassembly at that address confirms exactly
which column: columns 0-6 store their debounced, bit-inverted row byte
at `base+3+column` (matching this document's matrix scanner section
above); columns 7 and 8 are special-cased into `base+2` and `base+1`
respectively, gated behind a much longer 8-consecutive-reads debounce
(vs. 2 reads for columns 0-6) rather than the shorter one. `base+1` is
`0x20000011` — so `FUN_080044fc`'s status byte is **matrix column 8**
(0-indexed — the 9th and last column), a genuine physical input after
all, just reached through the scanner's column-7/8 special-casing
rather than a plain `matrix_state[]` read. `firmware/`'s `buttons.c`
had guessed `matrix_state[7]` as a placeholder; the confirmed source is
`matrix_state[8]`, now updated there.

## Revised: pad velocity sensing — `FUN_08003ab8` (medium confidence)

Full trace also revises the earlier "joystick?" guess. The loop structure
reads a per-pad analog-like value (via the same kind of table-indexed
lookup pattern as the knob handler, `DAT_08003e14[...]`, strongly
suggesting it also reads from the ADC/DMA-refreshed buffer, likely a
*different* set of channels than the 8 used for knobs), applies threshold
gating (`< 0x41`, `> 0x80`) and a linear scale-to-127 formula
(`((value - 0x80) * 0x7F) / 0x220`, clamped) that looks exactly like
**velocity scaling for a pressure/velocity-sensitive drum pad**, with a
small state machine per pad tracking hit/release/decay phases (fields at
offsets `+1` through `+5` of a per-pad record).

**This revises the earlier ADC-channel-count note**: the original
firmware's 16-channel ADC scan (`FIRMWARE_ANALYSIS.md`'s ADC/DMA section)
is now more plausibly 8 knobs + 8 pad-velocity-sense channels, not 8
knobs + unused headroom as originally guessed.

**Net effect on the joystick question**: neither of these two functions
is a pitch/mod joystick handler — and, as established below, there is no
such control on this hardware to find. The "8 knobs + 8 pad-velocity"
channel split above accounts for the ADC scan in full.

## Candidate: tap-tempo / arpeggiator clock — `FUN_08006988` (low confidence)

Takes 4 direct parameters (not a matrix bitmask like the key/pad
scanners), and computes a rolling average of intervals between edge
events on what appears to be a single digital input (tap counter,
interval ring-buffer, average-of-N clamped to a minimum of 250 — classic
tap-tempo-to-BPM math). Calls `ring_push` 4 times across different
branches. Not confirmed which physical control this is (no dedicated "TAP
TEMPO" button is obviously present on this device's layout, so this may
be a held-button-tap gesture on an existing control, or arpeggiator
timing derived some other way). Lowest confidence of the functions
discussed in this document — flagged for follow-up rather than relied on.

## Two more `ring_push` callers identified, completing the call graph

Completing the trace of every direct caller of `FUN_08006d54` (there are
8 distinct ones total):

- **`FUN_08002588` — likely the arpeggiator step sequencer.** Indexes the
  per-program record (the confirmed 101-byte stride) and tracks a
  progressing step count against a stored threshold, gated on a record
  field (`record+7`). Shape matches "advance through a held-note sequence
  each time it's this step's turn," consistent with an arpeggiator's core
  loop rather than a physical-control handler. Medium confidence.
- **`FUN_08005188` — likely stuck-note/pad-off cleanup.** Iterates 8
  fixed slots, and for any slot flagged, resets its state block and —
  only if the stored pending event's Code Index Number is `8` (Note Off)
  and its velocity byte is a valid `< 0x80` — force-sends that stored
  Note Off via `ring_push`. Reads as a timeout/safety mechanism ensuring
  notes don't stay stuck on. Medium confidence.

**Correction: there is no pitch/mod joystick on this hardware at all.**
The search for one across this document (including the disproven
`FUN_08004990` lead above) was chasing a false premise, carried over from
generic MPK Mini product knowledge — later hardware revisions have a
4-way joystick, but this Gen 1/AD07 unit does not. Confirmed directly
against a photo of the actual device. The real control set is: 25 keys,
8 pads, 8 knobs, and a small cluster of buttons (octave up/down, program
up/down, and others) — no analog pitch/mod control at all. With that
premise removed, all 8 `ring_push` callers are now fully and correctly
accounted for:

| Function | Identified as |
| --- | --- |
| `FUN_080048f4` | Key/pad matrix scanner |
| `FUN_08004990` | Key edge detector → Note On/Off |
| `FUN_0800478c` | Knob → CC |
| `FUN_08003ab8` | Pad velocity sensing |
| `FUN_080044fc` | Octave buttons + device-initiated SysEx status |
| `FUN_08002eac` | SysEx editor-protocol handler |
| `FUN_08002588` | Arpeggiator step sequencer (likely) |
| `FUN_08005188` | Stuck-note/pad-off cleanup (likely) |
| `FUN_08006988` | Tap-tempo/arpeggiator clock (still lowest confidence — see above; possibly a held-gesture on the program or octave buttons rather than a dedicated control) |

**Practical implication for the replacement firmware**: no joystick/pitch-
bend/mod-wheel support needs implementing at all — `firmware/`'s existing
scope (keys, pads not yet implemented, knobs, and the octave/program
buttons not yet implemented) already covers the complete real control
surface of this device.

## Not yet analyzed

The remaining ~165 of 204 functions. Several items originally listed here
have since been resolved (superseded, struck below) as later sections of
this document worked through them:

- ~~ADC handling for the knobs (only 1 reference each to ADC1/ADC2 found)~~
  — resolved: ADC1 + DMA1 in continuous scan mode, addresses stored as
  data and dereferenced at runtime rather than embedded as literal
  instruction operands, which is why the initial reference search came up
  empty. See this document's ADC/DMA section.
- ~~The rest of `FUN_08004990` — CC/pitch-bend/aftertouch~~ — resolved:
  no such remainder exists (see the correction above; the function is
  fully traced at 157 lines), and there is no pitch-bend/joystick control
  on this hardware to find in the first place (confirmed against a photo
  of the real device).
- The main loop / scheduler — what actually calls the matrix scanner
  (`FUN_080048f4`), the edge detector (`FUN_08004990`), and the USB TX pump
  (`FUN_08004c44`), and in what order/timing. No timer peripheral (TIM1–4)
  references were found anywhere in the peripheral map, which is notable —
  suggests polling from a plain main loop rather than timer-interrupt-driven
  scanning, but the loop itself hasn't been located yet.
- Whether there's a symmetrical RX ring buffer for incoming MIDI (EP1 OUT).
- The 30+ generic USB driver functions (`FUN_08001464` through
  `FUN_080052c8`/`FUN_08006398` neighborhood) — low priority now that the
  actual application-level MIDI pipeline is understood; these are
  standard-library-shaped (SetEPTxStatus-style primitives) and less likely
  to matter for building replacement firmware than reimplementing the
  logic already documented above.

## Major new finding: 101-byte per-program record layout, mostly decoded

Tracing three previously-unidentified main-loop functions
(`FUN_08005734`, `FUN_08002400`, `FUN_08005ac8`) against the raw
decompiler output resolved most of the open placeholders in `firmware/`
in one pass. All three consistently use the confirmed `* 0x65` (101)
per-program stride, and their field offsets agree with each other.

### `FUN_08005ac8` — per-program record range validation/clamp (high confidence)

Takes a program index (`param_1 < 5` — only 5 programs get validated;
worth reconciling against how many total program slots this device has),
and clamps each field of that program's 101-byte record to a valid
range, resetting out-of-range values to a documented default. This is
effectively a self-documenting field map:

| Offset | Range (clamp) | Default | Likely meaning |
| --- | --- | --- | --- |
| `+0x00`, `+0x01` | 0-15 | — | Unidentified |
| `+0x02` | 0-8 | 4 | Unidentified (range matches 8 pads) |
| `+0x03` | 0-24 | 12 | Unidentified (range matches the arp clock-division ticks found in `FUN_08005734`, below — possibly related, not confirmed identical) |
| `+0x04` | boolean | — | **Arp on/off** — cross-confirmed against `FUN_08005734` (see below) |
| `+0x05` | 0-5 | 4 | Unidentified (6 values — plausibly an arp mode: up/down/up-down/random/order/chord) |
| `+0x06` | 0-7 | 7 | **Arp clock-division selector** — cross-confirmed against `FUN_08005734`'s `switch` (see below) |
| `+0x07`, `+0x08` | boolean | — | Unidentified |
| `+0x09` | 2-4 | — | Unidentified (plausibly arp octave range) |
| `+0x0a`,`+0x0b` | combined 15-bit value, 30-240 | 30 (low)/112 (high) | **Tempo** — the 30-240 range is a textbook BPM clamp |
| `+0x0c` | 0-3 | — | Unidentified |
| `+0x0d`..`+0x4c` | 8 × 8-byte sub-records | — | **Per-pad config** (8 pads = `PADS_NUM`) — see below |
| `+0x4d`..`+0x64` | 8 × 3-byte sub-records, fields 0-127 | — | **Per-knob CC config** (8 knobs = `ADC_NUM_CHANNELS`) — ends exactly at byte 100, confirming the 101-byte record boundary independently |

**Cross-checked against the two functions that actually *consume* these
ranges** (`FUN_0800478c`, this document's already-documented knob/CC
handler, and `FUN_08003ab8`, the pad velocity handler) — this is where
an earlier pass of this table had the two ranges backwards; corrected
here after reading both functions directly rather than relying on the
validator function's offsets alone:

- **`+0x4d`..`+0x64` (3 bytes × 8 knobs) — knob CC config, confirmed by
  `FUN_0800478c`.** Per knob: `+0` is simultaneously the "this knob is
  assigned" gate (skipped if 0) *and* the CC number sent (read, clamped
  to 0x7f, reused directly as the outgoing CC# — no separate CC-number
  field). `+1`/`+2` hold a smoothing/ramp state pair the function reads
  as a starting value and a target, gradually sliding the sent CC value
  toward the latest ADC reading rather than jumping instantly — these
  looked at first glance like they might be persistent per-program
  fields but are more likely working state, not confirmed either way.
  The MIDI channel for all 8 knobs comes from a single shared byte at
  `record+0x00` (clamped 0-15 per `FUN_08005ac8` above), not from each
  knob's own sub-record.
- **`+0x0d`..`+0x4c` (8 bytes × 8 pads) — pad config, confirmed by
  `FUN_08003ab8`.** Each pad can apparently be configured to send one
  of three message types, selected by a single shared mode byte (not
  per-pad): Note (data value from sub-record `+0x0`), Program Change
  (`+0x2`), or Control Change (`+0x4`) — i.e. bytes 0/2/4 of each
  8-byte sub-record are a note number, a program number, and a CC
  number respectively, and whichever one is active is chosen by that
  shared mode byte. Byte `+0x6` gates a toggle/latch behavior (seen
  used only when the active mode is Note or CC). Bytes `+1`, `+3`,
  `+5`, `+7` not yet identified.

**This directly narrows the remaining knob-CC and pad-note placeholders
in `firmware/`**: we now know *exactly* which bytes of the record to
target for empirical decoding (dump a program via SysEx, change one
knob's CC assignment or one pad's note in AKAI's real editor, dump
again, diff within the specific sub-record above) rather than searching
the full 101 bytes.

### `FUN_08005734` — arpeggiator engine parameter cache (medium-high confidence)

A change-triggered "recompute if the active program's arp-related fields
changed" function, called unconditionally every main-loop iteration
(cheap to call — it does nothing unless something actually changed).
Confirms:

- **`record+4` is the arp on/off flag** (`cVar1` in the decompiled
  output) — when it changes, the function resets two 25-entry SRAM
  arrays to `0xFF` (25 = this keyboard's key count — very likely a
  per-key "is this key part of the current arp hold set" tracking
  table).
- **`record+6` (0-7) selects a clock division**, via a `switch` that
  assigns two engine variables per case:

  | `record+6` | Tick value | Step value |
  | --- | --- | --- |
  | 0 | 0x18 (24) | 0xc (12) |
  | 1 | 0x10 (16) | 8 |
  | 2 | 0xc (12) | 6 |
  | 3 | 8 | 4 |
  | 4 | 6 | 3 |
  | 5 | 4 | 2 |
  | 6 | 3 | 1 |
  | 7 | 2 | 1 |

  24 ticks is the standard MIDI clock count for a quarter note, and this
  table halves roughly geometrically from there — strongly consistent
  with a standard "1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T"-style
  arpeggiator rate selector (exact note-length labels not confirmed, but
  the tick ratios are directly read from the binary, not guessed).
- When arp mode is *off* (`record+4 == 0`), the same `record+6`
  `switch` instead computes a *scaled tempo value* from a 16-bit input
  (`uVar3`) using near-identical divisors (`uVar3/1`, `uVar3*2/3`,
  `uVar3/2`, `uVar3/3`, `uVar3/4`, `uVar3/6`, `uVar3/8`([`uVar3>>3`]),
  `uVar3/12`) — the same musical-division family applied to a raw tempo
  period instead of a fixed tick table. Likely feeds the device's MIDI
  clock/tap-tempo output rather than the arp engine directly.

**Practical implication for `firmware/`'s `arp.c`**: the clock-division
tick/step table above is real, confirmed data — `STEP_INTERVAL_TICKS`'s
placeholder could be replaced with a proper 8-entry division table once
a real tempo source is identified (the `+0x0a`/`+0x0b` tempo field
found in `FUN_08005ac8` above is the natural candidate to combine with
this table). Not wired up yet — `arp.c` remains a fixed-tempo skeleton,
since the exact tick-to-real-time conversion (what timebase increments
these "ticks"?) isn't confirmed.

### `FUN_08002400` — ADC oversampling/averaging engine (medium-high confidence)

Runs every main-loop iteration but only actually does anything when a
gate function (`FUN_08003820(2)`, not traced) signals a new batch is
ready. When it fires: accumulates 16 channels' worth of values (`& 0xFFF`
— a 12-bit mask, matching this project's own 12-bit ADC readings) into
a running-sum array, and every 4th call, snapshots the accumulated sums
(right-shifted) into a separate "smoothed output" array, clears the
accumulator, and sets two "data ready" flags before calling
`FUN_08002566(..., 1)` (not traced — plausibly signals the knob/CC
handler that fresh smoothed data is available).

**This independently cross-confirms two things already reverse-engineered
this project**: the ADC really does sample **16 channels**, matching this
firmware's revised 8-knobs + 8-pads channel count (`adc.c`'s
`ADC_TOTAL_CHANNELS`); and the original firmware really does **4x
oversample** before using a reading, matching `knobs.c`'s
`OVERSAMPLE_COUNT`. Two independently-designed implementations (the
original's accumulate-and-shift, this project's sum-and-average)
converging on the same channel count and oversample factor is a good
sign this project's earlier, less-certain ADC finding was correct.

### `FUN_08004c90` — likely a pad output-mode status tracker (medium confidence)

Complex, only partially traced. **Correction**: an earlier pass of this
document guessed this might feed `FUN_080044fc`'s SysEx status byte —
disproven now that live Ghidra access confirmed `FUN_080044fc`'s status
byte comes straight from the matrix scanner (see the octave-button
resolution above), unrelated to this function. Re-characterized:

- **First branch** (gated on a flag, `*DAT_08004da4 != 0`): decodes a
  command-like byte (`*DAT_08004db0`) into one-hot bitmasks written to
  two separate output bytes — values 1-8 set bits in one byte, 9-0x10
  in the other, and 0x7f resets both to `0xFF`. Shaped like decoding a
  received "select slot N" command (a host/editor command, or an
  internal dispatch value) rather than anything GPIO-driven.
- **Second branch** (the flag clear): iterates 8 slots, and for each,
  reads one of three record-relative bytes (offsets `+4`/`+6`/`+8` from
  a per-slot, stride-10 table) selected by a shared mode value
  (`*DAT_08003f6c`, 1/2/3) — the same 1/2/3 mode-selection shape already
  found in `FUN_08003ab8`'s pad Note/Program-Change/Control-Change
  selection. Diffs each against a stored previous value and, on change,
  sets or clears the corresponding bit of an 8-bit output byte.

**Best current guess**: a status-byte builder tracking which of the 8
pads currently has an active/non-default value in whichever output mode
(Note/PC/CC) is selected — plausibly feeding a host-facing "pad state"
report, separate from `FUN_080044fc`'s octave-button message. Not
confirmed which consumer reads the resulting byte; flagged for
follow-up rather than relied on.

## Resolved: the key-index lookup table and its indexing formula (high confidence)

Previously the single biggest open placeholder in `firmware/`: the
(column, row-bit) → physical key mapping. `FUN_08004990` (the key edge
detector, already documented above) was known to be fully traced with
"no hidden untraced content," but the exact indexing formula into its
0x08006fcd lookup table hadn't been pulled out of the decompiler's
pseudocode — the arithmetic survived decompilation as opaque bit-shift
expressions. Disassembling it directly (`arm-none-eabi-objdump` against
the verified firmware binary, since the live Ghidra session was
unavailable for part of this pass) resolved it cleanly:

```
table_index = column * 4 + (bit_position >> 1)
key_index   = table[table_index]
```

Read directly from the raw binary, the table at `0x08006fcd` (28 bytes,
7 columns × 4 slots) is:

```
00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f
10 11 12 13 14 15 16 17 18 ff ff ff
```

i.e. a clean `0..24` sequence followed by three `0xFF` ("no key")
sentinels — confirming both the formula and this project's earlier
guess that the table was a simple sequential key index. **This directly
resolves `firmware/keys.c`'s `key_index_table[][]` placeholder** with
real, hardware-fact data rather than a guess (now implemented there).

**A genuinely new sub-finding surfaced by finishing this trace**: the
formula's `bit_position >> 1` means two adjacent row-bits (`2N`,
`2N+1`) within a column map to the *same* key index. The surrounding
code (already partially read during the original pass, not re-derived
here) branches on whether the triggering bit is even or odd, uses a
per-key armed/fired state (so only the first of the pair actually
fires), and computes a timestamp delta between the two bits' edges that
directly becomes the outgoing Note On velocity byte. This is strong
evidence the physical keybed uses **two switches per key for genuine
velocity sensing** (a make-before-break dual-contact design), not a
single on/off contact — a more sophisticated keybed than this project
had assumed. Not reimplemented in `firmware/` yet (no tick/timestamp
source exists there), tracked as a follow-up rather than the
resolved-and-done key-index mapping itself.

### Follow-up: the dual-switch mechanism in full (mostly confirmed, one open question)

With live Ghidra access back, `FUN_08004990` was read in full end-to-end
rather than partially. Per key index (0-24), a 3-state byte tracks
idle/armed/fired, and the two row-bits sharing that key index
(confirmed `2N`/`2N+1` pair from the indexing formula above) each drive
one transition:

- **The odd-numbered bit**: transitioning to its "0" value while idle
  → arms the key (state 1) and records the current value of a 16-bit
  reference (`*(short*)(DAT_08004c04+4)`, called "the reference" below
  since its exact nature is the one open question here) into a
  per-key timestamp slot. Transitioning to its "1" value while in the
  fired state (2) → sends the Note Off and resets to idle; any other
  odd-bit "1" transition just resets to idle without sending anything.
- **The even-numbered bit**: transitioning to its "0" value while armed
  (state 1) → computes `delta = reference_now - stored_timestamp`,
  clamps it to 0-126, sets state to fired (2), and sends the Note On
  with **velocity = 127 − delta** (see the table below), plus a
  transpose applied via the current program's `record+2`/`record+3`
  fields (an octave/semitone offset — consistent with, though not
  identical in mechanism to, `firmware/buttons.c`'s octave feature).
  There's also a second, SysEx-editor-mode-gated branch here (guarded
  by the same `record+4`/two other flag bytes this document hasn't
  named yet) that records the note into an editor-facing "recently hit
  keys" list instead of sending real MIDI — not reimplemented, lower
  priority than the main path.

**The velocity "curve" is a plain linear inversion, not a nonlinear
curve** — reading the raw bytes at flash `0x08006f4e` (immediately
before the key-index table at `0x08006fcd`, same base pointer with a
`-0x7f` offset) gives 127 bytes: `7F 7E 7D ... 02 01`, i.e.
`table[n] = 127 - n`. So the real formula is simply
**`velocity = 127 - clamp(delta, 0, 126)`** — faster (smaller-delta)
double-switch actuation produces higher velocity, exactly as expected
for a real make-before-break velocity-sensing mechanism, and velocity
never reaches 0 (minimum 1). This value range (delta clamped to 0-126)
is physically consistent with the reference being real elapsed time in
milliseconds — a plausible window for two mechanical contacts closing
in sequence during a keypress.

**Resolved (with live Ghidra access, in a follow-up pass): what
increments `*(short*)(DAT_08004c04+4)` — and a correction to this
document's earlier claim about SysTick.**

Manually walking the disassembly at the two writer addresses
(`0x08006108`, `0x0800610e`, which fall in a gap in Ghidra's
auto-analysis — no function boundary defined there, `arm-none-eabi-
objdump` used instead) shows a plain 16-bit wraparound increment:
`if (counter == 0xFFFF) counter = 0; counter++;`, executed once per
call to the function containing it. That function is reached only by
an internal jump, not a `bl` call, so it's part of a larger function
whose own entry wasn't pinned down — but critically, **it is not
called from the SysTick interrupt**: the SysTick vector table entry
(flash offset `0x3C`) resolves to `0x080018F4`, and disassembling that
address directly shows a single instruction — `bx lr`. **`SysTick_Handler`
is a no-op stub**, identical in shape to every other unused exception
vector in this firmware.

This corrects an earlier claim in this document's ADC section ("no
evidence the original uses SysTick or any timer at all") — it's more
precise to say: **the original *configures* SysTick hardware** (traced
separately: `SysTick->LOAD = SYSCLK/16000`, `TICKINT` and `ENABLE` set)
**but its interrupt handler does nothing**, and the counter this section
is about is driven by plain call-frequency, not the SysTick hardware at
all. Two different, unrelated timer-adjacent facts were easy to
conflate; both are now stated precisely. Why the original bothers
configuring SysTick hardware it never acts on isn't resolved — possibly
vestigial, possibly polled via `SysTick->VAL`/`COUNTFLAG` somewhere this
pass didn't find.

**Practical implication for `firmware/keys.c`**: the reference this
project was waiting to confirm is a **main-loop call-rate counter, not
a calibrated wall-clock timer** — so `systick.c`'s real millisecond
timebase would in fact have been the *wrong* thing to hook this up to.
`keys.c` now implements the full dual-switch mechanism using an
equivalent free-running counter (incremented once per `keys_process()`
call, matching the original's category of timing source exactly, even
though the absolute calibration will differ from the original's actual
main-loop rate).

## Additional SysEx findings: confirmed factory defaults, the 'd' command, and independent packer confirmation

Following up on the SysEx section above, three more findings from the
same `FUN_08002eac` read:

**The `'d'` command's reply, reconstructed.** Its handler overwrites
bytes 5-8 of the *same buffer the request arrived in* (`pcVar10[5]=0,
[6]=1, [7]=*pbVar5 (current program), [8]=0xF7`), leaving bytes 0-4
(`F0 47 <id> 7C 'd'`) untouched, then packs 9 bytes starting from byte
0 via `FUN_08005ea4`. Reconstructed reply: `F0 47 <id> 7C 'd' 00 01
<current program> F7` — implemented in `firmware/sysex.c`'s
`send_status()`. Medium confidence: inferred that `FUN_08005ea4`'s
source pointer (`DAT_080036c4`) is the same address as the request
buffer rather than independently confirming it, since both resolve to
plain SRAM addresses this pass didn't cross-reference further.

**`FUN_08005ea4` independently confirms this project's own SysEx TX
packing.** Decompiling it shows the exact same algorithm `firmware/
sysex.c` was already written with: pack 3 raw bytes per USB-MIDI event
under CIN `0x4`, then a final event with CIN `4 + remaining_count`
(`0x5`/`0x6`/`0x7` for 1/2/3 leftover bytes) padded with zeros. Good
independent confirmation, not just an assumption about how USB-MIDI
SysEx framing "should" work.

**Confirmed factory-default record values**, read directly off the
`'j'` (`0x7F` sub-case) bootstrap/factory-reset handler, which writes
literal byte constants into a fresh record via negative offsets from
the receive buffer (`record_offset = pcVar10_offset + 0x1F9`, matching
the `'a'` handler's own `record_base = pcVar10 + program#*0x65 -
0x1F9`). Converting every write in that handler:

| Record offset | Confirmed factory default | Previously documented clamp/fallback (`FUN_08005ac8`) |
| --- | --- | --- |
| `0x00` (channel) | 0 | — |
| `0x01` | 0 | — |
| `0x02` | 4 | clamp 0-8, fallback 4 (matches) |
| `0x03` | 12 | clamp 0-24, fallback 12 (matches) |
| `0x04` (arp enable) | 0 (off) | boolean |
| `0x05` | **1** | clamp 0-5, fallback **4** (true default differs from the out-of-range fallback — expected, they're different concepts) |
| `0x06` (arp clock div) | **5** | clamp 0-7, fallback **7** (same distinction) |
| `0x07`, `0x08` | 0 | boolean |
| `0x09` | 3 | clamp 2-4 |
| `0x0a`/`0x0b` (tempo) | 0/120 → **120 BPM** | clamp 30-240 |
| `0x0c` | 0 | clamp 0-3 |
| Each pad's `+0x0` byte (`0x0d`, `0x15`, `0x1d`, ... `0x45`) | **pad index + 1** (1,2,3,4,5,6,7,8) | 0-127 |

The rest of each pad sub-record and the *entire* knob CC region
(`0x4d..0x64`) are **not touched** by this handler — this project has
no evidence for their true factory defaults, unlike the fields above.
`firmware/program.c`'s `init_one()` now uses these confirmed values for
every field this table covers, and keeps clearly-flagged placeholders
only for what's still unconfirmed (knob CCs, each pad's PC#/CC#
fields). Note also: the `+0x0` pad field's confirmed value (a small
1-8 index) casts doubt on this project's assumption that it's a MIDI
note number by default — 1-8 is an implausibly low note range. It may
be a plain pad-index/enable marker rather than "note number," with the
real default note living elsewhere or simply not factory-populated;
not re-resolved this pass.

**Not implemented**: the `'j'` command's `0x7F` full-reset reply uses a
**different message framing** than the rest of this protocol — unpacking
its hardcoded USB-MIDI bytes gives `F0 47 00 04 7C 6A 00 04 06 7F 02 00
64 F7`, which has an extra byte (`04`) before the `7C` delimiter
compared to every other command's `F0 47 <id> 7C <cmd> ...` layout, not
reconciled with the main header format this pass. Also not
implemented: `` ` `` (raw payload capture into a stack-local buffer
that's never read back out within the same call — likely dead/unused
in practice, or part of a multi-message flow this project hasn't
traced) and the `0x5a` sub-case of `'j'` (echoes back a single
hardware-status byte from a source not identified).

## Arpeggiator engine fully traced — direction modes and octave range confirmed

`FUN_08002588` (previously "likely the arpeggiator step sequencer,"
medium confidence, only partially read) was read in full with live
Ghidra access. It's a substantially larger function than the earlier
pass suggested — a complete arp engine, not just a step counter. Two
concrete new findings, both directly resolving fields this document
had previously flagged as "unidentified":

- **`record+0x05` is the arp direction mode (0-5)**, selected by a
  `switch(*(char*)(iVar16+5))` with 6 cases mapping cleanly onto
  standard arpeggiator modes:
  - `0`: increments a step index each call, wrapping to 0 on overflow.
  - `1`: decrements, wrapping to `count-1` — the confirmed factory
    default (`record+0x05` = 1, from the `'j'` bootstrap handler) is
    **Down**, not Up.
  - `2`, `3`: both maintain a direction-reversal flag and bounce
    between the ends of the held-note range — Up-Down and Down-Up
    (mirror images of each other in which end they start/turn at).
  - `4`: uses a 16-bit linear congruential generator,
    `x = x*0x6255 + 0x3619`, to pick a pseudo-random step each call —
    confirmed Random mode, with the exact RNG constants.
  - `5`: structurally identical to case `0` (Up) but bounds the step
    index against a *different* held-note count variable
    (`DAT_08002c98` vs. `DAT_0800299c`, used elsewhere in the function
    for the live/current note list) — plausibly an "Order played"
    mode using a separate insertion-order list, not independently
    confirmed since that second variable's own population wasn't
    traced this pass.
- **`record+0x0c` is the arp octave range (0-3 additional passes)**.
  Every direction-mode case increments a "pass" counter
  (`pcVar17[0xd]`) each time a full cycle through the held notes
  completes, wrapping it against `record+0x0c`; the note actually sent
  is offset by `pass * 12` semitones (`uVar18 = bVar8 + pcVar17[0xd]*0xc`,
  checked `< 0x80` before sending). Confirmed factory default 0 (no
  extra octaves), matching this document's earlier clamp-only finding
  (0-3, via `FUN_08005ac8`) now with a confirmed meaning attached.

**Implemented in `firmware/arp.c`**: all 6 modes (5 faithfully, mode 5
reimplemented identically to mode 0 given the residual uncertainty
above) and the octave-range repeat, replacing the earlier fixed
ascending-only skeleton. Gate length and latch (holding notes after
key release) are referenced elsewhere in this function's surrounding
logic but weren't traced closely enough this pass to implement with
confidence — still open.

## ADC channel ranges confirmed against AKAI's own schematic

The last remaining "needs real hardware" item — which physical knobs
and pads are wired to which ADC channels — turned out to be partially
answerable from a resource already in this repo: `ad07-schematic-
page7.jpg` (AKAI's own schematic, sheet "AD07_MPK8_V0.03"). Reading it
directly (cropped and upscaled for legibility):

- The "8KNOBS" section's connector is labeled with net names `ADC0`
  through `ADC7`, one per potentiometer (VR1-VR8), in that pin order.
- The "8 Dump PADs" section's two 4-pin connectors are labeled `ADC8`-
  `ADC11` and `ADC12`-`ADC15`.

This directly confirms the channel *ranges* this project's ADC/DMA
finding had already inferred from the firmware side (knobs 0-7, pads
8-15, not some other split) — two independent lines of evidence (the
original firmware's own 16-channel scan behavior, and now AKAI's own
schematic labels) agreeing is a good sign. **Not resolved**: the exact
trace routing from each individual pad (PAD1-PAD8) to its specific
channel within ADC8-15 — the schematic clearly shows non-trivial,
crossing wire routing there, but this project's copy of the schematic
isn't high enough resolution to read reliably pin-by-pin. Misreading
that risks a confidently-wrong mapping, which is worse than leaving it
an honest placeholder (numeric order, pad N = channel 8+N) — consistent
with this project's standing policy throughout.
