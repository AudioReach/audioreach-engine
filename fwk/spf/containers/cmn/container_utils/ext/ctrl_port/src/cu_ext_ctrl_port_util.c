/**
 * \file cu_ext_ctrl_port_util.c
 * \brief
 *     This file contains container utility functions for external control port handling (input and output).
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "cu_i.h"
#include "cu_ctrl_port_i.h"
#include "capi_intf_extn_imcl.h"
/* =======================================================================
Static Function Definitions
========================================================================== */

/* ==========================================================================  */

/*
 * Create an external control port's intent queue. Determines the number of elements,
 * bit mask, and name, and calls a common function to allocate the queue.
 */
static ar_result_t cu_create_ext_ctrl_port_queues(cu_base_t *cu_ptr, gu_ext_ctrl_port_t *gu_ext_port_ptr, uint32_t offset)
{
   ar_result_t        result = AR_EOK;
   cu_queue_handler_t q_handler;
   uint32_t           bit_mask = 0;

   POSAL_HEAP_ID operating_heap_id = gu_ext_port_ptr->int_ctrl_port_ptr->operating_heap_id;

   char incoming_intent_q_name[POSAL_DEFAULT_NAME_LEN]; // data queue name
   snprintf(incoming_intent_q_name, POSAL_DEFAULT_NAME_LEN, "%s%s%8lX", "IN_I", "GEN_CNTR", cu_ptr->gu_ptr->log_id);

   uint32_t max_num_elements = CU_MAX_INTENT_Q_ELEMENTS;

   bit_mask = cu_request_bit_in_bit_mask(&cu_ptr->available_ctrl_chan_mask);
   if (0 == bit_mask)
   {
      CU_MSG(cu_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Bit mask has no bits available 0x%lx",
             cu_ptr->available_bit_mask);
      return result;
   }
   q_handler = NULL;

#ifdef VERBOSE_DEBUGGING
   CU_MSG(cu_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "number of max incoming intent elements is determined to be %lu, bit_mask: 0x%lx",
          max_num_elements,
          bit_mask);
#endif

   // Update the current control channel mask.
   result = cu_init_queue(cu_ptr,
                            incoming_intent_q_name,
                            max_num_elements,
                            bit_mask,
                            q_handler,
                            cu_ptr->ctrl_channel_ptr,
                            &gu_ext_port_ptr->this_handle.q_ptr,
                            CU_PTR_PUT_OFFSET(gu_ext_port_ptr, offset),
                            operating_heap_id);

   // if operating heap for this control port is island then update the island channel mask.
   if (POSAL_IS_ISLAND_HEAP_ID(operating_heap_id))
   {
      cu_ptr->island_ctrl_chan_mask |= bit_mask;
   }

   return result;
}

static ar_result_t cu_flush_incoming_ctrl_msg_queue(cu_base_t *cu_ptr, gu_ext_ctrl_port_t *gu_ext_ctrl_port_ptr)
{
   if (NULL == gu_ext_ctrl_port_ptr->this_handle.q_ptr)
   {
      return AR_EOK;
   }

   // poll and process queued ctrl message before flushing the port
   cu_ext_ctrl_port_poll_and_process_ctrl_msgs(cu_ptr, gu_ext_ctrl_port_ptr);

   // If there are still any pending msgs that couldn't be polled & set to the module, Return the incoming control
   // messages to peer
   spf_svc_drain_ctrl_msg_queue(gu_ext_ctrl_port_ptr->this_handle.q_ptr);

   return AR_EOK;
}

static void cu_deinit_ext_ctrl_port_queues(cu_base_t *cu_ptr, gu_ext_ctrl_port_t *gu_ext_ctrl_port_ptr)
{
   if (gu_ext_ctrl_port_ptr->this_handle.q_ptr)
   {
      /*Release the available control channel mask mask */
      cu_ptr->available_ctrl_chan_mask |= posal_queue_get_channel_bit(gu_ext_ctrl_port_ptr->this_handle.q_ptr);

      // clear the bit from island channel mask
      cu_ptr->island_ctrl_chan_mask &= (~cu_ptr->available_ctrl_chan_mask);

      /*deinit the queue */
      posal_queue_deinit(gu_ext_ctrl_port_ptr->this_handle.q_ptr);
      gu_ext_ctrl_port_ptr->this_handle.q_ptr = NULL;
   }
}

ar_result_t cu_ctrl_port_process_inter_proc_outgoing_buf(cu_base_t *               me_ptr,
                                                         gu_ext_ctrl_port_t *      gu_ext_port_ptr,
                                                         capi_buf_t *              buf_ptr,
                                                         imcl_outgoing_data_flag_t flags,
                                                         uint32_t                  src_miid,
                                                         uint32_t                  peer_port_id)
{
   ar_result_t result = AR_EOK;

   uint32_t host_domain_id;
   __gpr_cmd_get_host_domain_id(&host_domain_id);
   gpr_packet_t *to_send_pkt_ptr = NULL;

   // Get spf_msg_header_t* from the intent buf pointer.
   spf_msg_header_t *header_ptr = (spf_msg_header_t *)((int8_t *)buf_ptr->data_ptr - INTENT_BUFFER_OFFSET);

   // Reinterpret the node itself as a spf_msg_t.
   spf_msg_t msg;
   msg.payload_ptr = header_ptr;

   // Reinterpret as a GPR message
   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = host_domain_id;
   args.src_port      = src_miid;
   args.dst_domain_id = gu_ext_port_ptr->peer_domain_id;
   args.dst_port      = gu_ext_port_ptr->peer_handle.module_instance_id;
   args.client_data   = 0;
   args.token         = 0; // don't care - No ack expected
   args.opcode        = flags.is_trigger ? IMCL_INTER_PROC_TRIGGER_MSG_GPR : IMCL_INTER_PROC_POLLING_MSG_GPR;
   args.payload_size  = buf_ptr->actual_data_len + sizeof(intf_extn_param_id_imcl_incoming_data_t);
   args.ret_packet    = &to_send_pkt_ptr;

   result = __gpr_cmd_alloc_ext(&args);
   if (AR_DID_FAIL(result) || NULL == to_send_pkt_ptr)
   {
      CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "IMCL: MDF: Allocating new GPR MSG pkt failed with %lu", result);
      // Need to be returned to the buffer queue.
      spf_msg_return_msg(&msg);
      return result;
   }
   /* prepare the cmd payload */
   uint8_t *payload_ptr = GPR_PKT_GET_PAYLOAD(uint8_t, to_send_pkt_ptr);
   memscpy(payload_ptr + sizeof(intf_extn_param_id_imcl_incoming_data_t),
           args.payload_size,
           buf_ptr->data_ptr,
           buf_ptr->actual_data_len);

   // populate the peer port ID
   intf_extn_param_id_imcl_incoming_data_t *payload_hdr_ptr = (intf_extn_param_id_imcl_incoming_data_t *)payload_ptr;
   payload_hdr_ptr->port_id                                 = peer_port_id;

   if (AR_EOK != (result = __gpr_cmd_async_send(to_send_pkt_ptr)))
   {

      CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "IMCL: MDF: sending msg to IMCL peer failed with %lu", result);
      // Need to be returned to the buffer queue.
      // spf_msg_return_msg(&msg);
      __gpr_cmd_free(to_send_pkt_ptr);
   }

   // Need to be returned to the buffer queue immediately in inter proc case
   spf_msg_return_msg(&msg);
   return result;
}

/*
 * Initialize an external control port.
 */
ar_result_t cu_init_ext_ctrl_port(cu_base_t *me_ptr, gu_ext_ctrl_port_t *gu_ext_ctrl_port_ptr, uint32_t ctrl_port_queue_offset)
{
   ar_result_t result = AR_EOK;

   // ext_in_port_ptr->configured_media_fmt;
   gu_ext_ctrl_port_ptr->this_handle.cmd_handle_ptr = &me_ptr->cmd_handle;

   result = cu_create_ext_ctrl_port_queues(me_ptr, gu_ext_ctrl_port_ptr, ctrl_port_queue_offset);

   if (AR_DID_FAIL(result))
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Ext control port Q create failed for miid 0x%x, portID 0x%x",
             gu_ext_ctrl_port_ptr->int_ctrl_port_ptr->module_ptr->module_instance_id,
             gu_ext_ctrl_port_ptr->int_ctrl_port_ptr->id);
      return result;
   }

   return result;
}

/*
 * De-init the external control port. The following
 *    1. Flush all the incoming control messages and return to the source.
 *    2. Destroy outgoing intent buffers.
 *    3. Destroy outgoing and incoming queues.
 */
ar_result_t cu_deinit_ext_ctrl_port(cu_base_t *me_ptr, gu_ext_ctrl_port_t *gu_ext_ctrl_port_ptr)
{
   ar_result_t result = AR_EOK;

   // Return incoming intent messages
   if (gu_ext_ctrl_port_ptr->this_handle.q_ptr)
   {
      cu_flush_incoming_ctrl_msg_queue(me_ptr, gu_ext_ctrl_port_ptr);

      // Destroy the incoming intent queue.
      cu_deinit_ext_ctrl_port_queues(me_ptr, gu_ext_ctrl_port_ptr);
   }

   // Destroy the output buffer Q
   cu_deinit_ctrl_port_buf_q(me_ptr,gu_ext_ctrl_port_ptr->int_ctrl_port_ptr);

   // invalidate the link
   gu_deinit_ext_ctrl_port(gu_ext_ctrl_port_ptr);
   return result;
}

static ar_result_t cu_send_inter_proc_peer_state_update(cu_base_t *         me_ptr,
                                                        gu_ext_ctrl_port_t *gu_ext_ctrl_port_ptr,
                                                        uint32_t            dst_domain_id,
                                                        uint32_t            sg_ops)
{
   ar_result_t result = AR_EOK;
   uint32_t    host_domain_id;
   __gpr_cmd_get_host_domain_id(&host_domain_id);

   imcl_inter_proc_peer_state_update_t to_send;
   to_send.ctrl_port_id = gu_ext_ctrl_port_ptr->peer_handle.port_id;
   to_send.sg_ops       = sg_ops;

   gpr_cmd_alloc_send_t args;
   args.src_domain_id = host_domain_id;
   args.src_port      = gu_ext_ctrl_port_ptr->int_ctrl_port_ptr->module_ptr->module_instance_id;
   args.dst_domain_id = dst_domain_id;
   args.dst_port      = gu_ext_ctrl_port_ptr->peer_handle.module_instance_id;
   args.client_data   = 0;
   args.token         = 0; // don't care
   args.opcode        = IMCL_INTER_PROC_PEER_STATE_UPDATE;
   args.payload_size  = sizeof(to_send);
   args.payload       = &to_send;

   result = __gpr_cmd_alloc_send(&args);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "MDF: IMCL: Sending inter-proc ctrl port state update failed with %lu; src miid = 0x%lx, src_domain_id = "
             "%lu dst miid = 0x%lx, dst_domain_id = %lu, sg_ops %lu",
             result,
             args.src_port,
             args.src_domain_id,
             args.dst_port,
             args.dst_domain_id,
             sg_ops);
   }

   AR_MSG(DBG_HIGH_PRIO,
          "MDF: IMCL: Sent inter-proc ctrl port state update; src miid = 0x%lx, src_domain_id = "
          "%lu dst miid = 0x%lx, dst_domain_id = %lu, sg_ops %lu",
          args.src_port,
          args.src_domain_id,
          args.dst_port,
          args.dst_domain_id,
          sg_ops);

   return result;
}

ar_result_t cu_operate_on_ext_ctrl_port(cu_base_t *          me_ptr,
                                        uint32_t             sg_ops,
                                        gu_ext_ctrl_port_t **gu_ext_ctrl_port_ptr_ptr,
                                        bool_t               is_self_sg)
{
   ar_result_t         result               = AR_EOK;
   gu_ext_ctrl_port_t *gu_ext_ctrl_port_ptr = *gu_ext_ctrl_port_ptr_ptr;

   if (!is_self_sg)
   {
      // Nothing to do for START and PREPARE on external control port (if not from self SG)
      if ((TOPO_SG_OP_START | TOPO_SG_OP_PREPARE) & sg_ops)
      {
         return AR_EOK;
      }
   }

   bool_t is_disconnect_needed =
      ((TOPO_SG_OP_DISCONNECT & sg_ops) && (cu_is_disconnect_ext_ctrl_port_needed(me_ptr, gu_ext_ctrl_port_ptr)));

   if ((TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops)
   {
      // Stop polling the control port queue.
      me_ptr->curr_ext_ctrl_chan_mask &= ~(posal_queue_get_channel_bit(gu_ext_ctrl_port_ptr->this_handle.q_ptr));
   }

   if (((TOPO_SG_OP_STOP | TOPO_SG_OP_CLOSE) & sg_ops) || (is_disconnect_needed))
   {
      // FLush incoming ctrl msg queue.
      cu_flush_incoming_ctrl_msg_queue(me_ptr, *gu_ext_ctrl_port_ptr_ptr);
   }

   if (is_disconnect_needed)
   {
      gu_ext_ctrl_port_ptr->peer_handle.spf_handle_ptr = NULL;
      if (gu_is_domain_id_remote(gu_ext_ctrl_port_ptr->peer_domain_id))
      {
         gu_ext_ctrl_port_ptr->peer_domain_id = 0; // disconnect
      }
   }

   if (is_self_sg)
   {
      if (TOPO_SG_OP_PREPARE & sg_ops)
      {
         result = cu_check_and_recreate_ctrl_port_buffers(me_ptr, gu_ext_ctrl_port_ptr->int_ctrl_port_ptr);
      }
      if (0 != gu_ext_ctrl_port_ptr->peer_domain_id && (gu_is_domain_id_remote(gu_ext_ctrl_port_ptr->peer_domain_id)))
      {
         cu_send_inter_proc_peer_state_update(me_ptr,
                                              gu_ext_ctrl_port_ptr,
                                              gu_ext_ctrl_port_ptr->peer_domain_id,
                                              sg_ops);
      }
   }
   else //(!is_self_sg)
   {
      if (TOPO_SG_OP_CLOSE & sg_ops)
      {
         me_ptr->topo_vtbl_ptr->ctrl_port_operation(gu_ext_ctrl_port_ptr->int_ctrl_port_ptr,
                                                    INTF_EXTN_IMCL_PORT_CLOSE,
                                                    me_ptr->heap_id);
      }
   }

   if (TOPO_SG_OP_CLOSE == sg_ops)
   {
      gu_ext_ctrl_port_ptr->gu_status = GU_STATUS_CLOSING;
   }

   return result;
}

ar_result_t cu_set_downgraded_state_on_ctrl_port(cu_base_t *       me_ptr,
                                                 gu_ctrl_port_t *  ctrl_port_ptr,
                                                 topo_port_state_t downgraded_state)
{
   ar_result_t result = AR_EOK;

   if (ctrl_port_ptr->ext_ctrl_port_ptr)
   {
      if ((TOPO_PORT_STATE_STARTED)&downgraded_state)
      {
         // Start polling the control port queue.
         me_ptr->curr_ext_ctrl_chan_mask |=
            posal_queue_get_channel_bit(ctrl_port_ptr->ext_ctrl_port_ptr->this_handle.q_ptr);
      }
   }

   // Apply Start/stop/suspend on the module only when the down graded state is updated.
   if ((TOPO_PORT_STATE_STARTED == downgraded_state) || (TOPO_PORT_STATE_STOPPED == downgraded_state) ||
       (TOPO_PORT_STATE_SUSPENDED == downgraded_state))
   {
      me_ptr->topo_vtbl_ptr->set_port_property(me_ptr->topo_ptr,
                                               TOPO_CONTROL_PORT_TYPE,
                                               PORT_PROPERTY_TOPO_STATE,
                                               ctrl_port_ptr,
                                               (uint32_t)downgraded_state);
   }

   return result;
}

ar_result_t cu_connect_ext_ctrl_port(cu_base_t *me_ptr, gu_ext_ctrl_port_t *gu_ext_ctrl_port_ptr)
{
   ar_result_t result = AR_EOK;

   // Create ctrl port recurring buffers if the module has requested for recurring buffers
   // during module init itself. No need to check error.
   cu_check_and_recreate_ctrl_port_buffers(me_ptr, gu_ext_ctrl_port_ptr->int_ctrl_port_ptr);

   return result;
}

ar_result_t cu_handle_inter_proc_triggered_imcl(cu_base_t *me_ptr, gpr_packet_t *packet_ptr)
{

   CU_MSG(me_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "IMCL: MDF: CTRL_PORT_TRIGGER: Executing control port trigger msg. current channel mask=0x%x",
          me_ptr->curr_chan_mask);

   ar_result_t result        = AR_EOK;
   void *      payload_ptr   = GPR_PKT_GET_PAYLOAD(void, packet_ptr);
   uint32_t    payload_size  = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);
   uint32_t    dst_miid      = packet_ptr->dst_port;
   uint32_t    src_domain_id = packet_ptr->src_domain_id;

   // AKR: to get the port ID, we need to open the imcl message right here.
   intf_extn_param_id_imcl_incoming_data_t *intent_hdr_ptr = (intf_extn_param_id_imcl_incoming_data_t *)payload_ptr;

   gu_ext_ctrl_port_t *ext_gu_ctrl_port_ptr =
      gu_get_ext_ctrl_port_for_inter_proc_imcl(me_ptr->gu_ptr, dst_miid, src_domain_id, intent_hdr_ptr->port_id);

   if (NULL == ext_gu_ctrl_port_ptr)
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "IMCL: MDF: CTRL_PORT_TRIGGER: Couldn't get gu ctrl port for peer miid 0x%lx",
             dst_miid);
      return AR_EFAILED;
   }

   // Check if the port state is started and then only do the set param. Else return.
   topo_port_state_t ctrl_port_state;
   me_ptr->topo_vtbl_ptr->get_port_property(me_ptr->topo_ptr,
                                            TOPO_CONTROL_PORT_TYPE,
                                            PORT_PROPERTY_TOPO_STATE,
                                            (void *)ext_gu_ctrl_port_ptr->int_ctrl_port_ptr,
                                            (uint32_t *)&ctrl_port_state);

   // Send the incoming intent to the capi module.
   result = me_ptr->topo_island_vtbl_ptr->handle_incoming_ctrl_intent(ext_gu_ctrl_port_ptr->int_ctrl_port_ptr,
                                                                     payload_ptr,
                                                                     payload_size,
                                                                     payload_size);
   if (AR_DID_FAIL(result))
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "IMCL: MDF: CMD:CTRL_PORT_TRIGGER: Handling incoming control message "
             "failed.");
   }
   else
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "IMCL: MDF: CMD:CTRL_PORT_TRIGGER: mid: 0x%x ctrl port id 0x%lx Set incoming ctrl message on done.",
             ext_gu_ctrl_port_ptr->int_ctrl_port_ptr->module_ptr->module_instance_id,
             ext_gu_ctrl_port_ptr->int_ctrl_port_ptr->id);
   }
   return result;
}

