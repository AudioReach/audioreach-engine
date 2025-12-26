MY_LOCAL_PATH_ARE := $(call my-dir)

BUILD_ARE_MODULES := $(MY_LOCAL_PATH_ARE)/scripts/make/spf_base.mk
PROJECT_BINARY_DIR := $(MY_LOCAL_PATH_ARE)/build/linux/
PROJECT_SOURCE_DIR := $(MY_LOCAL_PATH_ARE)
GLOBAL_SPF_LIBS_LIST :=

include $(MY_LOCAL_PATH_ARE)/scripts/make/kconfig_parser.mk
# read CONFIG_PROC_DOMAIN from .config file
CONFIG_LINE := $(file < $(PROJECT_BINARY_DIR).config)
CONFIG_PROC_DOMAIN := $(patsubst CONFIG_PROC_DOMAIN=%,%,$(filter CONFIG_PROC_DOMAIN=%,$(CONFIG_LINE)))
CONFIG_PROC_DOMAIN := $(subst ",,$(CONFIG_PROC_DOMAIN))

# Check if CONFIG_PROC_DOMAIN is defined and not empty
ifeq ($(CONFIG_PROC_DOMAIN),)
    $(error Undefined/Empty CONFIG_PROC_DOMAIN configuration)
endif

# Define supported PROC_DOMAIN values
SUPPORTED_PROC_DOMAINS := ADSP MDSP APPS SDSP CDSP CC_DSP GDSP_0 GDSP_1 APPS_2

ifeq ($(filter $(CONFIG_PROC_DOMAIN), $(SUPPORTED_PROC_DOMAINS)),)
    $(error Unsupported CONFIG_PROC_DOMAIN value: $(CONFIG_PROC_DOMAIN). Supported values are: $(SUPPORTED_PROC_DOMAINS))
else
    $(info CONFIG_PROC_DOMAIN value '$(CONFIG_PROC_DOMAIN)' is supported.)
endif

# Set derived variable
PROC_DOMAIN_NAME := PROC_DOMAIN_$(CONFIG_PROC_DOMAIN)

include $(MY_LOCAL_PATH_ARE)/modules/build/Android.mk
include $(MY_LOCAL_PATH_ARE)/fwk/build/Android.mk
