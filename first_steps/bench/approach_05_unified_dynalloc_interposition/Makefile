CXX ?= g++
CXXFLAGS ?= -O3 -DNDEBUG -std=c++17 -Wall -Wextra -pthread

all: client_app

client_app: client_app.cpp
	$(CXX) $(CXXFLAGS) -o client_app client_app.cpp

clean:
	rm -f client_app
