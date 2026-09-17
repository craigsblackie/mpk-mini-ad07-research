# Akai MPK Mini AD07 — SWD and firmware preservation findings

Recorded: 2026-09-15

## Objective and restrictions

Add BLE MIDI through an ESP32-C3 while preserving the original STM32 USB MIDI functionality, ideally simultaneously. Proposed future approach: analyse original firmware and mirror MIDI events through USART1 TX / PA9 to the ESP32-C3.

**Do not erase, unlock, change read protection, modify option bytes, or write STM32 flash before complete, verified backups exist. Removing STM32F1 read protection mass-erases main flash. No bypass has been run.**

## Hardware and prior observations

- Akai MPK Mini original / Gen 1, AD07 mainboard.
- MCU: STM32F102R8T6 (LQFP64, USB access line, medium-density; documented 64 KiB
  flash, 20 KiB SRAM). Corrected 2026-09-16 — see "MCU identification correction"
  below. Do not trust the earlier STM32F103R8T6 marking anywhere it still appears
  in older logs/filenames in this directory.
- Available: Flipper Zero, Raspberry Pi (model unspecified), ESP32-C3, Bus Pirate, logic analyser, multimeter.
- User previously observed no PA9/PA10 USART1 traffic during keyboard operation.
- Original USB comes directly from STM32 PA11/PA12 via 22-ohm R1/R2.
- JP1 is associated with BOOT0. Boot selection is sampled at reset; bridging it is not an RDP bypass.

## MCU identification correction (2026-09-16)

Earlier sessions and all prior extraction work in this directory assumed
**STM32F103R8T6** (performance line: USB + CAN). Direct inspection of the
physical chip marking corrects this to **STM32F102R8T6** (USB access line: USB,
no CAN, no advanced motor-control timers). Both are Cortex-M3, medium-density,
LQFP64, 64 KiB flash / 20 KiB SRAM parts documented under the same ST reference
manual (RM0008) and flash/option-byte programming manual (PM0075, "STM32F10xxx"
covers access, USB access, and performance lines together).

Implications:

- The earlier `DBGMCU_IDCODE` read (device ID field `0x410`) is the shared
  medium-density ID for the F101/F102/F103 sub-family and does not itself
  distinguish the exact line, so it does not contradict this correction.
- RDP Level 1 behaviour, the FLASH_OBR register, and the FPB/exception-based
  extraction methods (CVE-2020-8004, CVE-2020-13466) are architecturally
  identical across F101/F102/F103 — nothing about the two already-completed
  89.1% extraction passes needs to be redone or is invalidated by this
  correction.
- LQFP64 pin assignments for power/reset/boot/debug pins (NRST, BOOT0, VSS,
  SWDIO, SWCLK, PB2/BOOT1, PA9) are expected to be unchanged, since ST keeps
  these pin-compatible across the F101/F102/F103 medium-density family in a
  given package. **This has not been independently re-verified against the
  STM32F102R8 datasheet this session** — two fetch attempts against
  st.com/resource/en/datasheet/stm32f102r8.pdf timed out. Re-verify the exact
  pin table before soldering directly to any chip pin (PA9, PB2 in particular,
  since they have no header/test-point access on this board).
- Filenames and log files predating 2026-09-16 (e.g. `mpk-extractor-openocd-2.log`)
  may reference the old F103 assumption in comments/notes; the captured register
  values and binary dumps themselves are unaffected.

## CN3 discrepancy — use verified signals

The initial handoff claimed CN3 1=3.3 V, 2=NRST, 3=SWCLK, 4=SWDIO, with no ground. Visual inspection of the published AD07 service schematic (page 7) instead showed:

| CN3 schematic pin | Signal | STM32 LQFP64 pin |
| --- | --- | --- |
| 1 | GND | VSS: 18, 31, 47, 63 |
| 2 | SWDIO / PA13 | 46 |
| 3 | SWCLK / PA14 | 49 |
| 4 | NRST | 7 |

The three PCB photographs mentioned in the handoff were not attached in this session. The exact final physical CN3 wiring was not independently verified: the user swapped wires during troubleshooting and SWD subsequently worked. Do not infer physical header orientation from this table. Verify continuity with USB power disconnected.

## Flipper setup

Flipper name: Himimoni. USB VID:PID stays 0483:5740 in both normal and DAP modes, so the generic `lsusb` label “Virtual COM Port” alone is not enough to identify the mode.

- Normal mode serial: `flip_Himimoni`.
- DAP mode serial: `DAP_Himimoni`.
- DAP product: `Combined VCP and CMSIS-DAP Adapter`.
- CMSIS-DAP v1 HID and v2 bulk interfaces are present. OpenOCD successfully used v2.

In DAP Link:

1. Press Left on the main screen to open Config.
2. Select `SWC SWD Pins`.
3. Set it to `10,12` using Left/Right.
4. Return to the main screen and leave DAP Link running. Recheck the setting after restarting the app.

For this selection:

- Flipper physical pin 10 = SWCLK -> verified Akai SWCLK (schematic CN3 3).
- Flipper physical pin 12 = SWDIO -> verified Akai SWDIO (schematic CN3 2).
- Flipper GND -> verified board GND. The user used the Akai USB shield; continuity to board GND was not confirmed in conversation.
- No Flipper power output should be connected to the Akai supply.
- NRST was not required for the successful checks. Leave disconnected for these commands.
- Power the Akai via normal USB; use short signal wires.

The alternative app setting `2,3` uses Flipper PA7/PA6 and does not match wires on 10/12.

## Commands: identify the probe and connect

Run on the computer connected to the Flipper:

```bash
cd ~/mpk-mini-ad07
openocd --version
lsusb
lsusb -v -d 0483:5740
```

Look for `DAP_Himimoni` and the CMSIS-DAP interface descriptors. Close applications holding the Flipper connection if necessary.

Use the supplied configuration to connect at 50 kHz and read only device identification and the active protection register:

```bash
cd ~/mpk-mini-ad07
log="swd-check-$(date +%Y%m%d-%H%M%S).log"
timeout 20s openocd -f ./connect-readonly.cfg -l "$log"
```

If USB permissions prevent access, configure appropriate device permissions, or run this same reviewed command with `sudo`. A sandbox may also need host USB access. This file does not create a persistent debug server; it reads the registers and exits.

Equivalent command, as used successfully during this session:

```bash
timeout 20s openocd   -f interface/cmsis-dap.cfg   -c 'adapter serial DAP_Himimoni'   -c 'transport select swd'   -c 'set WORKAREASIZE 0'   -f target/stm32f1x.cfg   -c 'stm32f1x.cpu configure -event examine-end {}'   -c 'adapter speed 50'   -c 'reset_config none'   -c 'gdb_port disabled'   -c 'telnet_port disabled'   -c 'tcl_port disabled'   -c 'init'   -c 'mdw 0xE0042000 1'   -c 'mdw 0x4002201C 1'   -c 'shutdown'
```

“Read-only” means no flash/option-byte writes or reset/halt commands. SWD attachment itself changes debug state and can affect execution of protected firmware. The stock target's examine-end handler is explicitly cleared so it does not write DBGMCU_CR. To restore normal operation after debugging, disconnect the probe and power-cycle the Akai with its original boot-jumper arrangement.

## Confirmed results

The following is an excerpt transcribed from successful tool output, not a separately captured raw log:

```text
Info : SWD DPIDR 0x1ba01477
Info : [stm32f1x.cpu] Cortex-M3 r1p1 processor detected
Info : [stm32f1x.cpu] target has 6 breakpoints, 4 watchpoints
0xe0042000: 20036410
0x4002201c: 03fffffe
shutdown command invoked
```

- CPU connection succeeded repeatedly once the physical/app setup was corrected.
- DBGMCU_IDCODE = `0x20036410`; device ID field = `0x410`.
- FLASH_OBR = `0x03FFFFFE`, repeated across successful checks.
- FLASH_OBR bit 1 (RDPRT) = 1: **read protection enabled**, equivalent to RDP Level 1.
- Bit 0 (OPTERR) = 0.
- This STM32F1 uses protected/unprotected states; it does not have the permanent Level 2 of some newer STM32 families.
- An attempted halfword flash-capacity read starting at `0x1FFFF7E0` failed with `Failed to read memory at 0x1ffff7e2`. Therefore 64 KiB is based on the reported part marking/datasheet, not a successful capacity-register read.
- Raw RDP option bytes were not successfully read. The active state was established from FLASH_OBR.
- A recheck after the user bridged a jumper still returned `0x03FFFFFE`. The jumper was presumed to be JP1, but was not visually identified.
- Two independently executed partial extraction passes now exist. They are
  byte-for-byte identical, but neither is a complete restoration backup because
  1,790 of 16,384 words are explicitly marked unknown.
- No reset, halt, erase, unlock, flash programming, or option-byte changes were issued by the session's SWD checks.

Earlier failures:

- `cannot read IDR`: probe found, target SWD communication failed. This alone does not establish protection state.
- `unable to find a matching CMSIS-DAP device`: DAP interface absent or temporarily re-enumerating. One check confirmed normal Flipper serial mode rather than DAP mode.

## Published extraction bypasses

### Exception-based extraction — CVE-2020-8004

Marc Schink and Johannes Obermaier demonstrated leakage through interrupt/exception vector fetches. It uses SWD, resets, single-stepping, SRAM and CPU/register changes rather than flash or option-byte programming. Their STM32F103 experiment recovered 89.1% of flash, with systematic gaps. The original demonstration used an STM32F103RB.

- Research: https://blog.zapb.de/stm32f1-exceptional-failure/
- Extractor: https://gitlab.zapb.de/zapb/stm32f1-firmware-extractor

Assessment: tested successfully on this Akai with the Flipper/OpenOCD setup.
Two passes recovered 89.1% of the 64 KiB range and produced identical marked
images. The systematic unknown words remain unknown; the output is not a
restoration backup and does not authorise flashing.

### Power glitch plus FPB — CVE-2020-13466

The WOOT 2020 research demonstrated full extraction on STM32F103 using a short power interruption, temporary SRAM code and the Flash Patch and Breakpoint unit. FPB redirects fetches; it does not itself program flash. The paper tested STM32F103C8; operation on this R8 revision remains unverified.

- Paper, section 7.4 and Appendix D: https://www.usenix.org/system/files/woot20-paper-obermaier.pdf
- Original implementation: https://github.com/JohannesObermaier/f103-analysis/tree/master/h3
- RP2040 implementation: https://github.com/CTXz/stm32f1-picopwner

Pico Pwner requires an RP2040 board (e.g. Raspberry Pi Pico), a SWD probe, access to BOOT0, BOOT1/PB2, NRST, supply control, and a UART TX. PA9/USART1 TX is supported. A Linux Raspberry Pi is not a drop-in replacement for the supplied RP2040 firmware. The upstream tool targets the F103/F101/F102 family generically via the shared Cortex-M3/flash architecture, so the STM32F102R8T6 correction above does not change applicability.

Assessment: credible full-backup route. The upstream project's own default hardware
setup drives the target's VDD directly from a Pico GPIO (no MOSFET/transistor),
which only works if the Pico GPIO is the *sole* power source for the target during
the attack and the target's current draw stays within a GPIO's safe limits — see
`FULL_DUMP_PICO.md` for the current-draw pre-check and the full wiring plan.
Practise on a spare MCU before any power manipulation on the Akai if one becomes
available. No such work was performed yet.

## Complete verified backup obtained (2026-09-17)

The Pico power-glitch route (item 3 below, originally planned as a next step)
succeeded on 2026-09-17 after 12 attempts across two sessions — see
`FULL_DUMP_PICO.md` → "SUCCESS" for full detail. Two independent full-flash
passes, byte-for-byte identical, agreeing with every word of the partial
dump below: `mpk-mini-full-pass2.bin` (131,072 bytes,
`sha256: c189957e3b18bae01e97599ad573ff59bd633ce5fd11fe2eabdce1f08cfbc653`).

This corrects an earlier assumption: the chip is **128 KB flash**, not the
64 KB the "R8" marking would normally imply.

**This is now the trusted restoration backup.** The original "next steps"
list below (photography, Ghidra analysis, then only afterward the USART1/BLE
mirror plan) remains the right order — do not flash anything or touch RDP/
option bytes on the STM32 without re-reading `mpk-mini-full-pass2.bin` first.

## Next steps

1. Photograph and record the working physical wiring and boot-jumper state.
2. Load the verified marked partial image into Ghidra while retaining all
   `0xDEADBEEF` words as unknown. (Superseded: the complete dump above can
   now be loaded instead, with no unknown words at all.)
3. ~~If a complete backup is still needed, assess the Pico power-glitch route
   separately.~~ Done — see "Complete verified backup obtained" above.
4. Only after complete verified backups exist, return to the USART1
   mirror/ESP32 BLE-MIDI firmware plan. **Complete verified backup now
   exists — this plan may now proceed.**

## Exception extractor audit and first attempt (2026-09-15)

The upstream extractor was cloned at commit
`fac667a29c32e97eb5e70007f262faffed8995a2` into
`stm32f1-firmware-extractor/`. Source review of `main.py` and `openocd.py`
found no flash erase/program/unlock commands and no writes to the flash
controller or option-byte region.

Its intentional target changes are volatile: CPU reset/halt, four 16-bit
instructions written at SRAM addresses `0x20000000` through `0x20000006`,
register writes, exception-controller writes, VTOR relocation, and repeated
single stepping. These actions disrupt the running Akai firmware and require a
normal power cycle afterward, but they do not change STM32 flash or option
bytes.

`extractor-openocd.cfg` was added for the Flipper probe. A planned 16-word test
did not begin because OpenOCD could no longer read the target SWD DPIDR:

```text
Error: Error connecting DP: cannot read IDR
```

The same failure occurred with the previously successful
`connect-readonly.cfg`, confirming that the extractor configuration was not the
cause. No SRAM/register writes, reset, halt, exception generation, or firmware
extraction occurred during those failed attempts. The physical connection was
subsequently restored and extraction succeeded as recorded below.

## Verified partial extraction passes (2026-09-16)

The exception extractor was run twice over the complete documented 64 KiB flash
address range. Unrecoverable 32-bit words were written as the explicit marker
`0xDEADBEEF`; they were not guessed or filled with erased-flash values.

Both assembled images are 65,536 bytes and compare byte-for-byte identical:

```text
SHA-256: 9df6eb85c02fe153dd5da28e19d9cf61fbe41bfadeb262476183d57701bb3d13
Total words:     16384
Recovered words: 14594
Marked unknown:   1790
Coverage:        89.1%
```

With DAP Link running and the Akai powered normally, start the dedicated
OpenOCD server in one terminal:

```bash
cd ~/mpk-mini-ad07
openocd -f ./extractor-openocd.cfg -l mpk-extractor-pass-N.log
```

Run a complete marked pass in another terminal:

```bash
cd ~/mpk-mini-ad07/stm32f1-firmware-extractor
python3 main.py 0x00000000 0x4000 \
  --num-exceptions 59 --value 0xdeadbeef --binary \
  > ~/mpk-mini-ad07/mpk-mini-partial-pass-N.bin
```

Do not reuse an existing output filename. Stop OpenOCD with Ctrl-C only after
the extractor exits. The target is left in a debug-altered volatile state, so
disconnect SWD and power-cycle it before normal use.

Artifacts:

- `mpk-mini-partial-pass1.bin` — complete marked output from pass 1.
- `mpk-mini-partial-pass2.bin` — assembled marked output from pass 2.
- `mpk-mini-partial-pass2-prefix.bin` — 42,484-byte valid prefix retained
  after one transient target-halt/register-read failure at offset `0xA5F4`.
- `mpk-mini-partial-pass2-tail.bin` — 23,052-byte resumed extraction starting
  at offset `0xA5F4`.
- `mpk-extractor-pass1.log` and `mpk-extractor-pass2.log` — OpenOCD logs.

Pass 2 resumed with this extractor range after the transient failure:

```bash
cd ~/mpk-mini-ad07/stm32f1-firmware-extractor
python3 main.py 0x0000a5f4 0x1683 \
  --num-exceptions 59 --value 0xdeadbeef --binary \
  > /tmp/mpk-mini-partial-pass2-tail.bin
```

The prefix and tail lengths add exactly to 65,536 bytes. `cmp` reported no
differences between the assembled pass 2 image and pass 1, and their SHA-256
digests match.

The pass-2 segments were assembled and checked with:

```bash
cp mpk-mini-partial-pass2-prefix.bin mpk-mini-partial-pass2.bin
dd if=mpk-mini-partial-pass2-tail.bin \
  of=mpk-mini-partial-pass2.bin oflag=append conv=notrunc status=none
stat -c '%n %s bytes' mpk-mini-partial-pass2.bin
cmp -s mpk-mini-partial-pass1.bin mpk-mini-partial-pass2.bin
sha256sum -c SHA256SUMS
```

**These are verified repeatable partial analysis images, not complete firmware
backups and not safe flash images.** The 1,790 markers represent systematic
gaps in this exception-fetch method; agreement between passes verifies
repeatability but does not recover those missing words.

OpenOCD was stopped after extraction. Disconnect SWD and power-cycle the Akai
with its normal boot-jumper state before normal use.

## Primary documentation

- Akai AD07 service manual, schematic page 7: https://www.manualslib.com/manual/2972663/Akai-Mpk-Mini.html?page=7
- ST STM32F102R8 datasheet (correct part, unverified this session — fetch timed
  out twice): https://www.st.com/resource/en/datasheet/stm32f102r8.pdf
- ST STM32F103R8 datasheet (superseded reference, kept only because the earlier
  pin table in this file was read from it under the old chip assumption):
  https://www.st.com/resource/en/datasheet/stm32f103r8.pdf
- ST PM0075 (RDP and erase behaviour, covers STM32F10xxx generically): https://www.st.com/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf
- DAP Link source: https://github.com/flipperdevices/flipperzero-good-faps/tree/dev/dap_link
- Exact menu labels: https://github.com/flipperdevices/flipperzero-good-faps/blob/dev/dap_link/gui/scenes/dap_scene_config.c
- OpenOCD target configuration: https://github.com/openocd-org/openocd/blob/master/tcl/target/stm32f1x.cfg
