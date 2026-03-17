/**
 * \file thin_topo.c
 * \brief
 *     This file contains functions for thin topo.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"
#include "thin_topo.h"
#include "thin_topo_inline.h"

void thin_topo_destroy(gen_topo_t *topo_ptr)
{
   thin_topo_reset_handle(topo_ptr, TRUE);

   // prepare list of started modules with default tgp satisfied.
   spf_list_delete_list((spf_list_node_t **)&topo_ptr->list_of_active_md_node_ptr, TRUE);
}

void thin_topo_reset_handle(gen_topo_t *topo_ptr, bool_t free_handle_memory)
{
   if (NULL == topo_ptr->thin_topo_ptr)
   {
      return;
   }

   // free list of started modules with default tgp satisfied.
   spf_list_delete_list((spf_list_node_t **)&topo_ptr->thin_topo_ptr->active_module_list_ptr, TRUE);
   // free list of started  ext in ports
   spf_list_delete_list((spf_list_node_t **)&topo_ptr->thin_topo_ptr->active_ext_in_list_ptr, TRUE);
   // free list of started  ext out ports
   spf_list_delete_list((spf_list_node_t **)&topo_ptr->thin_topo_ptr->active_ext_out_list_ptr, TRUE);

   if (free_handle_memory)
   {
      posal_memory_free(topo_ptr->thin_topo_ptr);
      topo_ptr->thin_topo_ptr = NULL;
   }

   TOPO_MSG(topo_ptr->gu.log_id, DBG_LOW_PRIO, "Resetting thin topo mem_freed?%lu", free_handle_memory);
}

void thin_topo_update_exit_flags(gen_topo_t *topo_ptr,
                                 bool_t      has_voice_sg,
                                 bool_t      need_soft_timer_extn,
                                 bool_t      has_duty_cycling_module,
                                 bool_t      has_signal_tgp_module,
                                 bool_t      requires_module_looping,
                                 bool_t      has_unsupported_module)
{
   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_unsupported_module, (has_unsupported_module ? TRUE : FALSE));

   THIN_TOPO_SET_EXIT_FLAG(topo_ptr,
                           is_signal_trigger_deactive,
                           (topo_ptr->flags.is_signal_triggered && topo_ptr->flags.is_signal_triggered_active ? FALSE
                                                                                                              : TRUE));
   THIN_TOPO_SET_EXIT_FLAG(topo_ptr,
                           has_more_than_one_sg,
                           (topo_ptr->gu.sg_list_ptr && topo_ptr->gu.sg_list_ptr->next_ptr ? TRUE : FALSE));

   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_voice_sg, has_voice_sg);

   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_parallel_paths, ((topo_ptr->gu.num_parallel_paths > 1) ? TRUE : FALSE));

   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_sync_module, topo_ptr->flags.is_sync_module_present);

   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_active_dm_module, (topo_ptr->flags.is_dm_enabled ? TRUE : FALSE));
   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_soft_timer_module, need_soft_timer_extn);

   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_duty_cycling_module, has_duty_cycling_module);
   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_active_data_tgp_extn, (topo_ptr->num_data_tpm ? TRUE : FALSE));
   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_active_signal_tgp_extn, has_signal_tgp_module);

   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, requires_module_looping, requires_module_looping);

   if (topo_ptr->list_of_active_md_node_ptr)
   {
      uint8_t count = 0;
      for (spf_list_node_t *temp_ptr = topo_ptr->list_of_active_md_node_ptr; NULL != temp_ptr;
           count++, LIST_ADVANCE(temp_ptr))
         ;
      THIN_TOPO_SET_EXIT_FLAG(topo_ptr, num_active_md_nodes_in_cntr, count);
   }

   bool_t has_to_preserve_prebuffer = FALSE;
   if (topo_ptr->topo_to_cntr_vtable_ptr->check_if_any_ext_in_has_to_preserve_prebuffer)
   {
      topo_ptr->topo_to_cntr_vtable_ptr->check_if_any_ext_in_has_to_preserve_prebuffer(topo_ptr,
                                                                                       &has_to_preserve_prebuffer);
   }
   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_to_preserve_prebuffer, has_to_preserve_prebuffer);
   TOPO_MSG(topo_ptr->gu.log_id, DBG_LOW_PRIO, "Updated Thin topo flags=0x%lX", topo_ptr->exit_flags.word);
}

ar_result_t thin_topo_init_handle(gen_topo_t *topo_ptr)
{
   if (NULL == topo_ptr->thin_topo_ptr)
   {
      topo_ptr->thin_topo_ptr = (thin_topo_t *)posal_memory_malloc(sizeof(thin_topo_t), topo_ptr->heap_id);
      if (NULL == topo_ptr->thin_topo_ptr)
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Failed allocation memory for the thin topo context info handle.");
         return AR_ENOMEMORY;
      }
      memset(topo_ptr->thin_topo_ptr, 0, sizeof(thin_topo_t));
   }
   return AR_EOK;
}

void thin_topo_reset_signal_tgp_flag(gen_topo_t *topo_ptr)
{
   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_active_signal_tgp_extn, FALSE);
   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
         if (gen_topo_is_module_signal_trigger_policy_active(module_ptr))
         {
            THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_active_signal_tgp_extn, TRUE);

#ifdef VERBOSE_DEBUGGING
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_LOW_PRIO,
                     "Module 0x%lx has active signal trigger policy, cannot process in thin topo.",
                     module_ptr->gu.module_instance_id);
#endif
         }
      }
   }
}