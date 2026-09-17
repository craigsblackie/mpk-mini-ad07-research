#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stddef.h>

void usb_init(void);
void usb_poll(void);

/* Send one USB-MIDI packet (up to 64 bytes, already formatted as
 * 4-byte USB-MIDI events) out endpoint 0x81 IN. This is the low-level
 * transmit call -- see midi_ring.h for the buffer that feeds it, and
 * FIRMWARE_ANALYSIS.md's "complete USB MIDI TX pipeline" section for
 * how the original firmware structures this same handoff. */
void usb_midi_send(const uint8_t *data, size_t len);

#endif /* USB_H */
