#[[
   @file CMakeLists.txt

   @brief
]]

cmake_minimum_required(VERSION 3.10)

set(nxp_device_sources
	${LIB_ROOT}/capi/src/capi_nxp_device_utils.c
	${LIB_ROOT}/capi/src/capi_nxp_device.c
)

set(nxp_device_includes
	${LIB_ROOT}/api
	${LIB_ROOT}/capi/inc
	${LIB_ROOT}/capi/src
)

spf_module_sources(
	KCONFIG		CONFIG_NXP_DEVICE_SINK
	NAME		nxp_device_sink
	MAJOR_VER	1
	MINOR_VER	0
	AMDB_ITYPE	"capi"
	AMDB_MTYPE	"end_point"
	AMDB_MID	"0x0700109C"
	AMDB_TAG	"capi_nxp_device_sink"
	AMDB_MOD_NAME	"MODULE_ID_NXP_DEVICE_SINK"
	SRCS		${nxp_device_sources}
	INCLUDES	${nxp_device_includes}
	H2XML_HEADERS	"${LIB_ROOT}api/nxp_device_api.h"
	CFLAGS		"-Wno-address-of-packed-member"
)
