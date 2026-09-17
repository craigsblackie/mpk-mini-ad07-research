/*
 * Linker script for STM32F102R8T6.
 *
 * Flash size confirmed as 128 KiB from the extracted firmware dump
 * (FULL_DUMP_PICO.md / FIRMWARE_ANALYSIS.md) -- larger than the 64 KiB
 * the "R8" marking nominally implies.
 *
 * SRAM size NOT independently confirmed -- using the datasheet-nominal
 * 20 KiB for medium-density STM32F1 parts. TODO: verify against real
 * hardware (e.g. read the flash-size/mem-size system registers, or a
 * SWD memory probe) before relying on this for anything that pushes
 * close to the limit.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
  RAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 20K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .data : AT (_etext)
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM
}
