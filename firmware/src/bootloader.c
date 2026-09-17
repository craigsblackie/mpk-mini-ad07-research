/*
 * DFU/system-bootloader entry -- see bootloader.h.
 */
#include "bootloader.h"
#include "stm32f102.h"

#define COLUMN7_PIN_MASK 0x4000u /* PC14 -- matches matrix.c's column_mask[7] */
#define SYSTEM_MEMORY_BASE 0x1FFFF000u
#define DEBOUNCE_SAMPLES 100

static void short_delay(volatile int n)
{
	while (n-- > 0) {
	}
}

static uint8_t column7_rows_pressed(void)
{
	/* Select column 7 (active low), all others idle high. */
	GPIOC->ODR = (GPIOC->ODR | 0xFF80u) & ~COLUMN7_PIN_MASK;
	short_delay(200);
	uint8_t rows = (uint8_t)(~GPIOB->IDR >> 8); /* inverted: bit set = pressed */
	GPIOC->ODR |= 0xFF80u;
	return rows;
}

static void jump_to_system_bootloader(void)
{
	uint32_t sp = *(volatile uint32_t *)SYSTEM_MEMORY_BASE;
	uint32_t reset_vector = *(volatile uint32_t *)(SYSTEM_MEMORY_BASE + 4);

	__asm volatile("cpsid i" ::: "memory");
	SysTick->CTRL = 0;

	__asm volatile("msr msp, %0" ::"r"(sp));
	((void (*)(void))reset_vector)();

	while (1) {
		/* Never reached. */
	}
}

void bootloader_check_entry(void)
{
	RCC->APB2ENR |= RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN;

	/* PC14 (column 7): push-pull output, 2 MHz. CRH nibble for pin 14
	 * is bits [27:24]: CNF=00 (push-pull), MODE=10 (output 2 MHz). */
	GPIOC->CRH = (GPIOC->CRH & ~(0xFu << 24)) | (0x2u << 24);
	GPIOC->ODR |= 0xFF80u; /* idle: all columns deselected */

	/* PB8..PB15 (rows): input with pull-up. CNF=10, MODE=00 -> nibble
	 * 0x8 per pin; ODR bit set selects pull-up (vs. pull-down). */
	GPIOB->CRH = (GPIOB->CRH & ~0xFFFFFFFFu) | 0x88888888u;
	GPIOB->ODR |= 0xFF00u;

	uint8_t last = column7_rows_pressed();
	int stable = 0;
	while (stable < DEBOUNCE_SAMPLES) {
		short_delay(200);
		uint8_t now = column7_rows_pressed();
		if (now == last && now != 0) {
			stable++;
		} else {
			stable = 0;
			last = now;
			if (now == 0) {
				return; /* nothing held -- normal boot */
			}
		}
	}

	jump_to_system_bootloader();
}
