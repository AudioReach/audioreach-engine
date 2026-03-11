LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libspf_utils_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
	$(LOCAL_PATH)/circular_buffer/inc \
	$(LOCAL_PATH)/cmn/api \
	$(LOCAL_PATH)/cmn/inc \
	$(LOCAL_PATH)/interleaver/inc \
	$(LOCAL_PATH)/list/inc \
	$(LOCAL_PATH)/lpi_pool/inc \
	$(LOCAL_PATH)/thread_pool/inc \
	$(LOCAL_PATH)/thread_pool/src \
	$(LOCAL_PATH)/watchdog_svc/inc

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libspf_utils
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/circular_buffer/inc \
	$(LOCAL_PATH)/circular_buffer/src \
	$(LOCAL_PATH)/cmn/api \
	$(LOCAL_PATH)/cmn/inc \
	$(LOCAL_PATH)/interleaver/inc \
	$(LOCAL_PATH)/list/inc \
	$(LOCAL_PATH)/list/src \
	$(LOCAL_PATH)/lpi_pool/inc \
	$(LOCAL_PATH)/thread_pool/inc \
	$(LOCAL_PATH)/thread_pool/src \
	$(LOCAL_PATH)/watchdog_svc/inc

LOCAL_SRC_FILES := \
	circular_buffer/src/spf_circular_buffer.c \
	circular_buffer/src/spf_circular_buffer_island.c \
	circular_buffer/src/spf_circular_buffer_md_utils.c \
	circular_buffer/src/spf_circular_buffer_utils.c \
	cmn/src/spf_bufmgr.c \
	cmn/src/spf_bufmgr_island.c \
	cmn/src/spf_debug_info_dump.c \
	cmn/src/spf_hashtable.c \
	cmn/src/spf_main.c \
	cmn/src/spf_msg_utils.c \
	cmn/src/spf_msg_utils_island.c \
	cmn/src/spf_ref_counter.c \
	cmn/src/spf_svc_calib.c \
	cmn/src/spf_svc_utils.c \
	cmn/src/spf_sys_util.c \
	interleaver/src/spf_interleaver_island.c \
	list/src/spf_list_utils.c \
	list/src/spf_list_utils_island.c \
	lpi_pool/src/spf_lpi_pool_utils.c \
	lpi_pool/src/spf_lpi_pool_utils_island.c \
	thread_pool/src/spf_thread_pool.c \
	thread_pool/src/spf_thread_pool_island.c \
	watchdog_svc/src/spf_watchdog_svc.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17 -g -DSPF_WATCHDOG_SVC_PERIOD_US=100000 -DUSES_SPF_THREAD_POOL

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr \
    libdiag

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_api libspf_interfaces_headers libapm_headers libamdb_headers libirm_headers libdls_headers
LOCAL_STATIC_LIBRARIES := \
    capi_data_logging \
    capi_priority_sync \
    capi_splitter \
    capi_sync \
    libamdb \
    libapm \
    libdls \
    libirm \
    libposal \
    libspf_interfaces \
    libspf_cu \
    libspf_gen_cntr \
    libspf_spl_cntr \
    libspf_olc \
    libspf_gen_topo \
    libspf_spl_topo \
    libspf_gu \
    libspf_topo_utils \
    libspf_icb

LOCAL_STATIC_LIBRARIES += $(GLOBAL_SPF_LIBS_LIST)
GLOBAL_SPF_LIBS_LIST :=

include $(BUILD_SHARED_LIBRARY)
