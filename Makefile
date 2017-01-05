MODULE := libtx
RANLIB ?= ranlib
THIS_PATH := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

LOCAL_TARGETS := txcat libtx.a
LOCAL_CXXFLAGS := -I$(THIS_PATH)/include
LOCAL_CFLAGS := $(LOCAL_CXXFLAGS)
LOCAL_LDLIBS := -lstdc++

ifeq ($(BUILD_TARGET), mingw)
LOCAL_LDFLAGS += -static
LOCAL_LDLIBS += -lws2_32
endif

ifeq ($(BUILD_TARGET), Linux)
LOCAL_LDLIBS += -lrt
endif

VPATH += $(THIS_PATH)

LOCAL_COREOBJ = tx_loop.o tx_timer.o tx_platform.o tx_aiocb.o tx_debug.o
LOCAL_OBJECTS = $(LOCAL_COREOBJ) tx_poll.o tx_epoll.o tx_kqueue.o tx_completion_port.o

all: $(LOCAL_TARGETS)

$(LOCAL_TARGETS): OBJECTS := $(LOCAL_OBJECTS)
$(LOCAL_TARGETS): CFLAGS  := $(LOCAL_CFLAGS) $(CFLAGS)
$(LOCAL_TARGETS): CXXFLAGS := $(LOCAL_CXXFLAGS) $(CXXFLAGS)

$(LOCAL_TARGETS): LDLIBS   := $(LOCAL_LDLIBS) $(LDLIBS)
$(LOCAL_TARGETS): LDFLAGS  := $(LOCAL_LDFLAGS) $(LDFLAGS)

netcat: netcat.o ncatutil.o $(OBJECTS)

txcat: txcat.o $(LOCAL_OBJECTS)

txget: txget.o $(LOCAL_OBJECTS)

libtx.a: $(LOCAL_OBJECTS)
	$(AR) crv $@ $(OBJECTS)
	$(RANLIB) $@

$(MODULE).clean:
	$(RM) $(OBJECTS) $(TARGETS)

