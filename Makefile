# Catan engine — clang++ build (no CMake required)
CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -I include

OBJS = build/topology.o build/board.o build/state.o build/rules.o build/eval.o build/mcts.o build/train.o build/json_api.o build/strategy.o build/visits.o

.PHONY: all clean ui

all: build/catan_advise build/catan_train build/catan_bench build/catan_bridge

ui: build/catan_bridge
	python3 web/server.py

build:
	mkdir -p build

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/catan_advise: apps/advise.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) apps/advise.cpp $(OBJS) -o $@

build/catan_train: apps/train.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) apps/train.cpp $(OBJS) -o $@

build/catan_bench: apps/bench.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) apps/bench.cpp $(OBJS) -o $@

build/catan_bridge: apps/bridge.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) apps/bridge.cpp $(OBJS) -o $@

clean:
	rm -rf build

