LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libapm_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/ext/offload/inc \
    $(LOCAL_PATH)/ext/debug_info_dump/inc \
    $(LOCAL_PATH)/ext/debug_info_cfg/inc

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libapm
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/core/src \
    $(wildcard $(LOCAL_PATH)/ext/**/inc) \
    $(wildcard $(LOCAL_PATH)/ext/**/src) \
    $(LOCAL_PATH)/ext/proxy/proxy_cmn/inc \
    $(LOCAL_PATH)/ext/proxy/proxy_vcpm/inc \
    $(LOCAL_PATH)/ext/offload/src

LOCAL_SRC_FILES := \
    core/src/apm.c \
    core/src/apm_cmd_sequencer.c \
    core/src/apm_cmd_utils.c \
    core/src/apm_gpr_cmd_handler.c \
    core/src/apm_gpr_cmd_parser.c \
    core/src/apm_gpr_if.c \
    core/src/apm_graph_db.c \
    core/src/apm_msg_rsp_handler.c \
    core/src/apm_msg_utils.c \
    ext/apm_db_query/src/apm_db_query.c \
    ext/close_all/src/apm_close_all_utils.c \
    ext/cmn/src/apm_ext_cmn.c \
    ext/cntr_peer_heap_utils/src/apm_cntr_peer_heap_utils.c \
    ext/data_path/src/apm_data_path_utils.c \
    ext/debug_info_cfg/src/apm_debug_info_cfg.c \
    ext/debug_info_dump/src/apm_debug_info_dump.c \
    ext/err_hdlr/src/apm_err_hdlr_utils.c \
    ext/gpr_cmd_rsp_hdlr/src/apm_gpr_cmd_rsp_hdlr.c \
    ext/graph_utils/src/apm_graph_utils.c \
    ext/offload/src/apm_offload_memmap_handler.c \
    ext/offload/src/apm_offload_memmap_utils.c \
    ext/offload/src/apm_offload_utils.c \
    ext/parallel_cmd_utils/src/apm_parallel_cmd_utils.c \
    ext/proxy/proxy_cmn/src/apm_proxy_utils.c \
    ext/proxy/proxy_cmn/src/apm_proxy_cmd_rsp_handler.c \
    ext/proxy/proxy_vcpm/stub_src/apm_proxy_vcpm_stub.c \
    ext/pwr_mgr/src/apm_pwr_mgr_utils.c \
    ext/runtime_link_hdlr/src/apm_runtime_link_hdlr_utils.c \
    ext/set_get_cfg/src/apm_set_get_cfg_utils.c \
    ext/shared_mem/src/apm_shmem_util.c \
    ext/spf_cmd_hdlr/src/apm_spf_cmd_hdlr.c \
    ext/sys_util/src/apm_sys_util.c


LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_utils_headers libspf_api libspf_interfaces_headers libspf_cu_headers libamdb_headers libirm_headers libcoredrv_headers
LOCAL_STATIC_LIBRARIES := libspf_utils libspf_interfaces libposal libspf_cu libamdb libirm

include $(BUILD_STATIC_LIBRARY)
