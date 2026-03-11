/**
 * \file gen_cntr_peer_cntr_input.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "apm.h"
#include "cu_events.h"
#include "media_fmt_extn_api.h"

ar_result_t gen_cntr_process_pending_data_cmd_peer_cntr(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;

   // process messages
   switch (ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      case SPF_MSG_DATA_BUFFER:
      case SPF_MSG_DATA_BUFFER_V2:
         // this happens due to timestamp discontinuity.
         break;
      case SPF_MSG_DATA_MEDIA_FORMAT:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_MED_PRIO,
                      " processing input media format from peer svc. input discont%u",
                      ext_in_port_ptr->flags.input_discontinuity);

         spf_msg_header_t *msg_header      = (spf_msg_header_t *)ext_in_port_ptr->cu.input_data_q_msg.payload_ptr;
         result                            = gen_cntr_data_ctrl_cmd_handle_inp_media_fmt_from_upstream_cntr(me_ptr,
                                                                                 ext_in_port_ptr,
                                                                                 msg_header,
                                                                                 TRUE /* is_data_path */);
         ext_in_port_ptr->flags.pending_mf = FALSE;
         gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, result, FALSE);

         break;
      }
      default:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_MED_PRIO,
                      " unknown opcode 0x%lx",
                      ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);
         break;
      }
   }

   return result;
}

/**
 * This can be called from both data and control paths.
 */
ar_result_t gen_cntr_data_ctrl_cmd_handle_inp_media_fmt_from_upstream_cntr(gen_cntr_t *            me_ptr,
                                                                           gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                           spf_msg_header_t *      msg_header,
                                                                           bool_t                  is_data_path)
{
   ar_result_t result = AR_EOK;

   // get media format update cmd payload
   // get the mediaFormat structure
   spf_msg_media_format_t *media_fmt = (spf_msg_media_format_t *)&msg_header->payload_start;

   cu_ext_in_port_upstream_frame_length_t local_upstream_frame_len;
   //Transfer upstream_max_frame_len_bytes to downstream container
   local_upstream_frame_len.frame_len_bytes   = media_fmt->upstream_frame_len.frame_len_bytes;
   local_upstream_frame_len.frame_len_samples = media_fmt->upstream_frame_len.frame_len_samples;
   local_upstream_frame_len.frame_len_us      = media_fmt->upstream_frame_len.frame_len_us;
   local_upstream_frame_len.sample_rate       = media_fmt->upstream_frame_len.sample_rate;

   topo_media_fmt_t local_media_fmt;
   result |= tu_convert_media_fmt_spf_msg_to_topo(me_ptr->topo.gu.log_id, media_fmt, &local_media_fmt, me_ptr->cu.heap_id);

   result |= gen_cntr_input_media_format_received(me_ptr,
                                                   (gu_ext_in_port_t *)ext_in_port_ptr,
                                                   &local_media_fmt,
												   &local_upstream_frame_len,
                                                   is_data_path);

   return result;
}

ar_result_t gen_cntr_check_if_any_ext_in_has_to_preserve_prebuffer(gen_topo_t *topo_ptr,
                                                                   bool_t     *has_to_preserve_prebuffer)
{

   *has_to_preserve_prebuffer = FALSE;
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = topo_ptr->gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      if (ext_in_port_ptr->cu.preserve_prebuffer)
      {
         *has_to_preserve_prebuffer = TRUE;
         return AR_EOK;
      }
   }

   return AR_EOK;
}