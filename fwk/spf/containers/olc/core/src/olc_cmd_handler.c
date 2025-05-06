/**
 * \file olc_cmd_handler.c
 * \brief
 *     This file contains olc functions for command handling.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "olc_driver.h"
#include "olc_i.h"
#include "apm.h"

/* =======================================================================
Static Function Definitions
========================================================================== */
/**
 * called for all use cases. both for internal and external clients.
 */

ar_result_t olc_handle_port_data_thresh_change_event(void *ctx_ptr)
{
   olc_t                      *me_ptr              = (olc_t *)ctx_ptr;
   ar_result_t                 result              = AR_EOK;
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;

   INIT_EXCEPTION_HANDLING
   /** Threshold prop is by default complete.
    * When it's not possible to complete due to absence of media fmt,
    * callees of this func will set it to TRUE.
    * if we clear port_thresh event flag, then buffers will never get created.*/
   // bool_t thresh_prop_not_complete = FALSE;

   // no reconciliation needed for OLC
   // no reconciliation needed for OLC
   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, &me_ptr->topo, FALSE /*do_reconcile*/);

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_MED_PRIO,
           " in olc_handle_port_data_thresh_change_event. thresh event %u, media_fmt_event %u",
           capi_event_flag_ptr->port_thresh,
           capi_event_flag_ptr->media_fmt_event);

   // if (!(((0 == lcm_threshold.sample_rate) && (0 == lcm_threshold.thresh_samples)) || (0 ==
   // lcm_threshold.thresh_us)))
   {
      icb_frame_length_t fm = {
         .sample_rate = 0, .frame_len_samples = 0, .frame_len_us = me_ptr->configured_frame_size_us,
      };

      TRY(result, cu_handle_frame_len_change(&me_ptr->cu, &fm, me_ptr->cu.period_us));

      olc_handle_frame_length_n_state_change(me_ptr, me_ptr->cu.cntr_frame_len.frame_len_us);
      olc_cu_update_path_delay(&me_ptr->cu, 0);
   }

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   OLC_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, " olc_handle_port_data_thresh_change_event complete");

   return result;
}

ar_result_t olc_input_media_format_received(void *                                  ctx_ptr,
                                            gu_ext_in_port_t *                      gu_ext_in_port_ptr,
                                            topo_media_fmt_t *                      media_fmt_ptr,
                                            cu_ext_in_port_upstream_frame_length_t *upstream_frame_len_ptr,
                                            bool_t                                  is_data_path)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t        result                = AR_EOK;
   olc_t *            me_ptr                = (olc_t *)ctx_ptr;
   olc_ext_in_port_t *ext_in_port_ptr       = (olc_ext_in_port_t *)gu_ext_in_port_ptr;
   bool_t             FORCE_AGGREGATE_FALSE = FALSE;
   bool_t             is_pack_pcm           = SPF_IS_PACKETIZED_OR_PCM(media_fmt_ptr->data_format);

   if (is_pack_pcm)
   {
      TOPO_PRINT_PCM_MEDIA_FMT(me_ptr->topo.gu.log_id, media_fmt_ptr, "container input");
   }
   else
   {
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_HIGH_PRIO,
              "Container input media format SPF_data_format: %lu, fmt_id: 0x%lX",
              media_fmt_ptr->data_format,
              media_fmt_ptr->fmt_id);
   }

   if (SPF_DEINTERLEAVED_RAW_COMPRESSED == media_fmt_ptr->data_format)
   {
      AR_MSG(DBG_ERROR_PRIO, "Received Deinterleaved Raw Compressed data");
      return AR_EFAILED;
   }

   // update total_bytes, such that duration remains constant.
   if (tu_has_media_format_changed(&ext_in_port_ptr->cu.media_fmt, media_fmt_ptr))
   {
      if (0 != ext_in_port_ptr->bytes_from_prev_buf)
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_ERROR_PRIO,
                 "When media format changed, expect bytes_from_prev_buf to be zero.");
      }
   }

   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   // Update total_bytes, such that duration remains constant.
   if (tu_has_media_format_changed(&ext_in_port_ptr->cu.media_fmt, media_fmt_ptr))
   {

      topo_port_state_t in_port_sg_state;

      // Copy incoming media format as the actual media format of the external input port.
      tu_copy_media_fmt(&ext_in_port_ptr->cu.media_fmt, media_fmt_ptr);

      // Copy external input port media fmt to internal.

      TRY(result, gen_topo_set_input_port_media_format(&me_ptr->topo, in_port_ptr, media_fmt_ptr));

      // media format propagation depends on sg state not on port state because port state depends on downstream as
      // well. (due to propagation)
      in_port_sg_state = topo_sg_state_to_port_state(gen_topo_get_sg_state(in_port_ptr->gu.cmn.module_ptr->sg_ptr));

      /**
       * Control path:
       * Propagate control path media fmt only if the SG is in prepare state.
       *    If SG is in start state, then data path takes care of propagation.
       *    If SG is in stop state, then handle_prepare cmd will take care.
       * Data path:
       * Propagate data path media fmt only if the SG started.
       */
      if ((!is_data_path && (TOPO_PORT_STATE_PREPARED == in_port_sg_state)) ||
          (is_data_path && (TOPO_PORT_STATE_STARTED == in_port_sg_state)))
      {
         gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;

         // no reconciliation needed for OLC
         GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
         GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr,
                                                             &me_ptr->topo,
                                                             FALSE /*do_reconcile*/);

         spdm_handle_input_media_format_update(&me_ptr->spgm_info,
                                               (void *)media_fmt_ptr,
                                               ext_in_port_ptr->wdp_ctrl_cfg_ptr->sdm_port_index,
                                               is_data_path);
         TRY(result, olc_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE));
         TRY(result, olc_handle_clk_vote_change(me_ptr, CU_PM_REQ_KPPS_BW, FORCE_AGGREGATE_FALSE, NULL, NULL));
         TRY(result, me_ptr->cu.cntr_vtbl_ptr->port_data_thresh_change(&me_ptr->cu));

         capi_event_flag_ptr->media_fmt_event = FALSE;
         capi_event_flag_ptr->process_state   = FALSE;
      }
   }

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

static ar_result_t olc_post_operate_on_connected_input(olc_t *                    me_ptr,
                                                       gen_topo_output_port_t *   out_port_ptr,
                                                       spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                                       uint32_t                   sg_ops)
{
   ar_result_t            result           = AR_EOK;
   gen_topo_module_t *    module_ptr       = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;
   gen_topo_input_port_t *conn_in_port_ptr = (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr;
   if (conn_in_port_ptr)
   {
      gen_topo_sg_t *other_sg_ptr = (gen_topo_sg_t *)conn_in_port_ptr->gu.cmn.module_ptr->sg_ptr;
      if (!gu_is_sg_id_found_in_spf_array(spf_sg_list_ptr, other_sg_ptr->gu.id))
      {
         // see notes in gen_cntr_post_operate_on_ext_in_port
         if (((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops) &&
             (TOPO_PORT_STATE_STARTED == conn_in_port_ptr->common.state) &&
             (TOPO_DATA_FLOW_STATE_AT_GAP != conn_in_port_ptr->common.data_flow_state))
         {
            // If sg op is,
            // STOP/FLUSH - insert internal flushing eos
            // SUSPEND - insert DFG
            uint32_t is_upstream_realtime = FALSE;
            gen_topo_get_port_property(&me_ptr->topo,
                                       TOPO_DATA_OUTPUT_PORT_TYPE,
                                       PORT_PROPERTY_IS_UPSTREAM_RT,
                                       out_port_ptr,
                                       &is_upstream_realtime);

            /* If upstream is real-time we can avoid sending eos downstream for FLUSH command since this implies
            upstream is in STARTED state and will be pumping data in real time. If we flush while upstream is in
            STARTED state, it might lead to an infinite loop since data is dropped while new data is coming in.
            In current cases, when we seek, upstream sg is flushed and spr will be pumping 0's downstream which flushes
            out the device leg while ensuring there are no endpoint underruns. Upstream rt check will not work in cases
            where timestamp is valid since it would lead to ts discontinuities due to dropping of data at ext inp of
            device leg.

            We still need eos to be sent when upstream is STOPPED, to flush out any data stuck in the device pipeline.
             */
            if ((TOPO_SG_OP_STOP & sg_ops) || ((TOPO_SG_OP_FLUSH & sg_ops) && (is_upstream_realtime == FALSE)))
            {
               bool_t INPUT_PORT_ID_NONE             = 0; // Internal input ports don't have unique ids. Use 0 instead.
               module_cmn_md_eos_flags_t eos_md_flag = {.word = 0 };
               eos_md_flag.is_flushing_eos = TRUE;
               eos_md_flag.is_internal_eos = TRUE;

               uint32_t bytes_across_ch = gen_topo_get_total_actual_len(&conn_in_port_ptr->common);

               result = gen_topo_create_eos_for_cntr(&me_ptr->topo,
                                                     (gen_topo_input_port_t *)conn_in_port_ptr,
                                                     INPUT_PORT_ID_NONE,
                                                     me_ptr->cu.heap_id,
                                                     &conn_in_port_ptr->common.sdata.metadata_list_ptr,
                                                     NULL,         /* md_flag_ptr */
                                                     NULL,         /*tracking_payload_ptr*/
                                                     &eos_md_flag, /* eos_payload_flags */
                                                     bytes_across_ch,
                                                     conn_in_port_ptr->common.media_fmt_ptr);

               if (AR_SUCCEEDED(result))
               {
                  me_ptr->topo.flags.process_us_gap               = TRUE;
                  conn_in_port_ptr->common.sdata.flags.marker_eos = TRUE;
               }

               OLC_MSG(me_ptr->topo.gu.log_id,
                       DBG_MED_PRIO,
                       "MD_DBG: EOS set at SG boundary within container (0x%lX, 0x%lx) to (0x%lX, 0x%lx), result %ld",
                       module_ptr->gu.module_instance_id,
                       out_port_ptr->gu.cmn.id,
                       conn_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                       conn_in_port_ptr->gu.cmn.id,
                       result);

               // for internal ports, flushing EOS needs zeros, which must be assigned here.
               gen_topo_set_pending_zeros((gen_topo_module_t *)conn_in_port_ptr->gu.cmn.module_ptr, conn_in_port_ptr);
            }
            else if (TOPO_SG_OP_SUSPEND & sg_ops) // insert DFG
            {
               uint32_t         bytes_across_all_ch = gen_topo_get_total_actual_len(&conn_in_port_ptr->common);
               module_cmn_md_t *out_md_ptr          = NULL;
               result                               = gen_topo_create_dfg_metadata(&me_ptr->topo,
                                                     &conn_in_port_ptr->common.sdata.metadata_list_ptr,
                                                     me_ptr->cu.heap_id,
                                                     &out_md_ptr,
                                                     bytes_across_all_ch,
                                                     conn_in_port_ptr->common.media_fmt_ptr);
               if (out_md_ptr)
               {
                  // set the flag to call process frames and propagate DFG out
                  me_ptr->topo.flags.process_us_gap = TRUE;

                  OLC_MSG(me_ptr->topo.gu.log_id,
                          DBG_HIGH_PRIO,
                          "MD_DBG: DFG set at SG boundary within container (0x%lX, 0x%lx) to (0x%lX, 0x%lx) at "
                          "offset %lu, result "
                          "%ld",
                          module_ptr->gu.module_instance_id,
                          out_port_ptr->gu.cmn.id,
                          conn_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                          conn_in_port_ptr->gu.cmn.id,
                          out_md_ptr->offset,
                          result);
               }
            }
         }
      }
   }
   return result;
}

ar_result_t olc_post_operate_on_subgraph(void *                     base_ptr,
                                         uint32_t                   sg_ops,
                                         topo_sg_state_t            sg_state,
                                         gu_sg_t *                  gu_sg_ptr,
                                         spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   ar_result_t    result = AR_EOK;
   olc_t *        me_ptr = (olc_t *)base_ptr;
   gen_topo_sg_t *sg_ptr = (gen_topo_sg_t *)gu_sg_ptr;

   for (gu_module_list_t *module_list_ptr = sg_ptr->gu.module_list_ptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         // Check if connected port is not in a subgraph being operated on.
         olc_post_operate_on_connected_input(me_ptr, out_port_ptr, spf_sg_list_ptr, sg_ops);
      }
   }

   return result;
}

/**
 *
 * sg_ptr->state is changed in the caller only after all operations are successful.
 */
ar_result_t olc_operate_on_subgraph(void *                     base_ptr,
                                    uint32_t                   sg_ops,
                                    topo_sg_state_t            sg_state,
                                    gu_sg_t *                  gu_sg_ptr,
                                    spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t    result = AR_EOK;
   olc_t *        me_ptr = (olc_t *)base_ptr;
   gen_topo_sg_t *sg_ptr = (gen_topo_sg_t *)gu_sg_ptr;

   VERIFY(result, me_ptr->cu.topo_vtbl_ptr && me_ptr->cu.topo_vtbl_ptr->operate_on_modules);

   TRY(result,
       me_ptr->cu.topo_vtbl_ptr->operate_on_modules((void *)&me_ptr->topo,
                                                    sg_ops,
                                                    sg_ptr->gu.module_list_ptr,
                                                    spf_sg_list_ptr));

   for (gu_module_list_t *module_list_ptr = sg_ptr->gu.module_list_ptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         if (out_port_ptr->gu.ext_out_port_ptr)
         {
            // Handles the case where both ends of this connection belong to the same SG.
            TRY(result,
                me_ptr->cu.cntr_vtbl_ptr->operate_on_ext_out_port(&me_ptr->cu,
                                                                  sg_ops,
                                                                  &out_port_ptr->gu.ext_out_port_ptr,
                                                                  TRUE /* is_self_sg */));

            // if (ext_out_port_ptr->gu.downstream_handle_ptr) : any ext connection is issued appropriate cmd by APM.
            // no container to container messaging necessary.
         }
      }

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         if (in_port_ptr->gu.ext_in_port_ptr)
         {
            // Handles the case where both ends of the connection from this ext in port belong to the same SG.
            TRY(result,
                me_ptr->cu.cntr_vtbl_ptr->operate_on_ext_in_port(&me_ptr->cu,
                                                                 sg_ops,
                                                                 &in_port_ptr->gu.ext_in_port_ptr,
                                                                 TRUE /* is_self_sg */));

            // if (ext_in_port_ptr->gu.upstream_handle_ptr) : any ext connection is issued appropriate cmd by APM.
            // no container to container messaging necessary.
         }
      }

      for (gu_ctrl_port_list_t *ctrl_port_list_ptr = module_ptr->gu.ctrl_port_list_ptr; (NULL != ctrl_port_list_ptr);
           LIST_ADVANCE(ctrl_port_list_ptr))
      {
         gu_ctrl_port_t *ctrl_port_ptr = (gu_ctrl_port_t *)ctrl_port_list_ptr->ctrl_port_ptr;

         if (ctrl_port_ptr->ext_ctrl_port_ptr)
         {
            // Handles the case where both ends of the ctrl connection from this ext in port belong to the same SG.
            TRY(result,
                me_ptr->cu.cntr_vtbl_ptr->operate_on_ext_ctrl_port(&me_ptr->cu,
                                                                   sg_ops,
                                                                   &ctrl_port_ptr->ext_ctrl_port_ptr,
                                                                   TRUE /* is_self_sg */));
         }
      }
   }

   // Operation was successful (did not go to CATCH). Apply new state.
   if (TOPO_SG_STATE_INVALID != sg_state)
   {
      sg_ptr->state = sg_state;
   }

   if (TOPO_SG_OP_CLOSE == sg_ops)
   {
      sg_ptr->gu.gu_status = GU_STATUS_CLOSING;
   }

   // do not set the sg_ops because OLC is not going to use this optimization properly
   // gu_sg_ptr->curr_sg_ops = sg_ops;

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/* CLOSE, FLUSH, STOP are handled in olc_operate_on_ext_in_port context.
 * START, PREPARE and SUSPEND is not handled here, its handled based on downgraded state in
 * olc_set_downgraded_state_on_input_port
 */
ar_result_t olc_operate_on_ext_in_port(void *             base_ptr,
                                       uint32_t           sg_ops,
                                       gu_ext_in_port_t **ext_in_port_pptr,
                                       bool_t             is_self_sg)
{
   ar_result_t        result          = AR_EOK;
   olc_t *            me_ptr          = (olc_t *)base_ptr;
   olc_ext_in_port_t *ext_in_port_ptr = (olc_ext_in_port_t *)*ext_in_port_pptr;

   bool_t is_disconnect_needed = ((TOPO_SG_OP_DISCONNECT & sg_ops) &&
                                  (cu_is_disconnect_ext_in_port_needed((cu_base_t *)base_ptr, &ext_in_port_ptr->gu)));

   bool_t is_reset_needed =
      is_disconnect_needed || ((is_self_sg) && ((TOPO_SG_OP_CLOSE | TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP) & sg_ops));

   if (TOPO_SG_OP_START & sg_ops)
   {
      if (ext_in_port_ptr->wdp_ctrl_cfg_ptr)
      {
         spdm_write_dl_pcd(&me_ptr->spgm_info, ext_in_port_ptr->wdp_ctrl_cfg_ptr->sdm_port_index);
      }
   }

   // Flush
   // 1. input if close/flush cmd is received or if Peer SG stopped.
   // 2. Flush if self SG/connected peer(US) is stopped.
   //
   // Note that if US is stopped and self is started we are flushing. We can ideally process the input buffers at
   // since self is started. But if we don't flush immediately, US CLOSE can be potentially delayed because it will
   // wait for the buffers.
   if (((TOPO_SG_OP_CLOSE | TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP) & sg_ops) || (is_disconnect_needed))
   {
      // For stop/flush operation, we need to preserve the MF (which is like stream associated MD)
      olc_flush_input_data_queue(me_ptr,
                                 ext_in_port_ptr,
                                 ((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP) & sg_ops) /* keep data msg */,
                                 (TOPO_SG_OP_FLUSH & sg_ops),
                                 FALSE);
   }

   // Only change the connected state of a port not in the current subgraph.
   if (!is_self_sg)
   {
      // inform module for ext conn (for self-sg case, operate_on_sg will take care or at inter-sg,intra-container
      // level)
      if (TOPO_SG_OP_CLOSE & sg_ops)
      {
         gen_topo_module_t *module_ptr = ((gen_topo_module_t *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr);
         if (module_ptr->capi_ptr) // for completeness. from existing design, will not get executed
         {
            gen_topo_input_port_t *in_port_ptr = ((gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr);
            result                             = gen_topo_capi_set_data_port_op_from_sg_ops(module_ptr,
                                                                sg_ops,
                                                                &in_port_ptr->common.last_issued_opcode,
                                                                TRUE,
                                                                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                                                                ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);
         }

         // If input port is still not at gap, it means that the EOS introduced at prev stop still didn't get a
         // chance to move out. that EOS will be removed by gen_cntr_ext_in_port_reset.
         // At close, as port will be destroyed, data-flow-state info will be lost if we don't do the below.
         // Insert new internal EOS at output if we are not at-gap.
         // Modules that support metadata propagation must insert EOS at port-close by themselves.
         // Framework doesn't take care of such modules.

         // it's ok to move EOS even if there's pending data or algo delay because upstream stop can algo-reset the
         // module.

         // only SISO modules support across module MD propagation.
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
         if (gen_topo_fwk_owns_md_prop(module_ptr) &&
             (TOPO_DATA_FLOW_STATE_AT_GAP != in_port_ptr->common.data_flow_state) &&
             module_ptr->gu.output_port_list_ptr)
         {
            uint32_t                INPUT_PORT_ID_NONE = 0; // The external input port will be closed, no need for ref.
            gen_topo_output_port_t *out_port_ptr =
               (gen_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr;
            module_cmn_md_eos_flags_t eos_md_flag = {.word = 0 };
            eos_md_flag.is_flushing_eos = TRUE;
            eos_md_flag.is_internal_eos = TRUE;

            uint32_t bytes_across_ch = gen_topo_get_total_actual_len(&out_port_ptr->common);
            // OLC_CA
            result = gen_topo_create_eos_for_cntr(&me_ptr->topo,
                                                  NULL, // for output ports we don't need container ref.
                                                  INPUT_PORT_ID_NONE,
                                                  me_ptr->cu.heap_id,
                                                  &out_port_ptr->common.sdata.metadata_list_ptr,
                                                  NULL,         /* md flags ptr */
                                                  NULL,         /* tracking payload ptr*/
                                                  &eos_md_flag, /* eos_payload_flags */
                                                  bytes_across_ch,
                                                  out_port_ptr->common.media_fmt_ptr);

            if (AR_SUCCEEDED(result))
            {
               out_port_ptr->common.sdata.flags.marker_eos = TRUE;
            }

            OLC_MSG(me_ptr->topo.gu.log_id,
                    DBG_MED_PRIO,
                    "MD_DBG: EOS moved to output port as part of close input (0x%lX, 0x%lx), result %ld",
                    module_ptr->gu.module_instance_id,
                    out_port_ptr->gu.cmn.id,
                    result);
         }

         // also removes EOS on input port. moving EOS from input is harder than creating new one (as there could be
         // multiple EOSes whose offsets may differ)
         is_reset_needed = TRUE;
      }
   }

   // we cannot reset port except in self stop/flush cases, as resetting puts the port in data flow state = at-gap.
   // we can reset only after EOS is propagated ( upstream to downstream)
   if (is_reset_needed)
   {
      olc_ext_in_port_reset(me_ptr, ext_in_port_ptr);
   }

   if (is_disconnect_needed)
   {
      ext_in_port_ptr->gu.upstream_handle.spf_handle_ptr = NULL;
   }

   // Stop listening to input,
   // 1. If the port is getting closed.
   // 2. IF the port received Self/Peer STOP.
   // 3. If self SG is getting suspended.
   bool_t stop_listen_mask = ((TOPO_SG_OP_CLOSE | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops);
   stop_listen_mask |= ((TOPO_SG_OP_SUSPEND & sg_ops) && is_self_sg);
   if (stop_listen_mask)
   {
		 cu_stop_listen_to_mask(&me_ptr->cu, ext_in_port_ptr->cu.bit_mask);
         if (ext_in_port_ptr->wdp_ctrl_cfg_ptr)
         {
            cu_stop_listen_to_mask(&me_ptr->cu, ext_in_port_ptr->wdp_ctrl_cfg_ptr->sat_rw_bit_mask);
         }
   }

   if (TOPO_SG_OP_CLOSE == sg_ops)
   {
      ext_in_port_ptr->gu.gu_status = GU_STATUS_CLOSING;
   }

   return result;
}

ar_result_t olc_post_operate_on_ext_in_port(void *                     base_ptr,
                                            uint32_t                   sg_ops,
                                            gu_ext_in_port_t **        ext_in_port_pptr,
                                            spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   ar_result_t            result          = AR_EOK;
   olc_t                 *me_ptr          = (olc_t *)base_ptr;
   olc_ext_in_port_t     *ext_in_port_ptr = (olc_ext_in_port_t *)*ext_in_port_pptr;
   gen_topo_input_port_t *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   /* If ext input port receives both a self and peer stop/flush from upstream (any order)
    * eos need not be set since next DS container will anyway get the eos because of this self stop
    *
    * EOS needs to be inserted only if this port is started (port started => self start && downstream start
    * and it not already at gap
    */
   if (((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops) &&
       (TOPO_PORT_STATE_STARTED == in_port_ptr->common.state) &&
       (TOPO_DATA_FLOW_STATE_AT_GAP != in_port_ptr->common.data_flow_state))
   {
      uint32_t is_upstream_realtime = FALSE;
      gen_topo_get_port_property(&me_ptr->topo,
                                 TOPO_DATA_INPUT_PORT_TYPE,
                                 PORT_PROPERTY_IS_UPSTREAM_RT,
                                 in_port_ptr,
                                 &is_upstream_realtime);

      /* If upstream is real-time we can avoid sending eos downstream for FLUSH command since this implies
      upstream is in STARTED state and will be pumping data in real time. If we flush while upstream is in
      STARTED state, it might lead to an infinite loop since data is dropped while new data is coming in.
      In current cases, when we seek, upstream sg is flushed and spr will be pumping 0's downstream which flushes
      out the device leg while ensuring there are no endpoint underruns. Upstream rt check will not work in cases
      where timestamp is valid since it would lead to ts discontinuities due to dropping of data at ext inp of
      device leg.

      We still need eos to be sent when upstream is STOPPED, to flush out any data stuck in the device pipeline.
       */
      if ((TOPO_SG_OP_STOP & sg_ops) || ((TOPO_SG_OP_FLUSH & sg_ops) && (is_upstream_realtime == FALSE)))

      {
         module_cmn_md_eos_flags_t flag = { .word = 0 };
         flag.is_flushing_eos           = TRUE;
         flag.is_internal_eos           = TRUE;

            spdm_process_send_wr_eos(&me_ptr->spgm_info, &flag, ext_in_port_ptr->wdp_ctrl_cfg_ptr->sdm_port_index);

         if (AR_SUCCEEDED(result))
         {
            me_ptr->topo.flags.process_us_gap = TRUE;
         }
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_LOW_PRIO,
                 "MD_DBG: Created EOS for ext in port (0x%0lX, 0x%lx) with result 0x%lx",
                 in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                 in_port_ptr->gu.cmn.id,
                 result);
      }
      else if (TOPO_SG_OP_SUSPEND & sg_ops)
      {
         module_cmn_md_t *out_md_ptr = NULL;
         result                      = gen_topo_create_dfg_metadata(&me_ptr->topo,
                                               &ext_in_port_ptr->md_list_ptr,
                                               me_ptr->cu.heap_id,
                                               &out_md_ptr,
                                               ext_in_port_ptr->buf.actual_data_len,
                                               &ext_in_port_ptr->cu.media_fmt);
         if (out_md_ptr)
         {
            // set the flag to call process frames and propagate DFG out
            me_ptr->topo.flags.process_us_gap = TRUE;

            OLC_MSG(me_ptr->topo.gu.log_id,
                    DBG_HIGH_PRIO,
                    "MD_DBG: Inserted DFG at ext in port (0x%lX, 0x%lx) at offset %lu, result %ld",
                    in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                    in_port_ptr->gu.cmn.id,
                    out_md_ptr->offset,
                    result);
         }
      }
   }

   return result;
}

/**
 * For external ports, gen_cntr_operate_on_ext_in_port & gen_cntr_operate_on_ext_out_port is called
 * in 2 contexts:
 * 1. in the context of subgraph command: both ends of the connection belongs to the same SG.
 * 2. in the context of handle list of subgraph: this is an inter-SG connection.
 *
 * CLOSE, FLUSH, RESET, STOP are handled in this context.
 * START is not handled here, its handled based on downgraded state in gen_cntr_set_downgraded_state_on_output_port.
 */
ar_result_t olc_operate_on_ext_out_port(void *              base_ptr,
                                        uint32_t            sg_ops,
                                        gu_ext_out_port_t **ext_out_port_pptr,
                                        bool_t              is_self_sg)
{
   ar_result_t         result           = AR_EOK;
   olc_t *             me_ptr           = (olc_t *)base_ptr;
   olc_ext_out_port_t *ext_out_port_ptr = (olc_ext_out_port_t *)*ext_out_port_pptr;

   bool_t is_disconnect_needed = ((TOPO_SG_OP_DISCONNECT & sg_ops) &&
                                  (cu_is_disconnect_ext_out_port_needed((cu_base_t *)base_ptr, &ext_out_port_ptr->gu)));

   // Specifies what to listen on the output. external output or read ipc queue
   if (TOPO_SG_OP_START & sg_ops )
   {
      if (ext_out_port_ptr->rdp_ctrl_cfg_ptr)
      {
         spdm_read_dl_pcd(&me_ptr->spgm_info, ext_out_port_ptr->rdp_ctrl_cfg_ptr->sdm_port_index);
      }
   }

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_MED_PRIO,
           "olc_operate_on_ext_out_port: output channel mask: 0x%x ",
           ext_out_port_ptr->cu.bit_mask);

   if ((TOPO_SG_OP_CLOSE | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops)
   {
      cu_stop_listen_to_mask(&me_ptr->cu, ext_out_port_ptr->cu.bit_mask);
      if (ext_out_port_ptr->rdp_ctrl_cfg_ptr)
      {
         cu_stop_listen_to_mask(&me_ptr->cu, ext_out_port_ptr->rdp_ctrl_cfg_ptr->sat_rw_bit_mask);
      }
   }

   if (((TOPO_SG_OP_CLOSE | TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP) & sg_ops) || (is_disconnect_needed))
   {
      olc_flush_output_data_queue(me_ptr, ext_out_port_ptr, (sg_ops & TOPO_SG_OP_FLUSH));
   }

   if (((TOPO_SG_OP_CLOSE | TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP) & sg_ops) || (is_disconnect_needed))
   {
      (void)olc_ext_out_port_reset(me_ptr, ext_out_port_ptr);
   }

   if (is_disconnect_needed)
   {
      ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr = NULL;
   }

   // Only change the connected state of a port not in the current subgraph.
   if (!is_self_sg)
   {
      // inform module for ext conn (for self-sg case, operate_on_sg will take care at module level or at
      // inter-sg,intra-container level)
      if (TOPO_SG_OP_CLOSE & sg_ops)
      {
         gen_topo_module_t *module_ptr = ((gen_topo_module_t *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr);
         if (module_ptr->capi_ptr) // just for completeness. not used in OLC with present design
         {
            gen_topo_output_port_t *out_port_ptr = ((gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr);
            result                               = gen_topo_capi_set_data_port_op_from_sg_ops(module_ptr,
                                                                sg_ops,
                                                                &out_port_ptr->common.last_issued_opcode,
                                                                FALSE, /* is_input */
                                                                ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                                                                ext_out_port_ptr->gu.int_out_port_ptr->cmn.id);
         }
         else
         {
            // framework module handling if any
         }
      }
   }

   if (TOPO_SG_OP_CLOSE == sg_ops)
   {
      ext_out_port_ptr->gu.gu_status = GU_STATUS_CLOSING;
   }

   return result;
}

ar_result_t olc_register_module_events(olc_t *me_ptr, gpr_packet_t *packet_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   apm_cmd_header_t *in_apm_cmd_header = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, packet_ptr);
   bool_t            is_out_of_band    = (0 != in_apm_cmd_header->mem_map_handle);
   uint8_t *         payload_ptr       = NULL;
   uint32_t          alignment_size    = 0;
   gpr_packet_t *    dummy_pkt_ptr     = NULL;

   result = spf_svc_get_cmd_payload_addr(me_ptr->cu.gu_ptr->log_id,
                                         packet_ptr,
                                         &dummy_pkt_ptr,
                                         (uint8_t **)&payload_ptr,
                                         &alignment_size,
                                         NULL,
                                         apm_get_mem_map_client());
   if (AR_EOK != result)
   {
      OLC_MSG(me_ptr->cu.gu_ptr->log_id, DBG_ERROR_PRIO, "Failed to get payload ptr");
      THROW(result, AR_EFAILED);
   }

   TRY(result,
       sgm_handle_register_module_events(&me_ptr->spgm_info,
                                         payload_ptr,
                                         in_apm_cmd_header->payload_size,
                                         packet_ptr->token,
                                         packet_ptr->dst_port,
                                         packet_ptr->src_port,
                                         packet_ptr->src_domain_id,
                                         !is_out_of_band,
                                         packet_ptr->opcode));

   CATCH(result, CU_MSG_PREFIX, me_ptr->cu.gu_ptr->log_id)
   {
   }
   return result;
}

/**
 * Wrapper function to cu_set_get_cfgs_packed which calls set_param_begin/end beforehand for set_cfgs.
 */
ar_result_t olc_set_get_cfgs_packed(olc_t *me_ptr, gpr_packet_t *packet_ptr, spf_cfg_data_type_t cfg_type)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   apm_cmd_header_t *in_apm_cmd_header = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, packet_ptr);
   bool_t            is_out_of_band    = (0 != in_apm_cmd_header->mem_map_handle);
   uint8_t *         param_data_ptr    = NULL;
   uint32_t          alignment_size    = 0;
   gpr_packet_t *    dummy_pkt_ptr     = NULL;

   if ((SPF_CFG_DATA_PERSISTENT == cfg_type) || (SPF_CFG_DATA_SHARED_PERSISTENT == cfg_type))
   {
      // No need to malloc and copy here. Just change the memmap handle and send to the approporiate container.
      // we don't even need to invalidate the memory. When we get a response from the satellite, just ack the msg back
      sgm_handle_persistent_set_get_cfg_packed(&me_ptr->spgm_info,
                                               in_apm_cmd_header,
                                               packet_ptr->dst_port,
                                               packet_ptr->opcode);
      return result;
   }

   if (is_out_of_band)
   {
      result = spf_svc_get_cmd_payload_addr(me_ptr->cu.gu_ptr->log_id,
                                            packet_ptr,
                                            &dummy_pkt_ptr,
                                            (uint8_t **)&param_data_ptr,
                                            &alignment_size,
                                            NULL,
                                            apm_get_mem_map_client());

      if (AR_EOK != result)
      {
         OLC_MSG(me_ptr->cu.gu_ptr->log_id, DBG_ERROR_PRIO, "Failed to get payload ptr");
         THROW(result, AR_EFAILED);
      }
   }
   else
   {
      param_data_ptr = (uint8_t *)(in_apm_cmd_header + 1);
   }

   sgm_handle_set_get_cfg_packed(&me_ptr->spgm_info,
                                 param_data_ptr,
                                 in_apm_cmd_header->payload_size,
                                 packet_ptr->dst_port,
                                 !is_out_of_band,
                                 packet_ptr->opcode);

   CATCH(result, CU_MSG_PREFIX, me_ptr->cu.gu_ptr->log_id)
   {
   }

   return result;
}

/**
 * Handling of the satellite path GPR command.
 */
ar_result_t olc_sgm_gpr_cmd(cu_base_t *base_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t   result     = AR_EOK;
   olc_t *       me_ptr     = (olc_t *)base_ptr;
   gpr_packet_t *packet_ptr = (gpr_packet_t *)me_ptr->cu.cmd_msg.payload_ptr;

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:SGM_GPR cmd: Executing GPR command from satellite, "
           "opCode(%lX) token(%lx)",
           packet_ptr->opcode,
           packet_ptr->token);

   TRY(result, spgm_cmd_queue_handler(base_ptr, &me_ptr->spgm_info, packet_ptr));

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:SGM_GPR cmd: Done executing GPR command from satellite, result=0x%lx",
           result);

   return result;
}

/**
 * Handling of the control path GPR command.
 */
ar_result_t olc_gpr_cmd(cu_base_t *base_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t   result            = AR_EOK;
   olc_t *       me_ptr            = (olc_t *)base_ptr;
   gpr_packet_t *packet_ptr        = (gpr_packet_t *)me_ptr->cu.cmd_msg.payload_ptr;
   bool_t        wait_for_response = FALSE;

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:GPR cmd: Executing GPR command, "
           "opCode(%lX) token(%lx)",
           packet_ptr->opcode,
           packet_ptr->token);

   switch (packet_ptr->opcode)
   {
      case APM_CMD_SET_CFG:
      {
         OLC_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, "SET cfg received from GPR");
         TRY(result, olc_set_get_cfgs_packed(me_ptr, packet_ptr, SPF_CFG_DATA_TYPE_DEFAULT));
         wait_for_response = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, packet_ptr->opcode);
         break;
      }
      case APM_CMD_GET_CFG:
      {
         OLC_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, "GET cfg received from GPR");

         TRY(result, olc_set_get_cfgs_packed(me_ptr, packet_ptr, SPF_CFG_DATA_TYPE_DEFAULT));
         wait_for_response = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, packet_ptr->opcode);
         break;
      }

      case APM_CMD_REGISTER_CFG:
      case APM_CMD_DEREGISTER_CFG:
      {
         OLC_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, "REG/DEREG cfg received from GPR");
         TRY(result, olc_set_get_cfgs_packed(me_ptr, packet_ptr, SPF_CFG_DATA_PERSISTENT));
         wait_for_response = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, packet_ptr->opcode);
         break;
      }

      case APM_CMD_REGISTER_SHARED_CFG:
      case APM_CMD_DEREGISTER_SHARED_CFG:
      {
         OLC_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, "Shared REG/DEREG cfg received from GPR");
         TRY(result, olc_set_get_cfgs_packed(me_ptr, packet_ptr, SPF_CFG_DATA_SHARED_PERSISTENT));
         wait_for_response = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, packet_ptr->opcode);
         break;
      }

      case APM_CMD_REGISTER_MODULE_EVENTS:
      {

         if ((packet_ptr->dst_port == me_ptr->spgm_info.sgm_id.cont_id) &&
             (packet_ptr->src_domain_id == me_ptr->host_domain_id))
         {
            result = cu_register_module_events(&me_ptr->cu, packet_ptr);
            if (AR_EOK != result)
            {
               OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Failed to register events with container");
            }
         }
         else
         {
            OLC_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, "Register module events received from GPR");
            TRY(result, olc_register_module_events(me_ptr, packet_ptr));
            wait_for_response = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, packet_ptr->opcode);
         }
         break;
      }

      default:
      {
         gu_module_t *module_ptr = (gu_module_t *)gu_find_module(&me_ptr->topo.gu, packet_ptr->dst_port);
         VERIFY(result, NULL != module_ptr);

         result = AR_EUNSUPPORTED;
         break;
      }
   }

   if (TRUE == wait_for_response)
   {
      result = sgm_cache_cmd_msg(&me_ptr->spgm_info, packet_ptr->opcode, &me_ptr->cu.cmd_msg);
      memset(&me_ptr->cu.cmd_msg, 0, sizeof(spf_msg_t));
      me_ptr->cu.curr_chan_mask &= (~OLC_CMD_BIT_MASK);
   }

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:GPR cmd: Done executing "
           "GPR command, result=0x%lx",
           result);

   if (FALSE == wait_for_response)
   {
      __gpr_cmd_end_command(packet_ptr, result);
   }
   return result;
}

/**
 * Handling of the control path set cfg command and get cfg command.
 */
ar_result_t olc_set_get_cfg(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id         = 0;
   bool_t   is_set_cfg_msg = FALSE;
   olc_t *  me_ptr         = NULL;

   spf_msg_cmd_param_data_cfg_t *    cfg_cmd_ptr  = NULL;
   spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr = NULL;

   VERIFY(result, (NULL != base_ptr));

   me_ptr = (olc_t *)base_ptr;
   log_id = me_ptr->topo.gu.log_id;

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:SET_CFG: Executing set-cfg command. current channel mask=0x%x",
           me_ptr->cu.curr_chan_mask);

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_param_data_cfg_t));

   cfg_cmd_ptr = (spf_msg_cmd_param_data_cfg_t *)&header_ptr->payload_start;

   is_set_cfg_msg = (SPF_MSG_CMD_SET_CFG == me_ptr->cu.cmd_msg.msg_opcode) ? TRUE : FALSE;
   cmd_extn_ptr   = (spgm_set_get_cfg_cmd_extn_info_t *)posal_memory_malloc(sizeof(spgm_set_get_cfg_cmd_extn_info_t),
                                                                          base_ptr->heap_id);
   if (NULL == cmd_extn_ptr)
   {
      result = AR_ENOMEMORY;
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_ERROR_PRIO,
              "CMD:SET_GET_CFG: Failed to allocate memory for "
              "set_get_cfg cmd_extn_ptr, result %lu.",
              result);
      THROW(result, AR_ENOMEMORY);
   }
   else
   {
      memset(cmd_extn_ptr, 0, sizeof(spgm_set_get_cfg_cmd_extn_info_t));
   }

   TRY(result, olc_preprocess_set_get_cfg(base_ptr, cfg_cmd_ptr, cmd_extn_ptr));

   switch (cmd_extn_ptr->cfg_destn_type)
   {
      case CFG_FOR_SATELLITE_ONLY:
      {
         TRY(result,
             olc_process_satellite_set_get_cfg(base_ptr, cmd_extn_ptr, header_ptr->payload_size, is_set_cfg_msg));
         break;
      }
      case CFG_FOR_CONTAINER_ONLY:
      {
         TRY(result, olc_process_container_set_get_cfg(base_ptr, cmd_extn_ptr, me_ptr->cu.cmd_msg.msg_opcode));
         break;
      }
      case CFG_FOR_SATELLITE_AND_CONTAINER:
      {
         TRY(result, olc_process_container_set_get_cfg(base_ptr, cmd_extn_ptr, me_ptr->cu.cmd_msg.msg_opcode));
         TRY(result,
             olc_process_satellite_set_get_cfg(base_ptr, cmd_extn_ptr, header_ptr->payload_size, is_set_cfg_msg));
         break;
      }
      default:
      {
         THROW(result, AR_EUNSUPPORTED);
      }
   }

   CU_MSG(log_id,
          DBG_HIGH_PRIO,
          "CMD:SET_PARAM: Done executing set-cfg command, current channel mask=0x%x. result=0x%lx",
          me_ptr->cu.curr_chan_mask,
          result);

   CATCH(result, CU_MSG_PREFIX, log_id)
   {
   }

   if (cmd_extn_ptr)
   {
      if (0 == cmd_extn_ptr->pending_resp_counter)
      {
         if (CFG_FOR_SATELLITE_AND_CONTAINER == cmd_extn_ptr->cfg_destn_type)
         {
            if (cmd_extn_ptr->cntr_cfg_cmd_ptr)
            {
               posal_memory_free(cmd_extn_ptr->cntr_cfg_cmd_ptr);
            }
            if (cmd_extn_ptr->sat_cfg_cmd_ptr)
            {
               posal_memory_free(cmd_extn_ptr->sat_cfg_cmd_ptr);
            }
         }
         spf_msg_ack_msg(&me_ptr->cu.cmd_msg, result);
         if (cmd_extn_ptr)
         {
            posal_memory_free(cmd_extn_ptr);
         }
      }
      else // waiting for response
      {
         memset(&me_ptr->cu.cmd_msg, 0, sizeof(spf_msg_t));
      }
   }
   else
   {
      spf_msg_ack_msg(&me_ptr->cu.cmd_msg, result);
   }

   CU_MSG(log_id, DBG_HIGH_PRIO, "CMD:SET_PARAM: Done executing set-cfg command, result=0x%lx", result);

   return result;
}

/**
 * Handling of the control path graph open command.
 */
ar_result_t olc_graph_open(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_topo_graph_init_t graph_init_data   = { 0 };
   bool_t                sg_open_state     = FALSE;
   bool_t                wait_for_response = FALSE;
   uint32_t              log_id            = 0;
   spgm_cmd_rsp_node_t * rsp_node_ptr      = NULL;

   olc_t *                   me_ptr           = NULL;
   spf_msg_cmd_graph_open_t *open_cmd_ptr     = NULL;
   spf_msg_cmd_graph_open_t *olc_open_cmd_ptr = NULL;
   spf_msg_header_t *        header_ptr       = NULL;

   if (NULL == base_ptr)
   {
      OLC_MSG(log_id, DBG_HIGH_PRIO, "GRAPH_OPEN: Executing graph open command. Invalid base_ptr");
      return AR_EBADPARAM;
   }

   me_ptr       = (olc_t *)base_ptr;
   log_id       = me_ptr->topo.gu.log_id;
   rsp_node_ptr = &me_ptr->cmd_rsp_node;

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:GRAPH_OPEN: Executing graph open command."
           "current channel mask=0x%x",
           me_ptr->cu.curr_chan_mask);

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, (NULL != header_ptr));
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_open_t));

   open_cmd_ptr = (spf_msg_cmd_graph_open_t *)&header_ptr->payload_start;
   VERIFY(result, (NULL != open_cmd_ptr));

   TRY(result, olc_parse_container_cfg(me_ptr, open_cmd_ptr->container_cfg_ptr));

   /* Parse the open command payload and get the payload for the OLC and satellite Graph open */
   {
      /** ALlocate memory for OLC graph open payload */
      if (NULL == (olc_open_cmd_ptr =
                      (spf_msg_cmd_graph_open_t *)posal_memory_malloc(header_ptr->payload_size, base_ptr->heap_id)))
      {

         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_ERROR_PRIO,
                 "CMD:GRAPH_OPEN: Failed to allocate memory for OLC open command payload.");

         // Acknowledge the open command with failure
         spf_msg_ack_msg(&me_ptr->cu.cmd_msg, AR_ENOMEMORY);
         return AR_ENOMEMORY;
      }
      me_ptr->olc_core_graph_open_cmd_ptr = olc_open_cmd_ptr;
      //  Create the OLC core payload (payload for the container creation in the master domain context)
      TRY(result, olc_create_graph_open_payload(&me_ptr->spgm_info, open_cmd_ptr, olc_open_cmd_ptr));

      // Derive and create the satellite graph open payload from the OLC open payload which is sent from APM
      TRY(result, sgm_handle_open(&me_ptr->spgm_info, open_cmd_ptr, header_ptr->payload_size));
      sg_open_state = TRUE; // indicates that the satellite graph open is successfully sent
   }

   // OLC_CA: Might need to register before sending the open, but we dont know the satellite PD info
   TRY(result, olc_serv_reg_notify_register(me_ptr, me_ptr->spgm_info.sgm_id.sat_pd));

   {
      gu_sizes_t payload_size = {.ext_ctrl_port_size = OLC_EXT_CTRL_PORT_SIZE_W_QS,
                                 .ext_in_port_size   = OLC_EXT_IN_PORT_SIZE_W_QS,
                                 .ext_out_port_size  = OLC_EXT_OUT_PORT_SIZE_W_QS,
                                 .in_port_size       = sizeof(gen_topo_input_port_t),
                                 .out_port_size      = sizeof(gen_topo_output_port_t),
                                 .ctrl_port_size     = sizeof(gen_topo_ctrl_port_t),
                                 .sg_size            = sizeof(gen_topo_sg_t),
                                 .module_size        = sizeof(olc_module_t) };

      result = gu_validate_graph_open_cmd(&me_ptr->topo.gu, olc_open_cmd_ptr);

      if (AR_DID_FAIL(result))
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_HIGH_PRIO,
                 "CMD:GRAPH_OPEN: Link Connection validation for graph open failed. current channel mask=0x%x. "
                 "result=0x%lx.",
                 me_ptr->cu.curr_chan_mask,
                 result);
         spf_msg_ack_msg(&me_ptr->cu.cmd_msg, result);
         return result;
      }

      TRY(result, gu_create_graph(&me_ptr->topo.gu, olc_open_cmd_ptr, &payload_size, me_ptr->cu.heap_id));
      me_ptr->cu.gu_ptr = &me_ptr->topo.gu;
      // gu_print_graph(&me_ptr->topo.gu);

      // initialize media format pointer for the new ports.
      gen_topo_set_default_media_fmt_at_open(&me_ptr->topo);
   }

   graph_init_data.spf_handle_ptr = &me_ptr->cu.spf_handle;
   graph_init_data.gpr_cb_fn      = cu_gpr_callback;
   graph_init_data.capi_cb        = gen_topo_capi_callback;
   graph_init_data.propagate_rdf  = TRUE;
   // Update gu structures based on changes to graph.
   TRY(result, gen_topo_create_modules(&me_ptr->topo, &graph_init_data));
   // stack_size = graph_init_data.max_stack_size;

   wait_for_response = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, APM_CMD_GRAPH_OPEN);
   if (FALSE == wait_for_response)
   {
      rsp_node_ptr->opcode     = APM_CMD_GRAPH_OPEN;
      rsp_node_ptr->rsp_result = result;
      rsp_node_ptr->token      = 0;
      rsp_node_ptr->cmd_msg    = &me_ptr->cu.cmd_msg;
      olc_graph_open_rsp_h(&me_ptr->cu, rsp_node_ptr);
   }
   else
   {
      result = sgm_cache_cmd_msg(&me_ptr->spgm_info, APM_CMD_GRAPH_OPEN, &me_ptr->cu.cmd_msg);
      memset(&me_ptr->cu.cmd_msg, 0, sizeof(spf_msg_t));
      me_ptr->cu.curr_chan_mask &= (~OLC_CMD_BIT_MASK);
   }

#ifdef OLC_VERBOSE_DEBUGGING
   gu_print_graph(&me_ptr->topo.gu);
#endif

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
      if (open_cmd_ptr)
      {
         olc_handle_failure_at_graph_open(me_ptr, open_cmd_ptr, result, sg_open_state);
      }
      else
      {
         spf_msg_ack_msg(&me_ptr->cu.cmd_msg, result);
      }
   }

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:GRAPH_OPEN: processed graph open command. current channel mask=0x%x. result=0x%lx.",
           me_ptr->cu.curr_chan_mask,
           result);

   return result;
}

/**
 * Handling of the control path graph prepare command.
 */
ar_result_t olc_graph_prepare(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t             log_id            = 0;
   uint32_t             curr_chan_mask    = 0;
   bool_t               wait_for_response = FALSE;
   spgm_cmd_rsp_node_t *rsp_node_ptr      = NULL;

   olc_t *                   me_ptr        = NULL;
   spf_msg_header_t *        header_ptr    = NULL;
   spf_msg_cmd_graph_mgmt_t *cmd_gmgmt_ptr = NULL;

   me_ptr         = (olc_t *)base_ptr;
   log_id         = me_ptr->topo.gu.log_id;
   curr_chan_mask = me_ptr->cu.curr_chan_mask;

   rsp_node_ptr = &me_ptr->cmd_rsp_node;

   rsp_node_ptr->opcode     = APM_CMD_GRAPH_PREPARE;
   rsp_node_ptr->rsp_result = AR_EOK;
   rsp_node_ptr->token      = 0;
   rsp_node_ptr->cmd_msg    = &me_ptr->cu.cmd_msg;

   OLC_MSG(log_id, DBG_HIGH_PRIO, "Executing prepare graph, current channel mask=0x%x", curr_chan_mask);

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, (NULL != header_ptr));
   cmd_gmgmt_ptr = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
   VERIFY(result, (NULL != cmd_gmgmt_ptr));

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_PREPARE, TOPO_SG_STATE_PREPARED));

   // if the num of sub > 0, send the prepare command to satellite sub graph
   if (cmd_gmgmt_ptr->sg_id_list.num_sub_graph > 0)
   {
      // call to create the payload and send the prepare command to the satellite graph
      TRY(result, sgm_handle_prepare(&me_ptr->spgm_info, cmd_gmgmt_ptr, header_ptr->payload_size));
      // Get the response status for the command. If the command handling passed and there is no requirement to
      // wait for response from the satellite
      wait_for_response        = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, APM_CMD_GRAPH_PREPARE);
      rsp_node_ptr->rsp_result = result;
   }
   else
   {
      wait_for_response = FALSE;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   if (FALSE == wait_for_response)
   {
      // Handle the prepare command response
      rsp_node_ptr->rsp_result = result;
      olc_graph_prepare_rsp_h(base_ptr, rsp_node_ptr);
   }
   else
   {
      result = sgm_cache_cmd_msg(&me_ptr->spgm_info, APM_CMD_GRAPH_PREPARE, &me_ptr->cu.cmd_msg);
      memset(&me_ptr->cu.cmd_msg, 0, sizeof(spf_msg_t));
      me_ptr->cu.curr_chan_mask &= (~OLC_CMD_BIT_MASK);
   }

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:Prepare Graph:Done executing prepare graph. "
           "current channel mask=0x%x. result=0x%lx.",
           curr_chan_mask,
           result);

   return result;
}

/**
 * Handling of the control path graph start command.
 */
ar_result_t olc_graph_start(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *                   me_ptr            = (olc_t *)base_ptr;
   uint32_t                  log_id            = me_ptr->topo.gu.log_id;
   spf_msg_header_t *        header_ptr        = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   spf_msg_cmd_graph_mgmt_t *cmd_gmgmt_ptr     = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
   bool_t                    wait_for_response = FALSE;
   spgm_cmd_rsp_node_t *     rsp_node_ptr      = NULL;

   OLC_MSG(log_id, DBG_HIGH_PRIO, "Executing start command. current channel mask=0x%x", me_ptr->cu.curr_chan_mask);

   rsp_node_ptr             = &me_ptr->cmd_rsp_node;
   rsp_node_ptr->opcode     = APM_CMD_GRAPH_START;
   rsp_node_ptr->rsp_result = AR_EOK;
   rsp_node_ptr->token      = 0;
   rsp_node_ptr->cmd_msg    = &me_ptr->cu.cmd_msg;

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_START, TOPO_SG_STATE_STARTED));

   if (cmd_gmgmt_ptr->sg_id_list.num_sub_graph > 0)
   {
      result                   = sgm_handle_start(&me_ptr->spgm_info, cmd_gmgmt_ptr, header_ptr->payload_size);
      wait_for_response        = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, APM_CMD_GRAPH_START);
      rsp_node_ptr->rsp_result = result;
   }
   else
   {
      wait_for_response = FALSE;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   if (FALSE == wait_for_response)
   {
      rsp_node_ptr->rsp_result = result;
      olc_graph_start_rsp_h(base_ptr, rsp_node_ptr);
   }
   else
   {
      result = sgm_cache_cmd_msg(&me_ptr->spgm_info, APM_CMD_GRAPH_START, &me_ptr->cu.cmd_msg);
      memset(&me_ptr->cu.cmd_msg, 0, sizeof(spf_msg_t));
      me_ptr->cu.curr_chan_mask &= (~OLC_CMD_BIT_MASK);
   }

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:START:Done executing start command. "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return result;
}

/**
 * Handling of the control path graph suspend command.
 */
ar_result_t olc_graph_suspend(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *                   me_ptr            = (olc_t *)base_ptr;
   uint32_t                  log_id            = me_ptr->topo.gu.log_id;
   spf_msg_header_t *        header_ptr        = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   spf_msg_cmd_graph_mgmt_t *cmd_gmgmt_ptr     = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
   bool_t                    wait_for_response = FALSE;
   spgm_cmd_rsp_node_t *     rsp_node_ptr      = NULL;

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_HIGH_PRIO,
           "CMD:SUSPEND: Executing suspend Command. current channel mask=0x%x",
           me_ptr->cu.curr_chan_mask);

   rsp_node_ptr             = &me_ptr->cmd_rsp_node;
   rsp_node_ptr->opcode     = APM_CMD_GRAPH_SUSPEND;
   rsp_node_ptr->rsp_result = AR_EOK;
   rsp_node_ptr->token      = 0;
   rsp_node_ptr->cmd_msg    = &me_ptr->cu.cmd_msg;

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_SUSPEND, TOPO_SG_STATE_SUSPENDED));

   if (cmd_gmgmt_ptr->sg_id_list.num_sub_graph > 0)
   {
      result                   = sgm_handle_suspend(&me_ptr->spgm_info, cmd_gmgmt_ptr, header_ptr->payload_size);
      wait_for_response        = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, APM_CMD_GRAPH_SUSPEND);
      rsp_node_ptr->rsp_result = result;
   }
   else
   {
      wait_for_response = FALSE;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   if (FALSE == wait_for_response)
   {
      rsp_node_ptr->rsp_result = result;
      olc_graph_suspend_rsp_h(base_ptr, rsp_node_ptr);
   }
   else
   {
      result = sgm_cache_cmd_msg(&me_ptr->spgm_info, APM_CMD_GRAPH_SUSPEND, &me_ptr->cu.cmd_msg);
      memset(&me_ptr->cu.cmd_msg, 0, sizeof(spf_msg_t));
      me_ptr->cu.curr_chan_mask &= (~OLC_CMD_BIT_MASK);
   }

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:SUSPEND:Done Executing suspend command. current channel mask=0x%x. result=0x%lx.",
           me_ptr->cu.curr_chan_mask,
           result);

   return result;
}

/**
 * Handling of the control path graph stop command.
 */
ar_result_t olc_graph_stop(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *                   me_ptr            = (olc_t *)base_ptr;
   uint32_t                  log_id            = me_ptr->topo.gu.log_id;
   spf_msg_header_t *        header_ptr        = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   spf_msg_cmd_graph_mgmt_t *cmd_gmgmt_ptr     = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
   bool_t                    wait_for_response = FALSE;
   spgm_cmd_rsp_node_t *     rsp_node_ptr      = NULL;

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:STOP: Executing stop command. current channel mask=0x%x",
           base_ptr->curr_chan_mask);

   rsp_node_ptr             = &me_ptr->cmd_rsp_node;
   rsp_node_ptr->opcode     = APM_CMD_GRAPH_STOP;
   rsp_node_ptr->rsp_result = AR_EOK;
   rsp_node_ptr->token      = 0;
   rsp_node_ptr->cmd_msg    = &me_ptr->cu.cmd_msg;

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_STOP, TOPO_SG_STATE_STOPPED));

   if (cmd_gmgmt_ptr->sg_id_list.num_sub_graph > 0)
   {
      result                   = sgm_handle_stop(&me_ptr->spgm_info, cmd_gmgmt_ptr, header_ptr->payload_size);
      wait_for_response        = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, APM_CMD_GRAPH_STOP);
      rsp_node_ptr->rsp_result = result;
   }
   else
   {
      wait_for_response = FALSE;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   if (FALSE == wait_for_response)
   {
      rsp_node_ptr->rsp_result = result;
      olc_graph_stop_rsp_h(base_ptr, rsp_node_ptr);
   }
   else
   {
      result = sgm_cache_cmd_msg(&me_ptr->spgm_info, APM_CMD_GRAPH_STOP, &me_ptr->cu.cmd_msg);
      memset(&me_ptr->cu.cmd_msg, 0, sizeof(spf_msg_t));
      me_ptr->cu.curr_chan_mask &= (~OLC_CMD_BIT_MASK);
   }

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:STOP:Done Executing stop command. "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return result;
}

/**
 * Handling of the control path graph flush command.
 */
ar_result_t olc_graph_flush(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *                   me_ptr            = (olc_t *)base_ptr;
   uint32_t                  log_id            = me_ptr->topo.gu.log_id;
   spf_msg_header_t *        header_ptr        = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   spf_msg_cmd_graph_mgmt_t *cmd_gmgmt_ptr     = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
   bool_t                    wait_for_response = FALSE;
   spgm_cmd_rsp_node_t *     rsp_node_ptr      = NULL;

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:FLUSH:Executing flush command, current channel mask=0x%x",
           base_ptr->curr_chan_mask);

   rsp_node_ptr             = &me_ptr->cmd_rsp_node;
   rsp_node_ptr->opcode     = APM_CMD_GRAPH_FLUSH;
   rsp_node_ptr->rsp_result = AR_EOK;
   rsp_node_ptr->token      = 0;
   rsp_node_ptr->cmd_msg    = &me_ptr->cu.cmd_msg;

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_FLUSH, TOPO_SG_STATE_INVALID));

   if (cmd_gmgmt_ptr->sg_id_list.num_sub_graph > 0)
   {
      result                   = sgm_handle_flush(&me_ptr->spgm_info, cmd_gmgmt_ptr, header_ptr->payload_size);
      wait_for_response        = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, APM_CMD_GRAPH_FLUSH);
      rsp_node_ptr->rsp_result = result;
   }
   else
   {
      wait_for_response = FALSE;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   if (FALSE == wait_for_response)
   {
      rsp_node_ptr->rsp_result = result;
      olc_graph_flush_rsp_h(base_ptr, rsp_node_ptr);
   }
   else
   {
      result = sgm_cache_cmd_msg(&me_ptr->spgm_info, APM_CMD_GRAPH_FLUSH, &me_ptr->cu.cmd_msg);
      memset(&me_ptr->cu.cmd_msg, 0, sizeof(spf_msg_t));
      me_ptr->cu.curr_chan_mask &= (~OLC_CMD_BIT_MASK);
   }

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:FLUSH:Done executing flush command, current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return result;
}

/**
 * Handling of the control path graph close command.
 */
ar_result_t olc_graph_close(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t             log_id            = 0;
   bool_t               wait_for_response = FALSE;
   spgm_cmd_rsp_node_t *rsp_node_ptr      = NULL;

   olc_t *me_ptr = (olc_t *)base_ptr;
   // spf_cntr_sub_graph_list_t *sg_list_ptr   = NULL;
   spf_msg_cmd_graph_mgmt_t *cmd_gmgmt_ptr = NULL;
   spf_msg_header_t *        header_ptr    = NULL;

   VERIFY(result, NULL != base_ptr);

   log_id = me_ptr->topo.gu.log_id;

   header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_mgmt_t));

   cmd_gmgmt_ptr = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:CLOSE:Executing close Command, "
           "current channel mask=0x%x",
           base_ptr->curr_chan_mask);

   rsp_node_ptr             = &me_ptr->cmd_rsp_node;
   rsp_node_ptr->opcode     = APM_CMD_GRAPH_CLOSE;
   rsp_node_ptr->rsp_result = AR_EOK;
   rsp_node_ptr->token      = 0;
   rsp_node_ptr->cmd_msg    = &me_ptr->cu.cmd_msg;

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_CLOSE, TOPO_SG_STATE_STOPPED));

   if (1 == me_ptr->satellite_up_down_status)
   {
      if (cmd_gmgmt_ptr->sg_id_list.num_sub_graph > 0)
      {
         result                   = sgm_handle_close(&me_ptr->spgm_info, cmd_gmgmt_ptr, header_ptr->payload_size);
         wait_for_response        = sgm_get_cmd_rsp_status(&me_ptr->spgm_info, APM_CMD_GRAPH_CLOSE);
         rsp_node_ptr->rsp_result = result;
      }
      else
      {
         wait_for_response = FALSE;
      }
   }

   if (FALSE == wait_for_response)
   {
      rsp_node_ptr->rsp_result = result;
      olc_graph_close_rsp_h(base_ptr, rsp_node_ptr);
   }
   else
   {
      result = sgm_cache_cmd_msg(&me_ptr->spgm_info, APM_CMD_GRAPH_CLOSE, &me_ptr->cu.cmd_msg);
      memset(&me_ptr->cu.cmd_msg, 0, sizeof(spf_msg_t));
      me_ptr->cu.curr_chan_mask &= (~OLC_CMD_BIT_MASK);
   }

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:CLOSE:Done executing close command, "
           "current channel mask=0x%x. result=0x%lx.",
           me_ptr ? base_ptr->curr_chan_mask : 0,
           result);

   // Catch here so we don't print an error on AR_ETERMINATED.
   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/*
 * Any external output ports whose media format on the external port doesn't match internal port, try to resend media
 * format to the external port. This is needed when there is an external output elementary module - after device
 * switch the external output port gets transferred to the previous module which already has sent media format. We
 * need to retrigger handling to copy media format from internal to external output port structures.
 */
static ar_result_t olc_check_prop_int_mf_to_ext_port(olc_t *me_ptr)
{
   ar_result_t             result                = AR_EOK;
   gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
   while (ext_out_port_list_ptr)
   {
      olc_ext_out_port_t *    ext_out_port_ptr = (olc_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      gen_topo_output_port_t *out_port_ptr =
         (gen_topo_output_port_t *)ext_out_port_list_ptr->ext_out_port_ptr->int_out_port_ptr;
      if ((out_port_ptr->common.flags.is_mf_valid) &&
          tu_has_media_format_changed(&ext_out_port_ptr->cu.media_fmt, out_port_ptr->common.media_fmt_ptr))
      {
         out_port_ptr->common.flags.media_fmt_event = TRUE;

         // Set overall mf flag to true to force assign_non_buf_lin_chains(), which should be handled at connect since
         // graph shape may change to attach tap point modules.
         GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(&me_ptr->topo, media_fmt_event);
      }

      LIST_ADVANCE(ext_out_port_list_ptr);
   }

   return result;
}

/**
 * Handling of the control path graph connect command.
 */
ar_result_t olc_graph_connect(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *  me_ptr = (olc_t *)base_ptr;
   uint32_t log_id = me_ptr->topo.gu.log_id;

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:CONNECT:Executing connect command, current channel mask=0x%x",
           base_ptr->curr_chan_mask);

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   TRY(result, cu_graph_connect(base_ptr));

   /*
    * Any external output ports whose media format on the external port doesn't match internal port, try to resend media
    * format to the external port. This is needed when there is an external output elementary module - after device
    * switch the external output port gets transferred to the previous module which already has sent media format. We
    * need to retrigger handling to copy media format from internal to external output port structures.
    */
   olc_check_prop_int_mf_to_ext_port(me_ptr);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   /*
    * Connect is a substep of graph-open. For creating ext ports buf, downstream queue info is needed
    *  (to populate in dst handle of gk handle)
    * This means only after connect we can handle threshold (which handles ext ports buf).
    */

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:CONNECT:Done excuting connect command, "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return spf_msg_ack_msg(&base_ptr->cmd_msg, AR_EOK);
}

/**
 * Handling of the control path graph disconnect command.
 */
ar_result_t olc_graph_disconnect(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *           me_ptr     = (olc_t *)base_ptr;
   uint32_t          log_id     = me_ptr->topo.gu.log_id;
   spf_msg_header_t *header_ptr = NULL;

   header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_mgmt_t));

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:DISCONNECT:Executing disconnect command, "
           "current channel mask=0x%x",
           base_ptr->curr_chan_mask);

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_DISCONNECT, TOPO_SG_STATE_INVALID));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   // OLC_CA
   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:DISCONNECT:Done excuting disconnect command, "
           "current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return spf_msg_ack_msg(&base_ptr->cmd_msg, AR_EOK);
}

/**
 * Handling of the control path graph destroy container command.
 */
ar_result_t olc_destroy_container(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   olc_t *     me_ptr = (olc_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.gu.log_id;

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:DESTROY:destroy received. "
           "current channel mask=0x%x",
           me_ptr->cu.curr_chan_mask);

   spf_msg_t cmd_msg = me_ptr->cu.cmd_msg;
   result            = olc_destroy(me_ptr);
   spf_msg_ack_msg(&cmd_msg, result);

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:DESTROY:Done destroy to down stream service. "
           "current channel mask=0x%x. result=0x%lx.",
           me_ptr->cu.curr_chan_mask,
           result);

   // send AR_ETERMINATED so calling routine knows the destroyer has been invoked.
   // Even in the fail case, we will still need to terminate the thread.
   return AR_ETERMINATED;
}

/**
 * Handling for a control path media format command. Check that upstream is
 * connected and that we didn't receive this command while running. Then,
 * converts the media format to the internal struct and calls the
 * offload driver function to handle a new input media format.
 */
ar_result_t olc_ctrl_path_media_fmt_handler(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;

   INIT_EXCEPTION_HANDLING
   gu_ext_in_port_t * gu_ext_in_port_ptr;
   cu_ext_in_port_t * ext_in_port_ptr;
   olc_ext_in_port_t *cnt_ext_in_port_ptr;
   olc_t *            me_ptr     = (olc_t *)base_ptr;
   uint32_t           log_id     = me_ptr->topo.gu.log_id;
   spf_msg_header_t * header_ptr = (spf_msg_header_t *)base_ptr->cmd_msg.payload_ptr;

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   VERIFY(result, base_ptr->cntr_vtbl_ptr && base_ptr->cntr_vtbl_ptr->input_media_format_received);
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_media_format_t));

   // Media format comes for external input port only
   gu_ext_in_port_ptr  = (gu_ext_in_port_t *)header_ptr->dst_handle_ptr;
   ext_in_port_ptr     = (cu_ext_in_port_t *)(gu_ext_in_port_ptr + base_ptr->ext_in_port_cu_offset);
   cnt_ext_in_port_ptr = (olc_ext_in_port_t *)gu_ext_in_port_ptr;

   topo_port_state_t port_state;
   base_ptr->topo_vtbl_ptr->get_port_property(base_ptr->topo_ptr,
                                              TOPO_DATA_INPUT_PORT_TYPE,
                                              PORT_PROPERTY_TOPO_STATE,
                                              (void *)gu_ext_in_port_ptr->int_in_port_ptr,
                                              (uint32_t *)&port_state);

   // For all commands addressed to ports, check if connection exists
   // (E.g. during disconnect cntr to cntr commands may be still sent. Such cmd must be dropped).
   if (NULL == gu_ext_in_port_ptr->upstream_handle.spf_handle_ptr)
   {
      OLC_MSG(log_id,
              DBG_HIGH_PRIO,
              "CMD:CTRL_PATH_MEDIA_FMT: received when no connection is present. "
              "Dropping.");
   }
   else
   {
      /* if downgraded state of the port is 'STARTED' then ctrl path media fmt is dropped.
       * The SG of this port may be started though. Port state is already downgraded when
       * updating state at the end of command handling.
       * need to check both as we don't downgrade port_state for input ports during port op (US to DS, only data path)*/
      if ((TOPO_PORT_STATE_STARTED == ext_in_port_ptr->connected_port_state) && (TOPO_PORT_STATE_STARTED == port_state))
      {
         OLC_MSG(log_id,
                 DBG_ERROR_PRIO,
                 " input media format control cmd received in start state. Module 0x%lX, port 0x%lx",
                 gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                 gu_ext_in_port_ptr->int_in_port_ptr->cmn.id);
         result = AR_EUNSUPPORTED;
      }
      else
      {
         // get media format update cmd payload
         spf_msg_media_format_t *media_fmt_ptr = (spf_msg_media_format_t *)&header_ptr->payload_start;

         OLC_MSG(log_id,
                 DBG_MED_PRIO,
                 "processing input media format control cmd, data_format = %lu. Module 0x%lX, port 0x%lx",
                 media_fmt_ptr->df,
                 gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                 gu_ext_in_port_ptr->int_in_port_ptr->cmn.id);

         // OLC can get both PCM and raw_compressed as input data
         spdm_handle_input_media_format_update(&me_ptr->spgm_info,
                                               (void *)media_fmt_ptr,
                                               cnt_ext_in_port_ptr->wdp_ctrl_cfg_ptr->sdm_port_index,
                                               FALSE);
      }
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:CTRL_PATH_MEDIA_FMT:Done excuting media format cmd, current channel mask=0x%x. result=0x%lx.",
           base_ptr->curr_chan_mask,
           result);

   return result;
}

/**
 * Handling of the control path media format command.
 */
ar_result_t olc_ctrl_path_media_fmt(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *  me_ptr = (olc_t *)base_ptr;
   uint32_t log_id = me_ptr->topo.gu.log_id;

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:CTRL_PATH_MEDIA_FMT: Executing media format cmd. "
           "current channel mask=0x%x",
           me_ptr->cu.curr_chan_mask);

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   TRY(result, olc_ctrl_path_media_fmt_handler(base_ptr));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:CTRL_PATH_MEDIA_FMT:Done excuting media format cmd,"
           " current channel mask=0x%x. result=0x%lx.",
           me_ptr->cu.curr_chan_mask,
           result);

   return spf_msg_ack_msg(&me_ptr->cu.cmd_msg, result);
}

/**
 * Handling of the frame len information from downstream.
 */
ar_result_t olc_icb_info_from_downstream(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *  me_ptr = (olc_t *)base_ptr;
   uint32_t log_id = me_ptr->topo.gu.log_id;

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:FRAME_LEN_DS: ICB: Executing ICB from DS. "
           "current channel mask=0x%x",
           me_ptr->cu.curr_chan_mask);

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   TRY(result, cu_cmd_icb_info_from_downstream(base_ptr));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           "CMD:FRAME_LEN_DS: ICB: Done executing ICB info from DS, "
           "current channel mask=0x%x. result=0x%lx.",
           me_ptr->cu.curr_chan_mask,
           result);

   return spf_msg_ack_msg(&me_ptr->cu.cmd_msg, result);
}

ar_result_t olc_handle_peer_port_property_update_cmd_to_satellite(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *  me_ptr = (olc_t *)base_ptr;
   uint32_t log_id = me_ptr->topo.gu.log_id;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)base_ptr->cmd_msg.payload_ptr;
   if (!base_ptr->cmd_msg.payload_ptr)
   {
      OLC_MSG(log_id, DBG_HIGH_PRIO, "CMD:PEER_PORT_PROPERTY_UPDATE: Received null payload");
      return AR_EFAILED;
   }

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   spf_msg_peer_port_property_update_t *peer_prop_payload_ptr =
      (spf_msg_peer_port_property_update_t *)&header_ptr->payload_start;
   spf_msg_peer_port_property_info_t *cur_ptr = peer_prop_payload_ptr->payload;

   for (uint32_t i = 0; i < peer_prop_payload_ptr->num_properties; i++)
   {
      switch (cur_ptr->property_type)
      {
         case PORT_PROPERTY_IS_UPSTREAM_RT:
         {
            gu_ext_in_port_t * dst_port_ptr    = (gu_ext_in_port_t *)header_ptr->dst_handle_ptr;
            olc_ext_in_port_t *ext_in_port_ptr = (olc_ext_in_port_t *)(dst_port_ptr);

            if (ext_in_port_ptr->wdp_ctrl_cfg_ptr)
            {
                TRY(result,
                    sdm_handle_peer_port_property_update_cmd(&me_ptr->spgm_info,
                                                             ext_in_port_ptr->wdp_ctrl_cfg_ptr->sdm_port_index,
                                                             cur_ptr));
            }

            break;
         }
         case PORT_PROPERTY_IS_DOWNSTREAM_RT:
         {
            gu_ext_out_port_t * dst_port_ptr     = (gu_ext_out_port_t *)header_ptr->dst_handle_ptr;
            olc_ext_out_port_t *ext_out_port_ptr = (olc_ext_out_port_t *)(dst_port_ptr);

            if (ext_out_port_ptr->rdp_ctrl_cfg_ptr)
            {
                TRY(result,
                    sdm_handle_peer_port_property_update_cmd(&me_ptr->spgm_info,
                                                             ext_out_port_ptr->rdp_ctrl_cfg_ptr->sdm_port_index,
                                                             cur_ptr));
            }

            break;
         }
         case PORT_PROPERTY_TOPO_STATE:
         {
            // state is always propagated in control path only from downstream to upstream.
            gu_ext_out_port_t * dst_port_ptr     = (gu_ext_out_port_t *)header_ptr->dst_handle_ptr;
            olc_ext_out_port_t *ext_out_port_ptr = (olc_ext_out_port_t *)(dst_port_ptr);
            topo_port_state_t ds_state = (topo_port_state_t)cur_ptr->property_value;

            if (ext_out_port_ptr->rdp_ctrl_cfg_ptr)
            {
                TRY(result,
                    sdm_handle_peer_port_property_update_cmd(&me_ptr->spgm_info,
                                                             ext_out_port_ptr->rdp_ctrl_cfg_ptr->sdm_port_index,
                                                             cur_ptr));
                // Specifies what to listen on the output. external output or read ipc queue
                if (TOPO_PORT_STATE_STARTED == ds_state)
                {
                   spdm_read_dl_pcd(&me_ptr->spgm_info, ext_out_port_ptr->rdp_ctrl_cfg_ptr->sdm_port_index);               
                }
            }

            break;
         }

         default:
         {
            OLC_MSG(log_id,
                    DBG_ERROR_PRIO,
                    "CMD:PEER_PORT_PROPERTY_UPDATE: Invalid property_type=0x%lx",
                    cur_ptr->property_type);
            result = AR_EUNSUPPORTED;
            break;
         }
      }
      cur_ptr++;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }
   return result;
}

/* Handles Peer port property update command in OLC container.*/
ar_result_t olc_handle_peer_port_property_update_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *  me_ptr = (olc_t *)base_ptr;
   uint32_t log_id = me_ptr->topo.gu.log_id;

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   result = cu_handle_peer_port_property_update_cmd(base_ptr);
   result = olc_handle_peer_port_property_update_cmd_to_satellite(base_ptr);

   if (AR_SUCCEEDED(result))
   {
      olc_handle_frame_length_n_state_change(me_ptr, me_ptr->cu.cntr_frame_len.frame_len_us); // OLC_CA
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }
   return spf_msg_return_msg(&base_ptr->cmd_msg);
}

ar_result_t olc_handle_upstream_stop_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *  me_ptr = (olc_t *)base_ptr;
   uint32_t log_id = me_ptr->topo.gu.log_id;

   spf_msg_header_t * header_ptr      = (spf_msg_header_t *)base_ptr->cmd_msg.payload_ptr;
   gu_ext_in_port_t * dst_port_ptr    = (gu_ext_in_port_t *)header_ptr->dst_handle_ptr;
   olc_ext_in_port_t *ext_in_port_ptr = (olc_ext_in_port_t *)(dst_port_ptr);

   CU_MSG(me_ptr->topo.gu.log_id,
          DBG_HIGH_PRIO,
          "CMD:UPSTREAM_STOP_CMD: Executing upstream stop for (0x%lX, 0x%lx). current channel mask=0x%x",
          dst_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
          dst_port_ptr->int_in_port_ptr->cmn.id,
          base_ptr->curr_chan_mask);

   VERIFY(result, (1 == me_ptr->satellite_up_down_status));

   olc_flush_input_data_queue(me_ptr, ext_in_port_ptr, TRUE /* keep data msg */, FALSE, FALSE);

   if (AR_DID_FAIL(result))
   {
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_HIGH_PRIO,
              "CMD:UPSTREAM_STOP_CMD: Handling upstream stop message failed for(0x%lX, 0x%lx)",
              dst_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
              dst_port_ptr->int_in_port_ptr->cmn.id);
   }
   else
   {
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_LOW_PRIO,
              "CMD:UPSTREAM_STOP_CMD: (0x%lX, 0x%lx) handling upstream stop done",
              dst_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
              dst_port_ptr->int_in_port_ptr->cmn.id);
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return spf_msg_return_msg(&base_ptr->cmd_msg);
}

ar_result_t olc_register_events_utils(cu_base_t *       base_ptr,
                                      gu_module_t *     gu_module_ptr,
                                      topo_reg_event_t *reg_event_payload_ptr,
                                      bool_t            is_register,
                                      bool_t *          capi_supports_v1_event_ptr)
{
   ar_result_t result = AR_EOK;

   return result;
}
