CXX := `root-config --cxx`
ROOTCLING=rootcling
MARCH := `root-config --arch`
LD:=$(CXX)

SRC=./src/

UNAME := $(shell uname)

CFLAGS += $(shell root-config --ldflags --cflags --glibs)

default: miniTRB_compress

all: miniTRB_compress miniTRB_clusterize test

miniTRB_compress: ./src/miniTRB_compress.cxx
	$(CXX) -o$@ $< $(CFLAGS)

miniTRB_clusterize: ./src/miniTRB_clusterize.cxx
	$(CXX) -o$@ $< $(CFLAGS)

test: ./src/test.cxx
	$(CXX) -o$@ $< $(CFLAGS)

clean:
	rm -f ./miniTRB_compress
	rm -f ./miniTRB_clusterize
	rm -f ./test