# Host-side tests for the bridge. Both suites compile the real firmware
# sources against the stub ESP-IDF/NimBLE headers in stub/ -- no hardware,
# no ESP-IDF install needed.
#
#   test        BLE-MIDI translation: encode a stream, decode it back,
#               assert the recovered bytes match what went in.
#   editor_test the editor's SysEx translation, driven against a fake
#               keyboard that answers 'c'/'a'/'v'/'d' like the real one.
#               Also checks editor.c's copy of the firmware's wire-reorder
#               table against firmware/src/program.c, byte for byte.

CFLAGS = -std=gnu11 -Wall -Wextra -g -fsanitize=address,undefined \
         -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function \
         -Istub -I../main
FIRMWARE_PROGRAM_C = ../../firmware/src/program.c

STUBS = $(wildcard stub/*.h stub/*/*.h stub/*/*/*.h)

test: test.c ../main/main.c $(STUBS)
	$(CC) $(CFLAGS) test.c -o $@

editor_test: editor_test.c ../main/editor.c ../main/sysex_bridge.h $(STUBS)
	$(CC) $(CFLAGS) -DEDITOR_HOST_TEST \
	  -DFIRMWARE_PROGRAM_C='"$(FIRMWARE_PROGRAM_C)"' editor_test.c -o $@

check: test editor_test
	./test
	@echo
	./editor_test

clean:
	rm -f test editor_test

.PHONY: check clean
