CC      ?= gcc
CXX     ?= g++

CFLAGS   = -Wall -O2
CXXFLAGS = -Wall -O2
INCLUDES = -I. -Ipt

ifdef DEBUG
	CFLAGS   = -Wall -g -DDEBUG
	CXXFLAGS = -Wall -g -DDEBUG
endif

C_SRCS   = main.c
CPP_SRCS = can_hal.cpp

OBJS = main.o can_hal.o

TARGET = can_hal_test

all: $(TARGET)

debug:
	$(MAKE) DEBUG=1

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) -lpthread

clean:
	rm -f $(OBJS) $(TARGET)