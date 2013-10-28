RMR ?=rm -f

LDLIBS += -lstdc++
CFLAGS += -Iinclude -D__BSD_VISIBLE -D_KERNEL
CXXFLAGS += $(CFLAGS)

BUILD_TARGET := "UNKOWN"

ifeq ($(LANG),)
BUILD_TARGET := "mingw"
else
BUILD_TARGET := $(findstring mingw, $(CC))
endif

ifeq ($(BUILD_TARGET),)
BUILD_TARGET := $(shell uname)
endif

ifeq ($(BUILD_TARGET), mingw)
CFLAGS += -Iwindows
LDLIBS += -lws2_32
endif

ifeq ($(BUILD_TARGET), Linux)
LDLIBS += -lrt
endif

TARGETS = txcat
XCLEANS = txcat.o
COREOBJ = tx_loop.o tx_timer.o tx_socket.o tx_platform.o
OBJECTS = $(COREOBJ) tx_poll.o tx_select.o \
		  tx_epoll.o tx_kqueue.o tx_completion_port.o

all: $(TARGETS)

txcat: txcat.o $(OBJECTS)

.PHONY: clean

clean:
	$(RM) $(OBJECTS) $(TARGETS) $(XCLEANS)

