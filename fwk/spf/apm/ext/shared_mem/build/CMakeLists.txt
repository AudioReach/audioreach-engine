#[[
   @file CMakeLists.txt

   @brief
]]
cmake_minimum_required(VERSION 3.10)

#Add the source files
set (lib_srcs_list
     ${LIB_ROOT}/src/apm_shmem_util.c
     ${LIB_ROOT}/src/apm_shmem_util_v2.c
    )

#Call spf_build_static_library to generate the static library
spf_build_static_library(shared_mem
                         "${lib_incs_list}"
                         "${lib_srcs_list}"
                         "${lib_defs_list}"
                         "${lib_flgs_list}"
                         "${lib_link_libs_list}"
                        )
