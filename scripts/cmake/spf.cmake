#[[
   @file
   spf.cmake

   @brief
   This file defines functions to collect source files and include paths.

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear
]]

function(spf_parse_kconfig)
	file(
		STRINGS
		${SPF_DOT_CONFIG_PATH}
		configs_list
		REGEX "^${SPF_KCONFIG_PREFIX}"
		ENCODING "${SPF_KCONFIG_ENCODING}"
	)

	foreach(kcfg ${configs_list})
		# Match "CONFIG_.*="
		string(REGEX MATCH "^(CONFIG_.*)=" kcfg_name ${kcfg})
		# Remove extra =
		set(kcfg_name ${CMAKE_MATCH_1})
		# Match "=.*"
		string(REGEX MATCH "=(.+$)" kcfg_val ${kcfg})
		# Remove extra =
		set(kcfg_val ${CMAKE_MATCH_1})
		
		# If config value is surrounded by QUotes 
		if("${kcfg_val}" MATCHES "^\"(.*)\"$")
			set(kcfg_val ${CMAKE_MATCH_1})
		endif()

		set("${kcfg_name}" "${kcfg_val}" PARENT_SCOPE)
	endforeach()
endfunction()

function(check_supported_proc_domain CONFIG_PROC_DOMAIN)
    set(SUPPORTED_PROC_DOMAINS "ADSP" "MDSP" "APPS" "SDSP" "CDSP" "CC_DSP" "GDSP_0" "GDSP_1" "APPS_2")
    list(FIND SUPPORTED_PROC_DOMAINS "${CONFIG_PROC_DOMAIN}" DOMAIN_INDEX)
    if(DOMAIN_INDEX EQUAL -1)
        message(FATAL_ERROR
            "Unsupported CONFIG_PROC_DOMAIN value: '${CONFIG_PROC_DOMAIN}'.\n"
            "Supported values are: ${SUPPORTED_PROC_DOMAINS}")
    else()
        message(STATUS "CONFIG_PROC_DOMAIN value: '${CONFIG_PROC_DOMAIN}' is supported.")
    endif()
endfunction()

function(get_absolute_path given_path abs_path)
		if(IS_ABSOLUTE ${given_path})
			set(${abs_path} ${given_path} PARENT_SCOPE)
		else()
			set(${abs_path} ${CMAKE_CURRENT_SOURCE_DIR}/${given_path} PARENT_SCOPE)
		endif()
endfunction()

function(spf_sources)
	foreach(src_path ${ARGN})
		set(abs_path "")
		get_absolute_path(${src_path} abs_path)
		target_sources(spf PRIVATE ${abs_path})
	endforeach()
endfunction()

function(spf_module_sources)
	cmake_parse_arguments(
	SPF_MODULE
	"OBFUSCATE"
	"KCONFIG;NAME;MAJOR_VER;MINOR_VER;AMDB_ITYPE;AMDB_MTYPE;AMDB_MID;AMDB_TAG;AMDB_MOD_NAME;AMDB_FMT_ID1"
	"SRCS;INCLUDES;H2XML_HEADERS;QACT_MODULE_TYPE;CFLAGS;STATIC_LIB_PATH"
	# Parser Input
	${ARGN}
	)

	if (NOT "${SPF_MODULE_STATIC_LIB_PATH}" STREQUAL "")
		message(STATUS "Prebuild binary configuration for module: ${SPF_MODULE_NAME}")
		set(SPF_MODULE_NAME "${SPF_MODULE_NAME}")
		spf_include_directories(${SPF_MODULE_INCLUDES})
		add_library(${SPF_MODULE_NAME} STATIC IMPORTED GLOBAL)
		if(IS_ABSOLUTE ${SPF_MODULE_STATIC_LIB_PATH})
			set(lib_abs_path ${SPF_MODULE_STATIC_LIB_PATH})
		else()
			set(lib_abs_path ${CMAKE_CURRENT_SOURCE_DIR}/${SPF_MODULE_STATIC_LIB_PATH})
		endif()
		set_target_properties(${SPF_MODULE_NAME} PROPERTIES IMPORTED_LOCATION ${lib_abs_path})
		set_property(GLOBAL APPEND PROPERTY GLOBAL_SPF_LIBS_LIST ${SPF_MODULE_NAME})

		set(post_build_commands "")
		set(json_file "${PROJECT_BINARY_DIR}/libs_cfg/${SPF_MODULE_NAME}.json")
		file(WRITE ${json_file}
		"[\n"
		"   {\n"
		"      \"lib_name\"      : \"${SPF_MODULE_NAME}\",\n"
		"      \"build\"         : \"STATIC_BUILD_NO_STRIP\",\n"
		"      \"lib_major_ver\" : ${SPF_MODULE_MAJOR_VER},\n"
		"      \"lib_minor_ver\" : ${SPF_MODULE_MINOR_VER},\n"
		"      \"amdb_info\"     :\n"
		"      {\n"
		"         \"itype\"              : \"${SPF_MODULE_AMDB_ITYPE}\",\n"
		"         \"mtype\"              : \"${SPF_MODULE_AMDB_MTYPE}\",\n"
		"         \"mid\"                : \"${SPF_MODULE_AMDB_MID}\",\n"
		"         \"tag\"                : \"${SPF_MODULE_AMDB_TAG}\",\n"
		"         \"module_name\"        : \"${SPF_MODULE_AMDB_MOD_NAME}\",\n"
		"         \"qact_module_type\"   : \"${SPF_MODULE_QACT_MODULE_TYPE}\",\n"
		"         \"fmt_id1\"            : \"${SPF_MODULE_AMDB_FMT_ID1}\"\n"
		"      }\n"
		"   }\n"
		"]\n"
		)

		foreach(inc_path ${SPF_MODULE_H2XML_HEADERS})
			set(abs_path "")
			get_absolute_path(${inc_path} abs_path)

			list(APPEND
				post_build_commands
				COMMAND
				mkdir -p ${PROJECT_BINARY_DIR}/h2xml_autogen
				COMMAND
				${H2XML} -conf ${H2XML_CONFIG} ${H2XML_FLAGS} -o ${PROJECT_BINARY_DIR}/h2xml_autogen ${H2XML_INCLUDES} -t spfModule -a "@h2xml_processors{${PROC_DOMAIN_NAME}}" ${abs_path}
			)
		endforeach()

		add_custom_target(${SPF_MODULE_NAME}_h2xml)

		add_custom_command(
			TARGET ${SPF_MODULE_NAME}_h2xml
			POST_BUILD
			${post_build_commands}
		        COMMENT "Running Post Build Scripts for ${SPF_MODULE_NAME}"
			VERBATIM
		)
		add_dependencies(spf ${SPF_MODULE_NAME}_h2xml)

	elseif (${SPF_MODULE_KCONFIG} MATCHES "y")
		spf_sources(${SPF_MODULE_SRCS})
		spf_include_directories(${SPF_MODULE_INCLUDES})
		set(SPF_MODULE_NAME "${SPF_MODULE_NAME}")
		set(post_build_commands "")
		set(json_file "${PROJECT_BINARY_DIR}/libs_cfg/${SPF_MODULE_NAME}.json")
		file(WRITE ${json_file}
		"[\n"
		"   {\n"
		"      \"lib_name\"      : \"${SPF_MODULE_NAME}\",\n"
		"      \"build\"         : \"STATIC_BUILD_NO_STRIP\",\n"
		"      \"lib_major_ver\" : ${SPF_MODULE_MAJOR_VER},\n"
		"      \"lib_minor_ver\" : ${SPF_MODULE_MINOR_VER},\n"
		"      \"amdb_info\"     :\n"
		"      {\n"
		"         \"itype\"              : \"${SPF_MODULE_AMDB_ITYPE}\",\n"
		"         \"mtype\"              : \"${SPF_MODULE_AMDB_MTYPE}\",\n"
		"         \"mid\"                : \"${SPF_MODULE_AMDB_MID}\",\n"
		"         \"tag\"                : \"${SPF_MODULE_AMDB_TAG}\",\n"
		"         \"module_name\"        : \"${SPF_MODULE_AMDB_MOD_NAME}\",\n"
		"         \"qact_module_type\"   : \"${SPF_MODULE_QACT_MODULE_TYPE}\",\n"
		"         \"fmt_id1\"            : \"${SPF_MODULE_AMDB_FMT_ID1}\"\n"
		"      }\n"
		"   }\n"
		"]\n"
		)

		foreach(inc_path ${SPF_MODULE_H2XML_HEADERS})
			set(abs_path "")
			get_absolute_path(${inc_path} abs_path)

			list(APPEND
				post_build_commands
				COMMAND
				mkdir -p ${PROJECT_BINARY_DIR}/h2xml_autogen
				COMMAND
				${H2XML} -conf ${H2XML_CONFIG} ${H2XML_FLAGS} -o ${PROJECT_BINARY_DIR}/h2xml_autogen ${H2XML_INCLUDES} -t spfModule -a "@h2xml_processors{${PROC_DOMAIN_NAME}}" ${abs_path}
			)
		endforeach()

		add_custom_target(${SPF_MODULE_NAME}_h2xml)

		add_custom_command(
			TARGET ${SPF_MODULE_NAME}_h2xml
			POST_BUILD
			${post_build_commands}
		        COMMENT "Running Post Build Scripts for ${SPF_MODULE_NAME}"
			VERBATIM
		)
		add_dependencies(spf ${SPF_MODULE_NAME}_h2xml)

	elseif(${SPF_MODULE_KCONFIG} MATCHES "m")
		set(SPF_MODULE_NAME "${SPF_MODULE_NAME}")
		set(soname "${SPF_MODULE_NAME}.so")
		set(post_build_commands "")
		add_library(${SPF_MODULE_NAME} SHARED "" )
		set(json_file "${PROJECT_BINARY_DIR}/libs_cfg/${SPF_MODULE_NAME}.json")
		target_compile_options(${SPF_MODULE_NAME} PRIVATE ${SPF_MODULE_CFLAGS})
		file(WRITE ${json_file} 
		"[\n"
		"   {\n"
		"      \"lib_name\"      : \"${SPF_MODULE_NAME}\",\n"
		"      \"build\"         : \"SHARED_BUILD_STRIP\",\n"
		"      \"lib_major_ver\" : ${SPF_MODULE_MAJOR_VER},\n"
		"      \"lib_minor_ver\" : ${SPF_MODULE_MINOR_VER},\n"
		"      \"amdb_info\"     :\n"
		"      {\n"
		"         \"itype\"              : \"${SPF_MODULE_AMDB_ITYPE}\",\n"
		"         \"mtype\"              : \"${SPF_MODULE_AMDB_MTYPE}\",\n"
		"         \"mid\"                : \"${SPF_MODULE_AMDB_MID}\",\n"
		"         \"tag\"                : \"${SPF_MODULE_AMDB_TAG}\",\n"
		"         \"module_name\"        : \"${SPF_MODULE_AMDB_MOD_NAME}\",\n"
		"         \"qact_module_type\"   : \"${SPF_MODULE_QACT_MODULE_TYPE}\",\n"
		"         \"fmt_id1\"            : \"${SPF_MODULE_AMDB_FMT_ID1}\"\n"
		"      }\n"
		"   }\n"
		"]\n"
		)

		foreach(src_path ${SPF_MODULE_SRCS})
			set(abs_path "")
			get_absolute_path(${src_path} abs_path)
			target_sources(${SPF_MODULE_NAME} PRIVATE ${abs_path})
		endforeach()

		foreach(inc_path ${SPF_MODULE_INCLUDES})
			set(abs_path "")
			get_absolute_path(${inc_path} abs_path)
			target_include_directories(${SPF_MODULE_NAME} PRIVATE ${abs_path})
		endforeach()

		foreach(inc_path ${SPF_MODULE_H2XML_HEADERS})
			set(abs_path "")
			get_absolute_path(${inc_path} abs_path)

			list(APPEND
				post_build_commands
				COMMAND
				mkdir -p ${PROJECT_BINARY_DIR}/h2xml_autogen
				COMMAND
				${H2XML} -conf ${H2XML_CONFIG} ${H2XML_FLAGS} -o ${PROJECT_BINARY_DIR}/h2xml_autogen ${H2XML_INCLUDES} -t spfModule -a "@h2xml_processors{${PROC_DOMAIN_NAME}}" ${abs_path}
			)
		endforeach()
		set_target_properties(${SPF_MODULE_NAME} PROPERTIES PREFIX "" SOVERSION ${SPF_MODULE_MAJOR_VER})
		if (NOT ${CONFIG_MODULES_DEBUG} MATCHES "y")
			list(APPEND
				post_build_commands
				COMMAND
				${CMAKE_STRIP} --strip-debug ${CMAKE_CURRENT_BINARY_DIR}/${soname}
			)
		endif()
#TODO
#SYMBOl OBFUSCATION
		install(TARGETS ${SPF_MODULE_NAME}
			LIBRARY DESTINATION lib
		)

		add_custom_command(
			TARGET ${SPF_MODULE_NAME}
			POST_BUILD
			${post_build_commands}
			COMMENT "Running Post Build Scripts for ${soname}"
			VERBATIM
		)
	endif()
endfunction()

function(spf_add_static_library lib_name lib_path)
	set(abs_path "")
	get_absolute_path(${lib_path} abs_path)

	# Use GLOBAL for lib visibility in root CMakeLists.
	add_library(${lib_name} STATIC IMPORTED GLOBAL)

	set_target_properties(${lib_name} PROPERTIES IMPORTED_LOCATION ${abs_path})
	target_link_libraries(spf_static_libraries INTERFACE ${lib_name})
endfunction()

function(spf_include_directories)
	foreach(inc_path ${ARGN})
		set(abs_path "")
		get_absolute_path(${inc_path} abs_path)
		target_include_directories(spf PRIVATE ${abs_path})
	endforeach()
endfunction()

function(spf_h2xml_sources)
	foreach(src_path ${ARGN})
		set(abs_path "")
		get_absolute_path(${src_path} abs_path)
		set(SPF_H2XML_SOURCES " ${SPF_H2XML_SOURCES}  ${abs_path}")
	endforeach()
endfunction()

function(spf_h2xml_include_directories)
	foreach(inc_path ${ARGN})
		set(abs_path "")
		get_absolute_path(${inc_path} abs_path)
		set(H2XML_INCLUDES ${H2XML_INCLUDES} -I ${abs_path})
	endforeach()
	set(H2XML_INCLUDES ${H2XML_INCLUDES} PARENT_SCOPE)
endfunction()
