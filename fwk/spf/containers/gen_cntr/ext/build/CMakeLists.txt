#[[
   @file CMakeLists.txt

   @brief

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear

]]
cmake_minimum_required(VERSION 3.10)

include_directories(
                    ../cmn_sh_mem/inc
                   )

#Add the sub directories
add_subdirectory(../bt_codec_fwk_ext/build bt_codec_fwk_ext)
add_subdirectory(../cmn_sh_mem/build cmn_sh_mem)
add_subdirectory(../err_check/build err_check)
add_subdirectory(../metadata/build metadata)
add_subdirectory(../offload/build offload)
add_subdirectory(../peer_cntr/build peer_cntr)
add_subdirectory(../placeholder/build placeholder)
add_subdirectory(../pure_st/build pure_st)
add_subdirectory(../pass_thru_cntr/build pass_thru_cntr)
add_subdirectory(../rd_sh_mem_ep/build rd_sh_mem_ep)
add_subdirectory(../sync_fwk_ext/build sync_fwk_ext)
add_subdirectory(../wr_sh_mem_ep/build wr_sh_mem_ep)
add_subdirectory(../thin_topo_cntr_utils/build thin_topo_cntr_utils)