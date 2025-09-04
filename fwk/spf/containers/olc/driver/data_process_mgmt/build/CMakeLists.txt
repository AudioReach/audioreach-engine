#[[
   @file CMakeLists.txt

   @brief

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear

]]
cmake_minimum_required(VERSION 3.10)

set (lib_incs_list
     ../../inc
     ${LIB_ROOT}/inc
     ${LIB_ROOT}/src
    )

#Add the source files
set (lib_srcs_list
     ${LIB_ROOT}/src/spdm_data_buf_handler.c
     ${LIB_ROOT}/src/spdm_data_handler.c
     ${LIB_ROOT}/src/spdm_data_msg_handler.c
     ${LIB_ROOT}/src/spdm_data_port_handler.c
     ${LIB_ROOT}/src/spdm_media_fmt_handler.c
     ${LIB_ROOT}/src/spdm_meta_data_handler.c
     ${LIB_ROOT}/src/spdm_port_config_handler.c
     ${LIB_ROOT}/src/spdm_port_event_handler.c
     ${LIB_ROOT}/src/spdm_state_prop_handler.c
     ${LIB_ROOT}/src/spdm_utils.c
    )

#Call spf_build_static_library to generate the static library
spf_build_static_library(data_process
                         "${lib_incs_list}"
                         "${lib_srcs_list}"
                         "${lib_defs_list}"
                         "${lib_flgs_list}"
                         "${lib_link_libs_list}"
                        )
