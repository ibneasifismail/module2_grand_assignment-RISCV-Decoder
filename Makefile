# ==============================================================================
# Makefile — RISC-V RV32I Instruction Decoder
#
# Targets:
#   make / make all     Build the optimized release binary at bin/riscv-decoder
#   make debug          Build with -g -O0 and no optimization, for use with gdb
#   make test           Build and run the unit test suite (test/test_decoder)
#   make valgrind       Run the decoder under Valgrind against a sample program
#                        to confirm zero memory leaks
#   make clean          Remove all build artifacts (build/, bin/, test binary)
# ==============================================================================

CC      := gcc
CSTD    := -std=c11
WARN    := -Wall -Wextra -Wpedantic
INCLUDE := -Iinclude

SRC_DIR   := src
BUILD_DIR := build
BIN_DIR   := bin
TEST_DIR  := test

TARGET    := $(BIN_DIR)/riscv-decoder
TEST_BIN  := $(TEST_DIR)/test_decoder

SOURCES   := $(SRC_DIR)/main.c $(SRC_DIR)/decoder.c $(SRC_DIR)/memory.c
OBJECTS   := $(BUILD_DIR)/main.o $(BUILD_DIR)/decoder.o $(BUILD_DIR)/memory.o

# Decoder + memory only (no main) — what the unit test suite links against
LIB_SOURCES := $(SRC_DIR)/decoder.c $(SRC_DIR)/memory.c

CFLAGS_RELEASE := $(CSTD) $(WARN) $(INCLUDE) -O2
CFLAGS_DEBUG   := $(CSTD) $(WARN) $(INCLUDE) -g -O0 -DDEBUG

# Default sample program used by `make valgrind`
SAMPLE_HEX := $(TEST_DIR)/programs/mixed.hex

.PHONY: all clean test debug valgrind

# ------------------------------------------------------------------------
# all — default target: build the optimized release binary
# ------------------------------------------------------------------------
all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(CFLAGS_RELEASE) $(OBJECTS) -o $@
	@echo "Built $(TARGET)"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_RELEASE) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# ------------------------------------------------------------------------
# debug — unoptimized build with debug symbols, for gdb
# ------------------------------------------------------------------------
debug: $(BUILD_DIR) $(BIN_DIR)
	$(CC) $(CFLAGS_DEBUG) $(SOURCES) -o $(BIN_DIR)/riscv-decoder-debug
	@echo "Built debug binary at $(BIN_DIR)/riscv-decoder-debug (run with gdb)"

# ------------------------------------------------------------------------
# test — build and run the unit test suite
# ------------------------------------------------------------------------
test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_DIR)/test_decoder.c $(LIB_SOURCES)
	$(CC) $(CFLAGS_RELEASE) $(TEST_DIR)/test_decoder.c $(LIB_SOURCES) -o $(TEST_BIN)

# ------------------------------------------------------------------------
# valgrind — run the built decoder under Valgrind's memcheck tool against
# a sample hex program, failing loudly on any leak or invalid access.
# ------------------------------------------------------------------------
valgrind: debug
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 \
		$(BIN_DIR)/riscv-decoder-debug $(SAMPLE_HEX)

# ------------------------------------------------------------------------
# clean — remove all generated files
# ------------------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(TEST_BIN)
	@echo "Cleaned build artifacts"
