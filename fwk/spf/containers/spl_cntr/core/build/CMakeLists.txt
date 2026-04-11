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
    )

#Add the source files

if (${CONFIG_SPL_CNTR} MATCHES "y")
set (lib_srcs_list
     ${LIB_ROOT}/src/spl_cntr_buf_util.c
     ${LIB_ROOT}/src/spl_cntr_cmd_handler.c
     ${LIB_ROOT}/src/spl_cntr_data_handler.c
     ${LIB_ROOT}/src/spl_cntr_eos_util.c
     ${LIB_ROOT}/src/spl_cntr_event_util.c
     ${LIB_ROOT}/src/spl_cntr_ext_port_util.c
     ${LIB_ROOT}/src/spl_cntr_fwk_extns.c
     ${LIB_ROOT}/src/spl_cntr_gpd.c
     ${LIB_ROOT}/src/spl_cntr_pm.c
     ${LIB_ROOT}/src/spl_cntr.c
    )
else()
# Stub compilation
set (lib_srcs_list
     ${LIB_ROOT}/stub_src/spl_cntr.c
     )
endif()

#Call spf_build_static_library to generate the static library
spf_build_static_library(spl_cntr_main
                         "${lib_incs_list}"
                         "${lib_srcs_list}"
                         "${lib_defs_list}"
                         "${lib_flgs_list}"
                         "${lib_link_libs_list}"
                        )
