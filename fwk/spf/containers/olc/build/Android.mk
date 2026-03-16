LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libspf_olc_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/cmn/inc \
    $(LOCAL_PATH)/driver/inc \
    $(LOCAL_PATH)/driver/data_process_mgmt/inc \
    $(LOCAL_PATH)/driver/resource_mgmt_utils/inc \
    $(LOCAL_PATH)/driver/satellite_graph_mgmt/inc


LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libspf_olc
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/cmn/inc \
    $(LOCAL_PATH)/core/src \
    $(LOCAL_PATH)/driver/inc \
    $(LOCAL_PATH)/driver/data_process_mgmt/inc \
    $(LOCAL_PATH)/driver/data_process_mgmt/src \
    $(LOCAL_PATH)/driver/resource_mgmt_utils/inc \
    $(LOCAL_PATH)/driver/resource_mgmt_utils/src \
    $(LOCAL_PATH)/driver/satellite_graph_mgmt/inc \
    $(LOCAL_PATH)/driver/satellite_graph_mgmt/src


LOCAL_SRC_FILES := \
    core/src/olc_buf_util.c \
    core/src/olc.c \
    core/src/olc_calib_utils.c \
    core/src/olc_cmd_handler.c \
    core/src/olc_data_handler.c \
    core/src/olc_eos_util.c \
    core/src/olc_pm.c \
    core/src/olc_rsp_handler.c \
    core/src/olc_servreg_event_handler.c \
    core/src/olc_sp_utils.c \
    core/src/olc_timestamp.c \
    core/src/olc_util.c \
    driver/data_process_mgmt/src/spdm_data_buf_handler.c \
    driver/data_process_mgmt/src/spdm_data_handler.c \
    driver/data_process_mgmt/src/spdm_data_msg_handler.c \
    driver/data_process_mgmt/src/spdm_data_port_handler.c \
    driver/data_process_mgmt/src/spdm_media_fmt_handler.c \
    driver/data_process_mgmt/src/spdm_meta_data_handler.c \
    driver/data_process_mgmt/src/spdm_port_config_handler.c \
    driver/data_process_mgmt/src/spdm_port_event_handler.c \
    driver/data_process_mgmt/src/spdm_state_prop_handler.c \
    driver/data_process_mgmt/src/spdm_utils.c \
    driver/resource_mgmt_utils/src/srm_ipc_comm_utils.c \
    driver/resource_mgmt_utils/src/srm_ipc_shmem_utils.c \
    driver/satellite_graph_mgmt/src/sgm.c \
    driver/satellite_graph_mgmt/src/sgm_cmd_handler.c \
    driver/satellite_graph_mgmt/src/sgm_cmd_handler_utils.c \
    driver/satellite_graph_mgmt/src/sgm_cmd_parser_utils.c \
    driver/satellite_graph_mgmt/src/sgm_event_handler.c \
    driver/satellite_graph_mgmt/src/sgm_path_delay_utils.c \
    driver/satellite_graph_mgmt/src/sgm_rsp_handler.c \
    driver/satellite_graph_mgmt/src/sgm_servreg_event_handler.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_utils_headers libspf_api libspf_interfaces_headers libspf_gu_headers libspf_gen_topo_headers libspf_topo_utils_headers libspf_icb_headers libapm_headers libspf_cu_headers libamdb_headers libprivate_api_headers libirm_headers libplatform_cfg_headers
LOCAL_STATIC_LIBRARIES := libspf_utils libspf_interfaces libposal libspf_gu libspf_gen_topo libspf_topo_utils libspf_icb libapm libspf_cu libamdb

include $(BUILD_STATIC_LIBRARY)

