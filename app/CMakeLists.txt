cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(audioreach_engine_sample)

target_sources(app PRIVATE src/main.c)

target_link_libraries(app PUBLIC spf)

set(INC_PATH ${ZEPHYR_AUDIOREACH_ENGINE_MODULE_DIR})

target_include_directories(app PRIVATE
    ${INC_PATH}/fwk/spf/utils/cmn/inc
    ${INC_PATH}/fwk/platform/posal/inc
    ${INC_PATH}/fwk/platform/posal/inc/linux/
    ${INC_PATH}/fwk/platform/posal/inc/linux/stringl
    ${INC_PATH}/fwk/api/ar_utils
    ${INC_PATH}/fwk/api/ar_utils/generic/
    ${INC_PATH}/ar_osal/api
    ${INC_PATH}/gpr/api
    )

zephyr_include_directories()