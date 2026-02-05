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
     ${LIB_ROOT}/src
     ${LIB_ROOT}/inc
    )

#Add the source files

if (${CONFIG_IRM} MATCHES "y")
set (lib_srcs_list
     ${LIB_ROOT}/src/irm.c
     ${LIB_ROOT}/src/irm_apm_if_utils.c
     ${LIB_ROOT}/src/irm_cmd_handler.c
     ${LIB_ROOT}/src/irm_cntr_if_utils.c
     ${LIB_ROOT}/src/irm_list_utils.c
     ${LIB_ROOT}/src/irm_offload_utils.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/irm_prof_driver.c
     ${LIB_ROOT}/src/irm_static_module_utils.c
     ${LIB_ROOT}/src/irm_sim_utils.c
    )
else()
set (lib_srcs_list
     ${LIB_ROOT}/stub_src/irm_stub.c
    )
endif()

#Call spf_build_static_library to generate the static library
spf_build_static_library(irm
                         "${lib_incs_list}"
                         "${lib_srcs_list}"
                         "${lib_defs_list}"
                         "${lib_flgs_list}"
                         "${lib_link_libs_list}"
                        )
