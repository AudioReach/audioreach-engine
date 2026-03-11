/**
 * \file thin_topo_cntr_utils.c
 * \brief
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// clang-format off
/*
$Header:
*/
// clang-format on

#include "gen_cntr_i.h"
#include "thin_topo_cntr_utils_i.h"
#include "irm_cntr_prof_util.h"


GEN_TOPO_STATIC void thin_topo_reset_topo_buf_info(gen_topo_common_port_t *cmn_port_ptr)
{
   cmn_port_ptr->flags.buf_origin = GEN_TOPO_BUF_ORIGIN_INVALID;
   for (uint32_t b = 0; b < cmn_port_ptr->sdata.bufs_num; b++)
   {
      cmn_port_ptr->bufs_ptr[b].data_ptr        = NULL;
      cmn_port_ptr->bufs_ptr[b].actual_data_len = 0;
      cmn_port_ptr->bufs_ptr[b].max_data_len    = 0;
   }
}

/** Iterate backwards from the given input till the external input port(nblc_start_ptr)*/
GEN_CNTR_STATIC gen_cntr_ext_in_port_t *thin_topo_is_inplace_nblc_from_cur_in_to_ext_in(
   uint32_t               log_id,
   gen_topo_input_port_t *cur_in_port_ptr)
{
   gen_topo_input_port_t *nblc_start_ptr = cur_in_port_ptr->nblc_start_ptr;

   /** this function applies only in the context of inputs present in the NBLC of the external inputs*/
   if (!nblc_start_ptr || !nblc_start_ptr->gu.ext_in_port_ptr)
   {
      return NULL;
   }

   /** If current input iterator reached external input terminate the loop */
   while (nblc_start_ptr != cur_in_port_ptr)
   {
      /** this is unexpected if external output, shouldnt have entered the while loop*/
      if (!cur_in_port_ptr->gu.conn_out_port_ptr)
      {
         GEN_CNTR_MSG(log_id,
                      DBG_ERROR_PRIO,
                      " Potential issue with NBLC assignment, reached unexpected external input "
                      "(MIID,PID)(0x%lx,0x%lx)",
                      cur_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      cur_in_port_ptr->gu.cmn.id);
         return FALSE;
      }

      /** check if the prev module is inplace*/
      gen_topo_module_t *prev_module_ptr = (gen_topo_module_t *)cur_in_port_ptr->gu.conn_out_port_ptr->cmn.module_ptr;
      if (!gen_topo_is_inplace_or_disabled_siso(prev_module_ptr))
      {
         return NULL;
      }

      if (prev_module_ptr->gu.input_port_list_ptr)
      {
         gen_topo_input_port_t *prev_in_port_ptr =
            (gen_topo_input_port_t *)prev_module_ptr->gu.input_port_list_ptr->ip_port_ptr;

         /** update iterator and continue the loop*/
         cur_in_port_ptr = prev_in_port_ptr;
         continue;
      }
      else
      {
         /** unexpected, didnt reach the external input portential issue with NBLC assignment */
         GEN_CNTR_MSG(log_id,
                      DBG_ERROR_PRIO,
                      " Potential issue with NBLC assignment, reached unexpected external input "
                      "(MIID,PID)(0x%lx,0x%lx)",
                      cur_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      cur_in_port_ptr->gu.cmn.id);
         return NULL;
      }
   }

   return (gen_cntr_ext_in_port_t *)nblc_start_ptr->gu.ext_in_port_ptr;
}

GEN_CNTR_STATIC ar_result_t thin_topo_reset_ext_in_pass_thru_upstream_buf_flag(gen_cntr_t             *me_ptr,
                                                                               gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                               gen_topo_input_port_t  *in_port_ptr)
{
   gen_topo_t *topo_ptr = &me_ptr->topo;
   /** Check if the US and self operating frame lengths are exactly same. Also check if the given nblc is not DM
    * enabled path.*/
   bool_t is_variable_nblc_path                     = gen_topo_is_in_port_in_dm_variable_nblc(topo_ptr, in_port_ptr);
   ext_in_port_ptr->flags.pass_thru_upstream_buffer = FALSE;

   // check if US and DS has same operating frame lengths
   uint32_t upstream_pcm_frame_len_bytes = 0;
   if (FALSE == is_variable_nblc_path)
   {
      // check upstreams's PCM frame length
      if (SPF_IS_PCM_DATA_FORMAT(ext_in_port_ptr->cu.media_fmt.data_format))
      {
         upstream_pcm_frame_len_bytes =
            topo_us_to_bytes(ext_in_port_ptr->cu.upstream_frame_len.frame_len_us, in_port_ptr->common.media_fmt_ptr);
      }

      // check if PCM data needs reframing at external inputs
      if (SPF_IS_PCM_DATA_FORMAT(ext_in_port_ptr->cu.media_fmt.data_format) &&
          (upstream_pcm_frame_len_bytes == in_port_ptr->common.max_buf_len))
      {
         ext_in_port_ptr->flags.pass_thru_upstream_buffer = TRUE;
      }
   }

   GEN_CNTR_MSG(topo_ptr->gu.log_id,
                DBG_HIGH_PRIO,
                "External input port id (MIID, PID)(0x%lx, 0x%lx) has frame size for (US,DS)(%lu,%lu) "
                "pass_thru_upstream_buffer:%lu",
                in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                in_port_ptr->gu.cmn.id,
                ext_in_port_ptr->cu.upstream_frame_len.frame_len_us,
                me_ptr->cu.cntr_frame_len.frame_len_us,
                ext_in_port_ptr->flags.pass_thru_upstream_buffer);
   return AR_EOK;
}

/**
 * Module activeness is checked to see if a module process needs to be called in thin topo context.
 *
 * Note that the main difference between thin topo' and gen topo's version of module activeness check if
 * data flow state. todo: Potentially these two functions can be merged in future.
 *
 * A module is considered active if,
 * 1. Its SG is started
 * 2. If atleast one input && one output is trigger is satisifed. port trigger is considered satisfied only
 *    if port is STARTED && Data flow started && has valid MF.
 * 3. If source module's input port triggers are considered satified by default.
 * 4. If sink module's output port triggers are considered satified by default.
 * 5. If STM module's is always considered active.
 * 6. If a module is disabled and has the bypass ptr set. In that case module is considered bypassed.
 * 7. If its a fwk module i.e capi_ptr is NULL. in that case module will be considered as bypassed.
 */
GEN_CNTR_STATIC bool_t thin_topo_is_module_active(gen_topo_module_t *module_ptr, bool_t need_to_ignore_state)
{
   // Firtly check if modules input && output triggers are satisfied.
   //    1. If the module is acting as source input trigger by default satisified, same logic applies for sink as well.
   //    2. If the module is not source/sink, then atleast one output and input trigger must satisfied.

   // if a module cannot have outputs, or if module currently doesnt have any outputs connected and its min outputs are
   // zero.
   bool_t atleast_one_output_trigger_satisfied =
      (0 == module_ptr->gu.max_output_ports) ||
      ((0 == module_ptr->gu.num_output_ports) && (0 == module_ptr->gu.min_output_ports));

   // if module cannot have inputs, or if module currently doesnt have any inputs connected and its min inputs are
   // zero.
   bool_t atleast_one_input_trigger_satisfied =
      (0 == module_ptr->gu.max_input_ports) ||
      ((0 == module_ptr->gu.num_input_ports) && (0 == module_ptr->gu.min_input_ports));

   if (!need_to_ignore_state)
   {
      /**
       * If module belongs to a SG that's not started, then module is not active
       * But if SG is started, doesn't mean port is started (need to check down-graded state)
       */
      if (!gen_topo_is_module_sg_started(module_ptr))
      {
         return FALSE;
      }

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
         // If the input port is not started or doesn't have a buffer (media-fmt prop didn't happen)
         // then process cannot be called on the module.
         if ((TOPO_PORT_STATE_STARTED != in_port_ptr->common.state) ||
             (FALSE == in_port_ptr->common.flags.is_mf_valid) ||
             (TOPO_DATA_FLOW_STATE_AT_GAP == in_port_ptr->common.data_flow_state))
         {
            continue;
         }
         else
         {
            atleast_one_input_trigger_satisfied = TRUE;
         }
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         if ((TOPO_PORT_STATE_STARTED != out_port_ptr->common.state) ||
             (FALSE == out_port_ptr->common.flags.is_mf_valid))
         {
            continue;
         }
         else
         {
            atleast_one_output_trigger_satisfied = TRUE;
            break;
         }
      }
   }

   // todo: check if there is a module with mixed data flow states, switch to Gen topo.

   bool_t active_as_per_state =
      need_to_ignore_state || (atleast_one_input_trigger_satisfied && atleast_one_output_trigger_satisfied);
   if (active_as_per_state || module_ptr->flags.need_stm_extn)
   {
      if (module_ptr->bypass_ptr || !module_ptr->capi_ptr || !module_ptr->flags.disabled)
      {
         return TRUE;
      }
   }
   return FALSE;
}

/** Replaces the buffer in the port with a new topo buffer, and copies data from old to new buffer.*/
GEN_CNTR_STATIC ar_result_t thin_topo_replace_with_topo_buffer(gen_cntr_t             *me_ptr,
                                                               gen_topo_module_t      *module_ptr,
                                                               gen_topo_common_port_t *cmn_port_ptr,
                                                               uint32_t                port_id)
{

   // this can happen b4 thresh/MF prop
   if (0 == cmn_port_ptr->max_buf_len || !cmn_port_ptr->bufs_ptr[0].data_ptr ||
       !cmn_port_ptr->bufs_ptr[0].actual_data_len)
   {
      return AR_EOK;
   }

   // internally mem needed for topo_buf_mgr_element_t is counted.
   // Also ref count is initialized to 1.
   int8_t  *prev_ptr        = cmn_port_ptr->bufs_ptr[0].data_ptr;
   uint32_t prev_buf_origin = cmn_port_ptr->flags.buf_origin;
   int8_t  *new_ptr         = NULL;

   ar_result_t result = topo_buf_manager_get_buf(&me_ptr->topo, &new_ptr, cmn_port_ptr->max_buf_len);
   if (NULL == new_ptr)
   {
      return AR_EFAILED;
   }

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                " Module 0x%lX: Port id 0x%lx, replacing prev buf (0x%lx, (%lu of %lu)) with new buf (0x%lx, "
                "(%lu, %lu))",
                module_ptr->gu.module_instance_id,
                port_id,
                cmn_port_ptr->bufs_ptr[0].data_ptr,
                cmn_port_ptr->bufs_ptr[0].actual_data_len,
                cmn_port_ptr->bufs_ptr[0].max_data_len,
                new_ptr,
                MIN(cmn_port_ptr->bufs_ptr[0].actual_data_len, cmn_port_ptr->max_buf_len_per_buf),
                cmn_port_ptr->max_buf_len_per_buf);
#endif
   for (uint32_t b = 0; b < cmn_port_ptr->sdata.bufs_num; b++)
   {
      cmn_port_ptr->bufs_ptr[b].actual_data_len = memscpy(new_ptr,
                                                          cmn_port_ptr->max_buf_len_per_buf,
                                                          cmn_port_ptr->bufs_ptr[b].data_ptr,
                                                          cmn_port_ptr->bufs_ptr[b].actual_data_len);

      cmn_port_ptr->bufs_ptr[b].data_ptr     = new_ptr;
      cmn_port_ptr->bufs_ptr[b].max_data_len = cmn_port_ptr->max_buf_len_per_buf;

      new_ptr += cmn_port_ptr->max_buf_len_per_buf;
   }
   cmn_port_ptr->flags.buf_origin = GEN_TOPO_BUF_ORIGIN_BUF_MGR;

   // return prev buffer to buffer manager
   if (GEN_TOPO_BUF_ORIGIN_BUF_MGR == prev_buf_origin)
   {
      topo_buf_manager_element_t *wrapper_ptr = (topo_buf_manager_element_t *)(prev_ptr - TBF_BUF_PTR_OFFSET);
      if (wrapper_ptr->ref_count >= 1)
      {
         wrapper_ptr->ref_count--;
      }

      if (0 == wrapper_ptr->ref_count)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      " Module 0x%lX: Port id 0x%lx, return buf to buf mgr 0x%p",
                      module_ptr->gu.module_instance_id,
                      port_id,
                      cmn_port_ptr->bufs_ptr[0].data_ptr);
#endif
         topo_buf_manager_return_buf(&me_ptr->topo, prev_ptr);
      }
   }
   return result;
}

/** Free buffers at the ports without any data */
GEN_CNTR_STATIC ar_result_t thin_topo_return_assigned_port_buffers(gen_cntr_t *me_ptr,
                                                                   bool_t      force_free_buffers_with_data)
{
   gu_module_list_t *module_list_ptr = me_ptr->topo.started_sorted_module_list_ptr;

   // release buffers if not required
   for (; (NULL != module_list_ptr); LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         uint32_t data_dropped = 0;
         if (in_port_ptr->common.bufs_ptr[0].actual_data_len && force_free_buffers_with_data)
         {
            data_dropped                                    = in_port_ptr->common.bufs_ptr[0].actual_data_len;
            in_port_ptr->common.bufs_ptr[0].actual_data_len = 0;

            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module 0x%lX: Input port id 0x%lx, dropping data data_dropped=%lu, unexpected",
                         module_ptr->gu.module_instance_id,
                         in_port_ptr->gu.cmn.id,
                         data_dropped);

            // crashes only for sim
            spf_svc_crash();
         }

#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "Module 0x%lX: Input port id 0x%lx, Freeing input port buffer 0x%lx len (%lu of %lu) origin %lu, "
                      "data_dropped=%lu",
                      module_ptr->gu.module_instance_id,
                      in_port_ptr->gu.cmn.id,
                      in_port_ptr->common.bufs_ptr[0].data_ptr,
                      in_port_ptr->common.bufs_ptr[0].actual_data_len,
                      in_port_ptr->common.bufs_ptr[0].max_data_len,
                      in_port_ptr->common.flags.buf_origin,
                      data_dropped);
#endif

         bool_t has_a_held_ext_in_buffer = (GEN_TOPO_BUF_ORIGIN_EXT_BUF & in_port_ptr->common.flags.buf_origin) &&
                                           in_port_ptr->common.bufs_ptr[0].data_ptr;

         if (0 == in_port_ptr->common.bufs_ptr[0].actual_data_len)
         {
            /** else it means topo buffer was assigned due to an underrun, return the buffer so that external buffer
             * can be reused for the next signal trigger.*/
            in_port_ptr->common.flags.force_return_buf = TRUE;
            gen_topo_check_return_one_buf_mgr_buf(&me_ptr->topo,
                                                  &in_port_ptr->common,
                                                  in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                                  in_port_ptr->gu.cmn.id);

            in_port_ptr->common.bufs_ptr[0].data_ptr     = NULL;
            in_port_ptr->common.bufs_ptr[0].max_data_len = 0;
            in_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_INVALID;
            gen_topo_set_all_bufs_len_to_zero(&in_port_ptr->common);
         }
         else // if port has data replace it with a topo buffer
         {
            if (GEN_TOPO_BUF_ORIGIN_BUF_MGR != in_port_ptr->common.flags.buf_origin)
            {
               // replace with a topo buffer
               thin_topo_replace_with_topo_buffer(me_ptr, module_ptr, &in_port_ptr->common, in_port_ptr->gu.cmn.id);
            }
         }

         // free external input if it was held for pass thru
         gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)in_port_ptr->gu.ext_in_port_ptr;
         if (ext_in_port_ptr && has_a_held_ext_in_buffer)
         {
#ifdef VERBOSE_DEBUGGING
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_LOW_PRIO,
                         "Module 0x%lX: Input port id 0x%lx, Freeing the held/pass thru external input buffer 0x%lx "
                         "max_len %lu",
                         module_ptr->gu.module_instance_id,
                         in_port_ptr->gu.cmn.id,
                         ext_in_port_ptr->buf.data_ptr,
                         ext_in_port_ptr->buf.max_data_len);
#endif

            gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
         }
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         uint32_t data_dropped = 0;
         if (out_port_ptr->common.bufs_ptr[0].actual_data_len && force_free_buffers_with_data)
         {
            data_dropped                                     = out_port_ptr->common.bufs_ptr[0].actual_data_len;
            out_port_ptr->common.bufs_ptr[0].actual_data_len = 0;

            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module 0x%lX: Output port id 0x%lx, dropping data data_dropped=%lu, unexpected",
                         module_ptr->gu.module_instance_id,
                         out_port_ptr->gu.cmn.id,
                         data_dropped);

            // crashes only for sim
            spf_svc_crash();
         }
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "Module 0x%lX: Output port id 0x%lx, Freeing output port buffer 0x%lx len (%lu of %lu) origin "
                      "%lu, data_dropped=%lu",
                      module_ptr->gu.module_instance_id,
                      out_port_ptr->gu.cmn.id,
                      out_port_ptr->common.bufs_ptr[0].data_ptr,
                      out_port_ptr->common.bufs_ptr[0].actual_data_len,
                      out_port_ptr->common.bufs_ptr[0].max_data_len,
                      out_port_ptr->common.flags.buf_origin,
                      data_dropped);
#endif

         if (0 == out_port_ptr->common.bufs_ptr[0].actual_data_len)
         {
            out_port_ptr->common.flags.force_return_buf = TRUE;
            gen_topo_check_return_one_buf_mgr_buf(&me_ptr->topo,
                                                  &out_port_ptr->common,
                                                  out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                                  out_port_ptr->gu.cmn.id);

            out_port_ptr->common.bufs_ptr[0].data_ptr     = NULL;
            out_port_ptr->common.bufs_ptr[0].max_data_len = 0;
            out_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_INVALID;
            gen_topo_set_all_bufs_len_to_zero(&out_port_ptr->common);
         }
         else // if port has data replace it with a topo buffer
         {
            if (GEN_TOPO_BUF_ORIGIN_BUF_MGR != out_port_ptr->common.flags.buf_origin)
            {
               // replace with a topo buffer
               thin_topo_replace_with_topo_buffer(me_ptr, module_ptr, &out_port_ptr->common, out_port_ptr->gu.cmn.id);
            }
         }
      }
   }

   me_ptr->topo.proc_context.process_info.atleast_one_inp_holds_ext_buffer = FALSE;

   return AR_EOK;
}

GEN_CNTR_STATIC ar_result_t gen_cntr_prepare_remaining_ext_inputs_before_switch(gen_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      gen_topo_input_port_t  *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

      // check if there data in the external input, it means port is already setup else its needs to be setup by gen
      // topo.
      if (in_port_ptr->common.bufs_ptr[0].actual_data_len)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "Module 0x%lX: External Input port id 0x%lx buffer 0x%lx lens (%lu of %lu) setup is done, skip "
                      "setup now.",
                      in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      in_port_ptr->gu.cmn.id,
                      ext_in_port_ptr->buf.data_ptr,
                      ext_in_port_ptr->buf.actual_data_len,
                      ext_in_port_ptr->buf.max_data_len);
#endif

         // required for gen topo to indicate port is ready to process.
         ext_in_port_ptr->flags.ready_to_go = TRUE;
         continue;
      }

      // if thin topo exited at/before setting up ext inputs, it needs to setup remaining ports.
      if (me_ptr->topo.thin_topo_ptr->state <= THIN_TOPO_EXITED_AT_EXT_IN_BUFFER_SETUP)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "Module 0x%lX: External Input port id 0x%lx buffer needs to be setup by gen topo.",
                      in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      in_port_ptr->gu.cmn.id);
#endif

         // poll and repare external input buffer for this port
         gen_cntr_st_prepare_input_buffers_per_port(me_ptr, ext_in_port_ptr);
      }
   }

   return result;
}

/** At any point checks if there is a need to exit thin topo and continue processing gen topo, because of some non
 * steady state events */
ar_result_t gen_cntr_switch_from_thin_topo_to_gen_topo_during_process(gen_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   gen_topo_t *topo_ptr = &me_ptr->topo;

   GEN_CNTR_MSG(topo_ptr->gu.log_id,
                DBG_HIGH_PRIO,
                "Switching to Gen Topo in process context exit_state:%lu exit_flags:0x%lX",
                topo_ptr->thin_topo_ptr->state,
                me_ptr->topo.exit_flags.word);

   topo_ptr->proc_context.curr_trigger = GEN_TOPO_SIGNAL_TRIGGER;

   // continues processing from this module in

   // free any port buffers without data, and replace with topo buffers if it has data
   // if there is a port buffer with data, it will be retained if its a topo buffer.
   // If its external buffer propagated in thin topo, it must be replaced with topo buffer.
   // Note that for that one interrupt cycle it might result in an extra copy from topo -> ext buffer,
   TRY(result, thin_topo_return_assigned_port_buffers(me_ptr, FALSE /* flag: need to drop data*/));

   // Handle ext port setup,
   // 1. check if external ports/buffers need to setup as per gen topo requirements
   // 2. some scratch variables and proc info variables required for gen topo are updated here

   // we might have exited after partially setting up external inputs, need to continue here.
   TRY(result, gen_cntr_prepare_remaining_ext_inputs_before_switch(me_ptr));

   // if thin topo exited at/before setting up ext inputs, it needs to setup remaining output ports as well.
   if (topo_ptr->thin_topo_ptr->state <= THIN_TOPO_EXITED_AT_EXT_IN_BUFFER_SETUP)
   {
      gen_cntr_st_prepare_output_buffers(me_ptr);
   }

   // todo: i think data can be stalled only at inputs ? if module raised any events input wil not be consumed and
   // output is not generated. hence we wont have a case where data is stalled at output seems like.
   // i think if module generated any ext output data, it would have been sent downstream during its post process, and
   // in gen topo post process container will not see any data remaning to be post processed, and skips the port.

   result = gen_cntr_data_process_frames(me_ptr);

   topo_ptr->thin_topo_ptr->state = THIN_TOPO_SWITCHED_TO_GEN_TOPO;

   // Imp: reset data structures related to thin topo after process is done, to avoid delay in process context.

   // destory any unused buffers in the topo buf manager list, which might have been allocated in thin topo but not
   // necessary for gen topo any more.
   topo_buf_manager_destroy_all_unused_buffers(topo_ptr, TRUE);

   topo_ptr->thin_topo_ptr->gen_topo_proc_next_module_list_ptr = NULL;

   cu_set_handler_for_bit_mask(&me_ptr->cu, GEN_CNTR_TIMER_BIT_MASK, gen_cntr_signal_trigger);

   // reset thin topo exit flags if required
   thin_topo_reset_handle(topo_ptr, FALSE);

   CATCH(result, GEN_CNTR_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

/** at the end of gen topo handling check if the context can be switch to Thin topo. From the next interrupt processing
 * will be done in with Thin topo */
ar_result_t gen_cntr_switch_to_thin_topo_util_(gen_cntr_t *me_ptr)
{
   ar_result_t result   = AR_EOK;
   gen_topo_t *topo_ptr = &me_ptr->topo;
   INIT_EXCEPTION_HANDLING

   GEN_CNTR_MSG(topo_ptr->gu.log_id,
                DBG_HIGH_PRIO,
                "Switching to Thin Topo exit_flags:0x%lX",
                me_ptr->topo.exit_flags.word);

   // reassign buffers as needed by the thin topo.
   TRY(result, thin_topo_update_process_info(me_ptr));

   topo_ptr->thin_topo_ptr->state = THIN_TOPO_ENTERED;

   cu_set_handler_for_bit_mask(&me_ptr->cu, GEN_CNTR_TIMER_BIT_MASK, thin_topo_signal_trigger_handler);

   CATCH(result, GEN_CNTR_MSG_PREFIX, topo_ptr->gu.log_id)
   {
      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_HIGH_PRIO,
                   "Failed switching to Thin Topo exit_flags:0x%lX result 0x%lx",
                   me_ptr->topo.exit_flags.word,
                   result);
   }

   return result;
}

/** Iterate backwards from the given input till the external input port(nblc_start_ptr)*/
GEN_TOPO_STATIC gen_cntr_ext_out_port_t *thin_topo_is_inplace_nblc_from_cur_out_to_ext_out(
   uint32_t                log_id,
   gen_topo_output_port_t *cur_out_port_ptr)
{
   /** this function applies only in the context of inputs present in the NBLC of the external inputs*/
   if (!cur_out_port_ptr || !cur_out_port_ptr->nblc_end_ptr || !cur_out_port_ptr->nblc_end_ptr->gu.ext_out_port_ptr)
   {
      return NULL;
   }

   /** If current output iterator reached external output terminate the loop */
   gen_topo_output_port_t *nblc_end_port_ptr = cur_out_port_ptr->nblc_end_ptr;
   while (nblc_end_port_ptr != cur_out_port_ptr)
   {
      /** unexpectedly reached external output, shouldnt have entered the while loop*/
      if (cur_out_port_ptr->gu.ext_out_port_ptr)
      {
         GEN_CNTR_MSG(log_id,
                      DBG_ERROR_PRIO,
                      " Potential issue with NBLC assignment, reached unexpected external output "
                      "(MIID,PID)(0x%lx,0x%lx)",
                      cur_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      cur_out_port_ptr->gu.cmn.id);
         return NULL;
      }

      /** check if the next module is inplace*/
      gen_topo_module_t *next_module_ptr = (gen_topo_module_t *)cur_out_port_ptr->gu.conn_in_port_ptr->cmn.module_ptr;
      if (!gen_topo_is_inplace_or_disabled_siso(next_module_ptr))
      {
         return NULL;
      }

      if (next_module_ptr->gu.output_port_list_ptr)
      {
         gen_topo_output_port_t *next_out_port_ptr =
            (gen_topo_output_port_t *)next_module_ptr->gu.output_port_list_ptr->op_port_ptr;

         /** update iterator and continue the loop*/
         cur_out_port_ptr = next_out_port_ptr;
         continue;
      }
      else
      {
         /** unexpected, didnt reach the external output portential issue with NBLC assignment */
         GEN_CNTR_MSG(log_id,
                      DBG_ERROR_PRIO,
                      " Potential issue with NBLC assignment, reached unexpected external output "
                      "(MIID,PID)(0x%lx,0x%lx)",
                      cur_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      cur_out_port_ptr->gu.cmn.id);
         return NULL;
      }
   }

   return (gen_cntr_ext_out_port_t *)nblc_end_port_ptr->gu.ext_out_port_ptr;
}

/** Must be called whenever there is change in,
 *    1. If there is change in graph state, updates thin topo's process module list.
 *    2. Module process state event changes.
 *    3. Threshold and media format changes.
 *    4. dynamic inplace changes, since buffer assignement will change.
 *    5. Upstream frame length change is received by the external input ports.
 *    6. A module proc state is enabled
 *    7. when there is a switch from Gen/PST topo to Thin topo, to assign static buffers
 */
ar_result_t thin_topo_update_process_info(gen_cntr_t *me_ptr)
{
   ar_result_t result   = AR_EOK;
   gen_topo_t *topo_ptr = &me_ptr->topo;
   INIT_EXCEPTION_HANDLING

   if (NULL == topo_ptr->started_sorted_module_list_ptr)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Started sorted order is not updated yet, unable to assign topo buffers");
      return AR_EFAILED;
   }

   // Ideally there will not be any partial data when there is a switch into thin topo
   TRY(result, thin_topo_return_assigned_port_buffers(me_ptr, TRUE /* flag: need to drop data*/));

   thin_topo_reset_handle(topo_ptr, FALSE);

   topo_buf_manager_reset_static_assignment_list(topo_ptr);

   gu_module_list_t *module_list_ptr = topo_ptr->started_sorted_module_list_ptr;

   // release buffers if not required
   for (; (NULL != module_list_ptr); LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

      // check if the module satisfies default trigger policy
      bool_t is_module_active = thin_topo_is_module_active(module_ptr, FALSE /**need_to_ignore_state=FALSE */);
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Module 0x%lX: is_module_active:%lu ",
               module_ptr->gu.module_instance_id,
               is_module_active);

      if (FALSE == is_module_active)
      {
         continue;
      }

      // add to list of process list
      TRY(result,
          spf_list_insert_tail((spf_list_node_t **)&topo_ptr->thin_topo_ptr->active_module_list_ptr,
                               module_ptr,
                               topo_ptr->heap_id,
                               TRUE));

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t   *in_port_ptr                = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
         gen_cntr_ext_in_port_t  *nblc_start_ext_in_port_ptr = NULL;
         gen_cntr_ext_out_port_t *nblc_end_ext_out_port_ptr  = NULL;

#ifdef VERBOSE_DEBUGGING
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "Module 0x%lX: Input port id 0x%lx, port_state %lu, check and assign topo buffers",
                  module_ptr->gu.module_instance_id,
                  in_port_ptr->gu.cmn.id,
                  in_port_ptr->common.state);
#endif
         // reset pass_thru_upstream_buffer flag in ext in ports
         if (in_port_ptr->gu.ext_in_port_ptr)
         {
            thin_topo_reset_ext_in_pass_thru_upstream_buf_flag(me_ptr,
                                                               (gen_cntr_ext_in_port_t *)
                                                                  in_port_ptr->gu.ext_in_port_ptr,
                                                               in_port_ptr);
         }

         /** Free any exisiting buffer and reset the buffer states */
         in_port_ptr->common.flags.force_return_buf = TRUE;
         gen_topo_check_return_one_buf_mgr_buf(topo_ptr,
                                               &in_port_ptr->common,
                                               in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                               in_port_ptr->gu.cmn.id);

         // reset the ext buffer propagation flag
         // check if ext output buffer can be propagated first, else check if ext in can be propagated.
         in_port_ptr->common.flags.thin_topo_can_assign_ext_in_buffer  = FALSE;
         in_port_ptr->common.flags.thin_topo_can_assign_ext_out_buffer = FALSE;
         gen_topo_output_port_t *prev_out_port_ptr = (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr;
         if (prev_out_port_ptr &&
             (NULL != (nblc_end_ext_out_port_ptr =
                          thin_topo_is_inplace_nblc_from_cur_out_to_ext_out(topo_ptr->gu.log_id, prev_out_port_ptr))))
         {
            in_port_ptr->common.flags.thin_topo_can_assign_ext_out_buffer = TRUE;

            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     " Module 0x%lX: Input port id 0x%lx, not assigning topo buffer because ext output port can be "
                     "propagated.",
                     module_ptr->gu.module_instance_id,
                     in_port_ptr->gu.cmn.id);

            thin_topo_reset_topo_buf_info(&in_port_ptr->common);
         }
         else if ((NULL != (nblc_start_ext_in_port_ptr =
                               thin_topo_is_inplace_nblc_from_cur_in_to_ext_in(topo_ptr->gu.log_id, in_port_ptr))) &&
                  nblc_start_ext_in_port_ptr->flags.pass_thru_upstream_buffer)
         {
            in_port_ptr->common.flags.thin_topo_can_assign_ext_in_buffer = TRUE;

            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "Module 0x%lX: Input port id 0x%lx, not assigning topo buffer because (US,DS)(%lu,%lu) "
                     "frame size are same.",
                     module_ptr->gu.module_instance_id,
                     in_port_ptr->gu.cmn.id,
                     nblc_start_ext_in_port_ptr->cu.upstream_frame_len.frame_len_us,
                     me_ptr->cu.cntr_frame_len.frame_len_us);

            thin_topo_reset_topo_buf_info(&in_port_ptr->common);
         }
         else if (TOPO_PORT_STATE_STARTED == in_port_ptr->common.state) /** Assign topo buffer */
         {
            result =
               gen_topo_check_get_in_buf_from_buf_mgr(topo_ptr,
                                                      in_port_ptr,
                                                      (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr);
            if (AR_DID_FAIL(result))
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        " Module 0x%lX: Input port id 0x%lx, error getting buffer",
                        module_ptr->gu.module_instance_id,
                        in_port_ptr->gu.cmn.id);
            }
         }

#ifdef THIN_TOPO_BUF_ASSIGNMENT_DEBUG
         THIN_TOPO_PRINT_PORT_BUF_INFO(module_ptr->gu.module_instance_id,
                                       in_port_ptr->gu.cmn.id,
                                       in_port_ptr->common,
                                       topo_ptr->proc_context.proc_result,
                                       "input",
                                       "buffer assignment");
#endif
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t  *out_port_ptr     = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
         gen_topo_input_port_t   *next_in_port_ptr = (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr;
         gen_cntr_ext_out_port_t *nblc_end_ext_out_port_ptr = NULL;

#ifdef VERBOSE_DEBUGGING
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "Module 0x%lX: Output port id 0x%lx, port_state %lu, check and assign topo buffers",
                  module_ptr->gu.module_instance_id,
                  out_port_ptr->gu.cmn.id,
                  out_port_ptr->common.state);
#endif

         /* return existing topo buffer*/
         out_port_ptr->common.flags.force_return_buf = TRUE;
         gen_topo_check_return_one_buf_mgr_buf(topo_ptr,
                                               &out_port_ptr->common,
                                               out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                               out_port_ptr->gu.cmn.id);

         // check if cur module is inplace
         gen_topo_input_port_t *cur_mod_inplace_in_port_ptr = NULL;
         if (gen_topo_is_inplace_or_disabled_siso(module_ptr))
         {
            cur_mod_inplace_in_port_ptr = (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;
         }

         out_port_ptr->common.flags.thin_topo_can_assign_ext_in_buffer  = FALSE;
         out_port_ptr->common.flags.thin_topo_can_assign_ext_out_buffer = FALSE;
         if (NULL == next_in_port_ptr)
         {
            // if ext output port can assign ext buffer to cur output port
            out_port_ptr->common.flags.thin_topo_can_assign_ext_out_buffer = TRUE;
            thin_topo_reset_topo_buf_info(&out_port_ptr->common);
         }
         if (NULL != (nblc_end_ext_out_port_ptr =
                         thin_topo_is_inplace_nblc_from_cur_out_to_ext_out(topo_ptr->gu.log_id, out_port_ptr)))
         {
            //  can assign ext buffer to the cur output if NBLC end is ext output
            out_port_ptr->common.flags.thin_topo_can_assign_ext_out_buffer = TRUE;

            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     " Module 0x%lX: Output port id 0x%lx, not assigning topo buffer because ext output port can "
                     "be propagated.",
                     module_ptr->gu.module_instance_id,
                     out_port_ptr->gu.cmn.id);

            thin_topo_reset_topo_buf_info(&out_port_ptr->common);
         }
         else if (cur_mod_inplace_in_port_ptr &&
                  cur_mod_inplace_in_port_ptr->common.flags.thin_topo_can_assign_ext_in_buffer)
         {
            // can assign ext buffer to cur output if cur module is inplace and cur module's input can reuse ext in
            // buffer
            out_port_ptr->common.flags.thin_topo_can_assign_ext_in_buffer = TRUE;

            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     " Module 0x%lX: Output port id 0x%lx, not assigning topo buffer because ext in buffer is "
                     "propagated",
                     module_ptr->gu.module_instance_id,
                     out_port_ptr->gu.cmn.id);

            thin_topo_reset_topo_buf_info(&out_port_ptr->common);
         }
         else if (TOPO_PORT_STATE_STARTED == out_port_ptr->common.state) /* assign topo buffer */
         {
            result = gen_topo_check_get_out_buf_from_buf_mgr(topo_ptr, module_ptr, out_port_ptr);
            if (AR_DID_FAIL(result))
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        " Module 0x%lX: Output port id 0x%lx, error getting buffer",
                        module_ptr->gu.module_instance_id,
                        out_port_ptr->gu.cmn.id);
            }
         }

#ifdef THIN_TOPO_BUF_ASSIGNMENT_DEBUG
         THIN_TOPO_PRINT_PORT_BUF_INFO(module_ptr->gu.module_instance_id,
                                       out_port_ptr->gu.cmn.id,
                                       out_port_ptr->common,
                                       topo_ptr->proc_context.proc_result,
                                       "output",
                                       "buffer assignment cmd");
#endif
      }

      // ideally after process this particular module will not have any data in inputs, so can be reused for the next
      // modules output. Note that next module input will use the current modules output always.
      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         if (GEN_TOPO_BUF_ORIGIN_BUF_MGR == in_port_ptr->common.flags.buf_origin)
         {
#ifdef VERBOSE_DEBUGGING
            GEN_CNTR_MSG(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         "Module 0x%lX: port id 0x%lx, return buf 0x%p statically assigned free list",
                         module_ptr->gu.module_instance_id,
                         in_port_ptr->gu.cmn.id,
                         in_port_ptr->common.bufs_ptr[0].data_ptr);
#endif
            // add input buffer to the temp free list for the downstream modules to use.
            topo_buf_manager_static_buf_assign_add_buf_to_temp_free_list(topo_ptr,
                                                                         in_port_ptr->common.bufs_ptr[0].data_ptr);
         }
      }
   }

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = topo_ptr->gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      gen_topo_input_port_t  *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

      if (TOPO_PORT_STATE_STARTED == in_port_ptr->common.state)
      {
         // add to list of process list
         TRY(result,
             spf_list_insert_tail((spf_list_node_t **)&topo_ptr->thin_topo_ptr->active_ext_in_list_ptr,
                                  ext_in_port_ptr,
                                  topo_ptr->heap_id,
                                  TRUE));
#ifdef VERBOSE_DEBUGGING
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "Module 0x%lX: Input port id 0x%lx, adding to thin topo's active ext inport list.",
                  in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  in_port_ptr->gu.cmn.id);
#endif
      }
      else
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "Module 0x%lX: External input port id 0x%lx, is not active for thin topo process.",
                  in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  in_port_ptr->gu.cmn.id);
      }
   }

   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = topo_ptr->gu.ext_out_port_list_ptr; ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      gen_topo_output_port_t  *out_port_ptr     = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

      if (TOPO_PORT_STATE_STARTED == out_port_ptr->common.state)
      {
         // add to list of process list
         TRY(result,
             spf_list_insert_tail((spf_list_node_t **)&topo_ptr->thin_topo_ptr->active_ext_out_list_ptr,
                                  ext_out_port_ptr,
                                  topo_ptr->heap_id,
                                  TRUE));
#ifdef VERBOSE_DEBUGGING
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "Module 0x%lX: External output port id 0x%lx, adding to thin topo's active ext outport list.",
                  out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  out_port_ptr->gu.cmn.id);
#endif
      }
      else
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "Module 0x%lX: External output port id 0x%lx, is not active for thin topo process.",
                  out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  out_port_ptr->gu.cmn.id);
      }
   }

   // destory any unused buffers in the topo buf manager list
   topo_buf_manager_destroy_all_unused_buffers(topo_ptr, TRUE);

   topo_buf_manager_reset_static_assignment_list(topo_ptr);

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
      thin_topo_reset_handle(topo_ptr, TRUE);
   }

   return result;
}

// if currently processing with thin topo, check if anything needs to be updated in thin topo or needs to switch to
// gen topo
ar_result_t thin_topo_handle_fwk_events(gen_cntr_t                 *me_ptr,
                                        gen_topo_capi_event_flag_t *capi_event_flag_ptr,
                                        cu_event_flags_t           *fwk_event_flag_ptr)
{
   gen_topo_t *topo_ptr = &me_ptr->topo;
   ar_result_t result   = AR_EOK;
   INIT_EXCEPTION_HANDLING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "Handling events, fwk events 0x%lX, capi events 0x%lX state %ld",
                fwk_event_flag_ptr->word,
                capi_event_flag_ptr->word,
                me_ptr->topo.thin_topo_ptr ? me_ptr->topo.thin_topo_ptr->state : 0xFFFF);

   if (check_if_cntr_is_processing_with_thin_topo(topo_ptr)) // check if currently thin topo
   {
      // check if thin topo needs to be exited, if graph is getting stopped started module list will be NULL
      // hence need to be switched to gen topo as well.
      if (check_if_needed_to_exit_thin_topo(topo_ptr) || (NULL == topo_ptr->started_sorted_module_list_ptr))
      {
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Switching to Gen Topo in the event handling context exit_flags:0x%lX",
                      me_ptr->topo.exit_flags.word);

         // there could be EOS metadata due to a port stop cmd, hence we cannot drop data. otherwise
         // there wouldn't be any partial data left from thin topo.
         bool_t drop_pending_data = (NULL == topo_ptr->started_sorted_module_list_ptr);
         TRY(result, thin_topo_return_assigned_port_buffers(me_ptr, drop_pending_data));

         cu_set_handler_for_bit_mask(&me_ptr->cu, GEN_CNTR_TIMER_BIT_MASK, gen_cntr_signal_trigger);
         thin_topo_reset_handle(topo_ptr, FALSE);

         topo_ptr->thin_topo_ptr->state = THIN_TOPO_SWITCHED_TO_GEN_TOPO;

         // destory any unused buffers in the topo buf manager list, which might have been allocated in thin topo but
         // not necessary for gen topo any more.
         topo_buf_manager_destroy_all_unused_buffers(topo_ptr, TRUE);
      }
      // check if thin topo process info needs to be updated because of an event/cmd
      else if (fwk_event_flag_ptr->port_state_change || fwk_event_flag_ptr->sg_state_change ||
               fwk_event_flag_ptr->frame_len_change || fwk_event_flag_ptr->upstream_frame_len_change ||
               capi_event_flag_ptr->process_state || capi_event_flag_ptr->dynamic_inplace_change ||
               capi_event_flag_ptr->media_fmt_event || capi_event_flag_ptr->port_thresh)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "Updating thin topo due to events ");
#endif
         return thin_topo_update_process_info(me_ptr);
      }
   }
   else // currently in gen topo, no need to switch here gen cntr will switch at the end
   {
// nothing to do
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_HIGH_PRIO,
                   "Cntr not processing with thin topo so nothing to do in event handling context exit_flags:0x%lx",
                   me_ptr->topo.exit_flags.word);

#endif
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

gu_module_list_t *thin_topo_get_gen_topo_module_list_ptr(gen_topo_t *topo_ptr, gu_module_t *gu_module_ptr)
{
   if (NULL == gu_module_ptr)
   {
      return NULL;
   }

   // search and get the list pointer of the module from gen topo list.
   gu_module_list_t *temp_module_list_ptr = (gu_module_list_t *)topo_ptr->started_sorted_module_list_ptr;
   while (temp_module_list_ptr)
   {
      if (temp_module_list_ptr->module_ptr == gu_module_ptr)
      {
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      "Found MID:0x%lx list ptr 0x%lx from gen topo list",
                      gu_module_ptr->module_instance_id,
                      temp_module_list_ptr);
         return temp_module_list_ptr;
      }

      LIST_ADVANCE(temp_module_list_ptr);
   }

   GEN_CNTR_MSG(topo_ptr->gu.log_id,
                DBG_ERROR_PRIO,
                "Could not find MID:0x%lx list ptr from gen topo list",
                gu_module_ptr->module_instance_id);

   return NULL;
}
