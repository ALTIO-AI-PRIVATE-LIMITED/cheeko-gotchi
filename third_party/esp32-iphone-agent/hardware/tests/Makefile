# Host unit tests for the firmware's pure-C parts. Usage: make -C hardware/tests
SRC = ../hid_bridge/src
CFLAGS = -std=c11 -Wall -Wextra -g -fsanitize=address,undefined

test: test_main.c $(SRC)/bplist/bplist.c $(SRC)/video/gop.c
	$(CC) $(CFLAGS) -o $@ $^
	./test

clean:
	rm -f test

.PHONY: clean
