ROOT := ../..
PPLANG := $(ROOT)/bin/pplang
PP_STD := $(ROOT)/lib/pp_std

SRCS := $(wildcard *.pp)
PPIS  := $(wildcard *.ppi)
TARGET := pi_demo

all: $(TARGET)

$(PPLANG):
	$(MAKE) -C $(ROOT)

$(PP_STD)/bin/pp_std.a:
	$(MAKE) -C $(PP_STD)

BASE_PPI := --ppi $(PP_STD)/extern/pp_io.ppi
PPI_FLAGS := $(foreach f,$(PPIS),--ppi $(f))

$(TARGET): $(PPLANG) $(SRCS) $(PPIS) $(PP_STD)/bin/pp_std.a
	$(PPLANG) $(BASE_PPI) $(PPI_FLAGS) $(SRCS) $(PP_STD)/bin/pp_std.a -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
