cmake_minimum_required(VERSION 3.5)

project(ar-gpr)

set(gpr_includes
    api/
    api/private/
    core/inc/
    core/inc/ar_utils/generic/
    core/src/
    ext/dynamic_allocation/inc/
    ext/logging/inc/
    )

if ( ARCH MATCHES "^(zephyr)")
    list(APPEND gpr_includes
        datalinks/gpr_zephyr/inc/
    )
endif()

include_directories(${gpr_includes})

set(gpr_SOURCE
    core/src/gpr_drv.c
    core/src/gpr_drv_island.c
    core/src/gpr_list.c
    core/src/gpr_list_island.c
    core/src/gpr_main.c
    core/src/gpr_memq.c
    core/src/gpr_memq_island.c
    core/src/hash_based/gpr_session.c
    core/src/hash_based/gpr_session_island.c
    ext/dynamic_allocation/src/gpr_dynamic_allocation.c
    ext/logging/src/gpr_log_generic.c
    ext/logging/stub_src/gpr_log_diag_stub.c
    )

if ( ARCH MATCHES "^(zephyr)")
    list(APPEND gpr_SOURCE
        datalinks/gpr_zephyr/src/gpr_zephyr_open_amp.c
        platform/zephyr/gpr_init_zephyr_wrapper.c
    )
endif()

add_definitions(-DSESSION_ARRAY_SIZE=200)
add_library(ar-gpr SHARED ${gpr_SOURCE})

if ( ARCH MATCHES "^(zephyr)")
    target_link_libraries(ar-gpr PUBLIC zephyr_interface)
    target_link_libraries(ar-gpr PUBLIC open_amp metal)
endif()