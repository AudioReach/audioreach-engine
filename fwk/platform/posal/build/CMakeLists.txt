#[[
   @file CMakeLists.txt

   @brief

   @copyright
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear

]]
cmake_minimum_required(VERSION 3.10)

#Include directories
set (lib_incs_list
     ${LIB_ROOT}/inc
     ${LIB_ROOT}/inc/private
     ${LIB_ROOT}/src/generic
     ${LIB_ROOT}/inc/${TGT_SPECIFIC_FOLDER}
     ${LIB_ROOT}/src
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}
     ${LIB_ROOT}/inc/${TGT_SPECIFIC_FOLDER}/stringl
    )

#Add the source files
set (lib_srcs_list
     ${LIB_ROOT}/src/generic/posal_bufpool_island.c
     ${LIB_ROOT}/src/generic/posal_bufpool.c
     ${LIB_ROOT}/src/generic/posal_channel_island.c
     ${LIB_ROOT}/src/generic/posal_channel.c
     ${LIB_ROOT}/src/generic/posal_queue_island.c
     ${LIB_ROOT}/src/generic/posal_queue.c
     ${LIB_ROOT}/src/generic/posal_signal_island.c
     ${LIB_ROOT}/src/generic/posal_signal.c
     ${LIB_ROOT}/src/generic/posal_std.c
     ${LIB_ROOT}/src/generic/posal_thread_prio.c
     ${LIB_ROOT}/src/generic/posal_thread_profiling.c
     ${LIB_ROOT}/src/generic/posal_data_log_island.c
     ${LIB_ROOT}/src/generic/posal_err_fatal.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_cache_island.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_cache.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_memory_island.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_memory.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_memorymap.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_memorymap_island.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_memory_stats.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_mem_prof.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_mems_util.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_mutex.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_nmutex.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_power_mgr.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_thread_attr_cfg.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_${TGT_SPECIFIC_FOLDER}_signal.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_${TGT_SPECIFIC_FOLDER}_stubs.c
     ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_${TGT_SPECIFIC_FOLDER}_thread.c
    )

if(ARSPF_WIN_PORTING) #platform windows
   list (APPEND lib_srcs_list
      ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_timer_mmtimer.c
      #${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_timer_timerqueue.c
      #${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_timer_waitable_timers.c
   )
elseif(CONFIG_ARCH_ZEPHYR)
   list (APPEND lib_srcs_list
      ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_condvar.c
      ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_timer.c
      #posal_rtld is not supported in zephyr.
   )
else() # for QNX & LRH
   list (APPEND lib_srcs_list
      ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_condvar.c
      ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_timer.c
      ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_rtld.c
   )
endif()

if(USE_SIM)
   list (APPEND lib_srcs_list
      ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_data_log.c
      #${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_data_log_sim.c
   )
else()
   list (APPEND lib_srcs_list
      ${LIB_ROOT}/src/${TGT_SPECIFIC_FOLDER}/posal_data_log.c
   )
endif()

if (CONFIG_DLS_DATA_LOGGING)
   set (lib_defs_list
      DLS_DATA_LOGGING
   )
endif()

#Set the libraries to link with the target
if(ARSPF_WIN_PORTING)
   set (lib_link_libs_list
      winmm.lib
   )
elseif(CONFIG_ARCH_ZEPHYR)
   set (lib_link_libs_list
      zephyr_interface
   )
else()
   set (lib_link_libs_list
   )
endif()

#Call spf_build_static_library to generate the static library
spf_build_static_library(posal
                         "${lib_incs_list}"
                         "${lib_srcs_list}"
                         "${lib_defs_list}"
                         "${lib_flgs_list}"
                         "${lib_link_libs_list}"
                        )
