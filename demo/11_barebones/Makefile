ROOT := ../..
PPLANG := $(ROOT)/bin/pplang
AS := as
CC := i686-elf-gcc

SRCS := $(wildcard *.pp)
TARGET := kernel_demo
CFLAGS ?= -O2 -Wall

all: $(TARGET)

$(PPLANG):
	$(MAKE) -C $(ROOT)

# Extra flags passed to pplang (e.g. -g, -O2)
PPCFLAGSEXTRA ?=

$(TARGET): boot.o kernel.o
	$(CC) -T linker.ld -o $@ -ffreestanding -O2 -nostdlib $^ -lgcc

iso: $(TARGET).iso

$(TARGET).iso: $(TARGET)
	mkdir -p isodir/boot/grub
	cp $(TARGET) isodir/boot/$(TARGET)
	grub-mkrescue -o $@ isodir

boot.o: boot.s
	$(AS) $< -o $@ --32

kernel.o: kernel.pp $(PPLANG)
	$(PPLANG) $(PPCFLAGSEXTRA) --ffreestanding --target i386-unknown-linux-gnu -c -o $@ $<

clean:
	rm -f $(TARGET) *.o isodir/boot/$(TARGET) *.iso

.PHONY: all clean iso
