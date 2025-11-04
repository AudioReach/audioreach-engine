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
                    ${LIB_ROOT}/cmn/inc
                    ${LIB_ROOT}/core/src
                    ${LIB_ROOT}/driver/inc
                    ${LIB_ROOT}/driver/data_process_mgmt/inc
                    ${LIB_ROOT}/driver/data_process_mgmt/src
                    ${LIB_ROOT}/driver/resource_mgmt_utils/inc
                    ${LIB_ROOT}/driver/resource_mgmt_utils/src
                    ${LIB_ROOT}/driver/satellite_graph_mgmt/inc
                    ${LIB_ROOT}/driver/satellite_graph_mgmt/src
                   )

#Add the sub directory
add_subdirectory(${LIB_ROOT}/core/build core)
add_subdirectory(${LIB_ROOT}/driver/build driver)
