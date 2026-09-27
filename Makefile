# Makefile - parallel merge sort
#
#   make          build ./parallel_merge_sort
#   make run      small demo run in both modes
#   make test     unit tests for the merge + small end-to-end runs
#   make bench    1..10 workers x 3 runs x (threads, processes) -> raw_data.csv
#   make clean    remove everything that was built

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -pthread -D_FILE_OFFSET_BITS=64
LDLIBS  = -pthread -lrt

BIN      = parallel_merge_sort
TEST_BIN = test_merge
SRCS     = src/main.c src/sort.c src/merge.c src/message_queue.c src/sync.c
HEADERS  = src/common.h src/merge.h src/message_queue.h src/sync.h

# Dataset size for `make bench` (1 billion = 4 GB). Override for a quick
# try, e.g.  make bench BENCH_N=10000000
BENCH_N ?= 1000000000
CSV     ?= raw_data.csv

.PHONY: all run test bench clean

all: $(BIN)

$(BIN): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

$(TEST_BIN): tests/test_merge.c src/sort.c src/merge.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ tests/test_merge.c src/sort.c src/merge.c $(LDLIBS)

run: $(BIN)
	./$(BIN) -m thread  -w 4 -n 10000000
	./$(BIN) -m process -w 4 -n 10000000

test: $(TEST_BIN) $(BIN)
	./$(TEST_BIN)
	./$(BIN) -m thread  -w 1  -n 1000000 -q
	./$(BIN) -m process -w 1  -n 1000000 -q
	./$(BIN) -m thread  -w 10 -n 1000000 -q
	./$(BIN) -m process -w 10 -n 1000000 -q
	./$(BIN) -m process -w 16 -n 1000000 -q

# A new seed for every run, so no run sorts the same data twice.
bench: $(BIN)
	rm -f $(CSV)
	for mode in thread process; do \
	  for w in 1 2 3 4 5 6 7 8 9 10; do \
	    for r in 1 2 3; do \
	      ./$(BIN) -m $$mode -w $$w -r $$r -s $$((r * 100 + w)) \
	               -n $(BENCH_N) -c $(CSV) -q || exit 1; \
	    done; \
	  done; \
	done
	@echo "results written to $(CSV)"

clean:
	rm -f $(BIN) $(TEST_BIN) *.o
