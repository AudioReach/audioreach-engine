LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libamdb_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/ext/inc \
    $(LOCAL_PATH)/ext/src

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libamdb
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/core/inc \
    $(LOCAL_PATH)/core/src \
    $(LOCAL_PATH)/ext/inc \
    $(LOCAL_PATH)/autogen/linux \
    $(LOCAL_PATH)/ext/src

FILE_LIST := $(wildcard $(LOCAL_PATH)/autogen/linux/*.c)
LOCAL_SRC_FILES := $(FILE_LIST:$(LOCAL_PATH)/%=%)

LOCAL_SRC_FILES += \
    core/src/amdb.c \
    core/src/amdb_capi.c \
    core/src/amdb_capi_island.c \
    core/src/amdb_static_loading.c \
    core/src/amdb_utils.c \
    core/src/private/amdb_private_utils.c \
    ext/src/amdb_cmd_handler.c \
    ext/src/amdb_offload_utils.c \
    ext/src/amdb_parallel_loader.c \
    ext/src/amdb_queue.c \
    ext/src/amdb_resource_voter.c \
    ext/src/amdb_thread.c

LOCAL_CFLAGS += -flto -O3 -Wall -ffixed-x18 -std=c17 -DAMDB_REG_SPF_MODULES

LOCAL_CFLAGS_32 += -mfpu=neon -fasm -ftree-vectorize -O3
LOCAL_CFLAGS_64 += -fasm -ftree-vectorize -O3 -march=armv8-a+crypto

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    LOCAL_CFLAGS += -fsanitize=shadow-call-stack
endif

LOCAL_SHARED_LIBRARIES := \
    liblx-osal \
    libar-gpr

LOCAL_HEADER_LIBRARIES := libposal_headers libspf_utils_headers libspf_api libspf_interfaces_headers libapm_headers libirm_headers
LOCAL_STATIC_LIBRARIES := libspf_utils libspf_interfaces libposal

include $(BUILD_STATIC_LIBRARY)
