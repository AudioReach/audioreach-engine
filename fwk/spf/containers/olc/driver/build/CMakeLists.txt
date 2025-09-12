#[[
   @file CMakeLists.txt

   @brief

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear

]]
cmake_minimum_required(VERSION 3.10)

#Add the sub directory
add_subdirectory(${LIB_ROOT}/data_process_mgmt/build data_process)
add_subdirectory(${LIB_ROOT}/resource_mgmt_utils/build resource_mgmt)
add_subdirectory(${LIB_ROOT}/satellite_graph_mgmt/build satellite_graph)
