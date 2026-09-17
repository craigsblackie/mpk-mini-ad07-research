# Host-side round-trip tests for the BLE-MIDI translation logic.
#
# main.c is compiled against the stub headers in stub/, which provide just
# enough of ESP-IDF and NimBLE to link. Outbound MIDI is pushed through the
# parser, the outbound queue and the packet builder, then every resulting
# notification is fed back through the inbound decoder; the recovered byte
# stream must match what went in.

CFLAGS = -std=gnu11 -Wall -Wextra -g -fsanitize=address,undefined \
         -Wno-unused-parameter -Wno-unused-variable -Istub -I../main

test: test.c ../main/main.c $(wildcard stub/*.h stub/*/*.h)
	$(CC) $(CFLAGS) test.c -o $@

check: test
	./test

clean:
	rm -f test

.PHONY: check clean
