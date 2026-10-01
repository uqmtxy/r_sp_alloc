CXX ?= g++
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17 -Wall -Wextra -fPIC -pthread
LDFLAGS ?= -shared -pthread

all: libdynalloc.so

libdynalloc.so: dynalloc.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o libdynalloc.so dynalloc.cpp

clean:
	rm -f libdynalloc.so
