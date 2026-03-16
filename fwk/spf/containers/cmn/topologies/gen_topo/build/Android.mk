LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libspf_gen_topo_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/ext/ctrl_port/inc \
    $(LOCAL_PATH)/ext/data_port_ops_intf_ext/inc \
    $(LOCAL_PATH)/ext/dm_ext/inc \
    $(LOCAL_PATH)/ext/global_shmem_msg/inc \
    $(LOCAL_PATH)/ext/island_exit/inc \
    $(LOCAL_PATH)/ext/metadata/inc \
    $(LOCAL_PATH)/ext/module_bypass/inc \
    $(LOCAL_PATH)/ext/path_delay/inc \
    $(LOCAL_PATH)/ext/pcm_fwk_ext/inc \
    $(LOCAL_PATH)/ext/prof/inc \
    $(LOCAL_PATH)/ext/pure_st_topo/inc \
    $(LOCAL_PATH)/ext/sync_fwk_ext/inc \
    $(LOCAL_PATH)/../topo_interface/inc

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libspf_gen_topo
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/core/src \
    $(LOCAL_PATH)/ext/ctrl_port/inc \
    $(LOCAL_PATH)/ext/data_port_ops_intf_ext/inc \
    $(LOCAL_PATH)/ext/dm_ext/inc \
    $(LOCAL_PATH)/ext/global_shmem_msg/inc \
    $(LOCAL_PATH)/ext/island_exit/inc \
    $(LOCAL_PATH)/ext/metadata/inc \
    $(LOCAL_PATH)/ext/module_bypass/inc \
    $(LOCAL_PATH)/ext/path_delay/inc \
    $(LOCAL_PATH)/ext/pcm_fwk_ext/inc \
    $(LOCAL_PATH)/ext/prof/inc \
    $(LOCAL_PATH)/ext/pure_st_topo/inc \
    $(LOCAL_PATH)/ext/pure_st_topo/src \
    $(LOCAL_PATH)/ext/sync_fwk_ext/inc \
    $(LOCAL_PATH)/../topo_interface/inc

LOCAL_SRC_FILES := \
    core/src/gen_topo.c \
    core/src/gen_topo_buf_util_island.c \
    core/src/gen_topo_capi.c \
    core/src/gen_topo_capi_cb_handler.c \
    core/src/gen_topo_capi_cb_handler_island.c \
    core/src/gen_topo_data_flow_state.c \
    core/src/gen_topo_data_flow_state_md_island.c \
    core/src/gen_topo_data_process.c \
    core/src/gen_topo_data_process_island.c \
    core/src/gen_topo_debug_info.c \
    core/src/gen_topo_fwk_extn_utils.c \
    core/src/gen_topo_intf_extn_utils.c \
    core/src/gen_topo_island.c \
    core/src/gen_topo_pm.c \
    core/src/gen_topo_propagation.c \
    core/src/gen_topo_public_functions.c \
    core/src/gen_topo_public_functions_md_island.c \
    core/src/gen_topo_trigger_policy.c \
    core/src/gen_topo_trigger_policy_change_island.c \
    core/src/gen_topo_trigger_policy_island.c \
    core/src/topo_buf_mgr.c \
    ext/ctrl_port/src/gen_topo_ctrl_port.c \
    ext/ctrl_port/src/gen_topo_ctrl_port_island.c \
    ext/data_port_ops_intf_ext/src/gen_topo_data_port_ops_intf_ext.c \
    ext/dm_ext/src/gen_topo_dm_ext.c \
    ext/dm_ext/src/gen_topo_dm_ext_island.c \
    ext/global_shmem_msg/src/gen_topo_global_shmem_msg.c \
    ext/island_exit/src/gen_topo_capi_metadata_exit_island.c \
    ext/island_exit/src/gen_topo_metadata_exit_island.c \
    ext/metadata/src/gen_topo_metadata.c \
    ext/metadata/src/gen_topo_metadata_island.c \
    ext/module_bypass/src/gen_topo_module_bypass.c \
    ext/path_delay/src/gen_topo_path_delay.c \
    ext/pcm_fwk_ext/src/gen_topo_pcm_fwk_ext.c \
    ext/prof/src/gen_topo_prof.c \
    ext/pure_st_topo/src/gen_topo_pure_st_process.c \
    ext/pure_st_topo/src/gen_topo_pure_st_process_island.c \
    ext/sync_fwk_ext/src/gen_topo_sync_fwk_ext.c


LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_utils_headers libspf_api libspf_interfaces_headers libspf_gu_headers libamdb_headers libspf_topo_utils_headers libirm_headers libapm_headers
LOCAL_STATIC_LIBRARIES := libspf_utils libspf_interfaces libposal libspf_gu libamdb libspf_topo_utils

include $(BUILD_STATIC_LIBRARY)
