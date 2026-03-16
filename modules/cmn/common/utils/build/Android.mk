LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := lib_spm_cmn_utils_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/inc

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := lib_spm_cmn_utils
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/inc

LOCAL_SRC_FILES := \
    src/basic_op.c


LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/inc

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_interfaces_headers libspf_api
LOCAL_STATIC_LIBRARIES := libposal libspf_interfaces

include $(BUILD_STATIC_LIBRARY)
