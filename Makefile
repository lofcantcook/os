# Makefile - parallel sort project (message passing, synchronization, merge)
#
# Tested with GCC on Linux (Manjaro/Arch, glibc >= 2.34). -lrt is kept for
# older glibc where mq_* and sem_* still live in librt.

CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -pedantic -D_GNU_SOURCE -pthread
LDLIBS  += -pthread -lrt

SRC_DIR   := src
TEST_DIR  := tests
BUILD_DIR := build

LIB_SRCS := $(SRC_DIR)/chunk.c $(SRC_DIR)/msgq.c $(SRC_DIR)/ipc_sync.c $(SRC_DIR)/merge.c
LIB_OBJS := $(LIB_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

BIN      := ipc_sort
TEST_BIN := $(BUILD_DIR)/test_merge

.PHONY: all test run clean

all: $(BIN)

$(BIN): $(LIB_OBJS) $(BUILD_DIR)/ipc_sort.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_BIN): $(LIB_OBJS) $(BUILD_DIR)/test_merge.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_merge.o: $(TEST_DIR)/test_merge.c $(wildcard $(SRC_DIR)/*.h) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

test: $(TEST_BIN) $(BIN)
	./$(TEST_BIN)
	./$(BIN) -n 2000000 -w 1 -q
	./$(BIN) -n 2000000 -w 4 -q
	./$(BIN) -n 2000000 -w 16 -q

run: $(BIN)
	./$(BIN) -n 10000000 -w 4

clean:
	rm -rf $(BUILD_DIR) $(BIN)
