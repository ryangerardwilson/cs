CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

bin_cs: cs.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f bin_cs
