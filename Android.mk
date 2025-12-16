LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := test_dqbuf
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)


LOCAL_SRC_FILES := test_dqbuf.c


LOCAL_CFLAGS := \
	-Wall \
	-Werror \
	-Wno-unused-parameter \
	-DANDROID \
	-D_FILE_OFFSET_BITS=64


LOCAL_SHARED_LIBRARIES := \
	libc \
	liblog \
	libcutils \
	libutils 

include $(BUILD_EXECUTABLE)



include $(CLEAR_VARS)
LOCAL_MODULE := consumer
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)


LOCAL_SRC_FILES :=  consumer.c


LOCAL_CFLAGS := \
	-Wall \
	-Werror \
	-Wno-unused-parameter \
	-DANDROID \
	-D_FILE_OFFSET_BITS=64


LOCAL_SHARED_LIBRARIES := \
	libc \
	liblog \
	libcutils \
	libutils 

include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)
LOCAL_MODULE := producer
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)


LOCAL_SRC_FILES :=  producer.c


LOCAL_CFLAGS := \
	-Wall \
	-Werror \
	-Wno-unused-parameter \
	-DANDROID \
	-D_FILE_OFFSET_BITS=64


LOCAL_SHARED_LIBRARIES := \
	libc \
	liblog \
	libcutils \
	libutils 

include $(BUILD_EXECUTABLE)



