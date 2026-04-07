LOCAL_PATH := $(call my-dir)/..

#####################################
# Module: FIR_FILTER
#####################################
include $(CLEAR_VARS)
LOCAL_MODULE             := lib_fir
LOCAL_VENDOR_MODULE      := true
LOCAL_MODULE_TAGS        := optional

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/capi/inc \
    $(LOCAL_PATH)/capi/src \
    $(LOCAL_PATH)/lib/inc \
    $(LOCAL_PATH)/lib/src \
    $(PROJECT_SOURCE_DIR)/modules/cmn/common/utils/inc

LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/capi/inc \
    $(LOCAL_PATH)/lib/inc

LOCAL_SRC_FILES := \
    capi/src/capi_fir_filter.cpp \
    capi/src/capi_fir_filter_utils.cpp \
    capi/src/capi_fir_filter_utils_v2.cpp \
    capi/src/capi_fir_filter_xfade_utils.cpp \
    lib/src/fir_lib.c \
    lib/src/fir_lib_process.c

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

LOCAL_SPF_MODULE_KCONFIG          := CONFIG_FIR_FILTER
LOCAL_SPF_MODULE_NAME             := $(LOCAL_MODULE)
LOCAL_SPF_MODULE_MAJOR_VER        := 1
LOCAL_SPF_MODULE_MINOR_VER        := 0
LOCAL_SPF_MODULE_AMDB_ITYPE       := "capi"
LOCAL_SPF_MODULE_AMDB_MTYPE       := "pp"
LOCAL_SPF_MODULE_AMDB_MID         := "0x07001022"
LOCAL_SPF_MODULE_AMDB_TAG         := "capi_fir"
LOCAL_SPF_MODULE_AMDB_MOD_NAME    := "MODULE_ID_FIR_FILTER"
LOCAL_SPF_MODULE_QACT_MODULE_TYPE := ""
LOCAL_SPF_MODULE_AMDB_FMT_ID1     := "MODULE_ID_FIR_FILTER"
LOCAL_SPF_MODULE_H2XML_HEADERS    := "$(LOCAL_PATH)/api/api_fir.h"

include $(BUILD_ARE_MODULES)
