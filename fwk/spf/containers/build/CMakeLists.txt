#[[
   @file CMakeLists.txt

   @brief

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear

]]
cmake_minimum_required(VERSION 3.10)

#Include directories
include_directories(
                    ../cmn/container_utils/core/inc
                    ../cmn/container_utils/core/inc/generic
                    ../cmn/container_utils/ext/ctrl_port/inc
                    ../cmn/container_utils/ext/duty_cycle/inc
                    ../cmn/container_utils/ext/island_exit/inc
                    ../cmn/container_utils/ext/offload/inc
                    ../cmn/container_utils/ext/path_delay/inc
                    ../cmn/container_utils/ext/prof/inc
                    ../cmn/container_utils/ext/soft_timer_fwk_ext/inc
                    ../cmn/container_utils/ext/global_shmem_msg/inc
                    ../cmn/container_utils/ext/voice/inc
                    ../cmn/graph_utils/inc
                    ../cmn/icb/inc
                    ../cmn/topologies/gen_topo/core/inc
                    ../cmn/topologies/gen_topo/ext/module_bypass/inc
                    ../cmn/topologies/topo_interface/inc
                    ../cmn/topologies/gen_topo/ext/ctrl_port/inc
                    ../cmn/topologies/gen_topo/ext/data_port_ops_intf_ext/inc
                    ../cmn/topologies/gen_topo/ext/dm_ext/inc
                    ../cmn/topologies/gen_topo/ext/island_exit/inc
                    ../cmn/topologies/gen_topo/ext/metadata/inc
                    ../cmn/topologies/gen_topo/ext/module_bypass/inc
                    ../cmn/topologies/gen_topo/ext/path_delay/inc
                    ../cmn/topologies/gen_topo/ext/pcm_fwk_ext/inc
                    ../cmn/topologies/gen_topo/ext/prof/inc
                    ../cmn/topologies/gen_topo/ext/pure_st_topo/inc
                    ../cmn/topologies/gen_topo/ext/sync_fwk_ext/inc
                    ../cmn/topologies/gen_topo/ext/global_shmem_msg/inc
                    ../cmn/topologies/gen_topo/ext/thin_topo/inc
                    ../cmn/topologies/gen_topo/ext/thin_topo/src
                    ../cmn/topologies/spl_topo/core/inc
                    ../cmn/topologies/spl_topo/ext/dm_fwk_ext/inc
                    ../cmn/topologies/spl_topo/ext/mimo_proc_state_intf_ext/inc
                    ../cmn/topologies/spl_topo/ext/sync_fwk_ext/inc
                    ../cmn/topologies/spl_topo/ext/trigger_policy_fwk_ext/inc
                    ../cmn/topologies/topo_utils/inc
                    ../gen_cntr/core/inc
                    ../gen_cntr/ext/thin_topo_cntr_utils/inc
                    ../olc/inc
                    ../spl_cntr/inc
                   )

#Add the sub directories
add_subdirectory(../cmn/build cmn)
add_subdirectory(../gen_cntr/build gen_cntr)
add_subdirectory(../spl_cntr/build spl_cntr)
add_subdirectory(../olc/build olc)
