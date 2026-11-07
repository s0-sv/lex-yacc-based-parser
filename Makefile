CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
BUILD    := build
BIN      := $(BUILD)/minicc
SRC      := src/ast.cpp src/sema.cpp src/interp.cpp src/codegen.cpp src/main.cpp
OBJ      := $(SRC:src/%.cpp=$(BUILD)/%.o) $(BUILD)/parser.o $(BUILD)/lexer.o

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread

$(BUILD)/parser.cpp $(BUILD)/parser.hpp: src/parser.y | $(BUILD)
	bison -Wall --defines=$(BUILD)/parser.hpp -o $(BUILD)/parser.cpp $<

$(BUILD)/lexer.cpp: src/lexer.l | $(BUILD)
	flex -o $@ $<

$(BUILD)/parser.o: $(BUILD)/parser.cpp src/ast.hpp
	$(CXX) $(CXXFLAGS) -Isrc -I$(BUILD) -c -o $@ $<

$(BUILD)/lexer.o: $(BUILD)/lexer.cpp $(BUILD)/parser.hpp src/ast.hpp
	$(CXX) $(CXXFLAGS) -Wno-unused-function -Wno-sign-compare -Isrc -I$(BUILD) -c -o $@ $<

$(BUILD)/%.o: src/%.cpp src/ast.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -c -o $@ $<

$(BUILD):
	mkdir -p $@

test: $(BIN)
	python3 tests/run_tests.py

web: $(BIN)
	python3 web/server.py

clean:
	rm -rf $(BUILD)

.PHONY: all test web clean
