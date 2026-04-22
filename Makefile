CXX      := $(shell root-config --cxx)
ROOTCLING := rootcling
MARCH    := $(shell root-config --arch)
LD       := $(CXX)
UNAME    := $(shell uname)

SRC      := ./src
OBJ      := ./obj

CLI11_DIR ?= third_party/CLI11/include

CFLAGS   := $(shell root-config --cflags) -g -fPIC -pthread \
            -I$(ROOTSYS)/include -I$(CLI11_DIR)
LDFLAGS  := $(shell root-config --glibs)
OPTFLAGS := -O3

# Precompiled header
PCH_SRC := $(CLI11_DIR)/CLI/CLI.hpp
PCH_OUT := $(OBJ)/CLI.hpp.gch

# Targets
TARGETS :=  ASTRA_convert ASTRA_info raw_clusterize raw_cn \
			raw_threshold_scan calibration readOM
			
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
ASTRA_convert: $(OBJ)/ASTRA_convert.o $(OBJ)/PAPERO.o
	$(LD) -o $@ $^ $(CFLAGS) $(LDFLAGS)

ASTRA_info: $(OBJ)/ASTRA_info.o $(OBJ)/PAPERO.o
	$(LD) -o $@ $^ $(CFLAGS) $(LDFLAGS)

raw_clusterize: $(OBJ)/raw_clusterize.o $(OBJ)/event.o
	$(LD) -o $@ $^ $(CFLAGS) $(LDFLAGS)

raw_cn: $(OBJ)/raw_cn.o $(OBJ)/event.o
	$(LD) -o $@ $^ $(CFLAGS) $(LDFLAGS)

raw_threshold_scan: $(OBJ)/raw_threshold_scan.o $(OBJ)/event.o
	$(LD) -o $@ $^ $(CFLAGS) $(LDFLAGS)

calibration: $(OBJ)/calibration.o $(OBJ)/event.o $(OBJ)/PAPERO.o
	$(LD) -o $@ $^ $(CFLAGS) $(LDFLAGS)

raw_viewer:
	$(ROOTCLING) -f guiDict.cpp $(SRC)/viewerGUI.h $(SRC)/udpSocket.cpp $(SRC)/guiLinkDef.h
	$(CXX) $(CFLAGS) $(OPTFLAGS) $(SRC)/viewerGUI.cpp $(SRC)/event.cpp guiDict.cpp -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS) raw_viewer
	rm -rf $(OBJ)
	rm -f guiDict.cpp guiDict_rdict.pcm