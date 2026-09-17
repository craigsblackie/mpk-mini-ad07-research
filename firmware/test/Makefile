# Host-side tests for the parts of the firmware that are pure logic.
#
# velocity.c has no hardware dependencies, so it compiles and runs here
# directly. The flash page layout constants are mirrored from program.c
# and asserted against the real page size, so a settings block that grows
# past the page is caught at test time rather than by corrupting a device.

CFLAGS = -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -I../include

velocity_test: velocity_test.c ../src/velocity.c ../include/velocity.h
	$(CC) $(CFLAGS) velocity_test.c ../src/velocity.c -lm -o $@

check: velocity_test
	./velocity_test

clean:
	rm -f velocity_test

.PHONY: check clean
