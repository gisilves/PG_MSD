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


default: miniTRB_compress

all: miniTRB_compress miniTRB_clusterize miniTRB_calibration miniTRB_threshold_scan miniTRB_cn miniTRB_viewer

.PHONY: all

miniTRB_compress: ./src/miniTRB_compress.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS)

miniTRB_clusterize: ./src/miniTRB_clusterize.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)
	
miniTRB_calibration: ./src/miniTRB_calibration.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

miniTRB_threshold_scan: ./src/miniTRB_threshold_scan.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS)

miniTRB_cn: ./src/miniTRB_cn.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

miniTRB_viewer: 
	$(ROOTCLING) -f guiDict.cxx ./src/viewerGUI.hh ./src/guiLinkDef.h
	$(CXX) ./src/viewerGUI.cxx guiDict.cxx  -o$@ $< $(CFLAGS) $(OPTFLAGS)

clean:
	rm -f ./miniTRB_compress
	rm -f ./miniTRB_clusterize
	rm -f ./miniTRB_calibration
	rm -f ./miniTRB_cn
	rm -f ./miniTRB_threshold_scan
	rm -f ./miniTRB_viewer