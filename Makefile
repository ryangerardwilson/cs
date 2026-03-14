CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
OUT ?= bin_cs
CS_VERSION ?= $(shell sed -n 's/^__version__ = \"\\(.*\\)\"$$/\\1/p' _version.py)
CS_REPO_OWNER ?=
CS_REPO_NAME ?=
CFLAGS += -DCS_VERSION=\"$(CS_VERSION)\" -DCS_REPO_OWNER=\"$(CS_REPO_OWNER)\" -DCS_REPO_NAME=\"$(CS_REPO_NAME)\"

all: $(OUT)

$(OUT): cs.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: all clean
clean:
	rm -f bin_cs
