LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := lib_capi_history_buffer_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/capi/inc \
    $(LOCAL_PATH)/api

LOCAL_VENDOR_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := lib_capi_history_buffer
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/capi/inc \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/capi/src

LOCAL_SRC_FILES := \
    capi/src/capi_history_buffer_imcl_utils.c\
    capi/src/capi_history_buffer_utils.c\
    capi/src/capi_history_buffer.c


LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_interfaces_headers libspf_api
LOCAL_STATIC_LIBRARIES := libposal libspf_interfaces

LOCAL_SPF_MODULE_KCONFIG          := CONFIG_HISTORY_BUFFER
LOCAL_SPF_MODULE_NAME             := $(LOCAL_MODULE)
LOCAL_SPF_MODULE_MAJOR_VER        := 1
LOCAL_SPF_MODULE_MINOR_VER        := 0
LOCAL_SPF_MODULE_AMDB_ITYPE       := "capi"
LOCAL_SPF_MODULE_AMDB_MTYPE       := "Detector"
LOCAL_SPF_MODULE_AMDB_MID         := "0x07001182"
LOCAL_SPF_MODULE_AMDB_TAG         := "capi_history_buffer"
LOCAL_SPF_MODULE_AMDB_MOD_NAME    := "MODULE_ID_HISTORY_BUFFER"
LOCAL_SPF_MODULE_QACT_MODULE_TYPE := ""
LOCAL_SPF_MODULE_AMDB_FMT_ID1     := "MODULE_ID_HISTORY_BUFFER"
LOCAL_SPF_MODULE_H2XML_HEADERS    := "$(LOCAL_PATH)/api/history_buffer_api.h"

include $(BUILD_ARE_MODULES)