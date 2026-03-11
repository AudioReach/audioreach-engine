LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libspf_gen_cntr_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/cmn/inc \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/core/src \
    $(LOCAL_PATH)/core/src/generic \
    $(LOCAL_PATH)/ext/bt_codec_fwk_ext/inc \
    $(LOCAL_PATH)/ext/cmn_sh_mem/inc \
    $(LOCAL_PATH)/ext/err_check/inc \
    $(LOCAL_PATH)/ext/metadata/inc \
    $(LOCAL_PATH)/ext/offload/inc \
    $(LOCAL_PATH)/ext/pass_thru_cntr/inc \
    $(LOCAL_PATH)/ext/peer_cntr/inc \
    $(LOCAL_PATH)/ext/placeholder/inc \
    $(LOCAL_PATH)/ext/pure_st/inc \
    $(LOCAL_PATH)/ext/rd_sh_mem_ep/inc \
    $(LOCAL_PATH)/ext/sync_fwk_ext/inc \
    $(LOCAL_PATH)/ext/wr_sh_mem_ep/inc \
    $(LOCAL_PATH)/ext/thin_topo_cntr_utils/src \
    $(LOCAL_PATH)/ext/thin_topo_cntr_utils/inc


LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libspf_gen_cntr
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/cmn/inc \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/core/src \
    $(LOCAL_PATH)/core/src/generic \
    $(LOCAL_PATH)/ext/bt_codec_fwk_ext/inc \
    $(LOCAL_PATH)/ext/cmn_sh_mem/inc \
    $(LOCAL_PATH)/ext/err_check/inc \
    $(LOCAL_PATH)/ext/metadata/inc \
    $(LOCAL_PATH)/ext/offload/inc \
    $(LOCAL_PATH)/ext/pass_thru_cntr/inc \
    $(LOCAL_PATH)/ext/peer_cntr/inc \
    $(LOCAL_PATH)/ext/placeholder/inc \
    $(LOCAL_PATH)/ext/pure_st/inc \
    $(LOCAL_PATH)/ext/rd_sh_mem_ep/inc \
    $(LOCAL_PATH)/ext/sync_fwk_ext/inc \
    $(LOCAL_PATH)/ext/wr_sh_mem_ep/inc \
    $(LOCAL_PATH)/ext/thin_topo_cntr_utils/src \
    $(LOCAL_PATH)/ext/thin_topo_cntr_utils/inc

LOCAL_SRC_FILES := \
    core/src/gen_cntr.c \
    core/src/gen_cntr_buf_util.c \
    core/src/gen_cntr_cmd_handler.c \
    core/src/gen_cntr_data_handler.c \
    core/src/gen_cntr_data_handler_island.c \
    core/src/gen_cntr_data_msg_handler.c \
    core/src/gen_cntr_fwk_extn_utils.c \
    core/src/gen_cntr_fwk_extn_utils_island.c \
    core/src/gen_cntr_island.c \
    core/src/gen_cntr_pm.c \
    core/src/gen_cntr_st_handler.c \
    core/src/gen_cntr_st_handler_island.c \
    core/src/gen_cntr_trigger_policy.c \
    core/src/gen_cntr_trigger_policy_island.c \
    core/src/gen_cntr_utils.c \
    core/src/gen_cntr_utils_island.c \
    core/src/gen_cntr_utils_md_island.c \
    ext/bt_codec_fwk_ext/stub_src/gen_cntr_bt_codec_fwk_ext.c \
    ext/cmn_sh_mem/src/gen_cntr_cmn_sh_mem.c \
    ext/err_check/stub_src/gen_cntr_err_check.c \
    ext/err_check/stub_src/gen_cntr_err_handler.c \
    ext/metadata/src/gen_cntr_metadata_island.c \
    ext/metadata/src/gen_cntr_metadata.c \
    ext/offload/src/gen_cntr_offload_utils.c \
    ext/peer_cntr/src/gen_cntr_peer_cntr_input.c \
    ext/peer_cntr/src/gen_cntr_peer_cntr_input_island.c \
    ext/peer_cntr/src/gen_cntr_peer_cntr_output.c \
    ext/peer_cntr/src/gen_cntr_peer_cntr_output_island.c \
    ext/pass_thru_cntr/src/pt_buf_utils.c \
    ext/pass_thru_cntr/src/pt_cmd_handler.c \
    ext/pass_thru_cntr/src/pt_cntr.c \
    ext/pass_thru_cntr/src/pt_cntr_process.c \
    ext/placeholder/stub_src/gen_cntr_placeholder.c \
    ext/pure_st/stub_src/gen_cntr_pure_st_handler.c \
    ext/pure_st/stub_src/gen_cntr_pure_st_handler_island.c \
    ext/sync_fwk_ext/stub_src/gen_cntr_sync_fwk_ext.c \
    ext/thin_topo_cntr_utils/src/thin_topo_cntr_utils_island.c \
    ext/thin_topo_cntr_utils/src/thin_topo_cntr_utils.c


LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_utils_headers libspf_api libspf_interfaces_headers libspf_gu_headers libspf_gen_topo_headers libspf_topo_utils_headers libspf_icb_headers libapm_headers libspf_cu_headers libamdb_headers libplatform_cfg_headers libprivate_api_headers libirm_headers
LOCAL_STATIC_LIBRARIES := libspf_utils libspf_interfaces libposal libspf_gu libspf_gen_topo libspf_topo_utils libspf_icb libapm libspf_cu libamdb

include $(BUILD_STATIC_LIBRARY)


include $(CLEAR_VARS)

LOCAL_MODULE := lib_wr_shmem_ep
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/ext/wr_sh_mem_ep/inc

LOCAL_SRC_FILES := \
    ext/wr_sh_mem_ep/src/gen_cntr_wr_sh_mem_ep.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libspf_gen_cntr_headers libspf_interfaces_headers libapm_headers libspf_utils_headers libspf_api libposal_headers libspf_cu_headers libplatform_cfg_headers libspf_gu_headers libspf_gen_topo_headers libspf_topo_utils_headers libspf_icb_headers libamdb_headers
LOCAL_STATIC_LIBRARIES := libspf_gen_cntr libspf_interfaces libapm libspf_utils libposal libspf_cu libspf_gu libspf_gen_topo libspf_topo_utils libspf_icb libamdb

LOCAL_SPF_MODULE_KCONFIG          := CONFIG_WR_SH_MEM_EP
LOCAL_SPF_MODULE_NAME             := $(LOCAL_MODULE)
LOCAL_SPF_MODULE_MAJOR_VER        := 1
LOCAL_SPF_MODULE_MINOR_VER        := 0
LOCAL_SPF_MODULE_AMDB_ITYPE       := "capi"
LOCAL_SPF_MODULE_AMDB_MTYPE       := "framework"
LOCAL_SPF_MODULE_AMDB_MID         := "0x07001000"
LOCAL_SPF_MODULE_AMDB_TAG         := "framework"
LOCAL_SPF_MODULE_AMDB_MOD_NAME    := "MODULE_ID_WR_SHARED_MEM_EP"
LOCAL_SPF_MODULE_QACT_MODULE_TYPE := "generic"
LOCAL_SPF_MODULE_H2XML_HEADERS    := "$(MY_LOCAL_PATH_ARE)/fwk/api/modules/wr_sh_mem_ep_api.h"

include $(BUILD_ARE_MODULES)


include $(CLEAR_VARS)

LOCAL_MODULE := lib_rd_shmem_ep
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/ext/rd_sh_mem_ep/inc

LOCAL_SRC_FILES := \
    ext/rd_sh_mem_ep/src/gen_cntr_rd_sh_mem_ep.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libspf_gen_cntr_headers libspf_interfaces_headers libapm_headers libspf_utils_headers libspf_api libposal_headers libspf_cu_headers libplatform_cfg_headers libspf_gu_headers libspf_gen_topo_headers libspf_topo_utils_headers libspf_icb_headers libamdb_headers
LOCAL_STATIC_LIBRARIES := libspf_gen_cntr libspf_interfaces libapm libspf_utils libposal libspf_cu libspf_gu libspf_gen_topo libspf_topo_utils libspf_icb libamdb

LOCAL_SPF_MODULE_KCONFIG          := CONFIG_RD_SH_MEM_EP
LOCAL_SPF_MODULE_NAME             := $(LOCAL_MODULE)
LOCAL_SPF_MODULE_MAJOR_VER        := 1
LOCAL_SPF_MODULE_MINOR_VER        := 0
LOCAL_SPF_MODULE_AMDB_ITYPE       := "capi"
LOCAL_SPF_MODULE_AMDB_MTYPE       := "framework"
LOCAL_SPF_MODULE_AMDB_MID         := "0x07001001"
LOCAL_SPF_MODULE_AMDB_TAG         := "framework"
LOCAL_SPF_MODULE_AMDB_MOD_NAME    := "MODULE_ID_RD_SHARED_MEM_EP"
LOCAL_SPF_MODULE_QACT_MODULE_TYPE := "generic"
LOCAL_SPF_MODULE_H2XML_HEADERS    := "$(MY_LOCAL_PATH_ARE)/fwk/api/modules/rd_sh_mem_ep_api.h"

include $(BUILD_ARE_MODULES)

include $(CLEAR_VARS)

LOCAL_MODULE := lib_placeholder_dec
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/ext/placeholder/inc

LOCAL_SRC_FILES := \
    ext/placeholder/src/gen_cntr_placeholder.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libspf_gen_cntr_headers libspf_interfaces_headers libapm_headers libspf_utils_headers libspf_api libposal_headers libspf_cu_headers libplatform_cfg_headers libspf_gu_headers libspf_gen_topo_headers libspf_topo_utils_headers libspf_icb_headers libamdb_headers
LOCAL_STATIC_LIBRARIES := libspf_gen_cntr libspf_interfaces libapm libspf_utils libposal libspf_cu libspf_gu libspf_gen_topo libspf_topo_utils libspf_icb libamdb

LOCAL_SPF_MODULE_KCONFIG          := CONFIG_PLACEHOLDER_ENC_DEC
LOCAL_SPF_MODULE_NAME             := $(LOCAL_MODULE)
LOCAL_SPF_MODULE_MAJOR_VER        := 1
LOCAL_SPF_MODULE_MINOR_VER        := 0
LOCAL_SPF_MODULE_AMDB_ITYPE       := "capi"
LOCAL_SPF_MODULE_AMDB_MTYPE       := "framework"
LOCAL_SPF_MODULE_AMDB_MID         := "0x07001009"
LOCAL_SPF_MODULE_AMDB_TAG         := "framework"
LOCAL_SPF_MODULE_AMDB_MOD_NAME    := "MODULE_ID_PLACEHOLDER_DECODER"
LOCAL_SPF_MODULE_QACT_MODULE_TYPE := "generic"
LOCAL_SPF_MODULE_H2XML_HEADERS    := "$(MY_LOCAL_PATH_ARE)/fwk/api/modules/common_enc_dec_api.h"

include $(BUILD_ARE_MODULES)

include $(CLEAR_VARS)

LOCAL_MODULE := lib_placeholder_enc
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/ext/placeholder/inc

LOCAL_SRC_FILES := \
    ext/placeholder/src/gen_cntr_placeholder.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libspf_gen_cntr_headers libspf_interfaces_headers libapm_headers libspf_utils_headers libspf_api libposal_headers libspf_cu_headers libplatform_cfg_headers libspf_gu_headers libspf_gen_topo_headers libspf_topo_utils_headers libspf_icb_headers libamdb_headers
LOCAL_STATIC_LIBRARIES := libspf_gen_cntr libspf_interfaces libapm libspf_utils libposal libspf_cu libspf_gu libspf_gen_topo libspf_topo_utils libspf_icb libamdb

LOCAL_SPF_MODULE_KCONFIG          := CONFIG_PLACEHOLDER_ENC_DEC
LOCAL_SPF_MODULE_NAME             := $(LOCAL_MODULE)
LOCAL_SPF_MODULE_MAJOR_VER        := 1
LOCAL_SPF_MODULE_MINOR_VER        := 0
LOCAL_SPF_MODULE_AMDB_ITYPE       := "capi"
LOCAL_SPF_MODULE_AMDB_MTYPE       := "framework"
LOCAL_SPF_MODULE_AMDB_MID         := "0x07001008"
LOCAL_SPF_MODULE_AMDB_TAG         := "framework"
LOCAL_SPF_MODULE_AMDB_MOD_NAME    := "MODULE_ID_PLACEHOLDER_ENCODER"
LOCAL_SPF_MODULE_QACT_MODULE_TYPE := "generic"
LOCAL_SPF_MODULE_H2XML_HEADERS    := "$(MY_LOCAL_PATH_ARE)/fwk/api/modules/common_enc_dec_api.h"

include $(BUILD_ARE_MODULES)
