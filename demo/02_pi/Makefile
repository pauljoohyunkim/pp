ROOT := ../..
PPLANG := $(ROOT)/bin/pplang
PP_STD := $(ROOT)/lib/pp_std

all: pi_demo

$(PP_STD)/bin/pp_std.a:
	$(MAKE) -C $(PP_STD)

pi_demo: main.pp $(PP_STD)/bin/pp_std.a
	$(PPLANG) main.pp $(PP_STD)/bin/pp_std.a -o $@

clean:
	rm -f pi_demo

.PHONY: all clean
