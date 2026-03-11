#[[
   @file CMakeLists.txt

   @brief
]]
cmake_minimum_required(VERSION 3.10)

#Add the source files
set (lib_srcs_list
     ${LIB_ROOT}/src/apm_multi_client_db.c
     ${LIB_ROOT}/src/apm_multi_client_msg_rsp_handler.c
     ${LIB_ROOT}/src/apm_multi_client_utils.c
     ${LIB_ROOT}/src/apm_multi_client.c
    )

#Call spf_build_static_library to generate the static library
spf_build_static_library(apm_multi_client
                         "${lib_incs_list}"
                         "${lib_srcs_list}"
                         "${lib_defs_list}"
                         "${lib_flgs_list}"
                         "${lib_link_libs_list}"
                        )
