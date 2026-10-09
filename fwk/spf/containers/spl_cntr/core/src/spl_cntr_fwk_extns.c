/**
 * \file spl_cntr_fwk_extns.c
 * \brief
 *     This file contains common functions for spl_cntr framework extension handling.
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "spl_cntr_i.h"

ar_result_t spl_cntr_handle_fwk_extn_at_init(gen_topo_t *topo_ptr, gen_topo_module_t *module_context_ptr)
{
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   ar_result_t        result      = AR_EOK;
   uint32_t           topo_offset = offsetof(spl_cntr_t, topo);
   spl_cntr_t        *me_ptr      = (spl_cntr_t *)(((uint8_t *)topo_ptr) - topo_offset);
   spl_topo_module_t *module_ptr  = (spl_topo_module_t *)module_context_ptr;

   /* Framework extn init handling*/
   if (module_ptr->flags.need_voice_delivery_extn)
   {
      SPF_CRITICAL_SECTION_START(&topo_ptr->gu);

      me_ptr->is_voice_delivery_cntr = TRUE;

      TRY(result, spl_cntr_fwk_extn_voice_delivery_send_proc_start_signal_info(me_ptr, module_ptr));

      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_HIGH_PRIO, "FWK_EXTN_VOICE_DELIVERY init handling done");
      SPF_CRITICAL_SECTION_END(&topo_ptr->gu);
   }
   if (module_ptr->t_base.flags.need_cntr_frame_dur_extn)
   {
      if (me_ptr->cu.cntr_frame_len.frame_len_us)
      {
         SPF_CRITICAL_SECTION_START(&topo_ptr->gu);
         // sync was sending me_ptr->threshold_data.configured_frame_size_us
         TRY(result,
             gen_topo_fwk_ext_set_cntr_frame_dur_per_module(&me_ptr->topo.t_base,
                                                            &module_ptr->t_base,
                                                            me_ptr->cu.cntr_frame_len.frame_len_us));
         SPF_CRITICAL_SECTION_END(&topo_ptr->gu);
      }
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_HIGH_PRIO, "FWK_EXTN_CNTR_FRAME_DURATION init handling done");

   }
   if (module_ptr->t_base.flags.need_proc_dur_extn)
   {
      if (me_ptr->cu.cntr_proc_duration)
      {
         SPF_CRITICAL_SECTION_START(&topo_ptr->gu);
         TRY(result,
             spl_cntr_fwk_extn_set_cntr_proc_duration_per_module(me_ptr, module_ptr, me_ptr->cu.cntr_proc_duration));
         SPF_CRITICAL_SECTION_END(&topo_ptr->gu);
      }
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_HIGH_PRIO, "FWK_EXTN_CNTR_PROC_DURATION init handling done");
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   SPF_CRITICAL_SECTION_END(&topo_ptr->gu);

   return result;
}

ar_result_t spl_cntr_handle_fwk_extn_at_deinit(gen_topo_t *topo_ptr, gen_topo_module_t *module_context_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t        result      = AR_EOK;
   uint32_t           topo_offset = offsetof(spl_cntr_t, topo);
   spl_cntr_t *       me_ptr      = (spl_cntr_t *)(((uint8_t *)topo_ptr) - topo_offset);
   spl_topo_module_t *module_ptr  = (spl_topo_module_t *)module_context_ptr;

   /* Framework extn init handling*/
   if (module_ptr->flags.need_voice_delivery_extn)
   {
      TRY(result, spl_cntr_fwk_extn_voice_delivery_close(me_ptr));
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_HIGH_PRIO, "FWK_EXTN_VOICE_DELIVERY de-init handling done");
   }
   if (module_ptr->t_base.flags.need_soft_timer_extn)
   {
      cu_fwk_extn_soft_timer_destroy_at_close(&me_ptr->cu, &module_ptr->t_base.gu);
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_HIGH_PRIO, "FWK_EXTN_SOFT_TIMER de-init handling done");
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/* RR: Temp utility function to drop EOS when received at SPL_CNTR for ECNS cases.*/
bool_t is_module_with_ecns_extn_found(spl_cntr_t *me_ptr)
{
   bool_t is_ecns_found = FALSE;

   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.t_base.gu.sg_list_ptr; sg_list_ptr; LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         spl_topo_module_t *curr_module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;
         if (curr_module_ptr->flags.need_ecns_extn)
         {
            is_ecns_found = TRUE;
            break;
         }
      }
   }
   return is_ecns_found;
}