/*
 * Minimal STM32F102R8T6 register definitions.
 *
 * Hand-written from the public ST reference manual (RM0008) register
 * map, covering only the peripherals this project currently uses.
 * Not derived from, or copied out of, the original AKAI firmware --
 * these are standard STM32F1 peripheral addresses/bit layouts, the
 * same for any STM32F1 project.
 */
#ifndef STM32F102_H
#define STM32F102_H

#include <stdint.h>

#define __IO volatile

/* ---- Core peripherals ---- */
typedef struct {
	__IO uint32_t CTRL;
	__IO uint32_t LOAD;
	__IO uint32_t VAL;
	__IO uint32_t CALIB;
} SysTick_TypeDef;
#define SysTick ((SysTick_TypeDef *)0xE000E010u)

/* ---- RCC ---- */
typedef struct {
	__IO uint32_t CR;
	__IO uint32_t CFGR;
	__IO uint32_t CIR;
	__IO uint32_t APB2RSTR;
	__IO uint32_t APB1RSTR;
	__IO uint32_t AHBENR;
	__IO uint32_t APB2ENR;
	__IO uint32_t APB1ENR;
} RCC_TypeDef;
#define RCC ((RCC_TypeDef *)0x40021000u)

#define RCC_APB2ENR_AFIOEN  (1u << 0)
#define RCC_APB2ENR_IOPAEN  (1u << 2)
#define RCC_APB2ENR_IOPBEN  (1u << 3)
#define RCC_APB2ENR_IOPCEN  (1u << 4)
#define RCC_APB1ENR_USBEN   (1u << 23)

#define RCC_CR_HSEON    (1u << 16)
#define RCC_CR_HSERDY   (1u << 17)
#define RCC_CR_PLLON    (1u << 24)
#define RCC_CR_PLLRDY   (1u << 25)

#define RCC_CFGR_SW_PLL       0x2u
#define RCC_CFGR_SWS_MASK     0xCu
#define RCC_CFGR_SWS_PLL      0x8u
#define RCC_CFGR_PLLSRC_HSE   (1u << 16)
#define RCC_CFGR_PLLXTPRE_DIV1 (0u << 17)
#define RCC_CFGR_PLLMUL6      (0x4u << 18)
/* PLLMUL field encodes (multiplier - 2); 0x4 = x6, giving
 * 8 MHz HSE * 6 = 48 MHz -- exactly USB full-speed's required clock,
 * with no USB prescaler needed (matches this board's 8 MHz crystal,
 * confirmed from the Y1 marking in the board photos). */

/* ---- FLASH interface (for wait-state config at higher clocks) ---- */
typedef struct {
	__IO uint32_t ACR;
} FLASH_TypeDef;
#define FLASH_IF ((FLASH_TypeDef *)0x40022000u)
#define FLASH_ACR_LATENCY_2 0x2u /* 2 wait states, required for 48-72 MHz per RM0008 */

/* ---- GPIO ---- */
typedef struct {
	__IO uint32_t CRL;
	__IO uint32_t CRH;
	__IO uint32_t IDR;
	__IO uint32_t ODR;
	__IO uint32_t BSRR;
	__IO uint32_t BRR;
	__IO uint32_t LCKR;
} GPIO_TypeDef;
#define GPIOA ((GPIO_TypeDef *)0x40010800u)
#define GPIOB ((GPIO_TypeDef *)0x40010C00u)
#define GPIOC ((GPIO_TypeDef *)0x40011000u)

/* ---- USB device peripheral ---- */
typedef struct {
	__IO uint32_t EPR[8];
	uint32_t RESERVED[8];
	__IO uint32_t CNTR;
	__IO uint32_t ISTR;
	__IO uint32_t FNR;
	__IO uint32_t DADDR;
	__IO uint32_t BTABLE;
} USB_TypeDef;
#define USB ((USB_TypeDef *)0x40005C00u)
#define USB_PMA_BASE 0x40006000u

#define USB_CNTR_FRES   (1u << 0)
#define USB_CNTR_PDWN   (1u << 1)
#define USB_CNTR_LP_MODE (1u << 2)
#define USB_CNTR_FSUSP  (1u << 3)
#define USB_CNTR_RESETM (1u << 10)
#define USB_CNTR_CTRM   (1u << 15)

#define USB_ISTR_EP_ID  0x000Fu
#define USB_ISTR_DIR    (1u << 4)
#define USB_ISTR_RESET  (1u << 10)
#define USB_ISTR_CTR    (1u << 15)

/* EPnR STAT_TX/STAT_RX toggle values (write-1-to-toggle bits, per RM0008) */
#define USB_EP_STAT_DISABLED 0u
#define USB_EP_STAT_STALL    1u
#define USB_EP_STAT_NAK      2u
#define USB_EP_STAT_VALID    3u

#endif /* STM32F102_H */
