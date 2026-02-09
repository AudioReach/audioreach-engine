cmake_minimum_required(VERSION 3.5)

project(ar-osal)

set(osal_includes
    ./api
    )

include_directories(${osal_includes})

set(osal_SOURCE
    ./src/linux/ar_osal_heap.c
    ./src/linux/ar_osal_log.c
    ./src/linux/ar_osal_mutex.c
    ./src/linux/ar_osal_mem_op.c
    ./src/linux/ar_osal_string.c
    ./src/linux/ar_osal_thread.c
    ./src/linux/ar_osal_timer.c
    ./src/linux/qcom/ar_osal_servreg.c
    # ToDO: Pull latest osal from graphservices and use generic servreg implementation by default.
)

add_definitions(-DAR_OSAL_USE_SYSLOG)

add_library(ar-osal SHARED ${osal_SOURCE})

# Zephyr Specific
if ( ARCH MATCHES "^(zephyr)")
    target_link_libraries(ar-osal PUBLIC zephyr_interface)
endif()