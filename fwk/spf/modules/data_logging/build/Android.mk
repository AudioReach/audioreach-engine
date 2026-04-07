LOCAL_PATH := $(call my-dir)/..

#####################################
# Module: DATA LOGGING
#####################################
include $(CLEAR_VARS)
LOCAL_MODULE             := lib_data_logging
LOCAL_MODULE_TAGS        := optional
LOCAL_VENDOR_MODULE      := true

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/capi/inc \
    $(LOCAL_PATH)/capi/src \
    $(LOCAL_PATH)/api

LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/capi/inc

LOCAL_SRC_FILES := \
    capi/src/capi_data_logging.c \
    capi/src/capi_data_logging_island.c \
    capi/src/capi_data_logging_config_utils.c

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
    libspf_utils_headers

LOCAL_STATIC_LIBRARIES := \
    libposal \
    libspf_interfaces \
    libspf_utils

LOCAL_SPF_MODULE_KCONFIG          := CONFIG_DATA_LOGGING
LOCAL_SPF_MODULE_NAME             := $(LOCAL_MODULE)
LOCAL_SPF_MODULE_MAJOR_VER        := 1
LOCAL_SPF_MODULE_MINOR_VER        := 0
LOCAL_SPF_MODULE_AMDB_ITYPE       := "capi"
LOCAL_SPF_MODULE_AMDB_MTYPE       := "generic"
LOCAL_SPF_MODULE_AMDB_MID         := "0x0700101a"
LOCAL_SPF_MODULE_AMDB_TAG         := "capi_data_logging"
LOCAL_SPF_MODULE_AMDB_MOD_NAME    := "MODULE_ID_DATA_LOGGING"
LOCAL_SPF_MODULE_QACT_MODULE_TYPE := ""
LOCAL_SPF_MODULE_AMDB_FMT_ID1     := "MODULE_ID_DATA_LOGGING"
LOCAL_SPF_MODULE_H2XML_HEADERS    := "$(LOCAL_PATH)/api/data_logging_api.h"

include $(BUILD_ARE_MODULES)
