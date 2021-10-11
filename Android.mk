LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE := libtx
LOCAL_CFLAGS += -I$(LOCAL_PATH)/.. -I$(LOCAL_PATH)/include -fPIC
LOCAL_LDFLAGS += -fPIE -pie -llog

LOCAL_SRC_FILES := tx_loop.cpp tx_timer.cpp tx_platform.cpp tx_aiocb.cpp tx_debug.cpp tx_poll.cpp tx_epoll.cpp tx_kqueue.cpp tx_completion_port.cpp
# include $(BUILD_SHARED_LIBRARY)
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE := txcat
LOCAL_SRC_FILES := txcat.cpp
LOCAL_CFLAGS += -I$(LOCAL_PATH)/include -fPIC
LOCAL_LDFLAGS += -static
LOCAL_STATIC_LIBRARIES := libtx
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE := txget
LOCAL_SRC_FILES := txget.cpp
LOCAL_CFLAGS += -I$(LOCAL_PATH)/include -fPIC
LOCAL_LDFLAGS += -static
LOCAL_STATIC_LIBRARIES := libtx
include $(BUILD_EXECUTABLE)
