CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CXXFLAGS += -g
else
	CXXFLAGS += -O2
endif

server: main.cc config.cc
	$(CXX) -o $@ $^ $(CXXFLAGS) -lpthread

clean:
	rm -r server