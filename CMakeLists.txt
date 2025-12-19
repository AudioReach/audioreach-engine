# Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

## VARIABLES:
#
# ARCH: Specify SoC Architecture
#    Supported Values: "hexagon, linux"
# V_ARCH: Arch variant
# CONFIG: config file to use
#
set(AR_MAJOR_VERSION 1)
set(AR_MINOR_VERSION 0)
set(AR_PATCHLEVEL 0)
set(NAME "AR")
set(AR_VERSION "AR-${AR_MAJOR_VERSION}.${AR_MINOR_VERSION}-${AR_PATCHLEVEL}")
cmake_minimum_required(VERSION 3.24)  

project(AR VERSION 1.0)
set(H2XML "${PROJECT_SOURCE_DIR}/scripts/h2xml/h2xml")
set(H2XML_CONFIG "${PROJECT_SOURCE_DIR}/scripts/h2xml/h2xml_config_spf.xml")
set(H2XML_FLAGS -D __H2XML__)
set(H2XML_INCLUDES "" )
set(spf_json_entries "")
set(spf_json_file ${PROJECT_BINARY_DIR}/spf.json)
set(SPF_CHIPSET "SDM845")
set(SPF_TIME_STAMP "")
string(TIMESTAMP SPF_TIME_STAMP "[%Y-%m-%d %H:%M:%S]" UTC)

option(ARSDK_BUILD_ENABLED  "AudioReach SDK build" ON)
option(CONFIG_DYNAMIC_LOADING "option to include the required source files for dynamic loading support" ON)
option(DISABLE_UNIT_TEST "preprocessor defintion to disable APM unit tests" ON)
set(LIB_ROOT ../)
#Set the global property to generate the list of SPF static libs
set_property(GLOBAL PROPERTY GLOBAL_SPF_LIBS_LIST)

if(ARSDK_BUILD_ENABLED)
    add_definitions(-DARSDK_BUILD_ENABLED)
endif()

if ((NOT ARCH MATCHES "^(hexagon)") AND (NOT ARCH MATCHES "^(linux)"))
   message(FATAL_ERROR
      "Please specify supported Architecture.\n")
endif()

#for DEBUG
#set(CMAKE_VERBOSE_MAKEFILE true)

if(${CMAKE_VERSION} VERSION_LESS "3.12.0")
        # FindPythonInterp is bugged and may sometimes be unable to find
        # Python 3 when both Python 2 & 3 are in PATH,
        # so it's always better to use CMake 3.12+
        find_package(PythonInterp 3.0)
        set(PYTHON3 "${PYTHON_EXECUTABLE}")
else()
        find_package(Python3 COMPONENTS Interpreter)
        set(PYTHON3 "${Python3_EXECUTABLE}")
endif()

# Kconfig related setup
set(SPF_DOT_CONFIG_PATH ${PROJECT_BINARY_DIR}/.config)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${PROJECT_BINARY_DIR} ${SPF_DOT_CONFIG_PATH})

if (NOT CONFIG)
   set (CONFIG "defconfig")
endif()

message(STATUS "using ${PROJECT_SOURCE_DIR}/arch/${ARCH}/configs/${CONFIG} config")
configure_file("${PROJECT_SOURCE_DIR}/arch/${ARCH}/configs/${CONFIG}" ${SPF_DOT_CONFIG_PATH} COPYONLY)

include(scripts/cmake/spf.cmake)
include(scripts/cmake/spf_build.cmake)

spf_h2xml_include_directories(
fwk/api/apm
fwk/api/ar_utils
fwk/api/modules
${PROJECT_SOURCE_DIR}/ar_osal/api
modules/processing/channel_mixer/api
modules/audio/pcm_decoder/api
modules/audio/pcm_encoder/api
modules/cmn/pcm_mf_cnv/capi/mfc/api
modules/cmn/pcm_mf_cnv/capi/pcm_cnv/api
modules/processing/gain_control/drc/api
modules/processing/gain_control/iir_mbdrc/api
modules/cmn/api
modules/processing/volume_control/capi/gain/api
modules/processing/volume_control/capi/soft_vol/api
modules/cmn/common/internal_api
modules/processing/PoplessEqualizer/api
modules/processing/gain_control/limiter/api
modules/cmn/simple_accumulator_limiter/api
)
###
###   Some strings used for picking inc paths based on arch, tgt/sim & static/shared.
###   ARCH (hexagon), STATIC_SHARED_DIR_NAME (static, shared), SIM_TGT_STR (sim, tgt)
###
set(STATIC_SHARED_DIR_NAME "shared")
if (${CONFIG_SPF_AS_STATIC_LIB} MATCHES "y")
   set(STATIC_SHARED_DIR_NAME "static")
endif()

set(SIM_TGT_STR "sim")

set(SPF_KCONFIG_PREFIX "CONFIG_")
set(SPF_KCONFIG_ENCODING "UTF-8")
spf_parse_kconfig()

include(scripts/cmake/${ARCH}-toolchain.cmake OPTIONAL)

# Check for supported PROC_DOMAIN configuration
if(NOT DEFINED CONFIG_PROC_DOMAIN OR "${CONFIG_PROC_DOMAIN}" STREQUAL "")
   message(FATAL_ERROR "Undefined/Empty PROC_DOMAIN configuration.\n")
endif()

check_supported_proc_domain(${CONFIG_PROC_DOMAIN})
set(PROC_DOMAIN_NAME "PROC_DOMAIN_${CONFIG_PROC_DOMAIN}")

if ( ARCH MATCHES "^(hexagon)")
   set(hexagon_INCLUDE
      ${PROJECT_SOURCE_DIR}/ar_osal/api
      ${HEXAGON_LIB_DIR}
   )
   set(TGT_SPECIFIC_FOLDER hexagon)
endif()

if ( ARCH MATCHES "^(linux)")
   set(linux_INCLUDE
      ${PROJECT_SOURCE_DIR}/ar_osal/api
   )
   set(TGT_SPECIFIC_FOLDER linux)
endif()

## Global Includes
include_directories(
   ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/module/audio_cmn_lib/
   ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/module/capi_cmn/cmn/inc/
   ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/module/shared_lib_api/inc/
   ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/module/capi/
   ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/module/metadata/api/
   ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/module/capi/adv/
   ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/module/capi_cmn/ctrl_port/inc
   ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/module/internal_api/modules_prv/api/
   ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/module/imcl/api
   ${PROJECT_SOURCE_DIR}/fwk/api/modules/
   ${PROJECT_SOURCE_DIR}/fwk/api/apm/
   ${PROJECT_SOURCE_DIR}/fwk/spf/apm/core/inc/
   ${PROJECT_SOURCE_DIR}/fwk/api/apm/private
   ${PROJECT_SOURCE_DIR}/fwk/api/vcpm/
   ${PROJECT_SOURCE_DIR}/fwk/api/private
   ${PROJECT_SOURCE_DIR}/fwk/api/ar_utils/
   ${PROJECT_SOURCE_DIR}/fwk/api/ar_utils/generic
   ${PROJECT_SOURCE_DIR}/fwk/platform/posal/inc/${TGT_SPECIFIC_FOLDER}/stringl
   ${PROJECT_SOURCE_DIR}/fwk/platform/posal/inc/${TGT_SPECIFIC_FOLDER}
   ${PROJECT_SOURCE_DIR}/fwk/platform/posal/inc
   ${hexagon_INCLUDE}
   ${linux_INCLUDE}
   ${PROJECT_SOURCE_DIR}/fwk/platform/posal/inc/generic
   ${PROJECT_SOURCE_DIR}/gpr/api
   ${PROJECT_SOURCE_DIR}/fwk/spf/utils/cmn/inc
   ${PROJECT_SOURCE_DIR}/fwk/spf/utils/interleaver/inc
   ${PROJECT_SOURCE_DIR}/fwk/spf/utils/cmn/api
   ${PROJECT_SOURCE_DIR}/fwk/spf/utils/list/inc
   ${PROJECT_SOURCE_DIR}/fwk/spf/dls/api
   ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/fwk/api
   )

set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--export-dynamic")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/empty.c" "")

if (${CONFIG_SPF_AS_STATIC_LIB} MATCHES "y")
   add_library(spf STATIC "" )
else()
   add_library(spf SHARED "${CMAKE_CURRENT_BINARY_DIR}/empty.c" )
endif()

message (STATUS "Generating AMDB configs")

# Execute before amdb target is configured as amdb target uses generated
# ouput files from spf_libs_config_parser.py execution.
execute_process(
   COMMAND ${CMAKE_COMMAND} -E env
      BUILDPATH=""
      OPENSRC="y"
      PROJECT_BINARY_DIR=${PROJECT_BINARY_DIR}
      PROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
      CHIPSET=${SPF_CHIPSET}
      TGT_SPECIFIC_FOLDER=${TGT_SPECIFIC_FOLDER}
      TIMESTAMP=${SPF_TIME_STAMP}
      ARCH=${ARCH}
      AR_VERSION=${AR_VERSION}
      AR_MAJOR_VERSION=${AR_MAJOR_VERSION}
      AR_MINOR_VERSION=${AR_MINOR_VERSION}
      AR_PATCHLEVEL=${AR_PATCHLEVEL}
      ${PYTHON3} ${PROJECT_SOURCE_DIR}/scripts/spf/spf_libs_config_parser.py -c ${SPF_CHIPSET} -f "GEN_SHARED_LIBS,AVS_TEST"
   RESULTS_VARIABLE AMDB_AUTOGEN_OUTPUT
)

message(STATUS "AMDB config generation result: ${AMDB_AUTOGEN_OUTPUT}")

add_custom_target(
   menuconfig
   COMMAND ${CMAKE_COMMAND} -E env
      srctree=${PROJECT_SOURCE_DIR}
      CC_VERSION_TEXT=${CC_VERSION_TEXT}
      ARCH=${ARCH}
      AR_VERSION=${AR_VERSION}
      AR_MAJOR_VERSION=${AR_MAJOR_VERSION}
      AR_MINOR_VERSION=${AR_MINOR_VERSION}
      AR_PATCHLEVEL=${AR_PATCHLEVEL}
      ${PYTHON3} ${PROJECT_SOURCE_DIR}/scripts/kconfig/menuconfig.py
      ${PROJECT_SOURCE_DIR}/Kconfig
   WORKING_DIRECTORY ${GENERATED_DIRECTORY}
   COMMENT "MenuConfig"
   VERBATIM
   USES_TERMINAL
)

add_compile_definitions(AMDB_REG_SPF_MODULES)
add_subdirectory(fwk/build)
add_subdirectory(modules)

#Read the list of libraries that were added using the global property
get_property(spf_static_libs GLOBAL PROPERTY GLOBAL_SPF_LIBS_LIST)

if ( ARCH MATCHES "^(linux)")
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPR REQUIRED IMPORTED_TARGET gpr)
target_link_libraries(spf PUBLIC PkgConfig::GPR)
endif()

target_link_libraries(spf PUBLIC "$<LINK_GROUP:RESCAN,${spf_static_libs}>" "-Wl,--allow-multiple-definition")

# Install header APIs to support ARE on APPS. These APIs are needed by
# audioreach-graphmgr (AGM) server to initialize audioreach-engine framework.
file(GLOB POSAL_INC ./fwk/platform/posal/inc/*.h)
file(GLOB_RECURSE POSAL_${TGT_SPECIFIC_FOLDER}_INC ./fwk/platform/posal/inc/${TGT_SPECIFIC_FOLDER}/*.h)
set(HEADER_APIS
	${POSAL_INC}
	${POSAL_${TGT_SPECIFIC_FOLDER}_INC}
	${PROJECT_SOURCE_DIR}/fwk/spf/utils/cmn/inc/spf_main.h
	${PROJECT_SOURCE_DIR}/fwk/api/ar_utils/ar_defs.h
	${PROJECT_SOURCE_DIR}/fwk/api/ar_utils/generic/ar_msg.h
	${PROJECT_SOURCE_DIR}/fwk/api/ar_utils/ar_error_codes.h
	)

install (FILES ${HEADER_APIS} DESTINATION include)
install(TARGETS spf RUNTIME DESTINATION lib)