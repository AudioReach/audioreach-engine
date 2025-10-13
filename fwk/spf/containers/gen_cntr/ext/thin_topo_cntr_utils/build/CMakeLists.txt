#[[
   @file CMakeLists.txt

   @brief
]]
cmake_minimum_required(VERSION 3.10)

#Include directories
set (lib_incs_list
     ${LIB_ROOT}/inc
     ${LIB_ROOT}/src
    )

#Add the source files
set (lib_srcs_list
     ${LIB_ROOT}/src/thin_topo_cntr_utils_island.c
     ${LIB_ROOT}/src/thin_topo_cntr_utils.c
    )

#Call spf_build_static_library to generate the static library
spf_build_static_library(thin_topo_cntr_utils
                         "${lib_incs_list}"
                         "${lib_srcs_list}"
                         "${lib_defs_list}"
                         "${lib_flgs_list}"
                         "${lib_link_libs_list}"
                        )
