RMR ?=rm -f
RANLIB ?=ranlib

LDLIBS += -lstdc++
CFLAGS += -Iinclude
CXXFLAGS += $(CFLAGS)

ifneq ($(TARGET),)
CC := $(TARGET)-gcc
LD := $(TARGET)-ld
AR := $(TARGET)-ar
CXX := $(TARGET)-g++
endif

BUILD_TARGET := "UNKOWN"

ifeq ($(LOGNAME),)
BUILD_TARGET := "mingw"
else
BUILD_TARGET := $(findstring mingw, $(CC))
endif

ifeq ($(BUILD_TARGET),)
BUILD_TARGET := $(shell uname)
endif

ifeq ($(BUILD_TARGET), mingw)
TARGETS = txcat.exe netcat.exe libtx.a
LDFLAGS += -static
LDLIBS += -lws2_32
else
TARGETS = txget txcat libtx.a
endif

ifeq ($(BUILD_TARGET), Linux)
LDLIBS += -lrt
endif

XCLEANS = txcat.o ncatutil.o
COREOBJ = tx_loop.o tx_timer.o tx_socket.o tx_platform.o tx_aiocb.o tx_debug.o
OBJECTS = $(COREOBJ) tx_poll.o tx_select.o \
		  tx_epoll.o tx_kqueue.o tx_completion_port.o

all: $(TARGETS)

txcat.exe: txcat.o $(OBJECTS)
	$(CC) $(LDFLAGS) -o txcat.exe txcat.o $(OBJECTS) $(LDLIBS)

netcat.exe: netcat.o ncatutil.o $(OBJECTS)
	$(CC) $(LDFLAGS) -o netcat.exe netcat.o ncatutil.o $(OBJECTS) $(LDLIBS)

txcat: txcat.o $(OBJECTS)
txget: txget.o $(OBJECTS)

txhttpfwd: txhttpfwd.o $(OBJECTS)

libtx.a: $(OBJECTS)
	$(AR) crv libtx.a $(OBJECTS)
	$(RANLIB) libtx.a

.PHONY: clean

clean:
	$(RM) $(OBJECTS) $(TARGETS) $(XCLEANS)

