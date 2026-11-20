#[[
   @file CMakeLists.txt

   @brief Build for OSRC-based Media Format Converter (MODULE_ID_MFC_OSRC).

   The opensource_resampler lib sources are compiled directly into this target
   rather than as a separate subdirectory — it is not a CAPI module of its own.
]]
cmake_minimum_required(VERSION 3.10)

set(mfc_osrc_sources
    ${LIB_ROOT}/capi/src/capi_mfc_osrc.cpp
    ${LIB_ROOT}/capi/src/capi_mfc_osrc_island.cpp
    ${LIB_ROOT}/capi/src/capi_mfc_osrc_utils.cpp
    ${LIB_ROOT}/capi/src/capi_mfc_osrc_utils_island.cpp
    ${LIB_ROOT}/lib/src/mfc_osrc_converter.c
    ${LIB_ROOT}/lib/src/mfc_osrc_converter_island.c
    ${LIB_ROOT}/lib/src/mfc_osrc_init.c
    ${LIB_ROOT}/lib/src/mfc_osrc_init_island.c
    ${LIB_ROOT}/lib/src/mfc_osrc_process.c
    ${LIB_ROOT}/lib/src/mfc_osrc_process_island.c
    ${LIB_ROOT}/lib/src/pc_float/pc_float.c
    ${PROJECT_SOURCE_DIR}/modules/processing/resamplers/opensource_resampler/lib/src/osrc_rs_lib_wrapper.c
    ${PROJECT_SOURCE_DIR}/modules/processing/resamplers/opensource_resampler/lib/src/resampler.c
    ${PROJECT_SOURCE_DIR}/modules/processing/resamplers/opensource_resampler/lib/src/extrapolator.c
)

set(mfc_osrc_includes
    ${LIB_ROOT}/api
    ${LIB_ROOT}/capi/inc
    ${LIB_ROOT}/capi/src
    ${LIB_ROOT}/lib/inc
    ${PROJECT_SOURCE_DIR}/modules/processing/channel_mixer/lib/inc
    ${PROJECT_SOURCE_DIR}/modules/processing/channel_mixer/api
    ${PROJECT_SOURCE_DIR}/modules/processing/resamplers/opensource_resampler/lib/inc
    ${PROJECT_SOURCE_DIR}/modules/processing/resamplers/opensource_resampler/api
    ${PROJECT_SOURCE_DIR}/fwk/spf/utils/cmn/inc
    ${PROJECT_SOURCE_DIR}/fwk/spf/utils/interleaver/inc
    ${PROJECT_SOURCE_DIR}/fwk/spf/utils/lpi_pool/inc
    ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/fwk/api
    ${PROJECT_SOURCE_DIR}/fwk/spf/containers/cmn/graph_utils/inc
    ${PROJECT_SOURCE_DIR}/fwk/spf/interfaces/module/shared_lib_api/inc/generic
)

spf_module_sources(
   KCONFIG        CONFIG_MFC_OSRC
   NAME           mfc_osrc
   MAJOR_VER      1
   MINOR_VER      0
   AMDB_ITYPE     "capi"
   AMDB_MTYPE     "PP"
   AMDB_MID       "0x07001192"
   AMDB_TAG       "capi_mfc_osrc"
   AMDB_MOD_NAME  "MODULE_ID_MFC_OSRC"
   SRCS           ${mfc_osrc_sources}
   INCLUDES       ${mfc_osrc_includes}
   H2XML_HEADERS  "${LIB_ROOT}/api/mfc_osrc_api.h"
   CFLAGS         ""
)
