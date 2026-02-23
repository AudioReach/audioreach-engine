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
                    ../mux_demux/api
                    ../spr/api
                    ../../utils/interleaver/inc
                   )

#Add the sub directories
add_subdirectory(../data_logging/build data_logging)
add_subdirectory(../irm/build irm)
add_subdirectory(../mux_demux/build mux_demux)
add_subdirectory(../rat/build rat)
add_subdirectory(../latency/build latency)
add_subdirectory(../simple_splitter/build simple_splitter)
add_subdirectory(../data_marker/build data_marker)
add_subdirectory(../sh_mem_pull_push_mode/build sh_mem_pull_push_mode)
add_subdirectory(../sync_module/build sync_module)
add_subdirectory(../priority_sync/build priority_sync)
add_subdirectory(../spr/build spr)
