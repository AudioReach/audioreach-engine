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
                    ../dls/inc
                    ../apm/core/inc
                    ../apm/ext/inc
                    ../apm/ext/offload/inc
                    ../apm/cmn/inc
                    ../apm/graph_utils/inc
                    ../amdb/core/inc
                    ../containers/cmn/graph_utils/inc
                    ../containers/cmn/topologies/gen_topo/core/inc
                    ../interfaces/module/offload/api
                    ../interfaces/module/capi_cmn/ctrl_port/inc
                    ../modules/data_logging/api
                    ../modules/irm/inc
                    ../modules/irm/inc/hexagon
                    ../modules/irm/api
                    ../modules/rat/api
                    ../modules/sh_mem_pull_push_mode/api
                    ../utils/thread_pool/inc
                    ../utils/watchdog_svc/inc
                   )

#Add the sub directories
add_subdirectory(../dls/build dls)
add_subdirectory(../amdb/build amdb)
add_subdirectory(../apm/build apm)
add_subdirectory(../containers/build containers)
add_subdirectory(../interfaces/build interfaces)
add_subdirectory(../modules/build modules)
add_subdirectory(../utils/build utils)
