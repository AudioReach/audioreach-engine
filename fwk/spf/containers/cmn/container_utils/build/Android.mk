LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libspf_cu_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/core/inc/generic \
    $(LOCAL_PATH)/ext/async_cmd_handle/inc \
    $(LOCAL_PATH)/ext/ctrl_port/inc \
    $(LOCAL_PATH)/ext/duty_cycle/inc \
    $(LOCAL_PATH)/ext/global_shmem_msg/inc \
    $(LOCAL_PATH)/ext/island_exit/inc \
    $(LOCAL_PATH)/ext/offload/inc \
    $(LOCAL_PATH)/ext/path_delay/inc \
    $(LOCAL_PATH)/ext/prof/inc \
    $(LOCAL_PATH)/ext/soft_timer_fwk_ext/inc \
    $(LOCAL_PATH)/ext/voice/inc \

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libspf_cu
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/core/inc/generic \
    $(LOCAL_PATH)/core/src \
    $(LOCAL_PATH)/ext/async_cmd_handle/inc \
    $(LOCAL_PATH)/ext/ctrl_port/inc \
    $(LOCAL_PATH)/ext/ctrl_port/src \
    $(LOCAL_PATH)/ext/duty_cycle/inc \
    $(LOCAL_PATH)/ext/global_shmem_msg/inc \
    $(LOCAL_PATH)/ext/island_exit/inc \
    $(LOCAL_PATH)/ext/offload/inc \
    $(LOCAL_PATH)/ext/path_delay/inc \
    $(LOCAL_PATH)/ext/prof/inc \
    $(LOCAL_PATH)/ext/soft_timer_fwk_ext/inc \
    $(LOCAL_PATH)/ext/voice/inc \

LOCAL_SRC_FILES := \
    core/src/cu.c \
    core/src/cu_buf_util.c \
    core/src/cu_cmd_handler.c \
    core/src/cu_data_handler.c \
    core/src/cu_data_msg_handler.c \
    core/src/cu_events.c \
    core/src/cu_ext_port_util.c \
    core/src/cu_gpr_if.c \
    core/src/cu_island.c \
    core/src/cu_pm.c \
    core/src/cu_prebuffer.c \
    core/src/cu_state_handler.c \
    core/src/cu_utils.c \
    core/src/cu_utils_island.c \
    core/src/cu_utils_qsh_audio_island.c \
    ext/duty_cycle/src/cu_duty_cycle.c \
    ext/global_shmem_msg/src/cu_global_shmem_msg.c \
    ext/island_exit/src/cu_ctrl_port_exit_island.c \
    ext/offload/src/cu_offload_util.c \
    ext/path_delay/src/cu_path_delay.c \
    ext/prof/src/cu_prof.c \
    ext/soft_timer_fwk_ext/src/cu_soft_timer_fwk_ext.c \
    ext/voice/src/cu_voice_util.c \
    ext/ctrl_port/src/cu_ctrl_port_util.c \
    ext/ctrl_port/src/cu_ctrl_port_util_island.c \
    ext/ctrl_port/src/cu_ext_ctrl_port_util.c \
    ext/ctrl_port/src/cu_ext_ctrl_port_util_island.c \
    ext/ctrl_port/src/cu_int_ctrl_port_util.c \
    ext/ctrl_port/src/cu_int_ctrl_port_util_island.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_utils_headers libspf_api libspf_interfaces_headers libspf_gu_headers libspf_gen_topo_headers libspf_spl_topo_headers libspf_topo_utils_headers libspf_icb_headers libapm_headers libspf_gen_cntr_headers libspf_spl_cntr_headers libspf_olc_headers libprivate_api_headers libplatform_cfg_headers
LOCAL_STATIC_LIBRARIES := libspf_utils libspf_interfaces libposal libspf_gu libspf_gen_topo libspf_topo_utils libspf_icb libapm

include $(BUILD_STATIC_LIBRARY)
