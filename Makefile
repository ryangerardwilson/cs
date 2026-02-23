CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CS_VERSION ?= $(shell cat VERSION)
CS_REPO_OWNER ?=
CS_REPO_NAME ?=
CFLAGS += -DCS_VERSION=\"$(CS_VERSION)\" -DCS_REPO_OWNER=\"$(CS_REPO_OWNER)\" -DCS_REPO_NAME=\"$(CS_REPO_NAME)\"

bin_cs: cs.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f bin_cs
