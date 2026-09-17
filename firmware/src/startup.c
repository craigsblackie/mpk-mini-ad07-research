/*
 * Reset handler and vector table for STM32F102R8T6.
 *
 * Standard Cortex-M3 startup sequence: copy .data from flash to RAM,
 * zero .bss, call main(). Written fresh against the generic
 * Cortex-M3/STM32F1 startup pattern (the same shape every STM32F1
 * project's startup file has) -- not copied from the original firmware.
 */
#include <stdint.h>
#include "bootloader.h"

extern uint32_t _estack;
extern uint32_t _etext, _sdata, _edata, _sbss, _ebss;

void Reset_Handler(void);
static void Default_Handler(void);
extern int main(void);

void NMI_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void USB_LP_CAN1_RX0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

__attribute__((section(".isr_vector")))
const void *vector_table[] = {
	&_estack,
	Reset_Handler,
	NMI_Handler,
	HardFault_Handler,
	MemManage_Handler,
	BusFault_Handler,
	UsageFault_Handler,
	0, 0, 0, 0,
	SVC_Handler,
	DebugMon_Handler,
	0,
	PendSV_Handler,
	SysTick_Handler,
	/* IRQ0..IRQ19: not yet wired up. USB_LP_CAN1_RX0 is IRQ20 on
	 * STM32F102 (shares the CAN1 vector slot even though this part
	 * has no CAN) -- TODO: confirm this firmware actually uses the
	 * USB interrupt vs. pure polling, per FIRMWARE_ANALYSIS.md's
	 * open question about the main loop's exact timing model. */
	[20] = USB_LP_CAN1_RX0_IRQHandler,
};

static void Default_Handler(void)
{
	while (1) {
	}
}

void Reset_Handler(void)
{
	uint32_t *src, *dst;

	src = &_etext;
	dst = &_sdata;
	while (dst < &_edata) {
		*dst++ = *src++;
	}

	dst = &_sbss;
	while (dst < &_ebss) {
		*dst++ = 0;
	}

	bootloader_check_entry();

	main();

	while (1) {
	}
}
