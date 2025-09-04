#[[
   @file CMakeLists.txt

   @brief

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear

]]
cmake_minimum_required(VERSION 3.10)

set (lib_incs_list
     ${LIB_ROOT}/inc
     ${LIB_ROOT}/src
     ../../inc
    )

#Add the source files
set (lib_srcs_list
     ${LIB_ROOT}/src/sgm.c
     ${LIB_ROOT}/src/sgm_cmd_handler.c
     ${LIB_ROOT}/src/sgm_cmd_handler_utils.c
     ${LIB_ROOT}/src/sgm_cmd_parser_utils.c
     ${LIB_ROOT}/src/sgm_event_handler.c
     ${LIB_ROOT}/src/sgm_path_delay_utils.c
     ${LIB_ROOT}/src/sgm_rsp_handler.c
     ${LIB_ROOT}/src/sgm_servreg_event_handler.c
    )

#Call spf_build_static_library to generate the static library
spf_build_static_library(satellite_graph
                         "${lib_incs_list}"
                         "${lib_srcs_list}"
                         "${lib_defs_list}"
                         "${lib_flgs_list}"
                         "${lib_link_libs_list}"
                        )
