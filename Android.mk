MY_LOCAL_PATH_ARE := $(call my-dir)

BUILD_ARE_MODULES := $(MY_LOCAL_PATH_ARE)/scripts/make/spf_base.mk
PROJECT_BINARY_DIR := $(MY_LOCAL_PATH_ARE)/build/linux/
PROJECT_SOURCE_DIR := $(MY_LOCAL_PATH_ARE)
GLOBAL_SPF_LIBS_LIST :=
include $(MY_LOCAL_PATH_ARE)/scripts/make/kconfig_parser.mk

include $(MY_LOCAL_PATH_ARE)/modules/build/Android.mk
include $(MY_LOCAL_PATH_ARE)/fwk/build/Android.mk
