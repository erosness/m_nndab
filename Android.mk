
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES	:= external/nanomsg/src
LOCAL_SRC_FILES		:= nndab.c
LOCAL_MODULE_TAGS	:= optional
LOCAL_MODULE		:= nndab
LOCAL_SHARED_LIBRARIES	:= libnanomsg

include $(BUILD_EXECUTABLE)
