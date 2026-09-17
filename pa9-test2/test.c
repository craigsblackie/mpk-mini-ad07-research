#include <stdint.h>

#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018u)
#define GPIOA_CRH   (*(volatile uint32_t *)0x40010804u)
#define USART1_SR   (*(volatile uint32_t *)0x40013800u)
#define USART1_DR   (*(volatile uint32_t *)0x40013804u)
#define USART1_BRR  (*(volatile uint32_t *)0x40013808u)
#define USART1_CR1  (*(volatile uint32_t *)0x4001380Cu)

#define USARTDIV      0x00000341u /* 9600 baud @ 8MHz */
#define USART_CR1_MSK 0x00002008u /* 8N1, TX enable */
#define USART_SR_TXE  (1 << 7)

static void delay(void)
{
	volatile uint32_t i;
	for (i = 0; i < 800000; i++);
}

void test_main(void)
{
	uint8_t val = 0x00;

	RCC_APB2ENR |= (1 << 2);  /* GPIOA clock */
	RCC_APB2ENR |= (1 << 14); /* USART1 clock */

	GPIOA_CRH &= ~(0xFu << 4);
	GPIOA_CRH |=  (0xBu << 4); /* PA9 alt-func push-pull */

	USART1_BRR = USARTDIV;
	USART1_CR1 = USART_CR1_MSK;

	while (1) {
		while (!(USART1_SR & USART_SR_TXE));
		USART1_DR = val;
		val = (val == 0x00) ? 0xFF : 0x00;
		delay();
	}
}
