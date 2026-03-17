/**
 * \file gen_cntr_data_msg_handler.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "cu_events.h"
#include "media_fmt_extn_api.h"

/**
 * process_info_ptr == NULL => before process began
 */
ar_result_t gen_cntr_process_pending_input_data_cmd(gen_cntr_t *me_ptr)
{
   ar_result_t result            = AR_EOK;
   bool_t      is_any_mf_pending = FALSE;

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      topo_sg_state_t         sg_state        = gen_topo_get_sg_state(ext_in_port_ptr->gu.sg_ptr);

      // process the data command only if self sg is either prepared or started
      if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr &&
          (TOPO_SG_STATE_PREPARED == sg_state || TOPO_SG_STATE_STARTED == sg_state))
      {
         result |= ext_in_port_ptr->vtbl_ptr->process_pending_data_cmd(me_ptr, ext_in_port_ptr);
      }

      is_any_mf_pending |= ext_in_port_ptr->flags.pending_mf;
   }

   me_ptr->flags.is_any_ext_in_mf_pending = is_any_mf_pending;
   return result;
}


/**
 * called for all use cases. both for internal and external clients.
 */
ar_result_t gen_cntr_input_media_format_received(void *                                  ctx_ptr,
                                                 gu_ext_in_port_t *                      gu_ext_in_port_ptr,
                                                 topo_media_fmt_t *                      media_fmt_ptr,
                                                 cu_ext_in_port_upstream_frame_length_t *upstream_frame_len_ptr,
                                                 bool_t                                  is_data_path)
{
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   ar_result_t             result                = AR_EOK;
   gen_cntr_t             *me_ptr                = (gen_cntr_t *)ctx_ptr;
   gen_cntr_ext_in_port_t *ext_in_port_ptr       = (gen_cntr_ext_in_port_t *)gu_ext_in_port_ptr;
   gen_topo_input_port_t  *in_port_ptr           = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   gen_topo_module_t      *module_ptr            = (gen_topo_module_t*)in_port_ptr->gu.cmn.module_ptr;
   bool_t                  FORCE_AGGREGATE_FALSE = FALSE;
   bool_t                  is_pack_pcm           = SPF_IS_PACKETIZED_OR_PCM(media_fmt_ptr->data_format);

   if (is_pack_pcm)
   {
      TOPO_PRINT_PCM_MEDIA_FMT(me_ptr->topo.gu.log_id, media_fmt_ptr, "container input");
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "Container input media format SPF_data_format: %lu, fmt_id: 0x%lX",
                   media_fmt_ptr->data_format,
                   media_fmt_ptr->fmt_id);
   }

   topo_port_state_t in_port_sg_state;

   // media format propagation depends on sg state not on port state because port state depends on downstream as
   // well. (due to propagation)
   in_port_sg_state = topo_sg_state_to_port_state(gen_topo_get_sg_state(in_port_ptr->gu.cmn.module_ptr->sg_ptr));

   /**
    * Control path:
    * Propagate control path media fmt only if the SG is in prepare state or in start state and in data-flow-gap.
    *    If SG is in start state and data-flow state, then data path takes care of propagation.
    *    If SG is in stop state, then handle_prepare cmd will take care.
    * Data path:
    * Propagate data path media fmt only if the SG started.
    */
   bool_t is_propagation_possible = FALSE;
   if (!is_data_path)
   {
      if ((TOPO_PORT_STATE_PREPARED == in_port_sg_state) ||
          (TOPO_PORT_STATE_STARTED == in_port_sg_state &&
           TOPO_DATA_FLOW_STATE_AT_GAP == in_port_ptr->common.data_flow_state))
      {
         is_propagation_possible = TRUE;
      }
   }
   else
   {
      if (TOPO_PORT_STATE_STARTED == in_port_sg_state)
      {
         is_propagation_possible = TRUE;
      }
   }

   SPF_CRITICAL_SECTION_START(me_ptr->cu.gu_ptr);

   // Update total_bytes, such that duration remains constant.
   if (tu_has_media_format_changed(&ext_in_port_ptr->cu.media_fmt, media_fmt_ptr))
   {
      // if media format is changed then release (drop) the buffer held in the prebuffer Q
      cu_ext_in_release_prebuffers(&me_ptr->cu, &ext_in_port_ptr->gu);

      if (is_data_path && (GEN_TOPO_SIGNAL_TRIGGER == me_ptr->topo.proc_context.curr_trigger) &&
          in_port_ptr->common.bufs_ptr[0].actual_data_len)
      {
         // for media fmt to propagate there should be no pending data on the port.
         GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                             DBG_ERROR_PRIO,
                             "Dropping %lu data at (0x%lX, 0x%lx). Signal trigger cannot wait for pending data to be "
                             "flushed.",
                             in_port_ptr->common.bufs_ptr[0].actual_data_len,
                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);

         gen_topo_drop_all_metadata_within_range(me_ptr->topo.gu.log_id,
                                                 (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr,
                                                 &in_port_ptr->common,
                                                 gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common),
                                                 FALSE /*keep_eos_and_ba_md*/);

         gen_topo_set_all_bufs_len_to_zero(&in_port_ptr->common);
      }

      // Copy incoming media format as the actual media format of the external input port.
      tu_copy_media_fmt(&ext_in_port_ptr->cu.media_fmt, media_fmt_ptr);

      // Transfer upstream_max_frame_len_bytes to downstream container
      if (upstream_frame_len_ptr)
      {
         ext_in_port_ptr->cu.upstream_frame_len.frame_len_bytes   = upstream_frame_len_ptr->frame_len_bytes;
         ext_in_port_ptr->cu.upstream_frame_len.frame_len_samples = upstream_frame_len_ptr->frame_len_samples;
         ext_in_port_ptr->cu.upstream_frame_len.frame_len_us      = upstream_frame_len_ptr->frame_len_us;
         ext_in_port_ptr->cu.upstream_frame_len.sample_rate       = upstream_frame_len_ptr->sample_rate;
      }

      // Copy external input port media fmt to internal.
      TRY(result, gen_topo_set_input_port_media_format(&me_ptr->topo, in_port_ptr, &ext_in_port_ptr->cu.media_fmt));

      // ext in port receives packed deinterleaved, but preprocess (for PCM) in gen_cntr will convert to unpacked when
      // it sends to
      // first module. override here.
      if (SPF_IS_PCM_DATA_FORMAT(media_fmt_ptr->data_format) &&
          TOPO_DEINTERLEAVED_PACKED == media_fmt_ptr->pcm.interleaving)
      {
         topo_media_fmt_t tmp_mf = *in_port_ptr->common.media_fmt_ptr;
         tmp_mf.pcm.interleaving = module_ptr->flags.supports_deintlvd_unpacked_v2 ? TOPO_DEINTERLEAVED_UNPACKED_V2
                                                                                   : TOPO_DEINTERLEAVED_UNPACKED;

         tu_set_media_fmt(&me_ptr->topo.mf_utils, &in_port_ptr->common.media_fmt_ptr, &tmp_mf, me_ptr->topo.heap_id);
      }

      gen_topo_reset_pcm_unpacked_mask(&in_port_ptr->common);

      if (is_propagation_possible)
      {
         TRY(result, me_ptr->cu.topo_vtbl_ptr->propagate_media_fmt(&me_ptr->topo, is_data_path));
         TRY(result, gen_cntr_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE));

         //threshold will be updated as part of event handling.
         // this function could be in command context therefore reconcile the flags before handling the events.
         gen_cntr_reconcile_and_handle_fwk_events(me_ptr);

         // send media format downstream in control path.
         if (!is_data_path)
         {
            for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
                 (NULL != ext_out_port_list_ptr);
                 LIST_ADVANCE(ext_out_port_list_ptr))
            {
               gen_cntr_ext_out_port_t *ext_out_port_ptr =
                  (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
               gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

               if (ext_out_port_ptr->flags.out_media_fmt_changed || out_port_ptr->common.flags.media_fmt_event)
               {
                  gen_cntr_ext_out_port_apply_pending_media_fmt(me_ptr, &ext_out_port_ptr->gu);
               }
            }
         }
      }

      // gen_topo_handle_data_flow_preflow(&me_ptr->topo, &in_port_ptr->common, &in_port_ptr->gu.cmn);
   }
   else if ((upstream_frame_len_ptr) &&
            (cu_has_upstream_frame_len_changed(&ext_in_port_ptr->cu.upstream_frame_len, upstream_frame_len_ptr, media_fmt_ptr)))
   {
      cu_ext_in_release_prebuffers(&me_ptr->cu, &ext_in_port_ptr->gu);

      // Transfer upstream_max_frame_len_bytes to downstream container
      ext_in_port_ptr->cu.upstream_frame_len.frame_len_bytes   = upstream_frame_len_ptr->frame_len_bytes;
      ext_in_port_ptr->cu.upstream_frame_len.frame_len_samples = upstream_frame_len_ptr->frame_len_samples;
      ext_in_port_ptr->cu.upstream_frame_len.frame_len_us      = upstream_frame_len_ptr->frame_len_us;
      ext_in_port_ptr->cu.upstream_frame_len.sample_rate       = upstream_frame_len_ptr->sample_rate;

      // set upstream framelength changed event
      CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, upstream_frame_len_change);

      if (is_propagation_possible)
      {
         // Marking port threshold event to propagated upstream frame len
         GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(&me_ptr->topo, port_thresh);
         gen_cntr_reconcile_and_handle_fwk_events(me_ptr);
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   /*If external input is marked for preserving prebuffer then set handle_frame_done to true so that prebuffer Q can be
    * disabled after next frame processing*/
   if (ext_in_port_ptr->cu.preserve_prebuffer)
   {
      me_ptr->topo.flags.need_to_handle_frame_done = TRUE;

      THIN_TOPO_SET_EXIT_FLAG((&me_ptr->topo), has_to_preserve_prebuffer, TRUE);
   }

   SPF_CRITICAL_SECTION_END(me_ptr->cu.gu_ptr);

   return result;
}

ar_result_t gen_cntr_check_process_input_media_fmt_data_cmd(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;
   // process any partially processed data
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   if (in_port_ptr->flags.processing_began)
   {
      bool_t IS_MF_PENDING_TRUE = TRUE;
      gen_cntr_check_set_input_discontinuity_flag(me_ptr, ext_in_port_ptr, IS_MF_PENDING_TRUE);
   }

   // process frames takes care of processing input data cmd (media fmt etc) at the end.
   if (!ext_in_port_ptr->flags.input_discontinuity)
   {
      result = gen_cntr_process_pending_input_data_cmd(me_ptr);
      // need to make sure once input data cmd is released, appropriate mask is set
   }

   return result;
}
