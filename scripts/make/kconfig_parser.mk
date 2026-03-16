#
# Parse Kconfig file to extract module configurations.
#

ifeq ($(PROJECT_BINARY_DIR),)
   $(error "PROJECT_BINARY_DIR must be defined before calling this makefile.")
endif
ifeq ($(PROJECT_SOURCE_DIR),)
   $(error "PROJECT_SOURCE_DIR must be defined before calling this makefile.")
endif

CONFIG := defconfig
SPF_DOT_CONFIG_PATH := $(PROJECT_BINARY_DIR)/.config
$(shell mkdir -p $(PROJECT_BINARY_DIR))
$(shell cp $(PROJECT_SOURCE_DIR)/arch/linux/configs/$(CONFIG) $(SPF_DOT_CONFIG_PATH))

kconfig_lines := $(file < $(SPF_DOT_CONFIG_PATH))

kconfig_lines_y := $(patsubst %=y, %, $(filter %=y,$(kconfig_lines)))
kconfig_lines_y += $(patsubst %=Y, %, $(filter %=Y,$(kconfig_lines)))
$(foreach line, $(kconfig_lines_y), $(eval $(line) := y))

kconfig_lines_m := $(patsubst %=m, %, $(filter %=m,$(kconfig_lines)))
kconfig_lines_m += $(patsubst %=M, %, $(filter %=M,$(kconfig_lines)))
$(foreach line, $(kconfig_lines_m), $(eval $(line) := m))
