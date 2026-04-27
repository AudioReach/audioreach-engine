LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE        := lib_limiter
LOCAL_MODULE_TAGS   := optional
LOCAL_VENDOR_MODULE := true

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/lib/inc \
    $(LOCAL_PATH)/lib/src

LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/lib/inc \

LOCAL_SRC_FILES := \
    lib/src/limiter.c \
    lib/src/limiter24.c

LOCAL_CFLAGS    += -O3 -Wall -ffixed-x18

LOCAL_CFLAGS_32 += -mfpu=neon
LOCAL_CFLAGS_64 += -march=armv8-a+crypto

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := \
    libspf_api \
    libposal_headers \
    libspf_interfaces_headers \
    lib_spm_cmn_utils_headers \
    libspf_utils_headers

LOCAL_STATIC_LIBRARIES := \
    libposal \
    libspf_interfaces \
    lib_spm_cmn_utils \
    libspf_utils

include $(BUILD_STATIC_LIBRARY)
