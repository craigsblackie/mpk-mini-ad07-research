ENTRY(_start)
SECTIONS
{
  . = 0x20000400;
  .text : { *(.text*) }
}
