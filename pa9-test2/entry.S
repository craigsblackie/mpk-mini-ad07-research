.syntax unified
.thumb
.section .text
.thumb_func
_start:
	cpsid i         @ mask all maskable interrupts first, before anything else
	bl test_main
loop:
	b loop
