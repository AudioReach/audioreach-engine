LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libspf_spl_topo_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/ext/dm_fwk_ext/inc \
    $(LOCAL_PATH)/ext/mimo_proc_state_intf_ext/inc \
    $(LOCAL_PATH)/ext/sync_fwk_ext/inc \
    $(LOCAL_PATH)/ext/trigger_policy_fwk_ext/inc \
    $(LOCAL_PATH)/../topo_interface/inc

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libspf_spl_topo
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/core/src \
    $(LOCAL_PATH)/ext/dm_fwk_ext/inc \
    $(LOCAL_PATH)/ext/mimo_proc_state_intf_ext/inc \
    $(LOCAL_PATH)/ext/sync_fwk_ext/inc \
    $(LOCAL_PATH)/ext/trigger_policy_fwk_ext/inc \
    $(LOCAL_PATH)/../topo_interface/inc

LOCAL_SRC_FILES := \
    core/src/simp_topo.c \
    core/src/spl_topo.c \
    core/src/spl_topo_buf_utils.c \
    core/src/spl_topo_capi.c \
    core/src/spl_topo_capi_cb_handler.c \
    core/src/spl_topo_data_process.c \
    core/src/spl_topo_media_format_utils.c \
    core/src/spl_topo_metadata.c \
    core/src/spl_topo_pm.c \
    core/src/spl_topo_utils.c \
    ext/dm_fwk_ext/src/spl_topo_dm_fwk_ext.c \
    ext/mimo_proc_state_intf_ext/src/spl_topo_mimo_proc_state_intf_extn.c \
    ext/sync_fwk_ext/src/spl_topo_sync_fwk_ext.c \
    ext/trigger_policy_fwk_ext/src/spl_topo_trigger_policy_fwk_ext.c


LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_utils_headers libspf_api libspf_interfaces_headers libspf_gu_headers libamdb_headers libspf_topo_utils_headers libirm_headers libspf_gen_topo_headers
LOCAL_STATIC_LIBRARIES := libspf_utils libspf_interfaces libposal libspf_gu libamdb libspf_topo_utils

include $(BUILD_STATIC_LIBRARY)

