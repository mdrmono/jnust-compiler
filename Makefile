LLVM_CONFIG ?= llvm-config
CC := gcc
CXX := g++

BUILD_DIR := build
BIN_DIR := bin
TARGET := $(BIN_DIR)/jnustcomp

PARSER_SRC := src/jnustcomp.y
LEXER_SRC := src/jnustcomp.lex
RUNTIME_SRC := runtime/jnust-stdlib.c

PARSER_CC := $(BUILD_DIR)/jnustcomp.tab.cc
PARSER_H := $(BUILD_DIR)/jnustcomp.tab.h
LEXER_CC := $(BUILD_DIR)/jnustcomp.lex.cc
RUNTIME_OBJ := $(BUILD_DIR)/jnust-stdlib.o

CPPFLAGS += -Iinclude -Isrc -I$(BUILD_DIR) -Wno-deprecated-register -no-pie
CFLAGS += -no-pie
LLVM_FLAGS := $(shell $(LLVM_CONFIG) --cxxflags --cppflags --cflags --ldflags --libs core native)
LLVM_LIBDIR := $(shell $(LLVM_CONFIG) --libdir)
LDFLAGS += -Wl,-rpath,$(LLVM_LIBDIR)
LDLIBS += -ly -ll -lz -lncurses -ldl -lpthread -fexceptions

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(PARSER_CC) $(LEXER_CC) $(RUNTIME_OBJ) src/jnustcomp.cc include/jnust-defs.h | $(BIN_DIR)
	$(CXX) -o $@ $(PARSER_CC) $(LEXER_CC) $(RUNTIME_OBJ) $(LLVM_FLAGS) $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)

$(PARSER_CC) $(PARSER_H): $(PARSER_SRC) | $(BUILD_DIR)
	bison -b $(BUILD_DIR)/jnustcomp -d $<
	mv -f $(BUILD_DIR)/jnustcomp.tab.c $(PARSER_CC)

$(LEXER_CC): $(LEXER_SRC) $(PARSER_H) | $(BUILD_DIR)
	flex -o$@ $<

$(RUNTIME_OBJ): $(RUNTIME_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -g -c -o $@ $<

$(BIN_DIR) $(BUILD_DIR):
	mkdir -p $@

test: $(TARGET)
	$(TARGET) < tests/dev/default-passes.jnust > /dev/null 2> /dev/null

clean:
	$(RM) -r $(BUILD_DIR) $(BIN_DIR)
