CXX ?= c++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
PYTHON ?= python3

.PHONY: all original test clean
all: build/latency_simulator build/uniform_simulator build/tie_simulator

build:
	mkdir -p build

build/latency_simulator: src/latency_simulator.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/uniform_simulator: src/uniform_simulator.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/tie_simulator: src/tie_simulator.cpp src/latency_simulator.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

original: build/original_simulator

build/original_simulator: blockchainSimulator.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/compare_uniform: tests/compare_uniform.cpp src/uniform_simulator.cpp src/latency_simulator.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

test: all build/compare_uniform
	./build/latency_simulator test
	./build/uniform_simulator test
	./build/compare_uniform
	./build/tie_simulator test
	$(PYTHON) -m unittest discover -s tests -v

clean:
	rm -rf build
