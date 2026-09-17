#ifndef SYSEX_H
#define SYSEX_H

#include <stdint.h>
#include <stddef.h>

/*
 * AKAI editor-software SysEx protocol -- receive support.
 *
 * Implements the wire framing and command dispatch for FUN_08002eac,
 * decoded in FIRMWARE_ANALYSIS.md's "Confirmed: SysEx editor-protocol
 * handler" section: reassembles incoming USB-MIDI SysEx packets
 * (CIN 0x4-0x7 events, handled by overriding usb_midi_on_receive() --
 * see usb.h) into complete messages, then dispatches 'b' (select
 * program), 'a' (write/receive a program dump), and 'c' (read/send a
 * program dump) using program.c's decoded record layout and wire
 * reorder table.
 *
 * Also implements 'd' (status/ack query -- replies with the current
 * program number).
 *
 * NOT implemented: '`' (raw payload capture -- doesn't appear to do
 * anything durable in the original either) and 'j' (device
 * identification/bootstrap/factory-reset) -- 'j' in particular writes
 * a large block of hardcoded initialization data and its reply uses a
 * different framing than the rest of this protocol, not fully
 * reconciled with the main header format (see FIRMWARE_ANALYSIS.md).
 */
void sysex_init(void);

#endif /* SYSEX_H */
