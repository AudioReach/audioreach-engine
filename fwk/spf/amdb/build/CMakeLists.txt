#[[
   @file CMakeLists.txt

   @brief

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear

]]
cmake_minimum_required(VERSION 3.10)

#Include directories
set (lib_incs_list
     ${LIB_ROOT}/core/inc
     ${LIB_ROOT}/core/src
     ${LIB_ROOT}/ext/inc
     ${LIB_ROOT}/ext/src
    )

#Add the auto-generated source files
set(AUTO_GENERATE_DIR ${LIB_ROOT}/autogen/${TGT_SPECIFIC_FOLDER}/)

set(amdb_autogen_SOURCES
    ${AUTO_GENERATE_DIR}/spf_static_build_config.c
    ${AUTO_GENERATE_DIR}/spf_shared_build_config.c
    ${AUTO_GENERATE_DIR}/spf_static_build_config_private.c
    ${AUTO_GENERATE_DIR}/spf_shared_build_config_private.c
)

#Add the core source files
set (lib_srcs_list
     ${LIB_ROOT}/core/src/private/amdb_private_utils.c
     ${LIB_ROOT}/core/src/amdb.c
     ${LIB_ROOT}/core/src/amdb_capi.c
     ${LIB_ROOT}/core/src/amdb_capi_island.c
     ${LIB_ROOT}/core/src/amdb_utils.c
     ${amdb_autogen_SOURCES}
    )

#GEN_SHARED_LIBS in scons and CONFIG_DYNAMIC_LOADING in CMake serves the same purpose
if(NOT CONFIG_DYNAMIC_LOADING)
   list (APPEND lib_srcs_list
         ${LIB_ROOT}/ext/stub/amdb_offload_utils.c
         ${LIB_ROOT}/core/src/amdb_static_loading.c
         ${LIB_ROOT}/ext/stub/amdb_thread_stub.c
        )
else()
   list (APPEND lib_srcs_list
         ${LIB_ROOT}/ext/src/amdb_cmd_handler.c
         ${LIB_ROOT}/ext/src/amdb_dynamic_loading.c
         ${LIB_ROOT}/ext/src/amdb_offload_utils.c
         ${LIB_ROOT}/ext/src/amdb_parallel_loader.c
         ${LIB_ROOT}/ext/src/amdb_queue.c
         ${LIB_ROOT}/ext/src/amdb_resource_voter.c
         ${LIB_ROOT}/ext/src/amdb_thread.c
        )
endif()

#Add the compiler flags
set (lib_flgs_list
     -Wno-address-of-packed-member
    )

# Add definitions for target
# When SPF is compiled standalone, SPF modules are not part of base image and
# need to be registered seperately with AMDB. Define AMDB_REG_SPF_MODULES to
# register SPF modules with AMDB when SPF is compiled standalone.

if (CONFIG_COMPILE_SPF)
   set (lib_defs_list
        AMDB_REG_SPF_MODULES
   )
endif()

#Add definitions for target
if(CONFIG_DYNAMIC_LOADING)
set (lib_defs_list
        DL_INFO_DEFINED
    )
endif()

#Call spf_build_static_library to generate the static library
spf_build_static_library(amdb
                         "${lib_incs_list}"
                         "${lib_srcs_list}"
                         "${lib_defs_list}"
                         "${lib_flgs_list}"
                         "${lib_link_libs_list}"
                        )
