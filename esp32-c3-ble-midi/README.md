# ESP32-C3 Bluetooth LE MIDI bridge

This ESP-IDF project turns an HW-466AB ESP32-C3 SuperMini into a bidirectional
Bluetooth LE MIDI transport for the open MPK mini mk1 firmware. USB MIDI stays
active at the same time, and the bridge is powered from the keyboard, so there
is no second cable and no battery.

## Status

Built warning-free against ESP-IDF v6.0.3 for `esp32c3`. The app is 1.2 MB, so
`partitions.csv` gives it a 2 MB slot -- the default 1 MB layout cannot hold
BLE and WiFi together. **The assembled system
has not been bench-tested against the keyboard yet** — the wiring, the power
tap and the end-to-end MIDI path below are derived from the AD07 service
schematic and the firmware sources, not from a working unit. Work through
"Bring-up" in order rather than soldering everything first.

The BLE-MIDI translation itself is covered by host-side round-trip tests (see
"Tests"). Independently confirmed: the service and characteristic UUIDs used here match
the ones BlueZ 5.87 implements in `profiles/midi` byte-for-byte, so a Linux
host recognises the device as a MIDI peripheral rather than a generic GATT one.

## Prerequisites

The keyboard must already be running the open firmware in `../firmware`. The
stock AKAI firmware never drives USART1, so the bridge would sit silent. The
open firmware mirrors every outgoing USB-MIDI event to PA9 and feeds anything
arriving on PA10 into the same handler USB SysEx uses.

## Parts

- ESP32-C3 SuperMini (HW-466AB or equivalent), USB-C.
- A 1 A diode: 1N5819 Schottky preferred, but a plain 1N4007 works too --
  see "Power" for the headroom arithmetic.
- 100 µF electrolytic capacitor, 10 V or higher.
- 100 nF ceramic capacitor (optional but recommended).
- Thin enamelled or silicone wire, 30 AWG or similar, for the two chip pins.

## Wiring

### Signals

| MPK mini STM32 | ESP32-C3 SuperMini | Purpose |
|---|---|---|
| PA9 (LQFP64 pin 42) | GPIO4 | MPK MIDI TX to ESP RX |
| PA10 (LQFP64 pin 43) | GPIO5 | ESP MIDI TX to MPK RX |
| GND | GND | Common reference |

Both sides are 3.3 V logic and sit on the same board, so this is a direct
TTL link — no MIDI opto-isolator, no level shifting, no current-loop resistors.

PA9 and PA10 have **no header or test-point access on the AD07 board**; they
must be soldered directly to the 0.5 mm-pitch LQFP64 pins. Re-verify the pin
numbers against the STM32F102R8 datasheet before soldering: this project's
notes flag that the LQFP64 pin table was carried over from the F103 assumption
and not independently re-checked (see `../FINDINGS.md`). Buzz out continuity
with power removed first.

GND is easiest to take from CN3 pin 1, the SWD header, which is a real
through-hole connector.

### Power

Take the keyboard's filtered +5 V rail, not its 3.3 V rail:

| MPK mini | Protection | ESP32-C3 SuperMini |
|---|---|---|
| +5 V net (see below) | 1 A diode, anode to MPK, striped cathode to ESP | `5V` |
| GND | direct | `GND` |

The diode is not optional. The SuperMini's `5V` pin is wired **directly** to its
USB-C VBUS with no reverse protection on the board, so without it, plugging in
USB-C while the keyboard is powered puts two supplies on one node.

Either diode type works, because the SuperMini regulates with an **ME6211C33**,
a true low-dropout part -- 120 mV at 100 mA, 260 mV at 200 mA, so it needs only
about 3.7 V in even at the ~300 mA the radio peaks at:

| Diode | Forward drop | Left at `5V` from 5 V | Margin at WiFi peak |
|---|---|---|---|
| 1N5819 Schottky | ~0.35 V | ~4.65 V | ~0.95 V |
| 1N4007 silicon | ~0.8 V | ~4.2 V | ~0.5 V |

The Schottky is the better part and costs the same, but a 1N4007 has enough
headroom here. Its slow reverse recovery does not matter -- the diode only
blocks DC backfeed. (The "ME6211 needs 4.3 V" figure quoted in some write-ups
is specified at 500 mA sustained, which this board cannot dissipate anyway.)

**Check the regulator marking.** Some SuperMini batches carry an
**LP5907** (SMD marking `LLVB`) rated 250 mA, rather than the 500 mA ME6211.
An ESP32-C3 transmitting WiFi draws 276 mA or more, which is over that
rating, so `editor.c` caps the portal's transmit power to 11 dBm
(`AP_TX_POWER`). The portal serves one browser in the same room, so the range
full power buys is worthless while the current spike is not. BLE alone is well
inside either part.

On an LP5907 board the **1N4007 is the better diode**, which is the opposite
of the usual advice. Dropout is not the constraint -- the LP5907 needs only
~120 mV even at full load -- but the regulator dissipates `(Vin - 3.3) x I`,
so the silicon diode's larger drop moves heat off a SOT-23-5 package that has
very little copper to lose it into:

| Diode | LDO input | LDO dissipation at 280 mA |
|---|---|---|
| 1N4007 | ~4.2 V | **0.25 W** |
| 1N5819 | ~4.65 V | 0.38 W |

On the AD07 schematic the USB Mini-B connector CN2 feeds VBUS through ferrite
bead FB1 into the **+5 V** net, which supplies bulk cap C3, 100 nF C4 and the
input of U3, an LM1117-3.3. Tap that net — the positive leg of C3 or the U3
input pin are the two convenient points. It is filtered but upstream of the
regulator.

Do **not** feed the SuperMini's `3V3` pin from the MPK's 3.3 V rail. That rail
comes from the LM1117, a linear regulator in a small SMD package inside a
sealed plastic enclosure; adding the ESP32-C3's ~120 mA average and ~350 mA
transmit peaks would drop 1.7 V across it and add up to half a watt of heat it
has no budget for. Feeding the `5V` pin instead uses the SuperMini's own
regulator and leaves the LM1117's load unchanged.

Place the bulk capacitor across the SuperMini's `5V` and `GND` pins, close to
the board and the right way round, with the 100 nF in parallel if you have one.
The ESP32-C3's radio draws in bursts and the run back to the MPK's bulk cap is
long enough to matter.

100 uF is the sensible default. A larger one buffers the radio better, which
helps on an LP5907 board, but USB 2.0 limits downstream bulk capacitance to
10 uF per device (spec 7.2.4.1) because charging a big reservoir from cold
looks like a short: the inrush can trip a host port's over-current protection
or sag VBUS enough to disturb other devices on the same hub. Plenty of devices
exceed that limit happily -- if the keyboard enumerates reliably on the port
you actually use, and nothing else on the bus glitches when you plug it in,
the larger cap is fine. Otherwise drop back toward 100-220 uF. Do not add
series resistance to tame the inrush; it would eat the supply headroom the
diode arithmetic above depends on.

The open firmware already declares the USB 2.0 high-power maximum (500 mA in
`bMaxPower`) instead of stock's 100 mA, so the combined draw is within what the
keyboard is entitled to request. Plug the MPK into a real host port or a
powered hub; a passive hub sharing one port between several devices may not
deliver it.

### Pin locations on the SuperMini

With the USB-C connector at the bottom and the antenna at the top, the usual
HW-466AB layout puts GPIO4 on the left row and GPIO5 at the top of the right
row, with `5V` and `GND` adjacent at the bottom of the right row. Board
revisions differ — follow the silkscreen, not this description.

## Build and flash

Install and activate ESP-IDF (v6.0.3 is what this was built against), then:

```sh
cd esp32-c3-ble-midi
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

`ls /dev/ttyACM*` shows the native USB serial/JTAG port. If none appears, hold
`BOOT`, tap `RST`, release `BOOT`, and flash again. Exit the monitor with
`Ctrl-]`.

Flash the SuperMini over USB-C **with the keyboard unplugged**, so the two
5 V sources are not both live on the same node.

## Tests

The translation logic is exercised on the host, without hardware. `main.c` is
compiled against stub ESP-IDF and NimBLE headers; MIDI is pushed through the
parser, the outbound queue and the packet builder, and every notification that
comes out is fed back through the inbound decoder. The recovered byte stream
must match what went in.

```sh
cd esp32-c3-ble-midi/test
make check
```

Forty-eight cases across two suites, run under AddressSanitizer and UBSan.

The BLE suite covers channel messages, running status expansion, realtime
interleaved mid-message, inline and fragmented SysEx, SysEx ordered against
surrounding notes, a 100-message burst, retry under simulated controller
buffer exhaustion, timestamp rollover and timestamp-high packet splitting,
discard while unsubscribed, overlong SysEx recovery, and rejection of
malformed inbound packets.

The editor suite drives the HTTP handlers against a fake keyboard that answers
`'c'`, `'a'`, `'v'` and `'d'` the way the real one does: program decode,
read-modify-write, tempo clamping, rejection of a bad program index, a timeout
surfacing as an error rather than a fabricated success, settings round trip,
and a check that every byte the bridge puts on the wire is 7-bit safe. It also
compares `editor.c`'s copy of the firmware's wire-reorder table against
`firmware/src/program.c` byte for byte, so the two cannot drift apart
unnoticed.

This covers the protocol, not the wiring, the power tap, or the UART link.

## Bring-up

Do this in order. Each step is verifiable on its own.

1. **Flash the ESP alone**, on USB-C, nothing else connected. The monitor
   should print `advertising as MPK mini Open`, and the onboard LED should
   blink once per second.
2. **Pair it with nothing attached** (see below). A host should see the device
   and open a MIDI port; the LED should go solid. No MIDI will flow yet.
3. **Bench-wire the signals only.** Power each board from its own USB port,
   join GND, PA9→GPIO4 and PA10→GPIO5, and leave both 5 V rails separate.
   Press a key: it should arrive at the BLE host. Send SysEx from an editor
   over BLE: the keyboard should answer.
4. **Only then fit the diode, capacitor and 5 V tap**, unplug the SuperMini's
   USB-C, and confirm it still boots and connects on keyboard power alone.
5. Check that USB MIDI from the keyboard still works at the same time.

## Pairing

The device advertises as **MPK mini Open**. Connect to it from a BLE-MIDI-aware
path, not the operating system's generic Bluetooth audio pairing screen.

**Linux (BlueZ 5.87, confirmed to carry the MIDI profile on this machine):**

```sh
bluetoothctl
# scan on, wait for "MPK mini Open", then:
pair    XX:XX:XX:XX:XX:XX
trust   XX:XX:XX:XX:XX:XX
connect XX:XX:XX:XX:XX:XX
```

BlueZ's `midi` plugin then exposes an ALSA sequencer port; `aconnect -l` will
list it. `trust` makes it reconnect on its own afterwards.

**macOS / iOS:** Audio MIDI Setup → MIDI Studio → Bluetooth, or the Bluetooth
panel inside the DAW.

**Windows:** BLE MIDI is reachable only through the WinRT MIDI API, which most
DAWs do not use. Bridge it with MIDIberry plus loopMIDI, or a vendor BLE-MIDI
driver.

Bonds are stored in NVS (`CONFIG_BT_NIMBLE_NVS_PERSIST`), so pairing survives
the power cycle that happens every time the keyboard is unplugged.

## Status LED

Onboard LED on GPIO8, active low. Set `STATUS_LED_ENABLE` to 0 in `main.c` for
a board that wires it elsewhere.

| Pattern | Meaning |
|---|---|
| One short blink per second | Advertising, no host |
| Double blink | Connected, but the host has not subscribed to notifications |
| Solid | Connected and subscribed — MIDI will flow |
| Fast even blink | Editor portal is up (overrides the states above) |

## Editor portal

The bridge already speaks the keyboard's editor SysEx, so it can host the
editor itself. Press the SuperMini's **BOOT** button: it raises a WiFi access
point and serves a single-page editor. No driver, no host application, and
nothing that depends on the discontinued AKAI editor still running on a
current OS.

| | |
|---|---|
| Network | `MPK-mini-Open` |
| Password | `mpkmini1` |
| Address | `http://192.168.4.1/` |

Press BOOT again to shut the portal down. It is **off by default and not
persistent** -- it never comes up on its own, because an idle AP would share
the one 2.4 GHz antenna with BLE for no benefit and would roughly double the
bridge's draw on a supply taken from the keyboard's USB rail. BLE MIDI keeps
working while the portal is up; software coexistence is enabled for exactly
this overlap.

The editor covers the MIDI and pad channels, octave and transpose, the full
arpeggiator page, all eight pads across both banks (note, CC, program change,
toggle), all eight knobs (CC, low, high), and the velocity curves, with a live
graph of the key and pad response against the linear reference.

Two behaviours worth knowing. A save is **read-modify-write**: the page sends
only what changed, the bridge re-reads the program first, so nothing you did
not touch is disturbed. And a save is **confirmed, not assumed** -- the bridge
reads the program back and compares it before reporting success, so a failed
write surfaces as an error instead of a silent no-op.

Change the network name or password in `main/editor.c` (`AP_SSID`,
`AP_PASSWORD`; WPA2 needs at least 8 characters). The portal is a local AP
with no route to the internet, but anyone in range with the password can
change your programs -- treat it as you would any other open panel on the
instrument.

## Protocol and behaviour

- BLE service `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`, MIDI characteristic
  `7772E5DB-3868-4112-A1A9-F2669D106BF3`.
- Editor SysEx: the stock `'a'`/`'b'`/`'c'`/`'d'` commands, plus `'v'` for the
  velocity curves, which the open firmware adds (see `../firmware/README.md`).
- UART: 31250 baud, 8N1, GPIO4 RX, GPIO5 TX.
- Channel messages, running status, MIDI realtime and fragmented SysEx all
  cross in both directions. Running status from the keyboard is expanded to
  full messages on the BLE side, and BLE running status is expanded before it
  reaches the UART.
- Outbound MIDI is timestamped the moment it is parsed, queued, and packed so
  that several messages share one notification whenever they fall in the same
  BLE-MIDI timestamp window. When the controller runs out of buffers the sender
  waits and retries for about 200 ms instead of discarding, so a fast knob
  sweep cannot silently swallow the Note Off behind it.
- The queue is emptied on connect and disconnect, so a new host never receives
  MIDI from before it arrived.

Non-SysEx channel messages sent *to* the keyboard over BLE are accepted by the
bridge and passed to the STM32, which ignores everything except SysEx and MIDI
realtime — the same as over USB. The keyboard has no sound engine and no
MIDI-thru. What this does enable over Bluetooth is the editor's SysEx
program dump/restore and the arpeggiator's external MIDI clock.

## Known limits

- One central at a time (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`).
- SysEx from the keyboard is capped at 256 bytes per message, which covers the
  editor's 101-byte program records with room to spare.
- The bridge requests a 7.5–15 ms connection interval, but the central decides.
  A host that insists on a long interval will add latency the bridge cannot
  control.
