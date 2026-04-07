LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := lib_spm_cmn_utils_headers
LOCAL_VENDOR_MODULE := true

LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/inc

include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := lib_spm_cmn_utils
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_TAGS := optional

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/inc

LOCAL_SRC_FILES := \
    src/basic_op.c \
    src/audio_basic_op_ext.c \
    src/divide_qx.c \
    src/divide_qx_island.c \
    src/crossfade.c \
    src/iir_tdf2.c \
    src/buffer_converter.c \
    src/audio_buffer.cpp \
    src/audio_buffer32.c \
    src/simple_mm.c \
    src/audio_panner.cpp \
    src/audio_buffer32_island.c \
    src/exp10.c \
    src/log10.c \
    src/apiir_df1_opt.c \
    src/mathlib.c

LOCAL_CFLAGS    += -O3 -Wall -ffixed-x18

LOCAL_CFLAGS_32 += -mfpu=neon
LOCAL_CFLAGS_64 += -march=armv8-a+crypto

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/inc

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_interfaces_headers libspf_api
LOCAL_STATIC_LIBRARIES := libposal libspf_interfaces

include $(BUILD_STATIC_LIBRARY)
