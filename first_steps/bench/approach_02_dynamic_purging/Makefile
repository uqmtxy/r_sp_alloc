CXX ?= g++
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17 -Wall -Wextra -pthread

all: bench

bench: bench.cpp purging_allocator.hpp
	$(CXX) $(CXXFLAGS) -o bench bench.cpp

clean:
	rm -f bench
