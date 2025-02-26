CC      ?= gcc
CXX     ?= g++

UTILS_DIR = utils

CFLAGS   = -Wall -O2 -MMD -MP
CXXFLAGS = -Wall -O2 -MMD -MP

INCLUDES = -I. -Ipt -I$(UTILS_DIR)

ifdef DEBUG
CFLAGS   := -Wall -g -DDEBUG -MMD -MP
CXXFLAGS := -Wall -g -DDEBUG -MMD -MP
endif

C_SRCS   = main.c can_hal.c
UTILS_SRCS = $(wildcard $(UTILS_DIR)/*.c)
CPP_SRCS =

OBJS     = $(C_SRCS:.c=.o)
OBJS    += $(UTILS_SRCS:.c=.o)
OBJS    += $(CPP_SRCS:.cpp=.o)

TARGET  = can_hal_test

.PHONY: all debug clean

all: $(TARGET)

debug:
	$(MAKE) DEBUG=1

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(UTILS_DIR)/%.o: $(UTILS_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) -lpthread

rebuild:
	$(MAKE) clean
	$(MAKE)

clean:
	rm -f $(OBJS) $(TARGET) $(OBJS:.o=.d)

-include $(OBJS:.o=.d)
