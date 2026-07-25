/**
 * \file gen_cntr_err_handler.c
 * \brief
 *     This file contains functions that do error handeling.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "gen_cntr_utils.h"

static ar_result_t gen_cntr_err_handler_for_signal_miss(gen_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;

#ifdef ENABLE_SIGNAL_MISS_CRASH
   // Once we detect the signal miss we will restart the stm modules or force a crash.

   if (me_ptr->st_module.steady_state_interrupt_counter > 1000)
   {
      posal_err_fatal("Forcing Crash with Null pointer Access");
   }
#endif

   // Restarting STM module for recovery
   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      if (AR_EOK != (result = gen_cntr_fwk_extn_handle_at_stop(me_ptr, sg_list_ptr->sg_ptr->module_list_ptr)))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "During error handeling of signal miss : Failed to Disable one or more STM ");

         return result;
      }
   }

   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      if (AR_EOK != (result = gen_cntr_fwk_extn_handle_at_start(me_ptr, sg_list_ptr->sg_ptr->module_list_ptr)))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "During error handeling of signal miss : Failed to Enable one or more STM");
         return result;
      }
   }

   if (AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Error handling done with result %d", result);
   }
#ifdef VERBOSE_DEBUGGING
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Error handling done with result %d", result);
   }
#endif

   return result;
}

ar_result_t gen_cntr_check_handle_signal_miss(gen_cntr_t *me_ptr, bool_t is_after_process, bool_t *continue_processing)
{
   ar_result_t result = AR_EOK;
   me_ptr->st_module.signal_miss_counter++;

   if (me_ptr->topo.flags.need_to_ignore_signal_miss)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "Ignoring signal miss: is_after_process%u (0-due to cmd, 1-too long to process). Total signal miss "
                   "%lu, processed interrupts %lu, raised interrupts %lu",
                   is_after_process,
                   me_ptr->st_module.signal_miss_counter,
                   me_ptr->st_module.processed_interrupt_counter,
                   me_ptr->st_module.raised_interrupt_counter);

      me_ptr->st_module.processed_interrupt_counter = me_ptr->st_module.raised_interrupt_counter;
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Signal miss: is_after_process%u (0-due to cmd, 1-too long to process). Total signal miss "
                   "%lu, processed interrupts %lu, raised interrupts %lu",
                   is_after_process,
                   me_ptr->st_module.signal_miss_counter,
                   me_ptr->st_module.processed_interrupt_counter,
                   me_ptr->st_module.raised_interrupt_counter);

      // Call error handler
      if (AR_DID_FAIL(result = gen_cntr_err_handler_for_signal_miss(me_ptr)))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Error in handling signal miss before process");
      }
	  // reset the counters to make sure false interrupts during the error handling is ignored
      me_ptr->st_module.processed_interrupt_counter = 0;
      me_ptr->st_module.raised_interrupt_counter = 0;
      *continue_processing = FALSE;
   }

   return result;
}

bool_t gen_cntr_check_for_err_print(gen_topo_t *topo_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);

   if ((capi_cmn_divide(me_ptr->cu.period_us , 1000)) > GEN_CNTR_ERR_PRINT_INTERVAL_MS)
   {
      return TRUE;
   }

   uint32_t curr_time = (uint32_t)(posal_timer_get_time() / 1000);

   if (((curr_time - me_ptr->prev_err_print_time_ms) >= GEN_CNTR_ERR_PRINT_INTERVAL_MS) ||
       (0 == me_ptr->prev_err_print_time_ms))
   {
      me_ptr->topo.proc_context.err_print_time_in_this_process_ms = curr_time;
      return TRUE;
   }
   return FALSE;
}
