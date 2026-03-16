LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)
LOCAL_MODULE := libspf_api
LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/apm \
    $(LOCAL_PATH)/apm/private \
    $(LOCAL_PATH)/ar_utils \
    $(LOCAL_PATH)/ar_utils/linux \
    $(LOCAL_PATH)/modules \
    $(LOCAL_PATH)/vcpm \
    $(LOCAL_PATH)/private

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_HEADER_LIBRARY)
