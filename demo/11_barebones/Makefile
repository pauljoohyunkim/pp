ROOT := ../..
PPLANG := $(ROOT)/bin/pplang
AS := as

SRCS := $(wildcard *.pp)
TARGET := kernel_demo
CFLAGS ?= -O2 -Wall

all: $(TARGET)

$(PPLANG):
	$(MAKE) -C $(ROOT)

# Extra flags passed to pplang (e.g. -g, -O2)
PPCFLAGSEXTRA ?=

boot.o: boot.s
	$(AS) $< -o $@ --32

kernel.o: kernel.pp $(PPLANG)
	$(PPLANG) $(PPCFLAGSEXTRA) --ffreestanding --target i386-unknown-linux-gnu -c -o $@ $<

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
