ROOT := ../..
PPLANG := $(ROOT)/bin/pplang
PP_STD := $(ROOT)/lib/pp_std

all: bf_demo

$(PPLANG):
	$(MAKE) -C $(ROOT)

$(PP_STD)/bin/pp_std.a:
	$(MAKE) -C $(PP_STD)

bf_demo: $(PPLANG) main.pp brainfuck.pp $(PP_STD)/bin/pp_std.a
	$(PPLANG) --ppi $(PP_STD)/extern/pp_io.ppi --ppi $(PP_STD)/extern/pp_runtime.ppi --ppi brainfuck.ppi main.pp brainfuck.pp $(PP_STD)/bin/pp_std.a -o $@

clean:
	rm -f bf_demo

.PHONY: all clean
