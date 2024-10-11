/**
 *   \file capi_audio_dam_buffer_island.c
 *   \brief
 *        This file contains CAPI implementation of Audio Dam buffer module
 *
 * \copyright
 * Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_audio_dam_buffer_i.h"

capi_err_t get_output_arr_index_from_ctrl_port_id(capi_audio_dam_t *me_ptr,
                                                  uint32_t          ctrl_port_id,
                                                  uint32_t         *num_out_ports_ptr,
                                                  uint32_t          out_port_arr_idxs[])
{
   bool_t mapping_found = false;
   for (uint32_t arr_index = 0; arr_index < me_ptr->max_output_ports; arr_index++)
   {
      if (ctrl_port_id == me_ptr->out_port_info_arr[arr_index].ctrl_port_id)
      {
         out_port_arr_idxs[*num_out_ports_ptr] = arr_index;
         (*num_out_ports_ptr)++;

         DAM_MSG_ISLAND(me_ptr->miid,
                        DBG_HIGH_PRIO,
                        "capi_audio_dam: Mapping found Ctrl Port ID = 0x%lx to Out port id = %lu",
                        ctrl_port_id,
                        me_ptr->out_port_info_arr[arr_index].port_id);
         mapping_found = TRUE;
      }
   }

   if (mapping_found)
   {
      return AR_EOK;
   }

   DAM_MSG_ISLAND(me_ptr->miid,
                  DBG_ERROR_PRIO,
                  "capi_audio_dam: Port ID = 0x%lx to index mapping not found.",
                  ctrl_port_id);
   return AR_EBADPARAM;
}

/*------------------------------------------------------------------------
  Function name: capi_audio_dam_buffer_set_param
  Sets either a parameter value or a parameter structure containing multiple
  parameters. In the event of a failure, the appropriate error code is
  returned.
 * -----------------------------------------------------------------------*/
capi_err_t capi_audio_dam_buffer_set_param(capi_t                 *capi_ptr,
                                           uint32_t                param_id,
                                           const capi_port_info_t *port_info_ptr,
                                           capi_buf_t             *params_ptr)
{
   capi_err_t        result = CAPI_EOK;
   capi_audio_dam_t *me_ptr = (capi_audio_dam_t *)((capi_ptr));

   if (param_id == INTF_EXTN_PARAM_ID_IMCL_INCOMING_DATA)
   {
      /*TBD: Currently port ID is in the port info. We should change this later.*/
      if (NULL == params_ptr->data_ptr)
      {
         DAM_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "capi_dam: Set param id 0x%lx, received null buffer", param_id);
         return CAPI_EBADPARAM;
      }

      // Level 1 check
      if (params_ptr->actual_data_len < MIN_INCOMING_IMCL_PARAM_SIZE_SVA_DAM)
      {
         DAM_MSG_ISLAND(me_ptr->miid,
                        DBG_ERROR_PRIO,
                        "CAPI V2 DAM: Invalid payload size for incoming data %d",
                        params_ptr->actual_data_len);
         return CAPI_ENEEDMORE;
      }

      intf_extn_param_id_imcl_incoming_data_t *payload_ptr =
         (intf_extn_param_id_imcl_incoming_data_t *)params_ptr->data_ptr;

      uint32_t ctrl_port_id = payload_ptr->port_id;

      result = capi_audio_dam_imc_set_param_handler(me_ptr, ctrl_port_id, params_ptr);

      DAM_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "capi_audio_dam: IMC set param handler result 0x%x", result);
      return result;
   }

   return capi_audio_dam_buffer_set_param_non_island(capi_ptr, param_id, port_info_ptr, params_ptr);
}

capi_err_t capi_check_and_close_the_gate(capi_audio_dam_t *me_ptr, uint32_t op_arr_index, bool_t is_destroy)
{
   // Close the gate
   if (TRUE == me_ptr->out_port_info_arr[op_arr_index].is_gate_opened)
   {
      // Reset the output port state.
      me_ptr->out_port_info_arr[op_arr_index].is_gate_opened = FALSE;
   }

   me_ptr->out_port_info_arr[op_arr_index].is_drain_history           = FALSE;
   me_ptr->out_port_info_arr[op_arr_index].is_pending_gate_close      = FALSE;
   me_ptr->out_port_info_arr[op_arr_index].ftrt_unread_data_len_in_us = 0;

   /** reset batching mode to False, if it was enabled at gate open */
   audio_dam_stream_reader_enable_batching_mode(me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr,
                                                FALSE /**enable batching mode*/,
                                                0);

   // When gate is closed or port is closed, move the out port to nontriggerable group
   // for both signal and data triggers.`
   if (me_ptr->out_port_info_arr[op_arr_index].is_started)
   {
      me_ptr->data_tp.non_trigger_group_ptr
         ->out_port_grp_policy_ptr[me_ptr->out_port_info_arr[op_arr_index].port_index] =
         FWK_EXTN_PORT_NON_TRIGGER_BLOCKED;

      me_ptr->signal_tp.non_trigger_group_ptr
         ->out_port_grp_policy_ptr[me_ptr->out_port_info_arr[op_arr_index].port_index] =
         FWK_EXTN_PORT_NON_TRIGGER_BLOCKED;

      me_ptr->data_tp.trigger_groups_ptr[0]
         .out_port_grp_affinity_ptr[me_ptr->out_port_info_arr[op_arr_index].port_index] =
         FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE;

      me_ptr->signal_tp.trigger_groups_ptr[0]
         .out_port_grp_affinity_ptr[me_ptr->out_port_info_arr[op_arr_index].port_index] =
         FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE;

      capi_audio_dam_change_trigger_policy(me_ptr);
   }

   // During gate open, the channels might have been reordered based on best channel info from detection engines.
   // When control port is closing the gate but Dam module is not destroyed. we just need to revert the channel order
   // as per the output port cfg calib.
   //
   // For Acoustic Activity Detection usecase, we do not support best channel feature, so we dont have to revert the channel order.
   if (FALSE == me_ptr->out_port_info_arr[op_arr_index].is_peer_aad)
   {
      capi_audio_dam_reorder_chs_at_gate_close(me_ptr, op_arr_index, is_destroy);

      // In AAD usecase, do not update KPPS in process context.
      // And its not required to bump up KPPS since there is no IPC exchange in AAD opens the gate.
      capi_audio_dam_buffer_update_kpps_vote(me_ptr);
   }

   DAM_MSG_ISLAND(me_ptr->miid,
                  DBG_HIGH_PRIO,
                  "DAM: Gate at Port id %lu closed",
                  me_ptr->out_port_info_arr[op_arr_index].port_id);
   return CAPI_EOK;
}

capi_err_t capi_audio_dam_change_trigger_policy(capi_audio_dam_t *me_ptr)
{
   capi_err_t result = CAPI_EOK;

   // evaluate if tp needs to enabled/disabled.
   bool_t need_to_enable_tp = TRUE;

   // Note: We cannot use optimization to enable and disable TGP dynamically for Dam
   // when there are two Dams in same container this optimization will cause issues.
   //
   // eg: In Voice Activation usecase its causing KW read latency issue. If Dam switches to default TGP
   //     when gate is closed. Container pops and holds ext out buffer. When gate opens, dam
   //     updates TGP in cmd context to "optional" output and waits for the next interrupt to start
   //     draining KW data, causing almost ~40ms of latency.
   //
   //      Ideally if we are able to return the
   //     unprocessed buffer to the dataQ we should be able to trigger container before interrupt
   //     is fired, but with 2 Dams in Voice Activation+Acoustic Activity Detection, returning the buffer to dataQ is causing infinite loop.
   //     check gen_cntr_peer_output_island file for more details.
#if 0
   bool_t need_to_enable_tp = FALSE;
   if (me_ptr->cannot_toggle_to_default_tgp)
   {
      // tgp must be always enabled in this case,
      need_to_enable_tp = TRUE;
   }
   else // can toggle TGP
   {
      // TP needs to be enabled if atleast one output's gate is opened.
      for (uint32_t op_idx = 0; op_idx < me_ptr->max_output_ports; op_idx++)
      {
         if (me_ptr->out_port_info_arr[op_idx].is_gate_opened)
         {
            need_to_enable_tp = TRUE;
         }
      }
   }
#endif

   DAM_MSG_ISLAND(me_ptr->miid,
                  DBG_HIGH_PRIO,
                  "capi_audio_dam: Changing trigger policy toggle_allowed: %lu need_to_enable:%lu, "
                  "currently_enabled:%lu",
                  me_ptr->cannot_toggle_to_default_tgp,
                  need_to_enable_tp,
                  me_ptr->is_tp_enabled);

   // return if already disabled
   if (!me_ptr->is_tp_enabled && !need_to_enable_tp)
   {
      return result;
   }

   if (need_to_enable_tp)
   {
      // raise signal trigger policy change, this change can be incremental i.e making a secondry
      // output port optional/blocked at gate open
      me_ptr->policy_chg_cb.change_signal_trigger_policy_cb_fn(me_ptr->policy_chg_cb.context_ptr,
                                                               me_ptr->signal_tp.non_trigger_group_ptr,
                                                               FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL,
                                                               1,
                                                               me_ptr->signal_tp.trigger_groups_ptr);

      // raise data trigger policy change, this change can be incremental i.e making a secondry
      // output port optional/blocked at gate open
      me_ptr->policy_chg_cb.change_data_trigger_policy_cb_fn(me_ptr->policy_chg_cb.context_ptr,
                                                             me_ptr->data_tp.non_trigger_group_ptr,
                                                             FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL,
                                                             1,
                                                             me_ptr->data_tp.trigger_groups_ptr);
   }
   else // Disable TP
   {
      // Disable trigger policy
      me_ptr->policy_chg_cb.change_data_trigger_policy_cb_fn(me_ptr->policy_chg_cb.context_ptr,
                                                             NULL,
                                                             FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL,
                                                             0,
                                                             NULL);

      // Disable trigger policy
      me_ptr->policy_chg_cb.change_signal_trigger_policy_cb_fn(me_ptr->policy_chg_cb.context_ptr,
                                                               NULL,
                                                               FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL,
                                                               0,
                                                               NULL);
   }

   me_ptr->is_tp_enabled = need_to_enable_tp;

   return result;
}

capi_err_t capi_dam_insert_flushing_eos_at_out_port(capi_audio_dam_t   *me_ptr,
                                                    capi_stream_data_t *output,
                                                    bool_t              skip_voting_on_eos,
                                                    eos_type_t          eos_typ)
{
   capi_err_t             capi_result    = CAPI_EOK;
   capi_stream_data_v2_t *out_stream_ptr = (capi_stream_data_v2_t *)output;

   if (CAPI_STREAM_V2 != out_stream_ptr->flags.stream_data_version)
   {
      DAM_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "capi_audio_dam: stream version must be 1");
      return CAPI_EFAILED;
   }
   module_cmn_md_list_t **md_list_pptr = &out_stream_ptr->metadata_list_ptr;
   module_cmn_md_t       *new_md_ptr   = NULL;
   module_cmn_md_eos_t   *eos_md_ptr   = NULL;
   capi_heap_id_t         heap;
   heap.heap_id = (uint32_t)me_ptr->heap_id;

   capi_result = me_ptr->metadata_handler.metadata_create(me_ptr->metadata_handler.context_ptr,
                                                          md_list_pptr,
                                                          sizeof(module_cmn_md_eos_t),
                                                          heap,
                                                          FALSE /*is_out_band*/,
                                                          &new_md_ptr);

   new_md_ptr->metadata_id                     = MODULE_CMN_MD_ID_EOS;
   new_md_ptr->offset                          = 0;
   eos_md_ptr                                  = (module_cmn_md_eos_t *)&new_md_ptr->metadata_buf;
   eos_md_ptr->flags.is_flushing_eos           = TRUE;
   eos_md_ptr->flags.is_internal_eos           = TRUE;
   eos_md_ptr->flags.skip_voting_on_dfs_change = skip_voting_on_eos;
   eos_md_ptr->cntr_ref_ptr                    = NULL;
   new_md_ptr->tracking_ptr                    = NULL;

   DAM_MSG_ISLAND(me_ptr->miid,
                  DBG_HIGH_PRIO,
                  "DAM: Created and inserted flushing eos at output defer_voting:%lu",
                  skip_voting_on_eos);

   return capi_result;
}
