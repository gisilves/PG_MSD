CXX := `root-config --cxx`
ROOTCLING=rootcling
MARCH := `root-config --arch`
LD:=$(CXX)
SRC=./src/

ANYOPTION=$(SRC)/anyoption.cpp

UNAME := $(shell uname)

CFLAGS += $(shell root-config --ldflags --cflags --glibs) -g -fPIC
OPTFLAGS += -Ofast -mtune=native -march=native -w -funroll-loops -D_GLIBCXX_PARALLEL

ifeq ($(shell uname -s),Linux) # LINUX
OPTFLAGS += -fopenmp
endif


default: FOOT_compress

all: miniTRB_compress FOOT_compress PAPERO_compress raw_clusterize raw_threshold_scan raw_cn raw_viewer calibration bias_control

.PHONY: all

miniTRB_compress: ./src/miniTRB_compress.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

FOOT_compress: ./src/FOOT_compress.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

PAPERO_compress: ./src/PAPERO_compress.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

raw_clusterize: ./src/raw_clusterize.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

raw_threshold_scan: ./src/raw_threshold_scan.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS)

raw_cn: ./src/raw_cn.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

calibration: ./src/calibration.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

raw_viewer: 
	$(ROOTCLING) -f guiDict.cxx ./src/viewerGUI.hh ./src/guiLinkDef.h
	$(CXX) ./src/viewerGUI.cxx guiDict.cxx  -o$@ $< $(CFLAGS) $(OPTFLAGS)

bias_control: 
	$(ROOTCLING) -f guiDict.cxx ./src/biascontrol.hh ./src/guiLinkDef.h
	$(CXX) ./src/biascontrol.cxx guiDict.cxx  -o$@ $< $(CFLAGS) $(OPTFLAGS)

clean:
	rm -f ./miniTRB_compress
	rm -f ./FOOT_compress
	rm -f ./PAPERO_compress
	rm -f ./raw_clusterize
	rm -f ./raw_cn
	rm -f ./raw_threshold_scan
	rm -f ./raw_viewer
	rm -f ./calibration
	rm -f ./bias_control
