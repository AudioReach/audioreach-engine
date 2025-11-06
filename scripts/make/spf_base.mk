#
# Generate json file for respective SPF module
#
define generate_static_json_file
   $(shell \
     mkdir -p $(dir $(1)); \
     rm -rf $(1); \
     echo "[" >> $(1); \
     echo "   {" >> $(1); \
     echo "      \"lib_name\"      : \"$(LOCAL_SPF_MODULE_NAME)\"," >> $(1); \
     echo "      \"build\"         : \"STATIC_BUILD_NO_STRIP\"," >> $(1); \
     echo "      \"lib_major_ver\" : $(LOCAL_SPF_MODULE_MAJOR_VER)," >> $(1); \
     echo "      \"lib_minor_ver\" : $(LOCAL_SPF_MODULE_MINOR_VER)," >> $(1); \
     echo "      \"amdb_info\"     :" >> $(1); \
     echo "      {" >> $(1); \
     echo "         \"itype\"            : \"$(LOCAL_SPF_MODULE_AMDB_ITYPE)\"," >> $(1); \
     echo "         \"mtype\"            : \"$(LOCAL_SPF_MODULE_AMDB_MTYPE)\"," >> $(1); \
     echo "         \"mid\"              : \"$(LOCAL_SPF_MODULE_AMDB_MID)\"," >> $(1); \
     echo "         \"tag\"              : \"$(LOCAL_SPF_MODULE_AMDB_TAG)\"," >> $(1); \
     echo "         \"module_name\"      : \"$(LOCAL_SPF_MODULE_AMDB_MOD_NAME)\"," >> $(1); \
     echo "         \"qact_module_type\" : \"$(LOCAL_SPF_MODULE_QACT_MODULE_TYPE)\"," >> $(1); \
     echo "         \"fmt_id1\"          : \"$(LOCAL_SPF_MODULE_AMDB_FMT_ID1)\"" >> $(1); \
     echo "      }" >> $(1); \
     echo "   }" >> $(1); \
     echo "]" >> $(1); \
    )
endef

define generate_dynamic_json_file
   $(shell \
     mkdir -p $(dir $(1)); \
     rm -rf $(1); \
     echo "[" >> $(1); \
     echo "   {" >> $(1); \
     echo "      \"lib_name\"      : \"$(LOCAL_SPF_MODULE_NAME)\"," >> $(1); \
     echo "      \"build\"         : \"SHARED_BUILD_STRIP\"," >> $(1); \
     echo "      \"lib_major_ver\" : $(LOCAL_SPF_MODULE_MAJOR_VER)," >> $(1); \
     echo "      \"lib_minor_ver\" : $(LOCAL_SPF_MODULE_MINOR_VER)," >> $(1); \
     echo "      \"amdb_info\"     :" >> $(1); \
     echo "      {" >> $(1); \
     echo "         \"itype\"            : \"$(LOCAL_SPF_MODULE_AMDB_ITYPE)\"," >> $(1); \
     echo "         \"mtype\"            : \"$(LOCAL_SPF_MODULE_AMDB_MTYPE)\"," >> $(1); \
     echo "         \"mid\"              : \"$(LOCAL_SPF_MODULE_AMDB_MID)\"," >> $(1); \
     echo "         \"tag\"              : \"$(LOCAL_SPF_MODULE_AMDB_TAG)\"," >> $(1); \
     echo "         \"module_name\"      : \"$(LOCAL_SPF_MODULE_AMDB_MOD_NAME)\"," >> $(1); \
     echo "         \"qact_module_type\" : \"$(LOCAL_SPF_MODULE_QACT_MODULE_TYPE)\"," >> $(1); \
     echo "         \"fmt_id1\"          : \"$(LOCAL_SPF_MODULE_AMDB_FMT_ID1)\"" >> $(1); \
     echo "      }" >> $(1); \
     echo "   }" >> $(1); \
     echo "]" >> $(1); \
    )
endef

#
# Clears a list of variables using ":=".
#
define clear_var_list
$(foreach v,$(1),$(eval $(v):=))
endef

#
# local spf related variables
#
local_spf_variables := \
  LOCAL_SPF_MODULE_KCONFIG \
  LOCAL_SPF_MODULE_NAME \
  LOCAL_SPF_MODULE_MAJOR_VER \
  LOCAL_SPF_MODULE_MINOR_VER \
  LOCAL_SPF_MODULE_AMDB_ITYPE \
  LOCAL_SPF_MODULE_AMDB_MTYPE \
  LOCAL_SPF_MODULE_AMDB_MID \
  LOCAL_SPF_MODULE_AMDB_TAG \
  LOCAL_SPF_MODULE_AMDB_MOD_NAME \
  LOCAL_SPF_MODULE_QACT_MODULE_TYPE \
  LOCAL_SPF_MODULE_AMDB_FMT_ID1 \
  LOCAL_SPF_MODULE_H2XML_HEADERS \
  LOCAL_JSON_FILE

.PHONY: menuconfig amdbgen
menuconfig:
	export srctree=$(PROJECT_SOURCE_DIR); \
    export ARCH=linux; \
    export AR_VERSION=1.0-0; \
    export AR_MAJOR_VERSION=1; \
    export AR_MINOR_VERSION=0; \
    export AR_PATCHLEVEL=0; \
    python3 $(PROJECT_SOURCE_DIR)/scripts/kconfig/menuconfig.py $(PROJECT_SOURCE_DIR)/Kconfig

SPF_CHIPSET := "SDM845"
SPF_TIME_STAMP := $(shell date +'%Y-%m-%d %H:%M:%S UTC')
amdbgen:
	export BUILDPATH=""; \
    export OPENSRC="y"; \
    export PROJECT_BINARY_DIR=$(PROJECT_BINARY_DIR); \
    export PROJECT_SOURCE_DIR=$(PROJECT_SOURCE_DIR); \
    export CHIPSET=$(SPF_CHIPSET); \
    export TGT_SPECIFIC_FOLDER=linux; \
    export TIMESTAMP="$(SPF_TIME_STAMP)"; \
    export ARCH=linux; \
    export AR_VERSION=1.0-0; \
    export AR_MAJOR_VERSION=1; \
    export AR_MINOR_VERSION=0; \
    export AR_PATCHLEVEL=0; \
    python3 $(PROJECT_SOURCE_DIR)/scripts/spf/spf_libs_config_parser.py -c $(SPF_CHIPSET) -f "GEN_SHARED_LIBS"

#
# Main
#
LOCAL_SPF_MODULE_KCONFIG := $($(LOCAL_SPF_MODULE_KCONFIG))

H2XML          := $(PROJECT_SOURCE_DIR)/scripts/h2xml/h2xml
H2XML_CONFIG   := $(PROJECT_SOURCE_DIR)/scripts/h2xml/h2xml_config_spf.xml
H2XML_OUTPUT   := $(PROJECT_BINARY_DIR)/h2xml_autogen
H2XML_FLAG     := "-D __H2XML__"
H2XML_INCLUDES := -I $(abspath $(PROJECT_SOURCE_DIR)/fwk/api/apm/)
H2XML_INCLUDES += -I $(abspath $(PROJECT_SOURCE_DIR)/fwk/api/ar_utils/)
H2XML_INCLUDES += -I $(abspath $(PROJECT_SOURCE_DIR)/fwk/api/modules/)
H2XML_INCLUDES += -I $(abspath $(PROJECT_SOURCE_DIR)/ar_osal/api/)
$(shell mkdir -p $(H2XML_OUTPUT))

ifeq ($(LOCAL_SPF_MODULE_KCONFIG),y)

# generate json files
LOCAL_JSON_FILE := $(PROJECT_BINARY_DIR)/libs_cfg/$(LOCAL_MODULE).json
$(call generate_static_json_file, $(LOCAL_JSON_FILE))

# H2XML generator
$(foreach h2xm_header, $(LOCAL_SPF_MODULE_H2XML_HEADERS), \
   $(eval RET := $(shell $(H2XML) -conf $(H2XML_CONFIG) $(H2XML_FLAGS) -o $(H2XML_OUTPUT) $(H2XML_INCLUDES) -t spfModule -i $(h2xm_header) -a "@h2xml_processors{${PROC_DOMAIN_NAME}}")) \
)

# refresh amdb autogen
RET := $(shell export BUILDPATH=""; export OPENSRC="y"; export PROJECT_BINARY_DIR=$(PROJECT_BINARY_DIR); export PROJECT_SOURCE_DIR=$(PROJECT_SOURCE_DIR); export CHIPSET=$(SPF_CHIPSET); export TGT_SPECIFIC_FOLDER=linux; export TIMESTAMP="$(SPF_TIME_STAMP)"; export ARCH=linux; export AR_VERSION=1.0-0; export AR_MAJOR_VERSION=1; export AR_MINOR_VERSION=0; export AR_PATCHLEVEL=0; python3 $(PROJECT_SOURCE_DIR)/scripts/spf/spf_libs_config_parser.py -c $(SPF_CHIPSET) -f "GEN_SHARED_LIBS")

$(call clear_var_list, $(local_spf_variables))

GLOBAL_SPF_LIBS_LIST += $(LOCAL_MODULE)
include $(BUILD_STATIC_LIBRARY)

else ifeq ($(LOCAL_SPF_MODULE_KCONFIG),m)

# generate json files
LOCAL_JSON_FILE := $(PROJECT_BINARY_DIR)/libs_cfg/$(LOCAL_MODULE).json
$(call generate_dynamic_json_file, $(LOCAL_JSON_FILE))

# H2XML generator
$(foreach h2xm_header, $(LOCAL_SPF_MODULE_H2XML_HEADERS), \
   $(eval RET := $(shell $(H2XML) -conf $(H2XML_CONFIG) $(H2XML_FLAGS) -o $(H2XML_OUTPUT) $(H2XML_INCLUDES) -t spfModule -i $(h2xm_header) -a "@h2xml_processors{${PROC_DOMAIN_NAME}}")) \
)

# refresh amdb autogen
RET := $(shell export BUILDPATH=""; export OPENSRC="y"; export PROJECT_BINARY_DIR=$(PROJECT_BINARY_DIR); export PROJECT_SOURCE_DIR=$(PROJECT_SOURCE_DIR); export CHIPSET=$(SPF_CHIPSET); export TGT_SPECIFIC_FOLDER=linux; export TIMESTAMP="$(SPF_TIME_STAMP)"; export ARCH=linux; export AR_VERSION=1.0-0; export AR_MAJOR_VERSION=1; export AR_MINOR_VERSION=0; export AR_PATCHLEVEL=0; python3 $(PROJECT_SOURCE_DIR)/scripts/spf/spf_libs_config_parser.py -c $(SPF_CHIPSET) -f "GEN_SHARED_LIBS")

$(call clear_var_list, $(local_spf_variables))

include $(BUILD_SHARED_LIBRARY)

endif


