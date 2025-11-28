CXX := `root-config --cxx`
ROOTCLING=rootcling
MARCH := `root-config --arch`
LD:=$(CXX)
SRC=./src/

UNAME := $(shell uname)
CLI11_DIR ?= third_party/CLI11/include/CLI

CFLAGS += $(shell root-config --cflags --glibs) -g -fPIC -pthread -I$(ROOTSYS)/include -I$(CLI11_DIR)
OPTFLAGS += -O3


default: all

all: miniTRB_convert FOOT_convert PAPERO_convert AMS_convert PAPERO_info PAPERO_i2c raw_clusterize raw_cn raw_threshold_scan calibration readOM bias_control bias_controlPI

.PHONY: raw_viewer

miniTRB_convert: ./src/miniTRB_convert.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS)

FOOT_convert: ./src/FOOT_convert.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS)

PAPERO_convert: ./src/PAPERO_convert.cpp
	$(CXX) ./src/PAPERO.cpp -o$@ $< $(CFLAGS) $(OPTFLAGS)

AMS_convert: ./src/AMS_convert.cpp
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS)

PAPERO_info: ./src/PAPERO_info.cpp
	$(CXX) ./src/PAPERO.cpp -o$@ $< $(CFLAGS) $(OPTFLAGS)	

PAPERO_i2c: ./src/PAPERO_i2c.cpp
	$(CXX) ./src/PAPERO.cpp -o$@ $< $(CFLAGS) $(OPTFLAGS)	

raw_clusterize: ./src/raw_clusterize.cpp
	$(CXX) ./src/event.cpp -o$@ $< $(CFLAGS) $(OPTFLAGS)

raw_cn: ./src/raw_cn.cpp
	$(CXX) ./src/event.cpp -o$@ $< $(CFLAGS) $(OPTFLAGS)

raw_threshold_scan: ./src/raw_threshold_scan.cpp
	$(CXX) ./src/event.cpp -o$@ $< $(CFLAGS) $(OPTFLAGS)

calibration: ./src/calibration.cpp
	$(CXX) ./src/event.cpp -o$@ $< $(CFLAGS) $(OPTFLAGS)

raw_viewer: 
	$(ROOTCLING) -f guiDict.cpp ./src/viewerGUI.h ./src/udpSocket.cpp ./src/guiLinkDef.h
	$(CXX) ./src/viewerGUI.cpp ./src/event.cpp guiDict.cpp  -o$@ $< $(CFLAGS) $(OPTFLAGS)

readOM: ./src/readOM.cpp
	$(CXX) ./src/udpSocket.cpp -o$@ $< $(CFLAGS) $(OPTFLAGS)	

bias_control: 
	$(ROOTCLING) -f guiDict.cpp ./src/biascontrol.h ./src/guiLinkDef.h
	$(CXX) ./src/biascontrol.cpp ./src/event.cpp guiDict.cpp  -o$@ $< $(CFLAGS) $(OPTFLAGS)

bias_controlPI: 
	$(ROOTCLING) -f guiDict.cpp ./src/biascontrolPI.h ./src/guiLinkDef.h
	$(CXX) ./src/biascontrolPI.cpp guiDict.cpp  -o$@ $< $(CFLAGS) $(OPTFLAGS)

clean:
	rm -f PAPERO_convert
	rm -f PAPERO_info
	rm -f raw_clusterize
	rm -f raw_cn
	rm -f raw_threshold_scan
	rm -f calibration
	rm -f raw_viewer
	rm -f readOM
	rm -f bias_control
	rm -f bias_controlPI
	rm -f miniTRB_convert
	rm -f FOOT_convert
	rm -f AMS_convert