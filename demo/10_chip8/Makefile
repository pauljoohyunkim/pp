ROOT := ../..
PPLANG := $(ROOT)/bin/pplang
PP_STD := $(ROOT)/lib/pp_std

SRCS := $(wildcard *.pp)
CSRCS := $(wildcard *.c)
COBJS := $(CSRCS:.c=.o)
PPIS  := $(wildcard *.ppi)
TARGET := chip8_demo
CFLAGS ?= -O2 -Wall

all: $(TARGET)

$(PPLANG):
	$(MAKE) -C $(ROOT)

$(PP_STD)/bin/pp_std.a:
	$(MAKE) -C $(PP_STD)

# This demo needs both the I/O and the runtime PPI (pp_get_argv lives there).
BASE_PPI := --ppi $(PP_STD)/extern/pp_io.ppi --ppi $(PP_STD)/extern/pp_runtime.ppi --ppi $(PP_STD)/extern/pp_misc.ppi
PPI_FLAGS := --ppi chip8_term.ppi --ppi chip8.ppi

# Extra flags passed to pplang (e.g. -g, -O2)
PPCFLAGSEXTRA ?=

$(TARGET): $(PPLANG) $(SRCS) $(PPIS) $(COBJS) $(PP_STD)/bin/pp_std.a
	$(PPLANG) $(BASE_PPI) $(PPI_FLAGS) $(PPCFLAGSEXTRA) $(SRCS) $(COBJS) $(PP_STD)/bin/pp_std.a -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(COBJS)

.PHONY: all clean
