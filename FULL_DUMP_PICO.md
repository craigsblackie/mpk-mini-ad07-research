# Full AD07 dump with Raspberry Pi Pico

## SUCCESS (2026-09-17)

**Attempt 12 succeeded, after further rewiring done outside this session
between 2026-09-16 and 2026-09-17.** Two full passes,
`mpk-mini-full-pass1-attempt12.bin` and `mpk-mini-full-pass2.bin`, both
131,072 bytes, byte-for-byte identical
(`sha256: c189957e3b18bae01e97599ad573ff59bd633ce5fd11fe2eabdce1f08cfbc653`),
and all 14,594 words previously recovered by the exception-based extractor
(the 89.1% partial dump) agree perfectly with this full dump —
`verify-full-dump.py` reports both PASS checks clean. The vector table at
offset 0 is a valid Cortex-M reset/exception table (initial SP
`0x20000f18`, Reset handler `0x080001c1`, etc.), and content is a realistic
~29 KB of code/data (22.3% non-`0xFF`) with the remainder correctly erased.

**This chip is 128 KB flash**, not the 64 KB the "R8" part marking would
normally imply — confirmed by the dump correctly terminating in clean `0xFF`
at the `0x08020000` boundary. This matches the caveat already noted in this
document ("some R8 devices expose a 128 KiB physical flash array").

What changed between the failing session (11 attempts, all identical
timeout) and this success is not captured in this document — the user did
further rewiring outside this logged session. If revisiting this attack on
similar hardware in future, the prior 11-attempt failure log below remains
useful for what does *not* explain a failure (PA9, NRST, BOOT0, GND, SWD
quality, current-safety of direct GPIO drive were all individually
ruled out) even though the specific fix that resolved attempt 12 is unknown.

**The verified 128 KB dump (`mpk-mini-full-pass2.bin`, or either identical
pass) is the trusted restoration backup going forward.** Do not flash
anything to this STM32 or attempt to change RDP/option bytes without
re-reading this file.

## RDP removed and original firmware restored (2026-09-17)

With the verified backup in hand, RDP was removed and the original firmware
reflashed, at the user's explicit request:

1. Baseline confirmed: `stm32f1x options_read 0` → `read protection: on`.
2. `stm32f1x unlock 0` — succeeded (`stm32x unlocked.`). This triggers the
   STM32F1's hardware-enforced mass-erase as an unavoidable side effect of
   removing RDP Level 1; confirmed by the immediately following halted state
   reading `pc: 0xfffffffe` (all-`0xFF`, i.e. genuinely erased flash).
   **Required dropping the `WORKAREASIZE 0` override used throughout this
   whole project** (that override was there specifically to protect the
   SRAM-resident exploit payload during dumping; flash unlock/write genuinely
   needs a working area, and the first attempt with `WORKAREASIZE 0` still in
   place timed out).
3. Post-reset confirmation: `read protection: off`.
4. **Reflash gotcha — worth remembering for next time**: the chip's capacity
   register reports 64 KiB (the official "R8" marking), not the true 128 KiB
   physical size confirmed by the earlier dump. OpenOCD's flash bank is sized
   from that register by default, so the first `program` attempt silently
   only wrote the first 64 KiB (`Warn : no flash bank found for address
   0x08010000`, easy to miss). Fixed by explicitly overriding before sourcing
   the target config: `-c "set FLASH_SIZE 0x20000"` before
   `-f target/stm32f1x.cfg` on the command line. Re-running the full
   `program mpk-mini-full-pass2.bin 0x08000000 verify` with that override
   correctly reported `flash size = 128 KiB` and `Verified OK` for the whole
   image.
5. `reset run` to resume normal operation.

**Post-restore state**: the halted PC/SP immediately after the full reflash
(`pc: 0x080001c0`, `msp: 0x20000f18`) exactly matches the original firmware's
normal running state recorded before RDP was ever touched — strong
confirmation the restoration is a faithful, working copy, not just a
byte-identical-but-inert image. Final real-world check (does the Akai
enumerate as a MIDI device and respond to pads/keys via normal USB) to be
confirmed by the user after disconnecting the SWD probe.

**RDP is now OFF on this chip going forward.** This was left as-is
(not re-enabled) since normal SWD debug access will be wanted for the
USART1-mirror/ESP32 BLE-MIDI development work this whole project was
originally for (see `FINDINGS.md` objective). Re-enabling RDP later is
possible via `stm32f1x options_write` if ever wanted, but would need its own
care (same mass-erase-on-change caveats apply in reverse for some paths —
verify current ST documentation before doing so).

---

Status prior to the fix (kept for reference): 11 full attack attempts, all
failed identically (`Timeout: No data received from target`, 0-byte output).
Wiring was conclusively ruled out — a standalone SRAM test program
independent of the glitch mechanism proved PA9 genuinely carries data (496
bytes, statistically confirmed genuine signal) when the chip is actually
made to transmit, yet two real attack attempts on that same proven-good
connection (attempts 10 and 11) still produced only silence on both the Pico
and an independent receiver. See "Session summary and where this stands"
below for the full failure analysis — superseded by the success above, but
kept as a record of the elimination process.

MCU corrected 2026-09-16: **STM32F102R8T6**, not the previously assumed
STM32F103R8T6. See `FINDINGS.md` → "MCU identification correction" for detail.
This does not change the attack — F101/F102/F103 share the same Cortex-M3 core
and flash/RDP/FPB architecture — but pin numbers below carry a verification
caveat (see "Pin numbers — verification status").

## What this method changes

The Pico Pwner attack loads a small payload into STM32 SRAM through SWD, briefly
cycles target power, configures volatile Cortex-M3 FPB registers, and streams
flash through a USART TX pin. It does not erase/program STM32 flash, unlock RDP,
or write option bytes. SRAM and FPB changes disappear after a normal power cycle.

The official Pico attack firmware v1.2 is already installed on the connected
Pico. It enumerates as `/dev/ttyACM0` and by-id
`usb-Raspberry_Pi_Pico_E661410403243136-if00`.

## Revised plan: direct-GPIO wiring, no transistor

The upstream `stm32f1-picopwner` project's own documented default hardware setup
(`stm32f1-picopwner/README.md`, "Hardware Setup" section) connects the Pico's
power-control GPIO **directly** to the target's VDD — no MOSFET or BJT switch.
An earlier version of this document specified a P-MOSFET + NPN load-switch
circuit out of caution; that is not what upstream actually requires, and the
user's available parts are resistors only, so this revision follows upstream's
default instead.

**This only works under one condition: the Pico's GP2 pin must be the target's
sole power source during the attack.** The AD07's own 5V input and onboard
LM1117-3.3 regulator must not be delivering power to the STM32 at the same time
— if they were, GP2 driving low to "cut" power would just short against the
regulator's low-impedance output instead of actually de-powering the chip, and
could put excess current through the Pico's GPIO. Concretely: **run the Akai
with USB completely unplugged during the attack**, so the board has no other
power source, and let the Pico's GP2 pin power the whole 3.3V rail.

### Required pre-check: current draw

A single RP2040 GPIO safely sources on the order of 10–15 mA continuously
(well under its 50 mA absolute-maximum rating, which isn't the number to design
around). The AD07's 3.3V rail feeds more than the bare STM32 — key matrix,
LEDs, rotary encoder — unlike the bare "Blue Pill" board upstream tested on.

Before wiring anything to GP2:

1. Power the Akai normally via USB.
2. Measure current draw on the 3.3V rail with the multimeter already on hand
   (in series with the rail, or at the LM1117 output).
3. If draw is comfortably under ~15 mA: direct GP2→VDD wiring below should work
   as-is.
4. If draw is higher: direct drive is not safe to attempt. Use the low-side NPN
   fallback below instead.

### Parts on hand (2026-09-16)

- Resistors: 220 Ω, 560 Ω, 10 kΩ.
- Transistor: BC547B (small-signal NPN, TO-92).

Assigned as follows:

| Use | Value | Why |
| --- | --- | --- |
| NRST series resistor | 220 Ω | Low end of the 220 Ω–1 kΩ spec — NRST is the one timing-critical signal in this attack, so the lower value (less RC softening of the edge) is preferred over 1 kΩ. |
| PA9 series resistor | 560 Ω | Spec called for ~1 kΩ, but this is just a protective series resistor on a UART line, not timing-critical — 560 Ω is a fine substitute. |
| PB2/BOOT1 pull-up | 10 kΩ | Middle of the 1 kΩ–100 kΩ spec. |
| Low-side switch base resistor (fallback only) | 560 Ω | See below. |

### Low-side NPN fallback (fully specified, use only if the current-draw check fails)

Resourced by the BC547B on hand. This keeps the Akai powered normally through
its own regulator (unlike the direct GP2→VDD method, which requires the Akai's
USB to stay unplugged) and instead interrupts the board's GND return path:

- Keep the Akai powered via a normal USB cable from a wall charger or PC — **not**
  from the Pico — throughout the attack.
- Cut the **GND conductor** inside a spare/sacrificial USB cable feeding the
  Akai (not your only usable cable).
- Insert the BC547B as a low-side switch across that cut:
  - **Collector** → the Akai-side GND stub.
  - **Emitter** → the supply-side GND stub, which also ties to Pico GND and
    common ground.
  - **Base** ← 560 Ω ← Pico GP2. 560 Ω gives roughly 4.6 mA of base drive at
    3.3V logic-high, which comfortably saturates a BC547B for any load up to
    its 100 mA absolute maximum — well above what this board should draw.
- GP2 high → transistor saturates → GND path complete → board powered. GP2 low
  → GND path opens → board loses its return path → effectively unpowered, even
  though 5V is still present at the input.
- In this variant, GP2 in the wiring table below is **not** connected to VDD —
  it drives this transistor's base instead. Skip the C10 VDD tap entirely.

This is simpler than the originally-considered P-MOSFET high-side design (only
interrupts GND, not VBUS) and doesn't require any additional soldering on the
AD07 board itself beyond what's already done — only the sacrificial USB cable
and the transistor/resistor on a small piece of stripboard or dead-bug wiring.

## Wiring table

| Pico physical pin | Pico signal | AD07 / STM32 connection |
| --- | --- | --- |
| 38 | GND | Board GND (e.g. CN3 pin 1, or USB shield if continuity to board GND is confirmed) |
| 2 | GP1 / UART0 RX | STM32 PA9 / USART1 TX, through 560 Ω |
| 4 | GP2 | STM32 VDD — direct connection, no resistor (direct-drive method only; see the low-side NPN fallback above if the current-draw check fails, where GP2 instead drives the transistor base through 560 Ω) |
| 6 | GP4 | STM32 NRST, through 220 Ω |
| 7 | GP5 | STM32 BOOT0 |

Additional required connection:

- STM32 PB2/BOOT1 pulled up through 10 kΩ to **the same node GP2 drives in the
  direct-drive method** (i.e. to VDD, not to some other always-on 3.3V source —
  the C10 tap). This matters: if BOOT1's pull-up were tied to an independent
  supply, it would keep partially feeding the STM32's VDD net through the pin
  during the power-cut phase, undermining the glitch. Never leave BOOT1
  floating as an output — it must always have this pull-up present when
  powered. (If using the low-side NPN fallback instead, VDD stays continuously
  present from the board's own regulator, so this pull-up can go to that same
  continuously-present VDD without the same concern.)

## Physical access points on the AD07

Confirmed 2026-09-16 by direct inspection of the board photos
(`PXL_20260915_115053756.jpg`, `PXL_20260915_120141492.MP.jpg`,
`PXL_20260915_120153555.jpg`, `PXL_20260915_121955450.jpg`) and schematic page 7
(`ad07-schematic-page7.jpg`). The chip marking is visually confirmed as
`STM32F102 R8T6 GH205 93 CHN GH 029` in the close-up photo — matches the
2026-09-16 MCU correction in `FINDINGS.md`.

- **NRST, SWDIO, SWCLK, GND**: available at CN3, a 4-pin header immediately
  adjacent to U2 (the STM32), visible in the CN3 close-up photo. Pin 1 = GND,
  2 = SWDIO, 3 = SWCLK, 4 = NRST (verified in `FINDINGS.md`).
- **BOOT0**: available at JP1, silkscreened directly next to CN3/R4/C13 in the
  board photos. This matches schematic page 7's explicit label "Jumper in PCB
  to select Boot" pointing at the jumper next to the MCU symbol. Keep JP1 in
  its normal (open) state; GP5 drives this signal directly during the attack.
- **STM32 VDD — no chip-leg soldering needed.** `C10`, a 10 µF/16V electrolytic
  cap, sits immediately next to CN3 and U2 (visible in the CN3 close-up photo,
  marked "CS 10 16V"). A decoupling cap placed that close to an MCU is standard
  practice for sitting directly on that chip's own VDD/VSS pins, not some other
  filtered sub-rail — this is a far easier and safer solder point than a 0.5 mm
  QFP leg. **Before soldering, verify**: with the Akai powered normally via USB,
  measure C10 with a multimeter — it should read ~3.3V. This same measurement
  doubles as part of the current-draw pre-check earlier in this document.
- **PA9/USART1 TX** and **PB2/BOOT1**: no header or test point exists for
  either on this board — confirmed by reviewing all four photos, nothing else
  breaks these out. These require soldering a fine wire (30 AWG / wire-wrap
  wire is easiest) directly to the LQFP64 chip leg. See "Pin numbers" below for
  exact pin numbers and how to verify them against the physical chip before
  soldering. Verify by continuity before powering anything, and anchor the wire
  with a dab of hot glue or tape for strain relief once confirmed.

## Pin numbers on the STM32F102R8T6 (LQFP64)

From `FINDINGS.md`, read against the STM32F103R8 datasheet under the earlier
(incorrect) chip assumption, expected to still apply to the STM32F102R8T6 since
F101/F102/F103 medium-density parts share the same LQFP64 pinout by ST
convention:

| Signal | Pin number |
| --- | --- |
| NRST | 7 |
| PB2 / BOOT1 | 28 |
| PA9 / USART1_TX | 42 |
| PA13 / SWDIO | 46 |
| PA14 / SWCLK | 49 |
| BOOT0 | 60 |
| VSS (4 pins) | 18, 31, 47, 63 |

VDD pin numbers were never enumerated from the datasheet — not needed now that
C10 provides a tap point instead of a chip leg.

**Not independently re-verified against the actual STM32F102R8 datasheet.**
Three automated fetch attempts against
`st.com/resource/en/datasheet/stm32f102r8.pdf` timed out this session (large
PDF, no successful pull yet).

The close-up chip photo (`PXL_20260915_120141492.MP.jpg`) does show the pin-1
dot (top-left corner of the package) and what appear to be silkscreen
decade-count markers near the QFP corners, which the board manufacturer prints
specifically so an assembler/rework technician can count pins without a
datasheet. Before soldering to PA9 (pin 42) or PB2 (pin 28):

1. Use a loupe or macro photo to count pins from the pin-1 dot using those
   silkscreen markers as anchors.
2. Sanity-check your counting method first against a pin you already know is
   correct: count to pin 7 and confirm it lands on the same leg that traces to
   CN3 pin 4 (NRST) — you've already established that connection is correct via
   SWD checks in `FINDINGS.md`.
3. Only once that check passes, trust the same counting method to locate pins
   28 and 42.

## Software files and readiness (checked 2026-09-16)

- `picopwner-release-a1.2-t1.4/attack.uf2`: official Pico firmware v1.2.
  `sha256sum -c SHA256SUMS` in that directory passes clean on this file and all
  three `target_usartN.bin` files.
- `picopwner-release-a1.2-t1.4/target_usart1.bin`: official SRAM payload v1.4,
  matches PA9/USART1 wiring already in place.
- `dump-flipper.py` (top level, **not** `stm32f1-picopwner/dump.py`): the
  actual driver script to use. Source-audited this session — its
  `openocd_run()` is correctly hardcoded to `cmsis-dap.cfg`, `adapter serial
  DAP_Himimoni`, 50 kHz speed, and `WORKAREASIZE 0` regardless of the
  (vestigial, unused) `"stlink.cfg"` string still passed at each call site.
  **Do not run `stm32f1-picopwner/dump.py` for this attack** — that upstream
  script is hardcoded to `stlink.cfg` with no override flag and will not find
  the Flipper probe.
- `stm32f1-picopwner/`: audited upstream source, commit
  `76f17499abf7691289889564730c8528f2212ca0`, kept for reference/comparison
  only.
- **Bug found and fixed 2026-09-16**: `dump-flipper.py`'s `debug_probe_connected()`
  only recognized two ST-Link-oriented error strings as "not connected"
  (`"Error: open failed"`, `"Error: init mode failed (unable to connect to the
  target)"`), inherited unchanged from the upstream script. This board/probe
  combination's actual disconnect signature is `"Error: Error connecting DP:
  cannot read IDR"` (reproduced live by running the same OpenOCD command the
  function uses, with the SWD leads physically disconnected from CN3) — a
  different string, already documented separately in `FINDINGS.md` from an
  earlier session. Because it didn't match, the function always returned "still
  connected" regardless of actual physical state, trapping the run procedure at
  its opening "Debug probe already connected, please disconnect" check no
  matter what was actually connected. Fixed by adding `"cannot read IDR"` to
  the matched strings.
- Environment confirmed present: Python 3.14.7, pyserial 3.5, OpenOCD 0.12.0
  (exceeds the 0.11.0 minimum `dump-flipper.py` checks for), both
  `stm32f1x.cfg` and `cmsis-dap.cfg` in OpenOCD's script paths, and the Pico
  enumerated at `/dev/ttyACM0` (stable by-id:
  `usb-Raspberry_Pi_Pico_E661410403243136-if00`) as a serial device rather than
  a `RPI-RP2` mass-storage drive — consistent with the attack firmware already
  being flashed and running rather than sitting in the USB bootloader.
- `dump-flipper.py`'s default `--targetfw` path (`./target`, relative to its
  own location) does not exist in this layout — always pass
  `-t ./picopwner-release-a1.2-t1.4` explicitly, as in the run procedure below.

## Pin numbers on the STM32F102R8T6 (LQFP64)

From `FINDINGS.md`, read against the STM32F103R8 datasheet under the earlier
(incorrect) chip assumption, expected to still apply to the STM32F102R8T6 since
F101/F102/F103 medium-density parts share the same LQFP64 pinout by ST
convention:

| Signal | Pin number |
| --- | --- |
| NRST | 7 |
| PB2 / BOOT1 | 28 |
| PA9 / USART1_TX | 42 |
| PA13 / SWDIO | 46 |
| PA14 / SWCLK | 49 |
| BOOT0 | 60 |
| VSS (4 pins) | 18, 31, 47, 63 |

VDD pin numbers were never enumerated from the datasheet — not needed now that
C10 provides a tap point instead of a chip leg.

**Not independently re-verified against the actual STM32F102R8 datasheet.**
Three automated fetch attempts against
`st.com/resource/en/datasheet/stm32f102r8.pdf` timed out this session (large
PDF, no successful pull yet).

The close-up chip photo (`PXL_20260915_120141492.MP.jpg`) does show the pin-1
dot (top-left corner of the package) and what appear to be silkscreen
decade-count markers near the QFP corners, which the board manufacturer prints
specifically so an assembler/rework technician can count pins without a
datasheet. Before soldering to PA9 (pin 42) or PB2 (pin 28):

1. Use a loupe or macro photo to count pins from the pin-1 dot using those
   silkscreen markers as anchors.
2. Sanity-check your counting method first against a pin you already know is
   correct: count to pin 7 and confirm it lands on the same leg that traces to
   CN3 pin 4 (NRST) — you've already established that connection is correct via
   SWD checks in `FINDINGS.md`.
3. Only once that check passes, trust the same counting method to locate pins
   28 and 42.

PA9 and PB2 have both been soldered (2026-09-16), using this counting method.

## Run procedure

Wiring status as of 2026-09-16: all connections attached, using the low-side
NPN fallback (BC547B) rather than direct GP2→VDD, since a spare USB cable
wasn't available to isolate VBUS — see "Low-side NPN fallback" above for the
wiring actually in use. GP2 drives the BC547B base (through 560 Ω), not VDD
directly; VDD is fed continuously from Pico VBUS (pin 40) via C10/C1, so there
is no separate "make the VDD connection live" step — power follows GP2/BC547B
switching instead.

1. Double check the BC547B leg assignment (Collector/Base/Emitter) against the
   datasheet pinout, and check for shorts between the 5V wire and any GND point
   with the Pico unplugged.
2. Connect Pico USB. This powers the Pico itself, and — because the installed
   attack firmware drives GP2 high immediately on boot — also immediately
   saturates the BC547B and powers on the Akai. This is expected: the target
   needs to be powered and reachable via SWD for the initial payload load.
3. Make sure DAP Link is running on the Flipper with SWD pins set to `10,12`,
   but **do not connect its SWDIO/SWCLK/GND leads to CN3 yet.** The script's
   own opening check wants to start from a disconnected state — connecting the
   probe before launching it just triggers an immediate "Debug probe already
   connected, please disconnect" prompt (harmless, but avoid the extra step).
4. From `~/mpk-mini-ad07`, start the script:
   ```bash
   python3 dump-flipper.py \
     -p /dev/serial/by-id/usb-Raspberry_Pi_Pico_E661410403243136-if00 \
     -t ./picopwner-release-a1.2-t1.4 \
     -u 1 \
     -o ./mpk-mini-full-pass1.bin
   ```
5. Wait for it to print **"Waiting for debug probe (e.g. ST-Link) to be
   connected..."** — that is your cue to connect the Flipper's SWDIO/SWCLK/GND
   leads to CN3. It will then report RDP status and the detected SRAM entry
   point.
6. Follow the remaining prompts exactly, including when it asks you to
   disconnect the SWD debug probe again at a later point — the attack sequence
   depends on the probe being physically removed from the target (leads pulled
   off CN3, not just the Flipper's USB unplugged) at the right moment, not
   just idle.
7. Let the UART stream finish, then disconnect the Flipper leads and
   power-cycle the Akai (unplug/replug the Pico, or briefly interrupt GP2)
   before resuming normal use.
8. Repeat for a second full pass to a new filename, per the verification step
   below. Never reuse an output filename — the script truncates on open.

## Attempt log (2026-09-16)

Three full attack runs, all via `dump-flipper.py` (after the `debug_probe_connected()`
fix above), orchestrated with the script's own `input()` prompts satisfied
programmatically and the Flipper's SWD leads physically connected/disconnected
at CN3 at each prompt as instructed. Outputs:
`mpk-mini-full-pass1-attempt2.bin`, `mpk-mini-full-pass1-attempt3.bin` (a first
attempt predates this log and used `mpk-mini-full-pass1.bin`) — all three are
0 bytes.

All three runs followed the identical successful path up to the same point:
RDP confirmed on, SRAM entry point consistently detected at `0x108`
(`0x20000108`), target firmware loaded without error, probe disconnect
correctly detected, "Attack ready" reached, glitch triggered — then
`Timeout: No data received from target` every time. The Akai's own lights were
confirmed on (powered) both during and after each attempt, ruling out a total
loss of power.

Diagnosis performed between attempts (SWD reconnected each time to inspect
live CPU state, since the debug probe must be off CN3 during the glitch
itself):

- **A fresh `reset halt` after each failed attempt always shows the CPU
  executing from flash** (e.g. `pc: 0x080001c0`), never from SRAM. Since the
  FPB patch this exploit relies on is documented to persist across a reset,
  this indicates stage 1's FPB write in `stm32f1-picopwner/target/entry.S`
  never successfully took effect during the attack sequence — not just that
  its effect was later overwritten.
- **The power-cut/restore mechanism itself is confirmed working**: the Akai's
  lights visibly go through a cycle consistent with power loss and recovery,
  and the Pico's own firmware (`attack/attack.c`) only proceeds past its
  `while (gpio_get(RESET_PIN))` busy-wait once NRST is observed low, which it
  clearly did (attempts completed the full sequence rather than hanging at
  that step). This rules out the earlier concern that the low-side NPN switch
  might never achieve a real brownout at all.
- **BOOT0 direction confirmed working in both directions**: the very first SWD
  check performed this session (before any attack attempt) already showed
  `pc: 0x20000108` — proof BOOT0 being driven high via GP5/JP1 successfully
  put the chip into SRAM-boot mode. Since a plain push-pull GPIO output that
  reliably drives high against a weak pull resistor will reliably drive low
  too, this makes a stuck-BOOT0 wiring fault (e.g. JP1 landing on the wrong
  pad) unlikely, though it was never independently confirmed via the C10
  continuity check originally suggested.
- **The physical SWD connection at CN3 was found to be measurably
  flaky**: three repeated connection attempts in a row produced `SWD DPIDR
  0xe3780a77`, then `SWD DPIDR 0xe1f4063f` (both garbage — DPIDR is a fixed
  hardware ID that should never vary), then two consecutive correct reads of
  `0x1ba01477`. Roughly 1 in 3 raw connection attempts on this exact
  connection were corrupted. This was not fully resolved before the third
  attack attempt (which happened to connect cleanly) — **this points at a
  marginal/cold joint somewhere in the harness** (CN3 connection method,
  a resoldered leg, or similar) as the most likely root cause, on the
  reasoning that if this class of fault exists on the SWD leads, similarly
  marginal joints are plausible on NRST, BOOT0, or PA9, and this attack's
  timing (SRAM data retention window, FPB write, reset pulse) is sensitive
  enough that even brief intermittent contact loss during the critical
  window could explain a consistent, repeatable failure at exactly the
  stage-1-to-stage-2 handoff.

**Leading hypothesis, not yet confirmed**: a marginal physical connection
(most likely candidate: CN3 itself, if wired via friction-fit jumpers rather
than soldered) intermittently disrupting the glitch sequence's tight timing,
compounded by (independently possible) insufficient SRAM data retention
during the low-side GND-cutoff's discharge profile versus the originally
researched high-side VDD-cutoff.

**Recommended before a 4th attempt**: physically re-inspect every joint in the
harness (not just SWD) for cold/dull solder appearance and tug-test each one,
particularly the CN3 connection method itself. If failures persist after a
verified-solid re-inspection, the next real step is moving from the low-side
NPN fallback to a true high-side VDD cutoff (the originally researched
mechanism), which requires either a spare cable to isolate VBUS or a
scavenged P-MOSFET — neither available as of this session.

### 4th attempt (2026-09-16, after re-inspection)

A physical re-inspection was done between attempts. Confirmed by 4 consecutive
clean SWD connection checks immediately beforehand (all `DPIDR 0x1ba01477`, no
corrupted reads at all, versus the ~1-in-3 corruption rate seen earlier) — the
flaky-connection issue genuinely appears fixed. `mpk-mini-full-pass1-attempt4.bin`
run: RDP confirmed, SRAM entry point `0x108` detected, firmware loaded without
error, probe connect/disconnect both registered immediately with no retries
needed (unlike earlier attempts), attack triggered — **identical
`Timeout: No data received from target`, 0-byte output.**

This result meaningfully weakens the marginal-connection hypothesis as the
*sole* explanation: a demonstrably solid connection still produced the exact
same failure at the exact same point. Four full, cleanly-executed attempts
(1 predating this log, 3 logged here) have now failed identically regardless
of measured connection quality. The leading hypothesis is now the more
structural one: SRAM data-retention margin during the low-side GND-cutoff's
discharge profile not matching what this exploit (designed and tested against
a high-side VDD-cutoff on a bare "Blue Pill") expects — not fixable by further
re-soldering.

**Next real step**: move from the low-side NPN fallback to a true high-side
VDD cutoff. This needs either a spare cable to isolate VBUS for a P-MOSFET
high-side switch, or a scavenged P-MOSFET — track down one of these before
further attempts, rather than continuing to retry the current topology.

## Direct GPIO2 drive — evidence reassessed (2026-09-16/17)

Following the "next real step" above, an attempt was made to try the
upstream-default direct `GPIO2 → VDD` wiring (no transistor at all) as a
genuine high-side cutoff, in place of sourcing a MOSFET, reasoning that it's
the mechanism this exploit was actually designed and validated against. This
required rewiring: `C1 → Pico VBUS` disconnected entirely, `GP2` moved from
the BC547B base to land directly on **C10** (same node as the PB2 pull-up,
which was correctly left in place unchanged — see reasoning in-session), and
the BC547B bypassed (Collector bridged directly to Emitter) since GND no
longer needed to be switched.

**Three Pico USB dropout events were observed, but on review only one is good
evidence of a real current problem** — an in-session claim of "3/3 confirmed
brownouts, ruled out" was made in the moment and was wrong; corrected here:

1. Pico missing from the device list moments after a launch, shortly after
   physical wiring work had just been completed. **Ambiguous** — could be a
   current issue, could just as plausibly be incidental disturbance from
   handling the USB cable/wires while finishing the rewiring.
2. A crash mid-script with `OSError: [Errno 5] Input/output error` from
   `ser.write(b"1")` — the exact moment the attack sequence asks GP2 to cut and
   then restore power, i.e. exactly the moment an inrush current charging C10
   (and whatever else shares that 3.3V net) would hit, concurrent with the
   Pico also actively doing USB serial I/O. Nobody reported touching a cable
   for this one. **This is the one genuinely suspicious data point.**
3. A dropout that occurred right after the script printed its "please press
   reset or reconnect the Pico" prompt — **confirmed by the user to be their
   own manual USB unplug/replug**, not a dropout at all. This event provides
   no evidence either way and should not have been counted.

No physical damage was observed in any of the three events (LED off, no
warmth reported), and the Pico recovered fully each time.

**Net assessment: suspected, not proven.** One suspicious event (#2) is not
strong enough evidence to conclusively rule out direct GPIO drive on this
board, though it's also not nothing — an inrush-current brownout at exactly
the power-restore moment is a coherent, specific mechanism, not a vague
correlation. **Do not reconnect GP2 directly to C10/VDD without a deliberate,
controlled retest** (see below) — treat it as suspected-unsafe pending that
retest, not confirmed-safe.

**To get a clean answer**: retry with nobody touching any cable or wire for
the full duration of one attempt, watching `/dev/serial/by-id/` continuously
(polled automatically, not just checked after the fact) so a genuine
spontaneous dropout can be distinguished from incidental handling. If a
dropout still occurs with certainty that nothing was physically touched, that
would be clean evidence. If it completes without dropping, direct GPIO drive
may be viable after all and the current-draw concern was smaller than feared.

### Clean retest result (2026-09-17, `mpk-mini-full-pass1-attempt7.bin`)

Performed with continuous automated polling of `/dev/serial/by-id/` every
0.5s through the entire sequence (including the critical power-cut/restore
trigger moment where event #2 occurred), and nobody touching any cable or
wire from the point the script reached "waiting for debug probe" onward
(only the script's own normal per-run reset prompt at the very start involved
a manual USB replug, which is expected and unrelated).

**Result: no dropout at any point. Full clean run — RDP confirmed, SRAM entry
point `0x108`, firmware loaded, probe disconnect detected, attack triggered,
completed without any USB instability — but still ended in the same
`Timeout: No data received from target`, 0-byte output.**

This is a meaningful update: direct GPIO drive completed one full, cleanly
monitored cycle with zero evidence of a current problem, producing the
*exact same failure signature* as all 4 low-side NPN attempts. That weakens
the "switching topology changes SRAM-retention characteristics" hypothesis
considerably — if the power-cut mechanism itself were the differentiator,
this run should have looked different, not identical. Event #2's brownout now
looks more like an isolated incident than a reproducible property of this
topology, though a single clean run isn't enough to declare it fully safe
either.

**Revised leading hypothesis**: since both topologies (which differ only in
how power is cut) produce an identical failure, whatever is actually broken
is likely common to both — most plausibly the one link never independently
verified all session: **the PA9 wire itself**. Every other signal (NRST,
BOOT0, GND, SWD) has direct positive evidence of working (lights cycling
correctly, consistent SRAM entry-point detection every attempt, stable SWD
reads). PA9 has never produced a single confirmed byte, in either topology,
across 5 full attack attempts — its correctness has only ever been inferred
from a manual pin-count exercise against an unverified datasheet, never
functionally confirmed.

**Suggested next diagnostic** (not yet performed): isolate PA9 from the
overall attack sequence entirely. Load a minimal standalone SRAM program via
OpenOCD/SWD directly (bypassing RDP concerns entirely, since SRAM execution
via debug commands doesn't touch protected flash) that just initializes
USART1 and transmits a repeating known pattern, then set `pc`/`sp` via OpenOCD
and `resume` — independent of the Pico's glitch sequence entirely. Listening
for that pattern would need something other than the Pico's own firmware
(which only forwards data after detecting the specific dump-start magic) —
the Flipper's own UART/GPIO tooling, temporarily repurposed away from DAP
Link, is the most available option for this. Not attempted yet; worth doing
before further full attack attempts if the goal is to find the actual fault
rather than keep re-running the same untouched variable.

**Current state**: GP2 should be disconnected from C10 pending the clean
controlled retest above. The BC547B bridge (Collector-to-Emitter, from the
direct-drive rewiring) and the removed `C1 → Pico VBUS` connection have **not
been reverted back to the low-side NPN fallback wiring** as of the end of this
session — re-establishing that (reconnect `C1 → Pico VBUS`, unbridge the
BC547B, restore `GP2 → 560 Ω → BC547B base`) is needed before any further
attempt using that method.

**Where this leaves the project**: the low-side NPN fallback wires up fine
but the exploit fails to take hold (4/4, likely SRAM-retention/timing). Direct
high-side GPIO drive has one suspicious but unconfirmed data point suggesting
a current problem — worth a clean controlled retest (see above) before either
ruling it out or trusting it. If that retest confirms a real current problem,
a MOSFET (letting GP2 drive a gate at microamp levels while the MOSFET itself
switches the load current) is the remaining untried path, short of accepting
the existing 89.1% partial dump as the practical result.

## Verification

```bash
cd ~/mpk-mini-ad07
stat -c '%n %s bytes' mpk-mini-full-pass*.bin
sha256sum mpk-mini-full-pass*.bin
cmp -l mpk-mini-full-pass1.bin mpk-mini-full-pass2.bin
python3 ./verify-full-dump.py \
  ./mpk-mini-full-pass1.bin ./mpk-mini-full-pass2.bin \
  --partial ./mpk-mini-partial-pass1.bin
```

Require two identical full passes plus agreement with every non-marker word in
the existing 89.1% partial extraction before treating either pass as trustworthy.
Do not change RDP or flash the STM32 after one apparent success.

## Sources

- AD07 schematic page 7: https://www.manualslib.com/manual/2972663/Akai-Mpk-Mini.html?page=7
- Pico Pwner release: https://github.com/CTXz/stm32f1-picopwner/releases/tag/a1.2_t1.4
- Pico Pwner source and hardware-setup docs: https://github.com/CTXz/stm32f1-picopwner
- WOOT 2020 paper: https://www.usenix.org/system/files/woot20-paper-obermaier.pdf
- ST STM32F102R8 datasheet (pin numbers not yet independently re-verified from
  this source this session): https://www.st.com/resource/en/datasheet/stm32f102r8.pdf

## Session summary and where this stands (2026-09-17)

Ten full attack attempts (`mpk-mini-full-pass1.bin` predates this file's
attempt numbering; `mpk-mini-full-pass1-attempt2.bin` through `-attempt10.bin`
logged above), across two structurally different power-cut mechanisms, all
produced the identical result: RDP confirmed, SRAM entry point consistently
detected at `0x108`, firmware loaded without error, probe disconnect properly
detected, attack triggered — then `Timeout: No data received from target`,
0-byte output, every single time.

**A major clarifying result came from a standalone SRAM test program**
(`pa9-test2/`, source and build notes below), which bypasses the entire
glitch/FPB mechanism entirely: it's loaded directly into unused SRAM via SWD,
with SP/PC/xPSR set explicitly (not via a vector-table reset, which turned
out to have its own unrelated complications — see below), then resumed. It
simply initializes USART1 exactly like the real exploit's stage 2 and loops
transmitting `0x55` forever.

- Confirmed via register readback (`pc` sitting in the transmit loop,
  `USART1_SR` showing `TXE=1`) that the STM32 genuinely executes this code
  and the USART1 peripheral is configured correctly.
- With a multimeter probe held directly on the PA9 chip leg (bypassing the
  soldered wire and its 560 Ω resistor entirely) and an independent USB-UART
  adapter (Prolific PL2303) listening, **496 bytes were captured, 100% of
  which are exact bit-subsets of `0x55`** (verified programmatically — zero
  bytes contained any bit outside `0x55`'s pattern), including one perfect
  `0x55` byte. This is the unambiguous signature of a genuine, correct signal
  degraded by intermittent contact (bits stochastically dropping to 0), not
  noise. **This functionally confirms pin 42 is PA9** and that USART1
  transmission works correctly on the chip side — stronger confirmation than
  the datasheet cross-reference this was previously resting on.
- The soldered PA9 joint itself is therefore suspected of having the same
  class of marginal contact (a reheat during this session did not
  conclusively fix it — captures before and after were both sparse/weak) and
  is worth properly redoing, independent of everything below.

**However, attempt 10 — a real full attack run, with the independent PA9
capture running throughout on the exact same connection just proven capable
of carrying real data — still produced only 1 byte (`0x00`, consistent with
noise, not data).** This is the critical separation: the same physical path
that carried 496 bytes of recognizable signal during the manual test carried
essentially nothing during a real attack attempt. **The wiring is not what's
blocking the real attack.** The STM32 genuinely never reaches stage 2's
transmit code during the actual glitch sequence — stage 1's FPB patch either
never gets applied, or the chip never gets redirected to SRAM at all in the
real sequence, for reasons unrelated to PA9, NRST, BOOT0, GND, or power-cut
mechanism (all independently checked and functioning, see below).

What was checked and found **not** to be the cause of the real attack's
failure:

- Wiring continuity generally (user-verified).
- PA9 specifically — functionally confirmed correct and working (see above),
  though the permanent solder joint likely still needs redoing for a clean
  future full dump.
- NRST — functioning correctly (consistent entry-point detection every
  attempt implies correct reset-line monitoring).
- BOOT0 — confirmed driving correctly in both directions (the very first SWD
  read of the session already showed SRAM-boot behavior).
- GND/SWD connection quality — after early flakiness was found and a physical
  re-inspection fixed it (verified via repeated clean `DPIDR` reads).
- Current/brownout safety of direct GPIO drive — one full attempt completed
  with continuous dropout monitoring and no instability at all.

What was **not** resolved:

- The standalone SRAM test program hit unexplained Cortex-M reset/vector-table
  behavior on its first two design iterations — `reset halt` kept reporting
  the same `pc: 0x20000108` regardless of what was written to that SRAM
  address, and a version with a proper 2-word vector table at `0x20000000`
  still didn't get picked up by reset the way expected. Worked around
  (successfully) by explicitly setting `sp`/`pc`/`xPSR` via OpenOCD register
  writes and `resume` with no reset at all, avoiding the vector-table
  mechanism entirely — but why the vector-table approach didn't work as
  expected was never diagnosed. Not a concern for the actual exploit (which
  has its own separately-proven entry mechanism), but noted for anyone
  extending `pa9-test2/`.
- **The core question — why stage 1's FPB patch never takes hold during a
  real attack — remains unanswered**, and is now confirmed to be the actual
  and only remaining blocker, with wiring conclusively ruled out.

**Practical recommendation**: this has been an extensive, methodical
elimination process, and every checkable wiring/power/current hypothesis has
been checked. Further progress most likely needs either better instrumentation
(an oscilloscope to directly observe the NRST/VDD transition timing during the
actual glitch, which no tool used this session can substitute for) or
accepting the existing 89.1% partial dump (from the exception-based extractor,
`FINDINGS.md`) as the practical result of this effort. Continuing to retry the
identical procedure without new instrumentation or a new hypothesis is
unlikely to produce a different outcome.
