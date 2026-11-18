/**
 * \file cu_ipc_fwk_ext.c
 * \brief
 *     This file contains container utility functions for IPC Rx and Tx module.
 *
 *
 * \copyright
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "cu_i.h"
#include "rd_sh_mem_ep_api.h"
#include "wr_sh_mem_ep_api.h"
#include "ipc_module_cmn_api.h"

/* =======================================================================
Public Function Definitions
========================================================================== */

static ar_result_t cu_ipc_send_ipc_tx_port_stopped_ack_ipc_rx(cu_base_t         *base_ptr,
                                                              gu_ext_out_port_t *gu_ext_out_port_ptr,
                                                              topo_port_state_t  ds_state);

static ar_result_t cu_propagate_to_us_from_ipc_rx_ext_in_port(cu_base_t                           *base_ptr,
                                                              gu_ext_in_port_t                    *ext_in_port_ptr,
                                                              spf_msg_peer_port_property_update_t *prop_ptr);

static ar_result_t cu_propagate_to_ds_from_ipc_tx_ext_out_port(cu_base_t                           *base_ptr,
                                                               gu_ext_out_port_t                   *ext_out_port_ptr,
                                                               spf_msg_peer_port_property_update_t *prop_ptr);

///// IPC module utils
static void cu_ipc_tx_port_propagation_init(cu_base_t *base_ptr, cu_ext_out_port_t *cu_ext_out_port_ptr)
{
   cu_ext_out_port_ptr->prop_info.prop_enabled               = TRUE;
   cu_ext_out_port_ptr->prop_info.prop_ds_prop_to_us_fn      = NULL;
   cu_ext_out_port_ptr->prop_info.prop_us_prop_to_ds_fn      = cu_propagate_to_ds_from_ipc_tx_ext_out_port;
   cu_ext_out_port_ptr->prop_info.prop_us_state_ack_to_ds_fn = cu_ipc_send_ipc_tx_port_stopped_ack_ipc_rx;

   return;
}

static void cu_ipc_rx_port_propagation_init(cu_base_t *base_ptr, cu_ext_in_port_t *cu_ext_in_port_ptr)
{
   cu_ext_in_port_ptr->prop_info.prop_enabled               = TRUE;
   cu_ext_in_port_ptr->prop_info.prop_ds_prop_to_us_fn      = cu_propagate_to_us_from_ipc_rx_ext_in_port;
   cu_ext_in_port_ptr->prop_info.prop_us_prop_to_ds_fn      = NULL;
   cu_ext_in_port_ptr->prop_info.prop_us_state_ack_to_ds_fn = NULL;

   return;
}

/*
 * Init an external output port's data queue. Determines the number of elements,
 * bit mask, and name, and calls a common function to allocate the queue.
 */
static ar_result_t cu_ipc_tx_init_ext_out_queue(cu_base_t         *base_ptr,
                                                gu_ext_out_port_t *gu_ext_port_ptr,
                                                cu_ext_out_port_t *cu_ext_port_ptr,
                                                uint32_t           ext_out_queue_offset)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   cu_queue_handler_t q_handler;
   uint32_t           bit_mask = 0;
   char               data_q_name[POSAL_DEFAULT_NAME_LEN]; // data queue name
   snprintf(data_q_name, POSAL_DEFAULT_NAME_LEN, "%s%s%8lX", "B", "IPC_TX", base_ptr->gu_ptr->log_id);

   uint32_t max_num_elements = CU_MAX_OUT_BUF_Q_ELEMENTS;

   // for buffer driven mode we add the data queues to the container channel and mask
   bit_mask  = cu_request_bit_in_bit_mask(&base_ptr->available_bit_mask);
   q_handler = base_ptr->cntr_vtbl_ptr->ipc_port_ext_out_data_trigger_handler;

   if (0 == bit_mask)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Bit mask has no bits available 0x%lx",
             base_ptr->available_bit_mask);
      return result;
   }

   cu_ext_port_ptr->bit_mask = bit_mask;
   base_ptr->all_ext_out_mask |= bit_mask;

   void *q_mem_ptr = CU_PTR_PUT_OFFSET(gu_ext_port_ptr, ext_out_queue_offset);

   TRY(result,
       cu_init_queue(base_ptr,
                     data_q_name,
                     max_num_elements,
                     bit_mask,
                     q_handler,
                     base_ptr->channel_ptr,
                     &gu_ext_port_ptr->this_handle.q_ptr,
                     q_mem_ptr,
                     gu_get_downgraded_heap_id(base_ptr->heap_id, gu_ext_port_ptr->downstream_handle.heap_id)));

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Created the external output port 0x%lx data queue bit_mask: 0x%lx q_ptr: 0x%lx",
          gu_ext_port_ptr->int_out_port_ptr->cmn.id,
          bit_mask,
          gu_ext_port_ptr->this_handle.q_ptr);
#endif

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

/*
 * Init an external input port's data queue. Determines the number of elements,
 * bit mask, and name, and calls a common function to allocate the queue.
 */
static ar_result_t cu_ipc_rx_init_ext_in_queue(cu_base_t        *base_ptr,
                                               gu_ext_in_port_t *gu_ext_port_ptr,
                                               cu_ext_in_port_t *cu_ext_port_ptr,
                                               uint32_t          ext_in_queue_offset)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   cu_queue_handler_t q_handler;
   uint32_t           bit_mask = 0;

   char data_q_name[POSAL_DEFAULT_NAME_LEN]; // data queue name
   snprintf(data_q_name, POSAL_DEFAULT_NAME_LEN, "%s%s%8lX", "D", "IPC_RX", base_ptr->gu_ptr->log_id);

   uint32_t max_num_elements = CU_MAX_INP_DATA_Q_ELEMENTS;

   // for buffer driven mode we add the data queues to the container channel and mask
   bit_mask  = cu_request_bit_in_bit_mask(&base_ptr->available_bit_mask);
   q_handler = base_ptr->cntr_vtbl_ptr->ipc_port_ext_in_data_trigger_handler;

   if (0 == bit_mask)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Bit mask has no bits available 0x%lx",
             base_ptr->available_bit_mask);
      return result;
   }

   cu_ext_port_ptr->bit_mask = bit_mask;
   base_ptr->all_ext_in_mask |= bit_mask;

   void *q_mem_ptr = CU_PTR_PUT_OFFSET(gu_ext_port_ptr, ext_in_queue_offset);

   TRY(result,
       cu_init_queue(base_ptr,
                     data_q_name,
                     max_num_elements,
                     bit_mask,
                     q_handler,
                     base_ptr->channel_ptr,
                     &gu_ext_port_ptr->this_handle.q_ptr,
                     q_mem_ptr,
                     gu_get_downgraded_heap_id(base_ptr->heap_id, gu_ext_port_ptr->upstream_handle.heap_id)));

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Created the external input port 0x%lx data queue bit_mask: 0x%lx q_ptr: 0x%lx",
          gu_ext_port_ptr->int_in_port_ptr->cmn.id,
          bit_mask,
          gu_ext_port_ptr->this_handle.q_ptr);
#endif

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t cu_ipc_tx_init_ext_out_port(cu_base_t         *base_ptr,
                                        gu_ext_out_port_t *gu_ext_port_ptr,
                                        uint32_t           ext_out_queue_offset)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   cu_ext_out_port_t *cu_ext_port_ptr =
      (cu_ext_out_port_t *)((uint8_t *)gu_ext_port_ptr + base_ptr->ext_out_port_cu_offset);

   cu_module_t *cu_module_ptr =
      (cu_module_t *)((uint8_t *)gu_ext_port_ptr->int_out_port_ptr->cmn.module_ptr + base_ptr->module_cu_offset);

   gu_module_t *module_ptr = gu_ext_port_ptr->int_out_port_ptr->cmn.module_ptr;

   gu_ext_port_ptr->this_handle.cmd_handle_ptr = &base_ptr->cmd_handle;

   TRY(result,
       __gpr_cmd_register(module_ptr->module_instance_id, cu_ipc_module_gpr_callback, &gu_ext_port_ptr->this_handle));

   cu_ipc_tx_port_propagation_init(base_ptr, cu_ext_port_ptr);

   TRY(result, cu_ipc_tx_init_ext_out_queue(base_ptr, gu_ext_port_ptr, cu_ext_port_ptr, ext_out_queue_offset));

   cu_ext_port_ptr->propagated_port_state = TOPO_PORT_STATE_INVALID;

   if (cu_module_ptr->ipc_port_cb_info_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Unexpected: Found previously cached callback info for module 0x%lX ",
             module_ptr->module_instance_id);

      return AR_EFAILED;
   }

   cu_module_ptr->ipc_port_cb_info_ptr =
      (cu_fwk_extn_ipc_port_info_t *)posal_memory_malloc(sizeof(cu_fwk_extn_ipc_port_info_t), base_ptr->heap_id);

   if (NULL == cu_module_ptr->ipc_port_cb_info_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "Failed to allocate memory for IPC port callback info");
      THROW(result, AR_ENOMEMORY);
   }
   memset(cu_module_ptr->ipc_port_cb_info_ptr, 0, sizeof(cu_fwk_extn_ipc_port_info_t));

   // query module and get the external output data/cmd gpr handlers.
   TRY(result,
       base_ptr->cntr_vtbl_ptr
          ->get_and_update_fwk_extn_ipc_port_msg_cb_info(base_ptr,
                                                         module_ptr,
                                                         TRUE,
                                                         gu_ext_port_ptr->int_out_port_ptr->cmn.index));

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   return AR_EOK;
}

ar_result_t cu_destroy_ipc_ext_out_port_buffers(cu_base_t *base_ptr, gu_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   // poll and send buffer to module for destroy
   uint32_t num_polled_buffers = 0;
   while (posal_queue_poll(ext_out_port_ptr->this_handle.q_ptr))
   {
      /** Pops buffer from the queue and sets on the IPC module. */
      result = cu_ipc_port_ext_out_on_data_trigger(base_ptr, ext_out_port_ptr, FWK_EXT_IPC_PORT_FLAG_DESTROY_BUFS);

      if (AR_DID_FAIL(result))
      {
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "Flushing & destory failed for ext output port 0x%lx of Module 0x%lX ",
                ext_out_port_ptr->int_out_port_ptr->cmn.id,
                ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id);
         break;
      }
      num_polled_buffers++;
   }

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_LOW_PRIO,
          "Ext output port 0x%lx of Module 0x%lX. num_polled_buffers %lu and destoryed ",
          ext_out_port_ptr->int_out_port_ptr->cmn.id,
          ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
          num_polled_buffers);
#endif
   return result;
}

void cu_deinit_ipc_tx_ext_out_port(cu_base_t         *base_ptr,
                                   gu_ext_out_port_t *ext_out_port_ptr,
                                   bool_t             b_ignore_ports_from_sg_close,
                                   bool_t             force_deinit_all_ports)
{
   ar_result_t result = AR_EOK;
   SPF_MANAGE_CRITICAL_SECTION
   bool_t b_deinit = force_deinit_all_ports;

   cu_ext_out_port_t *cu_ext_port_ptr =
      (cu_ext_out_port_t *)((uint8_t *)ext_out_port_ptr + base_ptr->ext_out_port_cu_offset);

   // port already deinited, skip
   if (!ext_out_port_ptr->int_out_port_ptr)
   {
      return;
   }

   cu_module_t *cu_module_ptr =
      (cu_module_t *)((uint8_t *)ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr + base_ptr->module_cu_offset);

   if (!b_deinit &&
       (GU_STATUS_CLOSING == ext_out_port_ptr->gu_status || GU_STATUS_CLOSING == ext_out_port_ptr->sg_ptr->gu_status))
   {
      // If external port/SG is marked for the closing then deinit it.
      b_deinit = TRUE;

      // If subgraph is marked for the closing then deinit can be done later when subgraph is being destroyed.
      if (b_ignore_ports_from_sg_close && GU_STATUS_CLOSING == ext_out_port_ptr->sg_ptr->gu_status)
      {
         b_deinit = FALSE;
      }
   }

   if (FALSE == b_deinit)
   {
      // nothing to do
      return;
   }

   SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);

   posal_memory_free(cu_module_ptr->ipc_port_cb_info_ptr);

   cu_deinit_ext_port_queue(base_ptr, &ext_out_port_ptr->this_handle, cu_ext_port_ptr->bit_mask);

   gu_deinit_ext_out_port(ext_out_port_ptr);

   SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);
}

ar_result_t cu_ipc_rx_init_ext_in_port(cu_base_t        *base_ptr,
                                       gu_ext_in_port_t *gu_ext_port_ptr,
                                       uint32_t          ext_in_queue_offset)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   cu_ext_in_port_t *cu_ext_port_ptr =
      (cu_ext_in_port_t *)((uint8_t *)gu_ext_port_ptr + base_ptr->ext_in_port_cu_offset);

   cu_module_t *cu_module_ptr =
      (cu_module_t *)((uint8_t *)gu_ext_port_ptr->int_in_port_ptr->cmn.module_ptr + base_ptr->module_cu_offset);

   gu_module_t *module_ptr = gu_ext_port_ptr->int_in_port_ptr->cmn.module_ptr;

   gu_ext_port_ptr->this_handle.cmd_handle_ptr = &base_ptr->cmd_handle;

   TRY(result,
       __gpr_cmd_register(module_ptr->module_instance_id, cu_ipc_module_gpr_callback, &gu_ext_port_ptr->this_handle));

   cu_ipc_rx_port_propagation_init(base_ptr, cu_ext_port_ptr);

   TRY(result, cu_ipc_rx_init_ext_in_queue(base_ptr, gu_ext_port_ptr, cu_ext_port_ptr, ext_in_queue_offset));

   if (cu_module_ptr->ipc_port_cb_info_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Unexpected: Found previously cached callback info for module 0x%lX ",
             gu_ext_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id);

      return AR_EFAILED;
   }

   cu_module_ptr->ipc_port_cb_info_ptr =
      (cu_fwk_extn_ipc_port_info_t *)posal_memory_malloc(sizeof(cu_fwk_extn_ipc_port_info_t), base_ptr->heap_id);

   if (NULL == cu_module_ptr->ipc_port_cb_info_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "Failed to allocate memory for IPC port callback info");
      THROW(result, AR_ENOMEMORY);
   }
   memset(cu_module_ptr->ipc_port_cb_info_ptr, 0, sizeof(cu_fwk_extn_ipc_port_info_t));

   // query module and get the external input data/cmd gpr handlers.
   TRY(result,
       base_ptr->cntr_vtbl_ptr
          ->get_and_update_fwk_extn_ipc_port_msg_cb_info(base_ptr,
                                                         gu_ext_port_ptr->int_in_port_ptr->cmn.module_ptr,
                                                         TRUE,
                                                         gu_ext_port_ptr->int_in_port_ptr->cmn.index));

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   return AR_EOK;
}

void cu_deinit_ipc_rx_ext_in_port(cu_base_t        *base_ptr,
                                  gu_ext_in_port_t *ext_in_port_ptr,
                                  bool_t            b_ignore_ports_from_sg_close,
                                  bool_t            force_deinit_all_ports)
{
   ar_result_t result = AR_EOK;
   SPF_MANAGE_CRITICAL_SECTION
   bool_t b_deinit = force_deinit_all_ports;

   cu_ext_in_port_t *cu_ext_port_ptr =
      (cu_ext_in_port_t *)((uint8_t *)ext_in_port_ptr + base_ptr->ext_in_port_cu_offset);

   // port already deinited, skip
   if (!ext_in_port_ptr->int_in_port_ptr)
   {
      return;
   }

   gu_module_t *module_ptr = ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr;

   cu_module_t *cu_module_ptr = (cu_module_t *)((uint8_t *)module_ptr + base_ptr->module_cu_offset);

   if (!b_deinit &&
       (GU_STATUS_CLOSING == ext_in_port_ptr->gu_status || GU_STATUS_CLOSING == ext_in_port_ptr->sg_ptr->gu_status))
   {
      // If external port or SG is marked for the closing then deinit it.
      b_deinit = TRUE;

      // If subgraph is marked for the closing then deinit can be done later when subgraph is being destroyed.
      if (b_ignore_ports_from_sg_close && GU_STATUS_CLOSING == ext_in_port_ptr->sg_ptr->gu_status)
      {
         b_deinit = FALSE;
      }
   }

   if (FALSE == b_deinit)
   {
      // nothing to do
      return;
   }

   SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);

   cu_ipc_ext_input_flush_data_queue(base_ptr, ext_in_port_ptr);

   posal_memory_free(cu_module_ptr->ipc_port_cb_info_ptr);

   cu_deinit_ext_port_queue(base_ptr, &ext_in_port_ptr->this_handle, cu_ext_port_ptr->bit_mask);

   // invalidate the association with internal port, so that dangling link can be destroyed first
   gu_deinit_ext_in_port(ext_in_port_ptr);

   SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);
}

static fwk_extn_ipc_port_trigger_t cu_get_ipc_port_trigger_from_module(uint32_t     module_instance_id,
                                                                       cu_module_t *cu_module_ptr,
                                                                       bool_t       is_input,
                                                                       uint32_t     port_id)
{

   fwk_extn_ipc_port_trigger_t trigger = *(cu_module_ptr->ipc_port_cb_info_ptr->ext_port_trigger_shared_ptr);

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   if (is_input)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "External input (MIID, port_id) (0x%lx, 0x%lx) data trigger: %lu ",
             module_instance_id,
             port_id,
             trigger);
   }
   else
   {
      AR_MSG(DBG_HIGH_PRIO,
             "External output (MIID, port_id) (0x%lx, 0x%lx) data trigger: %lu ",
             module_instance_id,
             port_id,
             trigger);
   }
#endif

   return trigger;
}

ar_result_t cu_ipc_port_ext_in_on_data_trigger(cu_base_t        *base_ptr,
                                               gu_ext_in_port_t *gu_ext_in_port_ptr,
                                               uint32_t          trigger_flags)
{
   ar_result_t result = AR_EOK;

   cu_module_t *module_ptr =
      (cu_module_t *)((uint8_t *)gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr + base_ptr->module_cu_offset);

   // pop the buffer from the queue and set it to the module.
   spf_msg_t input_data_q_msg = { 0 };
   result = posal_queue_pop_front(gu_ext_in_port_ptr->this_handle.q_ptr, (posal_queue_element_t *)&input_data_q_msg);

   if (AR_EOK != result)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed to pop data buf from ext in queue of port_id 0x%lx ",
             gu_ext_in_port_ptr->int_in_port_ptr->cmn.id);
      return result;
   }

   if (SPF_MSG_CMD_GPR != input_data_q_msg.msg_opcode)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Ext in queue of port_id 0x%lx received unexpected opcode 0x%lx",
             gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
             input_data_q_msg.msg_opcode);
      return AR_EFAILED;
   }

   gpr_packet_t *packet_ptr = (gpr_packet_t *)(input_data_q_msg.payload_ptr);

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Popped IPC GPR data msg opcode 0x%lx on ext in queue of port_id 0x%lx trigger_flags: 0x%lx",
          packet_ptr->opcode,
          gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
          trigger_flags);
#endif

   // set the packet to the module, capi module is expected to free/end the GPR packet
   result = module_ptr->ipc_port_cb_info_ptr->data_msg_handler(module_ptr->ipc_port_cb_info_ptr->callback_handle_ptr,
                                                               TRUE,
                                                               gu_ext_in_port_ptr->int_in_port_ptr->cmn.index,
                                                               packet_ptr,
                                                               trigger_flags);
   if (AR_EOK != result)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed setting data trigger GPR opcode 0x%lx on ext in queue of port_id 0x%lx trigger_flags: 0x%lx ",
             packet_ptr->opcode,
             gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
             trigger_flags);

      if ((AR_ENOTREADY == result) && (FWK_EXT_IPC_PORT_FLAG_FLUSH == trigger_flags))
      {
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                "Intr queue of port_id 0x%lx is not ready for the message, return to the queue",
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.id);

         if (AR_DID_FAIL(result = posal_queue_push_back(gu_ext_in_port_ptr->this_handle.q_ptr,
                                                        (posal_queue_element_t *)&input_data_q_msg)))
         {
            CU_MSG(base_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Pushing back to queue failed");
            return result;
         }
      }
   }

   return result;
}

/**
 * \brief Handles data trigger for an external output port.
 *
 * This function pops a buffer from the external output queue, checks the opcode,
 * and passes the buffer packet to the module for further processing.
 *
 * @param base_ptr Pointer to the base structure.
 * @param gu_ext_out_port_ptr Pointer to the external output port structure.
 *
 * @return AR_EOK on success, error code otherwise.
 */
ar_result_t cu_ipc_port_ext_out_on_data_trigger(cu_base_t         *base_ptr,
                                                gu_ext_out_port_t *gu_ext_out_port_ptr,
                                                uint32_t           trigger_flags)
{
   ar_result_t result = AR_EOK;

   cu_module_t *module_ptr =
      (cu_module_t *)((uint8_t *)gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr + base_ptr->module_cu_offset);

   // pop the buffer from the queue and set it to the module.
   spf_msg_t output_data_q_msg = { 0 };
   // Attempt to pop the front element from the queue.
   result = posal_queue_pop_front(gu_ext_out_port_ptr->this_handle.q_ptr, (posal_queue_element_t *)&output_data_q_msg);
   if (AR_EOK != result)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed to pop data buf from ext out queue of port_id 0x%lx ",
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.id);
      return result;
   }

   // Pass the buffer packet to the module.
   // Check if the opcode is SPF_MSG_CMD_GPR
   if (SPF_MSG_CMD_GPR != output_data_q_msg.msg_opcode)
   {
      // If the opcode is not SPF_MSG_CMD_GPR, log an error message and return AR_EFAILED.
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Ext out queue of port_id 0x%lx received unexpected opcode 0x%lx",
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.id,
             output_data_q_msg.msg_opcode);
      return AR_EFAILED;
   }

   // Cast the payload pointer to a gpr_packet_t pointer.
   gpr_packet_t *packet_ptr = (gpr_packet_t *)(output_data_q_msg.payload_ptr);

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   // Log a debug message with the popped opcode and port ID.
   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Popped IPC GPR data msg opcode 0x%lx on ext out queue of port_id 0x%lx trigger_flags: 0x%lx",
          packet_ptr->opcode,
          gu_ext_out_port_ptr->int_out_port_ptr->cmn.id,
          trigger_flags);
#endif

   // set the packet to the module, capi module is expected to free/end the GPR packet
   // Set the packet to the module, capi module is expected to free/end the GPR packet.
   result = module_ptr->ipc_port_cb_info_ptr->data_msg_handler(module_ptr->ipc_port_cb_info_ptr->callback_handle_ptr,
                                                               TRUE,
                                                               gu_ext_out_port_ptr->int_out_port_ptr->cmn.index,
                                                               packet_ptr,
                                                               trigger_flags);

   // If the data message handler returns an error, log an error message.
   if (AR_EOK != result)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed setting data trigger GPR opcode 0x%lx on ext out queue of port_id 0x%lx trigger_flags: 0x%lx ",
             packet_ptr->opcode,
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.id,
             trigger_flags);
   }

   return result;
}

/**
 * \brief This function is used to trigger control messages for IPC ports.
 *
 * @param base_ptr A pointer to the base structure.
 *
 * @return ar_result_t The result of the operation.
 */
ar_result_t cu_ipc_port_ctrl_msg_trigger(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;

   gpr_packet_t *packet_ptr         = (gpr_packet_t *)base_ptr->cmd_msg.payload_ptr;
   uint32_t      module_instance_id = packet_ptr->dst_port;

   gu_module_t *gu_module_ptr = gu_find_module(base_ptr->gu_ptr, module_instance_id);
   if (NULL == gu_module_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed handling IPC cmd msg 0x%lx Module 0x%lx not found",
             packet_ptr->opcode,
             module_instance_id);
      return result;
   }
   cu_module_t *module_ptr = (cu_module_t *)((uint8_t *)gu_module_ptr + base_ptr->module_cu_offset);

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Popped IPC GPR cmd msg opcode 0x%lx on ctrl msg queue for module 0x%lx ",
          packet_ptr->opcode,
          module_instance_id);
#endif

   // set the packet to the module, capi module is expected to free/end the GPR packet
   // todo: currently not setting any port info for the ctrl msg callback
   result = module_ptr->ipc_port_cb_info_ptr->ctrl_msg_handler(module_ptr->ipc_port_cb_info_ptr->callback_handle_ptr,
                                                               FALSE,
                                                               0,
                                                               packet_ptr);
   if (AR_EOK != result)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed setting callback to module 0x%lx on the cmd msg trigger GPR opcode 0x%lx",
             module_instance_id,
             packet_ptr->opcode);
   }

   return result;
}

uint32_t cu_ipc_module_gpr_callback(gpr_packet_t *packet, void *callback_data)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spf_handle_t *handle_ptr = NULL;

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   AR_MSG(DBG_LOW_PRIO,
          SPF_LOG_PREFIX "IPC module GPR callback from src 0x%lX to dst 0x%lX, opcode 0x%lX, token 0x%lX",
          packet->src_port,
          packet->dst_port,
          packet->opcode,
          packet->token);
#endif

   uint32_t thread_id = 0;

   // No need of mutex here. If a container destroys a module, it will first
   // deregister from GPR/ Until this function finishes, GPR must block
   // deregister.
   handle_ptr = (spf_handle_t *)callback_data;

   /*Validate handles and queue pointers */
   VERIFY(result, (handle_ptr && handle_ptr->cmd_handle_ptr && handle_ptr->cmd_handle_ptr->cmd_q_ptr));

   thread_id = (uint32_t)posal_thread_get_tid_v2(handle_ptr->cmd_handle_ptr->thread_id);

   if (SPF_IPC_FWK_EXTN_GPR_CMD_PEER_PORT_PROPERTY_UPDATE == packet->opcode ||
       SPF_IPC_FWK_EXTN_GPR_CMD_UPSTREAM_IPC_TX_STOPPED_ACK == packet->opcode ||
       SPF_IPC_FWK_EXTN_GPR_CMD_INFORM_ICB_INFO == packet->opcode || PARAM_ID_DOWNSTREAM_FRAME_LENGTH == packet->opcode)
   {
#ifdef IPC_MODULE_VERBOSE_DEBUGGING
      AR_MSG(DBG_HIGH_PRIO, "CNTR TID: 0x%lx received ctrl cmd response GUID 0x%lX.", thread_id, packet->opcode);
#endif

      spf_msg_t msg;
      msg.payload_ptr = packet;
      msg.msg_opcode  = SPF_MSG_CMD_IPC_PORT_FWK_EXTN_GPR;

      AR_MSG(DBG_HIGH_PRIO,
             " ss_dbg: CNTR TID: 0x%lx handle_ptr->q_ptr 0x%lX.",
             thread_id,
             handle_ptr->cmd_handle_ptr->cmd_q_ptr);

      /** control commands */
      TRY(result,
          (ar_result_t)posal_queue_push_back(handle_ptr->cmd_handle_ptr->cmd_q_ptr, (posal_queue_element_t *)&msg));

      return result;
   }

   spf_msg_t msg;
   msg.payload_ptr     = packet;
   switch (cu_get_bits(packet->opcode, AR_GUID_TYPE_MASK, AR_GUID_TYPE_SHIFT))
   {
      case AR_GUID_TYPE_CONTROL_CMD_RSP:
      {
#ifdef IPC_MODULE_VERBOSE_DEBUGGING
         AR_MSG(DBG_HIGH_PRIO, "CNTR TID: 0x%lx received ctrl cmd response GUID 0x%lX.", thread_id, packet->opcode);
#endif

         msg.msg_opcode  = SPF_MSG_CMD_IPC_PORT_FWK_EXTN_GPR;

         AR_MSG(DBG_HIGH_PRIO,
                " ss_dbg: CNTR TID: 0x%lx handle_ptr->q_ptr 0x%lX.",
                thread_id,
                handle_ptr->cmd_handle_ptr->cmd_q_ptr);

         /** control commands */
         TRY(result,
             (ar_result_t)posal_queue_push_back(handle_ptr->cmd_handle_ptr->cmd_q_ptr, (posal_queue_element_t *)&msg));
         break;
      }
      case AR_GUID_TYPE_DATA_CMD_RSP:
      {

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
         AR_MSG(DBG_LOW_PRIO,
                "CNTR TID: 0x%lx received data cmd response GUID 0x%lX. q_ptr:0x%lx ",
                thread_id,
                packet->opcode,
                handle_ptr->q_ptr);
#endif

         /** data command rsp */
         msg.msg_opcode  = SPF_MSG_CMD_GPR;
         TRY(result, (ar_result_t)posal_queue_push_back(handle_ptr->q_ptr, (posal_queue_element_t *)&msg));
         break;
      }
      case AR_GUID_TYPE_CONTROL_CMD:
      {
#ifdef IPC_MODULE_VERBOSE_DEBUGGING
         AR_MSG(DBG_LOW_PRIO,
                "CNTR TID: 0x%lx received AR_GUID_TYPE_CONTROL_CMD, GUID 0x%lX. q_ptr:0x%lx ",
                thread_id,
                packet->opcode,
                handle_ptr->cmd_handle_ptr->cmd_q_ptr);
#endif

         /** control commands */
         msg.msg_opcode = SPF_MSG_CMD_GPR;
         TRY(result,
             (ar_result_t)posal_queue_push_back(handle_ptr->cmd_handle_ptr->cmd_q_ptr, (posal_queue_element_t *)&msg));
         break;
      }
      case AR_GUID_TYPE_DATA_CMD:
      {
#ifdef IPC_MODULE_VERBOSE_DEBUGGING
         AR_MSG(DBG_LOW_PRIO,
                "CNTR TID: 0x%lx received AR_GUID_TYPE_DATA_CMD, GUID 0x%lX. q_ptr:0x%lx ",
                thread_id,
                packet->opcode,
                handle_ptr->q_ptr);
#endif

         /** Data commands */
         msg.msg_opcode = SPF_MSG_CMD_GPR;
         TRY(result, (ar_result_t)posal_queue_push_back(handle_ptr->q_ptr, (posal_queue_element_t *)&msg));
         break;
      }
      /* Important: Data command opcodes are by default pushed to the IPC external ports data queue in the default
       * handler. */
      default:
      {

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
         AR_MSG(DBG_LOW_PRIO, "CNTR TID: 0x%lx Handling GUID 0x%lX with default callback", thread_id, packet->opcode);
#endif

         cu_gpr_callback(packet, callback_data);
      }
   }

   CATCH(result, "CNTR TID ID: 0x%lx ", thread_id)
   {
      __gpr_cmd_end_command(packet, result);
   }

   return result;
}

/** Polls IPC port data queue and sets on the module until module trigger is satisfied or if the queue is empty. */
ar_result_t cu_poll_and_setup_ipc_input_port_buffer(cu_base_t                   *base_ptr,
                                                    gu_ext_in_port_t            *ipc_ext_in_port_ptr,
                                                    fwk_extn_ipc_port_trigger_t *in_trigger_ptr,
                                                    bool_t                       should_underrun)
{
   ar_result_t result = AR_EOK;

   gu_module_t *module_ptr    = (gu_module_t *)ipc_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr;
   cu_module_t *cu_module_ptr = (cu_module_t *)((uint8_t *)module_ptr + base_ptr->module_cu_offset);
   uint32_t     port_id       = ipc_ext_in_port_ptr->int_in_port_ptr->cmn.id;

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   uint32_t num_polled_buffers = 0;
#endif

   fwk_extn_ipc_port_trigger_t temp;
   if (NULL == in_trigger_ptr)
   {
      in_trigger_ptr = &temp;
   }

   while (TRUE)
   {
      *in_trigger_ptr =
         cu_get_ipc_port_trigger_from_module(module_ptr->module_instance_id, cu_module_ptr, TRUE, port_id);

      if (FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED == *in_trigger_ptr)
      {
         break;
      }

      // since port needs more input buffers check if queue can be polled
      if (!posal_queue_poll(ipc_ext_in_port_ptr->this_handle.q_ptr))
      {
         if (ALLOW_UNDERRUN == should_underrun && (FWK_EXTN_IPC_PORT_BUFFER_NEEDED == *in_trigger_ptr))
         {
            // set the packet to the module, capi module is expected to free/end the GPR packet
            result = cu_module_ptr->ipc_port_cb_info_ptr
                        ->data_msg_handler(cu_module_ptr->ipc_port_cb_info_ptr->callback_handle_ptr,
                                           TRUE,
                                           ipc_ext_in_port_ptr->int_in_port_ptr->cmn.index,
                                           NULL,
                                           FWK_EXT_IPC_PORT_FLAG_UNDERRUN);
            if (AR_EOK != result)
            {
               CU_MSG(base_ptr->gu_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "Failed underrunning on IPC ext in port queue of port_id 0x%lx Module 0x%lx result 0x%lx",
                      ipc_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                      module_ptr->module_instance_id,
                      result);
            }
         }
#ifdef IPC_MODULE_VERBOSE_DEBUGGING
         else
         {
            CU_MSG(base_ptr->gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   "Skip underrunning IPC ext in port queue of port_id 0x%lx Module 0x%lx trigger %lu",
                   ipc_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                   module_ptr->module_instance_id,
                   *in_trigger_ptr);
         }
#endif
         break;
      }

      /** Pops buffer from the queue and sets on the IPC module. */
      result = cu_ipc_port_ext_in_on_data_trigger(base_ptr, ipc_ext_in_port_ptr, NULL);

      if (AR_DID_FAIL(result))
      {
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "process failed for ext input port 0x%lx of Module 0x%lX ",
                port_id,
                module_ptr->module_instance_id);
      }

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
      num_polled_buffers++;

      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_LOW_PRIO,
             "Ext input port 0x%lx of Module 0x%lX. num_polled_buffers %lu ",
             port_id,
             module_ptr->module_instance_id,
             num_polled_buffers);
#endif
   }

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_LOW_PRIO,
          "IPC ext input port 0x%lx of Module 0x%lX setup done, trigger= %lu num_polled_buffers: %lu",
          port_id,
          module_ptr->module_instance_id,
          *in_trigger_ptr,
          num_polled_buffers);
#endif

   return result;
}

ar_result_t cu_poll_and_setup_ipc_output_port_buffer(cu_base_t                   *base_ptr,
                                                     gu_ext_out_port_t           *ipc_ext_out_port_ptr,
                                                     fwk_extn_ipc_port_trigger_t *out_trigger_ptr,
                                                     bool_t                       allow_overrun)
{
   ar_result_t  result        = AR_EOK;
   gu_module_t *module_ptr    = (gu_module_t *)ipc_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr;
   cu_module_t *cu_module_ptr = (cu_module_t *)((uint8_t *)module_ptr + base_ptr->module_cu_offset);
   uint32_t     port_id       = ipc_ext_out_port_ptr->int_out_port_ptr->cmn.id;

   fwk_extn_ipc_port_trigger_t temp;
   if (NULL == out_trigger_ptr)
   {
      out_trigger_ptr = &temp;
   }

   *out_trigger_ptr =
      cu_get_ipc_port_trigger_from_module(module_ptr->module_instance_id, cu_module_ptr, FALSE /*Output*/, port_id);

   if (FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED == *out_trigger_ptr)
   {
#ifdef IPC_MODULE_VERBOSE_DEBUGGING
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_LOW_PRIO,
             "IPC ext output port 0x%lx of Module 0x%lX trigger satisifed, skip popping buf from queue.",
             port_id,
             module_ptr->module_instance_id);
#endif
      return result;
   }

   /** poll and see if queue has any buf. if not mark overrun & move to next port. */
   if (!posal_queue_poll(ipc_ext_out_port_ptr->this_handle.q_ptr))
   {
#ifdef IPC_MODULE_VERBOSE_DEBUGGING
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_LOW_PRIO,
             "IPC ext output port 0x%lx of Module 0x%lX trigger %lu, but no buffers in queue to pop, module "
             "needs to overrun "
             "internally.",
             port_id,
             module_ptr->module_instance_id,
             *out_trigger_ptr);
#endif

      if ((FWK_EXTN_IPC_PORT_BUFFER_NEEDED == *out_trigger_ptr) && (ALLOW_OVERRUN == allow_overrun))
      {
         // set the packet to the module, capi module is expected to free/end the GPR packet
         result = cu_module_ptr->ipc_port_cb_info_ptr
                     ->data_msg_handler(cu_module_ptr->ipc_port_cb_info_ptr->callback_handle_ptr,
                                        TRUE,
                                        ipc_ext_out_port_ptr->int_out_port_ptr->cmn.index,
                                        NULL,
                                        FWK_EXT_IPC_PORT_FLAG_OVERRUN);
         if (AR_EOK != result)
         {
            CU_MSG(base_ptr->gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "Failed overrun on IPC ext out port queue of port_id 0x%lx Module 0x%lx result 0x%lx",
                   ipc_ext_out_port_ptr->int_out_port_ptr->cmn.id,
                   module_ptr->module_instance_id,
                   result);
         }
      }

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Skip overrun on IPC ext out port queue of port_id 0x%lx Module 0x%lx trigger %lu",
             ipc_ext_out_port_ptr->int_out_port_ptr->cmn.id,
             module_ptr->module_instance_id,
             *out_trigger_ptr);
#endif

      return result;
   }

   /** Pops buffer from the queue and sets on the IPC module. */
   result = cu_ipc_port_ext_out_on_data_trigger(base_ptr, ipc_ext_out_port_ptr, NULL);

   if (AR_DID_FAIL(result))
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "process failed for ext output port 0x%lx of Module 0x%lX ",
             port_id,
             module_ptr->module_instance_id);
   }

   *out_trigger_ptr =
      cu_get_ipc_port_trigger_from_module(module_ptr->module_instance_id, cu_module_ptr, FALSE /*Output*/, port_id);

   return result;
}

/** Module can return AR_ENOTREADY if it doesnt want to flush a given data msg.
    Then the container pushes back the msg to the queue. This is usually done for
    media format msgs. */
ar_result_t cu_ipc_ext_input_flush_data_queue(cu_base_t *base_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;

   void *first_returned_payload_ptr = NULL;

   cu_module_t *module_ptr =
      (cu_module_t *)((uint8_t *)gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr + base_ptr->module_cu_offset);

   // poll and send buffer to module for destroy
   uint32_t num_polled_buffers = 0;
   while (posal_queue_poll(gu_ext_in_port_ptr->this_handle.q_ptr))
   {
      if (first_returned_payload_ptr)
      {
         spf_msg_t *front_ptr = NULL;
         posal_queue_peek_front(gu_ext_in_port_ptr->this_handle.q_ptr, (posal_queue_element_t **)&front_ptr);
         if (front_ptr->payload_ptr == first_returned_payload_ptr)
         {
            break;
         }
      }

      // pop the buffer from the queue and set it to the module.
      spf_msg_t input_data_q_msg = { 0 };
      result = posal_queue_pop_front(gu_ext_in_port_ptr->this_handle.q_ptr, (posal_queue_element_t *)&input_data_q_msg);

      if (AR_EOK != result)
      {
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "Failed to pop data buf from ext in queue of port_id 0x%lx ",
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.id);
         return result;
      }

      if (SPF_MSG_CMD_GPR != input_data_q_msg.msg_opcode)
      {
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "Ext in queue of port_id 0x%lx received unexpected opcode 0x%lx",
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                input_data_q_msg.msg_opcode);
         return AR_EFAILED;
      }

      gpr_packet_t *packet_ptr = (gpr_packet_t *)(input_data_q_msg.payload_ptr);

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "Popped IPC GPR data msg opcode 0x%lx on ext in queue of port_id 0x%lx trigger_flags: 0x%lx",
             packet_ptr->opcode,
             gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
             FWK_EXT_IPC_PORT_FLAG_FLUSH);
#endif

      // set the packet to the module, capi module is expected to free/end the GPR packet
      result = module_ptr->ipc_port_cb_info_ptr->data_msg_handler(module_ptr->ipc_port_cb_info_ptr->callback_handle_ptr,
                                                                  TRUE,
                                                                  gu_ext_in_port_ptr->int_in_port_ptr->cmn.index,
                                                                  packet_ptr,
                                                                  FWK_EXT_IPC_PORT_FLAG_FLUSH);
      if (AR_EOK != result)
      {
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "Failed setting data trigger GPR opcode 0x%lx on ext in queue of port_id 0x%lx trigger_flags: 0x%lx ",
                packet_ptr->opcode,
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                FWK_EXT_IPC_PORT_FLAG_FLUSH);

         if (AR_ENOTREADY == result)
         {
            CU_MSG(base_ptr->gu_ptr->log_id,
                   DBG_HIGH_PRIO,
                   "Intr queue of port_id 0x%lx is not ready for the message, return to the queue",
                   gu_ext_in_port_ptr->int_in_port_ptr->cmn.id);

            if (NULL == first_returned_payload_ptr)
            {
               first_returned_payload_ptr = input_data_q_msg.payload_ptr;
            }

            if (AR_DID_FAIL(result = posal_queue_push_back(gu_ext_in_port_ptr->this_handle.q_ptr,
                                                           (posal_queue_element_t *)&input_data_q_msg)))
            {
               CU_MSG(base_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Pushing back to queue failed");
               return result;
            }
         }
      }
   }

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   num_polled_buffers++;

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_LOW_PRIO,
          "Ext output port 0x%lx of Module 0x%lX. num_polled_buffers %lu and flushed ",
          gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
          gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
          num_polled_buffers);
#endif
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

// utility function to send ext output port property update to downstream peer port.
static ar_result_t cu_propagate_to_peer_ipc_ext_port(cu_base_t                           *base_ptr,
                                                     uint32_t                             module_instance_id,
                                                     spf_msg_peer_port_property_update_t *prop_ptr,
                                                     cu_fwk_extn_ipc_port_info_t         *ipc_info_ptr)
{
   ar_result_t result = AR_EOK;

   if (!base_ptr || !prop_ptr)
   {
      return AR_EFAILED;
   }

   if (!ipc_info_ptr || !ipc_info_ptr->peer_module_iid || !ipc_info_ptr->peer_proc_domain_id ||
       !ipc_info_ptr->peer_port_id)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "Warning: Can't propagate to peer container - peer handle is NULL");
      return AR_ENOTREADY;
      // when new connection comes, before handles are exchanged - we need to return error so that the prop flags are
      // reset propagation will be re-attempted at prepare
   }

   CU_MSG(base_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "num_properties: %lu", prop_ptr->num_properties);

   // Update the required for the data ptr and msg header.
   uint32_t gpr_pkt_size = (prop_ptr->num_properties - 1) * sizeof(spf_msg_peer_port_property_info_t) +
                           sizeof(spf_msg_peer_port_property_update_t);

   gpr_packet_t *gpr_pkt_ptr = NULL;

   gpr_cmd_alloc_ext_t args;
   uint32_t            src_domain_id = 0;
   __gpr_cmd_get_host_domain_id(&src_domain_id);
   args.src_domain_id = src_domain_id;
   args.src_port      = module_instance_id;
   args.dst_domain_id = ipc_info_ptr->peer_proc_domain_id;
   args.dst_port      = ipc_info_ptr->peer_module_iid;
   args.token         = SPF_IPC_FWK_EXTN_GPR_CMD_PEER_PORT_PROPERTY_UPDATE;
   args.opcode        = SPF_IPC_FWK_EXTN_GPR_CMD_PEER_PORT_PROPERTY_UPDATE;
   args.payload_size  = gpr_pkt_size;
   args.ret_packet    = &gpr_pkt_ptr;
   args.client_data   = 0;
   result             = __gpr_cmd_alloc_ext(&args);
   if (AR_DID_FAIL(result) || (NULL == gpr_pkt_ptr))
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Allocating IPC peer port update GPR pkt to send to dsp client failed with %lu",
             result);
      return AR_ENOMEMORY;
   }

   void *gpr_pkt_payload_ptr = GPR_PKT_GET_PAYLOAD(void, gpr_pkt_ptr);

   // update the actual size of control message
   spf_msg_peer_two_port_property_update_t *prop_hdr_ptr =
      (spf_msg_peer_two_port_property_update_t *)gpr_pkt_payload_ptr;
   prop_hdr_ptr->prop1 = *prop_ptr;

   if (prop_ptr->num_properties > 1)
   {
      spf_msg_peer_two_port_property_update_t *two_prop_ptr = (spf_msg_peer_two_port_property_update_t *)prop_ptr;
      prop_hdr_ptr->prop2                                   = two_prop_ptr->prop2;
   }

   if (AR_EOK != (result = __gpr_cmd_async_send(gpr_pkt_ptr)))
   {
      result = AR_EFAILED;
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed to send GPR to port property update to IPC peer miid 0x%lx domain id 0x%lx, result %d",
             ipc_info_ptr->peer_module_iid,
             ipc_info_ptr->peer_proc_domain_id,
             result);
      __gpr_cmd_free(gpr_pkt_ptr);
      return result;
   }

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Successfully send GPR port property update to IPC peer miid 0x%lx domain id 0x%lx",
          ipc_info_ptr->peer_module_iid,
          ipc_info_ptr->peer_proc_domain_id);

   return result;
}

static ar_result_t cu_propagate_to_ds_from_ipc_tx_ext_out_port(cu_base_t                           *base_ptr,
                                                               gu_ext_out_port_t                   *ext_out_port_ptr,
                                                               spf_msg_peer_port_property_update_t *prop_ptr)
{
   cu_ext_out_port_t *cu_ext_port_ptr =
      (cu_ext_out_port_t *)((uint8_t *)ext_out_port_ptr + base_ptr->ext_out_port_cu_offset);

   cu_module_t *cu_module_ptr =
      (cu_module_t *)((uint8_t *)ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr + base_ptr->module_cu_offset);

   uint32_t module_instance_id = ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id;

   return cu_propagate_to_peer_ipc_ext_port(base_ptr,
                                            module_instance_id,
                                            prop_ptr,
                                            cu_module_ptr->ipc_port_cb_info_ptr);
}

static ar_result_t cu_propagate_to_us_from_ipc_rx_ext_in_port(cu_base_t                           *base_ptr,
                                                              gu_ext_in_port_t                    *ext_in_port_ptr,
                                                              spf_msg_peer_port_property_update_t *prop_ptr)
{
   cu_ext_out_port_t *cu_ext_port_ptr =
      (cu_ext_out_port_t *)((uint8_t *)ext_in_port_ptr + base_ptr->ext_in_port_cu_offset);

   cu_module_t *cu_module_ptr =
      (cu_module_t *)((uint8_t *)ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr + base_ptr->module_cu_offset);

   uint32_t module_instance_id = ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id;

   return cu_propagate_to_peer_ipc_ext_port(base_ptr,
                                            module_instance_id,
                                            prop_ptr,
                                            cu_module_ptr->ipc_port_cb_info_ptr);
}

/** Handle */
ar_result_t cu_handle_ipc_peer_port_property_update_gpr_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:IPC_PEER_PORT_PROPERTY_UPDATE: current channel mask=0x%x",
          base_ptr->curr_chan_mask);

   gpr_packet_t *packet_ptr         = (gpr_packet_t *)base_ptr->cmd_msg.payload_ptr;
   uint32_t      module_instance_id = packet_ptr->dst_port;

   gu_module_t *gu_module_ptr = gu_find_module(base_ptr->gu_ptr, module_instance_id);
   if (NULL == gu_module_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed handling IPC cmd msg 0x%lx Module 0x%lx not found",
             packet_ptr->opcode,
             module_instance_id);
      __gpr_cmd_end_command(packet_ptr, AR_EFAILED);
      return result;
   }

   cu_module_t *module_ptr = (cu_module_t *)((uint8_t *)gu_module_ptr + base_ptr->module_cu_offset);

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Popped IPC GPR cmd msg opcode 0x%lx on ctrl msg queue for module 0x%lx ",
          packet_ptr->opcode,
          module_instance_id);
#endif

   if (SPF_IPC_FWK_EXTN_GPR_CMD_PEER_PORT_PROPERTY_UPDATE != packet_ptr->opcode)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Popped unsupported GPR cmd msg opcode 0x%lx for module 0x%lx ",
             packet_ptr->opcode,
             module_instance_id);
      __gpr_cmd_end_command(packet_ptr, AR_EFAILED);
      return result;
   }

   // get  the structure of the gpr packet
   int8_t *payload_ptr = GPR_PKT_GET_PAYLOAD(int8_t, packet_ptr);

   if (gu_module_ptr->ipc_input_port_list_ptr)
   {
      gu_ext_in_port_t *ipc_ext_in_port_ptr = gu_module_ptr->ipc_input_port_list_ptr->ip_port_ptr->ext_in_port_ptr;
      if (ipc_ext_in_port_ptr)
      {
         result = cu_process_peer_port_property_payload(base_ptr, payload_ptr, &ipc_ext_in_port_ptr->this_handle);
      }
      else
      {
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "Failed to propagate IPC peer port property from external input ipc port of module 0x%lx",
                module_instance_id);
      }
   }
   else if (gu_module_ptr->ipc_output_port_list_ptr)
   {
      gu_ext_out_port_t *ipc_ext_out_port_ptr = gu_module_ptr->ipc_output_port_list_ptr->op_port_ptr->ext_out_port_ptr;
      if (ipc_ext_out_port_ptr)
      {
         result = cu_process_peer_port_property_payload(base_ptr, payload_ptr, &ipc_ext_out_port_ptr->this_handle);
      }
      else
      {
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "Failed to propagate IPC peer port property from external output ipc port of module 0x%lx",
                module_instance_id);
      }
   }
   else
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             " Couldn't find an ipc port for module 0x%lx to propagate peer info",
             module_instance_id);
   }

   __gpr_cmd_free(packet_ptr);
   return result;
}

// utility function to send ext output port property update to downstream peer port
static ar_result_t cu_ipc_send_ipc_tx_port_stopped_ack_ipc_rx(cu_base_t         *base_ptr,
                                                              gu_ext_out_port_t *gu_ext_out_port_ptr,
                                                              topo_port_state_t  ds_state)
{
   ar_result_t result = AR_EOK;

   if (TOPO_PORT_STATE_STOPPED != ds_state)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_LOW_PRIO,
             "Upstream (0x%lX, 0x%lx) port not stopped, skip sending ack to stop ack to DS",
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.id);
      return result;
   }

   gu_module_t *gu_module_ptr = gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr;
   cu_module_t *module_ptr    = (cu_module_t *)((uint8_t *)gu_module_ptr + base_ptr->module_cu_offset);

   cu_fwk_extn_ipc_port_info_t *ipc_info_ptr = module_ptr->ipc_port_cb_info_ptr;

   if (!ipc_info_ptr || !ipc_info_ptr->peer_module_iid || !ipc_info_ptr->peer_proc_domain_id ||
       !ipc_info_ptr->peer_port_id)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "Warning: Can't send ack to ds ipx rx module cntr - peer info is invalid");
      return AR_ENOTREADY;
   }

   // Update the required for the data ptr and msg header.

   gpr_cmd_alloc_send_t args;
   uint32_t             src_domain_id = 0;
   __gpr_cmd_get_host_domain_id(&src_domain_id);
   args.src_domain_id = src_domain_id;
   args.src_port      = gu_module_ptr->module_instance_id;
   args.dst_domain_id = ipc_info_ptr->peer_proc_domain_id;
   args.dst_port      = ipc_info_ptr->peer_module_iid;
   args.token         = SPF_IPC_FWK_EXTN_GPR_CMD_UPSTREAM_IPC_TX_STOPPED_ACK;
   args.opcode        = SPF_IPC_FWK_EXTN_GPR_CMD_UPSTREAM_IPC_TX_STOPPED_ACK;
   args.client_data   = 0;
   args.payload_size  = 0;
   result             = __gpr_cmd_alloc_send(&args);
   if (AR_DID_FAIL(result))
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             " Failed to push the upstream (0x%lX, 0x%lx) stopped message to downstream %d. ",
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
             gu_ext_out_port_ptr->int_out_port_ptr->cmn.id,
             result);
      return AR_ENOMEMORY;
   }

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_LOW_PRIO,
          " sent upstream (0x%lX, 0x%lx) stopped to downstream ",
          gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
          gu_ext_out_port_ptr->int_out_port_ptr->cmn.id);

   return result;
}

ar_result_t cu_handle_upstream_ipc_tx_stopped_ack(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;

   gpr_packet_t *packet_ptr         = (gpr_packet_t *)base_ptr->cmd_msg.payload_ptr;
   uint32_t      module_instance_id = packet_ptr->dst_port;

   gu_module_t *gu_module_ptr = gu_find_module(base_ptr->gu_ptr, module_instance_id);
   if (NULL == gu_module_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed handling IPC cmd msg 0x%lx Module 0x%lx not found",
             packet_ptr->opcode,
             module_instance_id);
      return result;
   }

#ifdef IPC_MODULE_VERBOSE_DEBUGGING
   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Popped IPC GPR cmd msg opcode 0x%lx on ctrl msg queue for module 0x%lx ",
          packet_ptr->opcode,
          module_instance_id);
#endif

   if (SPF_IPC_FWK_EXTN_GPR_CMD_UPSTREAM_IPC_TX_STOPPED_ACK != packet_ptr->opcode)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Popped unsupported GPR cmd msg opcode 0x%lx for module 0x%lx ",
             packet_ptr->opcode,
             module_instance_id);
      return result;
   }

   if ((NULL == gu_module_ptr->ipc_input_port_list_ptr) ||
       !gu_module_ptr->ipc_input_port_list_ptr->ip_port_ptr->ext_in_port_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Popped unsupported GPR cmd msg opcode 0x%lx for module 0x%lx ",
             packet_ptr->opcode,
             module_instance_id);
      return AR_EFAILED;
   }

   gu_ext_in_port_t *ipc_ext_in_port_ptr = gu_module_ptr->ipc_input_port_list_ptr->ip_port_ptr->ext_in_port_ptr;

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_LOW_PRIO,
          "CMD:UPSTREAM_STOP_CMD: Executing upstream stop GPR cmd for (0x%lX, 0x%lx). current channel mask=0x%x",
          ipc_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
          ipc_ext_in_port_ptr->int_in_port_ptr->cmn.id,
          base_ptr->curr_chan_mask);

   result = cu_ipc_ext_input_flush_data_queue(base_ptr, (gu_ext_in_port_t *)ipc_ext_in_port_ptr);

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_LOW_PRIO,
          "CMD:UPSTREAM_STOP_CMD: (0x%lX, 0x%lx) handling upstream stop done",
          ipc_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
          ipc_ext_in_port_ptr->int_in_port_ptr->cmn.id);

   return result;
}

// ipc modules
ar_result_t cu_ipc_rx_create_send_ipc_info_to_upstreams(cu_base_t        *base_ptr,
                                                        cu_ext_in_port_t *ext_in_port_ptr,
                                                        gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;

   gu_module_t *gu_module_ptr = gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr;
   cu_module_t *module_ptr    = (cu_module_t *)((uint8_t *)gu_module_ptr + base_ptr->module_cu_offset);

   cu_fwk_extn_ipc_port_info_t *ipc_info_ptr = module_ptr->ipc_port_cb_info_ptr;

   if (!ipc_info_ptr || !ipc_info_ptr->peer_module_iid || !ipc_info_ptr->peer_proc_domain_id ||
       !ipc_info_ptr->peer_port_id)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "Warning: Can't send ds frame len info to us ipx tx module cntr - peer info is invalid");
      return AR_ENOTREADY;
   }

   // Update the required for the data ptr and msg header.
   uint32_t      gpr_pkt_size = sizeof(spf_msg_cmd_inform_icb_info_t);
   gpr_packet_t *gpr_pkt_ptr  = NULL;

   gpr_cmd_alloc_ext_t args;
   uint32_t            src_domain_id = 0;
   __gpr_cmd_get_host_domain_id(&src_domain_id);
   args.src_domain_id = src_domain_id;
   args.src_port      = gu_module_ptr->module_instance_id;
   args.dst_domain_id = ipc_info_ptr->peer_proc_domain_id;
   args.dst_port      = ipc_info_ptr->peer_module_iid;
   args.token         = SPF_IPC_FWK_EXTN_GPR_CMD_INFORM_ICB_INFO;
   args.opcode        = SPF_IPC_FWK_EXTN_GPR_CMD_INFORM_ICB_INFO;
   args.payload_size  = gpr_pkt_size;
   args.ret_packet    = &gpr_pkt_ptr;
   args.client_data   = 0;
   result             = __gpr_cmd_alloc_ext(&args);
   if (AR_DID_FAIL(result) || (NULL == gpr_pkt_ptr))
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Allocating IPC peer port update GPR pkt to send to dsp client failed with %lu",
             result);
      return AR_ENOMEMORY;
   }

   void *gpr_pkt_payload_ptr = GPR_PKT_GET_PAYLOAD(void, gpr_pkt_ptr);

   // update the actual size of control message
   spf_msg_cmd_inform_icb_info_t *icb_hdr_ptr = (spf_msg_cmd_inform_icb_info_t *)gpr_pkt_payload_ptr;

   icb_hdr_ptr->downstream_frame_len_samples       = base_ptr->cntr_frame_len.frame_len_samples;
   icb_hdr_ptr->downstream_frame_len_us            = base_ptr->cntr_frame_len.frame_len_us;
   icb_hdr_ptr->downstream_sample_rate             = base_ptr->cntr_frame_len.sample_rate;
   icb_hdr_ptr->downstream_period_us               = base_ptr->period_us;
   icb_hdr_ptr->downstream_consumes_variable_input = ext_in_port_ptr->icb_info.flags.variable_input;
   icb_hdr_ptr->downstream_is_self_real_time       = ext_in_port_ptr->icb_info.flags.is_real_time;
   icb_hdr_ptr->downstream_set_single_buffer_mode  = ext_in_port_ptr->icb_info.flags.is_default_single_buffering_mode;
   icb_hdr_ptr->downstream_sid                     = gu_ext_in_port_ptr->sg_ptr->sid;

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "ICB: informing frame length (%lu, %lu, %lu), period in us (%lu) to upstream of Module 0x%lX, %lu. "
          "real-time %u,variable-size %u ",
          base_ptr->cntr_frame_len.frame_len_samples,
          base_ptr->cntr_frame_len.sample_rate,
          base_ptr->cntr_frame_len.frame_len_us,
          base_ptr->period_us,
          gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
          gu_ext_in_port_ptr->int_in_port_ptr->cmn.index,
          ext_in_port_ptr->icb_info.flags.is_real_time,
          ext_in_port_ptr->icb_info.flags.variable_input);
   if (AR_EOK != (result = __gpr_cmd_async_send(gpr_pkt_ptr)))
   {
      result = AR_EFAILED;
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed to send GPR for frame length update to IPC peer miid 0x%lx cntr domain id 0x%lx, result %d",
             ipc_info_ptr->peer_module_iid,
             ipc_info_ptr->peer_proc_domain_id,
             result);
      __gpr_cmd_free(gpr_pkt_ptr);
      return result;
   }

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Successfully sent GPR frame length update to IPC peer miid 0x%lx cntr domain id 0x%lx",
          ipc_info_ptr->peer_module_iid,
          ipc_info_ptr->peer_proc_domain_id);
   return result;
}

// ipc modules
ar_result_t cu_ipc_tx_handle_icb_info_from_ds(cu_base_t         *base_ptr,
                                              gu_ext_out_port_t *gu_ext_out_port_ptr,
                                              cu_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t  result        = AR_EOK;
   gu_module_t *gu_module_ptr = gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr;

   if (FALSE == gu_is_ipc_ext_output_port(gu_ext_out_port_ptr))
   {
      return AR_EFAILED;
   }

   // Create a static dummy instance
   int dummy_var = 0;
   if (gu_ext_out_port_ptr->downstream_handle.spf_handle_ptr == NULL)
   {
      gu_ext_out_port_ptr->downstream_handle.spf_handle_ptr = (void *)&dummy_var;
   }

   cu_determine_ext_out_buffering(base_ptr, gu_ext_out_port_ptr);

   if ((void *)&dummy_var == gu_ext_out_port_ptr->downstream_handle.spf_handle_ptr)
   {
      gu_ext_out_port_ptr->downstream_handle.spf_handle_ptr = NULL;
   }

   // set param payload
   struct
   {
      apm_module_param_data_t             module_data;
      fwk_extn_param_id_ipc_buffer_info_t payload;
   } set_param_payload;

   set_param_payload.payload.num_reg_bufs    = ext_out_port_ptr->icb_info.icb.num_reg_bufs;
   set_param_payload.payload.num_reg_prebufs = ext_out_port_ptr->icb_info.icb.num_reg_prebufs;

   set_param_payload.module_data.module_instance_id = gu_module_ptr->module_instance_id;
   set_param_payload.module_data.param_id           = FWK_EXTN_PARAM_ID_IPC_BUFFER_INFO;
   set_param_payload.module_data.param_size =
      (sizeof(fwk_extn_param_id_ipc_buffer_info_t) + sizeof(apm_module_param_data_t));
   set_param_payload.module_data.error_code = 0;

   result = base_ptr->topo_vtbl_ptr->set_param(base_ptr->topo_ptr, &set_param_payload.module_data);
   if (AR_EOK != result)
   {
      CU_MSG(base_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "IPC frame len info handling failed");
      return result;
   }
   return result;
}

ar_result_t cu_ipc_output_ports_update_sg_state(cu_base_t        *me_ptr,
                                                gu_module_t      *module_ptr,
                                                topo_port_state_t self_port_state)
{
   // update IPC port states
   for (gu_output_port_list_t *ipc_out_port_list_ptr = module_ptr->ipc_output_port_list_ptr;
        (NULL != ipc_out_port_list_ptr);
        LIST_ADVANCE(ipc_out_port_list_ptr))
   {
      gu_output_port_t *ipc_out_port_ptr = (gu_output_port_t *)ipc_out_port_list_ptr->op_port_ptr;

      topo_port_state_t ds_downgraded_state = TOPO_PORT_STATE_INVALID;

      if (ipc_out_port_ptr->ext_out_port_ptr)
      {
         uint8_t *gu_ext_out_port_ptr = (uint8_t *)ipc_out_port_ptr->ext_out_port_ptr;

         cu_ext_out_port_t *ipc_ext_out_port_ptr =
            (cu_ext_out_port_t *)(gu_ext_out_port_ptr + me_ptr->ext_out_port_cu_offset);

         // for IPC external ports satellite APM will not inform the peer port states
         // for IPC module connected port state is assumed to be same as self sg state
         // since the IPC Tx and Rx always are in pairs and are expected to be in the same SG.
         // if there not in the same SG, there can be an issue b
         ipc_ext_out_port_ptr->connected_port_state = self_port_state;

         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_LOW_PRIO,
                "cu_update_sg_port_states: IPC output port 0x%d connected_port_state 0x%ld propagated_port_state "
                "0x%ld",
                ipc_out_port_ptr->cmn.id,
                ipc_ext_out_port_ptr->connected_port_state,
                ipc_ext_out_port_ptr->propagated_port_state);

         ds_downgraded_state =
            cu_evaluate_n_update_ext_out_ds_downgraded_port_state(me_ptr, ipc_out_port_ptr->ext_out_port_ptr);
      }
      else
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "cu_update_sg_port_states: unexpected! must have ext port assocaited with virt output port 0x%ld",
                ipc_out_port_ptr->cmn.id);
      }

      topo_port_state_t downgraded_state = tu_get_downgraded_state(self_port_state, ds_downgraded_state);

      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_LOW_PRIO,
             "cu_update_sg_port_states: IPC output port 0x%d downgraded_state 0x%ld",
             ipc_out_port_ptr->cmn.id,
             downgraded_state);

      if (TOPO_PORT_STATE_INVALID != downgraded_state)
      {
         me_ptr->topo_vtbl_ptr->set_port_property(me_ptr->topo_ptr,
                                                  TOPO_DATA_OUTPUT_PORT_TYPE,
                                                  PORT_PROPERTY_TOPO_STATE,
                                                  ipc_out_port_ptr,
                                                  downgraded_state);
      }
   }

   return AR_EOK;
}

ar_result_t cu_ipc_input_ports_update_sg_state(cu_base_t        *me_ptr,
                                               gu_module_t      *module_ptr,
                                               topo_port_state_t self_port_state)
{
   /* Initialize IPC input port states*/
   for (gu_input_port_list_t *ipc_in_port_list_ptr = module_ptr->ipc_input_port_list_ptr;
        (NULL != ipc_in_port_list_ptr);
        LIST_ADVANCE(ipc_in_port_list_ptr))
   {
      gu_input_port_t *ipc_in_port_ptr = (gu_input_port_t *)ipc_in_port_list_ptr->ip_port_ptr;

      if (ipc_in_port_ptr->ext_in_port_ptr)
      {
         uint8_t          *gu_ext_in_port_ptr = (uint8_t *)ipc_in_port_ptr->ext_in_port_ptr;
         cu_ext_in_port_t *ipc_ext_in_port_ptr =
            (cu_ext_in_port_t *)(gu_ext_in_port_ptr + me_ptr->ext_in_port_cu_offset);

         // for IPC external ports satellite APM will not inform the peer port states
         // for IPC module connected port state is assumed to be same as self sg state
         // since the IPC Tx and Rx always are in pairs and are expected to be in the same SG.
         // if there not in the same SG, there can be an issue because the actual connected SG
         // state can be different.
         // todo_mdf: future todo is to make code robust to handle IPC modules across different containers,
         // fix this by sending the connected port state from OLC directly to satellite containr with a new opcode.
         ipc_ext_in_port_ptr->connected_port_state = self_port_state;

         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_LOW_PRIO,
                "cu_update_sg_port_states: IPC input port 0x%d connected_port_state 0x%ld",
                ipc_in_port_ptr->cmn.id,
                ipc_ext_in_port_ptr->connected_port_state);
      }
      else
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "cu_update_sg_port_states: unexpected! must have ext input port assocaited with virt input port "
                "0x%ld",
                ipc_in_port_ptr->cmn.id);
      }

      me_ptr->topo_vtbl_ptr->set_port_property(me_ptr->topo_ptr,
                                               TOPO_DATA_INPUT_PORT_TYPE,
                                               PORT_PROPERTY_TOPO_STATE,
                                               ipc_in_port_ptr,
                                               self_port_state);
   }
   return AR_EOK;
}