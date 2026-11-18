/**
 * \file spl_cntr_ext_port_util.c
 * \brief
 *     This file contains spl_cntr utility functions for managing external ports (input and output.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "spl_cntr_i.h"

/**
 * Helper function for intializing a spl_topo_ext_buf_t used by external input/output ports.
 */
static ar_result_t spl_cntr_init_ext_port_local_buf(spl_cntr_t *me_ptr, spl_topo_ext_buf_t *local_buf_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   VERIFY(result, local_buf_ptr);

   local_buf_ptr->buf_ptr               = NULL;
   local_buf_ptr->num_bufs              = 0;

   local_buf_ptr->buf_timestamp_info.timestamp           = 0;
   local_buf_ptr->buf_timestamp_info.offset_bytes_per_ch = 0;
   local_buf_ptr->buf_timestamp_info.is_valid            = FALSE;

   local_buf_ptr->newest_timestamp_info.timestamp           = 0;
   local_buf_ptr->newest_timestamp_info.offset_bytes_per_ch = 0;
   local_buf_ptr->newest_timestamp_info.is_valid            = FALSE;

   local_buf_ptr->timestamp             = 0;
   local_buf_ptr->timestamp_is_valid    = FALSE;
   local_buf_ptr->end_of_frame          = FALSE;
   local_buf_ptr->data_flow_state       = TOPO_DATA_FLOW_STATE_AT_GAP;
   local_buf_ptr->first_frame_after_gap = FALSE;

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/*
 * Make it so that the pending media format is no longer pending. Using data
 * format unknown to check this.
 */
ar_result_t spl_cntr_invalidate_pending_media_fmt(topo_media_fmt_t *media_fmt_ptr)
{
   media_fmt_ptr->data_format = SPF_UNKNOWN_DATA_FORMAT;
   return AR_EOK;
}

/*
 * Initialize an external input port.
 */
ar_result_t spl_cntr_init_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr)
{
   ar_result_t             result       = AR_EOK;
   spl_cntr_t *            me_ptr       = (spl_cntr_t *)base_ptr;
   spl_cntr_ext_in_port_t *ext_port_ptr = (spl_cntr_ext_in_port_t *)gu_ext_port_ptr;
   ext_port_ptr->cu.id                  = cu_get_next_unique_id(&(me_ptr->cu));

   ext_port_ptr->gu.this_handle.cmd_handle_ptr       = &me_ptr->cu.cmd_handle;
   ext_port_ptr->held_data_msg_consumed_bytes_per_ch = 0;

   spl_cntr_init_ext_port_local_buf(me_ptr, &ext_port_ptr->topo_buf);

   result = spl_cntr_init_ext_in_queue(base_ptr, gu_ext_port_ptr);

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "Creating external input port idx = %ld, miid = 0x%lx. channel bit = 0x%x",
                ext_port_ptr->gu.int_in_port_ptr->cmn.index,
                ext_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_port_ptr->cu.bit_mask);

   // Start off with no pending media format.
   spl_cntr_invalidate_pending_media_fmt(&ext_port_ptr->pending_media_fmt);

   return result;
}

/**
 * Determines the number of elements needed in the external input port data
 * queue.
 */
uint32_t spl_cntr_get_in_queue_num_elements(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_port_ptr)
{
   return CU_MAX_INP_DATA_Q_ELEMENTS;
}

/*
 * Init an external input port's data queue. Determines the number of elements,
 * bit mask, and name, and calls a common function to allocate the queue.
 */
ar_result_t spl_cntr_init_ext_in_queue(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr)
{
   ar_result_t             result       = AR_EOK;
   spl_cntr_t *            me_ptr       = (spl_cntr_t *)base_ptr;
   spl_cntr_ext_in_port_t *ext_port_ptr = (spl_cntr_ext_in_port_t *)gu_ext_port_ptr;

   char data_q_name[POSAL_DEFAULT_NAME_LEN]; // data queue name
   snprintf(data_q_name, POSAL_DEFAULT_NAME_LEN, "%s%s%8lX", "D", "SPL_CNTR", me_ptr->topo.t_base.gu.log_id);

   uint32_t num_elements = spl_cntr_get_in_queue_num_elements(me_ptr, ext_port_ptr);

   uint32_t bit_mask = cu_request_bit_in_bit_mask(&me_ptr->cu.available_bit_mask);
   if (0 == bit_mask)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "While creating external input queue idx = %ld, miid = 0x%lx, bit mask has no bits available 0x%lx",
                   ext_port_ptr->gu.int_in_port_ptr->cmn.index,
                   ext_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   me_ptr->cu.available_bit_mask);
      return result;
   }

   ext_port_ptr->cu.bit_mask = bit_mask;
   result =
      cu_init_queue(&me_ptr->cu,
                    data_q_name,
                    num_elements,
                    ext_port_ptr->cu.bit_mask,
                    spl_cntr_input_data_q_trigger,
                    me_ptr->cu.channel_ptr,
                    &gu_ext_port_ptr->this_handle.q_ptr,
                    CU_PTR_PUT_OFFSET(gu_ext_port_ptr, ALIGN_8_BYTES(sizeof(spl_cntr_ext_in_port_t))),
                    gu_get_downgraded_heap_id(me_ptr->topo.t_base.heap_id, gu_ext_port_ptr->upstream_handle.heap_id));

   return result;
}

/**
 * Deinitialize an external input port.
 */
ar_result_t spl_cntr_deinit_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr)
{
   INIT_EXCEPTION_HANDLING

   ar_result_t             result       = AR_EOK;
   spl_cntr_t *            me_ptr       = (spl_cntr_t *)base_ptr;
   spl_cntr_ext_in_port_t *ext_port_ptr = (spl_cntr_ext_in_port_t *)gu_ext_port_ptr;

   TRY(result, spl_topo_deinit_ext_in_port(&me_ptr->topo, gu_ext_port_ptr));

   // Free the external input buffer.
   if (ext_port_ptr->topo_buf.buf_ptr)
   {
      MFREE_NULLIFY(ext_port_ptr->topo_buf.buf_ptr[0].data_ptr);
   }
   MFREE_NULLIFY(ext_port_ptr->topo_buf.buf_ptr);

   ext_port_ptr->topo_buf.buf_timestamp_info.timestamp           = 0;
   ext_port_ptr->topo_buf.buf_timestamp_info.offset_bytes_per_ch = 0;
   ext_port_ptr->topo_buf.buf_timestamp_info.is_valid            = FALSE;

   ext_port_ptr->topo_buf.newest_timestamp_info.timestamp           = 0;
   ext_port_ptr->topo_buf.newest_timestamp_info.offset_bytes_per_ch = 0;
   ext_port_ptr->topo_buf.newest_timestamp_info.is_valid            = FALSE;

   // Clear and release gpd bits.
   spl_cntr_remove_ext_in_port_from_gpd_mask(me_ptr, ext_port_ptr);

   cu_clear_bits_in_x(&me_ptr->gpd_optional_mask, ext_port_ptr->cu.bit_mask);
   ext_port_ptr->topo_buf.send_to_topo = FALSE;

   cu_release_bit_in_bit_mask(&me_ptr->cu, ext_port_ptr->cu.bit_mask);
   spf_svc_deinit_data_queue(ext_port_ptr->gu.this_handle.q_ptr);

   // invalidate the association with internal port, so that dangling link can be destroyed first
   gu_deinit_ext_in_port(gu_ext_port_ptr);

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Check if there is any data in the local buffer.
 */
bool_t spl_cntr_ext_in_port_has_unconsumed_data(gu_ext_in_port_t *gu_ext_port_ptr)
{
   bool_t IS_MAX_FALSE = FALSE;
   return (0 != spl_cntr_ext_in_port_get_buf_len(gu_ext_port_ptr, IS_MAX_FALSE));
}

/**
 * Check if there is any data in the local buffer.
 */
uint32_t spl_cntr_ext_in_port_get_buf_len(gu_ext_in_port_t *gu_ext_port_ptr, bool_t is_max)
{
   spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)gu_ext_port_ptr;
   uint32_t                buf_len         = 0;
   if (is_max)
   {
      buf_len = spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr)
                   ? (ext_in_port_ptr->topo_buf.buf_ptr->max_data_len * ext_in_port_ptr->cu.media_fmt.pcm.num_channels)
                   : 0;
   }
   else
   {
      buf_len =
         spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr)
            ? (ext_in_port_ptr->topo_buf.buf_ptr->actual_data_len * ext_in_port_ptr->cu.media_fmt.pcm.num_channels)
            : 0;
   }
   return buf_len;
}

/**
 * If the media format has changed on this port, then copies from the
 * external to internal port, and then propagates media format if already
 * prepared.
 */
ar_result_t spl_cntr_input_media_format_received(void *                                  ctx_ptr,
                                                 gu_ext_in_port_t *                      gu_ext_in_port_ptr,
                                                 topo_media_fmt_t *                      media_fmt_ptr,
                                                 cu_ext_in_port_upstream_frame_length_t *upstream_frame_len_ptr,
                                                 bool_t                                  is_data_path)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   spl_cntr_t *            me_ptr          = (spl_cntr_t *)ctx_ptr;
   spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)gu_ext_in_port_ptr;
   uint32_t                port_index      = 0;

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "SPL_CNTR received media format on external input port idx %ld miid 0x%lx, is_data_path %ld, data "
                "format %lu, "
                "fmt_id 0x%lx us_frame_len %lu",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                is_data_path,
                media_fmt_ptr->data_format,
                media_fmt_ptr->fmt_id,
                (upstream_frame_len_ptr ? upstream_frame_len_ptr->frame_len_us : 0));

   if (media_fmt_ptr->pcm.interleaving == TOPO_INTERLEAVED)
   {
      AR_MSG(DBG_ERROR_PRIO, "Received Interleaved data");
      return AR_EFAILED;
   }

   if (SPF_DEINTERLEAVED_RAW_COMPRESSED == media_fmt_ptr->data_format)
   {
      AR_MSG(DBG_ERROR_PRIO, "Received Deinterleaved Raw Compressed data");
      return AR_EFAILED;
   }

   spl_topo_module_t *module_ptr = (spl_topo_module_t *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr;

   media_fmt_ptr->pcm.interleaving = module_ptr->t_base.flags.supports_deintlvd_unpacked_v2
                                        ? TOPO_DEINTERLEAVED_UNPACKED_V2
                                        : TOPO_DEINTERLEAVED_UNPACKED;

   TOPO_PRINT_PCM_MEDIA_FMT(me_ptr->topo.t_base.gu.log_id, media_fmt_ptr, "SPL_CNTR input");

   if (tu_has_media_format_changed(&ext_in_port_ptr->cu.media_fmt, media_fmt_ptr))
   {
      // Set cached media fmt.
      tu_copy_media_fmt(&ext_in_port_ptr->pending_media_fmt, media_fmt_ptr);

      // We are done using the media format message, so free it. This is useful
      // for cases when we call check_and_process audio, since that will try and
      // buffer the held msg as if it was a data msg. Freeing here prevents this
      // behavior.
      cu_free_input_data_cmd(&me_ptr->cu, &ext_in_port_ptr->gu, result);

      // if media format changed then delete the data messages from the prebuffer Q.
      cu_ext_in_release_prebuffers(&me_ptr->cu, &ext_in_port_ptr->gu);

      // Deliver any partial data on all outputs, to avoid any unnecessary partially
      // filled buffer cases
      for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
           ext_out_port_list_ptr;
           LIST_ADVANCE(ext_out_port_list_ptr), port_index++)
      {
         me_ptr->topo.t_base.proc_context.ext_out_port_scratch_ptr[port_index].flags.release_ext_out_buf = TRUE;
      }

      if (spl_cntr_ext_in_port_has_unconsumed_data(&(ext_in_port_ptr->gu)))
      {
         // If there is unconsumed data on the input, we need to force that data
         // through the topology. So call check and process audio as if we had come
         // from an input data msg trigger.
         TRY(result, spl_cntr_check_and_process_audio(me_ptr, ext_in_port_ptr->cu.bit_mask));
      }
      else
      {
         // If there is no unconsumed data in the external input port, we can apply
         // the pending media format immediately. Otherwise, we have to check and apply
         // after topo process.
         spl_cntr_ext_in_port_apply_pending_media_fmt(me_ptr, ext_in_port_ptr, is_data_path);

         // Check if this media format made it to any external output ports. If so, we need to apply
         // on the output side now.
         spl_cntr_check_apply_ext_out_ports_pending_media_fmt(&(me_ptr->topo.t_base), is_data_path);
      }
   }

   // cache if upstream frame len is changed.
   if (upstream_frame_len_ptr && (cu_has_upstream_frame_len_changed(&ext_in_port_ptr->cu.upstream_frame_len,
                                                                         upstream_frame_len_ptr,
                                                                         media_fmt_ptr)))
   {
      // Transfer upstream_max_frame_len_bytes to downstream container
      ext_in_port_ptr->cu.upstream_frame_len.frame_len_bytes   = upstream_frame_len_ptr->frame_len_bytes;
      ext_in_port_ptr->cu.upstream_frame_len.frame_len_samples = upstream_frame_len_ptr->frame_len_samples;
      ext_in_port_ptr->cu.upstream_frame_len.frame_len_us      = upstream_frame_len_ptr->frame_len_us;
      ext_in_port_ptr->cu.upstream_frame_len.sample_rate       = upstream_frame_len_ptr->sample_rate;

      // set upstream framelength changed event inorder to update path delay.
      CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, upstream_frame_len_change);
   }

   // handle events
   TRY(result, spl_cntr_handle_fwk_events(me_ptr, is_data_path /*is_data_path*/));

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

ar_result_t spl_cntr_ext_in_port_apply_pending_media_fmt(spl_cntr_t *            me_ptr,
                                                         spl_cntr_ext_in_port_t *ext_port_ptr,
                                                         bool_t                  is_data_path)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   cu_base_t *            base_ptr              = &me_ptr->cu;
   cu_ext_in_port_t *     ext_in_port_ptr       = &ext_port_ptr->cu;
   topo_media_fmt_t *     media_fmt_ptr         = &ext_port_ptr->pending_media_fmt;
   spl_topo_input_port_t *in_port_ptr           = (spl_topo_input_port_t *)ext_port_ptr->gu.int_in_port_ptr;
   bool_t                 FORCE_AGGREGATE_FALSE = FALSE;
   topo_port_state_t      in_port_sg_state;

   VERIFY(result, base_ptr->topo_vtbl_ptr && base_ptr->topo_vtbl_ptr->propagate_media_fmt);

   if (!spl_cntr_media_fmt_is_pending(&ext_port_ptr->pending_media_fmt))
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "No pending media fmt to apply.");
#endif
      return AR_EFAILED;
   }

   // Copy incoming media format as the actual media format of the external input port.
   tu_copy_media_fmt(&ext_in_port_ptr->media_fmt, media_fmt_ptr);

   // Set the internal port's media fmt to pending.
   in_port_ptr->t_base.common.flags.media_fmt_event = TRUE;
   me_ptr->topo.simpt_event_flags.check_pending_mf   = TRUE;

   // Copy external input port media fmt to internal.
   TRY(result,
       gen_topo_set_input_port_media_format(&me_ptr->topo.t_base, &in_port_ptr->t_base, &ext_in_port_ptr->media_fmt));

   spl_topo_update_check_valid_mf_event_flag(&me_ptr->topo,
                                              &in_port_ptr->t_base.gu.cmn,
                                              in_port_ptr->t_base.common.flags.is_mf_valid);

   // media format propagation depends on sg state not on port state because port state depends on downstream as well.
   // (due to propagation)
   in_port_sg_state = topo_sg_state_to_port_state(gen_topo_get_sg_state(in_port_ptr->t_base.gu.cmn.module_ptr->sg_ptr));

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
           TOPO_DATA_FLOW_STATE_AT_GAP == in_port_ptr->t_base.common.data_flow_state))
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

   if (is_propagation_possible)
   {
      TRY(result, spl_topo_propagate_media_fmt(base_ptr->topo_ptr, is_data_path));
      TRY(result, spl_cntr_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE));
      TRY(result, spl_cntr_handle_clk_vote_change(me_ptr, CU_PM_REQ_KPPS_BW, TRUE, FORCE_AGGREGATE_FALSE, NULL, NULL));
   }

   // Pending media format is applied and therefore no longer pending.
   spl_cntr_invalidate_pending_media_fmt(&ext_port_ptr->pending_media_fmt);

   // update threshold/frame_len and check allocate external buffers
   TRY(result, base_ptr->cntr_vtbl_ptr->port_data_thresh_change(base_ptr));

   // Query fixed output samples, since new port may have become active due to media format.
   TRY(result, spl_cntr_query_topo_req_samples(me_ptr));

   // we have to update the simplified topo connection here as well.
   // Reason: if Sync/SAL which supports disabling itself in SISO operation is present at external input and if it
   // receives the new media format at input but doesn't generate any output media format then this module can not be
   // kept disabled for simplified topo and must be included in the connection.
   spl_topo_update_simp_module_connections(&me_ptr->topo);

   CATCH(result, SPL_CNTR_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

/*
 * Initialize an external output port.
 */
ar_result_t spl_cntr_init_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr)
{
   ar_result_t              result       = AR_EOK;
   spl_cntr_t *             me_ptr       = (spl_cntr_t *)base_ptr;
   spl_cntr_ext_out_port_t *ext_port_ptr = (spl_cntr_ext_out_port_t *)gu_ext_port_ptr;

   ext_port_ptr->sent_media_fmt = FALSE;

   spl_cntr_init_ext_port_local_buf(me_ptr, &ext_port_ptr->topo_buf);

   // Handle to the container command queue.
   ext_port_ptr->gu.this_handle.cmd_handle_ptr = &me_ptr->cu.cmd_handle;

   result = spl_cntr_init_ext_out_queue(base_ptr, gu_ext_port_ptr);

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "Creating external output port idx = %ld, miid = 0x%lx. channel bit = 0x%x",
                ext_port_ptr->gu.int_out_port_ptr->cmn.index,
                ext_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                ext_port_ptr->cu.bit_mask);

   spl_cntr_invalidate_pending_media_fmt(&ext_port_ptr->pending_media_fmt);

   return result;
}

/**
 * Implementation of topo_to_cntr callback to returns the external buffer associated with this external output port.
 */
void *spl_cntr_ext_out_port_get_ext_buf(gu_ext_out_port_t *gu_ext_out_port_ptr)
{
   spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)gu_ext_out_port_ptr;
   return &(ext_out_port_ptr->topo_buf);
}

/*
 * Init an external output port's data queue. Determines the number of elements,
 * bit mask, and name, and calls a common function to allocate the queue.
 */
ar_result_t spl_cntr_init_ext_out_queue(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr)
{
   ar_result_t              result       = AR_EOK;
   spl_cntr_t *             me_ptr       = (spl_cntr_t *)base_ptr;
   spl_cntr_ext_out_port_t *ext_port_ptr = (spl_cntr_ext_out_port_t *)gu_ext_port_ptr;

   char data_q_name[POSAL_DEFAULT_NAME_LEN]; // data queue name
   snprintf(data_q_name, POSAL_DEFAULT_NAME_LEN, "%s%s%8lX", "B", "SPL_CNTR", me_ptr->topo.t_base.gu.log_id);

   // init Q with CU_MAX_OUT_BUF_Q_ELEMENTS elements
   uint32_t num_elements = CU_MAX_OUT_BUF_Q_ELEMENTS;

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "SPL_CNTR number of bufQ elements for ext_out_port idx = %ld, miid = 0x%lx is determined to be %lu",
                ext_port_ptr->gu.int_out_port_ptr->cmn.index,
                ext_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                num_elements);

   uint32_t bit_mask = cu_request_bit_in_bit_mask(&me_ptr->cu.available_bit_mask);

   if (0 == bit_mask)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Bit mask has no bits available 0x%lx",
                   me_ptr->cu.available_bit_mask);
      return AR_EFAILED;
   }

   ext_port_ptr->cu.bit_mask = bit_mask;

   result = cu_init_queue(&me_ptr->cu,
                            data_q_name,
                            num_elements,
                            ext_port_ptr->cu.bit_mask,
                            spl_cntr_output_buf_q_trigger,
                            me_ptr->cu.channel_ptr,
                            &gu_ext_port_ptr->this_handle.q_ptr,
                            CU_PTR_PUT_OFFSET(gu_ext_port_ptr, ALIGN_8_BYTES(sizeof(spl_cntr_ext_out_port_t))),
                             gu_get_downgraded_heap_id(me_ptr->topo.t_base.heap_id, gu_ext_port_ptr->downstream_handle.heap_id));

   return result;
}

/*
 * Deinitialize an external output port.
 */
ar_result_t spl_cntr_deinit_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr)
{
   INIT_EXCEPTION_HANDLING

   ar_result_t              result       = AR_EOK;
   spl_cntr_t *             me_ptr       = (spl_cntr_t *)base_ptr;
   spl_cntr_ext_out_port_t *ext_port_ptr = (spl_cntr_ext_out_port_t *)gu_ext_port_ptr;

   TRY(result, spl_topo_deinit_ext_out_port(&me_ptr->topo, gu_ext_port_ptr));

   spl_cntr_return_held_out_buf(me_ptr, ext_port_ptr);

   // Clear and release gpd bit from gpd mask.
   spl_cntr_remove_ext_out_port_from_gpd_mask(me_ptr, ext_port_ptr);
   cu_clear_bits_in_x(&me_ptr->gpd_optional_mask, ext_port_ptr->cu.bit_mask);

   cu_release_bit_in_bit_mask(&me_ptr->cu, ext_port_ptr->cu.bit_mask);
   spf_svc_deinit_buf_queue(ext_port_ptr->gu.this_handle.q_ptr, &ext_port_ptr->cu.num_buf_allocated);

   // Free the external output buffer and temporary buffer.
   MFREE_NULLIFY(ext_port_ptr->topo_buf.buf_ptr);
   MFREE_NULLIFY(ext_port_ptr->temp_out_buf_ptr);

   // invalidate the association with internal port, so that dangling link can be destroyed first
   gu_deinit_ext_out_port(gu_ext_port_ptr);

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Remove all data from this external input port's local buffer. Clears the data flow state marker.
 */
ar_result_t spl_cntr_ext_in_port_flush_local_buffer(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t            result          = AR_EOK;
   spl_topo_input_port_t *int_in_port_ptr = (spl_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   // Flush local buffering.
   if (spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr) &&
       (NULL != ext_in_port_ptr->topo_buf.buf_ptr[0].data_ptr))
   {
      if (0 != ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len)
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Dropping %lu bytes of data in external input port input port idx %ld miid 0x%lx. (This may not "
                      "be an "
                      "error.)",
                      ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len * ext_in_port_ptr->topo_buf.num_bufs,
                      int_in_port_ptr->t_base.gu.cmn.index,
                      int_in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
      }

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
      for (uint32_t i = 0; i < ext_in_port_ptr->topo_buf.num_bufs; i++)
      {
         ext_in_port_ptr->topo_buf.buf_ptr[i].actual_data_len = 0;
      }
#else
      ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len = 0;
#endif
   }

   if (int_in_port_ptr->t_base.common.sdata.metadata_list_ptr)
   {
      bool_t IS_DROPPED_TRUE = TRUE;
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "Destroying all metadata for input port idx %ld miid 0x%lx",
                   int_in_port_ptr->t_base.gu.cmn.index,
                   int_in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);

      TRY(result,
          gen_topo_destroy_all_metadata(me_ptr->topo.t_base.gu.log_id,
                                        (void *)int_in_port_ptr->t_base.gu.cmn.module_ptr,
                                        &(int_in_port_ptr->t_base.common.sdata.metadata_list_ptr),
                                        IS_DROPPED_TRUE));

      // At this point, md_list is NULL so it cannot contain any flushing eos. Set marker to FALSE.
      int_in_port_ptr->t_base.common.sdata.flags.marker_eos = FALSE;
   }

   // Buffered data is gone -> buffered timestamps are irrelevant. Remove them.
   bool_t is_local_buf_timestamp_valid = ext_in_port_ptr->topo_buf.buf_timestamp_info.is_valid;
   TRY(result, spl_cntr_ext_in_port_clear_timestamp_discontinuity(&(me_ptr->topo.t_base), &(ext_in_port_ptr->gu)));

   // If we dropped data but there's still unconsumed data in the held buffer, we should set the buffer head timestamp
   // to the data_msg timestamp. If consumed_bytes == 0 then this happens naturally when buffering input data to the
   // local buffer.
   if(0 != ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch && (TRUE == is_local_buf_timestamp_valid))
   {
      TRY(result, spl_cntr_ext_in_port_re_push_data_msg_ts_to_local_buf(me_ptr, ext_in_port_ptr));
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Returns all messages in the external input port's data queue. Flushes data
 * from the local buffer.
 */
ar_result_t spl_cntr_flush_input_data_queue(spl_cntr_t *            me_ptr,
                                            spl_cntr_ext_in_port_t *ext_in_port_ptr,
                                            bool_t keep_data_msg, // keeps Media format and other data messages
                                            bool_t buffer_data) // buffer data to the local buffer (as much as possible)
{
   INIT_EXCEPTION_HANDLING

   ar_result_t result             = AR_EOK;
   void *      pushed_payload_ptr = NULL;

   if (NULL == ext_in_port_ptr->gu.this_handle.q_ptr)
   {
      return AR_EOK;
   }

   //move prebuffers to the the main Q
   cu_ext_in_requeue_prebuffers(&me_ptr->cu, &ext_in_port_ptr->gu);
   // Since data will be dropped therefore set the preserve prebuffer flag so that prebuffers can be preserved during
   // next frame processing.
   ext_in_port_ptr->cu.preserve_prebuffer = TRUE;

   // If local buffer doesn't exist then can't buffer data.
   if (!spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr))
   {
      buffer_data = FALSE;
   }

   // Flush local buffer if data buffering is not requested.
   if (!buffer_data)
   {
      TRY(result, spl_cntr_ext_in_port_flush_local_buffer(me_ptr, ext_in_port_ptr));
   }

   // If no held buffer, pop new message.
   if (NULL == ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      cu_get_input_data_msg(&me_ptr->cu, &ext_in_port_ptr->gu);
   }

   while (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      // Ignore non data buffer messages. Buffer data as long as there is space in the local buffer.
      if (buffer_data && (SPF_MSG_DATA_BUFFER == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode) &&
          (0 < spl_cntr_ext_in_get_free_space(me_ptr, ext_in_port_ptr)))
      {
         uint32_t data_needed = 0;
         TRY(result, spl_cntr_buffer_held_input_data(me_ptr, ext_in_port_ptr, &data_needed));
      }
      else if (SPF_MSG_DATA_BUFFER != ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
      {
         buffer_data = FALSE; // can't buffer more data after media format message.
      }

      // if data message, then push back to queue. Also stop popping when we see the first message we pushed
      if (keep_data_msg && (SPF_MSG_DATA_BUFFER != ext_in_port_ptr->cu.input_data_q_msg.msg_opcode))
      {
         if (NULL == pushed_payload_ptr)
         {
            pushed_payload_ptr = ext_in_port_ptr->cu.input_data_q_msg.payload_ptr;
         }

         if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_LOW_PRIO,
                         "Pushing data msg 0x%p back to queue during flush. opcode 0x%lX.",
                         ext_in_port_ptr->cu.input_data_q_msg.payload_ptr,
                         ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);

            TRY(result,
                posal_queue_push_back(ext_in_port_ptr->gu.this_handle.q_ptr,
                                      (posal_queue_element_t *)&(ext_in_port_ptr->cu.input_data_q_msg)));
            ext_in_port_ptr->cu.input_data_q_msg.payload_ptr = NULL;
         }
      }
      else
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_LOW_PRIO,
                      "dropping data msg 0x%p from the queue during flush. opcode 0x%lX.",
                      ext_in_port_ptr->cu.input_data_q_msg.payload_ptr,
                      ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);

         // first free up any data q msgs that we are already holding
         cu_free_input_data_cmd(&me_ptr->cu, &ext_in_port_ptr->gu, AR_EOK);
      }

      // There is no more held data message.
      ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch = 0;

      // peek and see if front of queue has the message we pushed back, if so, don't pop
      if (pushed_payload_ptr)
      {
         spf_msg_t *front_ptr = NULL;
         posal_queue_peek_front(ext_in_port_ptr->gu.this_handle.q_ptr, (posal_queue_element_t**)&front_ptr);
         if (front_ptr->payload_ptr == pushed_payload_ptr)
         {
            break;
         }
      }

      // Drain any queued buffers while there are input data messages.
      cu_get_input_data_msg(&me_ptr->cu, &ext_in_port_ptr->gu);
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return AR_EOK;
}

/**
 * Implementation of topo_to_cntr_vtable->ext_out_port_has_buffer
 * Returns whether or not there's a held node popped from the output port's bufQ, or if there's a temporary buffer (we
 * should still write into the temporary buffer therefore trigger policy should return TRUE).
 */
bool_t spl_cntr_ext_out_port_has_buffer(gu_ext_out_port_t *gu_ext_out_port_ptr)
{
   spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)gu_ext_out_port_ptr;
   return (NULL != ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr) || (NULL != ext_out_port_ptr->temp_out_buf_ptr);
}

/**
 * Applies a pending output media format (sets cu media format variable, resizes output buffers, sends media format
 * message downstream).
 *
 * This function should not be called if there is any unconsumed data in the output port. That needs to get delivered
 * first
 * since it would be at the old media format.
 */
ar_result_t spl_cntr_ext_out_port_apply_pending_media_fmt(spl_cntr_t *             me_ptr,
                                                          spl_cntr_ext_out_port_t *ext_out_port_ptr,
                                                          bool_t                   is_data_path)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   // Unused but needed for data thresh change event.
   spf_msg_t msg                     = { 0 };
   bool_t    mf_or_frame_len_changed = FALSE;
   bool_t    mf_changed              = FALSE;
   bool_t    FORCE_AGGREGATE_FALSE   = FALSE;
   if (spl_cntr_media_fmt_is_pending(&ext_out_port_ptr->pending_media_fmt))
   {
      mf_changed = tu_has_media_format_changed(&ext_out_port_ptr->cu.media_fmt, &ext_out_port_ptr->pending_media_fmt);
   }
   mf_or_frame_len_changed = mf_changed || (TRUE == ext_out_port_ptr->cu.flags.upstream_frame_len_changed);

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "An external output port buffer on port idx %ld miid 0x%lx, sent mf %ld, mf changed %ld.",
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                ext_out_port_ptr->sent_media_fmt,
                mf_changed);

   if ((!ext_out_port_ptr->sent_media_fmt || mf_or_frame_len_changed) &&
       (ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr))
   {
      // reseting this to avoid clearing pending media format if in case it is not sent.
      ext_out_port_ptr->sent_media_fmt = FALSE;

      // If we're holding an output buffer while applying pending media format, we need to return it before applying the
      // pending media format, or we'll get a memory corruption.
      bool_t swap_output_bufs = (NULL != ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr) && mf_or_frame_len_changed;
      if (swap_output_bufs)
      {
         // If we try to swap output buffers while there's unconsumed data, we would be dropping that data.
         VERIFY(result, !spl_cntr_ext_out_port_has_unconsumed_data(me_ptr, ext_out_port_ptr));

         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "An external output port buffer on port idx %ld miid 0x%lx was held while applying pending "
                      "media format. Returning and popping after resizing buffers.",
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
         cu_return_out_buf(&me_ptr->cu, &ext_out_port_ptr->gu);
         TRY(result, spl_cntr_clear_output_buffer(me_ptr, ext_out_port_ptr));
      }


      bool_t is_send_mf = FALSE;

      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)(ext_out_port_ptr->gu.int_out_port_ptr);
      topo_port_state_t       self_sg_state =
         topo_sg_state_to_port_state(gen_topo_get_sg_state(out_port_ptr->gu.cmn.module_ptr->sg_ptr));
      topo_port_state_t conn_port_state  = ext_out_port_ptr->cu.connected_port_state;
      topo_port_state_t downgraded_state = out_port_ptr->common.state;

      // There is no point is sending media format if self and downstream is not in started/prepared state
      if ((TOPO_PORT_STATE_STARTED == self_sg_state || TOPO_PORT_STATE_PREPARED == self_sg_state) &&
          (TOPO_PORT_STATE_STARTED == conn_port_state || TOPO_PORT_STATE_PREPARED == conn_port_state))
      {
         if (TOPO_PORT_STATE_STARTED == downgraded_state)
         {
            //if downstream is started then send media format if it is in data-path.
            //if media format is coming in control path then it will be sent in process context.
            is_send_mf = (is_data_path) ? TRUE : FALSE;
         }
         else
         {
            // If downstream down-graded state is not STARTED but since immediate downstream is either STARTED/PREPARED
            // therefore we can send the media format in control path.
            is_send_mf   = TRUE;
            is_data_path = FALSE;
         }
      }

      if (is_send_mf)
      {
         posal_queue_t *q_ptr = is_data_path
                                   ? ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr->q_ptr
                                   : ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr->cmd_handle_ptr->cmd_q_ptr;
         uint32_t media_fmt_opcode = is_data_path ? SPF_MSG_DATA_MEDIA_FORMAT : SPF_MSG_CMD_MEDIA_FORMAT;

         // Don't need this handling if media format didn't change.
         // if only frame length changed, we dont need to copy pending mf / handle kpps vote changed /ext buffer size change.
         // No need to handle ext buffer size change i.e spl_cntr_handle_ext_buffer_size_change(), since its
         // handled under fwk threshold change events context i.e spl_cntr_handle_int_port_data_thresh_change_event()
         if (mf_changed)
         {
            // Copy pending media format to output port.
            tu_copy_media_fmt(&ext_out_port_ptr->cu.media_fmt, &ext_out_port_ptr->pending_media_fmt);

            if (SPF_IS_PCM_DATA_FORMAT(ext_out_port_ptr->cu.media_fmt.data_format) &&
                TU_IS_ANY_DEINTERLEAVED_UNPACKED(ext_out_port_ptr->cu.media_fmt.pcm.interleaving))
            {
               ext_out_port_ptr->cu.media_fmt.pcm.interleaving = TOPO_DEINTERLEAVED_PACKED;
               // owing to spl_cntr_pack_external_output
            }

            // Resize buffers to new output media fmt.
            TRY(result, spl_cntr_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE));
            TRY(result,
                spl_cntr_handle_clk_vote_change(me_ptr, CU_PM_REQ_KPPS_BW, TRUE, FORCE_AGGREGATE_FALSE, NULL, NULL));
            spl_cntr_handle_ext_buffer_size_change((void *)me_ptr);
         }

         //if media format is valid then send it
         if (ext_out_port_ptr->cu.media_fmt.data_format != SPF_UNKNOWN_DATA_FORMAT)
         {
            // Create/deliver output media fmt.
            TRY(result,
                cu_create_media_fmt_msg_for_downstream(&me_ptr->cu, &ext_out_port_ptr->gu, &msg, media_fmt_opcode));
            TRY(result, cu_send_media_fmt_update_to_downstream(&me_ptr->cu, &ext_out_port_ptr->gu, &msg, q_ptr));

            ext_out_port_ptr->sent_media_fmt = TRUE;
         }
      }

      if (swap_output_bufs)
      {
         // Do NOT ignore AR_ENEEDMORE: We should be guaranteed to have an output buffer at this point since
         // we just returned one.
         TRY(result, spl_cntr_get_output_buffer(me_ptr, ext_out_port_ptr));

         // since we got the output buffer, update the gpd masks
         spl_cntr_update_gpd_and_cu_bit_mask(me_ptr);
      }
   }

   // We need to make sure we send the media format downstream at least once. If we don't send
   // during prepare, we should send on the data path even if media format is the same.
   if (ext_out_port_ptr->sent_media_fmt)
   {
      spl_cntr_invalidate_pending_media_fmt(&ext_out_port_ptr->pending_media_fmt);
      ext_out_port_ptr->cu.flags.upstream_frame_len_changed = FALSE;
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * For each external output port, checks if a media format is pending and if so,
 * delivers any data in the output buffer and then sends a media format message
 * downstream and clears the pending media fmt flag.
 */
ar_result_t spl_cntr_check_apply_ext_out_ports_pending_media_fmt(gen_topo_t *gen_topo_ptr, bool_t is_data_path)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result          = AR_EOK;
   spl_topo_t *topo_ptr        = (spl_topo_t *)gen_topo_ptr;
   spl_cntr_t *me_ptr          = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, topo_ptr);
   bool_t      delivered_audio = FALSE;

   // Check if this media format made it to any external output ports. If so, we need to apply
   // on the output side now.
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
        ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_HIGH_PRIO, "checking if output media fmt is pending! ");
#endif

      // If media fmt or output's frame length has changed, then send media fmt msg.
      if (spl_cntr_media_fmt_is_pending(&ext_out_port_ptr->pending_media_fmt) ||
          (TRUE == ext_out_port_ptr->cu.flags.upstream_frame_len_changed))
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_HIGH_PRIO, "output media fmt is now pending at fwk level! ");
#endif

         // Media format is pending, so we need to send buffer downstream and then send mf message downstream.
         if (!spl_cntr_is_output_buffer_empty(me_ptr, ext_out_port_ptr))
         {
            TRY(result, spl_cntr_deliver_output_buffer(me_ptr, ext_out_port_ptr));
            delivered_audio = TRUE;
         }

         spl_cntr_ext_out_port_apply_pending_media_fmt(me_ptr, ext_out_port_ptr, is_data_path);
      }
   }

   // If we delivered any audio, we should call the postgpd to set the output port gpd bit and
   // start listening for output again.
   if (delivered_audio)
   {
      // Query sample requirements. This should happen before postprocessing decision, in case enough samples
      // are already available
      TRY(result, spl_cntr_query_topo_req_samples(me_ptr));

      spl_cntr_update_gpd_and_cu_bit_mask(me_ptr);
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Set the external output port's pending media format to be the internal port's media format.
 * The topo layer uses this function to set the fwk layer media format after media format propagates
 * to the external port. The internal port will usually be an output port but could be an input port
 * if sending from the external input port itself.
 */
ar_result_t spl_cntr_set_pending_out_media_fmt(gen_topo_t             *gen_topo_ptr,
                                               gen_topo_common_port_t *cmn_port_ptr,
                                               gu_ext_out_port_t      *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)ext_out_port_ptr->int_out_port_ptr;

   gen_topo_initialize_bufs_sdata(gen_topo_ptr,
                                  &out_port_ptr->t_base.common,
                                  out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                                  out_port_ptr->t_base.gu.cmn.id);

   spl_cntr_ext_out_port_t *spl_cntr_ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_ptr;
   tu_copy_media_fmt(&spl_cntr_ext_out_port_ptr->pending_media_fmt, cmn_port_ptr->media_fmt_ptr);

   // The pending state has moved from the internal port to the external port.
   // NOTE: This means after calling spl_cntr_set_pending_out_media_fmt, we MUST deliver
   // an output buffer before writing any data to this output port (there is no longer
   // a check in topo_process to stop that from happening).
   cmn_port_ptr->flags.media_fmt_event = FALSE;

   return result;
}

/**
 * Currently only used during handle_prepare.
 */
ar_result_t spl_cntr_ext_out_port_apply_pending_media_fmt_cmd_path(void *base_ptr, gu_ext_out_port_t *ext_out_port_ptr)
{
   bool_t is_data_path = FALSE;
   return spl_cntr_ext_out_port_apply_pending_media_fmt((spl_cntr_t *)base_ptr,
                                                        (spl_cntr_ext_out_port_t *)ext_out_port_ptr,
                                                        is_data_path);
}

/**
 * Push a timestamp to the local input buffer. The new timestamp corresponds to the the sample
 * at the end of the buffer.
 */
ar_result_t spl_cntr_ext_in_port_push_timestamp_to_local_buf(spl_cntr_t *            me_ptr,
                                                             spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   DBG_INIT_EXCEPTION_HANDLING
   ar_result_t                result             = AR_EOK;
   spl_topo_timestamp_info_t *timestamp_info_ptr = NULL;
   spf_msg_header_t *         header_ptr    = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
   spf_msg_data_buffer_t *    input_buf_ptr = NULL;

   // We shouldn't be pushing timestamps to the local buffer if we haven't created it yet.
   DBG_VERIFY(result, header_ptr);
   DBG_VERIFY(result, spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr));

   input_buf_ptr = (spf_msg_data_buffer_t *)&header_ptr->payload_start;

   // If buf_timestamp_info is not set, insert timestamp in that spot. Otherwise overwrite the newest_timestamp_info.
   // Also, if the local buffer is empty, overwrite the buf timestamp with the incoming timestamp - empty buf means
   // current buf_timestamp was determined using extrapolation - actual incoming timestamp is more accurate.
   bool_t overwrite_buf_timestamp = !(ext_in_port_ptr->topo_buf.buf_timestamp_info.is_valid) ||
                                    (0 == ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len);

   timestamp_info_ptr = overwrite_buf_timestamp ? &(ext_in_port_ptr->topo_buf.buf_timestamp_info)
                                                : &(ext_in_port_ptr->topo_buf.newest_timestamp_info);

   /**
    * If the TS validity changes, then instead of throwing an exception, attempt to handle it as follows
    *  If there is data already present in the buffer, then honor the validity flag associated with it.
    *  Append the incoming timestamp to the newest_timestamp_info till the old data can be drained.
    *
    *  When does this happen? When a module like SPR is upstream of SC and underruns before valid TS
    *  data is provided by client.
    */
   if(overwrite_buf_timestamp && (ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len))
   {
      timestamp_info_ptr = &(ext_in_port_ptr->topo_buf.newest_timestamp_info);

      timestamp_info_ptr->offset_bytes_per_ch = ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len;
      timestamp_info_ptr->timestamp           = input_buf_ptr->timestamp;
      timestamp_info_ptr->is_valid            = TRUE;
   }
   else
   {
   	  // The timestamp corresponds to the sample at the end of the buffer.
      timestamp_info_ptr->offset_bytes_per_ch = ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len;
      timestamp_info_ptr->timestamp           = input_buf_ptr->timestamp;
      timestamp_info_ptr->is_valid            = TRUE;
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Pushed timestamp to ext in port idx = %ld, miid = 0x%lx local buffer, ts = %lu, offset_bytes_per_ch = "
                "%ld, overwrite_buf_timestamp %ld",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                (uint32_t)timestamp_info_ptr->timestamp,
                timestamp_info_ptr->offset_bytes_per_ch,
                overwrite_buf_timestamp);
#endif

   DBG_CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

ar_result_t spl_cntr_ext_in_port_re_push_data_msg_ts_to_local_buf(spl_cntr_t *            me_ptr,
                                                                  spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   DBG_INIT_EXCEPTION_HANDLING
   ar_result_t                result             = AR_EOK;
   spl_topo_timestamp_info_t *timestamp_info_ptr = &(ext_in_port_ptr->topo_buf.buf_timestamp_info);
   uint64_t *                 FRAC_TIME_PTR_NULL = NULL;

   spf_msg_header_t *         header_ptr    = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
   spf_msg_data_buffer_t *    input_buf_ptr = NULL;

   // We shouldn't be pushing timestamps to the local buffer if we haven't created it yet.
   DBG_VERIFY(result, header_ptr);
   DBG_VERIFY(result, spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr));

   // We only expect to call this function after dropping data.
   DBG_VERIFY(result, (0 == ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len));

   input_buf_ptr = (spf_msg_data_buffer_t *)&header_ptr->payload_start;

   // Forward extrapolate the timestamp according to the amount of data that was already consumed.
   timestamp_info_ptr->timestamp = input_buf_ptr->timestamp +
       (topo_bytes_per_ch_to_us(ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch,
                                &(ext_in_port_ptr->cu.media_fmt),
                                FRAC_TIME_PTR_NULL));

   timestamp_info_ptr->offset_bytes_per_ch = ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len;
   timestamp_info_ptr->is_valid            = TRUE;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Re-pushed timestamp to ext in port idx = %ld, miid = 0x%lx local buffer, ts = %lu, offset_bytes_per_ch = "
                "%ld",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                (uint32_t)timestamp_info_ptr->timestamp,
                timestamp_info_ptr->offset_bytes_per_ch);
#endif

   DBG_CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}


/**
 * Adjust the timestamps according to data which was removed from the buffer. The old buffer head timestamp
 * is removed and replaced by a timestamp which is extrapolaed based on the newest timestamp.
 */
ar_result_t spl_cntr_ext_in_port_adjust_timestamps(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   DBG_INIT_EXCEPTION_HANDLING
   ar_result_t result             = AR_EOK;
   uint64_t *  FRAC_TIME_PTR_NULL = NULL;

   DBG_VERIFY(result,
              ext_in_port_ptr && spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr) &&
                 (ext_in_port_ptr->topo_buf.buf_timestamp_info.is_valid ||
                  ext_in_port_ptr->topo_buf.newest_timestamp_info.is_valid));

   // Only buf_timestamp exists. Keep it and extrapolate the timestamp forwards.
   if ((!ext_in_port_ptr->topo_buf.newest_timestamp_info.is_valid))
   {
      // Forward extrapolate the buf timestamp - move it up by how much data was removed from the local buffer.
      ext_in_port_ptr->topo_buf.buf_timestamp_info.timestamp =
         ext_in_port_ptr->topo_buf.buf_timestamp_info.timestamp +
         (topo_bytes_per_ch_to_us(ext_in_port_ptr->topo_buf.bytes_consumed_per_ch,
                                  &(ext_in_port_ptr->cu.media_fmt),
                                  FRAC_TIME_PTR_NULL));

      // Offset doesn't need to be adjusted since it's always zero.
   }
   // Both buf_timestamp and newest_timestamp exists. Newest timestamp moves to the head of the buffer with updated
   // timestamp value based on backwards timestamp extrapolation.
   else
   {
      uint32_t newest_offset_bytes_per_ch = ext_in_port_ptr->topo_buf.newest_timestamp_info.offset_bytes_per_ch;

      // Two cases needed since bytes math is done in unsigned domain.
      // Newest timestamp still has positive offset after removing data. Move it to the head by backwards extrapolation.
      if (newest_offset_bytes_per_ch >= ext_in_port_ptr->topo_buf.bytes_consumed_per_ch)
      {
         uint32_t adjusted_offset_bytes_per_ch =
            newest_offset_bytes_per_ch - ext_in_port_ptr->topo_buf.bytes_consumed_per_ch;

         // This is to handle the case where topo buf info TS is invalid and newest_ts_info is valid
         // Return right away to avoid any timestamp movement till invalid TS data is sent out.
         if(!ext_in_port_ptr->topo_buf.buf_timestamp_info.is_valid)
         {
            ext_in_port_ptr->topo_buf.newest_timestamp_info.offset_bytes_per_ch = adjusted_offset_bytes_per_ch;
            return result;
         }

         // Moving the timestamp backwards so it is positioned at the head of the buffer.
         ext_in_port_ptr->topo_buf.buf_timestamp_info.timestamp =
            ext_in_port_ptr->topo_buf.newest_timestamp_info.timestamp -
            (topo_bytes_per_ch_to_us(adjusted_offset_bytes_per_ch,
                                     &(ext_in_port_ptr->cu.media_fmt),
                                     FRAC_TIME_PTR_NULL));
      }
      // Newest timestamp has a negative offset after removing data. Move it to the head by forwards extrapolation.
      else
      {
         uint32_t adjusted_offset_bytes_per_ch =
            ext_in_port_ptr->topo_buf.bytes_consumed_per_ch - newest_offset_bytes_per_ch;

         // Moving the timestamp forwards so it is positioned at the head of the buffer.
         ext_in_port_ptr->topo_buf.buf_timestamp_info.timestamp =
            ext_in_port_ptr->topo_buf.newest_timestamp_info.timestamp +
            (topo_bytes_per_ch_to_us(adjusted_offset_bytes_per_ch,
                                     &(ext_in_port_ptr->cu.media_fmt),
                                     FRAC_TIME_PTR_NULL));
      }

      // Offset doesn't need to be adjusted since it's always zero.

      // Since newest_timestamp moved into buf_timestamp position, clear newest_timestamp slot.
      ext_in_port_ptr->topo_buf.newest_timestamp_info.is_valid            = FALSE;
      ext_in_port_ptr->topo_buf.newest_timestamp_info.timestamp           = 0;
      ext_in_port_ptr->topo_buf.newest_timestamp_info.offset_bytes_per_ch = 0;
   }

   DBG_CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Estimates a timestamp for the data at the start of the local buffer by extrapolating
 * from the most recent past timestamp. Assigns that estimate into the local buffer's timestamp
 * field which will later be sent to the topo layer.
 */
ar_result_t spl_cntr_ext_in_port_assign_timestamp(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;

   // If there are no timestamps, we must send an invalid timestamp through the topo.
   if (!ext_in_port_ptr->topo_buf.buf_timestamp_info.is_valid)
   {
      ext_in_port_ptr->topo_buf.timestamp          = 0;
      ext_in_port_ptr->topo_buf.timestamp_is_valid = FALSE;
      return result;
   }

   // We can always use the buf_timestamp_info timestamp immediately since the offset is 0.
   ext_in_port_ptr->topo_buf.timestamp          = ext_in_port_ptr->topo_buf.buf_timestamp_info.timestamp;
   ext_in_port_ptr->topo_buf.timestamp_is_valid = TRUE;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Sending a valid timestamp to topo input port idx = %ld miid = 0x%lx, ts = %lu",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                (uint32_t)ext_in_port_ptr->topo_buf.timestamp);
#endif

   return result;
}

/**
 * Set the prev_actual_data_len field of the scratch data associated with the external input port.
 * We need to find the correct index of the scratch data by searching through the gu list of external
 * input ports for a match to the passed-in external input port pointer.
 */
ar_result_t spl_cntr_set_ext_in_port_prev_actual_data_len(gen_topo_t *      topo_ptr,
                                                          gu_ext_in_port_t *ext_in_port_ptr,
                                                          uint32_t          new_prev_actual_data_len)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result        = AR_EOK;
   uint32_t    topo_offset   = offsetof(spl_cntr_t, topo);
   spl_cntr_t *me_ptr        = (spl_cntr_t *)(((uint8_t *)topo_ptr) - topo_offset);
   uint32_t    in_port_index = 0;

   VERIFY(result, ext_in_port_ptr);

   // Searching for the proper index.
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr), in_port_index++)
   {
      gu_ext_in_port_t *cur_ext_in_port_ptr = (gu_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      if (cur_ext_in_port_ptr == ext_in_port_ptr)
      {
         me_ptr->topo.t_base.proc_context.ext_in_port_scratch_ptr[in_port_index].prev_actual_data_len =
            new_prev_actual_data_len;
         return result;
      }
   }

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_ERROR_PRIO, "Not able to find external input port.");
   result = AR_EFAILED;

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Topo to cntr call back to handle propagation at external output port.
 * If the propagated property is is_upstrm_rt, cmd is sent to downstream cntr.
 */
ar_result_t spl_cntr_set_propagated_prop_on_ext_output(gen_topo_t *              topo_ptr,
                                                       gu_ext_out_port_t *       gu_out_port_ptr,
                                                       topo_port_property_type_t prop_type,
                                                       void *                    payload_ptr)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, topo_ptr);

   spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)gu_out_port_ptr;

   if (PORT_PROPERTY_IS_UPSTREAM_RT == prop_type)
   {
      uint32_t *is_rt_ptr = (uint32_t *)payload_ptr;
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "Propagating to peer: from ext output port_id=0x%lx forward prop upstream-real-time, prop_value %u, "
                   "prev "
                   "value %u",
                   gu_out_port_ptr->int_out_port_ptr->cmn.id,
                   *is_rt_ptr,
                   ext_out_port_ptr->cu.icb_info.flags.is_real_time);

      // If realtime flag changed, that can effect number of required output buffers. So we may need to recreate
      // external output buffers.
      if (*is_rt_ptr != ext_out_port_ptr->cu.icb_info.flags.is_real_time)
      {
         ext_out_port_ptr->cu.icb_info.flags.is_real_time = *is_rt_ptr;

         if (FALSE == gu_is_ipc_ext_output_port(gu_out_port_ptr))
         {
            spl_cntr_recreate_ext_out_buffers((void *)&me_ptr->cu, gu_out_port_ptr);

            // downstream message is sent at the end
            // cu_inform_downstream_about_upstream_property
         }
      }
   }

   return result;
}

/**
 * Handle property propagation from upstream container.
 * Propagates the property update through the topology.
 */
ar_result_t spl_cntr_set_propagated_prop_on_ext_input(gen_topo_t *              topo_ptr,
                                                      gu_ext_in_port_t *        gu_in_port_ptr,
                                                      topo_port_property_type_t prop_type,
                                                      void *                    payload_ptr)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, topo_ptr);

   spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)gu_in_port_ptr;

   if (PORT_PROPERTY_IS_DOWNSTREAM_RT == prop_type)
   {
      uint32_t *is_rt_ptr = (uint32_t *)payload_ptr;

      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "Propagating to peer: from ext input port_id=0x%lx backward prop downstream-real-time, "
                   "prop_value=%u, "
                   "prev "
                   "value %u",
                   gu_in_port_ptr->int_in_port_ptr->cmn.id,
                   *is_rt_ptr,
                   ext_in_port_ptr->cu.icb_info.flags.is_real_time);

      if (*is_rt_ptr != ext_in_port_ptr->cu.icb_info.flags.is_real_time)
      {
         ext_in_port_ptr->cu.icb_info.flags.is_real_time = *is_rt_ptr;

         // upon receiving this message upstream will recreate ext buf

         // upstream message is sent later at the end
         // cu_inform_upstream_about_downstream_property
      }
   }

   return result;
}

/**
 * Clears timestamp discontinuity from the external input port. Also all past timestamps are now invalidated, so
 * remove them from the linked list.
 */
ar_result_t spl_cntr_ext_in_port_clear_timestamp_discontinuity(gen_topo_t *      topo_ptr,
                                                               gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   ar_result_t             result          = AR_EOK;
   spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)gu_ext_in_port_ptr;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   if (ext_in_port_ptr->topo_buf.timestamp_discontinuity)
   {
      SPL_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_HIGH_PRIO,
                   "Clearing timestmap discontinuity from external input port miid 0x%lx idx %ld",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);
   }
#endif

   ext_in_port_ptr->topo_buf.timestamp_discontinuity = FALSE;
   ext_in_port_ptr->topo_buf.disc_timestamp          = 0;
   ext_in_port_ptr->topo_buf.ts_disc_pos_bytes       = 0;

   ext_in_port_ptr->vptx_next_expected_ts    = 0;
   ext_in_port_ptr->vptx_ts_zeros_to_push_us = 0;
   ext_in_port_ptr->vptx_ts_valid            = FALSE;

   ext_in_port_ptr->topo_buf.buf_timestamp_info.timestamp           = 0;
   ext_in_port_ptr->topo_buf.buf_timestamp_info.offset_bytes_per_ch = 0;
   ext_in_port_ptr->topo_buf.buf_timestamp_info.is_valid            = FALSE;

   ext_in_port_ptr->topo_buf.newest_timestamp_info.timestamp           = 0;
   ext_in_port_ptr->topo_buf.newest_timestamp_info.offset_bytes_per_ch = 0;
   ext_in_port_ptr->topo_buf.newest_timestamp_info.is_valid            = FALSE;

   return result;
}

/**
 * This function is only used by spl_cntr_check_insert_missing_eos_on_next_module() to check if downstream
 * needs an EOS.
 */
static topo_data_flow_state_t spl_cntr_get_output_port_data_flow_state(spl_cntr_t *            me_ptr,
                                                                       spl_topo_output_port_t *out_port_ptr)
{
   // 1. If there is any dfg/flushing eos at the output port or connected input port, it's in flow gap state.
   if (gen_topo_md_list_has_flushing_eos_or_dfg(out_port_ptr->md_list_ptr) ||
       (out_port_ptr->t_base.gu.conn_in_port_ptr &&
        spl_topo_input_port_has_dfg_or_flushing_eos((gen_topo_input_port_t *)out_port_ptr->t_base.gu.conn_in_port_ptr)))
   {
      return TOPO_DATA_FLOW_STATE_AT_GAP;
   }

   // 2. If there is any data at the output port, it's in data_flowing.
   if (spl_topo_op_port_contains_unconsumed_data(&(me_ptr->topo), out_port_ptr))
   {
      return TOPO_DATA_FLOW_STATE_FLOWING;
   }

   // 3. Otherwise, use the data flow state of the connected input port or external output port.

   // External output port
   if (out_port_ptr->t_base.gu.ext_out_port_ptr)
   {
#if 0
      spl_topo_ext_buf_t *ext_buf_ptr =
         (spl_topo_ext_buf_t *)me_ptr->topo.t_base.topo_to_cntr_vtable_ptr->ext_out_port_get_ext_buf(
            out_port_ptr->t_base.gu.ext_out_port_ptr);

      return ext_buf_ptr->data_flow_state;
#else
      // Currently, data flow state is not implemented on external output ports. For purposes of pushing eos at close,
      // returning
      // flowing means that eos will always be pushed at upstream close.
      return TOPO_DATA_FLOW_STATE_FLOWING;
#endif
   }
   // Internal output port
   else if (out_port_ptr->t_base.gu.conn_in_port_ptr)
   {
      spl_topo_input_port_t *conn_in_port_ptr = (spl_topo_input_port_t *)out_port_ptr->t_base.gu.conn_in_port_ptr;
      return conn_in_port_ptr->t_base.common.data_flow_state;
   }

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "Error calculating output port idx %ld miid 0x%lx data flow state, returning data flow gap.",
                out_port_ptr->t_base.gu.cmn.index,
                out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);

   return TOPO_DATA_FLOW_STATE_AT_GAP;
}

/**
 * If internal eos is still at an input port of a module while that module is closed, we need to ensure that
 * eos gets propagated to the next module, otherwise data flow gap is never communicated downstream.
 */
ar_result_t spl_cntr_check_insert_missing_eos_on_next_module(gen_topo_t *           gc_topo_ptr,
                                                             gen_topo_input_port_t *gc_in_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t            result      = AR_EOK;
   spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)gc_in_port_ptr;
   spl_cntr_t *           me_ptr      = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, (spl_topo_t *)gc_topo_ptr);

   spl_topo_module_t *module_ptr = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;
#if 0
   // if module owns the md propagation then it should raise eos after port op close.
   /* but if all the input ports are closed for the first module,
    * process will not be called and module won't be able to put eos on output buffer. */
   if (!gen_topo_fwk_owns_md_prop(&module_ptr->t_base))
   {
      return result;
   }
#endif


   // Only move eof/dfg if all of a module's input ports are either closing or already in at-gap state.
   // If there are some port remained in data-flow state then module is responsible for pushing out eos
   // on the next process call based on close port_operation handling.

   // todo: a mimo module which has one of the output port as source will break with EOS insertion.

   //don't need to manually insert eos here if module itself is closing.
   bool_t is_module_closing = FALSE;
   cu_is_module_closing(&(me_ptr->cu), &(module_ptr->t_base.gu), &is_module_closing);
   if (is_module_closing)
   {
      return result;
   }

   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      if (TOPO_DATA_FLOW_STATE_FLOWING == in_port_ptr->common.data_flow_state)
      {
         bool_t is_port_closing = FALSE;
         cu_is_in_port_closing(&(me_ptr->cu), &(in_port_ptr->gu), &is_port_closing);
         if (!is_port_closing)
         { //if port is not closing then can't insert eos. module should handle from process call.
            return result;
         }
      }
   }


   if (0 < module_ptr->t_base.gu.num_output_ports)
   {
      // reset the module to clear flushing eos or internal metadata.
      gen_topo_reset_module(&me_ptr->topo.t_base, &module_ptr->t_base);

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->t_base.gu.output_port_list_ptr; out_port_list_ptr;
           LIST_ADVANCE(out_port_list_ptr))
      {
         spl_topo_output_port_t *out_port_ptr    = (spl_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
         topo_data_flow_state_t  output_port_dfs = spl_cntr_get_output_port_data_flow_state(me_ptr, out_port_ptr);
         if (TOPO_DATA_FLOW_STATE_AT_GAP != output_port_dfs)
         {
            uint32_t INPUT_PORT_ID_DONT_CARE = 0; // The input port will be closed, no need for ref.

            module_cmn_md_eos_flags_t flag = {.word = 0 };
            flag.is_flushing_eos = TRUE;
            flag.is_internal_eos = TRUE;

            uint32_t bytes_across_ch = spl_topo_get_out_port_data_len(&(me_ptr->topo), out_port_ptr, FALSE);
            TRY(result,
                gen_topo_create_eos_for_cntr(&me_ptr->topo.t_base,
                                             NULL, // for output ports we don't need container ref.
                                             INPUT_PORT_ID_DONT_CARE,
                                             me_ptr->cu.heap_id,
                                             &out_port_ptr->md_list_ptr,
                                             NULL, /* md_flag_ptr */
                                             NULL, /*tracking_payload_ptr*/
                                             &flag /* is_flushing */,
                                             bytes_across_ch,
                                             out_port_ptr->t_base.common.media_fmt_ptr));

            out_port_ptr->t_base.common.sdata.flags.end_of_frame = TRUE;
            out_port_ptr->t_base.common.sdata.flags.marker_eos   = TRUE;

            spl_topo_op_modify_md_when_new_data_arrives(&me_ptr->topo,
                                                        out_port_ptr,
                                                        0, /*new_data_amount*/
                                                        TRUE /* new_flushing_eos_arrived*/);

            me_ptr->topo.simpt_event_flags.check_eof = TRUE;

#ifdef SPL_SIPT_DBG
            TOPO_MSG(me_ptr->topo.t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "Output port idx = %ld, miid = 0x%lx eof set, check_eof becomes TRUE.",
                     out_port_ptr->t_base.gu.cmn.index,
                     out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_MED_PRIO,
                         "MD_DBG: EOS moved to output port idx %ld miid 0x%lx as part of close input, result %ld",
                         out_port_ptr->t_base.gu.cmn.index,
                         out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                         result);

            // If this output port is connected, set eof on the internal port to force propagation during process
            // This is to handle the corner case of input port being closed before eos could be propagated.
            if (out_port_ptr->t_base.gu.conn_in_port_ptr)
            {
               spl_topo_input_port_t *next_in_port_ptr =
                  (spl_topo_input_port_t *)out_port_ptr->t_base.gu.conn_in_port_ptr;

               next_in_port_ptr->t_base.common.sdata.flags.end_of_frame = TRUE;
               SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                            DBG_MED_PRIO,
                            "MD_DBG: EOF set on next input port idx %ld miid 0x%lx as part of close input ",
                            next_in_port_ptr->t_base.gu.cmn.index,
                            next_in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
            }
         }
      }
   }
   // If sink module, its ok to leave eos on input to be dropped. If non-sink without connected outputs, its ok to leave
   // eos on input to be dropped.
   else
   {
      return result;
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

ar_result_t spl_cntr_update_icb_info(gen_topo_t *topo_ptr)
{
   ar_result_t result      = AR_EOK;
   uint32_t    topo_offset = offsetof(spl_cntr_t, topo);
   spl_cntr_t *me_ptr      = (spl_cntr_t *)(((uint8_t *)topo_ptr) - topo_offset);

   bool_t is_variable_output = (spl_topo_fwk_ext_is_fixed_in_dm_module_present(me_ptr->topo.fwk_extn_info.dm_info));
   bool_t is_variable_input  = (spl_topo_fwk_ext_is_fixed_out_dm_module_present(me_ptr->topo.fwk_extn_info.dm_info));

   /* go over input ports and set variable size flag in icb info */
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      if (is_variable_input != ext_in_port_ptr->cu.icb_info.flags.variable_input)
      {
         ext_in_port_ptr->cu.icb_info.flags.variable_input = is_variable_input;

         // mark this flag inorder to inform upstream of any change in varaiable input for the ext port.
         ext_in_port_ptr->cu.prop_info.did_inform_us_of_frame_len_and_var_ip = FALSE;
      }
   }

   /* go over output ports and set variable size flag in icb info */
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
        ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      ext_out_port_ptr->cu.icb_info.flags.variable_output = is_variable_output;
   }

   return result;
}
