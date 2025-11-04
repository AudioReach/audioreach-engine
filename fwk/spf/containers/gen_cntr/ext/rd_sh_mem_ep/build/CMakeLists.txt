#[[
   @file CMakeLists.txt

   @brief

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear

]]
cmake_minimum_required(VERSION 3.10)

#Include directories
set(rd_sh_mem_ep_sources
     ${LIB_ROOT}/src/gen_cntr_rd_sh_mem_ep.c
    )

get_property(dir_includes DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)

#Add the source files
set (rd_sh_mem_ep_includes
     ${LIB_ROOT}/inc
     ${dir_includes}
    )

spf_module_sources(
     KCONFIG        CONFIG_RD_SH_MEM_EP
     NAME      rd_sh_mem_ep
     MAJOR_VER 1
     MINOR_VER 0
     AMDB_ITYPE     "capi"
     AMDB_MTYPE     "framework"
     AMDB_MID  "0x07001001"
     AMDB_TAG  "framework"
     AMDB_MOD_NAME  "MODULE_ID_RD_SHARED_MEM_EP"
     SRCS      ${rd_sh_mem_ep_sources}
     INCLUDES  ${rd_sh_mem_ep_includes}
     H2XML_HEADERS  "${PROJECT_SOURCE_DIR}/fwk/api/modules/rd_sh_mem_ep_api.h"
     QACT_MODULE_TYPE    "generic"
     CFLAGS         "-Wno-address-of-packed-member"
)
