MY_LOCAL_PATH_SPF_CONTAINERS := $(call my-dir)/..

include $(MY_LOCAL_PATH_SPF_CONTAINERS)/cmn/build/Android.mk
include $(MY_LOCAL_PATH_SPF_CONTAINERS)/gen_cntr/build/Android.mk
include $(MY_LOCAL_PATH_SPF_CONTAINERS)/spl_cntr/build/Android.mk
include $(MY_LOCAL_PATH_SPF_CONTAINERS)/olc/build/Android.mk
