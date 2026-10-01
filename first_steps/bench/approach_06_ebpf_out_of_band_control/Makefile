CXX ?= g++
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17 -Wall -Wextra -pthread

all: target_workload memory_supervisor

target_workload: target_workload.cpp
	$(CXX) $(CXXFLAGS) -o target_workload target_workload.cpp

memory_supervisor: ../../daemon/memory_supervisor.cpp
	$(CXX) $(CXXFLAGS) -o memory_supervisor ../../daemon/memory_supervisor.cpp

clean:
	rm -f target_workload memory_supervisor
