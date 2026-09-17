/* Host test: curve tables and the flash page image layout. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "velocity.h"

#define PROGRAM_RECORD_SIZE 101
#define PERSIST_RECORDS 4
#define PERSIST_CURRENT_OFFSET (PERSIST_RECORDS * PROGRAM_RECORD_SIZE)
#define PERSIST_VALID_OFFSET (PERSIST_CURRENT_OFFSET + 1)
#define PERSIST_SIZE (PERSIST_VALID_OFFSET + 2)
#define SETTINGS_OFFSET 0x200u
#define SETTINGS_BLOCK_SIZE 16u
#define PAGE_IMAGE_SIZE (SETTINGS_OFFSET + SETTINGS_BLOCK_SIZE)
#define FLASH_PAGE 1024u

static int fails;
static void ok(const char *n, int cond, const char *detail)
{
	if (cond) { printf("ok   %-34s %s\n", n, detail ? detail : ""); }
	else { printf("FAIL %-34s %s\n", n, detail ? detail : ""); fails++; }
}

int main(void)
{
	char buf[120];

	/* --- layout --- */
	ok("page image fits the flash page", PAGE_IMAGE_SIZE <= FLASH_PAGE,
	   (sprintf(buf, "%u of %u bytes", (unsigned)PAGE_IMAGE_SIZE, FLASH_PAGE), buf));
	ok("settings clear of stock area", SETTINGS_OFFSET >= PERSIST_SIZE + 1,
	   (sprintf(buf, "stock ends %u, settings at %u", PERSIST_SIZE + 1, (unsigned)SETTINGS_OFFSET), buf));
	ok("page image is halfword aligned", (PAGE_IMAGE_SIZE % 2) == 0, NULL);

	/* --- every curve is well formed --- */
	for (uint8_t c = 0; c < VELOCITY_CURVE_COUNT; c++) {
		if (c == VELOCITY_FIXED) continue;
		int mono = 1, ranged = 1;
		uint8_t prev = 0;
		for (int v = 1; v <= 127; v++) {
			uint8_t out = velocity_apply(c, (uint8_t)v, 100);
			if (out < 1 || out > 127) ranged = 0;
			if (out < prev) mono = 0;
			prev = out;
		}
		sprintf(buf, "curve %u", c);
		ok(mono ? "curve is monotonic" : "curve is monotonic", mono, buf);
		ok("curve stays in 1..127", ranged, buf);
		ok("curve anchors at 127", velocity_apply(c, 127, 100) == 127, buf);
		ok("curve never mutes a note", velocity_apply(c, 1, 100) >= 1, buf);
	}

	/* --- linear is exactly stock --- */
	int identity = 1;
	for (int v = 1; v <= 127; v++)
		if (velocity_apply(VELOCITY_LINEAR, (uint8_t)v, 100) != v) identity = 0;
	ok("linear is bit-identical to stock", identity, "all 127 inputs pass through");

	/* --- soft lifts, hard lowers, at the midpoint --- */
	ok("soft curve lifts mid velocity", velocity_apply(VELOCITY_SOFT, 64, 100) > 64,
	   (sprintf(buf, "64 -> %u", velocity_apply(VELOCITY_SOFT, 64, 100)), buf));
	ok("hard curve lowers mid velocity", velocity_apply(VELOCITY_HARD, 64, 100) < 64,
	   (sprintf(buf, "64 -> %u", velocity_apply(VELOCITY_HARD, 64, 100)), buf));
	ok("very hard is below hard", velocity_apply(VELOCITY_VERY_HARD, 64, 100) <
	                              velocity_apply(VELOCITY_HARD, 64, 100), NULL);
	ok("soft is above medium soft", velocity_apply(VELOCITY_SOFT, 32, 100) >
	                                velocity_apply(VELOCITY_MEDIUM_SOFT, 32, 100), NULL);

	/* --- fixed ignores input, clamps --- */
	int fixed_ok = 1;
	for (int v = 1; v <= 127; v++)
		if (velocity_apply(VELOCITY_FIXED, (uint8_t)v, 100) != 100) fixed_ok = 0;
	ok("fixed ignores played velocity", fixed_ok, "always 100");
	ok("fixed clamps zero up to 1", velocity_apply(VELOCITY_FIXED, 64, 0) == 1, NULL);

	/* --- out of range curve id falls back to linear --- */
	ok("unknown curve falls back to linear",
	   velocity_apply(VELOCITY_CURVE_COUNT + 9, 77, 100) == 77, NULL);
	ok("zero input never returns zero", velocity_apply(VELOCITY_SOFT, 0, 100) >= 1, NULL);

	printf("\n%s\n", fails ? "FAILURES PRESENT" : "all velocity tests passed");
	return fails != 0;
}
