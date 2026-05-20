#ifndef _SPF_H_
#define _SPF_H_

/**
 * \file gk.h
 * \brief
 *    This is the common include file to pick up all the necessary
 *  SPF headers.
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */

#include "ar_error_codes.h"
#include "spf_bufmgr.h"
#include "apm_api.h"
#include "apm_sub_graph_api.h"
#include "apm_container_api.h"
#include "apm_module_api.h"
#include "spf_cmn_if.h"
#include "spf_msg_util.h"
#include "spf_list_utils.h"
#include "spf_lpi_pool_utils.h"
#include "offload_apm_api.h"
#include "posal_timer.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/* The size of a pointer in bytes. For a 32-bit system, this value is 4. */
#define SPF_PTR_SIZE_BYTES sizeof(void *)

/* External command execution time threshold.
 * This time threshold is for whole command */
#define SPF_EXTERNAL_CMD_EXEC_TIME_THRESHOLD_US          (800000) /** 800 ms */

/* Internal command execution time threshold. This should be less than
 * SPF_EXTERNAL_CMD_EXEC_TIME_THRESHOLD_US which is 800ms, so given a safe margin
 * of 300ms. This is to avoid external timeout crashes due to internal timeout handling.
 * */
#define SPF_INTERNAL_CMD_EXEC_TIME_THRESHOLD_US          (500000) /** 500 ms */

#define IS_VOICE_SCENARIO_ID(scenario_id) \
    ((APM_SUB_GRAPH_SID_VOICE_CALL == (scenario_id)) || \
     (APM_SUB_GRAPH_SID_SAT_VOICE_CALL == (scenario_id)))

/* =======================================================================
Static Inline Functions
========================================================================== */

static inline uint32_t spf_get_bits(uint32_t x, uint32_t mask, uint32_t shift)
{
   return (x & mask) >> shift;
}

static inline void spf_set_bits(uint32_t *x_ptr, uint32_t val, uint32_t mask, uint32_t shift)
{
   val    = (val << shift) & mask;
   *x_ptr = (*x_ptr & ~mask) | val;
}

/**
  Utility function to convert tick to timestamp as per Q-timer with 19.2MHz.

  @param[in] tick_count   tick_count Tick Count by DMA..

  @return
  Time stamp in Nano-Seconds.

  @dependencies
  None. @newpage
 */
static inline uint64_t spf_util_tick_to_time_ns(uint64_t tick) {
  return (uint64_t)((tick * 10000ull) / 192ul);
}

/**
  Utility function to convert tick to timestamp as per Q-timer with 19.2MHz.

  @param[in] tick_count   tick_count Tick Count by DMA..

  @return
  Time stamp in Nano-Seconds.

  @dependencies
  None. @newpage
 */
static inline uint64_t spf_util_get_time_ns(void)
{
  return spf_util_tick_to_time_ns(posal_timer_get_hw_ticks());
}

#define SPF_UTIL_GET_CODE_EXECUTION_TIME(log_id, CONTEXT, get_delta, enable_print, XX_CODE_SECTION_XX) \
   uint64_t get_delta;                                                       \
   do {                                                                      \
      uint64_t __start_tick = posal_timer_get_hw_ticks();                    \
      XX_CODE_SECTION_XX                                                      \
      uint64_t __end_tick   = posal_timer_get_hw_ticks();                    \
      uint64_t __start_ns   = spf_util_tick_to_time_ns(__start_tick);     \
      uint64_t __end_ns     = spf_util_tick_to_time_ns(__end_tick);       \
      uint64_t __delta_ns   = __end_ns - __start_ns;                         \
      get_delta = __delta_ns;                                                \
      if (enable_print) {                                                    \
         AR_MSG_ISLAND(DBG_LOW_PRIO,                                         \
            "TS_STATS[0x%lx]:" CONTEXT " start_time (%lu, %lu) end_time (%lu, %lu) delta (%lu)",\
             log_id, (uint32_t)(__start_ns >> 32), (uint32_t)(__start_ns),   \
            (uint32_t)(__end_ns   >> 32), (uint32_t)(__end_ns),              \
            (uint32_t)(__delta_ns));          \
      }                                                                      \
   } while (0)


#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef _SPF_H_
