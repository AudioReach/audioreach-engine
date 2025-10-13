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
     ${LIB_ROOT}/inc
     ${LIB_ROOT}/src
    )

#Add the source files
set (lib_srcs_list
     ${LIB_ROOT}/src/gen_topo.c
     ${LIB_ROOT}/src/gen_topo_buf_util_island.c
     ${LIB_ROOT}/src/gen_topo_capi.c
     ${LIB_ROOT}/src/gen_topo_capi_cb_handler.c
     ${LIB_ROOT}/src/gen_topo_capi_cb_handler_island.c
     ${LIB_ROOT}/src/gen_topo_data_flow_state.c
     ${LIB_ROOT}/src/gen_topo_data_flow_state_md_island.c
     ${LIB_ROOT}/src/gen_topo_data_process.c
     ${LIB_ROOT}/src/gen_topo_debug_info.c
     ${LIB_ROOT}/src/gen_topo_data_process_island.c
     ${LIB_ROOT}/src/gen_topo_fwk_extn_utils.c
     ${LIB_ROOT}/src/gen_topo_intf_extn_utils.c
     ${LIB_ROOT}/src/gen_topo_island.c
     ${LIB_ROOT}/src/gen_topo_pm.c
     ${LIB_ROOT}/src/gen_topo_propagation.c
     ${LIB_ROOT}/src/gen_topo_public_functions.c
     ${LIB_ROOT}/src/gen_topo_public_functions_md_island.c
     ${LIB_ROOT}/src/gen_topo_trigger_policy.c
     ${LIB_ROOT}/src/gen_topo_trigger_policy_island.c
     ${LIB_ROOT}/src/gen_topo_trigger_policy_change_island.c
     ${LIB_ROOT}/src/topo_buf_mgr.c
    )

#Call spf_build_static_library to generate the static library
spf_build_static_library(gen_topo_core
                         "${lib_incs_list}"
                         "${lib_srcs_list}"
                         "${lib_defs_list}"
                         "${lib_flgs_list}"
                         "${lib_link_libs_list}"
                        )
