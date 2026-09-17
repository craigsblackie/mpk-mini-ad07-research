/*
 * Minimal STM32F1 USB full-speed device stack.
 *
 * Written from the STM32F1 USB peripheral's publicly documented
 * behavior (ST reference manual RM0008, "USB full-speed device
 * interface" chapter) -- this is generic STM32F1 hardware behavior,
 * the same for any project using this peripheral, not derived from
 * the original AKAI firmware's disassembly. Enough to enumerate as
 * the USB-MIDI device described in usb_descriptors.c and move bulk
 * data on EP1.
 *
 * Status: EP0 control transfers (GET_DESCRIPTOR with multi-packet IN
 * support, SET_ADDRESS, SET_CONFIGURATION), EP1 IN (MIDI send), and
 * EP1 OUT (incoming MIDI, dispatched via the weak usb_midi_on_receive
 * hook) are implemented. Not yet tested against real hardware.
 */
#include "usb.h"
#include "stm32f102.h"

extern const uint8_t usb_device_descriptor[18];
extern const uint8_t usb_config_descriptor[101];
extern const uint8_t usb_string_langid[4];
extern const uint8_t usb_string_manufacturer[];
extern const uint8_t usb_string_product[];

/* Buffer descriptor table layout (in USB PMA, 16-bit words packed
 * into 32-bit slots -- standard STM32F1 PMA addressing). BTABLE at
 * PMA offset 0; 4 entries (ADDR_TX, COUNT_TX, ADDR_RX, COUNT_RX) per
 * endpoint, 8 bytes each in the CPU's view (each occupying a 32-bit
 * slot despite being a 16-bit value -- PMA quirk). */
#define BTABLE_OFFSET 0x00u
#define EP0_TX_OFFSET 0x40u
#define EP0_RX_OFFSET 0x80u
#define EP1_TX_OFFSET 0xC0u
#define EP1_RX_OFFSET 0x100u
#define EP0_MAX_PACKET 16u
#define EP1_MAX_PACKET 64u

/* Each PMA "byte address" actually addresses a 16-bit-wide word at a
 * 32-bit-aligned slot: real offset = pma_addr * 2. */
static inline __IO uint16_t *pma(uint32_t offset)
{
	return (__IO uint16_t *)(USB_PMA_BASE + offset * 2u);
}

static void pma_write(uint32_t pma_offset, const uint8_t *src, uint16_t len)
{
	__IO uint16_t *p = pma(pma_offset);
	for (uint16_t i = 0; i < len; i += 2) {
		uint16_t lo = src[i];
		uint16_t hi = (i + 1 < len) ? src[i + 1] : 0;
		*p = lo | (hi << 8);
		p += 1; /* next 16-bit PMA slot */
	}
}

static void pma_read(uint32_t pma_offset, uint8_t *dst, uint16_t len)
{
	__IO uint16_t *p = pma(pma_offset);
	for (uint16_t i = 0; i < len; i += 2) {
		uint16_t v = *p;
		dst[i] = (uint8_t)v;
		if (i + 1 < len) {
			dst[i + 1] = (uint8_t)(v >> 8);
		}
		p += 1;
	}
}

static void btable_set(uint8_t ep, uint32_t tx_addr, uint16_t tx_count,
                        uint32_t rx_addr, uint16_t rx_count_reg)
{
	uint32_t base = BTABLE_OFFSET + ep * 8u;
	*pma(base + 0) = (uint16_t)tx_addr;
	*pma(base + 1) = tx_count;
	*pma(base + 2) = (uint16_t)rx_addr;
	*pma(base + 3) = rx_count_reg;
}

/* COUNT_RX register encodes buffer size as blocks, not a raw byte
 * count -- for a 64-byte single-buffer this is BL_SIZE=1 (32-byte
 * blocks), NUM_BLOCK=1 (2*32=64). */
#define RX_COUNT_64 ((1u << 15) | (1u << 10))
#define RX_COUNT_16 ((0u << 15) | (2u << 10)) /* 2 * 8 = 16 */

static void ep_set_stat_tx(uint8_t ep, uint32_t stat)
{
	uint32_t r = USB->EPR[ep];
	uint32_t toggle = (r ^ stat) & 0x0030u; /* STAT_TX is bits [5:4] */
	USB->EPR[ep] = (r & 0x870Fu) | 0x8000u | toggle;
	/* preserve CTR_RX/CTR_TX(write-0-to-clear, so keep as 1),
	 * EP_TYPE, EA; toggle only STAT_TX via the write-1-to-toggle bits */
}

static void ep_set_stat_rx(uint8_t ep, uint32_t stat)
{
	uint32_t r = USB->EPR[ep];
	uint32_t toggle = (r ^ (stat << 12)) & 0x3000u; /* STAT_RX is bits [13:12] */
	USB->EPR[ep] = (r & 0x078Fu) | 0x0080u | toggle;
}

static volatile uint8_t usb_address_pending;
static volatile uint8_t usb_address_value;
static volatile uint8_t usb_configured;

/* Multi-packet EP0 IN transfer state -- a control transfer whose data
 * stage exceeds EP0_MAX_PACKET (16 bytes) needs one call to
 * ep0_send_chunk() per IN token; ctrl_tx_remaining tracks what's left
 * to send after each one. ctrl_tx_need_zlp handles the case where the
 * total length is an exact multiple of the max packet size (or zero),
 * which per the USB spec requires a final zero-length packet so the
 * host knows the transfer is complete rather than expecting more. */
static const uint8_t *ctrl_tx_ptr;
static uint16_t ctrl_tx_remaining;
static uint8_t ctrl_tx_need_zlp;

void usb_init(void)
{
	RCC->APB1ENR |= RCC_APB1ENR_USBEN;

	USB->CNTR = USB_CNTR_FRES;
	for (volatile int i = 0; i < 100; i++) {
	}
	USB->CNTR = 0;
	USB->ISTR = 0;

	USB->BTABLE = BTABLE_OFFSET;
	btable_set(0, EP0_TX_OFFSET, 0, EP0_RX_OFFSET, RX_COUNT_16);
	btable_set(1, EP1_TX_OFFSET, 0, EP1_RX_OFFSET, RX_COUNT_64);

	/* EP0: control */
	USB->EPR[0] = 0x0200u; /* EP_TYPE = CONTROL, EA = 0 */
	ep_set_stat_rx(0, USB_EP_STAT_VALID);
	ep_set_stat_tx(0, USB_EP_STAT_NAK);

	/* EP1: bulk, address 1, both directions */
	USB->EPR[1] = 0x0001u; /* EP_TYPE = BULK, EA = 1 */
	ep_set_stat_rx(1, USB_EP_STAT_VALID);
	ep_set_stat_tx(1, USB_EP_STAT_NAK);

	USB->DADDR = 0x80u; /* EF=1, ADDR=0 */
	usb_address_pending = 0;
	usb_configured = 0;
}

/* Sends the next EP0_MAX_PACKET-sized (or smaller, for the final
 * chunk) piece of the pending control-transfer data, per
 * ctrl_tx_ptr/ctrl_tx_remaining. Called both for the first packet
 * (from ep0_send) and for each subsequent one (from usb_poll's CTR_TX
 * handler). */
static void ep0_send_chunk(void)
{
	uint16_t n = ctrl_tx_remaining < EP0_MAX_PACKET ? ctrl_tx_remaining : EP0_MAX_PACKET;

	pma_write(EP0_TX_OFFSET, ctrl_tx_ptr, n);
	*pma(BTABLE_OFFSET + 1) = n;

	ctrl_tx_ptr += n;
	ctrl_tx_remaining -= n;
	if (n == EP0_MAX_PACKET && ctrl_tx_remaining == 0) {
		/* exact multiple of the max packet size -- one more
		 * (zero-length) packet is needed so the host doesn't wait
		 * for a short packet that never comes. */
		ctrl_tx_need_zlp = 1;
	} else {
		ctrl_tx_need_zlp = 0;
	}

	ep_set_stat_tx(0, USB_EP_STAT_VALID);
}

static void ep0_send(const uint8_t *data, uint16_t len, uint16_t requested_len)
{
	ctrl_tx_ptr = data;
	ctrl_tx_remaining = len < requested_len ? len : requested_len;
	ep0_send_chunk();
}

static void handle_setup(void)
{
	uint8_t setup[8];
	pma_read(EP0_RX_OFFSET, setup, 8);

	uint8_t bmRequestType = setup[0];
	uint8_t bRequest = setup[1];
	uint16_t wValue = setup[2] | (setup[3] << 8);
	uint16_t wLength = setup[6] | (setup[7] << 8);

	if (bmRequestType == 0x80 && bRequest == 0x06) { /* GET_DESCRIPTOR, device-to-host */
		uint8_t type = (uint8_t)(wValue >> 8);
		uint8_t index = (uint8_t)wValue;
		switch (type) {
		case 0x01: /* DEVICE */
			ep0_send(usb_device_descriptor, sizeof usb_device_descriptor, wLength);
			return;
		case 0x02: /* CONFIGURATION */
			ep0_send(usb_config_descriptor, sizeof usb_config_descriptor, wLength);
			return;
		case 0x03: /* STRING */
			if (index == 0) {
				ep0_send(usb_string_langid, sizeof usb_string_langid, wLength);
			} else if (index == 1) {
				ep0_send(usb_string_manufacturer, usb_string_manufacturer[0], wLength);
			} else if (index == 2) {
				ep0_send(usb_string_product, usb_string_product[0], wLength);
			} else {
				ep_set_stat_tx(0, USB_EP_STAT_STALL);
			}
			return;
		default:
			ep_set_stat_tx(0, USB_EP_STAT_STALL);
			return;
		}
	} else if (bmRequestType == 0x00 && bRequest == 0x05) { /* SET_ADDRESS */
		usb_address_pending = 1;
		usb_address_value = (uint8_t)wValue;
		*pma(BTABLE_OFFSET + 1) = 0;
		ep_set_stat_tx(0, USB_EP_STAT_VALID); /* zero-length status */
		return;
	} else if (bmRequestType == 0x00 && bRequest == 0x09) { /* SET_CONFIGURATION */
		usb_configured = 1;
		*pma(BTABLE_OFFSET + 1) = 0;
		ep_set_stat_tx(0, USB_EP_STAT_VALID);
		return;
	}

	/* Unhandled request -- TODO: this currently covers only the
	 * minimum set needed for enumeration + MIDI class descriptor
	 * requests may be needed too (GET_STATUS etc. not handled). */
	ep_set_stat_tx(0, USB_EP_STAT_STALL);
}

void usb_poll(void)
{
	uint32_t istr = USB->ISTR;

	if (istr & USB_ISTR_RESET) {
		USB->ISTR = ~USB_ISTR_RESET;
		usb_init();
		return;
	}

	if (istr & USB_ISTR_CTR) {
		uint8_t ep = (uint8_t)(istr & USB_ISTR_EP_ID);
		if (ep == 0) {
			uint32_t r = USB->EPR[0];
			if (r & (1u << 15)) { /* CTR_RX */
				USB->EPR[0] = r & 0x078Fu & ~(1u << 15);
				if (r & (1u << 11)) { /* SETUP bit */
					handle_setup();
				}
				ep_set_stat_rx(0, USB_EP_STAT_VALID);
			}
			if (r & (1u << 7)) { /* CTR_TX */
				USB->EPR[0] = USB->EPR[0] & 0x078Fu & ~(1u << 7);
				if (usb_address_pending) {
					USB->DADDR = 0x80u | usb_address_value;
					usb_address_pending = 0;
				}
				if (ctrl_tx_remaining > 0 || ctrl_tx_need_zlp) {
					ep0_send_chunk();
				}
			}
		} else if (ep == 1) {
			uint32_t r = USB->EPR[1];
			if (r & (1u << 15)) { /* CTR_RX: incoming MIDI on EP1 OUT */
				USB->EPR[1] = r & 0x078Fu & ~(1u << 15);

				/* COUNT_RX's low 10 bits are the actual
				 * received byte count; upper bits are the
				 * fixed buffer-size config (RX_COUNT_64) and
				 * must be masked off here. */
				uint16_t count = *pma(BTABLE_OFFSET + 8 + 3) & 0x03FFu;
				if (count > 0 && count <= EP1_MAX_PACKET) {
					uint8_t rx_buf[EP1_MAX_PACKET];
					pma_read(EP1_RX_OFFSET, rx_buf, count);
					usb_midi_on_receive(rx_buf, count);
				}

				ep_set_stat_rx(1, USB_EP_STAT_VALID);
			}
			if (r & (1u << 7)) { /* CTR_TX: previous MIDI send completed */
				USB->EPR[1] = USB->EPR[1] & 0x078Fu & ~(1u << 7);
			}
		}
	}
}

void usb_midi_send(const uint8_t *data, size_t len)
{
	if (!usb_configured) {
		return;
	}
	if ((USB->EPR[1] & 0x0030u) >> 4 == USB_EP_STAT_VALID) {
		return; /* previous transfer still in flight */
	}
	if (len > EP1_MAX_PACKET) {
		len = EP1_MAX_PACKET;
	}
	pma_write(EP1_TX_OFFSET, data, (uint16_t)len);
	*pma(BTABLE_OFFSET + 8 + 1) = (uint16_t)len; /* EP1's COUNT_TX slot */
	ep_set_stat_tx(1, USB_EP_STAT_VALID);
}

__attribute__((weak)) void usb_midi_on_receive(const uint8_t *data, size_t len)
{
	(void)data;
	(void)len;
	/* Default: discard. Override elsewhere to do something with
	 * incoming MIDI (see usb.h). */
}
