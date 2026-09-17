/*
 * AKAI editor-software SysEx protocol -- receive/send support.
 *
 * Message framing, read directly off the original's FUN_08002eac
 * (FIRMWARE_ANALYSIS.md's "Follow-up pass: full command set and wire
 * encoding" section):
 *
 *   F0 47 <id> 7C <cmd> <len_hi> <len_lo> <program#> <payload...> F7
 *
 * For 'a' (write program) and 'c' (read/dump program), payload is
 * exactly 101 bytes -- the per-program record, reordered on the wire
 * per program.c's WIRE_TO_RECORD/RECORD_TO_WIRE tables (a pure byte
 * reorder, not a bit-packing scheme -- see program.h). Total message
 * length for those two commands is 110 bytes (8-byte header + 101-byte
 * payload + trailing F7).
 *
 * USB-MIDI carries SysEx as a sequence of 4-byte events with CIN
 * 0x4 (starts/continues, 3 data bytes), 0x5/0x6/0x7 (ends with
 * 1/2/3 data bytes, the last of which is the 0xF7 terminator) --
 * reassembled here into sysex_buf[] before dispatching.
 */
#include "sysex.h"
#include "usb.h"
#include "midi_ring.h"
#include "program.h"

#define SYSEX_MAX_LEN 128
#define MSG_TOTAL_LEN 110 /* 'a'/'c': 8-byte header + 101-byte payload + F7 */
#define HEADER_LEN 8

static uint8_t sysex_buf[SYSEX_MAX_LEN];
static uint16_t sysex_len;
static uint8_t last_id;

static void sysex_reset(void)
{
	sysex_len = 0;
}

static void sysex_append(uint8_t b)
{
	if (sysex_len >= SYSEX_MAX_LEN) {
		sysex_reset(); /* overflow -- drop whatever was accumulating */
		return;
	}
	sysex_buf[sysex_len++] = b;
}

static void send_dump(uint8_t program_index)
{
	uint8_t msg[MSG_TOTAL_LEN];
	msg[0] = 0xF0;
	msg[1] = 0x47;
	msg[2] = last_id;
	msg[3] = 0x7C;
	msg[4] = 'c';
	msg[5] = 0x00;
	msg[6] = MSG_TOTAL_LEN;
	msg[7] = program_index;

	uint8_t wire[PROGRAM_RECORD_SIZE];
	program_save_to_wire(program_index, wire);
	for (int i = 0; i < PROGRAM_RECORD_SIZE; i++) {
		msg[HEADER_LEN + i] = wire[i];
	}
	msg[MSG_TOTAL_LEN - 1] = 0xF7;

	int pos = 0;
	while (pos < MSG_TOTAL_LEN) {
		int remaining = MSG_TOTAL_LEN - pos;
		uint8_t event[4] = {0, 0, 0, 0};
		if (remaining > 3) {
			event[0] = 0x04;
			event[1] = msg[pos];
			event[2] = msg[pos + 1];
			event[3] = msg[pos + 2];
			pos += 3;
		} else if (remaining == 3) {
			event[0] = 0x07;
			event[1] = msg[pos];
			event[2] = msg[pos + 1];
			event[3] = msg[pos + 2];
			pos += 3;
		} else if (remaining == 2) {
			event[0] = 0x06;
			event[1] = msg[pos];
			event[2] = msg[pos + 1];
			pos += 2;
		} else {
			event[0] = 0x05;
			event[1] = msg[pos];
			pos += 1;
		}
		midi_ring_push(event, 4);
	}
}

static void sysex_process(void)
{
	if (sysex_len < HEADER_LEN + 1) {
		return;
	}
	if (sysex_buf[0] != 0xF0 || sysex_buf[1] != 0x47 || sysex_buf[3] != 0x7C) {
		return;
	}

	last_id = sysex_buf[2];
	uint8_t cmd = sysex_buf[4];
	uint8_t program_index = sysex_buf[7];

	if (program_index >= PROGRAM_COUNT) {
		return;
	}

	switch (cmd) {
	case 'b': /* select program -- no payload */
		current_program = program_index;
		break;
	case 'a': /* write program -- 101-byte payload follows the header */
		if (sysex_len == MSG_TOTAL_LEN) {
			program_load_from_wire(program_index, &sysex_buf[HEADER_LEN]);
			current_program = program_index;
		}
		break;
	case 'c': /* read/dump program -- reply with the same format 'a' sends */
		send_dump(program_index);
		break;
	default:
		/* '`', 'd', 'j', and the '~'-prefixed sub-protocol are not
		 * implemented -- see sysex.h's header comment. */
		break;
	}
}

static void handle_event(const uint8_t event[4])
{
	uint8_t cin = (uint8_t)(event[0] & 0x0F);

	switch (cin) {
	case 0x4:
		sysex_append(event[1]);
		sysex_append(event[2]);
		sysex_append(event[3]);
		break;
	case 0x5:
		sysex_append(event[1]);
		sysex_process();
		sysex_reset();
		break;
	case 0x6:
		sysex_append(event[1]);
		sysex_append(event[2]);
		sysex_process();
		sysex_reset();
		break;
	case 0x7:
		sysex_append(event[1]);
		sysex_append(event[2]);
		sysex_append(event[3]);
		sysex_process();
		sysex_reset();
		break;
	default:
		break; /* not a SysEx CIN -- not this module's concern */
	}
}

void usb_midi_on_receive(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i + 4 <= len; i += 4) {
		handle_event(&data[i]);
	}
}

void sysex_init(void)
{
	sysex_reset();
	last_id = 0;
}
