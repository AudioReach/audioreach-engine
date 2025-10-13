#[[
   @file CMakeLists.txt

   @brief

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear

]]
cmake_minimum_required(VERSION 3.10)

#Add the sub directories
add_subdirectory(../ctrl_port/build ctrl_port)
add_subdirectory(../data_port_ops_intf_ext/build data_port_ops_intf_ext)
add_subdirectory(../dm_ext/build dm_ext)
add_subdirectory(../island_exit/build island_exit)
add_subdirectory(../metadata/build metadata)
add_subdirectory(../module_bypass/build module_bypass)
add_subdirectory(../path_delay/build path_delay)
add_subdirectory(../pcm_fwk_ext/build pcm_fwk_ext)
add_subdirectory(../prof/build prof)
add_subdirectory(../pure_st_topo/build pure_st_topo)
add_subdirectory(../sync_fwk_ext/build sync_fwk_ext)
add_subdirectory(../global_shmem_msg/build global_shmem_msg)
add_subdirectory(../thin_topo/build thin_topo)