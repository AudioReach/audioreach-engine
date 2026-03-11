LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libspf_interfaces_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/fwk/api \
    $(LOCAL_PATH)/module/audio_cmn_lib \
    $(LOCAL_PATH)/module/capi \
    $(LOCAL_PATH)/module/capi/adv \
    $(LOCAL_PATH)/module/capi_cmn/cmn/inc \
    $(LOCAL_PATH)/module/capi_cmn/ctrl_port/inc \
    $(LOCAL_PATH)/module/capi_libraries/capi_library_internal_buffer/inc \
    $(LOCAL_PATH)/module/capi_libraries/capi_library_thread_pool/inc \
    $(LOCAL_PATH)/module/imcl/api \
    $(LOCAL_PATH)/module/internal_api/modules_prv/api \
    $(LOCAL_PATH)/module/metadata/api \
    $(LOCAL_PATH)/module/offload/api \
    $(LOCAL_PATH)/module/shared_lib_api/inc \
    $(LOCAL_PATH)/module/shared_lib_api/inc/generic

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libspf_interfaces
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/fwk/api \
    $(LOCAL_PATH)/module/audio_cmn_lib \
    $(LOCAL_PATH)/module/capi \
    $(LOCAL_PATH)/module/capi/adv \
    $(LOCAL_PATH)/module/capi_cmn/cmn/inc \
    $(LOCAL_PATH)/module/capi_cmn/ctrl_port/inc \
    $(LOCAL_PATH)/module/capi_libraries/capi_library_internal_buffer/inc \
    $(LOCAL_PATH)/module/capi_libraries/capi_library_thread_pool/inc \
    $(LOCAL_PATH)/module/imcl/api \
    $(LOCAL_PATH)/module/internal_api/modules_prv/api \
    $(LOCAL_PATH)/module/metadata/api \
    $(LOCAL_PATH)/module/offload/api \
    $(LOCAL_PATH)/module/shared_lib_api/inc

LOCAL_SRC_FILES := \
    module/capi_cmn/cmn/src/capi_cmn.c \
    module/capi_cmn/cmn/src/capi_cmn_island.c \
    module/capi_cmn/ctrl_port/src/capi_cmn_ctrl_port_list.c \
    module/capi_cmn/ctrl_port/src/capi_cmn_imcl_utils.c \
    module/capi_cmn/ctrl_port/src/capi_cmn_imcl_utils_island.c \
    module/capi_libraries/capi_library_internal_buffer/src/capi_library_internal_buffer.c \
    module/capi_libraries/capi_library_thread_pool/src/capi_library_thread_pool.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_HEADER_LIBRARIES := libarosal_headers libposal_headers libspf_api libspf_utils_headers libamdb_headers
LOCAL_STATIC_LIBRARIES := libposal libspf_utils libamdb

include $(BUILD_STATIC_LIBRARY)
