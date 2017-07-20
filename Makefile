MODULE := libtx
RANLIB ?= ranlib
THIS_PATH := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

LOCAL_TARGETS := txcat libtx.a
LOCAL_CXXFLAGS := -I$(THIS_PATH)/include
LOCAL_CFLAGS := $(LOCAL_CXXFLAGS)
LOCAL_LDLIBS := -lstdc++

ifeq ($(BUILD_TARGET), Linux)
LOCAL_LDLIBS += -lrt
endif

VPATH += $(THIS_PATH)

LOCAL_COREOBJ = tx_loop.o tx_timer.o tx_platform.o tx_aiocb.o tx_debug.o
LOCAL_OBJECTS = $(LOCAL_COREOBJ) tx_poll.o tx_epoll.o tx_kqueue.o tx_completion_port.o

CFLAGS += $(LOCAL_CFLAGS)
CXXFLAGS += $(LOCAL_CXXFLAGS)

LDLIBS += $(LOCAL_LDLIBS)
LDFLAGS += $(LOCAL_LDFLAGS)

libtx.a: $(LOCAL_OBJECTS)
	echo $(AR) crv $@ $^
	$(AR) crv $@ $^
	echo $(RANLIB) $@
	$(RANLIB) $@

netcat: netcat.o ncatutil.o $(LOCAL_OBJECTS)

txcat: txcat.o $(LOCAL_OBJECTS)

txget: txget.o $(LOCAL_OBJECTS)

