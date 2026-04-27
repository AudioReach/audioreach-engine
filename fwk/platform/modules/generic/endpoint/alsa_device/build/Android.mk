LOCAL_PATH := $(call my-dir)/..

ALSA_DEVICE_C_INCLUDES := \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/capi/inc \
    $(LOCAL_PATH)/capi/src \
    $(LOCAL_PATH)/lib/inc \
    $(LOCAL_PATH)/lib/src/tinyalsa \
    ${PROJECT_SOURCE_DIR}/fwk/spf/utils/interleaver/inc

ALSA_DEVICE_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/capi/inc

ALSA_DEVICE_SRC_FILES := \
    capi/src/capi_alsa_device.c \
    lib/src/tinyalsa/alsa_device_driver.c

ALSA_DEVICE_CFLAGS    += -O3 -Wall -ffixed-x18

ALSA_DEVICE_CFLAGS_32 += -mfpu=neon
ALSA_DEVICE_CFLAGS_64 += -march=armv8-a+crypto

ALSA_DEVICE_SHARED_LIBS := \
    liblx-osal \
    libar-gpr \
    libtinyalsa

ALSA_DEVICE_HEADER_LIBS := \
    libspf_api \
    libposal_headers \
    libspf_interfaces_headers \
    libspf_utils_headers

ALSA_DEVICE_STATIC_LIBS := \
    libposal \
    libspf_interfaces \
    libspf_utils

#####################################
# Module: ALSA DEVICE SINK
#####################################
include $(CLEAR_VARS)
LOCAL_MODULE                      := lib_alsa_device_sink
LOCAL_VENDOR_MODULE               := true
LOCAL_MODULE_TAGS                 := optional

LOCAL_C_INCLUDES                  := $(ALSA_DEVICE_C_INCLUDES)
LOCAL_EXPORT_C_INCLUDE_DIRS       := $(ALSA_DEVICE_EXPORT_C_INCLUDE_DIRS)
LOCAL_SRC_FILES                   := $(ALSA_DEVICE_SRC_FILES)
LOCAL_CFLAGS                      := $(ALSA_DEVICE_CFLAGS)
LOCAL_CFLAGS_32                   := $(ALSA_DEVICE_CFLAGS_32)
LOCAL_CFLAGS_64                   := $(ALSA_DEVICE_CFLAGS_64)
LOCAL_SHARED_LIBRARIES            := $(ALSA_DEVICE_SHARED_LIBS)
LOCAL_HEADER_LIBRARIES            := $(ALSA_DEVICE_HEADER_LIBS)
LOCAL_STATIC_LIBRARIES            := $(ALSA_DEVICE_STATIC_LIBS)

LOCAL_SPF_MODULE_KCONFIG          := CONFIG_ALSA_DEVICE_SINK
LOCAL_SPF_MODULE_NAME             := $(LOCAL_MODULE)
LOCAL_SPF_MODULE_MAJOR_VER        := 1
LOCAL_SPF_MODULE_MINOR_VER        := 0
LOCAL_SPF_MODULE_AMDB_ITYPE       := "capi"
LOCAL_SPF_MODULE_AMDB_MTYPE       := "end_point"
LOCAL_SPF_MODULE_AMDB_MID         := "0x18000002"
LOCAL_SPF_MODULE_AMDB_TAG         := "capi_alsa_device_sink"
LOCAL_SPF_MODULE_AMDB_MOD_NAME    := "MODULE_ID_ALSA_DEVICE_SINK"
LOCAL_SPF_MODULE_QACT_MODULE_TYPE := ""
LOCAL_SPF_MODULE_AMDB_FMT_ID1     := "MODULE_ID_ALSA_DEVICE_SINK"
LOCAL_SPF_MODULE_H2XML_HEADERS    := "$(LOCAL_PATH)/api/alsa_device_api.h"

include $(BUILD_ARE_MODULES)

#####################################
# Module: ALSA DEVICE SOURCE
#####################################
include $(CLEAR_VARS)
LOCAL_MODULE                      := lib_alsa_device_source
LOCAL_VENDOR_MODULE               := true
LOCAL_MODULE_TAGS                 := optional

LOCAL_C_INCLUDES                  := $(ALSA_DEVICE_C_INCLUDES)
LOCAL_EXPORT_C_INCLUDE_DIRS       := $(ALSA_DEVICE_EXPORT_C_INCLUDE_DIRS)
LOCAL_SRC_FILES                   := $(ALSA_DEVICE_SRC_FILES)
LOCAL_CFLAGS                      := $(ALSA_DEVICE_CFLAGS)
LOCAL_CFLAGS_32                   := $(ALSA_DEVICE_CFLAGS_32)
LOCAL_CFLAGS_64                   := $(ALSA_DEVICE_CFLAGS_64)
LOCAL_CPPFLAGS                    := $(ALSA_DEVICE_CPPFLAGS)
LOCAL_SHARED_LIBRARIES            := $(ALSA_DEVICE_SHARED_LIBS)
LOCAL_HEADER_LIBRARIES            := $(ALSA_DEVICE_HEADER_LIBS)
LOCAL_STATIC_LIBRARIES            := $(ALSA_DEVICE_STATIC_LIBS)

LOCAL_SPF_MODULE_KCONFIG          := CONFIG_ALSA_DEVICE_SOURCE
LOCAL_SPF_MODULE_NAME             := $(LOCAL_MODULE)
LOCAL_SPF_MODULE_MAJOR_VER        := 1
LOCAL_SPF_MODULE_MINOR_VER        := 0
LOCAL_SPF_MODULE_AMDB_ITYPE       := "capi"
LOCAL_SPF_MODULE_AMDB_MTYPE       := "end_point"
LOCAL_SPF_MODULE_AMDB_MID         := "0x18000003"
LOCAL_SPF_MODULE_AMDB_TAG         := "capi_alsa_device_source"
LOCAL_SPF_MODULE_AMDB_MOD_NAME    := "MODULE_ID_ALSA_DEVICE_SOURCE"
LOCAL_SPF_MODULE_QACT_MODULE_TYPE := ""
LOCAL_SPF_MODULE_AMDB_FMT_ID1     := "MODULE_ID_ALSA_DEVICE_SOURCE"
LOCAL_SPF_MODULE_H2XML_HEADERS    := "$(LOCAL_PATH)/api/alsa_device_api.h"

include $(BUILD_ARE_MODULES)
