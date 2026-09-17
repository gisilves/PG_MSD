CXX      := $(shell root-config --cxx)
ROOTCLING := rootcling
MARCH    := $(shell root-config --arch)
LD       := $(CXX)
UNAME    := $(shell uname)

SRC      := ./src
OBJ      := ./obj

CLI11_DIR ?= third_party/CLI11/include

CFLAGS   := $(shell root-config --cflags) -g -fPIC -pthread \
            -I$(ROOTSYS)/include -I$(CLI11_DIR) -Wvla
LDFLAGS  := $(shell root-config --glibs)
OPTFLAGS := -O3

# Precompiled header
PCH_SRC := $(CLI11_DIR)/CLI/CLI.hpp
PCH_OUT := $(OBJ)/CLI.hpp.gch

# Targets
TARGETS :=  miniMazinga_convert
.PHONY: all clean raw_viewer
default: all
all: $(TARGETS)

$(OBJ):
	mkdir -p $(OBJ)

# PCH build
$(PCH_OUT): $(PCH_SRC) | $(OBJ)
	$(CXX) $(CFLAGS) $(OPTFLAGS) -x c++-header $< -o $@

# Object file compilation
$(OBJ)/%.o: $(SRC)/%.cpp $(PCH_OUT) | $(OBJ)
	$(CXX) $(CFLAGS) $(OPTFLAGS) -c $< -o $@

# Link rules
miniMazinga_convert: $(OBJ)/miniMazinga_convert.o $(OBJ)/PAPERO.o
	$(LD) -o $@ $^ $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGETS)
	find $(OBJ) -type f -not -name 'CLI.hpp.gch' -delete

clean_all:
	rm -f $(TARGETS)
	rm -rf $(OBJ)
