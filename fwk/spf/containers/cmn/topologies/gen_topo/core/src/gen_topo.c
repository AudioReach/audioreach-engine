/**
 * \file gen_topo.c
 *
 * \brief
 *
 *     Basic topology implementation.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"
#include "gen_topo_capi.h"
#include "gen_topo_buf_mgr.h"
#include "gen_topo_prof.h"
#include "gen_topo_ctrl_port.h"
#include "thin_topo_inline.h"

// clang-format off
static const topo_cu_vtable_t gen_topo_cu_vtable =
{
   .propagate_media_fmt                 = gen_topo_propagate_media_fmt,
   .operate_on_modules                  = gen_topo_operate_on_modules,
   .operate_on_int_in_port              = gen_topo_operate_on_int_in_port,
   .operate_on_int_out_port             = gen_topo_operate_on_int_out_port,
   .operate_on_int_ctrl_port            = gen_topo_operate_on_int_ctrl_port,

   .ctrl_port_operation                 = gen_topo_set_ctrl_port_operation,

   .get_sg_state                        = gen_topo_get_sg_state,

   .destroy_all_metadata                = gen_topo_destroy_all_metadata,

   .propagate_boundary_modules_port_state  =  gen_topo_propagate_boundary_modules_port_state,

   .add_path_delay_info                 = gen_topo_add_path_delay_info,
   .update_path_delays                  = gen_topo_update_path_delays,
   .remove_path_delay_info              = gen_topo_remove_path_delay_info,
   .query_module_delay                  = gen_topo_query_module_delay,

   .propagate_port_property             = gen_topo_propagate_port_props,
   .propagate_port_property_forwards    = gen_topo_propagate_port_property_forwards,
   .propagate_port_property_backwards   = gen_topo_propagate_port_property_backwards,

   .get_port_property                   = gen_topo_get_port_property,
   .set_port_property                   = gen_topo_set_port_property,
   .set_param                           = gen_topo_set_param,
   .get_prof_info                       = gen_topo_get_prof_info,
   .get_port_threshold                  = gen_topo_get_port_threshold,
   .rtm_dump_data_port_media_fmt        = gen_topo_rtm_dump_data_port_mf_for_all_ports,
   .check_update_started_sorted_module_list   = gen_topo_check_update_started_sorted_module_list,
   .set_global_sh_mem_msg                     = gen_topo_set_global_sh_mem_msg,
};
// clang-format on

extern const topo_cu_island_vtable_t gen_topo_cu_island_vtable;
extern const gen_topo_vtable_t       gen_topo_vtable;

/* =======================================================================
Public Function Definitions
========================================================================== */
ar_result_t gen_topo_init_topo(gen_topo_t *topo_ptr, gen_topo_init_data_t *init_data_ptr, POSAL_HEAP_ID heap_id)
{
   init_data_ptr->topo_cu_vtbl_ptr        = &gen_topo_cu_vtable;
   init_data_ptr->topo_cu_island_vtbl_ptr = &gen_topo_cu_island_vtable;
   topo_ptr->topo_to_cntr_vtable_ptr      = init_data_ptr->topo_to_cntr_vtble_ptr;

   // Other callers such as spl_topo need to overwrite with their own function
   // table after calling gen_topo_init_topo.
   topo_ptr->gen_topo_vtable_ptr = &gen_topo_vtable;
   topo_ptr->heap_id             = heap_id;

   topo_buf_manager_init(topo_ptr);

   // by default this flag is set, it will be cleared port carries non-pcm media format.
   topo_ptr->flags.simple_threshold_propagation_enabled = TRUE;

   return AR_EOK;
}

ar_result_t gen_topo_create_input_port(gen_topo_t *topo, gen_topo_input_port_t *input_port_ptr)
{
   return AR_EOK;
}
ar_result_t gen_topo_create_output_port(gen_topo_t *topo, gen_topo_output_port_t *output_port_ptr)
{
   return AR_EOK;
}

uint32_t gen_topo_get_bufs_num_from_med_fmt(topo_media_fmt_t *med_fmt_ptr)
{
   uint32_t bufs_num = 1;
   if (SPF_IS_PACKETIZED_OR_PCM(med_fmt_ptr->data_format))
   {
      if (TU_IS_ANY_DEINTERLEAVED_UNPACKED(med_fmt_ptr->pcm.interleaving))
      {
         bufs_num = med_fmt_ptr->pcm.num_channels;
      }
   }
   else if (SPF_DEINTERLEAVED_RAW_COMPRESSED == med_fmt_ptr->data_format)
   {
      bufs_num = med_fmt_ptr->deint_raw.bufs_num;
   }
   // create at least one buf
   return ((0 == bufs_num) ? 1 : bufs_num);
}

ar_result_t gen_topo_initialize_bufs_sdata(gen_topo_t *            topo_ptr,
                                           gen_topo_common_port_t *cmn_port_ptr,
                                           uint32_t                miid,
                                           uint32_t                port_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   cmn_port_ptr->sdata.flags.stream_data_version = CAPI_STREAM_V2;

   uint32_t new_bufs_num = gen_topo_get_bufs_num_from_med_fmt(cmn_port_ptr->media_fmt_ptr);

   if (cmn_port_ptr->sdata.bufs_num != new_bufs_num)
   {
      cmn_port_ptr->sdata.bufs_num = new_bufs_num;

      MFREE_NULLIFY(cmn_port_ptr->bufs_ptr);

      if (0 != cmn_port_ptr->sdata.bufs_num)
      {
         MALLOC_MEMSET(cmn_port_ptr->bufs_ptr,
                       topo_buf_t,
                       cmn_port_ptr->sdata.bufs_num * sizeof(topo_buf_t),
                       topo_ptr->heap_id,
                       result);
      }
   }

   // max_buf_len may be assigned after assigning bufs_num.
   cmn_port_ptr->max_buf_len_per_buf = topo_div_num(cmn_port_ptr->max_buf_len, cmn_port_ptr->sdata.bufs_num);

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "Module 0x%lX, port_id 0x%lx, bufs_num %lu, max_buf_len_per_buf %lu, bufs_ptr 0x%p",
            miid,
            port_id,
            cmn_port_ptr->sdata.bufs_num,
            cmn_port_ptr->max_buf_len_per_buf,
            cmn_port_ptr->bufs_ptr);
#endif
   // by default assign the bufs_ptr in the port struct as the sdata buf.
   // assign here as num_proc_loops might have changed.
   cmn_port_ptr->sdata.buf_ptr = (capi_buf_t *)cmn_port_ptr->bufs_ptr;

   if (AR_DID_FAIL(result))
   {
      TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "failure in gen_topo_initialize_bufs_sdata");
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }
   return result;
}

/**
 * reset_capi_dependent_dont_destroy - remove only the things that depend on CAPI
 *
 */
ar_result_t gen_topo_destroy_cmn_port(gen_topo_t *            me_ptr,
                                      gen_topo_common_port_t *cmn_port_ptr,
                                      gu_cmn_port_t *         gu_cmn_port_ptr,
                                      bool_t                  reset_capi_dependent_dont_destroy)
{
   ar_result_t result = AR_EOK;
   SPF_MANAGE_CRITICAL_SECTION

   if (cmn_port_ptr->sdata.metadata_list_ptr)
   {
      bool_t IS_DROPPED_TRUE = TRUE;
      TOPO_MSG(me_ptr->gu.log_id,
               DBG_MED_PRIO,
               "Destroying metadata for port id %ld miid 0x%lx in destroy common port.",
               gu_cmn_port_ptr->id,
               gu_cmn_port_ptr->module_ptr->module_instance_id);

      result |= gen_topo_destroy_all_metadata(me_ptr->gu.log_id,
                                              (void *)gu_cmn_port_ptr->module_ptr,
                                              &(cmn_port_ptr->sdata.metadata_list_ptr),
                                              IS_DROPPED_TRUE);
   }

   // If a port that has threshold is going away, then container needs to re-calculate LCM threshold.
   if (cmn_port_ptr->flags.port_has_threshold)
   {
      GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(me_ptr, port_thresh);
   }

   SPF_CRITICAL_SECTION_START(&me_ptr->gu);

   if (cmn_port_ptr->bufs_ptr)
   {
      if (cmn_port_ptr->bufs_ptr[0].actual_data_len)
      {
         TOPO_MSG(me_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "Module 0x%lX, port_id 0x%lx, Dropping data %lu in destroy common port.",
                  gu_cmn_port_ptr->module_ptr->module_instance_id,
                  gu_cmn_port_ptr->id,
                  cmn_port_ptr->bufs_ptr[0].actual_data_len);
      }
      gen_topo_set_all_bufs_len_to_zero(cmn_port_ptr);
      cmn_port_ptr->flags.force_return_buf = TRUE;
      gen_topo_check_return_one_buf_mgr_buf(me_ptr,
                                            cmn_port_ptr,
                                            gu_cmn_port_ptr->module_ptr->module_instance_id,
                                            gu_cmn_port_ptr->id);
   }

   // reset the media format ptr.
   tu_set_media_fmt(&me_ptr->mf_utils, &cmn_port_ptr->media_fmt_ptr, NULL, me_ptr->heap_id);

   SPF_CRITICAL_SECTION_END(&me_ptr->gu);

   if (!reset_capi_dependent_dont_destroy)
   {
      gen_topo_destroy_all_delay_path_per_port(me_ptr, (gen_topo_module_t *)gu_cmn_port_ptr->module_ptr, cmn_port_ptr);

      MFREE_NULLIFY(cmn_port_ptr->bufs_ptr);
      cmn_port_ptr->sdata.bufs_num = 0;
      cmn_port_ptr->sdata.buf_ptr  = NULL;
   }

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(me_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "Module 0x%lX, port_id 0x%lx, port destroy done",
            gu_cmn_port_ptr->module_ptr->module_instance_id,
            gu_cmn_port_ptr->id);
#endif

   return result;
}

/**
 * note: graph utility memory is not destroyed by this func.
 *
 * there's no symmetric create_port because part of the symmetric portion happens during create, part during
 * port-thresh
 * event.
 */
ar_result_t gen_topo_destroy_input_port(gen_topo_t *me_ptr, gen_topo_input_port_t *in_port_ptr)
{
   return gen_topo_destroy_cmn_port(me_ptr,
                                    &in_port_ptr->common,
                                    &in_port_ptr->gu.cmn,
                                    FALSE /*reset_capi_dependent_dont_destroy*/);
}

/**
 * note: graph utility memory is not destroyed by this func.
 *
 * there's no symmetric create_port because part of the symmetric portion happens during create, part during
 * port-thresh event.
 */
ar_result_t gen_topo_destroy_output_port(gen_topo_t *me_ptr, gen_topo_output_port_t *out_port_ptr)
{
   return gen_topo_destroy_cmn_port(me_ptr,
                                    &out_port_ptr->common,
                                    &out_port_ptr->gu.cmn,
                                    FALSE /*reset_capi_dependent_dont_destroy*/);
}

// ar_result_t gen_topo_create_ext_in_port(gen_topo_ext_in_port_t *input_port_ptr) { return AR_EOK; }
// ar_result_t gen_topo_create_ext_output_port(gen_topo_ext_out_port_t *output_port_ptr) { return AR_EOK; }
ar_result_t gen_topo_create_input_ports(gen_topo_t *topo_ptr)
{
   return AR_EOK;
}
ar_result_t gen_topo_destroy_input_ports(gen_topo_t *topo_ptr)
{
   return AR_EOK;
}
// ar_result_t gen_topo_create_ext_in_ports(gen_topo_t *topo_ptr) { return AR_EOK; }
// ar_result_t gen_topo_destroy_ext_out_ports(gen_topo_t *topo_ptr) { return AR_EOK; }

ar_result_t gen_topo_query_and_create_capi(gen_topo_t *           topo_ptr,
                                           gen_topo_graph_init_t *graph_init_ptr,
                                           gen_topo_module_t *    module_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   // when creating capi module, send module heap ID instead of topo heap ID
   // (module heap ID is initialized to topo heap ID during graph open -
   //  and modified with Prop ID APM_MODULE_PROP_ID_HEAP_ID when handled)
   TRY(result,
       gen_topo_capi_create_from_amdb(module_ptr,
                                      topo_ptr,
                                      (void *)module_ptr->gu.amdb_handle,
                                      module_ptr->gu.module_heap_id,
                                      graph_init_ptr));

   TRY(result, gen_topo_fmwk_extn_handle_at_init(topo_ptr, module_ptr));
   TRY(result, gen_topo_intf_extn_handle_at_init(topo_ptr, module_ptr));

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

static ar_result_t gen_topo_create_virtual_stub(gen_topo_t *           topo_ptr,
                                                gen_topo_graph_init_t *graph_init_ptr,
                                                gen_topo_module_t *    module_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_HIGH_PRIO,
            "Module 0x%lX is virtually stubbed",
            module_ptr->gu.module_instance_id,
            module_ptr->flags.inplace);

   module_ptr->flags.disabled = TRUE;

   TRY(result, gen_topo_check_create_bypass_module(topo_ptr, module_ptr));

   module_ptr->flags.inplace         = TRUE;
   module_ptr->flags.dynamic_inplace = TRUE;

   // stack size for bypass is not high: graph_init_ptr->max_stack_size = MAX(graph_init_ptr->max_stack_size, 0);
   // port_has_threshold, requires_data_buffering -> default value of 0 works.

   TRY(result,
       __gpr_cmd_register(module_ptr->gu.module_instance_id,
                          graph_init_ptr->gpr_cb_fn,
                          graph_init_ptr->spf_handle_ptr));

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

static ar_result_t gen_topo_create_module(gen_topo_t *           topo_ptr,
                                          gen_topo_graph_init_t *graph_init_ptr,
                                          gen_topo_module_t *    module_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   module_ptr->serial_num           = topo_ptr->module_count++;
   module_ptr->kpps_scale_factor_q4 = UNITY_Q4;

   spf_handle_t *gpr_cb_handle = graph_init_ptr->spf_handle_ptr;

   if (AMDB_INTERFACE_TYPE_STUB == module_ptr->gu.itype)
   {
      TRY(result, gen_topo_create_virtual_stub(topo_ptr, graph_init_ptr, module_ptr));
   }
   else // CAPI & fmwk module
   {
      if (AMDB_MODULE_TYPE_FRAMEWORK != module_ptr->gu.module_type)
      {
         TRY(result, __gpr_cmd_register(module_ptr->gu.module_instance_id, graph_init_ptr->gpr_cb_fn, gpr_cb_handle));
         // In XPAN use cases, during module init, module registers with CPSS, which sends commands to the module
         // through GPR. If we have not registered for GPR first than this operation fails. Hence GPR registration
         // is required first.
         TRY(result, gen_topo_query_and_create_capi(topo_ptr, graph_init_ptr, module_ptr));

         // cannot use pure signal triggered topo
         if ((AR_GUID_OWNER_QC != (module_ptr->gu.module_id & AR_GUID_OWNER_MASK)))
         {
            topo_ptr->flags.cannot_be_pure_signal_triggered = TRUE;

            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_LOW_PRIO,
                     "Non QC module found, Container can never use pure signal triggered topology %u",
                     module_ptr->gu.module_id);
         }
      }

      // below func has common handling for capi module create + also does fwk module creation
      if (topo_ptr->topo_to_cntr_vtable_ptr->create_module)
      {
         TRY(result, topo_ptr->topo_to_cntr_vtable_ptr->create_module(topo_ptr, module_ptr, graph_init_ptr));
      }
   }

   module_ptr->num_proc_loops = 1;

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

ar_result_t gen_topo_init_set_get_data_port_properties(gen_topo_module_t *    module_ptr,
                                                       gen_topo_t *           topo_ptr,
                                                       bool_t                 is_placeholder_replaced,
                                                       gen_topo_graph_init_t *graph_init_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, NULL != module_ptr);
   VERIFY(result, NULL != topo_ptr);
   VERIFY(result, NULL != graph_init_ptr);

   // New ports may be added. need to set it to module.
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *in_port_ptr     = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      uint32_t               threshold_bytes = 0;

      if (!in_port_ptr->common.media_fmt_ptr)
      {
         tu_set_media_fmt(&topo_ptr->mf_utils, &in_port_ptr->common.media_fmt_ptr, NULL, topo_ptr->heap_id);
      }

      if (GU_STATUS_NEW == in_port_ptr->gu.cmn.gu_status || is_placeholder_replaced)
      {
         if (module_ptr->capi_ptr)
         {
            if (GU_STATUS_UPDATED == module_ptr->gu.gu_status)
            {
               // if module is new then gen_topo_intf_extn_data_ports_hdl_at_init takes care of this.
               TRY(result,
                   gen_topo_capi_set_data_port_op(module_ptr,
                                                  INTF_EXTN_DATA_PORT_OPEN,
                                                  &in_port_ptr->common.last_issued_opcode,
                                                  TRUE, /*is_input*/
                                                  in_port_ptr->gu.cmn.index,
                                                  in_port_ptr->gu.cmn.id));
            }

            // A module implementing frame duration extension must always use 1 byte threshold. See comment at threshold
            // event handling.
            if (!module_ptr->flags.need_cntr_frame_dur_extn)
            {
               gen_topo_capi_get_port_thresh(module_ptr->gu.module_instance_id,
                                             topo_ptr->gu.log_id,
                                             module_ptr->capi_ptr,
                                             CAPI_INPUT_PORT,
                                             in_port_ptr->gu.cmn.index,
                                             &threshold_bytes);
            }

            if (threshold_bytes > 1)
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_HIGH_PRIO,
                        "Module 0x%lX with ip thresh is now non-inplace, prev_inplace= %u ",
                        in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                        module_ptr->flags.inplace);
               // Set has threshold flag on the port and also make the module non inplace.
               // If port has threshold then there could be partial data left in the input buf after module-process.
               // But for inplace the buf belongs to output after process. Hence in this case, the inplace buffer may
               // have both input and output data, which results in corruption. To avoid this complication we don't
               // support inplace for port with thresholds. <Same logic applies to the output port context as well.>
               module_ptr->flags.inplace            = FALSE;
               module_ptr->flags.dynamic_inplace    = FALSE;

               in_port_ptr->common.threshold_raised = threshold_bytes;
            }
            else
            {
               in_port_ptr->common.threshold_raised = 0;
            }

            // Reset threshold info
            in_port_ptr->common.flags.port_has_threshold = (in_port_ptr->common.threshold_raised) ? TRUE : FALSE;
            in_port_ptr->common.port_event_new_threshold = in_port_ptr->common.threshold_raised;

            if (in_port_ptr->common.port_event_new_threshold)
            {
               GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, port_thresh);
            }
         }
         in_port_ptr->flags.need_more_input = TRUE; // initially keep need-more-input has TRUE.
         in_port_ptr->flags.was_eof_set     = TRUE; // initially, no need to detect discontinuity and handle EOF

         // at least one buf_ptr must be always present. needs to be done even if threshold prop is not successful
         // (e.g. decoding use case where ext-out buf is required to
         // start processing but media fmt is not known until decoding).
         if (!graph_init_ptr->input_bufs_ptr_arr_not_needed)
         {
            TRY(result,
                gen_topo_initialize_bufs_sdata(topo_ptr,
                                               &in_port_ptr->common,
                                               module_ptr->gu.module_instance_id,
                                               in_port_ptr->gu.cmn.id));
         }
      }
      // port is never updated
      in_port_ptr->gu.cmn.gu_status = GU_STATUS_DEFAULT;
   }

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_output_port_t *out_port_ptr    = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
      uint32_t                threshold_bytes = 0;

      if (!out_port_ptr->common.media_fmt_ptr)
      {
         tu_set_media_fmt(&topo_ptr->mf_utils, &out_port_ptr->common.media_fmt_ptr, NULL, topo_ptr->heap_id);
      }

      if (GU_STATUS_NEW == out_port_ptr->gu.cmn.gu_status || is_placeholder_replaced)
      {
         if (module_ptr->capi_ptr)
         {
            // if module is new, then gen_topo_create_module takes care of this.
            if (GU_STATUS_UPDATED == module_ptr->gu.gu_status)
            {
               TRY(result,
                   gen_topo_capi_set_data_port_op(module_ptr,
                                                  INTF_EXTN_DATA_PORT_OPEN,
                                                  &out_port_ptr->common.last_issued_opcode,
                                                  FALSE, /*is_input*/
                                                  out_port_ptr->gu.cmn.index,
                                                  out_port_ptr->gu.cmn.id));
            }

            // downstream state propagation should be blocked for decoder modules.
            // This is to make deocder in sync with network for real time playback (Vdec or Streaming).
            if (AMDB_MODULE_TYPE_DECODER == module_ptr->gu.module_type)
            {
               out_port_ptr->common.flags.is_state_prop_blocked = TRUE;
            }

            gen_topo_capi_get_port_thresh(module_ptr->gu.module_instance_id,
                                          topo_ptr->gu.log_id,
                                          module_ptr->capi_ptr,
                                          CAPI_OUTPUT_PORT,
                                          out_port_ptr->gu.cmn.index,
                                          &threshold_bytes);
            if (threshold_bytes > 1)
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_HIGH_PRIO,
                        "Module 0x%lX with op thresh is now non-inplace, prev_inplace= %u ",
                        out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                        module_ptr->flags.inplace);
               // Set has threshold flag on the port and make the module non-inplace.
               // <Refer input port context comment to understand the reason to make it non-inplace.>

               module_ptr->flags.inplace             = FALSE;
               module_ptr->flags.dynamic_inplace     = FALSE;
               out_port_ptr->common.threshold_raised = threshold_bytes;
            }
            else
            {

               out_port_ptr->common.threshold_raised = 0;
            }

            // Reset threshold info
            out_port_ptr->common.flags.port_has_threshold = (out_port_ptr->common.threshold_raised) ? TRUE : FALSE;
            out_port_ptr->common.port_event_new_threshold = out_port_ptr->common.threshold_raised;

            if (out_port_ptr->common.port_event_new_threshold)
            {
               GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, port_thresh);
            }
         }

         // at least one buf_ptr must be always present.
         TRY(result,
             gen_topo_initialize_bufs_sdata(topo_ptr,
                                            &out_port_ptr->common,
                                            module_ptr->gu.module_instance_id,
                                            out_port_ptr->gu.cmn.id));
      }
      // port is never updated

      out_port_ptr->gu.cmn.gu_status = GU_STATUS_DEFAULT;
   }
   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

/**
 *
 */
ar_result_t gen_topo_create_modules(gen_topo_t *topo_ptr, gen_topo_graph_init_t *graph_init_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   /* If async open is going on then this function will operate on async_gu which only contains new Subgraph.
    * If async open is not going on then this function will operate on main gu which will have all subgraphs.
    * */

   gu_sg_list_t *sg_list_ptr = get_gu_ptr_for_current_command_context(&topo_ptr->gu)->sg_list_ptr;

   topo_ptr->capi_cb = (NULL == graph_init_ptr->capi_cb) ? gen_topo_capi_callback : graph_init_ptr->capi_cb;

   for (; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;

      //  only operate on SG which are updated or new
      if (GU_STATUS_DEFAULT == sg_ptr->gu_status)
      {
         continue;
      }

      // Normally we would consider only updated or new SGs, but when updating max max input vs output ports,
      // we have to look at all modules even if there is no new handling.
      for (gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         // Consider only updated or new modules
         if (GU_STATUS_DEFAULT != module_ptr->gu.gu_status)
         {
            // New modules - create from AMDB; also set/get properties
            if (GU_STATUS_NEW == module_ptr->gu.gu_status)
            {
               module_ptr->topo_ptr = topo_ptr;

               // Also sets the ports.
               TRY(result, gen_topo_create_module(topo_ptr, graph_init_ptr, module_ptr));
            }

            /* Do data in/out port operations, and get port thresholds for all ports */
            TRY(result, gen_topo_init_set_get_data_port_properties(module_ptr, topo_ptr, FALSE, graph_init_ptr));

            /* Do control port operations, and get port thresholds for all ports */
            TRY(result, gen_topo_set_ctrl_port_properties(module_ptr, topo_ptr, FALSE));

            // As part of graph open new ports could have been opened, so need to update
            // dynamic inpace
            gen_topo_check_and_update_dynamic_inplace(module_ptr);

            module_ptr->gu.gu_status = GU_STATUS_DEFAULT;
         }
      }

      // Clearing gu status since we updated all needed modules in the sg. If status is already
      // default, this does nothing.
      sg_ptr->gu_status = GU_STATUS_DEFAULT;
   }

   if (!graph_init_ptr->skip_scratch_mem_reallocation)
   {
      // since new modules got created and there could be Max num ports change, check & recereate scratch memory
      TRY(result, gen_topo_check_n_realloc_scratch_memory(topo_ptr, TRUE /*is_open_context*/));
   }

   if (graph_init_ptr->propagate_rdf)
   {
      gen_topo_propagate_requires_data_buffering_upstream(topo_ptr);
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

ar_result_t gen_topo_check_n_realloc_scratch_memory(gen_topo_t *topo_ptr, bool_t is_open_context)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION

   uint32_t port_scratch_data_size = sizeof(gen_topo_port_scratch_data_t);
   uint32_t max_input_ports        = 0;
   uint32_t max_output_ports       = 0;
   uint32_t max_num_channels =
      tu_get_max_num_channels(&topo_ptr->mf_utils); // if MF is invalid, default is considered to be 1 channel

   uint32_t num_ext_in_ports  = 0;
   uint32_t num_ext_out_ports = 0;

   gu_sg_list_t *sg_list_ptr     = NULL;
   gu_sg_list_t *new_sg_list_ptr = NULL;

   num_ext_in_ports  = topo_ptr->gu.num_ext_in_ports;
   num_ext_out_ports = topo_ptr->gu.num_ext_out_ports;
   sg_list_ptr       = topo_ptr->gu.sg_list_ptr;

   // if async open handling is active then need to include the new external input/output ports and SG list pointer.
   if (is_open_context && topo_ptr->gu.async_gu_ptr)
   {
      gu_t *async_gu_ptr = get_gu_ptr_for_current_command_context(&topo_ptr->gu);
      num_ext_in_ports += async_gu_ptr->num_ext_in_ports;
      num_ext_out_ports += async_gu_ptr->num_ext_out_ports;
      new_sg_list_ptr = async_gu_ptr->sg_list_ptr; // async gu will have existing SGs as well.
   }

   while (sg_list_ptr)
   {
      for (; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
      {
         gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;

         // Normally we would consider only updated or new SGs, but when updating max max input vs output ports,
         // we have to look at all modules even if there is no new handling.
         for (gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr; (NULL != module_list_ptr);
              LIST_ADVANCE(module_list_ptr))
         {
            gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

            // Count the max of all modules, not just modules needing an update.
            max_input_ports  = MAX(max_input_ports, module_ptr->gu.max_input_ports);
            max_output_ports = MAX(max_output_ports, module_ptr->gu.max_output_ports);
         }
      }
      sg_list_ptr     = new_sg_list_ptr;
      new_sg_list_ptr = NULL;
   }

   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_HIGH_PRIO,
            "Reallocating scratch memory max_input_ports: %lu max_output_ports: %lu max_num_channels: %lu",
            max_input_ports,
            max_output_ports,
            max_num_channels);

   // Validate the max ports and num channels before proeceeding with allocations
   VERIFY(result, (CAPI_MAX_CHANNELS_V2 >= max_num_channels) && (0 < max_num_channels));

   int8_t *                          in_port_scratch_ptr      = NULL;
   int8_t *                          out_port_scratch_ptr     = NULL;
   gen_topo_ext_port_scratch_data_t *ext_in_port_scratch_ptr  = NULL;
   gen_topo_ext_port_scratch_data_t *ext_out_port_scratch_ptr = NULL;

   if (((topo_ptr->proc_context.max_num_channels != max_num_channels) ||
        (max_input_ports != topo_ptr->proc_context.num_in_ports)) &&
       (max_input_ports > 0))
   {
      uint32_t per_port_scratch_size = port_scratch_data_size + sizeof(capi_stream_data_v2_t *) +
                                       (max_num_channels * (sizeof(uint32_t) + sizeof(capi_buf_t)));

      MALLOC_MEMSET(in_port_scratch_ptr, int8_t, (max_input_ports * per_port_scratch_size), topo_ptr->heap_id, result);
   }

   if (((topo_ptr->proc_context.max_num_channels != max_num_channels) ||
        (max_output_ports != topo_ptr->proc_context.num_out_ports)) &&
       (max_output_ports > 0))
   {
      uint32_t per_port_scratch_size = port_scratch_data_size + sizeof(capi_stream_data_v2_t *) +
                                       (max_num_channels * (sizeof(uint32_t) + sizeof(capi_buf_t)));

      MALLOC_MEMSET(out_port_scratch_ptr, int8_t, max_output_ports * per_port_scratch_size, topo_ptr->heap_id, result);
   }

   if ((num_ext_in_ports != topo_ptr->proc_context.num_ext_in_ports) && (num_ext_in_ports > 0))
   {
      MALLOC_MEMSET(ext_in_port_scratch_ptr,
                    gen_topo_ext_port_scratch_data_t,
                    num_ext_in_ports * sizeof(gen_topo_ext_port_scratch_data_t),
                    topo_ptr->heap_id,
                    result);
   }
   if ((num_ext_out_ports != topo_ptr->proc_context.num_ext_out_ports) && (num_ext_out_ports > 0))
   {
      MALLOC_MEMSET(ext_out_port_scratch_ptr,
                    gen_topo_ext_port_scratch_data_t,
                    num_ext_out_ports * sizeof(gen_topo_ext_port_scratch_data_t),
                    topo_ptr->heap_id,
                    result);
   }

   SPF_CRITICAL_SECTION_START(&topo_ptr->gu);

   if (in_port_scratch_ptr)
   {
      int8_t *blob_ptr = in_port_scratch_ptr;

      MFREE_NULLIFY(topo_ptr->proc_context.in_port_scratch_ptr);

      // gen_topo_port_scratch_data_t
      topo_ptr->proc_context.in_port_scratch_ptr = (gen_topo_port_scratch_data_t *)(blob_ptr);
      blob_ptr += (max_input_ports * port_scratch_data_size);

      // capi_stream_data_v2_t
      topo_ptr->proc_context.in_port_sdata_pptr = (capi_stream_data_v2_t **)blob_ptr;
      blob_ptr += (max_input_ports * sizeof(capi_stream_data_v2_t *));

      for (uint32_t inp_idx = 0; inp_idx < max_input_ports; inp_idx++)
      {
         topo_ptr->proc_context.in_port_scratch_ptr[inp_idx].prev_actual_data_len = (uint32_t *)blob_ptr;
         blob_ptr += (sizeof(uint32_t) * max_num_channels);

         topo_ptr->proc_context.in_port_scratch_ptr[inp_idx].bufs = (capi_buf_t *)blob_ptr;
         blob_ptr += (sizeof(capi_buf_t) * max_num_channels);
      }

      topo_ptr->proc_context.num_in_ports = max_input_ports;
   }

   if (out_port_scratch_ptr)
   {
      int8_t *blob_ptr = out_port_scratch_ptr;
      MFREE_NULLIFY(topo_ptr->proc_context.out_port_scratch_ptr);

      // gen_topo_port_scratch_data_t
      topo_ptr->proc_context.out_port_scratch_ptr = (gen_topo_port_scratch_data_t *)(blob_ptr);
      blob_ptr += (max_output_ports * port_scratch_data_size);

      // capi_stream_data_v2_t
      topo_ptr->proc_context.out_port_sdata_pptr = (capi_stream_data_v2_t **)blob_ptr;
      blob_ptr += (max_output_ports * sizeof(capi_stream_data_v2_t *));

      for (uint32_t out_idx = 0; out_idx < max_output_ports; out_idx++)
      {
         topo_ptr->proc_context.out_port_scratch_ptr[out_idx].prev_actual_data_len = (uint32_t *)blob_ptr;
         blob_ptr += (sizeof(uint32_t) * max_num_channels);

         topo_ptr->proc_context.out_port_scratch_ptr[out_idx].bufs = (capi_buf_t *)blob_ptr;
         blob_ptr += (sizeof(capi_buf_t) * max_num_channels);
      }

      topo_ptr->proc_context.num_out_ports = max_output_ports;
   }

   // Update MAX channels
   topo_ptr->proc_context.max_num_channels = max_num_channels;

   if (ext_in_port_scratch_ptr)
   {
      MFREE_NULLIFY(topo_ptr->proc_context.ext_in_port_scratch_ptr);

      topo_ptr->proc_context.ext_in_port_scratch_ptr = ext_in_port_scratch_ptr;

      topo_ptr->proc_context.num_ext_in_ports = num_ext_in_ports;
   }

   if (ext_out_port_scratch_ptr)
   {
      MFREE_NULLIFY(topo_ptr->proc_context.ext_out_port_scratch_ptr);

      topo_ptr->proc_context.ext_out_port_scratch_ptr = ext_out_port_scratch_ptr;

      topo_ptr->proc_context.num_ext_out_ports = num_ext_out_ports;
   }

   SPF_CRITICAL_SECTION_END(&topo_ptr->gu);

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Invalid Max num_in_ports=%lu num_out_port=%lu num_channels=%lu",
               max_input_ports,
               max_output_ports,
               max_num_channels);
   }

   return result;
}

/**
 * reset_capi_dependent_dont_destroy - for placeholder module reset we want to only reset capi stuff and not destroy
 * everything
 */
void gen_topo_destroy_module(gen_topo_t *       topo_ptr,
                             gen_topo_module_t *module_ptr,
                             bool_t             reset_capi_dependent_dont_destroy)
{
   /*Every module is registered at the time of create, hence needs to be registered*/
   if (!reset_capi_dependent_dont_destroy)
   {
      // deregister is also done in gen_cntr_deinit_ext_out_port & gen_cntr_deinit_ext_in_port
      // to make sure dereg happens before queue destroy
      __gpr_cmd_deregister(module_ptr->gu.module_instance_id);
   }

   // At this time IRM will not be accessing the profing memory since APM would have informed it
   gen_topo_prof_handle_deinit(topo_ptr, module_ptr);

   gen_topo_fmwk_extn_handle_at_deinit(topo_ptr, module_ptr);

   // do this b4 port is destroyed due to dependency on bufs_ptr in gen_topo_get_out_port_data_len
   gen_topo_check_destroy_bypass_module(topo_ptr, module_ptr, TRUE);

   if (module_ptr->capi_ptr)
   {
      module_ptr->capi_ptr->vtbl_ptr->end(module_ptr->capi_ptr);
      MFREE_NULLIFY(module_ptr->capi_ptr);
   }

   if (topo_ptr->topo_to_cntr_vtable_ptr->destroy_module)
   {
      topo_ptr->topo_to_cntr_vtable_ptr->destroy_module(topo_ptr, module_ptr, reset_capi_dependent_dont_destroy);
   }

   // after calling end (because algo reset can cause trigger policy to be raised)
   gen_topo_destroy_trigger_policy(module_ptr);

   // Destroy module input and output port resources.
   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      gen_topo_destroy_cmn_port(topo_ptr,
                                &out_port_ptr->common,
                                &out_port_ptr->gu.cmn,
                                reset_capi_dependent_dont_destroy);

      if (reset_capi_dependent_dont_destroy)
      {
         gen_topo_reset_output_port_capi_dependent_portion(topo_ptr, module_ptr, out_port_ptr);
      }
   }

   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

      gen_topo_destroy_cmn_port(topo_ptr,
                                &in_port_ptr->common,
                                &in_port_ptr->gu.cmn,
                                reset_capi_dependent_dont_destroy);

      if (reset_capi_dependent_dont_destroy)
      {
         gen_topo_reset_input_port_capi_dependent_portion(topo_ptr, module_ptr, in_port_ptr);
      }
   }

   // if we are removing capi dependent part of the module only, we still need to reset other capi dependent part
   if (reset_capi_dependent_dont_destroy)
   {
      gen_topo_reset_module_capi_dependent_portion(topo_ptr, module_ptr);
   }
}
/**
 * if flag "b_destroy_all_modules" is set then all modules are destroyed.
 * if flag "b_destroy_all_modules" is unset then modules within subgraphs marked for close are destroyed.
 */
ar_result_t gen_topo_destroy_modules(gen_topo_t *topo_ptr, bool_t b_destroy_all_modules)
{
   gu_sg_list_t *sg_list_ptr = get_gu_ptr_for_current_command_context(&topo_ptr->gu)->sg_list_ptr;
   for (; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      // If sg_list is not providing then close all SGs.
      if (b_destroy_all_modules || GU_STATUS_CLOSING == sg_list_ptr->sg_ptr->gu_status)
      {
         for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
              LIST_ADVANCE(module_list_ptr))
         {
            gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
            gen_topo_destroy_module(topo_ptr, module_ptr, FALSE /*reset_capi_dependent_dont_destroy*/);
         }
      }
   }

   return AR_EOK;
}

ar_result_t gen_topo_destroy_topo(gen_topo_t *topo_ptr)
{
   tu_destroy_mf(&topo_ptr->mf_utils);

   topo_buf_manager_deinit(topo_ptr);

   MFREE_NULLIFY(topo_ptr->proc_context.in_port_scratch_ptr);
   MFREE_NULLIFY(topo_ptr->proc_context.out_port_scratch_ptr);
   MFREE_NULLIFY(topo_ptr->proc_context.ext_in_port_scratch_ptr);
   MFREE_NULLIFY(topo_ptr->proc_context.ext_out_port_scratch_ptr);

   // free the started sorted module list if not done yet
   spf_list_delete_list((spf_list_node_t **)&topo_ptr->started_sorted_module_list_ptr, TRUE);

   thin_topo_destroy(topo_ptr);

   return AR_EOK;
}

uint32_t gen_topo_get_port_threshold(void *port_ptr)
{
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)port_ptr;
   return gen_topo_get_curr_port_threshold(&out_port_ptr->common);
}

uint32_t gen_topo_get_curr_port_threshold(gen_topo_common_port_t *port_ptr)
{
   uint32_t threshold =
      (0 != port_ptr->port_event_new_threshold) ? port_ptr->port_event_new_threshold : port_ptr->max_buf_len;

   return threshold;
}

uint32_t gen_topo_get_curr_port_bufs_num_v2(gen_topo_common_port_t *port_ptr)
{
   uint32_t bufs_num_v2 = 0;
   if(SPF_DEINTERLEAVED_RAW_COMPRESSED == port_ptr->media_fmt_ptr->data_format)
   {
      bufs_num_v2 = port_ptr->media_fmt_ptr->deint_raw.bufs_num;
   }
   return bufs_num_v2;
}

void gen_topo_set_med_fmt_to_attached_module(gen_topo_t *topo_ptr, gen_topo_output_port_t *out_port_ptr)
{
   // Set media format on the attached module and assume it is successful.
   if (!out_port_ptr->gu.attached_module_ptr || !out_port_ptr->common.flags.media_fmt_event)
   {
      return;
   }

   gen_topo_module_t *attached_module_ptr = (gen_topo_module_t *)out_port_ptr->gu.attached_module_ptr;

   if (NULL == attached_module_ptr->capi_ptr)
   {
      // module is destroyed.
      return;
   }

   gen_topo_input_port_t *attached_input_port_ptr =
      (gen_topo_input_port_t *)attached_module_ptr->gu.input_port_list_ptr->ip_port_ptr;
   gen_topo_input_port_t *attached_output_port_ptr =
      ((gen_topo_input_port_t *)attached_module_ptr->gu.output_port_list_ptr)
         ? (gen_topo_input_port_t *)attached_module_ptr->gu.output_port_list_ptr->op_port_ptr
         : NULL;

   TOPO_PRINT_MEDIA_FMT(topo_ptr->gu.log_id,
                        attached_module_ptr,
                        attached_input_port_ptr,
                        out_port_ptr->common.media_fmt_ptr,
                        "elementary module input");

   // set the media format to the attached module's input port
   if (SPF_IS_PCM_DATA_FORMAT(out_port_ptr->common.media_fmt_ptr->data_format) &&
       TU_IS_ANY_DEINTERLEAVED_UNPACKED(out_port_ptr->common.media_fmt_ptr->pcm.interleaving))
   {
      // Irrespective of the unpacked version supported by the host set V2 if the attached supports V2,
      // else downgrade to V1.
      topo_media_fmt_t temp_mf = *(out_port_ptr->common.media_fmt_ptr);
      temp_mf.pcm.interleaving = attached_module_ptr->flags.supports_deintlvd_unpacked_v2
                                    ? TOPO_DEINTERLEAVED_UNPACKED_V2
                                    : TOPO_DEINTERLEAVED_UNPACKED;
      tu_set_media_fmt(&topo_ptr->mf_utils,
                       &attached_input_port_ptr->common.media_fmt_ptr,
                       &temp_mf,
                       topo_ptr->heap_id);
   }
   else
   {
      tu_set_media_fmt_from_port(&topo_ptr->mf_utils,
                                 &attached_input_port_ptr->common.media_fmt_ptr,
                                 out_port_ptr->common.media_fmt_ptr);
   }

   // set the media format to the attached module Capi
   ar_result_t result = gen_topo_capi_set_media_fmt(topo_ptr,
                                                    attached_module_ptr,
                                                    attached_input_port_ptr->common.media_fmt_ptr,
                                                    TRUE, // is input mf from tap module's perspective
                                                    attached_input_port_ptr->gu.cmn.index);

   if (AR_FAILED(result))
   {
      attached_input_port_ptr->common.flags.module_rejected_mf = TRUE;
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Warning: Attached Module 0x%lX: Set Input Media format failed. bypassing module. setting "
               "module_rejected_mf=%lu",
               attached_module_ptr->gu.module_instance_id,
               attached_input_port_ptr->common.flags.module_rejected_mf);
   }
   else
   {
      attached_input_port_ptr->common.flags.module_rejected_mf = FALSE;
      attached_input_port_ptr->common.flags.media_fmt_event    = FALSE;
      attached_input_port_ptr->common.flags.is_mf_valid        = TRUE;

      gen_topo_reset_pcm_unpacked_mask(&attached_input_port_ptr->common);

      if (attached_output_port_ptr)
      {
         // attached module's output port media format will be same as input.
         tu_set_media_fmt_from_port(&topo_ptr->mf_utils,
                                    &attached_output_port_ptr->common.media_fmt_ptr,
                                    out_port_ptr->common.media_fmt_ptr);

         // we can clear the media fmt event as next module is going to take the media fmt for its host module's
         // output port.
         attached_output_port_ptr->common.flags.media_fmt_event = FALSE;
         attached_output_port_ptr->common.flags.is_mf_valid     = TRUE;

         gen_topo_reset_pcm_unpacked_mask(&attached_input_port_ptr->common);
      }
   }
}

static ar_result_t gen_topo_prop_med_fmt_from_prev_out_to_next_in(gen_topo_t *                topo_ptr,
                                                                  gen_topo_module_t *         module_ptr,
                                                                  gen_topo_input_port_t *     in_port_ptr,
                                                                  bool_t                      is_data_path,
                                                                  gen_topo_capi_event_flag_t *capi_event_flag_ptr)
{
   ar_result_t             result       = AR_EOK;
   gen_topo_output_port_t *prev_out_ptr = (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr;

   bool_t is_run_time = (TOPO_PORT_STATE_STARTED == in_port_ptr->common.state);

   /* *
    *  propagation is possible only if port states are appropriate & also if there's a media fmt event.
    *
    *  Do not break going through the sorted module list if propagation is not possible.
    *  Modules may be connected nonlinearly.
    *
    */
   bool_t mf_event_present =
      (prev_out_ptr && prev_out_ptr->common.flags.media_fmt_event) || in_port_ptr->common.flags.media_fmt_event;

   bool_t propagation_possible = mf_event_present;

   bool_t is_rt = (in_port_ptr->common.flags.is_upstream_realtime ||
                   (/*topo_ptr->flags.is_signal_triggered && */ topo_ptr->flags.is_signal_triggered_active));

   // Force propagation if the input port hasn't received a media format yet and the previous output port
   // has a valid media format.
   // - (!propagation_possible): No need to force if already true.
   // - (prev_out_ptr): Only needed for internal input port case. External inputs will always receive a new media
   //   format on the data path before data during start.
   if ((!propagation_possible) && (!in_port_ptr->flags.media_fmt_received) && prev_out_ptr)
   {
      if (prev_out_ptr->common.flags.is_mf_valid)
      {
         propagation_possible = TRUE;

         prev_out_ptr->common.flags.media_fmt_event = TRUE;
         capi_event_flag_ptr->media_fmt_event       = TRUE;
      }
   }

   /*
      data_path:
    is_rt
       drop-data and propagated
    in_nrt
       if no data then propagate
     ctrl_path:
         if no data then propagate
    */
   if (propagation_possible) // this check prevents unnecessary warning prints.
   {
      if (is_data_path)
      {
         if (!is_run_time)
         {
            /** Data path media format must be propagated to only those parts of the graph which are started.
             */
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "Warning: Data path media format will not be propagated to not started ports (0x%lX, 0x%lx).",
                     in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                     in_port_ptr->gu.cmn.id);
            propagation_possible = FALSE;
         }

         // print warning only if RT & propagation not possible or if > 0 bytes. otherwise, no use printing.
         if (is_rt)
         {
            if ((in_port_ptr->common.bufs_ptr[0].actual_data_len > 0) || (module_ptr->pending_zeros_at_eos > 0))
            {
               // drop RT and propagate media format in case of RT/interrupt driven use cases even if
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Warning: Propagating media format to (0x%lX, 0x%lx) by dropping pending data %lu for real "
                        "time "
                        "case, pending zeros %lu dropped",
                        in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                        in_port_ptr->gu.cmn.id,
                        in_port_ptr->common.bufs_ptr[0].actual_data_len,
                        module_ptr->pending_zeros_at_eos);

               gen_topo_drop_all_metadata_within_range(topo_ptr->gu.log_id,
                                                       (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr,
                                                       &in_port_ptr->common,
                                                       gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common),
                                                       FALSE /*keep_eos_and_ba_md*/);
               // drop the data and propagate
               gen_topo_set_all_bufs_len_to_zero(&in_port_ptr->common);
               module_ptr->pending_zeros_at_eos = 0;
            }

            if (!propagation_possible)
            {
               propagation_possible = TRUE;
               // drop RT and propagate media format in case of RT/interrupt driven use cases even if
               // propagation_possible=FALSE due to prepare (above).
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Warning: Propagating media format to (0x%lX, 0x%lx) forcefully for real time case. "
                        "port_state%u)",
                        in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                        in_port_ptr->gu.cmn.id,
                        in_port_ptr->common.state);
            }
         }
         else
         {
            if ((in_port_ptr->common.bufs_ptr[0].actual_data_len > 0) || (module_ptr->pending_zeros_at_eos > 0))
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Warning: Media format propagation to (0x%lX, 0x%lx) not possible as there's pending data %lu, "
                        "or "
                        "pending zeros %lu",
                        in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                        in_port_ptr->gu.cmn.id,
                        in_port_ptr->common.bufs_ptr[0].actual_data_len,
                        module_ptr->pending_zeros_at_eos);
               propagation_possible = FALSE;
            }
         }
      }
      else
      {
#if 0 // this function is called only for SG which are in PREPARE or START state
         topo_port_state_t in_port_sg_state;
         in_port_sg_state = topo_sg_state_to_port_state(gen_topo_get_sg_state(in_port_ptr->gu.cmn.module_ptr->sg_ptr));

         if (TOPO_PORT_STATE_STOPPED == in_port_sg_state || TOPO_PORT_STATE_SUSPENDED == in_port_sg_state)
         {
            /** Control path media format must not be propagated to parts of the graph which are already running or
             * in stop state. Must be in prepare orstarted state.
             * this is just optimization to prevent multiple media fmt from propagating in stop state.
             */
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "Warning: Control path media format will not be propagated to "
                     "stopped (%lu) sg ports (sg must be in prepare/start state) (0x%lX, 0x%lx). ",
                     in_port_sg_state,
                     in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                     in_port_ptr->gu.cmn.id);
            propagation_possible = FALSE;
         }
#endif

         if ((in_port_ptr->common.bufs_ptr[0].actual_data_len > 0) || (module_ptr->pending_zeros_at_eos > 0))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Warning: Media format propagation to (0x%lX, 0x%lx) not possible as there's pending data %lu, or "
                     "pending zeros %lu",
                     in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                     in_port_ptr->gu.cmn.id,
                     in_port_ptr->common.bufs_ptr[0].actual_data_len,
                     module_ptr->pending_zeros_at_eos);
            propagation_possible = FALSE;
         }
      }
   }

   if (!propagation_possible)
   {
      // in case of control path MF sent after start, prop won't be done now. but we need to remember for later.
      if (mf_event_present)
      {
         in_port_ptr->common.flags.media_fmt_event = TRUE;
      }
      return result;
   }

   if (prev_out_ptr && prev_out_ptr->common.flags.media_fmt_event)
   {
      // for raw fmt, ptr created in capi callback is passed to next input, which is free'd in
      // gen_topo_capi_set_media_fmt or at port destroy
      if (SPF_IS_PCM_DATA_FORMAT(prev_out_ptr->common.media_fmt_ptr->data_format) &&
          TU_IS_ANY_DEINTERLEAVED_UNPACKED(prev_out_ptr->common.media_fmt_ptr->pcm.interleaving))
      {
         // Irrespective of what prev ports deinterleaving version is set, if the next supports V2,
         // propagate as V2, else downgrade to V1
         topo_media_fmt_t temp_mf = *prev_out_ptr->common.media_fmt_ptr;
         temp_mf.pcm.interleaving = module_ptr->flags.supports_deintlvd_unpacked_v2 ? TOPO_DEINTERLEAVED_UNPACKED_V2
                                                                                    : TOPO_DEINTERLEAVED_UNPACKED;
         tu_set_media_fmt(&topo_ptr->mf_utils, &in_port_ptr->common.media_fmt_ptr, &temp_mf, topo_ptr->heap_id);
      }
      else
      {
         tu_set_media_fmt_from_port(&topo_ptr->mf_utils,
                                    &in_port_ptr->common.media_fmt_ptr,
                                    prev_out_ptr->common.media_fmt_ptr);
      }

      prev_out_ptr->common.flags.media_fmt_event = FALSE;
      in_port_ptr->common.flags.media_fmt_event  = TRUE;
      in_port_ptr->flags.media_fmt_received      = TRUE;
      in_port_ptr->common.flags.is_mf_valid      = prev_out_ptr->common.flags.is_mf_valid;

      gen_topo_reset_pcm_unpacked_mask(&in_port_ptr->common);

      if (topo_ptr->flags.port_mf_rtm_dump_enable == TRUE)
      {
         uint32_t                port_id      = prev_out_ptr->gu.cmn.id;
         gen_topo_common_port_t *cmn_port_ptr = &prev_out_ptr->common;
         gen_topo_rtm_dump_change_in_port_mf(topo_ptr, module_ptr, cmn_port_ptr, port_id);
      }
   }

   // For ext in port this flag is set at container level.
   if (in_port_ptr->common.flags.media_fmt_event)
   {
      // If media format is updated then update port threshold as well.
      capi_event_flag_ptr->port_thresh = TRUE;

      if (in_port_ptr->common.media_fmt_ptr &&
          (SPF_DEINTERLEAVED_RAW_COMPRESSED == in_port_ptr->common.media_fmt_ptr->data_format))
      {
         THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_unsupported_mf, TRUE);
      }

      // Even if module is being bypassed, we need to set media fmt as setting media fmt may enable module.
      if (module_ptr->capi_ptr)
      {
         if (in_port_ptr->common.flags.is_mf_valid)
         {
            result = gen_topo_capi_set_media_fmt(topo_ptr,
                                                 module_ptr,
                                                 in_port_ptr->common.media_fmt_ptr,
                                                 TRUE, // is input mf?
                                                 in_port_ptr->gu.cmn.index);
         }
         if (AR_DID_FAIL(result))
         {
            in_port_ptr->common.flags.module_rejected_mf = TRUE;
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "Warning: Module 0x%lX: Set Input Media format failed. bypassing module. setting "
                     "module_rejected_mf=%lu",
                     module_ptr->gu.module_instance_id,
                     in_port_ptr->common.flags.module_rejected_mf);

            // cannot set module_ptr->flags.disabled as disabled in controlled by process state event.
            result = gen_topo_check_create_bypass_module(topo_ptr, module_ptr);
            if (AR_DID_FAIL(result))
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Module 0x%lX: Error creating bypass module",
                        module_ptr->gu.module_instance_id);
            }
            // no need to set capi_event_flag_ptr->process_state = TRUE;
         }
         else
         {
            // call destroy_bypass_module if module is enabled by this time and media fmt is success.
            if (module_ptr->bypass_ptr && !module_ptr->flags.disabled)
            {
               result = gen_topo_check_destroy_bypass_module(topo_ptr, module_ptr, FALSE);
            }

            in_port_ptr->common.flags.module_rejected_mf = FALSE;

            if (topo_ptr->flags.port_mf_rtm_dump_enable == TRUE)
            {
               uint32_t                port_id      = in_port_ptr->gu.cmn.id;
               gen_topo_common_port_t *cmn_port_ptr = &in_port_ptr->common;
               gen_topo_rtm_dump_change_in_port_mf(topo_ptr, module_ptr, cmn_port_ptr, port_id);
            }
         }

         if (module_ptr->bypass_ptr)
         {
            uint32_t kpps = 0;
            if (in_port_ptr->common.media_fmt_ptr)
            {
               kpps = topo_get_memscpy_kpps(in_port_ptr->common.media_fmt_ptr->pcm.bits_per_sample,
                                            in_port_ptr->common.media_fmt_ptr->pcm.num_channels,
                                            in_port_ptr->common.media_fmt_ptr->pcm.sample_rate);
            }
            capi_event_flag_ptr->kpps = (kpps != module_ptr->kpps);
            module_ptr->kpps          = kpps;
         }
      }

      bool_t is_placeholder = (MODULE_ID_PLACEHOLDER_DECODER == module_ptr->gu.module_id) ||
                              (MODULE_ID_PLACEHOLDER_ENCODER == module_ptr->gu.module_id);

      /* no CAPI, only one i/p and o/p port possible
       * propagate input to output for framework modules and bypass modules.
       * for placeholder decoder/encoder, do not propagate input MF to output. (E.g. propagating raw MF to output
       * shouldn't be done for placeholder decoder) unless disabled*/
      if ((is_placeholder && module_ptr->flags.disabled && !module_ptr->capi_ptr) ||
          (!is_placeholder && (!module_ptr->capi_ptr || module_ptr->bypass_ptr)))
      {
         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; NULL != out_port_list_ptr;
              LIST_ADVANCE(out_port_list_ptr))
         {
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

            if (in_port_ptr->common.flags.is_mf_valid && in_port_ptr->common.media_fmt_ptr &&
                tu_has_media_format_changed(out_port_ptr->common.media_fmt_ptr, in_port_ptr->common.media_fmt_ptr))
            {
               tu_set_media_fmt_from_port(&topo_ptr->mf_utils,
                                          &out_port_ptr->common.media_fmt_ptr,
                                          in_port_ptr->common.media_fmt_ptr);
               out_port_ptr->common.flags.media_fmt_event = TRUE;
               out_port_ptr->common.flags.is_mf_valid     = TRUE;

               gen_topo_reset_pcm_unpacked_mask(&out_port_ptr->common);

               if ((module_ptr->bypass_ptr) && (module_ptr->bypass_ptr->media_fmt_ptr) &&
                   (SPF_IS_PACKETIZED_OR_PCM(module_ptr->bypass_ptr->media_fmt_ptr->data_format)) &&
                   (1 == module_ptr->bypass_ptr->media_fmt_ptr->pcm.num_channels) &&
                   (out_port_ptr->common.media_fmt_ptr->pcm.interleaving !=
                    module_ptr->bypass_ptr->media_fmt_ptr->pcm.interleaving))
               {
                  // if num ch is 1, output mf picks the interleaving field of the bypassed module
                  // This is needed to ensure downstream modules receive inp mf acc to what they expect.
                  // For 1ch, interleaving field is actually dont care
                  out_port_ptr->common.media_fmt_ptr->pcm.interleaving =
                     module_ptr->bypass_ptr->media_fmt_ptr->pcm.interleaving;
               }

               TOPO_PRINT_MEDIA_FMT(topo_ptr->gu.log_id,
                                    module_ptr,
                                    out_port_ptr,
                                    out_port_ptr->common.media_fmt_ptr,
                                    "module output");

               gen_topo_set_med_fmt_to_attached_module(topo_ptr, out_port_ptr);
            }
         }
      }

      /*
       * can't destroy the input port's media format, this is shared with output ports media format as well
       * in case of fwk or bypass module

      // this memory for raw fmt is not required anymore. just free immediately after setting to the module.
      if (SPF_RAW_COMPRESSED == in_port_ptr->common.media_fmt_ptr->data_format)
      {
         tu_capi_destroy_raw_compr_med_fmt(&in_port_ptr->common.media_fmt_ptr->raw);
      }
       */

      in_port_ptr->common.flags.media_fmt_event = FALSE;
   }
   return result;
}

ar_result_t gen_topo_propagate_media_fmt(void *cxt_ptr, bool_t is_data_path)
{
   ar_result_t result   = AR_EOK;
   gen_topo_t *topo_ptr = (gen_topo_t *)cxt_ptr;

   result |= gen_topo_propagate_media_fmt_from_module(cxt_ptr, is_data_path, topo_ptr->gu.sorted_module_list_ptr);

   return result;
}

/**
 * before calling this, ext input port media fmt is assigned to input port media fmt.
 * framework extension callback allows different topo implementations to call this function and have
 * different framework extension handling.
 */
ar_result_t gen_topo_propagate_media_fmt_from_module(void *            cxt_ptr,
                                                     bool_t            is_data_path,
                                                     gu_module_list_t *start_module_list_ptr)
{
   ar_result_t                 result              = AR_EOK;
   gen_topo_t *                topo_ptr            = (gen_topo_t *)cxt_ptr;
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;

   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_MED_PRIO,
            "Propagating media format through topo from Module 0x%lX",
            (start_module_list_ptr ? start_module_list_ptr->module_ptr->module_instance_id : 0));

   // this is an event handler function
   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT

   // this function can be called directly from different contexts
   // therefore need to reconcile the event flags.
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, topo_ptr, TRUE /*do_reconcile*/);

   for (gu_module_list_t *module_list_ptr = start_module_list_ptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

      // only need to propagate media format from module SG which are in either PREAPRE or START state.
      // Consider a case where data-path is handling the media format propagation while the command-path is handling the
      // STOP/SUSPEND, we need to prevent access to the stop and suspended SGs from the data-path.
      if (gen_topo_is_module_sg_stopped_or_suspended(module_ptr))
      {
         continue;
      }

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         // Copy media fmt from prev out port to this input port (link/connection)
         gen_topo_prop_med_fmt_from_prev_out_to_next_in(topo_ptr,
                                                        module_ptr,
                                                        in_port_ptr,
                                                        is_data_path,
                                                        capi_event_flag_ptr);
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; NULL != out_port_list_ptr;
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
         if (SPF_UNKNOWN_DATA_FORMAT == out_port_ptr->common.media_fmt_ptr->data_format)
         {
            if (module_ptr->capi_ptr)
            {
               (void)gen_topo_capi_get_out_media_fmt(topo_ptr, module_ptr, out_port_ptr);
            }
         }

         // if output port has a media format event then mark threshold event flag so that threshold propagation can be
         // completed.
         if (out_port_ptr->common.flags.media_fmt_event)
         {
            capi_event_flag_ptr->port_thresh = TRUE;
         }
      }

      // check and update dynamic inplace flag based on new media format.
      gen_topo_check_and_update_dynamic_inplace(module_ptr);

      // if out port got destroyed and recreated while disabled, output needs to be re-initialized with input media
      // format.
      if (module_ptr->bypass_ptr) // SISO
      {
         /* If the output/input port doesn't exists then media format cannot be propagated in the same path.
            Parallel path may exists and still have to propagate in those paths */
         if ((module_ptr->gu.num_input_ports == 0) || (module_ptr->gu.num_output_ports == 0))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "Warning: Module 0x%lX is dangling. Media format cannot be propagated though current path. "
                     "Continuing with other parallel paths",
                     module_ptr->gu.module_instance_id);
            continue;
         }
         gen_topo_input_port_t * in_port_ptr = (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;
         gen_topo_output_port_t *out_port_ptr =
            (gen_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr;

         if (in_port_ptr->common.flags.is_mf_valid &&
             tu_has_media_format_changed(out_port_ptr->common.media_fmt_ptr, in_port_ptr->common.media_fmt_ptr))
         {
            tu_set_media_fmt_from_port(&topo_ptr->mf_utils,
                                       &out_port_ptr->common.media_fmt_ptr,
                                       in_port_ptr->common.media_fmt_ptr);
            out_port_ptr->common.flags.is_mf_valid     = TRUE;
            out_port_ptr->common.flags.media_fmt_event = TRUE;
            capi_event_flag_ptr->port_thresh           = TRUE;

            TOPO_PRINT_MEDIA_FMT(topo_ptr->gu.log_id,
                                 module_ptr,
                                 out_port_ptr,
                                 out_port_ptr->common.media_fmt_ptr,
                                 "bypass module output");

            gen_topo_set_med_fmt_to_attached_module(topo_ptr, out_port_ptr);
         }
      }
   }

   // Need to check if there is any change in Max channels since MF got propagated.
   capi_event_flag_ptr->realloc_scratch_mem = TRUE;

   // clear event (even if prop didn't go thru till the end). During data processing, it will complete.
   capi_event_flag_ptr->media_fmt_event = FALSE;

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(topo_ptr->gu.log_id, DBG_MED_PRIO, "Total number of media format blocks %lu", topo_ptr->mf_utils.num_nodes);
#endif

   return result;
}

void gen_topo_find_cached_set_event_prop(uint32_t         log_id,
                                         spf_list_node_t *event_list_ptr,
                                         uint32_t         event_id,
                                         void **          cached_node)
{
   *cached_node = NULL;
   for (spf_list_node_t *list_ptr = event_list_ptr; (NULL != list_ptr); LIST_ADVANCE(list_ptr))
   {
      gen_topo_cached_event_node_t *cached_event_node_ptr = (gen_topo_cached_event_node_t *)event_list_ptr->obj_ptr;
      if ((NULL != cached_event_node_ptr) && (event_id == cached_event_node_ptr->reg_event_payload.event_id))
      {
         *cached_node = (void *)cached_event_node_ptr;
         return;
      }
   }
}

/**
 * 50+1 modules in series possible
 */
#define NBLC_RECURSE_MAX_DEPTH 50

#define PRINT_NBLC(begin, current, end, str)                                                                           \
   TOPO_MSG(topo_ptr->gu.log_id,                                                                                       \
            DBG_LOW_PRIO,                                                                                              \
            str " : For (Module, port-id) (0x%lX, 0x%lx) : nblc start: (0x%lX, 0x%lx) nblc end: (0x%lX, 0x%lx) ",      \
            current->gu.cmn.module_ptr->module_instance_id,                                                            \
            current->gu.cmn.id,                                                                                        \
            (NULL == begin) ? 0 : begin->gu.cmn.module_ptr->module_instance_id,                                        \
            (NULL == begin) ? 0 : begin->gu.cmn.id,                                                                    \
            (NULL == end) ? 0 : end->gu.cmn.module_ptr->module_instance_id,                                            \
            (NULL == end) ? 0 : end->gu.cmn.id)

#define PRINT_STATE_PROP_F(src, dst, state)                                                                            \
   TOPO_MSG(topo_ptr->gu.log_id,                                                                                       \
            DBG_LOW_PRIO,                                                                                              \
            "gen_topo_port_state_prop: : propagating topo_port_state=0x%x forwards from (Module, out-port-id) "        \
            "(0x%lX, 0x%lx) to (0x%lX, 0x%lx)  ",                                                                      \
            state,                                                                                                     \
            src->gu.cmn.module_ptr->module_instance_id,                                                                \
            src->gu.cmn.id,                                                                                            \
            dst->gu.cmn.module_ptr->module_instance_id,                                                                \
            dst->gu.cmn.id)

#define PRINT_STATE_PROP_B(src, dst, state)                                                                            \
   TOPO_MSG(topo_ptr->gu.log_id,                                                                                       \
            DBG_LOW_PRIO,                                                                                              \
            "gen_topo_port_state_prop: : propagating topo_port_state=0x%x backwards from (Module, port-id) "           \
            "(0x%lX, 0x%lx) to (0x%lX, 0x%lx)  ",                                                                      \
            state,                                                                                                     \
            src->gu.cmn.module_ptr->module_instance_id,                                                                \
            src->gu.cmn.id,                                                                                            \
            dst->gu.cmn.module_ptr->module_instance_id,                                                                \
            dst->gu.cmn.id)

/** When we hit a MIMO module or a buffering module (including thresh modules), linear chain ends and a new one begins.
 * buffering modules are capable of saying need-more on the input port
 * pending data could linger for requires_data_buf=False, has_thread=True as cntr itself determines need-more.
 *
 * trigger policy module breaks the nblc because they may not need data on ports.
 * */
inline bool_t gen_topo_is_port_at_nblc_end(gu_module_t *gu_module_ptr, gen_topo_common_port_t *cmn_port_ptr)
{
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)gu_module_ptr;
   bool_t is_mimo = ((module_ptr->gu.num_input_ports > 1) || (module_ptr->gu.num_output_ports > 1)) ? TRUE : FALSE;
   return ((is_mimo || module_ptr->flags.need_mp_buf_extn || module_ptr->flags.need_trigger_policy_extn ||
            (cmn_port_ptr && (module_ptr->flags.requires_data_buf || cmn_port_ptr->flags.port_has_threshold))));
}

/**
 * Recursive (backwards in graph shape)
 *
 * if previous module has more than 1 input then it becomes end of nblc
 * otherwise, nblc pointer originally passed carries forward.
 *
 * nblc end is assigned to nblc beginning.
 *
 * (prev_in_port_ptr) - [ prev_module_ptr ] - (prev_out_port_ptr) -> in_port_ptr
 *
 * Asymmetry between nblc on input and nblc on output side:
 *    input nblc end can be NULL. output nblc end is not null usually.
 *    reason is nblc end on input side needs to be only buffering or multiport.
 *    however, nblc end on output side can be any module.
 *    This is the reason why extra argument is in input func (nblc_start_pptr)
 */
static ar_result_t gen_topo_assign_nblc_input_recursive(gen_topo_t *            topo_ptr,
                                                        gen_topo_input_port_t * in_port_ptr,
                                                        gen_topo_input_port_t * nblc_end_ptr,
                                                        gen_topo_input_port_t **nblc_start_pptr,
                                                        uint32_t *              recurse_depth_ptr)
{
   ar_result_t             result            = AR_EOK;
   gen_topo_output_port_t *prev_out_port_ptr = (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr;

   RECURSION_ERROR_CHECK_ON_FN_ENTRY(topo_ptr->gu.log_id, recurse_depth_ptr, NBLC_RECURSE_MAX_DEPTH);

   // ext-in cases.
   if (!prev_out_port_ptr)
   {
      in_port_ptr->nblc_end_ptr = nblc_end_ptr;
      *nblc_start_pptr          = in_port_ptr;

      RECURSION_ERROR_CHECK_ON_FN_EXIT(topo_ptr->gu.log_id, recurse_depth_ptr);
      return result;
   }

   gen_topo_module_t *prev_module_ptr = (gen_topo_module_t *)prev_out_port_ptr->gu.cmn.module_ptr;

   for (gu_input_port_list_t *in_port_list_ptr = prev_module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *prev_in_port_ptr     = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      in_port_ptr->nblc_end_ptr                   = NULL;
      gen_topo_input_port_t * new_nblc_end_ptr    = nblc_end_ptr;
      gen_topo_input_port_t * new_nblc_start_ptr  = NULL;
      gen_topo_input_port_t **new_nblc_start_pptr = nblc_start_pptr; // one location per nblc

      if (topo_ptr->gen_topo_vtable_ptr->is_port_at_nblc_end(&prev_module_ptr->gu, &prev_in_port_ptr->common))
      {
         prev_module_ptr->flags.is_nblc_boundary_module = TRUE;
#ifdef NBLC_DEBUG
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "Module 0x%x is an NBLC boundary module.",
                  prev_module_ptr->gu.module_instance_id);
#endif

         in_port_ptr->nblc_end_ptr = nblc_end_ptr;
         *nblc_start_pptr          = in_port_ptr;
         new_nblc_end_ptr          = prev_in_port_ptr;    // new nblc end
         new_nblc_start_pptr       = &new_nblc_start_ptr; // will be determined after recursion.
      }

      gen_topo_assign_nblc_input_recursive(topo_ptr,
                                           prev_in_port_ptr,
                                           new_nblc_end_ptr,
                                           new_nblc_start_pptr,
                                           recurse_depth_ptr);

      prev_in_port_ptr->nblc_start_ptr = *new_nblc_start_pptr;
      prev_in_port_ptr->nblc_end_ptr   = new_nblc_end_ptr;
#ifdef NBLC_DEBUG
      PRINT_NBLC(prev_in_port_ptr->nblc_start_ptr, prev_in_port_ptr, prev_in_port_ptr->nblc_end_ptr, "NBLC_IP");
#endif
   }

   RECURSION_ERROR_CHECK_ON_FN_EXIT(topo_ptr->gu.log_id, recurse_depth_ptr);
   return result;
}

/**
   NBLC output propagation is needed to find the end of nblc from a input boundary module.

   Propagation starts from output port of input boundary modules.
   A module is considered input boundary if its has,
       1) ext input port
       2) source module.

   The input boundary module's output port is start [nblc_start] of NBLC chain and is propagated downstream.
   The propagation ends if next module is MIMO/buffering module/ext output.

   When nblc_end is reached nblc_end is assigned to nblc_start port. And a new propagation starts from the next module.

   [nblc_start] -> .... <nblc_chain> ... -> [cur mod]-[out_port_ptr] ->
     -> [next_in_port_ptr]-[next_module_ptr]-[next_out_port_ptr]
 */
static ar_result_t gen_topo_assign_nblc_output_recursive(gen_topo_t *            topo_ptr,
                                                         gen_topo_output_port_t *out_port_ptr,
                                                         gen_topo_output_port_t *nblc_start_ptr,
                                                         uint32_t *              recurse_depth_ptr)
{
   ar_result_t result = AR_EOK;

   RECURSION_ERROR_CHECK_ON_FN_ENTRY(topo_ptr->gu.log_id, recurse_depth_ptr, NBLC_RECURSE_MAX_DEPTH);

   out_port_ptr->nblc_start_ptr = nblc_start_ptr;

   // ext-output cases, propagation stops assign nblc_end to the starting port of the chain.
   if (out_port_ptr->gu.ext_out_port_ptr)
   {
      nblc_start_ptr->nblc_end_ptr = out_port_ptr;

      RECURSION_ERROR_CHECK_ON_FN_EXIT(topo_ptr->gu.log_id, recurse_depth_ptr);
      return result;
   }

   // if the next modules is buffering or MIMO or Sink module nblc start ptr propagation stops here.
   // And a new nblc start ptr is propagated from the next module's output ports.
   gen_topo_input_port_t *next_in_port_ptr = (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr;
   gen_topo_module_t *    next_module_ptr  = (gen_topo_module_t *)next_in_port_ptr->gu.cmn.module_ptr;
   if ((next_module_ptr->gu.num_output_ports == 0) ||
       topo_ptr->gen_topo_vtable_ptr->is_port_at_nblc_end(&next_module_ptr->gu, &next_in_port_ptr->common))
   {
      next_module_ptr->flags.is_nblc_boundary_module = TRUE;

#ifdef NBLC_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "Module 0x%lX is an NBLC boundary module.",
               next_module_ptr->gu.module_instance_id);
#endif

      // assign current output port as the nblc end to the nblc_start port.
      nblc_start_ptr->nblc_end_ptr = out_port_ptr;

      // nblc ends here, so make it NULL.
      nblc_start_ptr = NULL;
   }

   // Iterate through the next modules output ports and continue propagation.
   for (gu_output_port_list_t *out_port_list_ptr = next_module_ptr->gu.output_port_list_ptr;
        (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_output_port_t *next_out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      // If propagation ended before this module, start new propagation from this module.
      gen_topo_output_port_t *new_nblc_start_ptr = nblc_start_ptr ? nblc_start_ptr : next_out_port_ptr;

      gen_topo_assign_nblc_output_recursive(topo_ptr, next_out_port_ptr, new_nblc_start_ptr, recurse_depth_ptr);

      next_out_port_ptr->nblc_end_ptr = new_nblc_start_ptr->nblc_end_ptr;

#ifdef NBLC_DEBUG
      PRINT_NBLC(next_out_port_ptr->nblc_start_ptr, next_out_port_ptr, next_out_port_ptr->nblc_end_ptr, "NBLC_OP");
#endif
   }

   RECURSION_ERROR_CHECK_ON_FN_EXIT(topo_ptr->gu.log_id, recurse_depth_ptr);
   return result;
}

/**
 * 1. starts from the end of topo (ext port or sink modules) and goes backwards to find assign the end of
 *  non-buffering-linear-chains to the beginning ports.
 *
 * 2. starts from the start of the topo (ext-input and source modules).
 */
ar_result_t gen_topo_assign_non_buf_lin_chains(gen_topo_t *topo_ptr)
{
   ar_result_t result = AR_EOK;

   INIT_EXCEPTION_HANDLING

   VERIFY(result, NULL != topo_ptr->gen_topo_vtable_ptr->is_port_at_nblc_end);

   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         // is-output-boundary-module? sink module and modules connected to ext ports are output side boundary modules.
         bool_t is_output_boundary_module = (0 == module_ptr->gu.num_output_ports);

         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
              (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
            if (out_port_ptr->gu.ext_out_port_ptr)
            {
               is_output_boundary_module = TRUE;
               break;
            }
         }

         // Module is considered input boundary if source module and modules connected to ext input ports.
         bool_t is_input_boundary_module = (0 == module_ptr->gu.num_input_ports);

         for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
              LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
            if (in_port_ptr->gu.ext_in_port_ptr)
            {
               is_input_boundary_module = TRUE;
               break;
            }
         }

         if (!is_output_boundary_module && !is_input_boundary_module)
         {
            // skip non-boundary modules as they are covered as part of propagation below.
            continue;
         }

         if (is_output_boundary_module)
         {
            /**
             * boundary module's input ports are ends of a any chain
             */
            for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr;
                 (NULL != in_port_list_ptr);
                 LIST_ADVANCE(in_port_list_ptr))
            {
               gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
               in_port_ptr->nblc_end_ptr          = NULL;
               gen_topo_input_port_t *nblc_end_ptr =
                  topo_ptr->gen_topo_vtable_ptr->is_port_at_nblc_end(&module_ptr->gu, &in_port_ptr->common)
                     ? in_port_ptr
                     : NULL;

               uint32_t               recurse_depth  = 0;
               gen_topo_input_port_t *nblc_start_ptr = NULL;
               gen_topo_assign_nblc_input_recursive(topo_ptr,
                                                    in_port_ptr,
                                                    nblc_end_ptr,
                                                    &nblc_start_ptr,
                                                    &recurse_depth);

               in_port_ptr->nblc_start_ptr = nblc_start_ptr;
#ifdef NBLC_DEBUG
               PRINT_NBLC(in_port_ptr->nblc_start_ptr, in_port_ptr, in_port_ptr->nblc_end_ptr, "NBLC_IP");
#endif
            }
         }

         if (is_input_boundary_module)
         {
            /* Input boundary module is beginning of the NBLC chain.
             * Recursively propagate start ptr till the nblc end, and assign the end_ptr to input boundary
             * module at the end of nblc. */
            for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
                 (NULL != out_port_list_ptr);
                 LIST_ADVANCE(out_port_list_ptr))
            {
               gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

               uint32_t recurse_depth = 0;
               gen_topo_assign_nblc_output_recursive(topo_ptr,
                                                     out_port_ptr,
                                                     out_port_ptr, // nblc_start_ptr
                                                     &recurse_depth);

#ifdef NBLC_DEBUG
               PRINT_NBLC(out_port_ptr->nblc_start_ptr, out_port_ptr, out_port_ptr->nblc_end_ptr, "NBLC_OP");
#endif
            }
         }

         // external, source and sink modules are at nblc boundary.
         module_ptr->flags.is_nblc_boundary_module = TRUE;

#ifdef NBLC_DEBUG
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "Module 0x%x is an NBLC boundary module.",
                  module_ptr->gu.module_instance_id);
#endif
      }
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}

capi_err_t gen_topo_check_and_update_dynamic_inplace(gen_topo_module_t *module_ptr)
{
   gen_topo_t *topo_ptr = (gen_topo_t *)module_ptr->topo_ptr;

   if (!module_ptr->flags.dynamic_inplace)
   {
      return CAPI_EOK;
   }

   // if module changes itself to inplace following conditions have to be met.
   // 1. Must be SISO, checked runtime, we cannot check here, if port is getting closed num_input_ports will not be
   //    updated yet. it will be updated at the end of close handling.
   // 2. Must have same input and output mf
   // 3. Must have same port threshold.
   if (module_ptr->gu.num_input_ports != 1 || module_ptr->gu.num_output_ports != 1)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX cannot be dynamic inplace, if its not SISO ",
               module_ptr->gu.module_instance_id);
      goto __bailtout;
   }

   // check ip/op mf
   gen_topo_input_port_t * in_port_ptr  = (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr;
   if (tu_has_media_format_changed(in_port_ptr->common.media_fmt_ptr, out_port_ptr->common.media_fmt_ptr))
   {
      if (in_port_ptr->common.flags.is_mf_valid && out_port_ptr->common.flags.is_mf_valid)
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX cannot be dynamic inplace, if media fmt is not same for ip & op. ",
                  module_ptr->gu.module_instance_id);
         goto __bailtout;
      }
   }

   if (gen_topo_get_curr_port_threshold(&in_port_ptr->common) !=
       gen_topo_get_curr_port_threshold(&out_port_ptr->common))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX cannot be dynamic inplace, if threshold is not same ",
               module_ptr->gu.module_instance_id);
      goto __bailtout;
   }

   return CAPI_EOK;

__bailtout:
   module_ptr->flags.pending_dynamic_inplace = FALSE;
   GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, dynamic_inplace_change);
   return CAPI_EFAILED;
}

spf_data_format_t gen_topo_convert_public_data_fmt_to_spf_data_format(uint32_t data_format)
{
   const spf_data_format_t mapper[] = {
      SPF_UNKNOWN_DATA_FORMAT,
      SPF_FIXED_POINT,         // DATA_FORMAT_FIXED_POINT = 1
      SPF_IEC61937_PACKETIZED, // DATA_FORMAT_IEC61937_PACKETIZED = 2
      SPF_IEC60958_PACKETIZED, // DATA_FORMAT_IEC60958_PACKETIZED = 3
      SPF_DSD_DOP_PACKETIZED,  // DATA_FORMAT_DSD_OVER_PCM = 4
      SPF_GENERIC_COMPRESSED,  // DATA_FORMAT_GENERIC_COMPRESSED = 5
      SPF_RAW_COMPRESSED,      // DATA_FORMAT_RAW_COMPRESSED = 6
      SPF_UNKNOWN_DATA_FORMAT,
      SPF_UNKNOWN_DATA_FORMAT, // To be added: SPF_COMPR_OVER_PCM_PACKETIZED,
      SPF_FLOATING_POINT,      // DATA_FORMAT_FLOATING_POINT = 9
   };
   if (data_format >= SIZE_OF_ARRAY(mapper))
   {
      return SPF_UNKNOWN_DATA_FORMAT;
   }
   else
   {
      return mapper[data_format];
   }
}

uint32_t gen_topo_convert_spf_data_fmt_public_data_format(spf_data_format_t spf_fmt)
{
   uint32_t index = 32 - s32_cl0_s32(spf_fmt); // SPF_FIXED_POINT = 1<<0. Leading zeros is 31. index is 1.

   const uint32_t mapper[] = {
      0,                               // SPF_UNKNOWN_DATA_FORMAT = 0,
      DATA_FORMAT_FIXED_POINT,         // SPF_FIXED_POINT = 1,
      DATA_FORMAT_FLOATING_POINT,      // SPF_FLOATING_POINT = 2,
      DATA_FORMAT_RAW_COMPRESSED,      // SPF_RAW_COMPRESSED = 3,
      DATA_FORMAT_IEC61937_PACKETIZED, // SPF_IEC61937_PACKETIZED = 4,
      DATA_FORMAT_DSD_OVER_PCM,        // SPF_DSD_DOP_PACKETIZED = 5,
      0,                               // SPF_COMPR_OVER_PCM_PACKETIZED = 6,
      DATA_FORMAT_GENERIC_COMPRESSED,  // SPF_GENERIC_COMPRESSED = 7,
      0,                               // SPF_IEC60958_PACKETIZED = 8,
      0,                               // SPF_IEC60958_PACKETIZED_NON_LINEAR = 9,
      0,                               // SPF_DEINTERLEAVED_RAW_COMPRESSED = 10,
      0,                               // SPF_MAX_FORMAT_TYPE = 0x7FFFFFFF
   };

   if (index >= SIZE_OF_ARRAY(mapper))
   {
      return 0;
   }
   else
   {
      return mapper[index];
   }
}

// clang-format on

topo_interleaving_t gen_topo_convert_public_interleaving_to_gen_topo_interleaving(uint16_t pcm_interleaving)
{
   topo_interleaving_t topo_interleaving = TOPO_INTERLEAVING_UNKNOWN;
   switch (pcm_interleaving)
   {
      case PCM_INTERLEAVED:
         topo_interleaving = TOPO_INTERLEAVED;
         break;
      case PCM_DEINTERLEAVED_PACKED:
         topo_interleaving = TOPO_DEINTERLEAVED_PACKED;
         break;
      case PCM_DEINTERLEAVED_UNPACKED:
         topo_interleaving = TOPO_DEINTERLEAVED_UNPACKED;
         break;
      default:
         break;
   }
   return topo_interleaving;
}

uint16_t gen_topo_convert_gen_topo_interleaving_to_public_interleaving(topo_interleaving_t topo_interleaving)
{
   uint16_t pcm_interleaving = PCM_INTERLEAVED;
   switch (topo_interleaving)
   {
      case TOPO_INTERLEAVED:
         pcm_interleaving = PCM_INTERLEAVED;
         break;
      case TOPO_DEINTERLEAVED_PACKED:
         pcm_interleaving = PCM_DEINTERLEAVED_PACKED;
         break;
      case TOPO_DEINTERLEAVED_UNPACKED:
         pcm_interleaving = PCM_DEINTERLEAVED_UNPACKED;
         break;
      case TOPO_DEINTERLEAVED_UNPACKED_V2:
         pcm_interleaving = PCM_DEINTERLEAVED_UNPACKED;
      default:
         break;
   }
   return pcm_interleaving;
}

topo_endianness_t gen_topo_convert_public_endianness_to_gen_topo_endianness(uint16_t pcm_endianness)
{
   topo_endianness_t gen_topo_endianness = TOPO_UNKONWN_ENDIAN;
   switch (pcm_endianness)
   {
      case PCM_LITTLE_ENDIAN:
         gen_topo_endianness = TOPO_LITTLE_ENDIAN;
         break;
      case PCM_BIG_ENDIAN:
         gen_topo_endianness = TOPO_BIG_ENDIAN;
         break;
      default:
         break;
   }
   return gen_topo_endianness;
}

uint16_t gen_topo_convert_gen_topo_endianness_to_public_endianness(topo_endianness_t gen_topo_endianness)
{
   uint16_t pcm_endianness = 0;
   switch (gen_topo_endianness)
   {
      case TOPO_LITTLE_ENDIAN:
         pcm_endianness = PCM_LITTLE_ENDIAN;
         break;
      case TOPO_BIG_ENDIAN:
         pcm_endianness = PCM_BIG_ENDIAN;
         break;
      default:
         break;
   }
   return pcm_endianness;
}

/**
 * index by capiv2 data format (index cannot be MAX_FORMAT_TYPE)
 */
spf_data_format_t gen_topo_convert_capi_data_format_to_spf_data_format(data_format_t capi_data_format)
{
   if (capi_data_format > CAPI_DEINTERLEAVED_RAW_COMPRESSED)
   {
      return SPF_UNKNOWN_DATA_FORMAT;
   }
   else
   {
      const spf_data_format_t mapper[] = { SPF_FIXED_POINT,
                                           SPF_FLOATING_POINT,
                                           SPF_RAW_COMPRESSED,
                                           SPF_IEC61937_PACKETIZED,
                                           SPF_DSD_DOP_PACKETIZED,
                                           SPF_COMPR_OVER_PCM_PACKETIZED,
                                           SPF_GENERIC_COMPRESSED,
                                           SPF_IEC60958_PACKETIZED,
                                           SPF_IEC60958_PACKETIZED_NON_LINEAR,
                                           SPF_DEINTERLEAVED_RAW_COMPRESSED };

      return mapper[capi_data_format];
   }
}

capi_interleaving_t gen_topo_convert_gen_topo_interleaving_to_capi_interleaving(topo_interleaving_t gen_topo_int)
{
   capi_interleaving_t capi_int = CAPI_INVALID_INTERLEAVING;
   switch (gen_topo_int)
   {
      case TOPO_INTERLEAVED:
         capi_int = CAPI_INTERLEAVED;
         break;
      case TOPO_DEINTERLEAVED_PACKED:
         capi_int = CAPI_DEINTERLEAVED_PACKED;
         break;
      case TOPO_DEINTERLEAVED_UNPACKED:
         capi_int = CAPI_DEINTERLEAVED_UNPACKED;
         break;
      case TOPO_DEINTERLEAVED_UNPACKED_V2:
         capi_int = CAPI_DEINTERLEAVED_UNPACKED_V2;
         break;
      default:
         break;
   }

   return capi_int;
}

topo_interleaving_t gen_topo_convert_capi_interleaving_to_gen_topo_interleaving(capi_interleaving_t capi_int)
{
   topo_interleaving_t gen_topo_int = TOPO_INTERLEAVING_UNKNOWN;
   switch (capi_int)
   {
      case CAPI_INTERLEAVED:
         gen_topo_int = TOPO_INTERLEAVED;
         break;
      case CAPI_DEINTERLEAVED_PACKED:
         gen_topo_int = TOPO_DEINTERLEAVED_PACKED;
         break;
      case CAPI_DEINTERLEAVED_UNPACKED:
         gen_topo_int = TOPO_DEINTERLEAVED_UNPACKED;
         break;
      case CAPI_DEINTERLEAVED_UNPACKED_V2:
         gen_topo_int = TOPO_DEINTERLEAVED_UNPACKED_V2;
         break;
      default:
         break;
   }

   return gen_topo_int;
}

ar_result_t gen_topo_handle_pending_dynamic_inplace_change(gen_topo_t *                topo_ptr,
                                                           gen_topo_capi_event_flag_t *capi_event_flag_ptr)
{
   if (capi_event_flag_ptr->dynamic_inplace_change)
   {
      return AR_EOK;
   }

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(topo_ptr->gu.log_id, DBG_LOW_PRIO, "Handling dynamic inplace change event. ");
#endif

   // check and update modules dynamic inplace flag, if there is a pending event.
   for (gu_module_list_t *module_list_ptr = topo_ptr->gu.sorted_module_list_ptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

      if (module_ptr->flags.pending_dynamic_inplace == module_ptr->flags.dynamic_inplace)
      {
         continue;
      }

      // dynamic inplace changed for the module
      module_ptr->flags.dynamic_inplace = module_ptr->flags.pending_dynamic_inplace;
   }

   // mark force return and return the buffers is data is not present, this will allow reassigning
   // the static buffers in nblc path properly whenever inplace event is raised.
   gen_topo_mark_buf_mgr_buffers_to_force_return(topo_ptr);

   // clear the event
   capi_event_flag_ptr->dynamic_inplace_change = FALSE;

   return AR_EOK;
}

ar_result_t gen_topo_aggregate_hw_acc_proc_delay(gen_topo_t *topo_ptr,
                                                 bool_t      only_aggregate,
                                                 uint32_t *  aggregate_hw_acc_proc_delay_ptr)
{
   uint32_t aggregate_hw_acc_proc_delay = 0;
   bool_t   need_to_ignore_state        = only_aggregate;

   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         // Why not checking data flow state? see gen_cntr_pm gen_cntr_vote_pm_conditionally
         if (gen_topo_is_module_active(module_ptr, need_to_ignore_state))
         {
            aggregate_hw_acc_proc_delay += module_ptr->hw_acc_proc_delay;
         }
      }
   }

   *aggregate_hw_acc_proc_delay_ptr = aggregate_hw_acc_proc_delay;

   return AR_EOK;
}

// assign default media format to the newly created module's ports.
void gen_topo_set_default_media_fmt_at_open(gen_topo_t *topo_ptr)
{
   SPF_MANAGE_CRITICAL_SECTION
   gu_t *open_gu_ptr = get_gu_ptr_for_current_command_context(&topo_ptr->gu);

   for (gu_sg_list_t *sg_list_ptr = open_gu_ptr->sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      // only update the new SGs, if a new port is opened for an existing module then it will be updated in
      // gen_topo_init_set_get_data_port_properties
      if (GU_STATUS_NEW != sg_list_ptr->sg_ptr->gu_status)
      {
         continue;
      }

      SPF_CRITICAL_SECTION_START(&topo_ptr->gu);
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         for (gu_input_port_list_t *input_port_list_ptr = module_list_ptr->module_ptr->input_port_list_ptr;
              (NULL != input_port_list_ptr);
              LIST_ADVANCE(input_port_list_ptr))
         {
            gen_topo_input_port_t *ip_port_ptr = (gen_topo_input_port_t *)(input_port_list_ptr->ip_port_ptr);
            tu_set_media_fmt(&topo_ptr->mf_utils, &ip_port_ptr->common.media_fmt_ptr, NULL, topo_ptr->heap_id);
         }

         for (gu_output_port_list_t *output_port_list_ptr = module_list_ptr->module_ptr->output_port_list_ptr;
              (NULL != output_port_list_ptr);
              LIST_ADVANCE(output_port_list_ptr))
         {
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)(output_port_list_ptr->op_port_ptr);
            tu_set_media_fmt(&topo_ptr->mf_utils, &out_port_ptr->common.media_fmt_ptr, NULL, topo_ptr->heap_id);
         }
      }
      SPF_CRITICAL_SECTION_END(&topo_ptr->gu);
   }
}

posal_pm_island_vote_t gen_topo_aggregate_island_vote(gen_topo_t *topo_ptr)
{
   posal_pm_island_vote_t aggregate_island_vote;
   aggregate_island_vote.is_valid         = TRUE;
   aggregate_island_vote.island_vote_type = PM_ISLAND_VOTE_ENTRY;
   aggregate_island_vote.island_type      = PM_ISLAND_TYPE_DEFAULT;

   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         if (gen_topo_is_module_active(module_ptr, FALSE))
         {
            if (GET_ACTUAL_HEAP_ID(module_ptr->gu.module_heap_id) ==
                GET_ACTUAL_HEAP_ID(posal_get_island_heap_id_v2(POSAL_ISLAND_HEAP_LLC)))
            {
               aggregate_island_vote.island_type = PM_ISLAND_TYPE_LOW_POWER_2;
            }

            if (PM_ISLAND_VOTE_EXIT == module_ptr->flags.voted_island_exit)
            {
               aggregate_island_vote.island_vote_type = PM_ISLAND_VOTE_EXIT;
               break;
            }
         }
      }
   }

   return aggregate_island_vote;
}

// caller's responsibility to take lock before calling this.
ar_result_t gen_topo_check_update_started_sorted_module_list(void *vtopo_ptr, bool_t b_force_update)
{
   gen_topo_t *topo_ptr = (gen_topo_t *)vtopo_ptr;
   ar_result_t result   = AR_EOK;

   if (b_force_update || GU_SORT_UPDATED == topo_ptr->gu.sort_status)
   {
      topo_ptr->gu.sort_status = GU_SORT_DEFAULT;

      if (!topo_ptr->gu.sorted_module_list_ptr)
      {
         return AR_EOK;
      }

      spf_list_delete_list((spf_list_node_t **)&topo_ptr->started_sorted_module_list_ptr, TRUE);

      for (gu_module_list_t *module_list_ptr = topo_ptr->gu.sorted_module_list_ptr; module_list_ptr;
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_sg_t *sg_ptr = (gen_topo_sg_t *)module_list_ptr->module_ptr->sg_ptr;

         if (TOPO_SG_STATE_STARTED == sg_ptr->state)
         {
            result |= spf_list_insert_tail((spf_list_node_t **)&topo_ptr->started_sorted_module_list_ptr,
                                           (void *)module_list_ptr->module_ptr,
                                           topo_ptr->heap_id,
                                           TRUE);
         }
      }

      if (AR_FAILED(result))
      {
         TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "failed in creating started_sorted_module_list %x.", result);
         spf_list_delete_list((spf_list_node_t **)&topo_ptr->started_sorted_module_list_ptr, TRUE);
      }
   }
   return result;
}
