/**
 * \file cu_ext_port_util.c
 * \brief
 *     This file contains container utility functions for external port handling (input and output).
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "cu_i.h"

/* =======================================================================
Public Function Definitions
========================================================================== */

ar_result_t cu_determine_ext_out_buffering(cu_base_t *base_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr)
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

   if (SPF_IS_RAW_COMPR_DATA_FMT(ext_out_port_ptr->media_fmt.data_format))
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

   if ((0 == ext_out_port_ptr->icb_info.ds_frame_len.frame_len_samples) &&
       (0 == ext_out_port_ptr->icb_info.ds_frame_len.sample_rate) &&
       (0 == ext_out_port_ptr->icb_info.ds_frame_len.frame_len_us) && (0 == ext_out_port_ptr->icb_info.ds_period_us) &&
       (0 == ext_out_port_ptr->icb_info.ds_flags.word) &&
       (0 == ext_out_port_ptr->icb_info.ds_sid))
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "downstream didn't inform about frame length yet for ext out port (0x%lX, %lu).",
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.index);
      return AR_EOK;
   }

   if ((0 == base_ptr->cntr_frame_len.frame_len_samples) && (0 == base_ptr->cntr_frame_len.frame_len_us) &&
       (0 == base_ptr->cntr_frame_len.sample_rate))
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
   us.len                 = base_ptr->cntr_frame_len;
   us.log_id              = base_ptr->gu_ptr->log_id;

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

// utility function to send ext output port property update to downstream peer port.
static ar_result_t cu_send_upstream_stopped_ack_to_downstream(cu_base_t *me_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   if (!gu_ext_out_port_ptr || !me_ptr || !gu_ext_out_port_ptr->downstream_handle.spf_handle_ptr)
   {
      return AR_EFAILED;
   }

   // Get spf_msg_header_t* from the intent buf pointer.
   // spf_msg_header_t *header_ptr;

   // no special payload
   uint32_t msg_pkt_size = GET_SPF_MSG_REQ_SIZE(0);

   // Get onetime msg from the buffer manager.
   // Reinterpret the node itself as a spf_msg_t.
   spf_msg_t msg;
   if (AR_DID_FAIL(spf_msg_create_msg(&msg,
                                      &msg_pkt_size,
                                      SPF_MSG_CMD_UPSTREAM_STOPPED_ACK,
                                      NULL,
                                      NULL,
                                      gu_ext_out_port_ptr->downstream_handle.spf_handle_ptr,
                                      me_ptr->heap_id)))
   {
      CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Failed to create message");
      return AR_EFAILED;
   }
   // header_ptr = (spf_msg_header_t *)msg.payload_ptr;

   result = spf_msg_send_cmd(&msg, gu_ext_out_port_ptr->downstream_handle.spf_handle_ptr);
   if (AR_DID_FAIL(result))
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             " Failed to push the upstream (0x%lX, 0x%lx) stopped message to downstream %d. ",
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.id,
             result);
      spf_msg_return_msg(&msg);
      return result;
   }
   else
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_LOW_PRIO,
             " sent upstream (0x%lX, 0x%lx) stopped to downstream ",
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.id);
   }

   return result;
}

static ar_result_t cu_send_us_state_to_ds(cu_base_t         *base_ptr,
                                          gu_ext_out_port_t *ext_out_port_ptr,
                                          topo_port_state_t  ds_state)
{
   // send only stopped state to downstream, no need to send suspend since data will not be flushed
   // by the downstream at suspend.
   if (TOPO_PORT_STATE_STOPPED == ds_state)
   {
      // send a message to DS indicating US stopped. This helps DS also flush. DS cannot flush until US is
      // stopped (inf loop).
      return cu_send_upstream_stopped_ack_to_downstream(base_ptr, ext_out_port_ptr);
   }
   // no need to communicate started state for peer container
   return AR_EOK;
}

typedef enum
{
   CU_UPDATE_STATES = 1,
   CU_INFORM_US     = 2,
   CU_INFORM_DS     = 4,
} cu_states_update_mask;

ar_result_t cu_process_peer_port_property(cu_base_t    *base_ptr,
                                          spf_handle_t *dst_handle_ptr,
                                          uint32_t      property_type,
                                          uint32_t      property_value,
                                          uint32_t     *need_to_update_states)
{
   ar_result_t result = AR_EOK;

   switch (property_type)
   {
      case PORT_PROPERTY_IS_UPSTREAM_RT:
      {
         gu_ext_in_port_t *gu_ext_in_port_ptr = (gu_ext_in_port_t *)dst_handle_ptr;
         cu_ext_in_port_t *ext_in_port_ptr =
            (cu_ext_in_port_t *)((uint8_t *)gu_ext_in_port_ptr + base_ptr->ext_in_port_cu_offset);

         uint32_t is_upstream_realtime = property_value;

         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX "CMD:PEER_PORT_PROPERTY_UPDATE: ext port (0x%lX,0x%lx) "
                               "received is upstream real time=%u  ",
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                is_upstream_realtime);

         // IF the connected input port has the the same property value,
         // we dont need to propagate further,
         if (is_upstream_realtime == ext_in_port_ptr->prop_info.is_us_rt)
         {
            break;
         }

         CU_SET_ONE_FWK_EVENT_FLAG(base_ptr, rt_ftrt_change);

         ext_in_port_ptr->prop_info.is_us_rt = is_upstream_realtime;

         // no ICB related assignments are needed here because only upstream decides num of buffers at ext out port.

         // Propagate to the modules in the topology.
         uint32_t recurse_depth = 0;
         result |= base_ptr->topo_vtbl_ptr->propagate_port_property_forwards(base_ptr->topo_ptr,
                                                                             gu_ext_in_port_ptr->int_in_port_ptr,
                                                                             PORT_PROPERTY_IS_UPSTREAM_RT,
                                                                             (uint32_t)is_upstream_realtime,
                                                                             &recurse_depth);
         // update states, because downstream state may propagate due to US being RT
         // downstream should be informed about the upstream RT property
         // upstream should be informed about the downstream states which gests propagated if upstream RT is true.
         *need_to_update_states |= (CU_UPDATE_STATES | CU_INFORM_DS | CU_INFORM_US);

         break;
      }
      case PORT_PROPERTY_IS_DOWNSTREAM_RT:
      {
         gu_ext_out_port_t *gu_ext_out_port_ptr = (gu_ext_out_port_t *)dst_handle_ptr;

         cu_ext_out_port_t *ext_out_port_ptr =
            (cu_ext_out_port_t *)(((uint8_t *)gu_ext_out_port_ptr + base_ptr->ext_out_port_cu_offset));

         uint32_t is_downstream_rt = property_value;

         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX
                "CMD:PEER_PORT_PROPERTY_UPDATE: ext port (0x%lX,0x%lx) received is downstream real time=%lu ",
                gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
                gu_ext_out_port_ptr->int_out_port_ptr->cmn.id,
                is_downstream_rt);

         // IF the connected output port has the the same property value we dont need to propagate further,
         if (is_downstream_rt == ext_out_port_ptr->prop_info.is_ds_rt)
         {
            // dont propagate from this ext output, since it is already propagated.
            break;
         }

         CU_SET_ONE_FWK_EVENT_FLAG(base_ptr, rt_ftrt_change);
         ext_out_port_ptr->prop_info.is_ds_rt = is_downstream_rt;

         // Propagate to the modules in the topology.
         uint32_t recurse_depth = 0;
         result |= base_ptr->topo_vtbl_ptr->propagate_port_property_backwards(base_ptr->topo_ptr,
                                                                              gu_ext_out_port_ptr->int_out_port_ptr,
                                                                              PORT_PROPERTY_IS_DOWNSTREAM_RT,
                                                                              (uint32_t)is_downstream_rt,
                                                                              &recurse_depth);

         if (AR_SUCCEEDED(result))
         {
            ext_out_port_ptr->icb_info.ds_flags.is_real_time = is_downstream_rt;

            // for ICB purpose (if this port's is_rt changes, then we may need to re-create bufs
            base_ptr->cntr_vtbl_ptr->ext_out_port_recreate_bufs(base_ptr, gu_ext_out_port_ptr);
         }

         // need to inform upstream about the propagated downstream RT property
         *need_to_update_states |= (CU_INFORM_US);

         break;
      }

      case PORT_PROPERTY_TOPO_STATE:
      {
         // state is always propagated in control path only from downstream to upstream.
         gu_ext_out_port_t *gu_ext_out_port_ptr = (gu_ext_out_port_t *)dst_handle_ptr;

         cu_ext_out_port_t *ext_out_port_ptr =
            (cu_ext_out_port_t *)(((uint8_t *)gu_ext_out_port_ptr + base_ptr->ext_out_port_cu_offset));

         topo_port_state_t ds_state = (topo_port_state_t)property_value;

         ext_out_port_ptr->propagated_port_state = ds_state;

         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                "CMD:PEER_PORT_PROPERTY_UPDATE: ext port (0x%lX,0x%lx) received state from downstream=%lu ",
                gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
                gu_ext_out_port_ptr->int_out_port_ptr->cmn.id,
                ds_state);

         topo_port_state_t self_port_state =
            topo_sg_state_to_port_state(base_ptr->topo_vtbl_ptr->get_sg_state(gu_ext_out_port_ptr->sg_ptr));

         topo_port_state_t ds_downgraded_state =
            cu_evaluate_n_update_ext_out_ds_downgraded_port_state(base_ptr, gu_ext_out_port_ptr);

         topo_port_state_t downgraded_state = tu_get_downgraded_state(self_port_state, ds_downgraded_state);

         topo_port_state_t old_val = TOPO_PORT_STATE_INVALID;

         // Get current input ports state from sg state.
         base_ptr->topo_vtbl_ptr->get_port_property(base_ptr->topo_ptr,
                                                    TOPO_DATA_OUTPUT_PORT_TYPE,
                                                    PORT_PROPERTY_TOPO_STATE,
                                                    (void *)gu_ext_out_port_ptr->int_out_port_ptr,
                                                    (uint32_t *)&old_val);

         // IF the new downgraded state is same as the existing downgraded state then don't need to propagate.
         // If the new downgraded state is PREPARE then it can be ignored as well.
         if (downgraded_state == old_val || TOPO_PORT_STATE_PREPARED == downgraded_state)
         {
            break;
         }

         CU_SET_ONE_FWK_EVENT_FLAG(base_ptr, port_state_change);

         // update states, because downstream state is changed
         // inform upstream about the udpated downstream state
         *need_to_update_states |= (CU_UPDATE_STATES | CU_INFORM_US);

         if (ext_out_port_ptr->prop_info.prop_enabled)
         {
            ext_out_port_ptr->prop_info.prop_us_state_ack_to_ds_fn(base_ptr, gu_ext_out_port_ptr, ds_state);
         }

         break;
      }

      default:
      {
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "CMD:PEER_PORT_PROPERTY_UPDATE: Invalid property_type=0x%lx",
                property_type);
         result = AR_EUNSUPPORTED;
         break;
      }
   }
   return result;
}

/**
 * a simple way to pass 2 props
 * note that depending pn prop1.num_prop, only one prop can also be present.
 */
typedef struct spf_msg_peer_two_port_property_update_t
{
   spf_msg_peer_port_property_update_t prop1;
   spf_msg_peer_port_property_info_t   prop2;
} spf_msg_peer_two_port_property_update_t;

ar_result_t cu_process_peer_port_property_payload(cu_base_t    *base_ptr,
                                                  int8_t       *payload_ptr,
                                                  spf_handle_t *dst_handle_ptr)
{
   ar_result_t result                    = AR_EOK;
   uint32_t    get_need_to_update_states = FALSE;

   spf_msg_peer_two_port_property_update_t *two_prop_ptr = (spf_msg_peer_two_port_property_update_t *)payload_ptr;
   spf_msg_peer_port_property_info_t       *cur_ptr      = two_prop_ptr->prop1.payload;

   for (uint32_t i = 0; i < two_prop_ptr->prop1.num_properties; i++)
   {
      result |= cu_process_peer_port_property(base_ptr,
                                              dst_handle_ptr,
                                              cur_ptr[i].property_type,
                                              cur_ptr[i].property_value,
                                              &get_need_to_update_states);
   }

   if (get_need_to_update_states & CU_UPDATE_STATES)
   {
      // state update depends on upstream RT property.
      result |= cu_update_all_sg_port_states(base_ptr, TRUE);
   }

   if (get_need_to_update_states & CU_INFORM_US)
   {
      result |= cu_inform_upstream_about_downstream_property(base_ptr);
   }
   if (get_need_to_update_states & CU_INFORM_DS)
   {
      result |= cu_inform_downstream_about_upstream_property(base_ptr);
   }

   return result;
}

ar_result_t cu_handle_peer_port_property_update_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:PEER_PORT_PROPERTY_UPDATE: current channel mask=0x%x",
          base_ptr->curr_chan_mask);

   // Get the ctrl port handle from the message header.
   spf_msg_header_t *header_ptr = (spf_msg_header_t *)base_ptr->cmd_msg.payload_ptr;
   if (!base_ptr->cmd_msg.payload_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "CMD:PEER_PORT_PROPERTY_UPDATE: Received null payload");
      return AR_EFAILED;
   }

   result =
      cu_process_peer_port_property_payload(base_ptr, (int8_t *)&header_ptr->payload_start, header_ptr->dst_handle_ptr);
   return result;
}

// utility function to send ext output port property update to downstream peer port.
static ar_result_t cu_propagate_to_peer_container_ext_port(cu_base_t                           *me_ptr,
                                                           gu_peer_handle_t                    *peer_hdl_ptr,
                                                           spf_msg_peer_port_property_update_t *prop_ptr)
{
   ar_result_t result = AR_EOK;

   if (!me_ptr || !prop_ptr)
   {
      return AR_EFAILED;
   }

   if (!peer_hdl_ptr || !peer_hdl_ptr->spf_handle_ptr)
   {
      CU_MSG(me_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "Warning: Can't propagate to peer container - peer handle is NULL");
      return AR_ENOTREADY;
      // when new connection comes, before handles are exchanged - we need to return error so that the prop flags are
      // reset propagation will be re-attempted at prepare
   }

   // Get spf_msg_header_t* from the intent buf pointer.
   spf_msg_header_t *header_ptr;

   // Update the required for the data ptr and msg header.
   uint32_t msg_pkt_size =
      GET_SPF_MSG_REQ_SIZE((prop_ptr->num_properties - 1) * sizeof(spf_msg_peer_port_property_info_t) +
                           sizeof(spf_msg_peer_port_property_update_t));

   // Get onetime msg from the buffer manager.
   // Reinterpret the node itself as a spf_msg_t.
   spf_msg_t msg;
   if (AR_DID_FAIL(spf_msg_create_msg(&msg,
                                      &msg_pkt_size,
                                      SPF_MSG_CMD_PEER_PORT_PROPERTY_UPDATE,
                                      NULL,
                                      NULL,
                                      peer_hdl_ptr->spf_handle_ptr,
                                      me_ptr->heap_id)))
   {
      CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Failed to create message");
      return AR_EFAILED;
   }
   header_ptr = (spf_msg_header_t *)msg.payload_ptr;

   // update the actual size of control message
   spf_msg_peer_two_port_property_update_t *prop_hdr_ptr =
      (spf_msg_peer_two_port_property_update_t *)&header_ptr->payload_start[0];
   prop_hdr_ptr->prop1 = *prop_ptr;

   if (prop_ptr->num_properties > 1)
   {
      spf_msg_peer_two_port_property_update_t *two_prop_ptr = (spf_msg_peer_two_port_property_update_t *)prop_ptr;
      prop_hdr_ptr->prop2                                   = two_prop_ptr->prop2;
   }

   result = spf_msg_send_cmd(&msg, peer_hdl_ptr->spf_handle_ptr);
   if (AR_DID_FAIL(result))
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             " Failed to push the port property update msg to peer contianer %d",
             result);
      spf_msg_return_msg(&msg);
      return result;
   }

   return result;
}

static ar_result_t cu_propagate_to_ds_peer_ext_port(cu_base_t                           *base_ptr,
                                                    gu_ext_out_port_t                   *ext_out_port_ptr,
                                                    spf_msg_peer_port_property_update_t *prop_ptr)
{
   return cu_propagate_to_peer_container_ext_port(base_ptr, &ext_out_port_ptr->downstream_handle, prop_ptr);
}

static ar_result_t cu_propagate_to_us_peer_ext_port(cu_base_t                           *base_ptr,
                                                    gu_ext_in_port_t                    *ext_in_port_ptr,
                                                    spf_msg_peer_port_property_update_t *prop_ptr)
{
   return cu_propagate_to_peer_container_ext_port(base_ptr, &ext_in_port_ptr->upstream_handle, prop_ptr);
}

/* Only stop, suspend, prepare and start is propagated to upstream. */
static bool_t cu_need_to_inform_peer_about_state(topo_port_state_t my_state)
{
   switch (my_state)
   {
      case TOPO_SG_STATE_STOPPED:
      case TOPO_SG_STATE_SUSPENDED:
      case TOPO_SG_STATE_STARTED:
      {
         return TRUE;
         break;
      }
      default:
      {
         return FALSE;
      }
   }
}

ar_result_t cu_inform_downstream_about_upstream_property(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;

   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = base_ptr->gu_ptr->ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      gu_ext_out_port_t *gu_ext_out_port_ptr = ext_out_port_list_ptr->ext_out_port_ptr;
      cu_ext_out_port_t *ext_out_port_ptr =
         (cu_ext_out_port_t *)((uint8_t *)gu_ext_out_port_ptr + base_ptr->ext_out_port_cu_offset);

      if (!ext_out_port_ptr->prop_info.prop_enabled)
      {
         continue;
      }

      // if downstream is not prepared or started then no need to inform the upstream-RT property.
      if (ext_out_port_ptr->connected_port_state != TOPO_PORT_STATE_PREPARED &&
          ext_out_port_ptr->connected_port_state != TOPO_PORT_STATE_STARTED)
      {
         continue;
      }

      spf_msg_peer_two_port_property_update_t two_prop;
      memset(&two_prop, 0, sizeof(spf_msg_peer_two_port_property_update_t));
      spf_msg_peer_port_property_info_t *cur_prop_ptr = two_prop.prop1.payload;

      /************* is upstream real time */
      uint32_t is_rt        = FALSE;
      uint32_t old_is_us_rt = FALSE;
      // out port propagates upstream RT
      base_ptr->topo_vtbl_ptr->get_port_property(base_ptr->topo_ptr,
                                                 TOPO_DATA_OUTPUT_PORT_TYPE,
                                                 PORT_PROPERTY_IS_UPSTREAM_RT,
                                                 (void *)gu_ext_out_port_ptr->int_out_port_ptr,
                                                 (uint32_t *)&is_rt);

      if ((is_rt != ext_out_port_ptr->prop_info.is_us_rt) || (!ext_out_port_ptr->prop_info.is_rt_informed))
      {
         two_prop.prop1.num_properties++;
         cur_prop_ptr->property_type  = PORT_PROPERTY_IS_UPSTREAM_RT;
         cur_prop_ptr->property_value = is_rt;
         cur_prop_ptr                 = &two_prop.prop2;
         // assume prop will be successful
         ext_out_port_ptr->prop_info.is_rt_informed = TRUE;
         old_is_us_rt                               = ext_out_port_ptr->prop_info.is_us_rt;
         ext_out_port_ptr->prop_info.is_us_rt       = is_rt;

         //mark this flag so that container real time flag can be updated.
         CU_SET_ONE_FWK_EVENT_FLAG(base_ptr, rt_ftrt_change);

         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_MED_PRIO,
                SPF_LOG_PREFIX
                "Propagating to peer port of (mod-inst-id, port-id) (0x%lX, 0x%lx) upstream real time is_rt=%u",
                gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
                gu_ext_out_port_ptr->int_out_port_ptr->cmn.id,
                is_rt);
      }
      /************* state should not be propagated downstream */

      if (two_prop.prop1.num_properties > 0)
      {
         result = ext_out_port_ptr->prop_info.prop_us_prop_to_ds_fn(base_ptr, gu_ext_out_port_ptr, &two_prop.prop1);
         if (AR_FAILED(result))
         {
            // resetting this flag so that we retry again later.
            ext_out_port_ptr->prop_info.is_rt_informed = FALSE;
            ext_out_port_ptr->prop_info.is_us_rt       = old_is_us_rt;

            CU_MSG(base_ptr->gu_ptr->log_id,
                   DBG_MED_PRIO,
                   "Cannot propagate to peer port of (mod-inst-id, port-id) (0x%lX, 0x%lx) upstream real time is_rt=%u, resetting",
                   gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
                   gu_ext_out_port_ptr->int_out_port_ptr->cmn.id,
                   old_is_us_rt);
         }
      }
   }

   // we don't want graph command to fail because of propagation failures.
   return AR_EOK;
}

ar_result_t cu_inform_upstream_about_downstream_property(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = base_ptr->gu_ptr->ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gu_ext_in_port_t *gu_ext_in_port_ptr = ext_in_port_list_ptr->ext_in_port_ptr;
      cu_ext_in_port_t *ext_in_port_ptr =
         (cu_ext_in_port_t *)((uint8_t *)gu_ext_in_port_ptr + base_ptr->ext_in_port_cu_offset);

      if (!ext_in_port_ptr->prop_info.prop_enabled)
      {
         continue;
      }

      // if upstream is not prepared or started then no need to inform the downstrea-RT property and downstrea-state.
      if (ext_in_port_ptr->connected_port_state != TOPO_PORT_STATE_PREPARED &&
          ext_in_port_ptr->connected_port_state != TOPO_PORT_STATE_STARTED)
      {
         continue;
      }

      spf_msg_peer_two_port_property_update_t two_prop;
      memset(&two_prop, 0, sizeof(spf_msg_peer_two_port_property_update_t));
      spf_msg_peer_port_property_info_t *cur_prop_ptr = two_prop.prop1.payload;

      {
         /**************** is downstream RT */
         uint32_t is_rt        = FALSE;
         uint32_t old_is_ds_rt = FALSE;
         // in port propagates downstream RT
         base_ptr->topo_vtbl_ptr->get_port_property(base_ptr->topo_ptr,
                                                    TOPO_DATA_INPUT_PORT_TYPE,
                                                    PORT_PROPERTY_IS_DOWNSTREAM_RT,
                                                    (void *)gu_ext_in_port_ptr->int_in_port_ptr,
                                                    (uint32_t *)&is_rt);

         if ((is_rt != ext_in_port_ptr->prop_info.is_ds_rt) || (!ext_in_port_ptr->prop_info.is_rt_informed))
         {
            two_prop.prop1.num_properties++;
            cur_prop_ptr->property_type  = PORT_PROPERTY_IS_DOWNSTREAM_RT;
            cur_prop_ptr->property_value = is_rt;
            cur_prop_ptr                 = &two_prop.prop2;

            // assume prop will be successful
            ext_in_port_ptr->prop_info.is_rt_informed = TRUE;
            old_is_ds_rt                              = ext_in_port_ptr->prop_info.is_ds_rt;
            ext_in_port_ptr->prop_info.is_ds_rt       = is_rt;

            //mark this flag so that container real time flag can be updated.
            CU_SET_ONE_FWK_EVENT_FLAG(base_ptr, rt_ftrt_change);

            CU_MSG(base_ptr->gu_ptr->log_id,
                   DBG_MED_PRIO,
                   SPF_LOG_PREFIX "Propagating to peer port of (mod-inst-id, port-id) (0x%lX, 0x%lx) downstream real time is_rt=%u",
                   gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                   is_rt);
         }

         /**************** downstream's state */
         topo_port_state_t state       = TOPO_PORT_STATE_INVALID;
         topo_port_state_t old_state   = TOPO_PORT_STATE_INVALID;
         topo_port_state_t my_sg_state = TOPO_PORT_STATE_INVALID;
         base_ptr->topo_vtbl_ptr->get_port_property(base_ptr->topo_ptr,
                                                    TOPO_DATA_INPUT_PORT_TYPE,
                                                    PORT_PROPERTY_TOPO_STATE,
                                                    (void *)gu_ext_in_port_ptr->int_in_port_ptr,
                                                    (uint32_t *)&state);
         my_sg_state = topo_sg_state_to_port_state(base_ptr->topo_vtbl_ptr->get_sg_state(gu_ext_in_port_ptr->sg_ptr));

         // 1.  if my sg state is STOP/SUSPEND then propagated state will also be same. send it to upstream
         // 2.  if my sg state is START then propagated state will be based on the downstream state propagation. send
         // the propagated state to the upstream.
         // 3.  if my sg state is PREPARE then propagated state will be STOP (since we are not handling PREPARE for
         // propagation), now we don't need to send the STOP state to the upstream. Because if we do then upstream will
         // send upstream-stop-ack. All this is unnecessary.
         if (cu_need_to_inform_peer_about_state(state) && cu_need_to_inform_peer_about_state(my_sg_state))
         {
            CU_MSG(base_ptr->gu_ptr->log_id,
                   DBG_MED_PRIO,
                   "Informing my state (0x%lX, 0x%lx) my port state=%u to peer",
                   gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                   state);

            if ((state != ext_in_port_ptr->prop_info.port_state) || (!ext_in_port_ptr->prop_info.is_state_informed))
            {
               two_prop.prop1.num_properties++;
               cur_prop_ptr->property_type  = PORT_PROPERTY_TOPO_STATE;
               cur_prop_ptr->property_value = state;
               // assume prop will be successful
               ext_in_port_ptr->prop_info.is_state_informed = TRUE;
               old_state                                    = ext_in_port_ptr->prop_info.port_state;
               ext_in_port_ptr->prop_info.port_state        = state;

               CU_MSG(base_ptr->gu_ptr->log_id,
                      DBG_MED_PRIO,
                      "Propagating to peer port of (mod-inst-id, port-id) (0x%lX, 0x%lx) topo_state =%u",
                      gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                      state);
            }
         }

         if (two_prop.prop1.num_properties > 0)
         {
            result = ext_in_port_ptr->prop_info.prop_ds_prop_to_us_fn(base_ptr, gu_ext_in_port_ptr, &two_prop.prop1);
            if (AR_FAILED(result))
            {
               // resetting these flags so that we retry again later.
               ext_in_port_ptr->prop_info.is_rt_informed    = FALSE;
               ext_in_port_ptr->prop_info.is_ds_rt          = old_is_ds_rt;
               ext_in_port_ptr->prop_info.is_state_informed = FALSE;
               ext_in_port_ptr->prop_info.port_state        = old_state;

               CU_MSG(base_ptr->gu_ptr->log_id,
                      DBG_MED_PRIO,
                      "Cannot Propagate to peer port of (mod-inst-id, port-id) (0x%lX, 0x%lx) port_state =%u, "
                      "resetting the flags to try later",
                      gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                      old_state);
            }
         }
      }
   }

   // we don't want graph command to fail because of propagation failures.
   return AR_EOK;
}

ar_result_t cu_peer_cntr_ds_propagation_init(cu_base_t *base_ptr, cu_ext_out_port_t *cu_ext_out_port_ptr)
{
   cu_ext_out_port_ptr->prop_info.prop_enabled               = TRUE;
   cu_ext_out_port_ptr->prop_info.prop_ds_prop_to_us_fn      = NULL;
   cu_ext_out_port_ptr->prop_info.prop_us_prop_to_ds_fn      = cu_propagate_to_ds_peer_ext_port;
   cu_ext_out_port_ptr->prop_info.prop_us_state_ack_to_ds_fn = cu_send_us_state_to_ds;

   return AR_EOK;
}

ar_result_t cu_peer_cntr_us_propagation_init(cu_base_t *base_ptr, cu_ext_in_port_t *cu_ext_in_port_ptr)
{
   cu_ext_in_port_ptr->prop_info.prop_enabled               = TRUE;
   cu_ext_in_port_ptr->prop_info.prop_ds_prop_to_us_fn      = cu_propagate_to_us_peer_ext_port;
   cu_ext_in_port_ptr->prop_info.prop_us_prop_to_ds_fn      = NULL;
   cu_ext_in_port_ptr->prop_info.prop_us_state_ack_to_ds_fn = cu_send_us_state_to_ds;
   return AR_EOK;
}

bool_t cu_has_upstream_frame_len_changed(cu_ext_in_port_upstream_frame_length_t *a1,
                                         cu_ext_in_port_upstream_frame_length_t *b1,
                                         topo_media_fmt_t                       *media_fmt_ptr)
{
   if (SPF_IS_PACKETIZED_OR_PCM(media_fmt_ptr->data_format))
   {
      if ((a1->frame_len_samples != b1->frame_len_samples) || (a1->frame_len_us != b1->frame_len_us) ||
          (a1->sample_rate != b1->sample_rate))
      {
         return TRUE;
      }
   }
   else if ((SPF_RAW_COMPRESSED == media_fmt_ptr->data_format) ||
            (SPF_DEINTERLEAVED_RAW_COMPRESSED == media_fmt_ptr->data_format))
   {
      if ((a1->frame_len_bytes != b1->frame_len_bytes) || (a1->frame_len_samples != b1->frame_len_samples) ||
          (a1->frame_len_us != b1->frame_len_us) || (a1->sample_rate != b1->sample_rate))
      {
         return TRUE;
      }
   }
   return FALSE;
}

static bool_t cu_is_ext_out_port_us_or_ds_rt(cu_base_t *base_ptr, gu_ext_out_port_t *ext_out_port_ptr)
{
   uint32_t is_downstream_realtime = FALSE;
   uint32_t is_upstream_realtime   = FALSE;
   base_ptr->topo_vtbl_ptr->get_port_property(base_ptr->topo_ptr,
                                              TOPO_DATA_OUTPUT_PORT_TYPE,
                                              PORT_PROPERTY_IS_UPSTREAM_RT,
                                              (void *)ext_out_port_ptr->int_out_port_ptr,
                                              &is_upstream_realtime);
   base_ptr->topo_vtbl_ptr->get_port_property(base_ptr->topo_ptr,
                                              TOPO_DATA_OUTPUT_PORT_TYPE,
                                              PORT_PROPERTY_IS_DOWNSTREAM_RT,
                                              (void *)ext_out_port_ptr->int_out_port_ptr,
                                              &is_upstream_realtime);

   return (is_downstream_realtime || is_upstream_realtime);
}

static bool_t cu_is_ext_in_port_us_or_ds_rt(cu_base_t *base_ptr, gu_ext_in_port_t *ext_in_port_ptr)
{
   uint32_t is_downstream_realtime = FALSE;
   uint32_t is_upstream_realtime   = FALSE;

   base_ptr->topo_vtbl_ptr->get_port_property(base_ptr->topo_ptr,
                                              TOPO_DATA_INPUT_PORT_TYPE,
                                              PORT_PROPERTY_IS_UPSTREAM_RT,
                                              (void *)ext_in_port_ptr->int_in_port_ptr,
                                              &is_upstream_realtime);
   base_ptr->topo_vtbl_ptr->get_port_property(base_ptr->topo_ptr,
                                              TOPO_DATA_INPUT_PORT_TYPE,
                                              PORT_PROPERTY_IS_DOWNSTREAM_RT,
                                              (void *)ext_in_port_ptr->int_in_port_ptr,
                                              &is_upstream_realtime);

   return (is_downstream_realtime || is_upstream_realtime);
}

/**
 * cu is real time if
 * any of its external ports is connected to an RT entity.
 */
bool_t cu_is_realtime(cu_base_t *base_ptr)
{
   base_ptr->flags.is_real_time = FALSE;
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = base_ptr->gu_ptr->ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      if (cu_is_ext_out_port_us_or_ds_rt(base_ptr, ext_out_port_list_ptr->ext_out_port_ptr))
      {
         base_ptr->flags.is_real_time = TRUE;
         return TRUE;
      }
   }
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = base_ptr->gu_ptr->ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      if (cu_is_ext_in_port_us_or_ds_rt(base_ptr, ext_in_port_list_ptr->ext_in_port_ptr))
      {
         base_ptr->flags.is_real_time = TRUE;
         return TRUE;
      }
   }
   return FALSE;
}
