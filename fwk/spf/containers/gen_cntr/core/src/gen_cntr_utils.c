/**
 * \file gen_cntr_utils.c
 * \brief
 *     This file contaouts utility functions for GEN_CNTR
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "capi_fwk_extns_island.h"
#include "pt_cntr.h"

#ifdef PROD_SPECIFIC_MAX_CH
static const uint32_t GEN_CNTR_PROCESS_STACK_SIZE     = 2048; // additional requirement based on profiling
#else
static const uint32_t GEN_CNTR_PROCESS_STACK_SIZE     = 1024;
#endif
static const uint32_t GEN_CNTR_PROCESS_STACK_SIZE_LPI = 1024;
static const uint32_t GEN_CNTR_BASE_STACK_SIZE        = 1024;
static const uint32_t GEN_CNTR_BASE_STACK_SIZE_LPI    = 1024;

static void gen_cntr_get_root_stack_size(gen_cntr_t *me_ptr, uint32_t *root_thread_stack_size)
{
   uint32_t               num_module             = 0;
   mid_stack_pair_info_t *root_thread_stack_prop = get_platform_prop_info(ROOT_THREAD_STACK_PROP, &num_module);

   *root_thread_stack_size = 0;
   if ((NULL != root_thread_stack_prop) && (0 != num_module))
   {
      uint8_t num_gu       = 1;
      gu_t *  gu_ptr_arr[] = { me_ptr->cu.gu_ptr, NULL };
      gu_t *  open_gu_ptr  = get_gu_ptr_for_current_command_context(gu_ptr_arr[0]);
      if (open_gu_ptr != gu_ptr_arr[0])
      {
         gu_ptr_arr[1] = open_gu_ptr;
         num_gu++;
      }

      for (uint32_t j = 0; j < num_gu; j++)
      {
         gu_t *gu_ptr = gu_ptr_arr[j];
         for (gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
         {
            gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;
            for (gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr; (NULL != module_list_ptr);
                 LIST_ADVANCE(module_list_ptr))
            {
               uint32_t module_id = module_list_ptr->module_ptr->module_id;
               for (uint32_t i = 0; i < num_module; i++)
               {
                  if (root_thread_stack_prop[i].module_id == module_id)
                  {
                     *root_thread_stack_size =
                        MAX(*root_thread_stack_size, root_thread_stack_prop[i].required_root_thread_stack_size_in_cntr);
                  }
               }
            }
         }
      }
   }
}

ar_result_t gen_cntr_get_thread_stack_size(gen_cntr_t *me_ptr, uint32_t *stack_size_ptr, uint32_t *root_stack_size_ptr)
{
   ar_result_t result     = AR_EOK;
   uint32_t    stack_size = 0;

   result = gen_topo_get_aggregated_capi_stack_size(&me_ptr->topo, &stack_size);
   if (AR_FAILED(result))
   {
      return result;
   }

   gen_cntr_get_root_stack_size(me_ptr, root_stack_size_ptr);

   stack_size = MAX(me_ptr->cu.configured_stack_size, stack_size);

   if (POSAL_IS_ISLAND_HEAP_ID(me_ptr->cu.heap_id))
   {
      stack_size = MAX(GEN_CNTR_BASE_STACK_SIZE_LPI, stack_size);
      stack_size += GEN_CNTR_PROCESS_STACK_SIZE_LPI;
   }
   else
   {
      stack_size = MAX(GEN_CNTR_BASE_STACK_SIZE, stack_size);
      stack_size += GEN_CNTR_PROCESS_STACK_SIZE;
   }

   // Check this after adding the GEN_CNTR_PROCESS_STACK_SIZE/GEN_CNTR_PROCESS_STACK_SIZE_LPI to the stack_size
   // to prevent multiple addition during relaunch
   stack_size = MAX(me_ptr->cu.actual_stack_size, stack_size);

   *stack_size_ptr = stack_size;

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX "Stack sizes: Configured %lu, actual %lu, final %lu",
                me_ptr->cu.configured_stack_size,
                me_ptr->cu.actual_stack_size,
                stack_size);
   return AR_EOK;
}

bool_t gen_cntr_check_if_time_critical(gen_cntr_t *me_ptr)
{
   // if cntr is started but cntr_proc_duration is still not set (happens in HW-EP b4 MF prop), still bump up prio (even
   // though not required).
   return (me_ptr->cu.flags.is_cntr_started && gen_cntr_is_signal_triggered(me_ptr) &&
           (me_ptr->cu.cntr_proc_duration <= GEN_CNTR_PROC_DUR_THRESH_FOR_PRIO_BUMP_UP));
}

// if is_bump_up == TRUE: returns TRUE if priority bump-up happened. else FALSE.
// if is_bump_up == FALSE, then voting is done with bump_up_factor of one.
// callers must call with is_bump_up only if call with is_bump_up returned TRUE.
bool_t gen_cntr_check_bump_up_thread_priority(cu_base_t *cu_ptr, bool_t is_bump_up, posal_thread_prio_t original_prio)
{
   SPF_MANAGE_CRITICAL_SECTION

   gen_cntr_t *me_ptr         = (gen_cntr_t *)cu_ptr;
   uint16_t    bump_up_factor = 1;

   SPF_CRITICAL_SECTION_START(cu_ptr->gu_ptr);
   if (is_bump_up)
   {
      if (gen_cntr_check_if_time_critical(me_ptr) && !me_ptr->flags.is_thread_prio_bumped_up)
      {
         bump_up_factor = GEN_CNTR_PROC_DUR_SCALE_FACTOR_FOR_CMD_PROC;
      }
      else
      {
         SPF_CRITICAL_SECTION_END(cu_ptr->gu_ptr);
         return FALSE;
      }
   }

   (void)gen_cntr_get_set_thread_priority(me_ptr, NULL, TRUE /*should_set*/, bump_up_factor, original_prio);
   SPF_CRITICAL_SECTION_END(cu_ptr->gu_ptr);
   return TRUE;
}

/**
 * bump_up_factor - used for end point containers to increase priority during cmd handling (after graph start).
 * e.g. if 1ms is regular interrupt time, during cmd we may make it 0.5 ms which bumps up priority.
 * Original prio is the priority before bump up - which could be prio voted by module or container
 */
ar_result_t gen_cntr_get_set_thread_priority(gen_cntr_t         *me_ptr,
                                             int32_t            *priority_ptr,
                                             bool_t              should_set,
                                             uint16_t            bump_up_factor,
                                             posal_thread_prio_t original_prio)
{
   ar_result_t         result  = AR_EOK;
   bool_t              has_stm = FALSE, is_real_time = FALSE;
   posal_thread_prio_t curr_prio = posal_thread_prio_get2(me_ptr->cu.cmd_handle.thread_id);
   posal_thread_prio_t new_prio  = curr_prio;
   posal_thread_prio_t new_prio1; //temp


   if (!me_ptr->cu.flags.is_cntr_started)
   {
      new_prio = posal_thread_get_floor_prio(SPF_THREAD_STAT_CNTR_ID);
   }
   else
   {
      if ((FRAME_SIZE_DONT_CARE == me_ptr->cu.cntr_proc_duration) || (0 == me_ptr->cu.cntr_proc_duration))
      {
         new_prio = curr_prio;
      }
      else
      {
         is_real_time = me_ptr->cu.flags.is_real_time;
         has_stm      = gen_cntr_is_signal_triggered(me_ptr);
         if (!is_real_time && !has_stm)
         {
            new_prio = posal_thread_get_floor_prio(SPF_THREAD_STAT_CNTR_ID);
         }
         else // rt
         {
            prio_query_t query_tbl;
            // vdec, vprx takes < 1ms to process & vdec is stm, but we don't want it to get higher prio compared to
            // HW-EP as margins are higher.
            uint32_t available_proc_dur_us = 0;
            if (me_ptr->cu.voice_info_ptr)
            {
               available_proc_dur_us = me_ptr->cu.voice_info_ptr->safety_margin_us;
            }
            else
            {
               available_proc_dur_us = capi_cmn_divide(me_ptr->cu.cntr_proc_duration, bump_up_factor);
            }

            query_tbl.frame_duration_us = available_proc_dur_us;
            query_tbl.static_req_id     = SPF_THREAD_DYN_ID;
            query_tbl.is_interrupt_trig = has_stm;
            result                      = posal_thread_calc_prio(&query_tbl, &new_prio);
            if (AR_DID_FAIL(result))
            {
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Failed to get GEN_CNTR thread priority");
               return result;
            }
         }
      }
   }

   // Fallback to highest prio between calculated prio and original prio(includes module voted prio) before bump up
   new_prio  = MAX(new_prio, original_prio);
   new_prio1 = new_prio;

   /**
    * If container prio is configured, then it is used independent of whether container is started, or
    * running commands during data processing or if it's FTRT or if its frame size is not known or
    * if a module changes container priority.
    */
   if (APM_CONT_PRIO_IGNORE != me_ptr->cu.configured_thread_prio)
   {
      new_prio = me_ptr->cu.configured_thread_prio;
   }

   if (curr_prio != new_prio)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   SPF_LOG_PREFIX
                   "GEN_CNTR thread priority %d (larger is higher prio), period_us %lu us, frame len %lu us, proc "
                   "duration %lu us, is real time %u, has_stm %u, cntr started %u, bump_up_factor %u",
                   new_prio,
                   me_ptr->cu.period_us,
                   me_ptr->cu.cntr_frame_len.frame_len_us,
                   me_ptr->cu.cntr_proc_duration,
                   is_real_time,
                   has_stm,
                   me_ptr->cu.flags.is_cntr_started,
                   bump_up_factor);

      if (new_prio1 != new_prio)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_HIGH_PRIO,
                            "Warning: thread priority: configured %d prio overrides internal logic %d", me_ptr->cu.configured_thread_prio, new_prio1);
      }

      if (should_set)
      {
         posal_thread_prio_t prio = (posal_thread_prio_t)new_prio;

         posal_thread_set_prio2(me_ptr->cu.cmd_handle.thread_id, prio);
      }
   }

   SET_IF_NOT_NULL(priority_ptr, new_prio);

   // always set when bump up factor is more than 1. else reset.
   me_ptr->flags.is_thread_prio_bumped_up = (bump_up_factor > 1);

   return AR_EOK;
}

ar_result_t gen_cntr_handle_proc_duration_change(cu_base_t *base_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   gen_cntr_reconcile_and_handle_fwk_events(me_ptr);
   return AR_EOK;
}

ar_result_t gen_cntr_handle_cntr_period_change(cu_base_t *base_ptr)
{
   gen_cntr_t *me_ptr   = (gen_cntr_t *)base_ptr;
   capi_err_t  err_code = CAPI_EOK;

   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         if (FALSE == module_ptr->flags.supports_period)
         {
            continue;
         }

         intf_extn_param_id_period_t param;
         param.period_us = me_ptr->cu.period_us;

         err_code = gen_topo_capi_set_param(me_ptr->topo.gu.log_id,
                                            module_ptr->capi_ptr,
                                            INTF_EXTN_PARAM_ID_PERIOD,
                                            (int8_t *)&param,
                                            sizeof(param));

         if ((err_code != CAPI_EOK) && (err_code != CAPI_EUNSUPPORTED))

         {

            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module 0x%lX: setting container period failed",
                         module_ptr->gu.module_instance_id);
            return capi_err_to_ar_result(err_code);
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_LOW_PRIO,
                         "Module 0x%lX: setting container period of %lu",
                         module_ptr->gu.module_instance_id,
                         param.period_us);
         }
      }
   }
   return AR_EOK;
}

ar_result_t gen_cntr_handle_cntr_set_calibration_ops_done(cu_base_t *base_ptr)
{
   gen_cntr_t *me_ptr   = (gen_cntr_t *)base_ptr;
   capi_err_t  err_code = CAPI_EOK;

   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         if (FALSE == module_ptr->flags.supports_calibration_ops_done)
         {
            continue;
         }

         err_code = gen_topo_capi_set_param(me_ptr->topo.gu.log_id,
                                            module_ptr->capi_ptr,
                                            INTF_EXTN_CALIBRATION_OPS_DONE,
                                            NULL, 0);

         if ((err_code != AR_EOK) && (err_code != AR_EUNSUPPORTED))
         {

            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module 0x%lX: setting calibration ops done failed",
                         module_ptr->gu.module_instance_id);
            return err_code;
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_LOW_PRIO,
                         "Module 0x%lX: setting calibration ops done success",
                         module_ptr->gu.module_instance_id
                         );
         }
      }
   }
   return AR_EOK;
}

/**
 * keeps Media format and other data messages
 */
ar_result_t gen_cntr_flush_input_data_queue(gen_cntr_t             *me_ptr,
                                            gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                            bool_t                  keep_data_msg)
{
   ar_result_t result             = AR_EOK;
   void       *pushed_payload_ptr = NULL;

   if (NULL == ext_in_port_ptr->gu.this_handle.q_ptr)
   {
      return AR_EOK;
   }

   // move prebuffers to the the main Q,so that data and metadata can be dropped in order.
   cu_ext_in_requeue_prebuffers(&me_ptr->cu, &ext_in_port_ptr->gu);

   // Since data will be dropped therefore set the preserve prebuffer flag so that prebuffers can be preserved during
   // next frame processing.
   ext_in_port_ptr->cu.preserve_prebuffer       = TRUE;
   me_ptr->topo.flags.need_to_handle_frame_done = TRUE;

   THIN_TOPO_SET_EXIT_FLAG((&me_ptr->topo), has_to_preserve_prebuffer, TRUE);

   do
   {
      // if data message, then push back to queue. Also stop popping when we see the first message we pushed
      if (keep_data_msg && !(ext_in_port_ptr->vtbl_ptr->is_data_buf(me_ptr, ext_in_port_ptr)))
      {
         if (NULL == pushed_payload_ptr)
         {
            pushed_payload_ptr = ext_in_port_ptr->cu.input_data_q_msg.payload_ptr;
         }

         if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_LOW_PRIO,
                         "Pushing data msg 0x%p back to queue during flush. opcode 0x%lX.",
                         ext_in_port_ptr->cu.input_data_q_msg.payload_ptr,
                         ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);

            // note that upstream won't be pushing at this time because upstream is stopped when we flush. hence there's
            // worry of messages going out-of-order.
            if (AR_DID_FAIL(result =
                               posal_queue_push_back(ext_in_port_ptr->gu.this_handle.q_ptr,
                                                     (posal_queue_element_t *)&(ext_in_port_ptr->cu.input_data_q_msg))))
            {
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Pushing MF back to queue failed");
               return result;
            }
            ext_in_port_ptr->cu.input_data_q_msg.payload_ptr = NULL;
         }
      }
      else
      {
         // first free up any data q msgs that we are already holding
         gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, TRUE);
      }

      // peek and see if front of queue has the message we pushed back, if so, don't pop
      if (pushed_payload_ptr)
      {
         spf_msg_t *front_ptr = NULL;
         posal_queue_peek_front(ext_in_port_ptr->gu.this_handle.q_ptr, (posal_queue_element_t **)&front_ptr);
         if (front_ptr->payload_ptr == pushed_payload_ptr)
         {
            break;
         }
      }

      // Drain any queued buffers while there are input data messages.
      gen_cntr_get_input_data_cmd(me_ptr, ext_in_port_ptr);

   } while (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);

   return AR_EOK;
}

ar_result_t gen_cntr_flush_output_data_queue(gen_cntr_t              *me_ptr,
                                             gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                             bool_t                   is_client_cmd)
{
   // Nothing to do if the external port is NULL
   if (!ext_out_port_ptr)
   {
      return AR_EOK;
   }

   if (ext_out_port_ptr->vtbl_ptr && ext_out_port_ptr->vtbl_ptr->flush)
   {
      ext_out_port_ptr->vtbl_ptr->flush(me_ptr, ext_out_port_ptr, is_client_cmd);
   }

   gen_cntr_clear_ext_out_bufs(ext_out_port_ptr, FALSE /* clear_max */);

   return AR_EOK;
}

void gen_cntr_reset_mf_pending_at_ext_in(gen_cntr_t *me_ptr)
{
   bool_t is_any_mf_pending = FALSE;

   SPF_MANAGE_CRITICAL_SECTION
   SPF_CRITICAL_SECTION_START(me_ptr->cu.gu_ptr);

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *temp_ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      if (temp_ext_in_port_ptr->flags.pending_mf)
      {
         is_any_mf_pending = TRUE;
         break;
      }
   }

   me_ptr->flags.is_any_ext_in_mf_pending = is_any_mf_pending;

   SPF_CRITICAL_SECTION_END(me_ptr->cu.gu_ptr);
}

ar_result_t gen_cntr_ext_in_port_int_reset(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;

   ext_in_port_ptr->flags.eof                 = FALSE;
   ext_in_port_ptr->flags.input_discontinuity = FALSE;
   ext_in_port_ptr->flags.pending_mf          = FALSE;

   // ideally centralized handler should reset global flag, for simplicity this place is chosen.
   gen_cntr_reset_mf_pending_at_ext_in(me_ptr);

#ifdef VERBOSE_DEBUGGING
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "External input reset Module 0x%lX, Port 0x%lx",
                in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                in_port_ptr->gu.cmn.id);
#endif

   return result;
}

ar_result_t gen_cntr_ext_in_port_reset(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;

   if (ext_in_port_ptr->flags.is_not_reset)
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

      gen_topo_reset_input_port(&me_ptr->topo, in_port_ptr);

      if (ext_in_port_ptr->md_list_ptr)
      {
         gen_topo_destroy_all_metadata(me_ptr->cu.gu_ptr->log_id,
                                       (void *)in_port_ptr->gu.cmn.module_ptr,
                                       &ext_in_port_ptr->md_list_ptr,
                                       TRUE /*is_dropped*/);
      }

      gen_cntr_ext_in_port_int_reset(me_ptr, ext_in_port_ptr);
   }
   return result;
}

ar_result_t gen_cntr_ext_out_port_reset(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   if (ext_out_port_ptr->flags.is_not_reset)
   {
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

      if (ext_out_port_ptr->md_list_ptr)
      {
         // ext out port has old metadata compared to the one in out_port_ptr, so free it first
         gen_topo_destroy_all_metadata(me_ptr->cu.gu_ptr->log_id,
                                       (void *)out_port_ptr->gu.cmn.module_ptr,
                                       &ext_out_port_ptr->md_list_ptr,
                                       TRUE /*is_dropped*/);
      }

      gen_topo_reset_output_port(&me_ptr->topo, out_port_ptr);

      gen_cntr_ext_out_port_int_reset(me_ptr, ext_out_port_ptr);
   }

   return result;
}

void gen_cntr_set_stm_ts_to_module(gen_cntr_t *me_ptr)

{
   capi_err_t err_code = CAPI_EOK;

   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         if (TRUE == module_ptr->flags.supports_stm_ts)
         {
            intf_extn_param_id_stm_ts_t stm_ts_payload = { 0 };
            stm_ts_payload.ts_ptr                      = me_ptr->st_module.st_module_ts_ptr;

            err_code = gen_topo_capi_set_param(me_ptr->topo.gu.log_id,
                                               module_ptr->capi_ptr,
                                               INTF_EXTN_PARAM_ID_STM_TS,
                                               (int8_t *)&stm_ts_payload,
                                               sizeof(stm_ts_payload));
         }

         if ((err_code != CAPI_EOK) && (err_code != CAPI_EUNSUPPORTED))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module 0x%lX: Failed to set timestamp pointer",
                         module_ptr->gu.module_instance_id);
         }
      }
   }
}

static ar_result_t gen_cntr_stm_fwk_extn_handle_disable(gen_cntr_t *me_ptr, gen_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   if (NULL == me_ptr->st_module.trigger_signal_ptr)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "STM module is already disabled.");
      return AR_EOK;
   }

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "Disabling CAPI signal triggered module 0x%lX",
                module_ptr->gu.module_instance_id);

   /* Disable the end point*/
   typedef struct
   {
      capi_custom_property_t cust_prop;
      capi_prop_stm_ctrl_t   ctrl;
   } stm_ctrl_t;

   stm_ctrl_t stm_ctrl;
   stm_ctrl.cust_prop.secondary_prop_id = FWK_EXTN_PROPERTY_ID_STM_CTRL;
   stm_ctrl.ctrl.enable                 = FALSE;

   capi_prop_t set_props[] = {
      { CAPI_CUSTOM_PROPERTY, { (int8_t *)(&stm_ctrl), sizeof(stm_ctrl), sizeof(stm_ctrl) }, { FALSE, FALSE, 0 } }
   };

   capi_proplist_t set_proplist = { SIZE_OF_ARRAY(set_props), set_props };

   if (CAPI_EOK != (result = module_ptr->capi_ptr->vtbl_ptr->set_properties(module_ptr->capi_ptr, &set_proplist)))
   {
      if (CAPI_EUNSUPPORTED == result)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Unsupported stm cfg for input port 0x%x", result);
      }
      else
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Failed to apply trigger and stm cfg for input port 0x%x",
                      result);
         result = AR_EFAILED;
      }
   }

   if (me_ptr->st_module.trigger_signal_ptr)
   {
      cu_deinit_signal(&me_ptr->cu, &me_ptr->st_module.trigger_signal_ptr);
      me_ptr->st_module.raised_interrupt_counter    = 0;
      me_ptr->st_module.processed_interrupt_counter = 0;
   }

   me_ptr->st_module.st_module_ts_ptr   = NULL;
   me_ptr->st_module.update_stm_ts_fptr = NULL;
   me_ptr->st_module.stm_ts_ctxt_ptr    = NULL;

   gen_cntr_set_stm_ts_to_module(me_ptr);

   return result;
}

static ar_result_t gen_cntr_stm_fwk_extn_handle_enable(gen_cntr_t *me_ptr, gen_topo_module_t *module_ptr)
{
   ar_result_t result   = AR_EOK;
   uint32_t    bit_mask = 0;
   INIT_EXCEPTION_HANDLING

   if (me_ptr->st_module.trigger_signal_ptr)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "STM module is already enabled.");
      return AR_EOK;
   }

   // Enable the STM control
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "Enabling CAPI signal triggered module 0x%lX",
                module_ptr->gu.module_instance_id);

   {
      /* Get the bit mask for stm signal, this signal trigger data processing
            on the container on every DMA interrupt or every time the timer expires */
      bit_mask = GEN_CNTR_TIMER_BIT_MASK;

      /* initialize the signal */
      TRY(result,
             cu_init_signal(&me_ptr->cu, bit_mask, gen_cntr_signal_trigger, &me_ptr->st_module.trigger_signal_ptr));

      /* Initialize interrupt counter */
      me_ptr->st_module.raised_interrupt_counter = 0;

      /* Property structure for stm trigger */
      typedef struct
      {
         capi_custom_property_t  cust_prop;
         capi_prop_stm_trigger_t trigger;
      } stm_trigger_t;

      stm_trigger_t stm_trigger;

      /* Populate the stm trigger */
      stm_trigger.cust_prop.secondary_prop_id     = FWK_EXTN_PROPERTY_ID_STM_TRIGGER;
      stm_trigger.trigger.signal_ptr              = (void *)me_ptr->st_module.trigger_signal_ptr;
      stm_trigger.trigger.raised_intr_counter_ptr = &me_ptr->st_module.raised_interrupt_counter;

      /* Enable the stm*/
      typedef struct
      {
         capi_custom_property_t cust_prop;
         capi_prop_stm_ctrl_t   ctrl;
      } stm_ctrl_t;

      stm_ctrl_t stm_ctrl;
      stm_ctrl.cust_prop.secondary_prop_id = FWK_EXTN_PROPERTY_ID_STM_CTRL;
      stm_ctrl.ctrl.enable                 = TRUE;

      capi_prop_t set_props[] = {
         { CAPI_CUSTOM_PROPERTY,
           { (int8_t *)(&stm_trigger), sizeof(stm_trigger), sizeof(stm_trigger) },
           { FALSE, FALSE, 0 } },
         { CAPI_CUSTOM_PROPERTY, { (int8_t *)(&stm_ctrl), sizeof(stm_ctrl), sizeof(stm_ctrl) }, { FALSE, FALSE, 0 } }
      };

      capi_proplist_t set_proplist = { SIZE_OF_ARRAY(set_props), set_props };

      if (CAPI_EOK != (result = module_ptr->capi_ptr->vtbl_ptr->set_properties(module_ptr->capi_ptr, &set_proplist)))
      {
         if (CAPI_EUNSUPPORTED == result)
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Unsupported stm cfg for input port 0x%x", result);
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Failed to apply trigger and stm cfg for input port 0x%x",
                         result);
            if (me_ptr->st_module.trigger_signal_ptr)
            {
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_ERROR_PRIO,
                            "Failed to set the trigger signal on the stm 0x%lx. Destroying trigger signal",
                            result);

               // Stop listening to the mask
               cu_stop_listen_to_mask(&me_ptr->cu, bit_mask);
               posal_signal_destroy(&me_ptr->st_module.trigger_signal_ptr);
               me_ptr->st_module.trigger_signal_ptr       = NULL;
               me_ptr->st_module.raised_interrupt_counter = 0;
            }
            THROW(result, AR_EFAILED);
         }
      }
      else
      {
         capi_param_id_stm_latest_trigger_ts_ptr_t cfg = { 0 };

         uint32_t param_payload_size = (uint32_t)sizeof(capi_param_id_stm_latest_trigger_ts_ptr_t);
         result                      = gen_topo_capi_get_param(me_ptr->topo.gu.log_id,
                                          module_ptr->capi_ptr,
                                          FWK_EXTN_PARAM_ID_LATEST_TRIGGER_TIMESTAMP_PTR,
                                          (int8_t *)&cfg,
                                          &param_payload_size);

         if (AR_DID_FAIL(result))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Failed to get timestamp pointer 0x%x. Ignoring.",
                         result);
         }
         me_ptr->st_module.st_module_ts_ptr   = cfg.ts_ptr;
         me_ptr->st_module.update_stm_ts_fptr = cfg.update_stm_ts_fptr;
         me_ptr->st_module.stm_ts_ctxt_ptr    = cfg.stm_ts_ctxt_ptr;

         if (NULL != cfg.ts_ptr)
         {
            me_ptr->st_module.st_module_ts_ptr->is_valid = FALSE;
         }

         gen_cntr_set_stm_ts_to_module(me_ptr);
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

ar_result_t gen_cntr_fwk_extn_handle_at_start(gen_cntr_t *me_ptr, gu_module_list_t *module_list_ptr)
{
   ar_result_t result = AR_EOK;

   gu_module_list_t *st_module_list_ptr = NULL;

   // first handle async extension modules
   // check if STM extension module is found and cache that module ptr
   for (; (NULL != module_list_ptr); LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

      if (module_ptr->flags.need_async_st_extn)
      {
         result |= gen_cntr_fwk_extn_async_signal_enable(me_ptr, module_ptr);
      }
      else if (module_ptr->flags.need_stm_extn)
      {
         // caches the STM module,
         spf_list_insert_tail((spf_list_node_t **)&st_module_list_ptr, module_ptr, me_ptr->cu.heap_id, TRUE);
      }
   }

   // handle STM module enable
   if (st_module_list_ptr)
   {
      // pass thru container allows enabling multiple HW instances in one container.
      if (check_if_pass_thru_container(me_ptr))
      {
         result |= pt_cntr_stm_fwk_extn_handle_enable((pt_cntr_t *)me_ptr, st_module_list_ptr);
      }
      else
      {
         if (NULL == st_module_list_ptr->next_ptr)
         {
            result |= gen_cntr_stm_fwk_extn_handle_enable(me_ptr, (gen_topo_module_t *)st_module_list_ptr->module_ptr);
         }
         else // if more than one STM module is found in generic container return error
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "more than 1 STM modules found.");
            result = AR_EFAILED;
         }
      }

      spf_list_delete_list((spf_list_node_t **)&st_module_list_ptr, TRUE);
   }

   return result;
}

ar_result_t gen_cntr_fwk_extn_handle_at_stop(gen_cntr_t *me_ptr, gu_module_list_t *module_list_ptr)
{
   ar_result_t       result             = AR_EOK;
   gu_module_list_t *st_module_list_ptr = NULL;

   for (; (NULL != module_list_ptr); LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
      if (module_ptr->flags.need_stm_extn)
      {
         // caches the STM module,
         spf_list_insert_tail((spf_list_node_t **)&st_module_list_ptr, module_ptr, me_ptr->cu.heap_id, TRUE);
      }
      else if (module_ptr->flags.need_async_st_extn)
      {
         result |= gen_cntr_fwk_extn_async_signal_disable(me_ptr, module_ptr);
      }
   }

   // handle STM module disable
   if (st_module_list_ptr)
   {
      if (check_if_pass_thru_container(me_ptr))
      {
         result = pt_cntr_stm_fwk_extn_handle_disable((pt_cntr_t *)me_ptr, st_module_list_ptr);
      }
      else
      {
         if (NULL == st_module_list_ptr->next_ptr)
         {
            result |= gen_cntr_stm_fwk_extn_handle_disable(me_ptr, (gen_topo_module_t *)st_module_list_ptr->module_ptr);
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "more than 1 STM modules found.");
            result = AR_EFAILED;
         }
      }
      spf_list_delete_list((spf_list_node_t **)&st_module_list_ptr, TRUE);
   }

   return result;
}

static ar_result_t gen_cntr_fwk_extn_handle_at_suspend(gen_cntr_t *me_ptr, gu_module_list_t *module_list_ptr)
{
   ar_result_t result         = AR_EOK;
   gu_module_list_t *st_module_list_ptr = NULL;

   for (; (NULL != module_list_ptr); LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
      if (module_ptr->flags.need_stm_extn)
      {
         // caches the STM module,
         spf_list_insert_tail((spf_list_node_t **)&st_module_list_ptr, module_ptr, me_ptr->cu.heap_id, TRUE);
      }
      else if (module_ptr->flags.need_async_st_extn)
      {
         result |= gen_cntr_fwk_extn_async_signal_disable(me_ptr, module_ptr);
      }
   }

   if (st_module_list_ptr)
   {
      if (check_if_pass_thru_container(me_ptr))
      {
         result = pt_cntr_stm_fwk_extn_handle_disable((pt_cntr_t *)me_ptr, st_module_list_ptr);
      }
      else // gen cntr
      {
         if (NULL == st_module_list_ptr->next_ptr)
         {
            result |= gen_cntr_stm_fwk_extn_handle_disable(me_ptr, (gen_topo_module_t *)st_module_list_ptr->module_ptr);
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "more than 1 STM modules found.");
            result = AR_EFAILED;
         }
      }
   }

   spf_list_delete_list((spf_list_node_t **)&st_module_list_ptr, TRUE);
   return result;
}

static ar_result_t gen_cntr_capi_set_fwk_extn_proc_dur_per_module(gen_cntr_t        *me_ptr,
                                                                  gen_topo_module_t *module_ptr,
                                                                  uint32_t           cont_proc_dur_us)
{
   capi_err_t err_code = CAPI_EOK;

   fwk_extn_param_id_container_proc_duration_t delay_ops;
   delay_ops.proc_duration_us = cont_proc_dur_us;

   err_code = gen_topo_capi_set_param(me_ptr->topo.gu.log_id,
                                      module_ptr->capi_ptr,
                                      FWK_EXTN_PARAM_ID_CONTAINER_PROC_DURATION,
                                      (int8_t *)&delay_ops,
                                      sizeof(delay_ops));

   if ((err_code != CAPI_EOK) && (err_code != CAPI_EUNSUPPORTED))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Module 0x%lX: setting container proc delay failed",
                   module_ptr->gu.module_instance_id);
      return capi_err_to_ar_result(err_code);
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Module 0x%lX: setting container proc delay of %lu",
                   module_ptr->gu.module_instance_id,
                   delay_ops.proc_duration_us);
   }

   return AR_EOK;
}

/* Iterates over all modules in the sg list and tries to set proc delay if it supports this extension*/
ar_result_t gen_cntr_capi_set_fwk_extn_proc_dur(gen_cntr_t *me_ptr, uint32_t cont_proc_dur_us)
{
   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         // if proc delay fwk extension is not supported return
         if (FALSE == module_ptr->flags.need_proc_dur_extn)
         {
            continue;
         }

         gen_cntr_capi_set_fwk_extn_proc_dur_per_module(me_ptr, module_ptr, cont_proc_dur_us);
      }
   }
   return AR_EOK;
}

static ar_result_t gen_cntr_handle_fwk_ext_at_init(gen_cntr_t *me_ptr, gen_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;
   SPF_MANAGE_CRITICAL_SECTION

   if (module_ptr->flags.need_thresh_cfg_extn)
   {
      /* if container frame len is configured in the samples by container property then it can not be set to the modules
       * as threshold extension because at this point sampling rate is not known. In this case, threshold will be set
       * based on the SG performance mode only and module must raise threshold to avoid container running at the
       * configured frame len.*/
      uint32_t new_thresh_us = (me_ptr->cu.conf_frame_len.frame_len_us)
                                  ? me_ptr->cu.conf_frame_len.frame_len_us
                                  : (1000 * TOPO_PERF_MODE_TO_FRAME_DURATION_MS(module_ptr->gu.sg_ptr->perf_mode));

      // todo: such modules must raise threshold. check SPR
      fwk_extn_param_id_threshold_cfg_t fm_dur = { .duration_us = new_thresh_us };
      result |= gen_topo_capi_set_param(me_ptr->topo.gu.log_id,
                                        module_ptr->capi_ptr,
                                        FWK_EXTN_PARAM_ID_THRESHOLD_CFG,
                                        (int8_t *)&fm_dur,
                                        sizeof(fm_dur));
   }

   if (module_ptr->flags.need_cntr_frame_dur_extn)
   {
      if (me_ptr->cu.cntr_frame_len.frame_len_us)
      {
         // frame len can change if any media format or threshold changed in the context of data-path processing.
         SPF_CRITICAL_SECTION_START(&me_ptr->topo.gu);
         gen_topo_fwk_ext_set_cntr_frame_dur_per_module(&me_ptr->topo,
                                                        module_ptr,
                                                        me_ptr->cu.cntr_frame_len.frame_len_us);
         SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);
      }
   }

   if (module_ptr->flags.need_proc_dur_extn)
   {
      if (me_ptr->cu.cntr_proc_duration)
      {
         // frame len can change if any media format or threshold changed in the context of data-path processing.
         SPF_CRITICAL_SECTION_START(&me_ptr->topo.gu);
         gen_cntr_capi_set_fwk_extn_proc_dur_per_module(me_ptr, module_ptr, me_ptr->cu.cntr_proc_duration);
         SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);
      }
   }

   return result;
}

static ar_result_t gen_cntr_handle_fwk_ext_at_deinit(gen_cntr_t *me_ptr, gen_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   if (module_ptr->flags.need_soft_timer_extn)
   {
      cu_fwk_extn_soft_timer_destroy_at_close(&me_ptr->cu, &module_ptr->gu);
   }
   return result;
}

ar_result_t gen_cntr_handle_fwk_extn_pre_subgraph_op(gen_cntr_t       *me_ptr,
                                                     uint32_t          sg_op,
                                                     gu_module_list_t *module_list_ptr)
{
   ar_result_t result = AR_EOK;

   if (sg_op & TOPO_SG_OP_STOP)
   {
      result = gen_cntr_fwk_extn_handle_at_stop(me_ptr, module_list_ptr);
   }
   else if (sg_op & TOPO_SG_OP_SUSPEND)
   {
      result = gen_cntr_fwk_extn_handle_at_suspend(me_ptr, module_list_ptr);
   }

   return result;
}

ar_result_t gen_cntr_handle_fwk_extn_post_subgraph_op(gen_cntr_t       *me_ptr,
                                                      uint32_t          sg_op,
                                                      gu_module_list_t *module_list_ptr)
{
   ar_result_t result = AR_EOK;
   if (sg_op & TOPO_SG_OP_START)
   {
      result = gen_cntr_fwk_extn_handle_at_start(me_ptr, module_list_ptr);
   }

   return result;
}

// Topo to cntr call back to handle propagation at external output port.
// If the propagated property is is_upstrm_rt, cmd is sent to downstream cntr.

// note: in case of port state property apply_downgraded_state_on_output_port is used
ar_result_t gen_cntr_set_propagated_prop_on_ext_output(gen_topo_t               *topo_ptr,
                                                       gu_ext_out_port_t        *gu_out_port_ptr,
                                                       topo_port_property_type_t prop_type,
                                                       void                     *payload_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);

   gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)gu_out_port_ptr;

   if (PORT_PROPERTY_IS_UPSTREAM_RT == prop_type)
   {
      uint32_t *is_rt_ptr = (uint32_t *)payload_ptr;

      if (*is_rt_ptr != ext_out_port_ptr->cu.icb_info.flags.is_real_time)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "Propagating to peer: from ext output (0x%lX,0x%lx) forward prop upstream-real-time, prop_value "
                      "%u, "
                      "prev value %u",
                      gu_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
                      gu_out_port_ptr->int_out_port_ptr->cmn.id,
                      *is_rt_ptr,
                      ext_out_port_ptr->cu.icb_info.flags.is_real_time);

         ext_out_port_ptr->cu.icb_info.flags.is_real_time = *is_rt_ptr;

         // For ICB: forward prop from this containers input (or RT module) to output
         gen_cntr_ext_out_port_recreate_bufs((void *)&me_ptr->cu, gu_out_port_ptr);

         // downstream message is sent at the end
         // cu_inform_downstream_about_upstream_property
      }
   }

   return result;
}

// Handle property propagation from downstream
// Propagates the property update through the topology.
// note: in case of port state property apply_downgraded_state_on_input_port is used
ar_result_t gen_cntr_set_propagated_prop_on_ext_input(gen_topo_t               *topo_ptr,
                                                      gu_ext_in_port_t         *gu_in_port_ptr,
                                                      topo_port_property_type_t prop_type,
                                                      void                     *payload_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);

   gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)gu_in_port_ptr;

   if (PORT_PROPERTY_IS_DOWNSTREAM_RT == prop_type)
   {
      uint32_t *is_rt_ptr = (uint32_t *)payload_ptr;

      if (*is_rt_ptr != ext_in_port_ptr->cu.icb_info.flags.is_real_time)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "Propagating to peer: from ext input (0x%lX,%lu) backward prop downstream-real-time, "
                      "prop_value=%u, "
                      "prev "
                      "value %u",
                      gu_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      gu_in_port_ptr->int_in_port_ptr->cmn.id,
                      *is_rt_ptr,
                      ext_in_port_ptr->cu.icb_info.flags.is_real_time);

         ext_in_port_ptr->cu.icb_info.flags.is_real_time = *is_rt_ptr;

         ext_in_port_ptr->cu.prop_info.did_inform_us_of_frame_len_and_var_ip = FALSE;

         // ICB: here we only need to store the value to see diff.
         // upon receiving this message upstream will recreate ext buf

         // upstream message is sent later at the end
         // cu_inform_upstream_about_downstream_property
      }
   }

   return result;
}

ar_result_t gen_cntr_raise_data_to_dsp_service_event_non_island(gen_topo_module_t *module_ptr,
                                                                capi_event_info_t *event_info_ptr)
{
   gen_cntr_t                       *me_ptr        = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, module_ptr->topo_ptr);
   ar_result_t                       result        = AR_EOK;
   capi_buf_t                       *payload       = &event_info_ptr->payload;
   capi_event_data_to_dsp_service_t *dsp_event_ptr = (capi_event_data_to_dsp_service_t *)(payload->data_ptr);

   switch (dsp_event_ptr->param_id)
   {
      case CAPI_BT_CODEC_EXTN_EVENT_ID_DISABLE_PREBUFFER:
      case CAPI_BT_CODEC_EXTN_EVENT_ID_KPPS_SCALE_FACTOR:
      {
         result |= gen_cntr_handle_bt_codec_ext_event(module_ptr, event_info_ptr);
         break;
      }
      case FWK_EXTN_DM_EVENT_ID_DISABLE_DM:
      {
         result |= gen_topo_handle_dm_disable_event(&me_ptr->topo, module_ptr, dsp_event_ptr);
         break;
      }
      case FWK_EXTN_DM_EVENT_ID_REPORT_MAX_SAMPLES:
      case FWK_EXTN_DM_EVENT_ID_REPORT_SAMPLES:
      {
         // no need to handle the event since gen cntr always makes sure that enough buffer is available
         break;
      }
      case FWK_EXTN_EVENT_ID_SOFT_TIMER_START:
      {
         if (dsp_event_ptr->payload.actual_data_len < sizeof(fwk_extn_event_id_soft_timer_start_t))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Error in callback function. The actual size %lu is less than the required size "
                         "%lu for id 0x%lX.",
                         payload->actual_data_len,
                         sizeof(fwk_extn_event_id_soft_timer_start_t),
                         (uint32_t)(dsp_event_ptr->param_id));
            return AR_EBADPARAM;
         }

         fwk_extn_event_id_soft_timer_start_t *data_ptr =
            (fwk_extn_event_id_soft_timer_start_t *)(dsp_event_ptr->payload.data_ptr);
         int64_t duration_us = data_ptr->duration_ms * 1000;

         /* Does not support timer extn*/
         if (TRUE == module_ptr->flags.need_soft_timer_extn)
         {
            result = cu_fwk_extn_soft_timer_start(&me_ptr->cu, &module_ptr->gu, data_ptr->timer_id, duration_us);
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module %lx doesnt support timer ext",
                         module_ptr->gu.module_instance_id);
         }
         break;
      }
      case FWK_EXTN_EVENT_ID_SOFT_TIMER_DISABLE:
      {
         if (dsp_event_ptr->payload.actual_data_len < sizeof(fwk_extn_event_id_soft_timer_disable_t))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Error in callback function. The actual size %lu is less than the required size "
                         "%lu for "
                         "id %lu.",
                         payload->actual_data_len,
                         sizeof(fwk_extn_event_id_soft_timer_disable_t),
                         (uint32_t)(dsp_event_ptr->param_id));
            return AR_EBADPARAM;
         }

         fwk_extn_event_id_soft_timer_disable_t *data_ptr =
            (fwk_extn_event_id_soft_timer_disable_t *)(dsp_event_ptr->payload.data_ptr);
         if (TRUE == module_ptr->flags.need_soft_timer_extn)
         {
            result = cu_fwk_extn_soft_timer_disable(&me_ptr->cu, &module_ptr->gu, data_ptr->timer_id);
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module %lx doesnt support timer ext",
                         module_ptr->gu.module_instance_id);
         }
         break;
      }
      case FWK_EXTN_SYNC_EVENT_ID_ENABLE_THRESHOLD_BUFFERING:
      {
         result =
            gen_cntr_fwk_extn_sync_handle_toggle_threshold_buffering_event(me_ptr, module_ptr, payload, dsp_event_ptr);
         break;
      }
      case FWK_EXTN_SYNC_EVENT_ID_DATA_PORT_ACTIVITY_STATE:
      {
         // GC ignores this event.
         break;
      }
      case FWK_EXTN_EVENT_ID_ISLAND_EXIT:
      {
         /* Triggering Island is handled as a part of
            gen_topo_capi_callback function. So just breaking here */
         break;
      }
      default:
      {
         return cu_handle_event_to_dsp_service_topo_cb(&me_ptr->cu, &module_ptr->gu, event_info_ptr);
      }
   }

   return result;
}

ar_result_t gen_cntr_handle_capi_event(gen_topo_module_t *module_ptr,
                                       capi_event_id_t    event_id,
                                       capi_event_info_t *event_info_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, module_ptr->topo_ptr);
   ar_result_t result = AR_EOK;

   result = cu_handle_capi_event(&me_ptr->cu, &module_ptr->gu, event_id, event_info_ptr);

   return result;
}

ar_result_t gen_cntr_handle_algo_delay_change_event(gen_topo_module_t *module_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, module_ptr->topo_ptr);

   if (me_ptr->cu.flags.is_cntr_started)
   {
      // send notification to VCPM
      if (me_ptr->cu.voice_info_ptr)
      {
         me_ptr->cu.voice_info_ptr->event_flags.did_algo_delay_change = TRUE;
      }
   }

   return AR_EOK;
}

/*
 * Handles downgraded port START/STOP if necessary and updates state in topo layer.
 * This dowgraded state is based on,
 *  1. Sub graph state 2. Peer sub graph state 3. Propagated state.
 *
 * This function is called from cu_update_all_port_state.
 */
ar_result_t gen_cntr_apply_downgraded_state_on_input_port(cu_base_t        *cu_ptr,
                                                          gu_input_port_t  *gu_in_port_ptr,
                                                          topo_port_state_t downgraded_state)
{
   ar_result_t             result          = AR_EOK;
   gen_cntr_t             *me_ptr          = (gen_cntr_t *)cu_ptr;
   gen_cntr_ext_in_port_t *ext_in_port_ptr = NULL;
   gen_topo_input_port_t * in_port_ptr     = (gen_topo_input_port_t *)gu_in_port_ptr;

   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != gu_in_port_ptr));

   // If its an external port apply the downgraded state on ext port.
   // NOTE: as an optimization we can apply the port state only if there is a change.
   if (gu_in_port_ptr->ext_in_port_ptr)
   {
      ext_in_port_ptr = (gen_cntr_ext_in_port_t *)gu_in_port_ptr->ext_in_port_ptr;

      // Input port can be flushed only if the connected peer port state is STOPPED. Else it may result in infinite
      // loop. Ideally upstream is stopped first and then downstream to avoid infinite loops.
      //
      // But, due to state propagation the external input can be stopped if ext output stop in the container is
      // propagated backwards. In this case the upstream peer container is still in started state and input should
      // not be flushed to prevent infinite loop.
      // Message to upstream is sent in cu_inform_upstream_about_downstream_property
      if ((ext_in_port_ptr->cu.connected_port_state == TOPO_PORT_STATE_STOPPED) &&
          ((TOPO_PORT_STATE_STOPPED == downgraded_state) || (TOPO_PORT_STATE_SUSPENDED == downgraded_state)))
      {
         gen_cntr_flush_input_data_queue(me_ptr, ext_in_port_ptr, TRUE /* keep data msg */);
      }

      if ((TOPO_PORT_STATE_STOPPED) == downgraded_state)
      {
         // resets both internal and external port structure.
         gen_cntr_ext_in_port_reset(me_ptr, ext_in_port_ptr);
         ext_in_port_ptr->flags.is_not_reset = FALSE;
      }
      else if ((TOPO_PORT_STATE_STARTED) == downgraded_state)
      {
         ext_in_port_ptr->flags.is_not_reset = TRUE;
      }

      // stop listening to input if port is suspended/stopped
      if ((TOPO_PORT_STATE_STOPPED) == downgraded_state || (TOPO_PORT_STATE_SUSPENDED) == downgraded_state)
      {
         cu_stop_listen_to_mask(&me_ptr->cu, ext_in_port_ptr->cu.bit_mask);
      }

   } // end of ext port handling
   else
   {
      // Reset input port, if stopped
      if (TOPO_PORT_STATE_STOPPED == downgraded_state)
      {
         gen_topo_reset_input_port(&me_ptr->topo, in_port_ptr);
      }
   }

   // Apply port state on the internal port.
   // set data port state on the module.
   result = gen_topo_capi_set_data_port_op_from_state((gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr,
                                                      downgraded_state,
                                                      &in_port_ptr->common.last_issued_opcode,
                                                      TRUE, // is_input
                                                      in_port_ptr->gu.cmn.index,
                                                      in_port_ptr->gu.cmn.id);

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/*
 * Handles port START/STOP if necessary and updates state in topo layer.
 *
 * This function is called from cu_update_all_port_state.
 */
ar_result_t gen_cntr_apply_downgraded_state_on_output_port(cu_base_t        *cu_ptr,
                                                           gu_output_port_t *gu_out_port_ptr,
                                                           topo_port_state_t downgraded_state)
{
   ar_result_t              result           = AR_EOK;
   gen_cntr_t              *me_ptr           = (gen_cntr_t *)cu_ptr;
   gen_cntr_ext_out_port_t *ext_out_port_ptr = NULL;
   gen_topo_output_port_t * out_port_ptr     = (gen_topo_output_port_t *)gu_out_port_ptr;
   INIT_EXCEPTION_HANDLING
   VERIFY(result, (NULL != gu_out_port_ptr));

   // If its an external port apply the downgraded state on ext output port.
   // NOTE: as an optimization we can apply the port state only if there is a change.
   if (gu_out_port_ptr->ext_out_port_ptr)
   {
      ext_out_port_ptr = (gen_cntr_ext_out_port_t *)gu_out_port_ptr->ext_out_port_ptr;

      if ((ext_out_port_ptr->cu.connected_port_state == TOPO_PORT_STATE_STOPPED) &&
          (TOPO_PORT_STATE_STOPPED == downgraded_state))
      {
         gen_cntr_flush_output_data_queue(me_ptr, ext_out_port_ptr, FALSE);
      }

      if ((TOPO_PORT_STATE_STOPPED == downgraded_state))
      {
         // resets both internal and external port structure.
         (void)gen_cntr_ext_out_port_reset(me_ptr, ext_out_port_ptr);
         ext_out_port_ptr->flags.is_not_reset = FALSE;
      }
      else if ((TOPO_PORT_STATE_STARTED) == downgraded_state)
      {
         ext_out_port_ptr->flags.is_not_reset = TRUE;
      }

      if ((TOPO_PORT_STATE_STOPPED == downgraded_state) || (TOPO_PORT_STATE_SUSPENDED == downgraded_state))
      {
         cu_stop_listen_to_mask(&me_ptr->cu, ext_out_port_ptr->cu.bit_mask);
      }
   } // end of ext port handling
   else
   {
      if (TOPO_PORT_STATE_STOPPED == downgraded_state)
      {
         gen_topo_reset_output_port(&me_ptr->topo, out_port_ptr);
      }
   }

   // Apply port state on the internal port.
   // Once output port is in start state, it comes out of reset state.
   if (TOPO_PORT_STATE_STARTED == downgraded_state)
   {
      out_port_ptr->common.flags.port_is_not_reset = TRUE;
   }

   // set data port state on the module.
   result = gen_topo_capi_set_data_port_op_from_state((gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr,
                                                      downgraded_state,
                                                      &out_port_ptr->common.last_issued_opcode,
                                                      FALSE, // is_input
                                                      out_port_ptr->gu.cmn.index,
                                                      out_port_ptr->gu.cmn.id);

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->cu.gu_ptr->log_id)
   {
   }

   return result;
}

/**
 * this is callback from topo
 */
ar_result_t gen_cntr_destroy_module(gen_topo_t        *topo_ptr,
                                    gen_topo_module_t *module_ptr,
                                    bool_t             reset_capi_dependent_dont_destroy)
{
   ar_result_t        result              = AR_EOK;
   gen_cntr_t        *me_ptr              = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);
   gen_cntr_module_t *gen_cntr_module_ptr = (gen_cntr_module_t *)module_ptr;

   gen_cntr_handle_fwk_ext_at_deinit(me_ptr, module_ptr);

   if (gen_cntr_module_ptr->fwk_module_ptr)
   {
      for (spf_list_node_t *cached_param_list_ptr = gen_cntr_module_ptr->fwk_module_ptr->cached_param_list_ptr;
           (NULL != cached_param_list_ptr);
           LIST_ADVANCE(cached_param_list_ptr))
      {
         gen_topo_cached_param_node_t *obj_ptr = (gen_topo_cached_param_node_t *)cached_param_list_ptr->obj_ptr;
         if (NULL != obj_ptr)
         {
            MFREE_NULLIFY(obj_ptr->payload_ptr);
         }
      }

      spf_list_delete_list_and_free_objs(&gen_cntr_module_ptr->fwk_module_ptr->cached_param_list_ptr,
                                         TRUE /* pool_used */);

      if (!reset_capi_dependent_dont_destroy)
      {
         MFREE_NULLIFY(gen_cntr_module_ptr->fwk_module_ptr);
      }
   }

   // for all modules delete cu event list
   cu_delete_all_event_nodes(&gen_cntr_module_ptr->cu.event_list_ptr);

   return result;
}

/**
 * this is callback from topo
 */
ar_result_t gen_cntr_create_module(gen_topo_t            *topo_ptr,
                                   gen_topo_module_t     *module_ptr,
                                   gen_topo_graph_init_t *graph_init_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_cntr_t        *me_ptr              = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);
   gen_cntr_module_t *gen_cntr_module_ptr = (gen_cntr_module_t *)module_ptr;

   if (AMDB_MODULE_TYPE_FRAMEWORK == module_ptr->gu.module_type)
   {
      MALLOC_MEMSET(gen_cntr_module_ptr->fwk_module_ptr,
                    gen_cntr_fwk_module_t,
                    sizeof(gen_cntr_fwk_module_t),
                    topo_ptr->heap_id,
                    result);

      switch (module_ptr->gu.module_id)
      {
         case MODULE_ID_WR_SHARED_MEM_EP:
         {
            TRY(result, gen_cntr_create_wr_sh_mem_ep(me_ptr, module_ptr, graph_init_ptr));
            break;
         }
         case MODULE_ID_RD_SHARED_MEM_EP:
         {
            TRY(result, gen_cntr_create_rd_sh_mem_ep(me_ptr, module_ptr, graph_init_ptr));
            break;
         }
         case MODULE_ID_PLACEHOLDER_ENCODER:
         case MODULE_ID_PLACEHOLDER_DECODER:
         {
            TRY(result, gen_cntr_create_placeholder_module(me_ptr, module_ptr, graph_init_ptr));
            break;
         }
         default:
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_HIGH_PRIO,
                         "Unknown fwk module id 0x%lX",
                         module_ptr->gu.module_instance_id);
            THROW(result, AR_EFAILED);
         }
      }
   }

   gen_cntr_handle_fwk_ext_at_init(me_ptr, module_ptr);

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

ar_result_t gen_cntr_cache_set_event_prop(gen_cntr_t        *me_ptr,
                                          gen_cntr_module_t *module_ptr,
                                          topo_reg_event_t  *event_cfg_payload_ptr,
                                          bool_t             is_register)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = me_ptr->topo.gu.log_id;

   int8_t       *event_cfg_ptr = NULL;
   POSAL_HEAP_ID heap_id;

   heap_id = me_ptr->topo.heap_id;

   cu_client_info_t cached_event_node;

   cached_event_node.src_port                  = event_cfg_payload_ptr->src_port;
   cached_event_node.src_domain_id             = event_cfg_payload_ptr->src_domain_id;
   cached_event_node.dest_domain_id            = event_cfg_payload_ptr->dest_domain_id;
   cached_event_node.token                     = event_cfg_payload_ptr->token;
   cached_event_node.event_cfg.actual_data_len = event_cfg_payload_ptr->event_cfg.actual_data_len;
   cached_event_node.event_cfg.data_ptr        = NULL;

   if (0 != cached_event_node.event_cfg.actual_data_len)
   {
      event_cfg_ptr = (int8_t *)posal_memory_malloc(cached_event_node.event_cfg.actual_data_len, heap_id);

      VERIFY(result, NULL != event_cfg_ptr);

      cached_event_node.event_cfg.data_ptr = event_cfg_ptr;
      memscpy(cached_event_node.event_cfg.data_ptr,
              cached_event_node.event_cfg.actual_data_len,
              event_cfg_payload_ptr->event_cfg.data_ptr,
              cached_event_node.event_cfg.actual_data_len);
   }

   GEN_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "Caching event ID %lx of size %lu",
                event_cfg_payload_ptr->event_id,
                cached_event_node.event_cfg.actual_data_len);

   if (is_register)
   {
      TRY(result,
          cu_event_add_client(log_id,
                              event_cfg_payload_ptr->event_id,
                              &cached_event_node,
                              &module_ptr->cu.event_list_ptr,
                              heap_id));
   }
   else
   {
      TRY(result,
          cu_event_delete_client(log_id,
                                 event_cfg_payload_ptr->event_id,
                                 &cached_event_node,
                                 &module_ptr->cu.event_list_ptr));
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, log_id)
   {
      MFREE_NULLIFY(event_cfg_ptr);
   }

   return result;
}

ar_result_t gen_cntr_aggregate_hw_acc_proc_delay(void *cu_ptr, uint32_t *hw_acc_proc_delay_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)cu_ptr;

   return gen_topo_aggregate_hw_acc_proc_delay(&me_ptr->topo, TRUE /* only aggregate */, hw_acc_proc_delay_ptr);
}

/* Note :
   PM_ISLAND_VOTE_DONT_CARE should only be passed to this function,
   if container is not started then vote for island don't care
   if all ports are ftrt and at gap then vote for island don't care.
   That means container is inactive and can be assumed that modules have stopped processing,
   So, module's vote is not legitimate in this case.
*/

ar_result_t gen_cntr_update_island_vote(gen_cntr_t *me_ptr, posal_pm_island_vote_t fwk_island_vote)
{
   ar_result_t result = AR_EOK;

   // If island container, vote for island participation here
   if (cu_is_island_container(&me_ptr->cu))
   {
      posal_pm_island_vote_t overall_island_vote;
      overall_island_vote.island_vote_type = PM_ISLAND_VOTE_EXIT;
      overall_island_vote.island_type      = PM_ISLAND_TYPE_DEFAULT;

      // Vote for Don't care island precedes over aggregated Votes from module.
      if (PM_ISLAND_VOTE_DONT_CARE == fwk_island_vote.island_vote_type)
      {
         overall_island_vote.island_vote_type = PM_ISLAND_VOTE_DONT_CARE;
      }
      else
      {
         posal_pm_island_vote_t topo_island_vote = gen_topo_aggregate_island_vote(&me_ptr->topo);
         if ((PM_ISLAND_VOTE_ENTRY == fwk_island_vote.island_vote_type) &&
             (PM_ISLAND_VOTE_ENTRY == topo_island_vote.island_vote_type))
         {
            overall_island_vote.island_vote_type = PM_ISLAND_VOTE_ENTRY;
            overall_island_vote.island_type      = topo_island_vote.island_type;
         }
      }

      // reset the frame count
      me_ptr->topo.wc_time_at_first_frame = 0;

      if ((me_ptr->topo.flags.aggregated_island_vote == overall_island_vote.island_vote_type) &&
          (me_ptr->topo.flags.aggregated_island_type == overall_island_vote.island_type))
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "Did not Vote for or against Island as vote is same as previous vote %lu - island_type: %lu",
                      overall_island_vote.island_vote_type,
                      overall_island_vote.island_type);
#endif
         return result;
      }

      result = cu_handle_island_vote(&me_ptr->cu, overall_island_vote);

      if (AR_EOK == result)
      {
         me_ptr->topo.flags.aggregated_island_vote = overall_island_vote.island_vote_type;
         me_ptr->topo.flags.aggregated_island_type = overall_island_vote.island_type;
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "Voted for Island, Vote type = %lu",
                      me_ptr->topo.flags.aggregated_island_vote);
#endif
      }
   }

   return result;
}

ar_result_t gen_cntr_check_and_vote_for_island_in_data_path_(gen_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   if (me_ptr->topo.wc_time_at_first_frame > 0)
   {
      // if first frame time is alredy updated then compare it with the current time.

      uint64_t time_from_first_frame = posal_timer_get_time() - me_ptr->topo.wc_time_at_first_frame;

      // allow island entry if we have spent time processing at least two frames
      if (time_from_first_frame >= (me_ptr->cu.cntr_frame_len.frame_len_us * GEN_CNTR_PRE_ISLAND_FRAMES_TO_PROCESS))
      {
         posal_pm_island_vote_t island_vote;
         island_vote.island_vote_type = PM_ISLAND_VOTE_ENTRY;
         gen_cntr_update_island_vote(me_ptr, island_vote);
      }
   }
   else
   {
      // if first_frame time is reset then get the current time
      me_ptr->topo.wc_time_at_first_frame = posal_timer_get_time();
   }

   return result;
}

ar_result_t gen_cntr_handle_fwk_events_util_(gen_cntr_t *                me_ptr,
                                             gen_topo_capi_event_flag_t *capi_event_flag_ptr,
                                             cu_event_flags_t *          fwk_event_flag_ptr)
{
   ar_result_t result = AR_EOK;

   // Save original prio which could be due to module vote or container vote
   posal_thread_prio_t original_prio = posal_thread_prio_get2(me_ptr->cu.cmd_handle.thread_id);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "Handling fwk events: fwk events 0x%lX, capi events 0x%lX original_prio %d",
                fwk_event_flag_ptr->word,
                capi_event_flag_ptr->word,
                original_prio);

   bool_t prio_bumped_up_locally =
      gen_cntr_check_bump_up_thread_priority(&me_ptr->cu, TRUE /* is bump up*/, original_prio);
   // if bumped up locally, then also release locally.It will either bump up or fallback to original prio

   // don't call gen_cntr_wait_for_trigger from here as this func is called from data path as well.
   if (capi_event_flag_ptr->media_fmt_event || fwk_event_flag_ptr->port_state_change)
   {
      me_ptr->cu.topo_vtbl_ptr->propagate_media_fmt(&me_ptr->topo, FALSE /*is_data_path*/);
      gen_cntr_update_cntr_kpps_bw(me_ptr, FALSE /*force_aggregate*/);
   }

   if (capi_event_flag_ptr->port_thresh)
   {
      me_ptr->cu.cntr_vtbl_ptr->port_data_thresh_change(&me_ptr->cu);
   }

   if (capi_event_flag_ptr->port_prop_is_up_strm_rt_change)
   {
      me_ptr->cu.topo_vtbl_ptr->propagate_port_property(&me_ptr->topo, PORT_PROPERTY_IS_UPSTREAM_RT);
      capi_event_flag_ptr->port_prop_is_up_strm_rt_change = FALSE;
      cu_inform_downstream_about_upstream_property(&me_ptr->cu);
      fwk_event_flag_ptr->rt_ftrt_change = TRUE;
   }

   if (capi_event_flag_ptr->port_prop_is_down_strm_rt_change)
   {
      me_ptr->cu.topo_vtbl_ptr->propagate_port_property(&me_ptr->topo, PORT_PROPERTY_IS_DOWNSTREAM_RT);
      capi_event_flag_ptr->port_prop_is_down_strm_rt_change = FALSE;
      cu_inform_upstream_about_downstream_property(&me_ptr->cu);
      fwk_event_flag_ptr->rt_ftrt_change = TRUE;
   }

   if (fwk_event_flag_ptr->rt_ftrt_change)
   {
      cu_is_realtime(&me_ptr->cu);
   }

   if (capi_event_flag_ptr->dynamic_inplace_change)
   {
      gen_topo_handle_pending_dynamic_inplace_change(&me_ptr->topo, capi_event_flag_ptr);
   }

   if (capi_event_flag_ptr->realloc_scratch_mem)
   {
      result = gen_topo_check_n_realloc_scratch_memory(&me_ptr->topo, FALSE /*is_open_context*/);
   }

   gen_cntr_perf_vote(me_ptr, original_prio, capi_event_flag_ptr, fwk_event_flag_ptr);

   if (capi_event_flag_ptr->is_signal_triggered_active_change)
   {
      uint32_t timer_bit_mask = GEN_CNTR_TIMER_BIT_MASK;

      if (me_ptr->topo.flags.is_signal_triggered_active)
      {
         cu_start_listen_to_mask(&me_ptr->cu, timer_bit_mask);
      }
      else
      {
         posal_signal_clear_inline(me_ptr->st_module.trigger_signal_ptr);
         cu_stop_listen_to_mask(&me_ptr->cu, timer_bit_mask);
      }
   }

   if (me_ptr->cu.flags.is_cntr_started)
   {
      // the voice flags are only to track changes on the steady state
      if (me_ptr->cu.voice_info_ptr)
      {
         me_ptr->cu.voice_info_ptr->event_flags.did_frame_size_change = fwk_event_flag_ptr->frame_len_change;
      }

      // Additional to module path delay, cntr's ext input's report additional delay depending upon the upstream frame
      // length and US RT, hence make sure path delay is updated if one of them changes.
      if (fwk_event_flag_ptr->frame_len_change || fwk_event_flag_ptr->proc_dur_change ||
          capi_event_flag_ptr->algo_delay_event || fwk_event_flag_ptr->upstream_frame_len_change ||
          fwk_event_flag_ptr->rt_ftrt_change)
      {
         cu_update_path_delay(&me_ptr->cu, CU_PATH_ID_ALL_PATHS);
      }

      // module active depends on state propagation, SG state cmd, module enable/disable, trigger policy change.
      if (fwk_event_flag_ptr->port_state_change || fwk_event_flag_ptr->sg_state_change ||
          capi_event_flag_ptr->process_state || capi_event_flag_ptr->data_trigger_policy_change ||
          capi_event_flag_ptr->signal_trigger_policy_change)
      {
         gen_topo_set_module_active_flag_for_all_modules(&me_ptr->topo);
      }

      if (fwk_event_flag_ptr->sg_state_change || fwk_event_flag_ptr->frame_len_change ||
          capi_event_flag_ptr->data_trigger_policy_change || capi_event_flag_ptr->signal_trigger_policy_change)
      {
         gen_cntr_check_and_assign_st_data_process_fn(me_ptr);
      }
   }
#ifdef CONTAINER_ASYNC_CMD_HANDLING
   else if (fwk_event_flag_ptr->frame_len_change && (NULL == me_ptr->cu.async_cmd_handle))
   {
      // setup the thread pool to handle asynchronous command if container is not started yet.
      cu_async_cmd_handle_init(&me_ptr->cu, GEN_CNTR_SYNC_CMD_BIT_MASK);
   }
#endif

   if (fwk_event_flag_ptr->proc_dur_change)
   {
      gen_cntr_capi_set_fwk_extn_proc_dur(me_ptr, me_ptr->cu.cntr_proc_duration);
   }

   if (me_ptr->cu.voice_info_ptr && capi_event_flag_ptr->hw_acc_proc_delay_event)
   {
      me_ptr->cu.voice_info_ptr->event_flags.did_hw_acc_proc_delay_change =
         capi_event_flag_ptr->hw_acc_proc_delay_event;
   }

#if 0
   // if there is MF/SG state change/process state change/threshold change process list is required to be udpated
   // for pass thru container.
   // this takes care of
   if (check_if_pass_thru_container(me_ptr))
   {
      if (me_ptr->cu.flags.is_cntr_started)
      {
         if ((fwk_event_flag_ptr->port_state_change || fwk_event_flag_ptr->sg_state_change ||
              capi_event_flag_ptr->port_prop_is_up_strm_rt_change))
         {
            result |= pt_cntr_update_module_process_list((pt_cntr_t *)me_ptr);
         }

         if (fwk_event_flag_ptr->frame_len_change || fwk_event_flag_ptr->upstream_frame_len_change ||
             capi_event_flag_ptr->media_fmt_event || capi_event_flag_ptr->port_thresh)
         {
            /** Assign topo buffers to the modules in the proc list*/
            result |= pt_cntr_assign_port_buffers((pt_cntr_t *)me_ptr);
         }
      }
   }
#endif

   // if currently processing with thin topo, check if anything needs to be updated in thin topo or needs to switch to
   // gen topo
   result |= thin_topo_handle_fwk_events(me_ptr, capi_event_flag_ptr, fwk_event_flag_ptr);

   capi_event_flag_ptr->word                     = 0;
   fwk_event_flag_ptr->word                      = 0;
   me_ptr->topo.flags.defer_voting_on_dfs_change = 0;

   // if priority was bumped up else where they prio will becomes normal when they bump-down.
   if (prio_bumped_up_locally)
   {
      gen_cntr_check_bump_up_thread_priority(&me_ptr->cu, FALSE /* is bump up*/, original_prio);
   }

   return result;
}

static uint32_t gen_cntr_aggregate_ext_in_port_delay_util(gen_cntr_t *me_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   gen_cntr_ext_in_port_t *ext_in_port_ptr      = (gen_cntr_ext_in_port_t *)gu_ext_in_port_ptr;
   gen_topo_input_port_t  *in_port_ptr          = (gen_topo_input_port_t *)gu_ext_in_port_ptr->int_in_port_ptr;
   gen_topo_input_port_t  *nblc_end_in_port_ptr = in_port_ptr;
   if (in_port_ptr->nblc_end_ptr && (in_port_ptr->nblc_end_ptr != in_port_ptr))
   {
      nblc_end_in_port_ptr = in_port_ptr->nblc_end_ptr;
   }

   // If the US is RT and cntr frame length is more than the upstream, then the input needs to buffer and
   // adds up to an additioanl path delay
   gen_topo_module_t *nblc_end_module_ptr      = (gen_topo_module_t *)nblc_end_in_port_ptr->gu.cmn.module_ptr;
   uint32_t           delay                    = 0;
   const uint32_t     upstrm_frame_duration_us = ext_in_port_ptr->cu.upstream_frame_len.frame_len_us;
   const uint32_t     self_frame_duration_us   = me_ptr->cu.cntr_frame_len.frame_len_us;
   if (ext_in_port_ptr->cu.prop_info.is_us_rt && upstrm_frame_duration_us && self_frame_duration_us &&
       self_frame_duration_us > upstrm_frame_duration_us)
   {
      /**
       * If topo doesn't require buf, then we call process only after the input buf is filled. This adds delay.
       * We need to subtract this delay by upstream frame duration.
       *
       * IF there is a sync module, container will be not be able to consume data from ext input even though there
       * req_data_buf==false, hence add delay if sync module is present.
       */
      bool_t is_sync_module_present = gen_topo_fwk_extn_does_sync_module_exist_downstream(&me_ptr->topo, in_port_ptr);
      if (!nblc_end_module_ptr->flags.requires_data_buf || is_sync_module_present)
      {
         // After first buffer arrives at the ext input, the no of additonal buffers required to start processing
         // ext input will account buffering delay.
         // Ex: cntr A (3ms) -> cntr B (7ms). Lets say at t=0 first 3ms arrives at B. B needs to wait for additional
         // 2*3ms buffers to start processing.
         uint32_t num_additional_buffers_req = TOPO_CEIL(self_frame_duration_us, upstrm_frame_duration_us) - 1;
         delay                               = num_additional_buffers_req * upstrm_frame_duration_us;
      }
   }

#ifdef VERBOSE_DEBUGGING
   uint32_t mask = 0;
   mask |= ext_in_port_ptr->cu.prop_info.is_us_rt;
   mask |= (nblc_end_module_ptr->flags.requires_data_buf) << 4;
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "Ext-in-port 0x%lx us frame len:%lu self frame len:%lu (sync_port,rdbf,us_rt):(0x%lx) delay %lu",
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                upstrm_frame_duration_us,
                self_frame_duration_us,
                mask,
                delay);
#endif

   return delay;
}

/**
 * Callback from CU to get additional container delays due to requires data buf etc
 */
uint32_t gen_cntr_get_additional_ext_in_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   return gen_cntr_aggregate_ext_in_port_delay_util(me_ptr, gu_ext_in_port_ptr);
}

/**
 * callback from topo layer to get container delays due to requires data buf etc
 */
uint32_t gen_cntr_aggregate_ext_in_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);
   return cu_aggregate_ext_in_port_delay(&me_ptr->cu, gu_ext_in_port_ptr);
}

/**
 * callback from CU to get additional container delays.
 */
uint32_t gen_cntr_get_additional_ext_out_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr)
{
   // gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   return 0;
}

/**
 * callback from topo to get container delays
 */
uint32_t gen_cntr_aggregate_ext_out_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);
   return cu_aggregate_ext_out_port_delay(&me_ptr->cu, gu_ext_out_port_ptr);
}

ar_result_t gen_cntr_dcm_topo_set_param(void *cu_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)cu_ptr;
   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;
      for (gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr; NULL != module_list_ptr;
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
         if (TRUE == module_ptr->flags.supports_module_allow_duty_cycling)
         {
            intf_extn_param_id_cntr_duty_cycling_enabled_t p = { .is_cntr_duty_cycling =
                                                                    me_ptr->cu.pm_info.flags.register_with_dcm };

            me_ptr->cu.pm_info.flags.module_disallows_duty_cycling = me_ptr->cu.pm_info.flags.register_with_dcm;
            result = gen_topo_capi_set_param(module_ptr->topo_ptr->gu.log_id,
                                             module_ptr->capi_ptr,
                                             INTF_EXTN_PARAM_ID_CNTR_DUTY_CYCLING_ENABLED,
                                             (int8_t *)&p,
                                             sizeof(p));
            if (AR_DID_FAIL(result))
            {
               TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "topo_port_rt_prop:set cntr duty cycling flag on module 0x%x, "
                        "me_ptr->cu.cntr_is_duty_cycling %u "
                        "failed",
                        module_ptr->gu.module_instance_id,
                        me_ptr->cu.pm_info.flags.register_with_dcm);
               return result;
            }
            TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "set cntr duty cycling flag on module 0x%x, "
                     "me_ptr->cu.cntr_is_duty_cycling %u ",
                     module_ptr->gu.module_instance_id,
                     me_ptr->cu.pm_info.flags.register_with_dcm);
         }
      }
   }
   return result;
}

bool_t gen_cntr_is_signal_triggered(gen_cntr_t *me_ptr)
{
   if (me_ptr->topo.flags.is_signal_triggered)
   {
      return TRUE;
   }
   return FALSE;
}

/**
 * If internal eos is still at an input port (not at gap) of a module while that input port is closed, we need to ensure
 * that eos gets propagated to the next module, otherwise data flow gap is never communicated downstream.
 */
ar_result_t gen_cntr_check_insert_missing_eos_on_next_module(gen_topo_t *topo_ptr, gen_topo_input_port_t *in_port_ptr)
{
   ar_result_t        result     = AR_EOK;
   gen_cntr_t        *me_ptr     = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;

#if 0
   // if module owns the md propagation then it should raise eos after port op close.
   /* but if all the input ports are closed for the first module,
    * process will not be called and module won't be able to put eos on output buffer. */
   if (!gen_topo_fwk_owns_md_prop(module_ptr))
   {
      return result;
   }
#endif

   // Only move eof/dfg if all of a module's input ports are either closing or already in at-gap state.
   // If there are some port remained in data-flow state then module is responsible for pushing out eos
   // on the next process call based on close port_operation handling.

   // todo: a mimo module which has one of the output port as source will break with EOS insertion.

   // don't need to manually insert eos here if module itself is closing.
   bool_t is_module_closing = FALSE;
   cu_is_module_closing(&(me_ptr->cu), &(module_ptr->gu), &is_module_closing);
   if (is_module_closing)
   {
      return result;
   }

   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; in_port_list_ptr;
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      if (TOPO_DATA_FLOW_STATE_FLOWING == in_port_ptr->common.data_flow_state)
      {
         bool_t is_port_closing = FALSE;
         cu_is_in_port_closing(&(me_ptr->cu), &(in_port_ptr->gu), &is_port_closing);
         if (!is_port_closing)
         { // if port is not closing then can't insert eos. module should handle from process call.
            return result;
         }
      }
   }

   // reset the module to clear flushing eos or internal metadata.
   gen_topo_reset_module(topo_ptr, module_ptr);

   uint32_t INPUT_PORT_ID_NONE = 0; // Creating at internal port, no need for ext ip ref.
   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; out_port_list_ptr;
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      // if port is at-gap or eos/dfg is already present (maybe froom previous call of this func) then continue to next
      // port
      if (TOPO_DATA_FLOW_STATE_AT_GAP == out_port_ptr->common.data_flow_state ||
          gen_topo_md_list_has_flushing_eos_or_dfg(out_port_ptr->common.sdata.metadata_list_ptr))
      {
         continue;
      }

      module_cmn_md_eos_flags_t eos_md_flag = { .word = 0 };
      eos_md_flag.is_flushing_eos           = TRUE;
      eos_md_flag.is_internal_eos           = TRUE;

      uint32_t bytes_across_ch = gen_topo_get_actual_len_for_md_prop(&out_port_ptr->common);
      result                   = gen_topo_create_eos_for_cntr(topo_ptr,
                                            NULL, // for output ports we don't need container ref.
                                            INPUT_PORT_ID_NONE,
                                            topo_ptr->heap_id,
                                            &out_port_ptr->common.sdata.metadata_list_ptr,
                                            NULL,         /* md_flag_ptr */
                                            NULL,         /*tracking_payload_ptr*/
                                            &eos_md_flag, /* eos_payload_flags */
                                            bytes_across_ch,
                                            out_port_ptr->common.media_fmt_ptr);

      if (AR_SUCCEEDED(result))
      {
         out_port_ptr->common.sdata.flags.marker_eos = TRUE;
         me_ptr->topo.flags.process_us_gap           = TRUE; // to make sure process is called and eos is propagated
      }

      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_MED_PRIO,
               "MD_DBG: EOS moved to output port as part of close input (0x%lX, 0x%lx), result %ld",
               module_ptr->gu.module_instance_id,
               out_port_ptr->gu.cmn.id,
               result);
   }

   return result;
}

// caller's responsibility to call this function synchronously with the main data processing thread.
ar_result_t gen_cntr_allocate_wait_mask_arr(gen_cntr_t *me_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result                     = AR_EOK;
   uint32_t    curr_num_of_parallel_paths = me_ptr->topo.gu.num_parallel_paths;
   gu_update_parallel_paths(&(me_ptr->topo.gu));

   // at least assign one parallel path
   me_ptr->topo.gu.num_parallel_paths =
      (0 == me_ptr->topo.gu.num_parallel_paths) ? 1 : me_ptr->topo.gu.num_parallel_paths;

   if (curr_num_of_parallel_paths != me_ptr->topo.gu.num_parallel_paths)
   {
      MFREE_REALLOC_MEMSET(me_ptr->wait_mask_arr,
                           uint32_t,
                           sizeof(uint32_t) * me_ptr->topo.gu.num_parallel_paths,
                           me_ptr->cu.heap_id,
                           result);
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      me_ptr->topo.gu.num_parallel_paths = 0;
   }
   return result;
}
