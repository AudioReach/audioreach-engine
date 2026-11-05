#[[
   @file CMakeLists.txt

   @brief

]]

cmake_minimum_required(VERSION 3.10)

set(alsa_device_sources
	${LIB_ROOT}/capi/src/capi_alsa_device.c
	${LIB_ROOT}/lib/src/tinyalsa/alsa_device_driver.c
)

set(alsa_device_includes
	${LIB_ROOT}/api
	${LIB_ROOT}/capi/inc
	${LIB_ROOT}/capi/src
	${LIB_ROOT}/lib/inc
	${LIB_ROOT}/lib/src/tinyalsa
	../../../../../../spf/utils/interleaver/inc
)

spf_module_sources(
	KCONFIG		CONFIG_ALSA_DEVICE_SINK
	NAME		alsa_device_sink
	MAJOR_VER	1
	MINOR_VER	0
	AMDB_ITYPE	"capi"
	AMDB_MTYPE	"end_point"
	AMDB_MID	"0x18000002"
	AMDB_TAG	"capi_alsa_device_sink"
	AMDB_MOD_NAME	"MODULE_ID_ALSA_DEVICE_SINK"
	SRCS		${alsa_device_sources}
	INCLUDES	${alsa_device_includes}
	H2XML_HEADERS	"${LIB_ROOT}api/alsa_device_api.h"
	CFLAGS		"-Wno-address-of-packed-member"
)

spf_module_sources(
	KCONFIG		CONFIG_ALSA_DEVICE_SOURCE
	NAME		alsa_device_source
	MAJOR_VER	1
	MINOR_VER	0
	AMDB_ITYPE	"capi"
	AMDB_MTYPE	"end_point"
	AMDB_MID	"0x18000003"
	AMDB_TAG	"capi_alsa_device_source"
	AMDB_MOD_NAME	"MODULE_ID_ALSA_DEVICE_SOURCE"
	SRCS		${alsa_device_sources}
	INCLUDES	${alsa_device_includes}
	H2XML_HEADERS	"${LIB_ROOT}api/alsa_device_api.h"
	CFLAGS		"-Wno-address-of-packed-member"
)
