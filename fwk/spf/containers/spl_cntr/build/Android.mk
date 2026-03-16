LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libspf_spl_cntr_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/cmn/inc \
    $(LOCAL_PATH)/ext/cntr_proc_dur_fwk_ext/inc \
    $(LOCAL_PATH)/ext/voice_delivery_fwk_ext/inc \
    $(LOCAL_PATH)/ext/sync_fwk_ext/inc


LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libspf_spl_cntr
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/cmn/inc \
    $(LOCAL_PATH)/core/src \
    $(LOCAL_PATH)/ext/cntr_proc_dur_fwk_ext/inc \
    $(LOCAL_PATH)/ext/voice_delivery_fwk_ext/inc \
    $(LOCAL_PATH)/ext/sync_fwk_ext/inc


LOCAL_SRC_FILES := \
    core/src/spl_cntr.c \
    core/src/spl_cntr_buf_util.c \
    core/src/spl_cntr_cmd_handler.c \
    core/src/spl_cntr_data_handler.c \
    core/src/spl_cntr_eos_util.c \
    core/src/spl_cntr_event_util.c \
    core/src/spl_cntr_ext_port_util.c \
    core/src/spl_cntr_fwk_extns.c \
    core/src/spl_cntr_gpd.c \
    core/src/spl_cntr_pm.c \
    ext/cntr_proc_dur_fwk_ext/src/spl_cntr_proc_dur_fwk_ext.c \
    ext/sync_fwk_ext/src/spl_cntr_sync_fwk_ext.c \
    ext/voice_delivery_fwk_ext/src/spl_cntr_voice_delivery_fwk_ext.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_utils_headers libspf_api libspf_interfaces_headers libspf_gu_headers libspf_gen_topo_headers libspf_spl_topo_headers libspf_topo_utils_headers libspf_icb_headers libapm_headers libspf_cu_headers libamdb_headers libprivate_api_headers libplatform_cfg_headers
LOCAL_STATIC_LIBRARIES := libspf_utils libspf_interfaces libposal libspf_gu libspf_gen_topo libspf_topo_utils libspf_icb libapm libspf_cu libamdb

include $(BUILD_STATIC_LIBRARY)

