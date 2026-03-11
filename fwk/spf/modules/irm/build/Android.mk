LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libirm_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/inc/linux \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/api

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libirm
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/inc/linux \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/api \
    $(LOCAL_PATH)/src

LOCAL_SRC_FILES := \
    src/irm.c \
    src/irm_apm_if_utils.c \
    src/irm_cmd_handler.c \
    src/irm_cntr_if_utils.c \
    src/irm_list_utils.c \
    src/irm_offload_utils.c \
    src/irm_sim_utils.c \
    src/irm_static_module_utils.c \
    src/linux/irm_prof_driver.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17 -g

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libdiag \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_api libspf_interfaces_headers libspf_utils_headers libapm_headers libamdb_headers
LOCAL_STATIC_LIBRARIES := libposal libspf_interfaces libapm libamdb

include $(BUILD_STATIC_LIBRARY)
