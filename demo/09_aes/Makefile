ROOT := ../..
PPLANG := $(ROOT)/bin/pplang
PP_STD := $(ROOT)/lib/pp_std

SRCS := $(wildcard *.pp)
PPIS  := $(wildcard *.ppi)
TARGET := aes_demo

all: $(TARGET)

$(PPLANG):
	$(MAKE) -C $(ROOT)

$(PP_STD)/bin/pp_std.a:
	$(MAKE) -C $(PP_STD)

# This demo needs both the I/O and the runtime PPI (pp_get_argv lives there).
BASE_PPI := --ppi $(PP_STD)/extern/pp_io.ppi --ppi $(PP_STD)/extern/pp_runtime.ppi
PPI_FLAGS := $(foreach f,$(PPIS),--ppi $(f))

# Extra flags passed to pplang (e.g. -g, -O2)
PPCFLAGSEXTRA ?=

$(TARGET): $(PPLANG) $(SRCS) $(PPIS) $(PP_STD)/bin/pp_std.a
	$(PPLANG) $(BASE_PPI) $(PPI_FLAGS) $(PPCFLAGSEXTRA) $(SRCS) $(PP_STD)/bin/pp_std.a -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
