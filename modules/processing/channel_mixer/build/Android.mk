LOCAL_PATH := $(call my-dir)/..

#####################################
# Module: CH MIXER
#####################################
include $(CLEAR_VARS)
LOCAL_MODULE             := lib_chmixer
LOCAL_MODULE_TAGS        := optional
LOCAL_VENDOR_MODULE      := true

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/capi/inc \
    $(LOCAL_PATH)/capi/src \
    $(LOCAL_PATH)/lib/inc

LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/capi/inc \
    $(LOCAL_PATH)/lib/inc

LOCAL_SRC_FILES := \
    capi/src/capi_chmixer.cpp \
    capi/src/capi_chmixer_utils.cpp \
    lib/src/ChannelMixerLib.c \
    lib/src/ChannelMixerLib_island.c \
    lib/src/ChannelMixerRemapRules.c

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

LOCAL_SPF_MODULE_KCONFIG       := CONFIG_CHMIXER
LOCAL_SPF_MODULE_NAME          := $(LOCAL_MODULE)
LOCAL_SPF_MODULE_MAJOR_VER     := 1
LOCAL_SPF_MODULE_MINOR_VER     := 0
LOCAL_SPF_MODULE_AMDB_ITYPE    := "capi"
LOCAL_SPF_MODULE_AMDB_MTYPE    := "PP"
LOCAL_SPF_MODULE_AMDB_MID      := "0x07001013"
LOCAL_SPF_MODULE_AMDB_TAG      := "capi_chmixer"
LOCAL_SPF_MODULE_AMDB_MOD_NAME := "MODULE_ID_CHMIXER"
LOCAL_SPF_MODULE_H2XML_HEADERS := "$(LOCAL_PATH)/api/chmixer_api.h"

include $(BUILD_ARE_MODULES)
