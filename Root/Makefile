CXX := `root-config --cxx`
ROOTCLING=rootcling
MARCH := `root-config --arch`
LD:=$(CXX)

SRC=./src/

UNAME := $(shell uname)

CFLAGS += $(shell root-config --ldflags --cflags --glibs) -g -fPIC
OPTFLAGS += -Ofast -mtune=native -march=native -w -funroll-loops -D_GLIBCXX_PARALLEL

ifeq ($(shell uname -s),Linux) # LINUX
OPTFLAGS += -fopenmp
endif

default: miniTRB_compress

all: miniTRB_compress miniTRB_clusterize test

miniTRB_compress: ./src/miniTRB_compress.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS)

miniTRB_clusterize: ./src/miniTRB_clusterize.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS)

test: ./src/test.cxx
	$(CXX) -o$@ $< $(CFLAGS) $(OPTFLAGS)

clean:
	rm -f ./miniTRB_compress
	rm -f ./miniTRB_clusterize
	rm -f ./test
