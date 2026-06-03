# SPDX-License-Identifier: BSD-3-Clause-Clear
# VARIABLES:
#
# V_ARCH: Hexagon Architecture version number
# 	Supported Values: v5, v55, v60, v61, v62, v65, v66
#
# HEXAGON_TOOLS_ROOT: Path to Hexagon Toolchain
#
# HEXAGON_SDK_ROOT: Full path to hexagon SDK

if (NOT HEXAGON_SDK_ROOT AND NOT CONFIG_HEXAGON_SDK_ROOT)
	#message(FATAL_ERROR
#		"Please specify path to HEXAGON_SDK_ROOT either in config or in cmdline.\n")
endif()

if (NOT HEXAGON_SDK_ROOT AND CONFIG_HEXAGON_SDK_ROOT)
	set (HEXAGON_SDK_ROOT ${CONFIG_HEXAGON_SDK_ROOT})
endif()

if (NOT HEXAGON_TOOLS_ROOT AND NOT CONFIG_HEXAGON_TOOLS_ROOT)
	message(FATAL_ERROR
		"Please specify path to HEXAGON_TOOLS_ROOT.\n")
endif()

if (NOT HEXAGON_TOOLS_ROOT AND CONFIG_HEXAGON_TOOLS_ROOT)
	set(HEXAGON_TOOLS_ROOT ${CONFIG_HEXAGON_TOOLS_ROOT})
endif()

if (NOT V_ARCH MATCHES "^(v5|v55|v60|v61|v62|v65|v66)")
	message(FATAL_ERROR
		" Please specify valid Hexagon processor version.\n")
endif()

message(STATUS "Preparing Hexagon ${V_ARCH} build with\n\tToolchain: ${CONFIG_HEXAGON_TOOLS_ROOT}\n\tSDK: ${CONFIG_HEXAGON_SDK_ROOT}")

set(HEXAGON_SDK_INCLUDES ${HEXAGON_SDK_ROOT}/incs)
set(HEXAGON_SDK_STDDEF_INCLUDES ${HEXAGON_SDK_ROOT}/incs/stddef)
set(HEXAGON_SDK_REMOTE_INCLUDES ${HEXAGON_SDK_ROOT}/ipc/fastrpc/remote/ship)
set(HEXAGON_SDK_RPCMEM_INCLUDES ${HEXAGON_SDK_ROOT}/ipc/fastrpc/rpcmem/inc)
string(REGEX REPLACE "^v" "" V_ARCH_NUM "${V_ARCH}")
set(HEXAGON_SDK_QURT_INCLUDES ${HEXAGON_SDK_ROOT}/rtos/qurt/computev${V_ARCH_NUM}/include/qurt)
set(HEXAGON_SDK_AUDIO_INCLUDES ${HEXAGON_SDK_ROOT}/incs/audio)
set(HEXAGON_LIB_DIR ${HEXAGON_TOOLS_ROOT}/Tools/target/hexagon/lib)
set(HEXAGON_ISS_DIR ${HEXAGON_TOOLS_ROOT}/Tools/lib/iss)
set(HEXAGON_LIB_PATH ${HEXAGON_TOOLS_ROOT}/Tools/target/hexagon/lib/${V_ARCH}/G0)
set(HEXAGON_TOOLSLIB ${HEXAGON_TOOLS_ROOT}/Tools/target/hexagon/lib/${V_ARCH}/G0/pic)

#Debug/Release
if(CONFIG_SPF_DEBUG)
	set(_OPT "-O2")
else()
	set(_OPT "-O0")
	set(_DBG "-g")
	set(_DBG_ASM "--gdwarf2")
endif()

set(CMAKE_C_OUTPUT_EXTENSION ".o")
set(CMAKE_ASM_OUTPUT_EXTENSION ".o")

set(CMAKE_ASM_COMPILER_FORCED 1)
set(CMAKE_CXX_COMPILER_FORCED 1)
set(CMAKE_C_COMPILER_FORCED 1)

set(CMAKE_ASM_COMPILER_ID "Clang")
set(CMAKE_CXX_COMPILER_ID "Clang")
set(CMAKE_C_COMPILER_ID "Clang")

set(CROSS_COMPILE "${HEXAGON_TOOLS_ROOT}/Tools/bin/${ARCH}-")
set(CMAKE_C_COMPILER ${CROSS_COMPILE}clang)
set(CMAKE_CXX_COMPILER ${CROSS_COMPILE}clang++)
set(CMAKE_AR ${CROSS_COMPILE}ar)
set(CMAKE_RANLIBR ${CROSS_COMPILE}ranlib)
set(CMAKE_NM ${CROSS_COMPILE}nm)
set(CMAKE_OBJDUMP ${CROSS_COMPILE}objdump)
set(CMAKE_OBJCOPY ${CROSS_COMPILE}objcopy)
set(CMAKE_OBJCOPY ${CROSS_COMPILE}objcopy)
set(CMAKE_LINK ${CROSS_COMPILE}link)
set(CMAKE_STRIP ${CROSS_COMPILE}strip)

set(CMAKE_FIND_ROOT_PATH  ".")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)


# Language-specific flags
#
set(ARCHCFLAGS
	-D__CUSTOM_FILE_IO__
	)

set(ARCHCXXFLAGS
	-DCONFIG_WCHAR_BUILTIN
	-D__CUSTOM_FILE_IO__
	)


set(ARCHCPUFLAGS
	-m${V_ARCH}
	-G0
	)

set(CMAKE_C_FLAGS  "-m${V_ARCH}  -c \
   -v \
   -G0 \
   ${_DBG} \
   ${_OPT} \
   ${_CODE} \
   -fPIC \
   -Wall \
   -Werror \
   -Wno-unused-variable \
   -Wno-cast-align \
   -Wpointer-arith \
   -Wno-missing-braces \
   -Wno-strict-aliasing \
   -Wno-unused-but-set-variable \
   -fno-exceptions \
   -fno-strict-aliasing \
   -fno-zero-initialized-in-bss \
   -fdata-sections \
   -Wnested-externs \
   -DCUST_H=\"\"cust405.adsp.prodq.h\"\" \
   -I${HEXAGON_SDK_INCLUDES} \
   -I${HEXAGON_SDK_STDDEF_INCLUDES} \
   -I ${HEXAGON_SDK_REMOTE_INCLUDES} \
   -I ${HEXAGON_SDK_QURT_INCLUDES} \
   -I ${HEXAGON_SDK_RPCMEM_INCLUDES}")

set(HEXAGON_ARCH_FLAGS "-march=${ARCH} \
   -mcpu=${ARCH}${V_ARCH} \
   -m${V_ARCH}")

set(HEXAGON_START_LINK_FLAGS "${HEXAGON_ARCH_FLAGS} \
   -shared \
   -call_shared \
   -G0 \
   ${HEXAGON_TOOLSLIB}/initS.o \
   ${HEXAGON_TOOLSLIB}/libgcc.a \
   -L${HEXAGON_TOOLSLIB} \
   -Bsymbolic \
   --wrap=malloc \
   --wrap=calloc \
   --wrap=free \
   --wrap=realloc \
   --wrap=memalign \
   --wrap=__stack_chk_fail \
   -lc \
   -soname=<TARGET_SONAME> \
   -o <TARGET>")
set(CMAKE_CXX_FLAGS ${CMAKE_C_FLAGS})
set(HEXAGON_END_LINK_FLAGS "--start-group \
   -lgcc \
   --end-group \
   ${HEXAGON_TOOLSLIB}/finiS.o" )

set(CMAKE_C_CREATE_SHARED_LIBRARY
        "${CMAKE_LINK} ${HEXAGON_START_LINK_FLAGS} --start-group --whole-archive <OBJECTS> <LINK_LIBRARIES> --end-group ${HEXAGON_END_LINK_FLAGS}")
set(CMAKE_CXX_CREATE_SHARED_LIBRARY
        "${CMAKE_LINK} ${HEXAGON_START_LINK_FLAGS} --start-group --whole-archive <OBJECTS> <LINK_LIBRARIES> --end-group ${HEXAGON_END_LINK_FLAGS}")
