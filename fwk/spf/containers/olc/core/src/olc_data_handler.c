/**
 * \file olc_data_handler.c
 * \brief
 *     This file contains olc functions for data handling.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "olc_driver.h"
#include "olc_i.h"
#include "apm.h"
#include "media_fmt_extn_api.h"
#include "gen_topo.h"
#include "irm_cntr_prof_util.h"
static ar_result_t olc_process_for_peer_cntr_delivery(olc_t *me_ptr, olc_ext_out_port_t *ext_in_port_ptr);
/* =======================================================================
Static Function Definitions
========================================================================== */
/* is_flush flag should be used for flushing case.
 * For EOS: for flushing case EOS needs to be dropped
 * This flag can be used for other scenarios where special handling is required when flushing is done
 */

/**
 * discontinuity  matters if we concatenate old and new data at the output buf.

 */
void olc_set_input_discontinuity_flag(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr)
{
   ext_in_port_ptr->flags.input_discontinuity = TRUE;
}

ar_result_t olc_free_input_md_data(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t           result     = AR_EOK;
   gen_topo_module_t *   module_ptr = NULL;
   module_cmn_md_list_t *node_ptr   = NULL;
   module_cmn_md_list_t *next_ptr   = NULL;
   module_cmn_md_t *     md_ptr     = NULL;

   if (ext_in_port_ptr->md_list_ptr)
   {
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_LOW_PRIO,
              "MD_DBG: Destroy all metadata for module 0x%lX with port_index %lu",
              ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
              ext_in_port_ptr->gu.int_in_port_ptr->cmn.index);
   }

   module_ptr = ((gen_topo_module_t *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr);
   node_ptr   = ext_in_port_ptr->md_list_ptr;

   while (node_ptr)
   {
      next_ptr = node_ptr->next_ptr;
      md_ptr   = node_ptr->obj_ptr;
      if (MODULE_CMN_MD_ID_EOS != md_ptr->metadata_id)
      {
         gen_topo_capi_metadata_destroy((void *)module_ptr, node_ptr, FALSE, &ext_in_port_ptr->md_list_ptr, 0, FALSE);
      }
      node_ptr = next_ptr;
   }
   return result;
}

ar_result_t olc_free_input_data_cmd(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr, uint32_t status, bool_t is_flush)
{

   olc_free_input_md_data(me_ptr, ext_in_port_ptr);

   if (!ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      return AR_EOK;
   }

   ar_result_t result = AR_EOK;
   {
      if (SPF_MSG_DATA_BUFFER == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
      {
         spf_msg_header_t *     header      = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
         spf_msg_data_buffer_t *buf_ptr     = (spf_msg_data_buffer_t *)&header->payload_start;
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

         // drop any metadata that came with the buffer
         gen_topo_destroy_all_metadata(me_ptr->topo.gu.log_id,
                                       (void *)in_port_ptr->gu.cmn.module_ptr,
                                       &buf_ptr->metadata_list_ptr,
                                       TRUE /* is dropped*/);
         buf_ptr->actual_size = 0;

         if (NULL != ext_in_port_ptr->buf.data_ptr) // for data commands
         {
            // reset input buffer params
            memset(&ext_in_port_ptr->buf, 0, sizeof(ext_in_port_ptr->buf));
         }
      }

      spf_msg_ack_msg(&ext_in_port_ptr->cu.input_data_q_msg, status);
   }

   // set payload to NULL to indicate we are not holding on to any input data msg
   ext_in_port_ptr->cu.input_data_q_msg.payload_ptr = NULL;

   return result;
}

static ar_result_t olc_input_data_set_up_peer_cntr(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t            result      = AR_EOK;
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   spf_msg_header_t *     header        = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
   spf_msg_data_buffer_t *input_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

   // if input buffer do not contain integer PCM samples per channel, return it immediately with error code
   if (SPF_IS_PCM_DATA_FORMAT(ext_in_port_ptr->cu.media_fmt.data_format))
   {
      uint32_t unit_size = ext_in_port_ptr->cu.media_fmt.pcm.num_channels *
                           TOPO_BITS_TO_BYTES(ext_in_port_ptr->cu.media_fmt.pcm.bits_per_sample);

      if (input_buf_ptr->actual_size % unit_size)
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_ERROR_PRIO,
                 "Returning an input buffer that do not "
                 "contain the same PCM samples on all channel!");
         return olc_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EBADPARAM, FALSE);
      }
   }

   // input_buf_ptr->offset                        = 0; // Assumption: valid data always starts from zero.
   ext_in_port_ptr->buf.data_ptr                = (uint8_t *)&input_buf_ptr->data_buf;
   ext_in_port_ptr->buf.actual_data_len         = input_buf_ptr->actual_size;
   ext_in_port_ptr->buf.max_data_len            = input_buf_ptr->actual_size; // deinterleaving is wrt this size.
   ext_in_port_ptr->buf.mem_map_handle          = 0; // mem map handle is not used for inter-container buffers.
   ext_in_port_ptr->flags.eof                   = FALSE;
   in_port_ptr->common.sdata.flags.end_of_frame = FALSE;
   in_port_ptr->common.sdata.flags.marker_eos   = FALSE;

   olc_process_eos_md_from_peer_cntr(me_ptr, ext_in_port_ptr, &input_buf_ptr->metadata_list_ptr);

   spf_list_merge_lists((spf_list_node_t **)&ext_in_port_ptr->md_list_ptr,
                        (spf_list_node_t **)&input_buf_ptr->metadata_list_ptr);

   olc_copy_timestamp_from_input(me_ptr, ext_in_port_ptr);

   olc_handle_ext_in_data_flow_begin(me_ptr, ext_in_port_ptr);

   return result;
}

ar_result_t olc_get_input_data_cmd(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result;
   if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_ERROR_PRIO,
              "Already holding on to an input data command. "
              "Cannot get another command until this is freed");
      return AR_EUNEXPECTED;
   }

   result = posal_queue_pop_front(ext_in_port_ptr->gu.this_handle.q_ptr,
                                  (posal_queue_element_t *)&(ext_in_port_ptr->cu.input_data_q_msg));
   if (AR_EOK != result)
   {
      ext_in_port_ptr->cu.input_data_q_msg.payload_ptr = NULL;
      return result;
   }

   //#ifdef VERBOSE_DEBUGGING
   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_LOW_PRIO,
           "Popped an input msg buffer 0x%lx with opcode 0x%x "
           "from (miid,port-id) (0x%lX, 0x%lx) queue, ",
           ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
           ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
           ext_in_port_ptr->cu.input_data_q_msg.payload_ptr,
           ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);
   //#endif

   return result;
}

/**
 * returns whether the input buffer being held is an data buffer (not EOS, Media fmt etc)
 */
static bool_t olc_is_input_a_data_buffer(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr)
{
   bool_t is_data_cmd = FALSE;
   // if no buffer held, then return FALSE
   if (!ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      return FALSE;
   }

   if (SPF_MSG_DATA_BUFFER == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      is_data_cmd = TRUE;
   }

   return is_data_cmd;
}

ar_result_t olc_flush_input_data_queue(olc_t *            me_ptr,
                                       olc_ext_in_port_t *ext_in_port_ptr,
                                       bool_t             keep_data_msg,
                                       bool_t             is_flush,
                                       bool_t             is_post_processing)
{
   ar_result_t result             = AR_EOK;
   void *      pushed_payload_ptr = NULL;

   if (NULL == ext_in_port_ptr->gu.this_handle.q_ptr)
   {
      return AR_EOK;
   }

   sgm_flush_write_data_port(&me_ptr->spgm_info,
                             ext_in_port_ptr->wdp_ctrl_cfg_ptr->sdm_port_index,
                             is_flush,
                             is_post_processing);

   do
   {
      // if data message, then push back to queue. Also stop popping when we see the first message we pushed
      if (keep_data_msg && !(olc_is_input_a_data_buffer(me_ptr, ext_in_port_ptr)))
      {
         if (NULL == pushed_payload_ptr)
         {
            pushed_payload_ptr = ext_in_port_ptr->cu.input_data_q_msg.payload_ptr;
         }

         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_LOW_PRIO,
                 "Pushing data msg 0x%p back to queue during flush",
                 ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);

         // note that upstream won't be pushing at this time because upstream is stopped when we flush. hence there's
         // worry of messages going out-of-order.
         if (AR_DID_FAIL(result =
                            posal_queue_push_back(ext_in_port_ptr->gu.this_handle.q_ptr,
                                                  (posal_queue_element_t *)&(ext_in_port_ptr->cu.input_data_q_msg))))
         {
            OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Pushing MF back to queue failed");
            return result;
         }
         ext_in_port_ptr->cu.input_data_q_msg.payload_ptr = NULL;
      }
      else
      {
         // first free up any data q msgs that we are already holding
         olc_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, TRUE);
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
      olc_get_input_data_cmd(me_ptr, ext_in_port_ptr);

   } while (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);

   return AR_EOK;
}

/**
 * called for all use cases. both for internal and external clients.
 */
ar_result_t olc_data_cmd_handle_packetized_or_pcm_inp_media_fmt(void *            ctx_ptr,
                                                                gu_ext_in_port_t *gu_ext_in_port_ptr,
                                                                topo_media_fmt_t *media_fmt_ptr,
                                                                bool_t            is_data_path)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t        result                = AR_EOK;
   olc_t *            me_ptr                = (olc_t *)ctx_ptr;
   olc_ext_in_port_t *ext_in_port_ptr       = (olc_ext_in_port_t *)gu_ext_in_port_ptr;
   bool_t             FORCE_AGGREGATE_FALSE = FALSE;
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

   if (SPF_IS_PACKETIZED_OR_PCM(media_fmt_ptr->data_format))
   {
      TOPO_PRINT_PCM_MEDIA_FMT(me_ptr->topo.gu.log_id, media_fmt_ptr, "container input");
   }
   else
   {
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_HIGH_PRIO,
              "Different media format.. SPF_data_format: %d, fmt_id: %d",
              media_fmt_ptr->data_format,
              media_fmt_ptr->fmt_id);
   }

   // Update total_bytes, such that duration remains constant.
   if (tu_has_media_format_changed(&ext_in_port_ptr->cu.media_fmt, media_fmt_ptr))
   {
      topo_port_state_t in_port_state;
      // Copy incoming media format as the actual media format of the external input port.
      tu_copy_media_fmt(&ext_in_port_ptr->cu.media_fmt, media_fmt_ptr);

      // ext in port receives packed deinterleaved, but preprocess in gen_cntr will convert to unpacked when it sends to
      // first module.
      if (TOPO_DEINTERLEAVED_PACKED == media_fmt_ptr->pcm.interleaving)
      {
         media_fmt_ptr->pcm.interleaving = TOPO_DEINTERLEAVED_PACKED;
      }

      // Copy external input port media fmt to internal.

      TRY(result, gen_topo_set_input_port_media_format(&me_ptr->topo, in_port_ptr, media_fmt_ptr));

      // Propagate media fmt if already prepared.
      in_port_state = in_port_ptr->common.state;

      /**
       * Control path:
       * Propagate control path media fmt only if the port is in prepare state.
       *    If port is in start state, then data path takes care of propagation.
       *    If port is in stop state, handle_prepare cmd will take care.
       * Data path:
       * Propagate data path media fmt only if the port started.
       */
      if ((!is_data_path && (TOPO_PORT_STATE_PREPARED == in_port_state)) ||
          (is_data_path && (TOPO_PORT_STATE_STARTED == in_port_state)))
      {
         // no reconciliation needed for OLC
         GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
         gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;

         GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr,
                                                             &me_ptr->topo,
                                                             FALSE /*do_reconcile*/);

         TRY(result, me_ptr->cu.topo_vtbl_ptr->propagate_media_fmt(&me_ptr->topo, is_data_path));
         TRY(result, olc_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE));
         // kpps bw voting is handled inside threshold change
         TRY(result, me_ptr->cu.cntr_vtbl_ptr->port_data_thresh_change(&me_ptr->cu));

         capi_event_flag_ptr->word = 0;
      }
   }

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/**
 * This can be called from both data and control paths.
 */
ar_result_t olc_data_ctrl_cmd_handle_inp_media_fmt_from_upstream_cntr(olc_t *            me_ptr,
                                                                      olc_ext_in_port_t *ext_in_port_ptr,
                                                                      spf_msg_header_t * msg_header,
                                                                      bool_t             is_data_path)
{
   ar_result_t result = AR_EOK;

   // get media format update cmd payload
   // get the mediaFormat structure
   spf_msg_media_format_t *media_fmt = (spf_msg_media_format_t *)&msg_header->payload_start;
   // OLC can get both PCM and raw_compressed as input data
   spdm_handle_input_media_format_update(&me_ptr->spgm_info,
                                         (void *)media_fmt,
                                         ext_in_port_ptr->wdp_ctrl_cfg_ptr->sdm_port_index,
                                         TRUE);
   return result;
}

/**
 * EoS arrived if EoS is at the ext input port or it has propagated inside and got stuck
 * at the nblc end (buffering module or decoder).
 */
static inline bool_t olc_has_eos_arrived(olc_ext_in_port_t *ext_in_port_ptr)
{
   return (ext_in_port_ptr->flags.flushing_eos);
}

/**
 * process_info_ptr == NULL => before process began
 */
ar_result_t olc_process_pending_input_data_cmd(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;

   if (NULL == ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      return AR_EUNEXPECTED;
   }

   // process messages
   switch (ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      case SPF_MSG_DATA_BUFFER:
         // this happens due to timestamp discontinuity.
         break;
      case SPF_MSG_DATA_MEDIA_FORMAT:
      {
         OLC_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, " processing input media format from peer cntr ");

         spf_msg_header_t *msg_header = (spf_msg_header_t *)ext_in_port_ptr->cu.input_data_q_msg.payload_ptr;

         result = olc_data_ctrl_cmd_handle_inp_media_fmt_from_upstream_cntr(me_ptr,
                                                                            ext_in_port_ptr,
                                                                            msg_header,
                                                                            TRUE /* is_data_path */);

         olc_free_input_data_cmd(me_ptr, ext_in_port_ptr, (uint32_t)result, FALSE);

         break;
      }
      default:
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_MED_PRIO,
                 " unknown opcode 0x%lx",
                 ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);
         break;
      }
   }

   return result;
}

static ar_result_t olc_check_process_input_media_fmt_data_cmd(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;
   // process any partially processed data
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   if (in_port_ptr->flags.processing_began) // todo : VB : check back
   {
      olc_set_input_discontinuity_flag(me_ptr, ext_in_port_ptr);
   }

   // process frames takes care of processing input data cmd (media fmt etc) at the end.
   if (!ext_in_port_ptr->flags.input_discontinuity)
   {
      result = olc_process_pending_input_data_cmd(me_ptr, ext_in_port_ptr);
      // need to make sure once input data cmd is released, appropriate mask is set
   }

   return result;
}

/* Poll for the input buffers only if,
 *  1. Internal input buffer is not filled and data buffers are present on the queue.
 *  2. There is no data discontinuity due to media format change etc.
 *  3. EOS is not received.
 */
static bool_t olc_need_to_poll_for_input_data(olc_ext_in_port_t *ext_in_port_ptr)
{
   if ((0 == ext_in_port_ptr->buf.actual_data_len) && (FALSE == ext_in_port_ptr->flags.input_discontinuity) &&
       (FALSE == ext_in_port_ptr->input_has_md) && (!olc_has_eos_arrived(ext_in_port_ptr)))
   {
      return TRUE;
   }

   return FALSE;
}

ar_result_t olc_input_dataQ_trigger_peer_cntr(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t num_polled_buffers = 0;
   // while (1)
   {
#ifdef VERBOSE_DEBUGGING
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_LOW_PRIO,
              "Popping new message from the buffer. num_polled_buffers=%d  ",
              num_polled_buffers);
#endif

      // Take next msg off the q
      TRY(result, olc_get_input_data_cmd(me_ptr, ext_in_port_ptr));

      // process messages
      switch (ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
      {
         case SPF_MSG_DATA_BUFFER:
         {
            TRY(result, olc_input_data_set_up_peer_cntr(me_ptr, ext_in_port_ptr));

            break;
         }
         case SPF_MSG_DATA_MEDIA_FORMAT:
         {
            OLC_MSG(me_ptr->topo.gu.log_id,
                    DBG_MED_PRIO,
                    "Input media format update cmd from peer "
                    "service to module, port index  (0x%lX, %lu) ",
                    ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                    ext_in_port_ptr->gu.int_in_port_ptr->cmn.index);

            olc_check_process_input_media_fmt_data_cmd(me_ptr, ext_in_port_ptr);

            break;
         }
         default:
         {
            OLC_MSG(me_ptr->topo.gu.log_id,
                    DBG_ERROR_PRIO,
                    "received unsupported message 0x%lx",
                    ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);
            result = AR_EUNSUPPORTED;
            result |= olc_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EUNSUPPORTED, FALSE);
            break;
         }
      }

      // Stop popping buffers if,
      //   1. Need to poll returns FALSE or
      if (!olc_need_to_poll_for_input_data(ext_in_port_ptr))
      {
         return (result);
      }
      num_polled_buffers++;
   }

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }
   return (result);
}

ar_result_t olc_write_done_handler(cu_base_t *base_ptr, uint32_t channel_bit_index)
{
   ar_result_t       result = AR_EOK;
   topo_port_state_t port_state;
   INIT_EXCEPTION_HANDLING
   olc_t *            me_ptr = (olc_t *)base_ptr;
   olc_ext_in_port_t *ext_in_port_ptr =
      (olc_ext_in_port_t *)cu_get_ext_in_port_for_bit_index(&me_ptr->cu, channel_bit_index);

   if (NULL == ext_in_port_ptr)
   {
      return AR_EUNEXPECTED;
   }

   TRY(result, olc_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE));

   port_state = ((gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr)->common.state;
   if (TOPO_PORT_STATE_STARTED == port_state)
   {
      spdm_write_dl_pcd(&me_ptr->spgm_info, ext_in_port_ptr->wdp_ctrl_cfg_ptr->sdm_port_index);
   }

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

ar_result_t olc_input_dataQ_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *me_ptr           = (olc_t *)base_ptr;
   bool_t is_data_consumed = 0;
#ifdef VERBOSE_DEBUGGING
   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_MED_PRIO,
           "olc_input_dataQ_trigger: channel_bit_index: 0x%x ",
           channel_bit_index);
#endif
   olc_ext_in_port_t *ext_in_port_ptr =
      (olc_ext_in_port_t *)cu_get_ext_in_port_for_bit_index(&me_ptr->cu, channel_bit_index);

   VERIFY(result, NULL != ext_in_port_ptr);

   TRY(result, olc_input_dataQ_trigger_peer_cntr(me_ptr, ext_in_port_ptr));

   if ((0 < ext_in_port_ptr->buf.max_data_len) || (ext_in_port_ptr->input_has_md))
   {
      ext_in_port_ptr->sdm_wdp_input_data.data_buf     = ext_in_port_ptr->buf;
      ext_in_port_ptr->sdm_wdp_input_data.offset       = 0;
      ext_in_port_ptr->sdm_wdp_input_data.buf_ts       = &ext_in_port_ptr->inbuf_ts;
      ext_in_port_ptr->sdm_wdp_input_data.md_list_pptr = &ext_in_port_ptr->md_list_ptr;

      // clang-format off
      IRM_PROFILE_MOD_PROCESS_SECTION(((gen_topo_module_t *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr)->prof_info_ptr, me_ptr->topo.gu.prof_mutex,
      spdm_process_data_write(&me_ptr->spgm_info,
                              ext_in_port_ptr->wdp_ctrl_cfg_ptr->sdm_port_index,
                              &ext_in_port_ptr->sdm_wdp_input_data,
                              &is_data_consumed);
      );
      // clang-format on

      if ((FALSE == ext_in_port_ptr->input_has_md) && (FALSE == is_data_consumed))
      {
         spdm_write_dl_pcd(&me_ptr->spgm_info, ext_in_port_ptr->wdp_ctrl_cfg_ptr->sdm_port_index);
         result = olc_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
         return AR_EFAILED;
      }
      else
      {
         spdm_write_dl_pcd(&me_ptr->spgm_info, ext_in_port_ptr->wdp_ctrl_cfg_ptr->sdm_port_index);
         // result = olc_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
      }
   }

   if (TRUE == ext_in_port_ptr->set_eos_to_sat_wr_ep_pending)
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
      topo_basic_reset_input_port(&me_ptr->topo, in_port_ptr, TRUE);
      ext_in_port_ptr->set_eos_to_sat_wr_ep_pending = FALSE;
   }

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

static inline uint32_t olc_get_all_output_port_mask(olc_t *me_ptr)
{
   uint32_t mask = 0;

   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      olc_ext_out_port_t *ext_out_port_ptr = (olc_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

      mask |= ext_out_port_ptr->cu.bit_mask;
   }

   return mask;
}

ar_result_t olc_create_ext_out_bufs(olc_t *             me_ptr,
                                    olc_ext_out_port_t *ext_port_ptr,
                                    uint32_t            num_out_bufs,
                                    uint32_t            new_buf_size)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   if (!gen_topo_is_ext_peer_container_connected((gen_topo_output_port_t *)ext_port_ptr->gu.int_out_port_ptr))
   {
      OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, " no external connections yet");
      return AR_EFAILED;
   }

   if (num_out_bufs > CU_MAX_OUT_BUF_Q_ELEMENTS)
   {
      OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, " not enough bufQ elements");
      return AR_ENORESOURCE;
   }

   if (new_buf_size <= 1)
   {
      // ext_port_ptr->cu.buf_max_size = 2048;
      return AR_EOK;
   }

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_LOW_PRIO,
           " Creating %lu external buffers of size %lu",
           (num_out_bufs - ext_port_ptr->cu.num_buf_allocated),
           new_buf_size);

   TRY(result,
       spf_svc_create_and_push_buffers_to_buf_queue(ext_port_ptr->gu.this_handle.q_ptr,
                                                    new_buf_size,
                                                    num_out_bufs,
                                                    ext_port_ptr->gu.downstream_handle.spf_handle_ptr,
                                                    (POSAL_HEAP_ID)POSAL_HEAP_DEFAULT,
                                                    &ext_port_ptr->cu.num_buf_allocated));

   ext_port_ptr->buf.data_ptr        = NULL; // will be populated when buf is popped from queue
   ext_port_ptr->buf.actual_data_len = 0;
   ext_port_ptr->buf.max_data_len    = new_buf_size;

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/* if a buffer is recreated, then
 * ext_port_ptr->out_data_buf_node.pBuffer is NULL
 * and the caller must go back to work loop.
 */
ar_result_t olc_check_realloc_ext_buffer(olc_t *me_ptr, olc_ext_out_port_t *ext_port_ptr)
{
   ar_result_t       result       = AR_EOK;
   spf_msg_header_t *buffer_ptr   = (spf_msg_header_t *)ext_port_ptr->cu.out_bufmgr_node.buf_ptr;
   bool_t            buf_relloc   = FALSE;
   uint32_t          new_buf_size = 0;

   if (NULL == buffer_ptr)
   {
      return result;
   }

   spf_msg_data_buffer_t *data_buf_ptr = (spf_msg_data_buffer_t *)&buffer_ptr->payload_start;

   // if buf size or count doesn't match then recreate.
   uint32_t num_bufs_needed =
      ext_port_ptr->cu.icb_info.icb.num_reg_bufs + ext_port_ptr->cu.icb_info.icb.num_reg_prebufs;
   uint32_t num_bufs_to_destroy = num_bufs_needed >= ext_port_ptr->cu.num_buf_allocated
                                     ? 0
                                     : (ext_port_ptr->cu.num_buf_allocated - num_bufs_needed);

   uint32_t num_bufs_to_create =
      num_bufs_needed > ext_port_ptr->cu.num_buf_allocated ? num_bufs_needed - ext_port_ptr->cu.num_buf_allocated : 0;

   new_buf_size = data_buf_ptr->max_size;
   buf_relloc   = ((data_buf_ptr->max_size < ext_port_ptr->cu.buf_max_size)) ? 1 : 0;
   if (buf_relloc)
   {
      new_buf_size = ext_port_ptr->cu.buf_max_size;
   }

   if (ext_port_ptr->required_buf_size > new_buf_size)
   {
      buf_relloc = buf_relloc && (!(data_buf_ptr->max_size == ext_port_ptr->required_buf_size));

      buf_relloc = buf_relloc || ((data_buf_ptr->max_size != ext_port_ptr->required_buf_size));
      if (buf_relloc)
      {
         new_buf_size = ext_port_ptr->required_buf_size;
      }
   }

   if ((buf_relloc) || (num_bufs_to_destroy > 0))
   {
      // Free the buffer
      posal_memory_free(buffer_ptr);
      ext_port_ptr->cu.num_buf_allocated--;
      ext_port_ptr->cu.out_bufmgr_node.buf_ptr = NULL;
      OLC_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, " Destroyed 1 external buffers 0x%p", buffer_ptr);
   }

   if ((buf_relloc) || (num_bufs_to_create > 0))
   {
      // one buf was destroyed, so create one more.
      result = olc_create_ext_out_bufs(me_ptr, ext_port_ptr, ext_port_ptr->cu.num_buf_allocated + 1, new_buf_size);
      if (AR_DID_FAIL(result))
      {
         OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, " Buffer recreate failed %d", result);
      }
   }
   return result;
}

/**
 * ext_port_ptr->out_data_buf_node.buf_ptr may be null when this func returns.
 */
ar_result_t olc_process_pop_out_buf(olc_t *me_ptr, olc_ext_out_port_t *ext_port_ptr)
{
   ar_result_t result = AR_EOK;

   // if a buf is already popped, return
   if ((NULL != ext_port_ptr->cu.out_bufmgr_node.buf_ptr))
   {
      return AR_EOK;
   }

   // poll until we get a buf that doesn't get recreated
   while ((posal_channel_poll(posal_queue_get_channel(ext_port_ptr->gu.this_handle.q_ptr),
                              posal_queue_get_channel_bit(ext_port_ptr->gu.this_handle.q_ptr))))
   {
      // Take next buffer off the q
      if (AR_DID_FAIL(result = posal_queue_pop_front(ext_port_ptr->gu.this_handle.q_ptr,
                                                     (posal_queue_element_t *)&(ext_port_ptr->cu.out_bufmgr_node))))
      {
         OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Failure on Out Buf Pop ");
         return result;
      }

#ifdef VERBOSE_DEBUGGING
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_LOW_PRIO,
              "popped out buffer  0x%p. Current bitmask 0x%x",
              ext_port_ptr->cu.out_bufmgr_node.buf_ptr,
              me_ptr->cu.curr_chan_mask);
#endif

      result = olc_check_realloc_ext_buffer(me_ptr, ext_port_ptr);
      if (AR_DID_FAIL(result))
      {
         OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Relloacate failed ");
         return result;
      }

      if (NULL != ext_port_ptr->cu.out_bufmgr_node.buf_ptr)
      {
         break;
      }
      // if buf got recreated, go back to loop.
   }

   return result;
}

void olc_return_back_out_buf(olc_t *me_ptr, olc_ext_out_port_t *ext_port_ptr)
{
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_port_ptr->gu.int_out_port_ptr;

   if (GEN_TOPO_BUF_ORIGIN_EXT_BUF == out_port_ptr->common.flags.buf_origin)
   {
      if (out_port_ptr->common.bufs_ptr[0].actual_data_len != 0)
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_HIGH_PRIO,
                 "Module 0x%lX: Warning: returning buffer 0x%p when it has valid data %lu",
                 out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                 out_port_ptr->common.bufs_ptr[0].data_ptr,
                 out_port_ptr->common.bufs_ptr[0].actual_data_len);
      }
      out_port_ptr->common.bufs_ptr[0].data_ptr        = NULL;
      out_port_ptr->common.bufs_ptr[0].actual_data_len = 0;
      out_port_ptr->common.bufs_ptr[0].max_data_len    = 0;
      out_port_ptr->common.flags.buf_origin            = GEN_TOPO_BUF_ORIGIN_INVALID;
   }

   spf_msg_header_t *header = (spf_msg_header_t *)(ext_port_ptr->cu.out_bufmgr_node.buf_ptr);
   if (NULL != header)
   {
      if (ext_port_ptr->cu.out_bufmgr_node.return_q_ptr)
      {
         spf_msg_t msg = {.payload_ptr = ext_port_ptr->cu.out_bufmgr_node.buf_ptr, .msg_opcode = 0 };
         spf_msg_return_msg(&msg);
      }
      ext_port_ptr->cu.out_bufmgr_node.buf_ptr = NULL;
   }
}

ar_result_t olc_init_after_popping_peer_cntr_out_buf(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   // either the input trigger caused us to reach here, or buf recreate happened.
   if (NULL != ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
   {
      spf_msg_header_t *     header      = (spf_msg_header_t *)(ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr);
      spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

      ext_out_port_ptr->buf.data_ptr        = (uint8_t *)(&(out_buf_ptr->data_buf));
      ext_out_port_ptr->buf.actual_data_len = 0;
      ext_out_port_ptr->buf.max_data_len    = out_buf_ptr->max_size;

      ext_out_port_ptr->num_frames_in_buf = 0;

      out_buf_ptr->actual_size = 0;
   }

   return result;
}

/**
 * sets up output buffers and calls process on topo.
 *
 * any of the output port can trigger this.
 * circ buff will provide buffers in case one of the output didn't have buffers
 * overtime all output ports need to consume at the same rate.
 */
static ar_result_t olc_output_buf_set_up_peer_cntr(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   // Get the channel status of all output data Q
   uint32_t all_ext_out_mask = olc_get_all_output_port_mask(me_ptr);

   uint32_t channel_status = posal_channel_poll(me_ptr->cu.channel_ptr, all_ext_out_mask & me_ptr->cu.curr_chan_mask);

   // assign output buffers
   olc_ext_out_port_t *temp_ext_out_port_ptr = ext_out_port_ptr;

   // If Signal is set in output data q then pop from output data q
   if (temp_ext_out_port_ptr->cu.bit_mask & channel_status)
   {
      // Buffer should have been returned to either Q or Circular buf_list in previous iteration.
      if (NULL != temp_ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
      {
         OLC_MSG(me_ptr->topo.gu.log_id,
                 DBG_ERROR_PRIO,
                 "Buffer is not NULL, Unexpected Error. MIID = 0x%x, port_index= %d ",
                 temp_ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                 temp_ext_out_port_ptr->gu.int_out_port_ptr->cmn.index);
         THROW(result, AR_EUNEXPECTED);
      }
      TRY(result, olc_process_pop_out_buf(me_ptr, temp_ext_out_port_ptr));
   }

   TRY(result, olc_init_after_popping_peer_cntr_out_buf(me_ptr, temp_ext_out_port_ptr));

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      // return all the buffer to their respective Q if anything failed.
      olc_return_back_out_buf(me_ptr, temp_ext_out_port_ptr);
   }
   return result;
}

ar_result_t olc_send_media_fmt_update_to_downstream(olc_t *             me_ptr,
                                                    olc_ext_out_port_t *ext_out_port_ptr,
                                                    spf_msg_t *         msg,
                                                    posal_queue_t *     q_ptr)
{
   ar_result_t result = AR_EOK;

   result = cu_send_media_fmt_update_to_downstream(&me_ptr->cu, &ext_out_port_ptr->gu, msg, q_ptr);

   return result;
}

ar_result_t olc_populate_peer_cntr_out_buf(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;
   // can happen during overrun in GEN_CNTR
   if (NULL == ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
   {
      return result;
   }
   spf_msg_header_t *     header      = (spf_msg_header_t *)(ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr);
   spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

   header->rsp_result = 0;

   out_buf_ptr->flags = 0;
   //   out_buf_ptr->offset = 0;

   out_buf_ptr->actual_size       = ext_out_port_ptr->buf.actual_data_len;
   out_buf_ptr->max_size          = ext_out_port_ptr->buf.max_data_len;
   out_buf_ptr->metadata_list_ptr = NULL;
   gen_topo_timestamp_t ts        = ext_out_port_ptr->out_buf_ts;

   cu_set_bits(&out_buf_ptr->flags,
               ts.valid,
               DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK,
               DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);
   out_buf_ptr->timestamp = ts.value;

   bool_t out_buf_has_flushing_eos;
   gen_topo_populate_metadata_for_peer_cntr(&(me_ptr->topo),
                                            &(ext_out_port_ptr->gu),
                                            &(ext_out_port_ptr->md_list_ptr),
                                            &(out_buf_ptr->metadata_list_ptr),
                                            &out_buf_has_flushing_eos);

#ifdef VERBOSE_DEBUGGING
   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_LOW_PRIO,
           "Outgoing buffer timestamp: %lu (0x%lx%lx), flag=0x%lx, size=%lu",
           (uint32_t)out_buf_ptr->timestamp,
           (uint32_t)(out_buf_ptr->timestamp >> 32),
           (uint32_t)out_buf_ptr->timestamp,
           out_buf_ptr->flags,
           out_buf_ptr->actual_size);
#endif

   return result;
}

ar_result_t olc_send_data_to_downstream_peer_cntr(olc_t *             me_ptr,
                                                  olc_ext_out_port_t *ext_out_port_ptr,
                                                  spf_msg_header_t *  out_buf_header)
{
   ar_result_t result = AR_EOK;

   // overrun in OLC_CNTR
   if (!out_buf_header)
   {
      return result;
   }
#ifdef VERBOSE_DEBUGGING

   spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&out_buf_header->payload_start;

   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_LOW_PRIO,
           "olc_send_pcm_to_downstream_peer_cntr: timestamp: %lu (0x%lx%lx). size %lu",
           (uint32_t)out_buf_ptr->timestamp,
           (uint32_t)(out_buf_ptr->timestamp >> 32),
           (uint32_t)out_buf_ptr->timestamp,
           (uint32_t)(out_buf_ptr->actual_size));
#endif

   posal_bufmgr_node_t out_buf_node = ext_out_port_ptr->cu.out_bufmgr_node;
   spf_msg_t *         data_msg     = spf_msg_convt_buf_node_to_msg(&out_buf_node,
                                                       SPF_MSG_DATA_BUFFER,
                                                       NULL,
                                                       NULL,
                                                       0,
                                                       &ext_out_port_ptr->gu.this_handle);

   if (ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr)
   {
      result = posal_queue_push_back(ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr->q_ptr,
                                     (posal_queue_element_t *)data_msg);
      if (AR_DID_FAIL(result))
      {
         OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Failed to deliver buffer dowstream. Dropping");
         olc_return_back_out_buf(me_ptr, ext_out_port_ptr);
      }

#ifdef VERBOSE_DEBUGGING
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_LOW_PRIO,
              "pushed buffer downstream 0x%p. Current bit mask 0x%x",
              ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr,
              me_ptr->cu.curr_chan_mask);
#endif
   }
   else
   {
      OLC_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "downstream not connected. dropping buffer");
      olc_return_back_out_buf(me_ptr, ext_out_port_ptr);
   }

   ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr = NULL;

   return result;
}

ar_result_t olc_send_peer_cntr_out_buffers(olc_t *             me_ptr,
                                           olc_ext_out_port_t *ext_out_port_ptr,
                                           spf_msg_t *         media_fmt_msg_ptr,
                                           bool_t              is_data_path)
{
   ar_result_t result = AR_EOK;

   if (media_fmt_msg_ptr && media_fmt_msg_ptr->payload_ptr)
   {

      posal_queue_t *q_ptr = is_data_path
                                ? ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr->q_ptr
                                : ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr->cmd_handle_ptr->cmd_q_ptr;

      result |= olc_send_media_fmt_update_to_downstream(me_ptr, ext_out_port_ptr, media_fmt_msg_ptr, q_ptr);
   }

   if (ext_out_port_ptr->md_list_ptr || (ext_out_port_ptr->buf.actual_data_len && ext_out_port_ptr->buf.data_ptr))
   {
      spf_msg_header_t *     header      = (spf_msg_header_t *)(ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr);
      spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

      olc_populate_peer_cntr_out_buf(me_ptr, ext_out_port_ptr);

      // pack the buffer
      topo_media_fmt_t *med_fmt_ptr = &ext_out_port_ptr->cu.media_fmt;
      topo_media_fmt_t *module_med_fmt_ptr =
         ((gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr)->common.media_fmt_ptr;

      // if module output unpacked, container needs to convert to packed.
      if (SPF_IS_PCM_DATA_FORMAT(module_med_fmt_ptr->data_format) &&
          (TOPO_DEINTERLEAVED_UNPACKED == module_med_fmt_ptr->pcm.interleaving))
      {
         // if actual=max len, then already packed.
         if (ext_out_port_ptr->buf.actual_data_len < ext_out_port_ptr->buf.max_data_len)
         {
            uint32_t num_ch           = med_fmt_ptr->pcm.num_channels;
            uint32_t bytes_per_ch     = ext_out_port_ptr->buf.actual_data_len / num_ch;
            uint32_t max_bytes_per_ch = ext_out_port_ptr->buf.max_data_len / num_ch;

            // ch=0 is at the right place already
            for (uint32_t ch = 1; ch < num_ch; ch++)
            {
               memsmove(ext_out_port_ptr->buf.data_ptr + ch * bytes_per_ch,
                        bytes_per_ch,
                        ext_out_port_ptr->buf.data_ptr + ch * max_bytes_per_ch,
                        bytes_per_ch);
            }
         }
         cu_handle_prebuffer(&me_ptr->cu, &ext_out_port_ptr->gu, out_buf_ptr, ext_out_port_ptr->cu.buf_max_size);
      }

      // ext_out_port_ptr->cu.total_bytes += out_buf_ptr->actual_size;

      result = olc_send_data_to_downstream_peer_cntr(me_ptr, ext_out_port_ptr, header);
   }
   else
   {
      olc_return_back_out_buf(me_ptr, ext_out_port_ptr);
   }

   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   if (GEN_TOPO_BUF_ORIGIN_EXT_BUF == out_port_ptr->common.flags.buf_origin)
   {
      out_port_ptr->common.bufs_ptr[0].data_ptr     = NULL;
      out_port_ptr->common.bufs_ptr[0].max_data_len = 0;
      out_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_INVALID;
      gen_topo_set_all_bufs_len_to_zero(&out_port_ptr->common);
   }
   ext_out_port_ptr->buf.data_ptr        = NULL;
   ext_out_port_ptr->buf.actual_data_len = 0;
   ext_out_port_ptr->buf.max_data_len    = 0;

   return result;
}
/**
 * Also returns buf back to queue if contains no samples.
 */
static ar_result_t olc_process_for_peer_cntr_delivery(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   bool_t   out_media_fmt_changed = FALSE;
   bool_t   FORCE_AGGREGATE_FALSE = FALSE;
   uint32_t media_fmt_opcode      = SPF_MSG_DATA_MEDIA_FORMAT;
   bool_t   is_data_path          = FALSE;
   // for each external out port, post-process data, send media fmt and data down.

   olc_ext_out_port_t *temp_ext_out_port_ptr = ext_out_port_ptr;
   topo_media_fmt_t *  out_actual_media_fmt  = &temp_ext_out_port_ptr->cu.media_fmt;
   {
      spf_msg_t media_fmt_msg = { 0 };
      if (ext_out_port_ptr->sat_graph_mf_changed)
      {
         typedef struct pcm_med_fmt_t
         {
            media_format_t          main;
            payload_media_fmt_pcm_t pcm_extn;
         } pcm_med_fmt_t;

         pcm_med_fmt_t *media_ptr                     = (pcm_med_fmt_t *)ext_out_port_ptr->sat_graph_mf_payload_ptr;
         temp_ext_out_port_ptr->out_media_fmt_changed = TRUE;

         // AKR: data format is in public form: Need to revert it to GK. It was working for PCM because both formats
         // have same val

         // VERIFY(result, 0 != media_ptr->main.payload_size);

         spf_data_format_t df = gen_topo_convert_public_data_fmt_to_spf_data_format(media_ptr->main.data_format);

         if (SPF_IS_PCM_DATA_FORMAT(df))
         {
            VERIFY(result, MEDIA_FMT_ID_PCM_EXTN == media_ptr->main.fmt_id);
            out_actual_media_fmt->fmt_id = MEDIA_FMT_ID_PCM; // Update the FMT ID from extn format to regular format
            out_actual_media_fmt->pcm.bit_width       = media_ptr->pcm_extn.bit_width;
            out_actual_media_fmt->pcm.bits_per_sample = media_ptr->pcm_extn.bits_per_sample;
            out_actual_media_fmt->pcm.endianness =
               gen_topo_convert_public_endianness_to_gen_topo_endianness(media_ptr->pcm_extn.endianness);
            out_actual_media_fmt->pcm.num_channels = media_ptr->pcm_extn.num_channels;
            out_actual_media_fmt->pcm.q_factor     = media_ptr->pcm_extn.q_factor;
            out_actual_media_fmt->pcm.sample_rate  = media_ptr->pcm_extn.sample_rate;
            out_actual_media_fmt->pcm.interleaving =
               gen_topo_convert_public_interleaving_to_gen_topo_interleaving(media_ptr->pcm_extn.interleaved);

            uint8_t *channel_mapping = (uint8_t *)(&(media_ptr->pcm_extn) + 1);
            memscpy(out_actual_media_fmt->pcm.chan_map,
                    out_actual_media_fmt->pcm.num_channels * sizeof(uint8_t),
                    channel_mapping,
                    media_ptr->pcm_extn.num_channels * sizeof(uint8_t));
         }
         else if (SPF_RAW_COMPRESSED == df)
         {
            uint8_t *payload = (uint8_t *)(&(media_ptr->main) + 1);
            tu_capi_create_raw_compr_med_fmt(me_ptr->topo.gu.log_id,
                                             (uint8_t *)&(payload),
                                             media_ptr->main.payload_size,
                                             media_ptr->main.fmt_id,
                                             out_actual_media_fmt,
                                             TRUE /*with header*/,
                                             me_ptr->cu.heap_id);
            out_actual_media_fmt->fmt_id = media_ptr->main.fmt_id;
         }
         else
         {
            OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Unsupported data format %lu", df);
            THROW(result, AR_EUNSUPPORTED);
         }
         out_actual_media_fmt->data_format = df;

         if (TRUE == temp_ext_out_port_ptr->out_media_fmt_changed)
         {
            gen_topo_output_port_t *out_port_ptr;
            out_port_ptr                     = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
            topo_port_state_t out_port_state = out_port_ptr->common.state;
            if (TOPO_PORT_STATE_STARTED == out_port_state)
            {
               is_data_path = TRUE;
            }
            media_fmt_opcode = is_data_path ? SPF_MSG_DATA_MEDIA_FORMAT : SPF_MSG_CMD_MEDIA_FORMAT;

            TRY(result,
                cu_create_media_fmt_msg_for_downstream(&me_ptr->cu,
                                                       &temp_ext_out_port_ptr->gu,
                                                       &media_fmt_msg,
                                                       media_fmt_opcode));
         }
      }

      result = olc_send_peer_cntr_out_buffers(me_ptr, temp_ext_out_port_ptr, &media_fmt_msg, is_data_path);
      if (AR_EOK == result)
      {
         ext_out_port_ptr->sat_graph_mf_changed       = FALSE;
         out_media_fmt_changed                        = temp_ext_out_port_ptr->out_media_fmt_changed;
         temp_ext_out_port_ptr->out_media_fmt_changed = FALSE;
      }
   }

   if (TRUE == out_media_fmt_changed)
   {
      // Container KPPS, BW depend on ext port media fmt
      TRY(result, olc_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE));
      TRY(result, olc_handle_clk_vote_change(me_ptr, CU_PM_REQ_KPPS_BW, FORCE_AGGREGATE_FALSE, NULL, NULL));
   }

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

ar_result_t olc_cu_determine_ext_out_buffering(cu_base_t *        base_ptr,
                                               gu_ext_out_port_t *gu_ext_out_port_ptr,
                                               uint32_t           us_frame_len)
{

   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   cu_ext_out_port_t *ext_out_port_ptr =
      (cu_ext_out_port_t *)((uint8_t *)gu_ext_out_port_ptr + base_ptr->ext_out_port_cu_offset);

   // if there's no peer-svc downstream (dsp client ok) return as zero buffers.
   if (NULL == gu_ext_out_port_ptr->downstream_handle.spf_handle_ptr)
   {
      return AR_EFAILED;
   }
   // Assume defaults
   memset(&ext_out_port_ptr->icb_info.icb, 0, sizeof(ext_out_port_ptr->icb_info.icb));
   ext_out_port_ptr->icb_info.icb.num_reg_bufs = 2;

   if (SPF_RAW_COMPRESSED == ext_out_port_ptr->media_fmt.data_format)
   {
      // AKR: hardcoded num icb to 2 for RAW compressed cases
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "ICB: Raw Compressed DF at ext out port (0x%lX, %lu). Num ICB = %lu Reg bufs",
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.index,
             ext_out_port_ptr->icb_info.icb.num_reg_bufs);
      return AR_EOK;
   }

   // ext_out_port_ptr->icb_info.flags.
   if ((0 == ext_out_port_ptr->icb_info.ds_frame_len.frame_len_samples) &&
       (0 == ext_out_port_ptr->icb_info.ds_frame_len.sample_rate) &&
       (0 == ext_out_port_ptr->icb_info.ds_frame_len.frame_len_us) && (0 == ext_out_port_ptr->icb_info.ds_period_us))
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "downstream didn't inform about frame length yet for ext out port (0x%lX, %lu).",
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.index);
      return AR_EOK;
   }

   if ((0 == us_frame_len) || (0 == ext_out_port_ptr->media_fmt.pcm.sample_rate))
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "self frame length yet not known yet for ext out port (0x%lX, %lu).",
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.index);
      return AR_EOK;
   }

   gu_sg_t *sg_ptr = gu_ext_out_port_ptr->sg_ptr;

   icb_upstream_info_t us = { { 0 } };
   // us.len                   = base_ptr->cntr_frame_len;
   us.len.frame_len_samples = us_frame_len;
   us.len.sample_rate       = ext_out_port_ptr->media_fmt.pcm.sample_rate;
   us.len.frame_len_us      = 0;
   us.log_id                = base_ptr->gu_ptr->log_id;

   us.flags       = ext_out_port_ptr->icb_info.flags;
   us.sid         = sg_ptr->sid;
   us.period_us   = base_ptr->period_us;
   us.disable_otp = ext_out_port_ptr->icb_info.disable_one_time_pre_buf;

   icb_downstream_info_t ds = { { 0 } };
   ds.len                   = ext_out_port_ptr->icb_info.ds_frame_len;
   ds.period_us             = ext_out_port_ptr->icb_info.ds_period_us;
   ds.flags                 = ext_out_port_ptr->icb_info.ds_flags;
   ds.sid                   = ext_out_port_ptr->icb_info.ds_sid;

   TRY(result, icb_determine_buffering(&us, &ds, &ext_out_port_ptr->icb_info.icb));

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

static inline ar_result_t olc_get_required_out_buf_size_and_count(olc_t *             me_ptr,
                                                                  olc_ext_out_port_t *ext_out_port_ptr,
                                                                  uint32_t *          num_out_buf,
                                                                  uint32_t *          req_out_buf_size,
                                                                  uint32_t            specified_out_buf_size,
                                                                  uint32_t            metadata_size)
{

   ar_result_t result     = AR_EOK;
   uint32_t    frame_size = 0;

   if (SPF_IS_PCM_DATA_FORMAT(ext_out_port_ptr->cu.media_fmt.data_format))
   {
      frame_size = *req_out_buf_size;
      frame_size /= (ext_out_port_ptr->cu.media_fmt.pcm.bits_per_sample >> 3);
      frame_size /= (ext_out_port_ptr->cu.media_fmt.pcm.num_channels);
   }

   result = olc_cu_determine_ext_out_buffering(&me_ptr->cu, &ext_out_port_ptr->gu, frame_size);

   *num_out_buf = ext_out_port_ptr->cu.icb_info.icb.num_reg_bufs + ext_out_port_ptr->cu.icb_info.icb.num_reg_prebufs;

   return result;
}

ar_result_t olc_recreate_ext_out_buffers(void *ctx_ptr, gu_ext_out_port_t *gu_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   cu_base_t *         base_ptr         = (cu_base_t *)ctx_ptr;
   olc_t *             me_ptr           = (olc_t *)base_ptr;
   olc_ext_out_port_t *ext_out_port_ptr = (olc_ext_out_port_t *)gu_out_port_ptr;
   uint32_t            buf_size         = 1920; // default frame-size

   if (0 == ext_out_port_ptr->required_buf_size)
   {
      if (SPF_RAW_COMPRESSED == ext_out_port_ptr->cu.media_fmt.data_format)
      {
         buf_size = 2048;
      }
      else if (SPF_IS_PCM_DATA_FORMAT(ext_out_port_ptr->cu.media_fmt.data_format))
      {
         buf_size = (ext_out_port_ptr->cu.media_fmt.pcm.sample_rate / 1000);
         buf_size *= (ext_out_port_ptr->cu.media_fmt.pcm.bits_per_sample >> 3);
         buf_size *= (ext_out_port_ptr->cu.media_fmt.pcm.num_channels);
         buf_size *= me_ptr->configured_frame_size_us;
         buf_size = buf_size / 1000;

         if (0 == buf_size)
         {
            buf_size = 1920;
         }
      }
   }
   else
   {
      buf_size = ext_out_port_ptr->required_buf_size;
   }

   if (buf_size)
   {
      uint32_t metadata_size = 0;
      uint32_t num_out_bufs = 0, req_out_buf_size = buf_size;
      result = olc_get_required_out_buf_size_and_count(me_ptr,
                                                       ext_out_port_ptr,
                                                       &num_out_bufs,
                                                       &req_out_buf_size,
                                                       buf_size,
                                                       metadata_size);
      if (AR_DID_FAIL(result))
      {
         return AR_EOK;
      }

      if (req_out_buf_size > ext_out_port_ptr->cu.buf_max_size)
      {
         /** if buffer was already popped and is returned now, then we need to pop it back after
          * recreate. this has to be done for queue as well as circ buf. */
         bool_t buf_was_present = ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr != NULL;

         uint32_t num_bufs_to_keep = (req_out_buf_size != ext_out_port_ptr->cu.buf_max_size) ? 0 : num_out_bufs;
         olc_destroy_ext_buffers(me_ptr, ext_out_port_ptr, num_bufs_to_keep);

         ext_out_port_ptr->cu.buf_max_size = req_out_buf_size;

         result |= olc_create_ext_out_bufs(me_ptr, ext_out_port_ptr, num_out_bufs, req_out_buf_size);

         result = sgm_recreate_output_buffers(&me_ptr->spgm_info,
                                              ext_out_port_ptr->cu.buf_max_size,
                                              ext_out_port_ptr->rdp_ctrl_cfg_ptr->sdm_port_index);

         if (buf_was_present && (ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr == NULL))
         {
            olc_process_pop_out_buf(me_ptr, ext_out_port_ptr);

            if (ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
            {
               olc_init_after_popping_peer_cntr_out_buf(me_ptr, ext_out_port_ptr);
            }
         }
      }
      else if (req_out_buf_size < ext_out_port_ptr->cu.buf_max_size)
      {
         ext_out_port_ptr->cu.buf_max_size = req_out_buf_size;

         result = sgm_recreate_output_buffers(&me_ptr->spgm_info,
                                              ext_out_port_ptr->cu.buf_max_size,
                                              ext_out_port_ptr->rdp_ctrl_cfg_ptr->sdm_port_index);
      }
   }

   return result;
}

ar_result_t olc_media_fmt_event_handler(cu_base_t *base_ptr,
                                        uint32_t   channel_bit_index,
                                        uint8_t *  mf_payload_ptr,
                                        uint32_t   mf_payload_size,
                                        uint32_t   required_buffer_size,
                                        bool_t     is_data_path)
{
   ar_result_t result = AR_EOK;
   // INIT_EXCEPTION_HANDLING
   olc_t *           me_ptr               = (olc_t *)base_ptr;
   bool_t            media_format_changed = TRUE;
   topo_port_state_t port_state;
   // gen_topo_timestamp_t *out_buf_ts = (gen_topo_timestamp_t *)out_ts;

   olc_ext_out_port_t *ext_out_port_ptr =
      (olc_ext_out_port_t *)cu_get_ext_out_port_for_bit_index(&me_ptr->cu, channel_bit_index);

   if ((NULL == ext_out_port_ptr) || (NULL == mf_payload_ptr))
   {
      return AR_EUNEXPECTED;
   }

   ext_out_port_ptr->buf.actual_data_len      = 0;
   ext_out_port_ptr->sat_graph_mf_payload_ptr = mf_payload_ptr;
   ext_out_port_ptr->sat_graph_mf_changed     = TRUE;
   if (required_buffer_size)
   {
      ext_out_port_ptr->required_buf_size = required_buffer_size;
   }

   // todo: make the check more clean
   result = olc_process_for_peer_cntr_delivery(me_ptr, ext_out_port_ptr);

   if ((TRUE == media_format_changed) && (AR_EOK == result))
   {
      result = olc_recreate_ext_out_buffers(me_ptr, &ext_out_port_ptr->gu);
   }

   port_state = ((gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr)->common.state;
   if (TOPO_PORT_STATE_STARTED == port_state)
   {
      spdm_read_dl_pcd(&me_ptr->spgm_info, ext_out_port_ptr->rdp_ctrl_cfg_ptr->sdm_port_index);
   }

   return result;
}

ar_result_t olc_read_done_handler(cu_base_t *base_ptr,
                                  uint32_t   channel_bit_index,
                                  uint32_t   buf_size_to_deliver,
                                  uint32_t   required_data_buf_size)
{
   ar_result_t result = AR_EOK;
   // INIT_EXCEPTION_HANDLING
   olc_t *           me_ptr = (olc_t *)base_ptr;
   topo_port_state_t port_state;

   olc_ext_out_port_t *ext_out_port_ptr =
      (olc_ext_out_port_t *)cu_get_ext_out_port_for_bit_index(&me_ptr->cu, channel_bit_index);

   if ((NULL == ext_out_port_ptr))
   {
      return AR_EUNEXPECTED;
   }

   if (required_data_buf_size)
   {
      ext_out_port_ptr->required_buf_size = required_data_buf_size;
   }
   ext_out_port_ptr->sdm_rdp_input_data.data_buf.actual_data_len = buf_size_to_deliver;
   ext_out_port_ptr->buf.actual_data_len = ext_out_port_ptr->sdm_rdp_input_data.data_buf.actual_data_len;
   result                                = olc_process_for_peer_cntr_delivery(me_ptr, ext_out_port_ptr);

   ext_out_port_ptr->is_empty_buffer_available = FALSE;

   port_state = ((gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr)->common.state;

   if (TOPO_PORT_STATE_STARTED == port_state)
   {
      spdm_read_dl_pcd(&me_ptr->spgm_info, ext_out_port_ptr->rdp_ctrl_cfg_ptr->sdm_port_index);
   }
   return result;
}

ar_result_t olc_get_read_ext_output_buf(cu_base_t *base_ptr, uint32_t channel_bit_index, void *ext_out_data_ptr)
{
   ar_result_t         result           = AR_EOK;
   olc_t *             me_ptr           = (olc_t *)base_ptr;
   olc_ext_out_port_t *ext_out_port_ptr = NULL;

   ext_out_port_ptr = (olc_ext_out_port_t *)cu_get_ext_out_port_for_bit_index(&me_ptr->cu, channel_bit_index);

   if ((NULL == ext_out_port_ptr))
   {
      return AR_EUNEXPECTED;
   }

   if (TRUE == ext_out_port_ptr->is_empty_buffer_available)
   {
      sdm_cnt_ext_data_buf_t *ext_data_buf_ptr = (sdm_cnt_ext_data_buf_t *)ext_out_data_ptr;
      *ext_data_buf_ptr                        = ext_out_port_ptr->sdm_rdp_input_data;
   }

   return result;
}

ar_result_t olc_get_ext_out_media_fmt(cu_base_t *base_ptr, uint32_t channel_bit_index, void *mf_out_data_ptr)
{
   ar_result_t         result           = AR_EOK;
   olc_t *             me_ptr           = (olc_t *)base_ptr;
   olc_ext_out_port_t *ext_out_port_ptr = NULL;

   ext_out_port_ptr = (olc_ext_out_port_t *)cu_get_ext_out_port_for_bit_index(&me_ptr->cu, channel_bit_index);

   if ((NULL == ext_out_port_ptr))
   {
      return AR_EUNEXPECTED;
   }

   topo_media_fmt_t *media_fmt_ptr = (topo_media_fmt_t *)mf_out_data_ptr;

   *media_fmt_ptr = ext_out_port_ptr->cu.media_fmt;

   return result;
}

/**
 * use cases:
 */
static ar_result_t olc_output_bufQ_trigger_peer_cntr(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EFAILED;
   INIT_EXCEPTION_HANDLING

   if (ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr != NULL)
   {
      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_ERROR_PRIO,
              "Unexpected error occured. Out buf is "
              "already present while processing output data Q.");
      return AR_EUNEXPECTED;
   }

   TRY(result, olc_output_buf_set_up_peer_cntr(me_ptr, ext_out_port_ptr));

   // mark the flag to set an output buffer is available.
   // call to send the read IPC buffer to satellite.
   // update the wait mask to listen out ipc_read_queue.
   // if the send buffer fails
   //    --> gpr issue, then fatal.
   //    --> there is no buffer to send. not usual scenario. we can still wait on the IPC read queue.

   if (ext_out_port_ptr->buf.max_data_len > 0)
   {

      TRY(result,
          spdm_process_data_release_read_buffer(&me_ptr->spgm_info,
                                                ext_out_port_ptr->rdp_ctrl_cfg_ptr->sdm_port_index));
      TRY(result, spdm_read_dl_pcd(&me_ptr->spgm_info, ext_out_port_ptr->rdp_ctrl_cfg_ptr->sdm_port_index));

      ext_out_port_ptr->sdm_rdp_input_data.data_buf     = ext_out_port_ptr->buf;
      ext_out_port_ptr->sdm_rdp_input_data.offset       = 0;
      ext_out_port_ptr->sdm_rdp_input_data.buf_ts       = &ext_out_port_ptr->out_buf_ts;
      ext_out_port_ptr->sdm_rdp_input_data.md_list_pptr = &ext_out_port_ptr->md_list_ptr;

      ext_out_port_ptr->is_empty_buffer_available = TRUE;
   }

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      // return all the buffer to their respective Q if anything failed.
      memset(&ext_out_port_ptr->sdm_rdp_input_data, 0, sizeof(sdm_cnt_ext_data_buf_t));
      olc_return_back_out_buf(me_ptr, ext_out_port_ptr);
   }

   return result;
}

ar_result_t olc_output_bufQ_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   olc_t *             me_ptr           = (olc_t *)base_ptr;
   olc_ext_out_port_t *ext_out_port_ptr = NULL;

   ext_out_port_ptr = (olc_ext_out_port_t *)cu_get_ext_out_port_for_bit_index(&me_ptr->cu, channel_bit_index);

   VERIFY(result, NULL != ext_out_port_ptr);
#ifdef VERBOSE_DEBUGGING
   OLC_MSG(me_ptr->topo.gu.log_id,
           DBG_MED_PRIO,
           "olc_output_bufQ_trigger: channel_bit_index: 0x%x ",
           channel_bit_index);
#endif

   /* Setups buffers at the output ports and calls the process frames */
   TRY(result, olc_output_bufQ_trigger_peer_cntr(me_ptr, ext_out_port_ptr));

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}
