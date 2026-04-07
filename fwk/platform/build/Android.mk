MY_LOCAL_PATH_PLATFORM := $(call my-dir)/..

include $(MY_LOCAL_PATH_PLATFORM)/posal/build/Android.mk
include $(MY_LOCAL_PATH_PLATFORM)/core_drv/build/Android.mk
include $(MY_LOCAL_PATH_PLATFORM)/private/build/Android.mk
include $(MY_LOCAL_PATH_PLATFORM)/modules/generic/endpoint/alsa_device/build/Android.mk
