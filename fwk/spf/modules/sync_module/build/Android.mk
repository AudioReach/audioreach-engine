LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := capi_sync_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/capi/inc

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := capi_sync
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/capi/inc \
    $(LOCAL_PATH)/capi/src \
    $(LOCAL_PATH)/api

LOCAL_SRC_FILES := \
    capi/src/capi_sync.c \
    capi/src/capi_sync_control_utils.c \
    capi/src/capi_sync_data_utils.c \
    capi/src/capi_sync_event_utils.c \
    capi/src/capi_sync_port_utils.c \
    capi/src/capi_sync_process_ec_mode.c \
    capi/src/capi_sync_process_generic.c \
    capi/src/capi_sync_utils.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17 -g

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libdiag

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_api libspf_interfaces_headers libspf_utils_headers libapm_headers libamdb_headers
LOCAL_STATIC_LIBRARIES := libposal libspf_interfaces libapm libamdb

include $(BUILD_STATIC_LIBRARY)

