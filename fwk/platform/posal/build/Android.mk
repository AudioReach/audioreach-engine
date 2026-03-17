LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libposal_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/inc/linux \
    $(LOCAL_PATH)/inc/linux/stringl \
    $(LOCAL_PATH)/inc
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libposal
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/inc/linux \
    $(LOCAL_PATH)/inc/linux/stringl \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/inc/private \
    $(LOCAL_PATH)/inc/generic \
    $(LOCAL_PATH)/src/generic \
    $(LOCAL_PATH)/src/linux \
    $(LOCAL_PATH)/../../spf/dls/api

LOCAL_SRC_FILES := \
    src/generic/posal_bufpool.c \
    src/generic/posal_bufpool_island.c \
    src/generic/posal_channel.c \
    src/generic/posal_channel_island.c \
    src/generic/posal_data_log_island.c \
    src/generic/posal_err_fatal.c \
    src/generic/posal_queue.c \
    src/generic/posal_queue_island.c \
    src/generic/posal_signal.c \
    src/generic/posal_signal_island.c \
    src/generic/posal_std.c \
    src/generic/posal_thread_prio.c \
    src/generic/posal_thread_profiling.c \
    src/generic/posal_thread_util.c \
    src/linux/posal.c \
    src/linux/posal_cache_island.c \
    src/linux/posal_cache.c \
    src/linux/posal_condvar.c \
    src/linux/posal_data_log.c \
    src/linux/posal_linux_signal.c \
    src/linux/posal_linux_stubs.c \
    src/linux/posal_linux_thread.c \
    src/linux/posal_mem_prof.c \
    src/linux/posal_memory.c \
    src/linux/posal_memory_island.c \
    src/linux/posal_memory_stats.c \
    src/linux/posal_memorymap.c \
    src/linux/posal_memorymap_island.c \
    src/linux/posal_mems_util.c \
    src/linux/posal_mutex.c \
    src/linux/posal_nmutex.c \
    src/linux/posal_power_mgr.c \
    src/linux/posal_rtld.c \
    src/linux/posal_thread_attr_cfg.c \
    src/linux/posal_timer.c \
    src/linux/private/posal_private.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17 -g -D_ANDROID_

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libdiag

LOCAL_HEADER_LIBRARIES := libarosal_headers libspf_interfaces_headers libspf_api libspf_utils_headers libprivate_api_headers libplatform_cfg_headers
LOCAL_STATIC_LIBRARIES :=

include $(BUILD_STATIC_LIBRARY)
