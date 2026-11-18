# Simulator C++17 tree: src/{util,core}/*.cpp -> build/libsimcore.a, tests/unit/test_*.cpp -> build/unit/*.
# `make test` builds and runs every unit test. The legacy C sources (emu/, launcher/) are not touched here.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc
B        := build

LIB_SRC  := $(wildcard src/util/*.cpp src/core/*.cpp)
LIB_OBJ  := $(patsubst src/%.cpp,$(B)/obj/%.o,$(LIB_SRC))
TESTS    := $(patsubst tests/unit/%.cpp,$(B)/unit/%,$(wildcard tests/unit/test_*.cpp))

.PHONY: all test clean
all: $(B)/libsimcore.a

$(B)/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(B)/libsimcore.a: $(LIB_OBJ)
	@mkdir -p $(B)
	ar rcs $@ $^

$(B)/unit/%: tests/unit/%.cpp $(B)/libsimcore.a
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(B)/libsimcore.a -o $@

test: $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; $$t; done; echo "unit tests: ok ($(words $(TESTS)))"

clean:
	rm -rf $(B)/obj $(B)/unit $(B)/libsimcore.a

-include $(LIB_OBJ:.o=.d)
