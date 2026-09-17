#ifndef BOOTLOADER_H
#define BOOTLOADER_H

/*
 * Firmware-update (DFU) entry: hold a button while powering on to jump
 * into ST's system memory bootloader instead of running the app.
 *
 * Reimplements the shape of the original firmware's FUN_08001cf0,
 * confirmed to run at the very start of reset (called from
 * FUN_08001cd6, itself reached directly from the reset-time code at
 * flash 0x0800013a, before any of the application's own peripheral
 * init) -- see FIRMWARE_ANALYSIS.md's bootloader-entry section. Call
 * this from Reset_Handler, before main(), to match.
 *
 * NOT byte-exact: the original debounces column 7's row read for ~100
 * consecutive stable samples and compares against a specific nonzero
 * "idle" sentinel value (0x08) whose exact meaning (a particular pull
 * configuration quirk on that column's rows, most likely) wasn't fully
 * resolved. This reimplementation drives column 7 with clean pull-ups
 * of its own and treats any nonzero (any button in that column
 * pressed) as the trigger, with its own debounce loop -- a clean
 * reimplementation of the same feature, not a port of the exact
 * comparison. The system-memory jump itself (0x1FFFF000, standard for
 * STM32F101/F102/F103 medium-density parts per ST's AN2606) is a
 * documented, standard procedure, not reverse-engineered.
 */
void bootloader_check_entry(void);

#endif /* BOOTLOADER_H */
