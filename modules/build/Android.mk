MY_LOCAL_PATH_MODULES := $(call my-dir)/..

include $(MY_LOCAL_PATH_MODULES)/processing/volume_control/build/Android.mk
include $(MY_LOCAL_PATH_MODULES)/cmn/common/utils/build/Android.mk
include $(MY_LOCAL_PATH_MODULES)/processing/channel_mixer/build/Android.mk
include $(MY_LOCAL_PATH_MODULES)/processing/filters/fir/build/Android.mk
include $(MY_LOCAL_PATH_MODULES)/processing/filters/multi_stage_iir/build/Android.mk
include $(MY_LOCAL_PATH_MODULES)/processing/gain_control/limiter/build/Android.mk
include $(MY_LOCAL_PATH_MODULES)/cmn/simple_accumulator_limiter/build/Android.mk