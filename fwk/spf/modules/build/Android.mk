MY_LOCAL_PATH_SPF_MODULES := $(call my-dir)/..

include $(MY_LOCAL_PATH_SPF_MODULES)/data_logging/build/Android.mk
include $(MY_LOCAL_PATH_SPF_MODULES)/irm/build/Android.mk
include $(MY_LOCAL_PATH_SPF_MODULES)/priority_sync/build/Android.mk
include $(MY_LOCAL_PATH_SPF_MODULES)/simple_splitter/build/Android.mk
include $(MY_LOCAL_PATH_SPF_MODULES)/sync_module/build/Android.mk

