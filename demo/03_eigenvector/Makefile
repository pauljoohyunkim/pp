ROOT := ../..
PPLANG := $(ROOT)/bin/pplang
PP_STD := $(ROOT)/lib/pp_std

all: eigenvector_demo

$(PPLANG):
	$(MAKE) -C $(ROOT)

$(PP_STD)/bin/pp_std.a:
	$(MAKE) -C $(PP_STD)

eigenvector_demo: $(PPLANG) main.pp $(PP_STD)/bin/pp_std.a
	$(PPLANG) main.pp $(PP_STD)/bin/pp_std.a -o $@

clean:
	rm -f eigenvector_demo

.PHONY: all clean
