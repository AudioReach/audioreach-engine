LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := lib_capi_gain_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/api

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := lib_capi_gain
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/src

LOCAL_SRC_FILES := \
    src/capi_gain.c \
    src/capi_gain_utils.c


LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_interfaces_headers libspf_api
LOCAL_STATIC_LIBRARIES := libposal libspf_interfaces
LOCAL_WHOLE_STATIC_LIBRARIES := lib_spm_cmn_utils lib_vol_ctrl_lib

LOCAL_SPF_MODULE_KCONFIG          := CONFIG_GAIN
LOCAL_SPF_MODULE_NAME             := $(LOCAL_MODULE)
LOCAL_SPF_MODULE_MAJOR_VER        := 1
LOCAL_SPF_MODULE_MINOR_VER        := 0
LOCAL_SPF_MODULE_AMDB_ITYPE       := "capi"
LOCAL_SPF_MODULE_AMDB_MTYPE       := "pp"
LOCAL_SPF_MODULE_AMDB_MID         := "0x07001002"
LOCAL_SPF_MODULE_AMDB_TAG         := "capi_gain"
LOCAL_SPF_MODULE_AMDB_MOD_NAME    := "MODULE_ID_GAIN"
LOCAL_SPF_MODULE_QACT_MODULE_TYPE := ""
LOCAL_SPF_MODULE_AMDB_FMT_ID1     := "MODULE_ID_GAIN"
LOCAL_SPF_MODULE_H2XML_HEADERS    := "$(LOCAL_PATH)/api/gain_api.h"

include $(BUILD_ARE_MODULES)
