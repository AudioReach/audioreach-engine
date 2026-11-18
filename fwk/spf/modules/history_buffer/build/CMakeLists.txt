#[[
   @file CMakeLists.txt

   @brief
]]
cmake_minimum_required(VERSION 3.10)

set(history_buffer_sources
    ${LIB_ROOT}capi/src/capi_history_buffer_imcl_utils.c
    ${LIB_ROOT}capi/src/capi_history_buffer_utils.c
    ${LIB_ROOT}capi/src/capi_history_buffer.c
)

set(history_buffer_includes
    ${LIB_ROOT}capi/inc
    ${LIB_ROOT}capi/src
    ${LIB_ROOT}api
)
spf_module_sources(
     KCONFIG        CONFIG_HISTORY_BUFFER
     NAME      history_buffer
     MAJOR_VER 1
     MINOR_VER 0
     AMDB_ITYPE     "capi"
     AMDB_MTYPE     "Detector"
     AMDB_MID  "0x07001182"
     AMDB_TAG  "capi_history_buffer"
     AMDB_MOD_NAME  "MODULE_ID_HISTORY_BUFFER"
     SRCS      ${history_buffer_sources}
     INCLUDES  ${history_buffer_includes}
     H2XML_HEADERS  "${LIB_ROOT}api/history_buffer_api.h"
     CFLAGS         "-Wno-address-of-packed-member"
)
