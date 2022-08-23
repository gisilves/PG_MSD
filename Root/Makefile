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

all: miniTRB_convert FOOT_convert PAPERO_convert PAPERO_info raw_clusterize raw_threshold_scan raw_cn raw_viewer calibration bias_control bias_controlPI ASTRA_convert

.PHONY: raw_viewer

miniTRB_convert: ./src/miniTRB_convert.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

FOOT_convert: ./src/FOOT_convert.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

PAPERO_convert: ./src/PAPERO_convert.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

ASTRA_convert: ./src/ASTRA_convert.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

PAPERO_info: ./src/PAPERO_info.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)	

raw_clusterize: ./src/raw_clusterize.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

raw_threshold_scan: ./src/raw_threshold_scan.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

raw_cn: ./src/raw_cn.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

calibration: ./src/calibration.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS) $(ANYOPTION)

raw_viewer: 
	$(ROOTCLING) -f guiDict.cxx ./src/viewerGUI.hh ./src/udpSocket.cpp ./src/guiLinkDef.h
	$(CXX) ./src/viewerGUI.cxx guiDict.cxx  -o$@ $< $(CFLAGS) $(OPTFLAGS)

bias_control: 
	$(ROOTCLING) -f guiDict.cxx ./src/biascontrol.hh ./src/guiLinkDef.h
	$(CXX) ./src/biascontrol.cxx guiDict.cxx  -o$@ $< $(CFLAGS) $(OPTFLAGS)

bias_controlPI: 
	$(ROOTCLING) -f guiDict.cxx ./src/biascontrolPI.hh ./src/guiLinkDef.h
	$(CXX) ./src/biascontrolPI.cxx guiDict.cxx  -o$@ $< $(CFLAGS) $(OPTFLAGS)

clean:
	rm -f ./miniTRB_convert
	rm -f ./FOOT_convert
	rm -f ./PAPERO_convert
	rm -f ./PAPERO_info
	rm -f ./ASTRA_convert
	rm -f ./raw_clusterize
	rm -f ./raw_cn
	rm -f ./raw_threshold_scan
	rm -f ./raw_viewer
	rm -f ./calibration
	rm -f ./bias_control
	rm -f ./bias_controlPI
