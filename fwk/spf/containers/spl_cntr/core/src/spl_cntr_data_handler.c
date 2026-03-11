/**
 * \file spl_cntr_data_handler.c
 * \brief
 *     This file contains spl_cntr functions for handling data path commands.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spl_cntr_i.h"

/**
 * Handling when data flow starts. Throw away any pending (stale) data in the local buffer and send sync module a will
 * start command.
 */
ar_result_t spl_cntr_ext_in_port_handle_data_flow_begin(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;

   // If not at gap, don't do anything.
   if (TOPO_DATA_FLOW_STATE_FLOWING == ext_in_port_ptr->topo_buf.data_flow_state)
   {
      return result;
   }

   spl_topo_update_check_data_flow_event_flag(&me_ptr->topo,
                                              &(ext_in_port_ptr->gu.int_in_port_ptr->cmn),
                                              TOPO_DATA_FLOW_STATE_FLOWING);


   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "Data flow started on external input (MIID,port_id)=(0x%lx,0x%lx), moving to data flow state flowing ",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);

   ext_in_port_ptr->topo_buf.data_flow_state       = TOPO_DATA_FLOW_STATE_FLOWING;
   ext_in_port_ptr->topo_buf.first_frame_after_gap = TRUE;

   //set flag so that prebuffers can be held in the prebuffer Q during first frame processing.
   ext_in_port_ptr->cu.preserve_prebuffer = TRUE;

   gen_topo_handle_fwk_ext_at_dfs(&me_ptr->topo.t_base, &ext_in_port_ptr->gu.int_in_port_ptr->cmn);

   return result;
}

//function to drop the data and metadata (except eos/dfg) from input buffer
void spl_cntr_drop_input_data(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   // If there is no held input data msg, there is nothing to free.
   if (!ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      return;
   }

   bool_t is_eos_dfg_found = FALSE;

   if (SPF_MSG_DATA_BUFFER == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      // For data buffers, mark the buffer as consumed before returning. Also
      // increment the buffer done counter.
      spf_msg_header_t *     header_ptr = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
      spf_msg_data_buffer_t *buf_ptr    = (spf_msg_data_buffer_t *)&header_ptr->payload_start;

      // drop the data
      buf_ptr->actual_size = 0;

      // invalidate timestamp
      cu_set_bits(&buf_ptr->flags,
                  FALSE,
                  DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK,
                  DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);

      // marking eos and dfg metadata offset to zero so that they are not dropped and propagated through the topo.
      module_cmn_md_list_t *md_list_ptr = buf_ptr->metadata_list_ptr;
      module_cmn_md_list_t *next_ptr;
      gu_module_t *         module_ptr = ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr;

      while (md_list_ptr)
      {
         next_ptr = md_list_ptr->next_ptr;

         if (MODULE_CMN_MD_ID_EOS != md_list_ptr->obj_ptr->metadata_id &&
             MODULE_CMN_MD_ID_DFG != md_list_ptr->obj_ptr->metadata_id)
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_HIGH_PRIO,
                         "Input port idx = %ld, miid = 0x%lx, metadata 0x%x dropped!",
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                         md_list_ptr->obj_ptr->metadata_id);
            gen_topo_capi_metadata_destroy((void *)module_ptr, md_list_ptr, TRUE, &buf_ptr->metadata_list_ptr, 0, FALSE);
         }
         else
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_HIGH_PRIO,
                         "Input port idx = %ld, miid = 0x%lx, metadata 0x%x setting offset to zero!",
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                         md_list_ptr->obj_ptr->metadata_id);
            md_list_ptr->obj_ptr->offset = 0;
            is_eos_dfg_found             = TRUE;
         }
         md_list_ptr = next_ptr;
      }
   }

   //if input port is already at gap or there was no eos/dfg metadata then release the buffer.
   if (!is_eos_dfg_found || TOPO_DATA_FLOW_STATE_FLOWING != ext_in_port_ptr->topo_buf.data_flow_state)
   {
      cu_free_input_data_cmd(&me_ptr->cu, &ext_in_port_ptr->gu, AR_EOK);
      return;
   }
}

/**
 * Callback function associated with an external input port's bitmask. Gets
 * called when a new data message arrives at that input port.
 * - Pop the input data message into the container object. This message must be
 *   returned before the end of the function.
 * - Routes different opcodes to their respective handling.
 */
ar_result_t spl_cntr_input_data_q_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;

   // Find the port that was triggered.
   spl_cntr_ext_in_port_t *ext_in_port_ptr =
      (spl_cntr_ext_in_port_t *)cu_get_ext_in_port_for_bit_index(base_ptr, channel_bit_index);

   VERIFY(result, ext_in_port_ptr);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Input data trigger on ext input port idx = %ld, miid = 0x%lx!",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
#endif

   // Pop the input data message.
   TRY(result, cu_get_input_data_msg(base_ptr, &ext_in_port_ptr->gu));

   // Process messages
   switch (ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      case SPF_MSG_DATA_BUFFER:
      {
         if (spl_cntr_fwk_extn_voice_delivery_need_drop_data_msg(me_ptr, ext_in_port_ptr))
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_HIGH_PRIO,
                         "Dropping data from buffer on ext input port idx = %ld, miid = 0x%lx for voice delivery cntr "
                         "due to data length < 1ms.",
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
            spl_cntr_drop_input_data(me_ptr, ext_in_port_ptr);

            // if input buffer is freed then return otherwise continue to handle matadata.
            if (!ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
            {
               break;
            }
         }

         // Triggers topo process if ready.
         if (TOPO_DATA_FLOW_STATE_FLOWING != ext_in_port_ptr->topo_buf.data_flow_state)
         {
            spl_cntr_ext_in_port_handle_data_flow_begin(me_ptr, ext_in_port_ptr);
         }

         //if prebuffer then hold it in the prebuffer Q
         if (ext_in_port_ptr->cu.preserve_prebuffer)
         {
            cu_ext_in_handle_prebuffer(&me_ptr->cu,
                                       &ext_in_port_ptr->gu,
                                       (me_ptr->fwk_extn_info.resync_signal_ptr) ? 1 : 0); //for VpTx, minimum buffer to hold is 1
         }

#ifdef PROC_DELAY_DEBUG
         gen_topo_module_t *module_ptr = (gen_topo_module_t*) ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr;

         if (IS_VOICE_SCENARIO_ID(module_ptr->gu.sg_ptr->sid))
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                                  DBG_HIGH_PRIO,
                                  "PROC_DELAY_DEBUG: SC Module 0x%lX: Ext Input data received on port 0x%lX",
                                  module_ptr->gu.module_instance_id, ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);
         }
#endif
         TRY(result, spl_cntr_check_and_process_audio(me_ptr, ext_in_port_ptr->cu.bit_mask));
         break;
      }
      case SPF_MSG_DATA_MEDIA_FORMAT:
      {
         SPL_CNTR_MSG(me_ptr->cu.gu_ptr->log_id, DBG_MED_PRIO, "Received input media format on the data path");

         // Can't do TRY because we need to free the input data command.
         result = spl_cntr_handle_data_path_media_fmt(me_ptr, ext_in_port_ptr);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "SPL_CNTR spl_cntr_handle_data_path_media_fmt result: %d",
                      result);
#endif

         if (SPF_MSG_DATA_MEDIA_FORMAT == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
         {
            cu_free_input_data_cmd(&me_ptr->cu, &ext_in_port_ptr->gu, result);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_MED_PRIO,
                         "SPL_CNTR cu_free_input_data_cmd result: %d",
                         result);
#endif
         }

         VERIFY(result, AR_EOK == result);
         break;
      }
      default:
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_ERROR_PRIO,
                      "SPL_CNTR received unsupported message 0x%lx",
                      ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);

         result = cu_free_input_data_cmd(&me_ptr->cu, &ext_in_port_ptr->gu, AR_EUNSUPPORTED);
         break;
      }
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Callback function associated with an external output port's bitmask. Gets
 * called when the downstream service returns a buffer to the output port's
 * buffer queue.
 * - Try to get an output buffer in case one is already held.
 * - Check and process audio.
 */
ar_result_t spl_cntr_output_buf_q_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;

   // Find the port that was triggered.
   spl_cntr_ext_out_port_t *ext_out_port_ptr =
      (spl_cntr_ext_out_port_t *)cu_get_ext_out_port_for_bit_index(base_ptr, channel_bit_index);

   VERIFY(result, ext_out_port_ptr);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Output buffer trigger on ext output port idx = %ld, miid = 0x%lx!",
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
#endif

   // This can return AR_ENEEDMORE in ts_disc case.
   result = (spl_cntr_get_output_buffer(me_ptr, ext_out_port_ptr) & (~AR_ENEEDMORE));
   VERIFY(result, result == AR_EOK);

   spl_cntr_check_and_process_audio(me_ptr, ext_out_port_ptr->cu.bit_mask);

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Handling for the data path media format command. Convert media format to internal struct and
 * delegate handling to common media format handler.
 */
ar_result_t spl_cntr_handle_data_path_media_fmt(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t       result                  = AR_EOK;
   spf_msg_header_t *in_media_fmt_header_ptr = (spf_msg_header_t *)ext_in_port_ptr->cu.input_data_q_msg.payload_ptr;

   spf_msg_media_format_t *in_spf_msg_fmt_ptr = (spf_msg_media_format_t *)&in_media_fmt_header_ptr->payload_start;
   topo_media_fmt_t        topo_med_fmt;

   tu_convert_media_fmt_spf_msg_to_topo(me_ptr->topo.t_base.gu.log_id,
                                        in_spf_msg_fmt_ptr,
                                        &topo_med_fmt,
                                        me_ptr->cu.heap_id);

   TRY(result,
       spl_cntr_input_media_format_received((void *)me_ptr,
                                            (gu_ext_in_port_t *)ext_in_port_ptr,
                                            &topo_med_fmt,
											NULL,/* cu_ext_in_port_upstream_frame_length_t upstream_frame_len_ptr*/
                                            TRUE /* is_data_path */));

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->cu.gu_ptr->log_id)
   {
   }

   return result;
}
