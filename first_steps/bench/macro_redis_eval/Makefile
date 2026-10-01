CXX ?= g++
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17 -Wall -Wextra -pthread

all: kv_production_server

kv_production_server: kv_production_server.cpp
	$(CXX) $(CXXFLAGS) -o kv_production_server kv_production_server.cpp

clean:
	rm -f kv_production_server
