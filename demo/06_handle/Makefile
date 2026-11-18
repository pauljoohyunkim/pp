ROOT := ../..
PPLANG := $(ROOT)/bin/pplang

SRCS := main.pp
HELPER_SRC := handle_demo.c
HELPER_OBJ := handle_demo.o
TARGET := 06_handle

all: $(TARGET)

$(PPLANG):
	$(MAKE) -C $(ROOT)

$(HELPER_OBJ): $(HELPER_SRC)
	clang -g -O0 -c $(HELPER_SRC) -o $(HELPER_OBJ)

$(TARGET): $(PPLANG) $(SRCS) $(HELPER_OBJ)
	$(PPLANG) $(SRCS) $(HELPER_OBJ) -o $@

clean:
	rm -f $(TARGET) $(HELPER_OBJ)

.PHONY: all clean
