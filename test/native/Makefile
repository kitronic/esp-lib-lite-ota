# LiteOTA Native Tests — Makefile
#
# Kitronic — https://github.com/kitronic/esp-lib-lite-ota
#
# Usage:
#   make            # build and run tests
#   make build      # build only
#   make run        # run only (must be built first)
#   make clean      # remove binaries
#   make valgrind   # run under valgrind (if installed)

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
LDFLAGS  ?=

BIN      := test_runner
SRC      := test_native.cpp

.PHONY: all build run clean valgrind

all: build run

build: $(BIN)

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

run: $(BIN)
	./$(BIN)

valgrind: $(BIN)
	valgrind --leak-check=full --error-exitcode=1 ./$(BIN)

clean:
	rm -f $(BIN)
	rm -rf *.dSYM