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
     ../../../apm/ext/debug_info_dump/inc
     ../../../interfaces/module/offload/api
    )

#Add the source files
set (lib_srcs_list
     ${LIB_ROOT}/src/spf_bufmgr_island.c
     ${LIB_ROOT}/src/spf_bufmgr.c
     ${LIB_ROOT}/src/spf_debug_info_dump.c
     ${LIB_ROOT}/src/spf_hashtable.c
     ${LIB_ROOT}/src/spf_main.c
     ${LIB_ROOT}/src/spf_msg_utils_island.c
     ${LIB_ROOT}/src/spf_msg_utils.c
     ${LIB_ROOT}/src/spf_ref_counter.c
     ${LIB_ROOT}/src/spf_svc_calib.c
     ${LIB_ROOT}/src/spf_svc_utils.c
     ${LIB_ROOT}/src/spf_sys_util.c
    )

#Add the compiler flags
set (lib_flgs_list
     -Wno-address-of-packed-member
    )

#Call spf_build_static_library to generate the static library
spf_build_static_library(spf_utils
                         "${lib_incs_list}"
                         "${lib_srcs_list}"
                         "${lib_defs_list}"
                         "${lib_flgs_list}"
                         "${lib_link_libs_list}"
                        )
