CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread
CORE_TARGET = core_server

all: build-core

build-core:
	$(CXX) $(CXXFLAGS) server.cpp -o $(CORE_TARGET)

run-core: build-core
	./$(CORE_TARGET)

run-server:
	node main.js

clean:
	rm -f $(CORE_TARGET)

.PHONY: all build-core run-core run-server clean
