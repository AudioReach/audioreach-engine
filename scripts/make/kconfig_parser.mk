#
# Parse Kconfig file to extract module configurations.
#

ifeq ($(PROJECT_BINARY_DIR),)
   $(error "PROJECT_BINARY_DIR must be defined before calling this makefile.")
endif
ifeq ($(PROJECT_SOURCE_DIR),)
   $(error "PROJECT_SOURCE_DIR must be defined before calling this makefile.")
endif

ifdef TARGET_BOARD_PLATFORM
    CONFIG := $(TARGET_BOARD_PLATFORM)_defconfig
else
    CONFIG := defconfig
endif

SPF_DOT_CONFIG_PATH := $(PROJECT_BINARY_DIR)/.config
$(shell mkdir -p $(PROJECT_BINARY_DIR))

ifdef TARGET_VENDOR
    CONFIG_PATH = $(PROJECT_SOURCE_DIR)/arch/$(TARGET_ARCH)/$(TARGET_VENDOR)
else
    CONFIG_PATH = $(PROJECT_SOURCE_DIR)/arch/$(TARGET_ARCH)/qcom
endif

$(shell cp $(CONFIG_PATH)/$(CONFIG) $(SPF_DOT_CONFIG_PATH))

kconfig_lines := $(file < $(SPF_DOT_CONFIG_PATH))

kconfig_lines_y := $(patsubst %=y, %, $(filter %=y,$(kconfig_lines)))
kconfig_lines_y += $(patsubst %=Y, %, $(filter %=Y,$(kconfig_lines)))
$(foreach line, $(kconfig_lines_y), $(eval $(line) := y))

kconfig_lines_m := $(patsubst %=m, %, $(filter %=m,$(kconfig_lines)))
kconfig_lines_m += $(patsubst %=M, %, $(filter %=M,$(kconfig_lines)))
$(foreach line, $(kconfig_lines_m), $(eval $(line) := m))
