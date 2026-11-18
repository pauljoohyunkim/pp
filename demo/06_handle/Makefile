ROOT := ../..
PPLANG := $(ROOT)/bin/pplang
PP_STD := $(ROOT)/lib/pp_std

SRCS := main.pp
HELPER_SRC := handle_demo.c
HELPER_OBJ := handle_demo.o
TARGET := handle_demo

PPCFLAGSEXTRA ?=

all: $(TARGET)

$(PPLANG):
	$(MAKE) -C $(ROOT)

$(HELPER_OBJ): $(HELPER_SRC)
	clang -g -O0 -I$(PP_STD)/include -c $(HELPER_SRC) -o $(HELPER_OBJ)

$(TARGET): $(PPLANG) $(SRCS) $(HELPER_OBJ)
	$(PPLANG) $(PPCFLAGSEXTRA) $(SRCS) $(HELPER_OBJ) -o $@

clean:
	rm -f $(TARGET) $(HELPER_OBJ) pp_handle_demo.txt

.PHONY: all clean
