CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Iinclude
LDFLAGS ?=
PORT    ?= /dev/bms

BUILDDIR := build
BINDIR   := install/bin

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=$(BUILDDIR)/%.o)
HDR = $(wildcard include/*.h)

TARGET = $(BINDIR)/bms_query

.PHONY: all clean distclean run test

all: $(TARGET)

# Final binary -> install/bin/
$(TARGET): $(OBJ) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

# Objects -> build/
$(BUILDDIR)/%.o: src/%.c $(HDR) | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR) $(BINDIR):
	mkdir -p $@

# Remove build artifacts only; logs/ is kept (see distclean).
clean:
	rm -rf $(BUILDDIR) $(TARGET)
	@echo "logs/ kept; run 'make distclean' to remove runtime data too"

distclean: clean
	rm -rf logs install
	@echo "removed logs/ and install/"

run: $(TARGET)
	./$(TARGET) $(PORT)

# ---- tests (binaries under build/) ----
$(BUILDDIR)/test_proto: tests/test_proto.c $(BUILDDIR)/protocol.o | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/test_balance: tests/test_balance.c $(BUILDDIR)/balance_fmt.o | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/test_prot: tests/test_prot.c $(BUILDDIR)/prot_fmt.o | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: $(BUILDDIR)/test_proto $(BUILDDIR)/test_balance $(BUILDDIR)/test_prot
	./$(BUILDDIR)/test_proto
	./$(BUILDDIR)/test_balance
	./$(BUILDDIR)/test_prot
