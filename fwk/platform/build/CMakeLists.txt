#[[
   @file CMakeLists.txt

   @brief

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear

   // clang-format off
   // clang-format on
]]
cmake_minimum_required(VERSION 3.10)

#Include directories
include_directories()

#Add the sub directories
add_subdirectory(../modules/build modules)

if (DEFINED CONFIG_COMPILE_POSAL)
add_subdirectory(../posal/build posal)
endif()
