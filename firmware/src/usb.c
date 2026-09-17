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
 * Status: EP0 control transfers (GET_DESCRIPTOR, SET_ADDRESS,
 * SET_CONFIGURATION) and EP1 IN (MIDI send) are implemented. EP1 OUT
 * (incoming MIDI) is enabled but not yet wired to anything -- TODO.
 * Not yet tested against real hardware.
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

static void ep0_send(const uint8_t *data, uint16_t len, uint16_t requested_len)
{
	uint16_t n = len < requested_len ? len : requested_len;
	if (n > EP0_MAX_PACKET) {
		n = EP0_MAX_PACKET; /* TODO: multi-packet control transfers
		                     * for descriptors > 16 bytes (config
		                     * descriptor is 101 bytes) -- not yet
		                     * implemented; needs IN-token-driven
		                     * continuation, tracked against
		                     * usb_poll()'s CTR_TX handling. */
	}
	pma_write(EP0_TX_OFFSET, data, n);
	*pma(BTABLE_OFFSET + 1) = n;
	ep_set_stat_tx(0, USB_EP_STAT_VALID);
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
			}
		} else if (ep == 1) {
			uint32_t r = USB->EPR[1];
			if (r & (1u << 15)) { /* CTR_RX: incoming MIDI on EP1 OUT */
				USB->EPR[1] = r & 0x078Fu & ~(1u << 15);
				/* TODO: read pma(EP1_RX_OFFSET) and hand off
				 * received bytes somewhere -- not yet wired
				 * up (see usb.h TODO). */
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
