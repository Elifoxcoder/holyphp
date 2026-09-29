# HolyPHP toolchain — builds the hphp compiler binary
#
# The gcc driver's collect2 intermittently fails on some Windows setups
# (on-access scanners); scripts/build.sh compiles objects with gcc -c and
# links with ld directly, which is reliable. On other platforms a plain
# `make CC=gcc ...` of the same sources works too.

CC      = gcc
CFLAGS  = -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
SRC     = $(wildcard src/hphpc/*.c)
RUNTIME = src/hphpc/runtime/hphp_rt.c src/hphpc/runtime/hphp_std.c
BIN     = hphp.exe

all: $(BIN)

$(BIN):
	bash scripts/build.sh

test: $(BIN)
	./$(BIN) run examples/hello.hphp
	./$(BIN) run examples/demo.hphp

# 1BRC benchmark test (needs the official 1brc repo in Downloads;
# override with BRC_ROWS=<n>, 0 = the real 1e9-row file):
#   make test-1brc BRC_ROWS=1000000
BRC_ROWS ?= 1000000
test-1brc: $(BIN)
	bash benchmarks/1brc_test.sh $(BRC_ROWS)

check: $(BIN)
	./$(BIN) check examples/hello.hphp
	./$(BIN) check examples/demo.hphp

clean:
	rm -rf build/obj $(BIN) hphp_out.* hp_demo* *.gen.c

.PHONY: all test test-1brc check clean
