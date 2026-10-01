CXX ?= g++
CXXFLAGS ?= -O3 -g -std=c++20 -Wall -Wextra -pedantic -pthread
LDFLAGS ?= -pthread

.PHONY: all clean run run-all

all: bench/alloc_workload

bench/alloc_workload: bench/alloc_workload.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

run: all
	./scripts/run_one.sh glibc

run-all: all
	./scripts/run_all.sh

clean:
	rm -f bench/alloc_workload
