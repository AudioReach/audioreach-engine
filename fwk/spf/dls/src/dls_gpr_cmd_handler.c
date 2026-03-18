/**
 * \file dls_gpr_cmd_handler.c
 * \brief
 *  	This file contains the GPR command handlers for the DLS service
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* ----------------------------------------------------------------------------
 * Include Files
 * ------------------------------------------------------------------------- */
#include "dls.h"
#include "dls_api.h"
#include "dls_i.h"

/* ----------------------------------------------------------------------------
 * Global Definitions/Externs
 * ------------------------------------------------------------------------- */
extern dls_param_id_config_buffer_t g_dls_config_buffer_info;
extern uint32_t g_dls_total_buf_count;
extern uint32_t g_dls_current_buf_count;

/* ----------------------------------------------------------------------------
 * Function Definitions
 * ------------------------------------------------------------------------- */
/**
  This function will handle the DLS_DATA_CMD_BUFFER_RETURN command received from
  the RTM client and clean up the buffers and mark them as available for the
  next data logging buffer request.

  @param[in]     dls_info_ptr                pointer to the DLS service info
  @param[in]     packet                      pointer to the GPR packet

  @param[out]    g_dls_config_buffer_info    DLS buffer configuration info

  @return        response to the client
 */
static ar_result_t dls_buffer_return(dls_t *dls_info_ptr,
                                     gpr_packet_t *packet)
{
   ar_result_t result = AR_EOK;
   dls_data_cmd_buffer_return_t *buf_ret_info = (dls_data_cmd_buffer_return_t *)GPR_PKT_GET_PAYLOAD(void, packet);

   /* check if the received number of buffers are not greater than the total buffers*/
   if (buf_ret_info->num_bufs > g_dls_total_buf_count)
   {
      AR_MSG(DBG_ERROR_PRIO, "dls_buffer_return(): number of buffers[%d] to clean up are more than the max buffers %d",
             buf_ret_info->num_bufs, g_dls_total_buf_count);
      result = AR_EBADPARAM;
      goto __dls_buf_ret_bail_out;
   }

   /* check if the received number of buffers are not less or equal to zero*/
   if (buf_ret_info->num_bufs <= 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "dls_buffer_return(): number of buffers[%d] to clean up are less or equal to zero",
             buf_ret_info->num_bufs);
      result = AR_EBADPARAM;
      goto __dls_buf_ret_bail_out;
   }

   posal_mutex_lock(dls_info_ptr->buf_return_mutex);

   dls_buf_start_addr_t *start_addr = (dls_buf_start_addr_t *)buf_ret_info->buf_start_addr;

   //get the buffer start and end address using the virtual address stored in the global struct
   uint64_t buf_start_addr = (((uint64_t)g_dls_config_buffer_info.buf_start_addr_msw) << 32) |
                              ((uint64_t)g_dls_config_buffer_info.buf_start_addr_lsw);
   uint64_t buf_end_addr = buf_start_addr + g_dls_config_buffer_info.total_buf_size;

   for (int32_t buf_num = 0; buf_num < buf_ret_info->num_bufs; buf_num++)
   {
      // map the physical address to the virtual address
      uint64_t buf_offset_virt_addr = 0;
      if (AR_DID_FAIL(result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(apm_get_mem_map_client(),
                                                                                   start_addr[buf_num].mem_map_handle,
                                                                                   start_addr[buf_num].buf_addr_lsw,
                                                                                   start_addr[buf_num].buf_addr_msw,
                                                                                   g_dls_config_buffer_info.max_log_pkt_size,
                                                                                   TRUE, // is_ref_counted
                                                                                   &buf_offset_virt_addr)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Phy to Virt Failed(paddr,vaddr)-->(%lx %lx, 0x%p)\n for the data buffer address",
                start_addr[buf_num].buf_addr_lsw,
                start_addr[buf_num].buf_addr_msw,
                (buf_offset_virt_addr));

         return AR_EFAILED;
      }

      if ((buf_offset_virt_addr < buf_start_addr) && ((buf_offset_virt_addr + g_dls_config_buffer_info.max_log_pkt_size) > buf_end_addr))
      {
         AR_MSG(DBG_ERROR_PRIO, "dls_buffer_return(): buffer starting at address 0x%p "
                                "is not in the range [start-0x%p, end-0x%p] of allocated memory",
                buf_offset_virt_addr,
                buf_start_addr,
                buf_end_addr);
         continue;
      }
      uint8_t *buf_addr = (uint8_t *)buf_offset_virt_addr;
      dls_buf_hdr_t *dls_buf = (dls_buf_hdr_t *)buf_addr;
      if(dls_buf->buf_state == DLS_BUF_AVAILABLE)
      {
         continue; //if the buffer is already cleaned and marked as available then continue with next buffer
      }
      memset(buf_addr, 0, g_dls_config_buffer_info.max_log_pkt_size);
      dls_buf->buf_state = (dls_buf_state_t)DLS_BUF_AVAILABLE;
      g_dls_current_buf_count++; //increment the available buffer counter

      AR_MSG(DBG_LOW_PRIO, "dls_buffer_return(): Buffer starting at addess 0x%p set to DLS_BUF_AVAILABLE state", buf_addr);
   }
   posal_mutex_unlock(dls_info_ptr->buf_return_mutex);

__dls_buf_ret_bail_out:
   return __gpr_cmd_end_command(packet, result);
}

/**
  This function will handle the event registration command received from
  the RTM client and cache the client information.

  @param[in]     dls_info_ptr    pointer to the DLS service info
  @param[in]     packet          pointer to the GPR packet

  @param[out]    dls_info_ptr    DLS client configuration info

  @return        result
 */
ar_result_t dls_event_register(dls_t *dls_info_ptr,
                               gpr_packet_t *gpr_pkt_ptr)
{
   ar_result_t result = AR_EOK;
   int32_t avail_idx = MAX_DLS_EVENT_CLIENTS;

   /** Get command header pointer */
   apm_cmd_header_t *cmd_header_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, gpr_pkt_ptr);

   /** Get the event param data pointer */
   apm_module_register_events_t *event_payload_ptr = (apm_module_register_events_t *)(cmd_header_ptr + 1);

   for (int32_t idx = 0; idx < MAX_DLS_EVENT_CLIENTS; idx++)
   {
      // if already registered, update the client info
      if (event_payload_ptr->event_id == dls_info_ptr->client_info[idx].event_id)
      {
         avail_idx = idx;
         break;
      }
      else if (!dls_info_ptr->client_info[idx].event_id) // if empty slot
      {
         avail_idx = idx;
      }
   }

   if (avail_idx >= MAX_DLS_EVENT_CLIENTS)
   {
      AR_MSG(DBG_ERROR_PRIO,
             " dls_event_register: could not find avail index for client event_id: 0x%lx is_register:%lu",
             event_payload_ptr->event_id,
             event_payload_ptr->is_register);
      result = AR_EFAILED;
      goto __dls_event_reg_bail_out;
   }

   if (event_payload_ptr->is_register)
   {
      dls_info_ptr->client_info[avail_idx].event_id = event_payload_ptr->event_id;
      dls_info_ptr->client_info[avail_idx].gpr_domain = gpr_pkt_ptr->src_domain_id;
      dls_info_ptr->client_info[avail_idx].gpr_port = gpr_pkt_ptr->src_port;
      dls_info_ptr->client_info[avail_idx].gpr_client_token = gpr_pkt_ptr->token;
   }
   else // clear the client info
   {
      memset(&dls_info_ptr->client_info[avail_idx], 0, sizeof(dls_event_client_info_t));
   }

   AR_MSG(DBG_HIGH_PRIO,
          " DLS: Done handling register events cmd. event_id: 0x%lx is_register:%lu result: 0x%lx",
          event_payload_ptr->event_id,
          event_payload_ptr->is_register,
          result);

__dls_event_reg_bail_out:
   return result;
}

/*
 * Command handler for DLS service/module
 */
ar_result_t dls_cmdq_gpr_cmd_handler(dls_t *dls_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t result = AR_EOK;
   gpr_packet_t *gpr_pkt_ptr;
   uint32_t cmd_opcode;
   uint8_t *cmd_payload_ptr;
   uint32_t aligned_param_size;
   bool_t rsc_id_found = FALSE;
   uint8_t *cmd_end_ptr;

   if (msg_ptr == NULL)
   {
      AR_MSG(DBG_ERROR_PRIO, "dls_cmdq_gpr_cmd_handler(): msg_ptr is NULL");
      return AR_EBADPARAM;
   }

   /** Get the pointer to GPR command */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   if (NULL == gpr_pkt_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "dls_cmdq_gpr_cmd_handler(): Received NULL payload ptr");
      return AR_EBADPARAM;
   }

   /** Get the GPR command opcode */
   cmd_opcode = gpr_pkt_ptr->opcode;

   switch (cmd_opcode)
   {
      case APM_CMD_REGISTER_MODULE_EVENTS:
      {
         // handles event registration cmd from the client
         result = dls_event_register(dls_info_ptr, gpr_pkt_ptr);
         __gpr_cmd_end_command(gpr_pkt_ptr, result);
         break;
      }

      case APM_CMD_SET_CFG:
      {
         /** Get command header pointer */
         apm_cmd_header_t *cmd_header_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, gpr_pkt_ptr);

         /** Get the module param data pointer */
         cmd_payload_ptr = (uint8_t *)cmd_header_ptr + sizeof(apm_cmd_header_t);
         apm_module_param_data_t *module_data_ptr = (apm_module_param_data_t *)cmd_payload_ptr;

         /* validate in-band mode */
         if (0 != cmd_header_ptr->mem_map_handle)
         {
            AR_MSG(DBG_ERROR_PRIO, "dls_cmdq_gpr_cmd_handler(): Out-of-band command mode is not supported");
            result = AR_EBADPARAM;
            goto __dls_gpr_cmd_hdlr_bail_out;
         }

         /** validate payload size and address */
         if (!cmd_header_ptr->payload_size || !cmd_payload_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "dls_cmdq_gpr_cmd_handler(): Received invalid calib payload, (payload_ptr, size) = (0x%lX, %lu).",
                   cmd_payload_ptr,
                   cmd_header_ptr->payload_size);
            result = AR_EBADPARAM;
            goto __dls_gpr_cmd_hdlr_bail_out;
         }

         /* validate module instance ID */
         if (module_data_ptr->module_instance_id != DLS_MODULE_INSTANCE_ID)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "dls_cmdq_gpr_cmd_handler(): Only handles DLS module (0x%lx). Module Instance ID: 0x%lx param_id: 0x%lx, "
                   "param_size: %d",
                   DLS_MODULE_INSTANCE_ID,
                   module_data_ptr->module_instance_id,
                   module_data_ptr->param_id,
                   module_data_ptr->param_size);
            result = AR_EBADPARAM;
            goto __dls_gpr_cmd_hdlr_bail_out;
         }

         result = dls_handle_set_cfg_cmd(dls_info_ptr, gpr_pkt_ptr);
         __gpr_cmd_end_command(gpr_pkt_ptr, result);
         break;
      }

      case DLS_DATA_CMD_BUFFER_RETURN:
      {
         dls_buffer_return(dls_info_ptr, gpr_pkt_ptr);
         break;
      }

      default:
      {
         result = AR_EUNSUPPORTED;
         AR_MSG(DBG_HIGH_PRIO, "dls_cmdq_gpr_cmd_handler(): Unsupported cmd/msg opcode: 0x%8lX", cmd_opcode);
         break;
      }
   }

__dls_gpr_cmd_hdlr_bail_out:
   /** End the GPR command when result is not AR_EOK */
   /** When result is AR_EOK, original packet is freed, and response packet is sent */
   if (result != AR_EOK)
   {
      __gpr_cmd_end_command(gpr_pkt_ptr, result);
   }

   return result;
}
