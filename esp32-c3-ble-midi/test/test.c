#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

int log_warn_count;
int64_t fake_us;
int delay_calls;

/* main.c taps completed SysEx for the editor and asks whether the portal
 * is up when painting the LED. Neither is under test here -- the editor
 * has its own suite -- so these stand in. */
bool sysex_bridge_offer(const uint8_t *message, size_t len) { (void)message; (void)len; return false; }
void editor_init(void) {}
void editor_toggle(void) {}
bool editor_active(void) { return false; }

/* ---- capture of what the bridge decodes back onto the UART ---- */
static uint8_t uart_out[65536];
static size_t uart_out_len;
void uart_write_bytes_capture(const uint8_t *d, size_t n)
{
	memcpy(uart_out + uart_out_len, d, n);
	uart_out_len += n;
}

/* ---- capture of BLE notifications ---- */
#define MAX_PKTS 512
static uint8_t pkts[MAX_PKTS][600];
static uint16_t pkt_len[MAX_PKTS];
static int pkt_count;

static uint16_t test_mtu = 256;
static int notify_fail_every;   /* fail every Nth notify with ENOMEM */
static int notify_attempts;

#include "main.c"

uint16_t ble_att_mtu(uint16_t conn) { (void)conn; return test_mtu; }
struct ble_hs_cfg_t ble_hs_cfg;
void ble_store_config_init(void) {}

static struct os_mbuf mbuf_pool[8];
static uint8_t mbuf_data[8][600];
static int mbuf_next;

struct os_mbuf *ble_hs_mbuf_from_flat(const void *buf, uint16_t len)
{
	struct os_mbuf *om = &mbuf_pool[mbuf_next++ % 8];
	memcpy(mbuf_data[mbuf_next % 8], buf, len);
	om->data = mbuf_data[mbuf_next % 8];
	om->len = len;
	return om;
}

int ble_hs_mbuf_to_flat(const struct os_mbuf *om, void *buf, uint16_t len, uint16_t *out)
{
	uint16_t n = om->len < len ? om->len : len;
	memcpy(buf, om->data, n);
	*out = n;
	return 0;
}

int ble_gatts_notify_custom(uint16_t conn, uint16_t handle, struct os_mbuf *om)
{
	(void)conn; (void)handle;
	notify_attempts++;
	if (notify_fail_every && (notify_attempts % notify_fail_every) == 0)
		return BLE_HS_ENOMEM;
	if (pkt_count >= MAX_PKTS) { printf("PKT OVERFLOW\n"); exit(1); }
	memcpy(pkts[pkt_count], om->data, om->len);
	pkt_len[pkt_count++] = om->len;
	return 0;
}

/* ---- harness ---- */
static void reset_all(void)
{
	memset(&uart_parser, 0, sizeof(uart_parser));
	out_head = out_tail = 0;
	pkt_count = 0;
	uart_out_len = 0;
	notify_attempts = 0;
	log_warn_count = 0;
	connection_handle = 0;
	midi_value_handle = 1;
	notifications_enabled = true;
	fake_us = 0;
}

static int failures;

static void check(const char *name, const uint8_t *in, size_t in_len,
                  const uint8_t *expect, size_t expect_len)
{
	for (size_t i = 0; i < in_len; i++) {
		uart_parser_byte(&uart_parser, in[i]);
		fake_us += 120; /* ~1 byte at 31250 baud; walks the timestamp along */
	}
	ble_midi_drain();

	/* Decode every notification back to raw MIDI. */
	for (int p = 0; p < pkt_count; p++)
		if (!ble_midi_write_to_uart(pkts[p], pkt_len[p])) {
			printf("FAIL %-28s packet %d rejected by decoder\n", name, p);
			failures++;
			return;
		}

	if (uart_out_len != expect_len || memcmp(uart_out, expect, expect_len) != 0) {
		printf("FAIL %-28s round trip differs\n", name);
		printf("  expected (%zu):", expect_len);
		for (size_t i = 0; i < expect_len; i++) printf(" %02x", expect[i]);
		printf("\n  got      (%zu):", uart_out_len);
		for (size_t i = 0; i < uart_out_len && i < 80; i++) printf(" %02x", uart_out[i]);
		printf("\n");
		failures++;
		return;
	}
	printf("ok   %-28s %zu bytes in %d packet(s)\n", name, expect_len, pkt_count);
}

int main(void)
{
	/* 1. Plain channel messages, all distinct statuses. */
	reset_all();
	{
		uint8_t in[] = {0x90,0x3c,0x64, 0x80,0x3c,0x00, 0xb0,0x07,0x7f,
		                0xc0,0x05, 0xe0,0x00,0x40, 0xa0,0x3c,0x10};
		check("channel messages", in, sizeof in, in, sizeof in);
	}

	/* 2. Running status must be expanded on the BLE side. */
	reset_all();
	{
		uint8_t in[]  = {0x90,0x3c,0x64, 0x3e,0x64, 0x40,0x64};
		uint8_t exp[] = {0x90,0x3c,0x64, 0x90,0x3e,0x64, 0x90,0x40,0x64};
		check("running status expanded", in, sizeof in, exp, sizeof exp);
	}

	/* 3. Realtime interleaved between data bytes of a channel message. */
	reset_all();
	{
		uint8_t in[]  = {0x90,0xf8,0x3c,0x64};
		uint8_t exp[] = {0xf8, 0x90,0x3c,0x64};   /* realtime emitted first */
		check("realtime inside message", in, sizeof in, exp, sizeof exp);
	}

	/* 4. Short SysEx, inline in one packet. */
	reset_all();
	{
		uint8_t in[] = {0xf0,0x47,0x00,0x26,0x66,0x01,0xf7};
		check("short sysex inline", in, sizeof in, in, sizeof in);
	}

	/* 5. SysEx mixed with notes, ordering preserved. */
	reset_all();
	{
		uint8_t in[] = {0x90,0x3c,0x64, 0xf0,0x47,0x01,0xf7, 0x80,0x3c,0x00};
		check("sysex between notes", in, sizeof in, in, sizeof in);
	}

	/* 6. Large SysEx that must fragment across packets (small MTU). */
	reset_all();
	test_mtu = 23;
	{
		uint8_t in[160];
		in[0] = 0xf0;
		for (int i = 1; i < 159; i++) in[i] = (uint8_t)(i & 0x7f);
		in[159] = 0xf7;
		check("sysex fragmented, mtu 23", in, sizeof in, in, sizeof in);
	}
	test_mtu = 256;

	/* 7. Large SysEx at full MTU still fragments (253 > capacity for 256B). */
	reset_all();
	{
		uint8_t in[256];
		in[0] = 0xf0;
		for (int i = 1; i < 255; i++) in[i] = (uint8_t)(i & 0x7f);
		in[255] = 0xf7;
		check("sysex 256 bytes", in, sizeof in, in, sizeof in);
	}

	/* 8. A burst big enough to prove aggregation actually packs. */
	reset_all();
	{
		static uint8_t in[300];
		size_t n = 0;
		for (int i = 0; i < 100; i++) {
			in[n++] = 0xb0; in[n++] = 0x14; in[n++] = (uint8_t)(i & 0x7f);
		}
		check("100 CC burst", in, n, in, n);
		if (pkt_count >= 100) {
			printf("FAIL %-28s no aggregation (%d packets)\n", "100 CC burst", pkt_count);
			failures++;
		} else {
			printf("     aggregation: 100 messages in %d packet(s)\n", pkt_count);
		}
	}

	/* 9. Backpressure: every 3rd notify returns ENOMEM; nothing may be lost. */
	reset_all();
	notify_fail_every = 3;
	test_mtu = 23;
	{
		static uint8_t in[300];
		size_t n = 0;
		for (int i = 0; i < 60; i++) {
			in[n++] = 0x90; in[n++] = (uint8_t)(0x30 + (i & 0x0f)); in[n++] = 0x64;
		}
		check("retry under ENOMEM", in, n, in, n);
	}
	notify_fail_every = 0;
	test_mtu = 256;

	/* 10. Timestamp rollover mid-burst must split packets, not corrupt them. */
	reset_all();
	{
		uint8_t in[]  = {0x90,0x3c,0x64};
		uint8_t exp[] = {0x90,0x3c,0x64};
		fake_us = 0x1fff * 1000 - 500;   /* just before the 13-bit wrap */
		check("timestamp wrap", in, sizeof in, exp, sizeof exp);
	}

	/* 11. Queue must be discarded when no host is subscribed. */
	reset_all();
	notifications_enabled = false;
	{
		uint8_t in[] = {0x90,0x3c,0x64};
		for (size_t i = 0; i < sizeof in; i++) uart_parser_byte(&uart_parser, in[i]);
		ble_midi_drain();
		if (pkt_count != 0 || out_used() != 0) {
			printf("FAIL %-28s queued while unsubscribed\n", "drop when unsubscribed");
			failures++;
		} else {
			printf("ok   %-28s nothing queued, nothing sent\n", "drop when unsubscribed");
		}
	}

	/* 12. Timestamp-high change mid-burst must start a new packet. */
	reset_all();
	{
		uint8_t in[30];
		size_t n = 0;
		for (int i = 0; i < 10; i++) {
			in[n++] = 0x90; in[n++] = (uint8_t)(0x30 + i); in[n++] = 0x64;
		}
		/* 20 ms per byte walks the 6-bit timestamp-high field several times. */
		for (size_t i = 0; i < n; i++) {
			uart_parser_byte(&uart_parser, in[i]);
			fake_us += 20000;
		}
		ble_midi_drain();
		for (int p = 0; p < pkt_count; p++)
			if (!ble_midi_write_to_uart(pkts[p], pkt_len[p])) {
				printf("FAIL %-28s packet %d rejected\n", "timestamp-high split", p);
				failures++;
			}
		bool split = pkt_count > 1;
		bool intact = uart_out_len == n && memcmp(uart_out, in, n) == 0;
		if (!split || !intact) {
			printf("FAIL %-28s split=%d intact=%d packets=%d\n",
			       "timestamp-high split", split, intact, pkt_count);
			failures++;
		} else {
			printf("ok   %-28s %d packets, stream intact\n",
			       "timestamp-high split", pkt_count);
		}
	}

	/* 13. SysEx longer than the parser's buffer is dropped, not overflowed. */
	reset_all();
	{
		uart_parser_byte(&uart_parser, 0xf0);
		for (int i = 0; i < 1000; i++) uart_parser_byte(&uart_parser, 0x01);
		uart_parser_byte(&uart_parser, 0xf7);
		/* A following note must still parse correctly. */
		uart_parser_byte(&uart_parser, 0x90);
		uart_parser_byte(&uart_parser, 0x3c);
		uart_parser_byte(&uart_parser, 0x64);
		ble_midi_drain();
		for (int p = 0; p < pkt_count; p++) ble_midi_write_to_uart(pkts[p], pkt_len[p]);
		uint8_t exp[] = {0x90, 0x3c, 0x64};
		if (uart_out_len != 3 || memcmp(uart_out, exp, 3) != 0) {
			printf("FAIL %-28s recovered %zu bytes\n", "overlong sysex dropped", uart_out_len);
			failures++;
		} else {
			printf("ok   %-28s dropped, parser resynced\n", "overlong sysex dropped");
		}
	}

	/* 14. Malformed inbound BLE packets must be rejected, never decoded. */
	{
		uint8_t bad1[] = {0x00, 0x80, 0x90};        /* no header status bit */
		uint8_t bad2[] = {0x80};                    /* header only */
		uint8_t bad3[] = {0x80, 0x80};              /* timestamp, no status */
		uint8_t bad4[] = {0x80, 0x80, 0x90, 0x3c};  /* truncated note on */
		int rejected = 0;
		rejected += !ble_midi_write_to_uart(bad1, sizeof bad1);
		rejected += !ble_midi_write_to_uart(bad2, sizeof bad2);
		rejected += !ble_midi_write_to_uart(bad3, sizeof bad3);
		rejected += !ble_midi_write_to_uart(bad4, sizeof bad4);
		if (rejected != 4) {
			printf("FAIL %-28s only %d/4 rejected\n", "malformed input rejected", rejected);
			failures++;
		} else {
			printf("ok   %-28s 4/4 rejected\n", "malformed input rejected");
		}
	}


	printf("\n%s\n", failures ? "FAILURES PRESENT" : "all round-trip tests passed");
	return failures != 0;
}
