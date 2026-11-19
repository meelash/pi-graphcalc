# Desktop simulator and unit tests for the calculator (host compiler, needs zlib).
ROOT    := ../..
BUILD   := $(ROOT)/build/sim
GEN     := $(ROOT)/build/gen
CORE    := $(wildcard ../core/*.cpp)
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c++17 -fno-exceptions -fno-rtti
CXXFLAGS += -I../core

all: $(BUILD)/sim $(BUILD)/mathtest $(BUILD)/exprtest

$(BUILD)/sim: $(CORE) sim.cpp $(GEN)/fontdata.cpp $(wildcard ../core/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(CORE) sim.cpp $(GEN)/fontdata.cpp -lz

MATH := ../core/mathlib.cpp ../core/numfmt.cpp ../core/expr.cpp ../core/base.cpp
$(BUILD)/mathtest: $(ROOT)/tests/math/mathtest.cpp $(MATH) $(wildcard ../core/*.h)
	@mkdir -p $(BUILD)
	$(CXX) -O2 -std=c++17 -o $@ $< $(MATH)

$(BUILD)/exprtest: $(ROOT)/tests/math/exprtest.cpp $(MATH) $(wildcard ../core/*.h)
	@mkdir -p $(BUILD)
	$(CXX) -O2 -std=c++17 -I../core -o $@ $< $(MATH)

$(GEN)/fontdata.cpp: $(ROOT)/scripts/gen_fonts.py $(wildcard $(ROOT)/assets/fonts/*.ttf)
	python3 $(ROOT)/scripts/gen_fonts.py $@

.PHONY: all clean
clean:
	rm -rf $(BUILD)
