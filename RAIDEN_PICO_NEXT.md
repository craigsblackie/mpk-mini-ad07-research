# Raiden Pico — prepared 2026-09-17, waiting on hardware

## Why

After 11 full attempts via `stm32f1-picopwner` (see `FULL_DUMP_PICO.md`), wiring
(PA9, NRST, BOOT0, GND, SWD) is conclusively ruled out as the cause of failure —
proven via a standalone SRAM test program showing PA9 genuinely carries data
under good contact, and real attack attempts on that same connection still
producing silence. The remaining blocker is stage 1's FPB patch never taking
hold, most likely because power-cut/restore timing doesn't match this board's
SRAM-retention window — plausibly due to board capacitance (C10 and other
rail decoupling) slowing the voltage decay versus the bare "Blue Pill" this
attack was designed against. `stm32f1-picopwner`'s Pico firmware has no way to
directly measure the actual voltage during the glitch to confirm or fix this;
it only infers timing from NRST's digital state.

[raiden-pico](https://github.com/AdamLaurie/raiden-pico) is a more capable
alternative: it has ADC-gated glitching with deterministic calibration — it
directly measures the target's voltage during the glitch and calibrates the
actual brown-out threshold for this specific board, rather than assuming
generic timing. It also implements the same STM32F1 RDP1 FPB-bypass technique,
with existing payloads for the F1 family in `raiden-pico/stm32_payloads/f1/`.
This is effectively the oscilloscope-equivalent capability `FULL_DUMP_PICO.md`
identified as the real next step, built into the tool itself.

## Hardware gap — now resolved

Requires a **Raspberry Pi Pico 2 (RP2350)**, not the original Pico (RP2040)
used all session — the ADC-gated calibration relies on RP2350-specific
timing/ADC characteristics. **User has ordered one; not yet arrived.**

## What's already done (software side, doesn't need the Pico 2)

- `~/mpk-mini-ad07/raiden-pico/` — cloned from the repo above.
- `~/mpk-mini-ad07/pico-sdk/` — cloned with all submodules (682 MB;
  `PICO_SDK_PATH` must point here).
- Build verified working end-to-end:
  ```bash
  export PICO_SDK_PATH=/home/user/mpk-mini-ad07/pico-sdk
  cd ~/mpk-mini-ad07/raiden-pico
  ./build.sh
  ```
  Produces `build/raiden_pico.uf2` (472 KB) — confirmed builds clean, no
  errors, on this machine's toolchain (arm-none-eabi-gcc, cmake already
  installed).

## What's still needed once the Pico 2 arrives

1. **Flash it**: hold BOOTSEL on the Pico 2, plug in USB, copy
   `build/raiden_pico.uf2` to the `RPI-RP2` drive that appears.
2. **A small voltage-divider circuit** for the ADC voltage-monitor input
   (GP26) — scales the target's VDD down to a safe ADC input range. Exact
   resistor values not yet confirmed from the repo docs; check
   `raiden-pico/README.md` / `GLITCHING_GUIDE.md` / `VMIN.md` in detail before
   wiring.
3. **Rewiring to raiden-pico's pin map** — different from `stm32f1-picopwner`'s:

   | Pico 2 GPIO | Function |
   | --- | --- |
   | GP4/GP5 | Target UART (PA9, or per selected USART) |
   | GP10-12 | Power control (ganged for current — may resolve the current-draw problem hit earlier with direct GPIO drive on the original Pico) |
   | GP13/14 | BOOT0/BOOT1 |
   | GP15 | NRST |
   | GP17/18 | SWD (SWCLK/SWDIO) |
   | GP26 | ADC voltage monitor (via divider) |

   Note this uses SWD directly from the Pico 2 itself (GP17/18), not a
   separate Flipper probe — re-examine whether the Flipper is still needed at
   all with this tool, or whether raiden-pico drives SWD itself
   (`src/swd.c` exists in the repo, suggesting the latter).
4. **Read the STM32F1 RDP1 Bypass workflow section of `README.md` in full**
   (only skimmed via web search this session) — it describes a `TARGET GLITCH
   SWEEP` calibration step before the actual bypass attempt, which is the key
   new capability this tool adds over what was used tonight.
5. The repo also contains extensive prior exploration notes
   (`GLITCH_TEST_RESULTS.md`, `SUCCESS_FOUND.md`, `VMIN.md`,
   `CRP_TRACE_FINDINGS.md`, `stm32_payloads/f1/stm32f1_bootrom_analysis.md`)
   from the tool's own development against STM32F1 targets — worth reading
   before wiring, since they may already document known-good timing ranges or
   gotchas specific to this exact attack class.

## Do not repeat past mistakes

- Verify PA9 (and any other pin) against this board's own confirmed pin
  numbers/access points in `FULL_DUMP_PICO.md` and `FINDINGS.md` — don't
  re-derive from scratch.
- Keep the same non-destructive discipline: no flash erase/program/unlock
  commands, no option-byte writes, until a complete verified backup exists.
