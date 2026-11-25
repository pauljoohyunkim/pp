INCLUDE=include
CC=clang
CXX=clang++
CFLAGS=-g -Wall -O0 -I$(INCLUDE) -MMD -MP $(shell llvm-config --cflags) $(CFLAGS_EXTRA)
CXXFLAGS=$(CFLAGS)
RELEASE_CFLAGS=-g -Wall -O2 -I$(INCLUDE) -MMD -MP $(shell llvm-config --cflags) $(CFLAGS_EXTRA)
RELEASE_CXXFLAGS=$(RELEASE_CFLAGS)

SRC=src
OBJ=obj
RELEASE_OBJ=obj/release
BIN=bin
TESTS=tests
SRCS=$(wildcard $(SRC)/pp_*.c)
OBJS=$(patsubst $(SRC)/%.c,$(OBJ)/%.o,$(SRCS))
RELEASE_OBJS=$(patsubst $(SRC)/%.c,$(RELEASE_OBJ)/%.o,$(SRCS))
DEPS=$(SRCS:.c=.d) $(SRC)/pplang.d
RELEASE_DEPS=$(SRCS:.c=.d) $(SRC)/pplang.d
UNITTEST_SRCS=$(wildcard $(TESTS)/*.cpp)
UNITTEST_OBJS=$(patsubst $(TESTS)/%.cpp,$(OBJ)/unittest_%.o,$(UNITTEST_SRCS))
UNITTEST_LDLIBS=$(shell pkg-config --libs gtest)
UNITTEST_DEPS=$(UNITTEST_SRCS:.cpp=.d)
LDLIBS=$(shell llvm-config --libs)
BINARY=pplang

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

pplang: $(BIN)/$(BINARY)
unittest: $(BIN)/unittest
release: $(BIN)/$(BINARY)_release

install: release
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BIN)/$(BINARY)_release $(DESTDIR)$(BINDIR)/$(BINARY)

uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/$(BINARY)

$(BIN)/$(BINARY): $(OBJS) $(OBJ)/$(BINARY).o
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BIN)/$(BINARY)_release: $(RELEASE_OBJS) $(RELEASE_OBJ)/$(BINARY).o
	$(CC) $(RELEASE_CFLAGS) $^ -o $@ $(LDLIBS)

$(BIN)/unittest: $(OBJS) $(UNITTEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS) $(UNITTEST_LDLIBS)

$(OBJ)/unittest_%.o: $(TESTS)/%.cpp
	$(CXX) $(CXXFLAGS) -MF $(TESTS)/$*.d -c $< -o $@

$(OBJ)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -MF $(SRC)/$*.d -c $< -o $@

$(RELEASE_OBJ)/%.o: $(SRC)/%.c
	@mkdir -p $(RELEASE_OBJ)
	$(CC) $(RELEASE_CFLAGS) -MF $(RELEASE_OBJ)/$*.d -c $< -o $@

-include $(DEPS) $(UNITTEST_DEPS)

clean:
	find $(OBJ) -type f ! -name '.gitkeep' -delete
	$(RM) -f $(BIN)/* $(DEPS) $(UNITTEST_DEPS)

clean-release:
	$(RM) -rf $(RELEASE_OBJ) $(BIN)/$(BINARY)_release

.PHONY: $(BINARY) unittest release clean clean-release install uninstall
