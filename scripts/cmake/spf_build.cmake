#[[
   @file
   spf_build.cmake

   @brief
   This file defines the functions to collect the source files and include paths.

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear
]]

#[[
   function      : spf_build_static_library

   description   : generates a static library with the received lib name, list of
                   src files, include paths, definitions and build flags

   input param0  : static library name
   input param1  : list of include paths
   input param2  : list of source files
   input param3  : list of target definitions
   input param4  : list of build flags/options
   input param5  : list of libraries to link with the target
 ]]
function(spf_build_static_library)
    #read the arguments into local variables
    set(static_lib_name ${ARGV0})

    #create the static library with lib name passed in the first argument
    if(CONFIG_ARCH_LINUX OR CONFIG_ARCH_ZEPHYR)
        add_library(${static_lib_name} OBJECT "")
        set_property(TARGET ${static_lib_name} PROPERTY POSITION_INDEPENDENT_CODE ON)
    else()
        add_library(${static_lib_name} STATIC "")
    endif()


    #update the global property to append the static library name
    set_property(GLOBAL APPEND PROPERTY GLOBAL_SPF_LIBS_LIST ${static_lib_name})

    #set all the empty lists
    set(incs_path_list "")
    set(comp_defs_list "")
    set(comp_flgs_list "")
    set(link_libs_list "")
    set(srcs_fils_list "")

    #loop through the received inc paths and update the list
    foreach(inc_path ${ARGV1})
        #message("inc_path: ${inc_path}")
        if(IS_ABSOLUTE ${inc_path})
            list(APPEND incs_path_list ${inc_path})
        else()
            list(APPEND incs_path_list ${CMAKE_CURRENT_SOURCE_DIR}/${inc_path})
        endif()
    endforeach()

    target_include_directories(${static_lib_name} PRIVATE ${incs_path_list})

    #loop through the received compiler definitions and update the list
    foreach(comp_def ${ARGV3})
        #message("comp_def: ${comp_def}")
        list(APPEND comp_defs_list ${comp_def})
    endforeach()

    #message("comp_defs_list: ${comp_defs_list}")
    target_compile_definitions(${static_lib_name} PRIVATE ${comp_defs_list})

    #loop through the received compiler flags and update the list
    foreach(comp_flag ${ARGV4})
        #message("comp_flag: ${comp_flag}")
        list(APPEND comp_flgs_list ${comp_flag})
    endforeach()

    #message("comp_flgs_list: ${comp_flgs_list}")
    add_definitions(${comp_flgs_list})

    #loop through the received compiler flags and update the list
    foreach(link_lib ${ARGV5})
        #message("link_lib: ${link_lib}")
        list(APPEND link_libs_list ${link_lib})
    endforeach()

    #message("link_libs_list: ${link_libs_list}")
    target_link_libraries(${static_lib_name} ${link_libs_list})

    #loop through the received src files and update the list
    foreach(src_file ${ARGV2})
        #message("src_file: ${src_file}")
        if(IS_ABSOLUTE ${src_file})
            list(APPEND srcs_fils_list ${src_file})
        else()
            list(APPEND srcs_fils_list ${CMAKE_CURRENT_SOURCE_DIR}/${src_file})
        endif()

        target_sources(${static_lib_name} PRIVATE ${srcs_fils_list})
    endforeach()
endfunction() #function(spf_build_static_library)
