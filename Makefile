CXX := `root-config --cxx`
ROOTCLING=rootcling
MARCH := `root-config --arch`
LD:=$(CXX)
SRC=./src/

ANYOPTION=$(SRC)/anyoption.cpp

UNAME := $(shell uname)

CFLAGS += $(shell root-config --cflags --glibs) -g -fPIC -pthread -I$(ROOTSYS)/include
OPTFLAGS += -O3

default: all

all: PAPERO_convert PAPERO_info raw_clusterize raw_viewer calibration bias_control

.PHONY: raw_viewer

miniTRB_convert: ./src/miniTRB_convert.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

FOOT_convert: ./src/FOOT_convert.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

PAPERO_convert: ./src/PAPERO_convert.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

AMS_convert: ./src/AMS_convert.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

ASTRA_convert: ./src/ASTRA_convert.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

ASTRA_info: ./src/ASTRA_info.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

PAPERO_info: ./src/PAPERO_info.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)	

raw_clusterize: ./src/raw_clusterize.cpp
	$(CXX) ./src/event.cpp -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

raw_threshold_scan: ./src/raw_threshold_scan.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

raw_cn: ./src/raw_cn.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

calibration: ./src/calibration.cpp
	$(CXX) ./src/event.cpp -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

raw_viewer: 
	$(ROOTCLING) -f guiDict.cpp ./src/viewerGUI.h ./src/guiLinkDef.h
	$(CXX) ./src/viewerGUI.cpp ./src/event.cpp guiDict.cpp  -o$@ $< $(CFLAGS) $(OPTFLAGS)

bias_control: 
	$(ROOTCLING) -f guiDict.cpp ./src/biascontrol.h ./src/guiLinkDef.h
	$(CXX) ./src/biascontrol.cpp ./src/event.cpp guiDict.cpp  -o$@ $< $(CFLAGS) $(OPTFLAGS)

bias_controlPI: 
	$(ROOTCLING) -f guiDict.cpp ./src/biascontrolPI.h ./src/guiLinkDef.h
	$(CXX) ./src/biascontrolPI.cpp guiDict.cpp  -o$@ $< $(CFLAGS) $(OPTFLAGS)

clean:
	rm -f ./miniTRB_convert
	rm -f ./FOOT_convert
	rm -f ./PAPERO_convert
	rm -f ./PAPERO_info
	rm -f ./ASTRA_convert
	rm -f ./AMS_convert
	rm -f ./raw_clusterize
	rm -f ./raw_cn
	rm -f ./raw_threshold_scan
	rm -f ./raw_viewer
	rm -f ./calibration
	rm -f ./bias_control
	rm -f ./bias_controlPI
