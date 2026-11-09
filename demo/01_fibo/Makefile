ROOT := ../..
PPLANG := $(ROOT)/bin/pplang
PP_STD := $(ROOT)/lib/pp_std

all: fibo_demo

$(PP_STD)/bin/pp_std.a:
	$(MAKE) -C $(PP_STD)

fibo_demo: main.pp fibo.pp $(PP_STD)/bin/pp_std.a
	$(PPLANG) main.pp fibo.pp $(PP_STD)/bin/pp_std.a -o $@

clean:
	rm -f fibo_demo

.PHONY: all clean
