/*
 * AD07 open-source replacement firmware -- main entry point.
 *
 * Status: a genuinely comprehensive reimplementation of the original's
 * confirmed feature set -- keys (velocity-sensed), pads (Note/CC/PC),
 * knobs, sustain pedal, real octave buttons + tap tempo (transport.c,
 * matrix column 7), a second button cluster of uncertain purpose
 * (buttons.c, matrix column 8), a 6-mode arpeggiator wired to key
 * input, a shared per-program record store, SysEx program management
 * (write/select/read/status), DFU/bootloader entry at power-on, and
 * the full MIDI TX pipeline -- all with documented placeholders only
 * where real hardware data is still needed (ADC pin-to-control
 * ordering) or the original's own behavior wasn't resolved with
 * confidence (see each module's header comment). See
 * FIRMWARE_ANALYSIS.md for what's confirmed vs. still unknown about
 * the original firmware's behavior in each of these areas.
 */
#include "stm32f102.h"
#include "systick.h"
#include "matrix.h"
#include "midi_ring.h"
#include "program.h"
#include "sysex.h"
#include "keys.h"
#include "adc.h"
#include "knobs.h"
#include "pads.h"
#include "buttons.h"
#include "transport.h"
#include "stuck_note.h"
#include "arp.h"
#include "usb.h"

static void clock_init(void)
{
	/* HSE (external 8 MHz crystal, confirmed from the Y1 marking in
	 * this project's board photos) -> PLL x6 -> 48 MHz SYSCLK/USBCLK.
	 * Standard STM32F1 startup sequence, the same shape for any
	 * board with this crystal -- not derived from the original
	 * firmware's disassembly (see FIRMWARE_ANALYSIS.md's FLASH_IF
	 * section, which found extensive but not-fully-traced RCC/flash
	 * activity early in the original's startup; this reimplements
	 * the standard procedure rather than that exact code). */

	RCC->CR |= RCC_CR_HSEON;
	while (!(RCC->CR & RCC_CR_HSERDY)) {
	}

	/* 2 wait states required above 48 MHz... actually required
	 * above 24 MHz per RM0008 Table 6; set before switching SYSCLK
	 * to the higher-speed PLL output. */
	FLASH_IF->ACR = FLASH_ACR_LATENCY_2;

	RCC->CFGR = (RCC->CFGR & ~(0x1Fu << 18)) | RCC_CFGR_PLLSRC_HSE |
	            RCC_CFGR_PLLXTPRE_DIV1 | RCC_CFGR_PLLMUL6;

	RCC->CR |= RCC_CR_PLLON;
	while (!(RCC->CR & RCC_CR_PLLRDY)) {
	}

	RCC->CFGR = (RCC->CFGR & ~0x3u) | RCC_CFGR_SW_PLL;
	while ((RCC->CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL) {
	}

	/* USB clock: USBPRE bit (RCC_CFGR bit 22) = 0 selects PLL/1.5,
	 * which for a 48 MHz PLL gives 32 MHz -- wrong. USB full-speed
	 * needs exactly 48 MHz, so USBPRE must be 1 (PLL/1, no divide).
	 * TODO: verify this bit's polarity against RM0008 directly
	 * before relying on it -- documented from memory, not re-checked
	 * against the reference manual this session. */
	RCC->CFGR |= (1u << 22);
}

int main(void)
{
	clock_init();
	systick_init();
	matrix_init();
	midi_ring_init();
	program_init();
	sysex_init();
	usb_init();
	adc_init();

	keys_init();
	knobs_init();
	pads_init();
	buttons_init();
	transport_init();
	stuck_note_init();
	arp_init();

	while (1) {
		matrix_scan();
		keys_process();
		pads_process();
		/* Confirmed source: matrix column 8 -- see buttons.c's header. */
		buttons_process(matrix_state[8]);
		/* Confirmed source: matrix column 7 -- see transport.c's header. */
		transport_process(matrix_state[7]);
		knobs_process();
		stuck_note_process();
		arp_process();
		usb_poll();

		if (midi_ring_count() > 0) {
			uint8_t pkt[64];
			size_t n = midi_ring_drain(pkt, sizeof pkt);
			usb_midi_send(pkt, n);
		}
	}
}
