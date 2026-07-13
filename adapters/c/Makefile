# adapters/c/Makefile -- builds the C reference adapter CLI (`adapter`) and
# its hand-rolled unit test runner (`test_runner`). Matches the "no shared
# build system, no language-specific integration required" spirit of
# CONTRIBUTING.md: a plain Makefile + gcc/clang, no CMake, no third-party
# build tooling.

CC ?= gcc
SRC_DIR := src
TEST_DIR := tests

# gnu11 (not plain c11): this adapter deliberately uses GCC/Clang's
# `__int128` extension for bignum-magnitude arithmetic (see
# src/bignum128.h's header comment) rather than reaching for a
# hand-rolled arbitrary-precision math library -- gnu11 documents that
# as an intentional extension use, not an accident caught by -pedantic.
CFLAGS := -std=gnu11 -Wall -Wextra -O2 -g
LDFLAGS := -lm

SRCS := $(wildcard $(SRC_DIR)/*.c)
LIB_SRCS := $(filter-out $(SRC_DIR)/main.c,$(SRCS))
TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)

SAN_FLAGS := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

.PHONY: all test test-asan adapter-asan clean

all: adapter

adapter: $(SRCS)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ $^ $(LDFLAGS)

test_runner: $(LIB_SRCS) $(TEST_SRCS)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ $^ $(LDFLAGS)

test: test_runner
	./test_runner

# Sanitizer-enabled unit test build (ASan + UBSan), per CONTRIBUTING.md's
# "memory safety is a first-class concern" requirement for this adapter.
# Only forces a rebuild of test_runner (not adapter): Make can't see that
# CFLAGS changed between a plain `test` and this target, so test_runner
# must be force-deleted to guarantee a fresh sanitizer-instrumented
# compile -- but that must not delete the unrelated `adapter` binary a
# separate `make adapter` step may have already produced (this exact
# cross-target deletion broke CI: the harness step found no adapter
# binary because this target's `clean` prerequisite had deleted it).
test-asan: CFLAGS += $(SAN_FLAGS)
test-asan: LDFLAGS += $(SAN_FLAGS)
test-asan:
	rm -f test_runner
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o test_runner $(LIB_SRCS) $(TEST_SRCS) $(LDFLAGS)
	./test_runner

# Sanitizer-enabled CLI binary, for running the conformance harness (or a
# raw vector sample) under ASan/UBSan -- CI wires this into the vector
# pass, not just the unit test run. Only forces a rebuild of `adapter`,
# for the same reason test-asan above only touches `test_runner`.
adapter-asan: CFLAGS += $(SAN_FLAGS)
adapter-asan: LDFLAGS += $(SAN_FLAGS)
adapter-asan:
	rm -f adapter
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o adapter $(SRCS) $(LDFLAGS)

clean:
	rm -f adapter test_runner
