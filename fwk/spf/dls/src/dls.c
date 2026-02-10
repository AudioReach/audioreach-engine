/**
 * \file dls.c
 * \brief
 *  	This file contains the DLS service/module implementation
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* ----------------------------------------------------------------------------
 * Include Files
 * ------------------------------------------------------------------------- */
#include "dls.h"
#include "dls_i.h"
#include "dls_log_pkt_hdr_api.h"
#include "dls_gpr_cmd_handler.h"
#include "gpr_ids_domains.h"
#include "dls_api.h"
#include "ar_guids.h"
#include "irm.h"

/* ----------------------------------------------------------------------------
 * Global Definitions
 * ------------------------------------------------------------------------- */
dls_param_id_config_buffer_t g_dls_config_buffer_info;

uint32_t g_dls_total_buf_count = 0;
uint32_t g_dls_current_buf_count = 0;
uint32_t g_dls_log_code[DLS_MAX_NUM_LOG_CODES] = {0};

/** dls module instance global object */
static dls_t g_dls_info;

/** Pointer to dls module global struct */
dls_t *g_dls_info_ptr;

static char           dls_cmd_q_name[] = "dls_cmd_q";
static char           dls_rsp_q_name[] = "dls_rsp_q";
static const uint32_t dls_cmd_q_mask   = 0x00000001UL;
static const uint32_t dls_rsp_q_mask   = 0x00000004UL;

static char DLS_THREAD_NAME[] = "DLS";
/** DLS service thread stack size in bytes */
#define DLS_THREAD_STACK_SIZE 4096

/* ----------------------------------------------------------------------------
 * Function Definitions
 * ------------------------------------------------------------------------- */
/**
  This is an utility function used to go through the list of the log codes and
  to return the log code index in the list.

  @param[in]     log_code    log code

  @param[out]    None

  @return        i            index of the log code from the list

  @dependencies  None
 */
ar_result_t dls_is_log_code_exists(uint32_t log_code)
{
   int32_t idx;
   for(idx = 0; idx < DLS_MAX_NUM_LOG_CODES; idx++)
   {
      if(log_code == g_dls_log_code[idx])
         break;
   }
   if(idx == DLS_MAX_NUM_LOG_CODES)
      return DLS_LOG_CODE_NOT_FOUND;
   else
      return idx; //send the log code index
}

/**
  This is an utility function used to go through the list of the log codes and
  to return the status. If log code exists then return TRUE, if not FALSE

  @param[in]     log_code    log code

  @param[out]    None

  @return        TRUE/FALSE  status of the log code presence

  @dependencies  None
 */
ar_result_t dls_log_code_status(uint32_t log_code)
{
   for(int32_t idx = 0; idx < DLS_MAX_NUM_LOG_CODES; idx++)
   {
      if(log_code == g_dls_log_code[idx])
      {
         return TRUE;
      }
   }

   return FALSE;
}

/**
  This is an utility function used to go through the list of the log codes and
  to return the status. If log code exists then return TRUE, if not FALSE

  @param[in]     event_id    event ID

  @param[out]    None

  @return        client_info  pointer to the client info

  @dependencies  None
 */
dls_event_client_info_t *dls_get_event_client_info(uint32_t event_id)
{
   // iterate through the loop and get the client info
   dls_event_client_info_t *client_info = NULL;
   for (int32_t idx = 0; idx < MAX_DLS_EVENT_CLIENTS; idx++)
   {
      if (g_dls_info_ptr->client_info[idx].event_id == event_id)
      {
         client_info = &g_dls_info_ptr->client_info[idx];
         return client_info;
      }
   }

   AR_MSG(DBG_ERROR_PRIO, "dls_get_event_client_info: Couldn't find client registration info for event_id: 0x%lx", event_id);
   return NULL;
}

/**
  This is an utility function used to go through the list of buffers and to
  acquire ana available buffer.

  @param[in]     log_code                 log code
  @param[in]     log_packet_size          buffer size to acquire

  @param[out]    g_dls_current_buf_count  buffer configuration information

  @return        buffer starting adddress

  @dependencies  None
 */
uint64_t dls_acquire_buffer(uint16_t log_code,
                            uint32_t log_packet_size)
{
   dls_log_hdr_type *hdr_ptr = NULL;
   bool_t buf_acquired =FALSE;
   uint32_t buf_offset = 0;
   uint64_t buf_offset_addr = 0;

   //to update the buffer with size, state and buffer availability
   log_packet_size = log_packet_size + sizeof(dls_buf_hdr_t);

   if (dls_is_log_code_exists(log_code) == DLS_LOG_CODE_NOT_FOUND)
   {
      AR_MSG(DBG_ERROR_PRIO, "Requested log code is currently disabled. Buffer acqisition process aborted and returning NULL");
      goto __dls_acquire_buf_bail_out;
   }

   if (log_packet_size > g_dls_config_buffer_info.max_log_pkt_size)
   {
      AR_MSG(DBG_ERROR_PRIO, "Request Buffer size (0x%x) is greater than the configured buffer size (0x%x). Returning NULL",
             log_packet_size,
             g_dls_config_buffer_info.max_log_pkt_size);
      goto __dls_acquire_buf_bail_out;
   }

   posal_mutex_lock(g_dls_info_ptr->buf_acquire_mutex);
   uint64_t buf_start_addr = (((uint64_t)g_dls_config_buffer_info.buf_start_addr_msw) << 32) |
                              ((uint64_t)g_dls_config_buffer_info.buf_start_addr_lsw);

   /* go through the buffers and acquire the available buffer */
   while ((buf_offset + g_dls_config_buffer_info.max_log_pkt_size) <= g_dls_config_buffer_info.total_buf_size)
   {
      buf_offset_addr = buf_start_addr + buf_offset;
      uint8_t *buf_ptr = (uint8_t *)buf_offset_addr;
      dls_buf_hdr_t *dls_buffer = (dls_buf_hdr_t *)buf_ptr;
      if (dls_buffer->buf_state == (dls_buf_state_t)DLS_BUF_AVAILABLE)
      {
         /* update the buffer status to acquired and the size */
         /*
         -------------------------------------------------------------------------
         | dls_buf_hdr_t | dls_log_hdr_type | dls_header_bitstream_data_t | data |
         -------------------------------------------------------------------------
         For more information, please see CCB/LLD document.
         */
         //This is the entire logged buffer size including sizeof(dls_buf_hdr_t)
         //sizeof(dls_log_hdr_type), sizeof(dls_header_bitstream_data_t) and the actual data.
         dls_buffer->buf_size = log_packet_size;
         dls_buffer->buf_state = (dls_buf_state_t)DLS_BUF_ACQUIRED;
         g_dls_current_buf_count--; // decrement the available buffer counter
         dls_buffer->buf_in_use = (uint32_t)(((float)g_dls_current_buf_count / (float)g_dls_total_buf_count) * 100);
         AR_MSG(DBG_LOW_PRIO, "DLS: Buffer starting address = 0x%p state = 0x%x buf availabity in % = %d",
                buf_offset_addr, dls_buffer->buf_state, dls_buffer->buf_in_use);

         // populate the log_hdr_type information in the buffer
         hdr_ptr = (dls_log_hdr_type *)(buf_offset_addr + sizeof(dls_buf_hdr_t));
         hdr_ptr->code = log_code;
         //This is the logged buffer size excluding sizeof(dls_buf_hdr_t)
         hdr_ptr->len = log_packet_size - sizeof(dls_buf_hdr_t);
         uint64_t timestamp = posal_timer_get_time();
         hdr_ptr->ts_lsw = GET_MSW_FROM_64BIT_WORD(timestamp);
         hdr_ptr->ts_msw = GET_LSW_FROM_64BIT_WORD(timestamp);
         buf_acquired = TRUE;
         break;
      }
      /* update the buffer offset address to get the next buffer start address */
      buf_offset = buf_offset + g_dls_config_buffer_info.max_log_pkt_size;
   }

   posal_mutex_unlock(g_dls_info_ptr->buf_acquire_mutex);

   /* return buffer starting address.
   existing log_alloc_buffer_internal() implementation expects the buffer start
   address where the dls_hdr_log_type information is stored. to support the
   existing implementation without any code changes, send the buffer address
   after dls_buf_hdr_t information. please note that during the buffer commit
   process, the buffer starting address will be shared with the client.
   */
   if(buf_acquired)
      return (buf_offset_addr + sizeof(dls_buf_hdr_t));

__dls_acquire_buf_bail_out:
   AR_MSG(DBG_ERROR_PRIO, "buffer not available for log_code 0x%x", log_code);
   return 0;
}

/**
  This is an utility function used to commit the buffer and to raise an event to
  notify the RTM client.

  @param[in]     log_pkt_ptr              pointer to the log packet

  @param[out]    g_dls_current_buf_count  buffer configuration information

  @return        result                   status error code

  @dependencies  None
 */
uint32_t dls_commit_buffer(void *log_pkt_ptr)
{
   uint32_t result = AR_EOK;
   uint32_t num_commit_bufs = 1; //by default 1 buffer will be committed
   uint8_t *buf_start_ptr = (uint8_t *)log_pkt_ptr - sizeof(dls_buf_hdr_t); //to bring back to the buffer starting address.
   dls_buf_hdr_t *dls_buffer = (dls_buf_hdr_t *) buf_start_ptr;
   dls_buffer->buf_state = (dls_buf_state_t)DLS_BUF_READY; //update the buffer state

   dls_event_client_info_t *client_info_ptr = dls_get_event_client_info(DLS_DATA_EVENT_ID_COMMIT_LOG_BUFFER);
   if (NULL == client_info_ptr)
   {
      // event client not found
      AR_MSG(DBG_ERROR_PRIO, "dls_commit_buffer: didn't find the client information. skipping the log buffer commit event");
      result = AR_EFAILED;
      goto __dls_commit_buf_bail_out;
   }
   /* increment the number of commit buffers count to allocate the memory to
   store the buffer starting addresses */
   //TODO: it is expected that in future a list will be created to prepare the
   //payload for the event and can grow based on the specified buffer count or
   //the timer. for the initial implementation, it is considered that the total
   //num bufs in the event will be always one.

   //calculate the memory to allocate in the GPR packet to store the event payload
   uint32_t event_commit_payload_size = sizeof(uint32_t) + (num_commit_bufs * sizeof(dls_buf_start_addr_t));

   // create packet for DLS_DATA_EVENT_ID_COMMIT_LOG_BUFFER
   // gpr pkt payload layout:
   //    gpr_packet_t
   //    apm_module_event_t
   uint32_t gpr_pkt_size = sizeof(apm_module_event_t) + event_commit_payload_size;

   gpr_packet_t *event_packet_ptr = dls_alloc_gpr_pkt(client_info_ptr->gpr_domain,
                                                      client_info_ptr->gpr_port,
                                                      client_info_ptr->gpr_client_token,
                                                      APM_EVENT_MODULE_TO_CLIENT,
                                                      gpr_pkt_size);
   if (NULL == event_packet_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "dls_commit_buffer: data event packet allocation failed");
      // Event packet couldnt be allocated
      result = AR_EFAILED;
      goto __dls_commit_buf_bail_out;
   }

   posal_mutex_lock(g_dls_info_ptr->buf_commit_mutex);

   int8_t *pkt_payload_ptr = GPR_PKT_GET_PAYLOAD(int8_t, event_packet_ptr);

   // populate apm_module_event_t
   apm_module_event_t *apm_event_ptr = (apm_module_event_t *)pkt_payload_ptr;
   apm_event_ptr->event_id           = DLS_DATA_EVENT_ID_COMMIT_LOG_BUFFER;
   apm_event_ptr->event_payload_size = gpr_pkt_size - sizeof(apm_module_event_t);

   //populate the commit buffer event payload
   dls_data_event_id_commit_log_buffer_t *commit_buf_ptr = (dls_data_event_id_commit_log_buffer_t *)(apm_event_ptr+1);

   commit_buf_ptr->num_bufs = num_commit_bufs; //update the number of buffers committing

   //update each buffer starting address
   for(int32_t buf_num = 0; buf_num < num_commit_bufs; buf_num++)
   {
      //get the physical address from the mapped memory region
      uint64_t buf_start_phy_addr = posal_memorymap_get_physical_addr_v2(&buf_start_ptr);

      commit_buf_ptr->buf_start_addr[buf_num].buf_addr_lsw = GET_LSW_FROM_64BIT_WORD((uint64_t)buf_start_phy_addr);
      commit_buf_ptr->buf_start_addr[buf_num].buf_addr_msw = GET_MSW_FROM_64BIT_WORD((uint64_t)buf_start_phy_addr);
   }

   // raise an APM event to the RTM client
   result = dls_gpr_async_send_packet(event_packet_ptr);

   AR_MSG(DBG_HIGH_PRIO,
          "dls_commit_buffer: Sent DLS_DATA_EVENT_ID_COMMIT_LOG_BUFFER event 0x%lX "
          "to client dest port: 0x%lX, dest domain ID: 0x%lX, client_token: 0x%lX",
          apm_event_ptr->event_id,
          client_info_ptr->gpr_port,
          client_info_ptr->gpr_domain,
          client_info_ptr->gpr_client_token);

   posal_mutex_unlock(g_dls_info_ptr->buf_commit_mutex);

__dls_commit_buf_bail_out:
   return result;
}

/**
  This utility function will zero out the buffer and sets the state to AVAILABLE
  Note that the memory is allocated by the DLS client. Hence DLS will not free
  any memory. It only zero out the buffer and mark the buffer as available for
  the next data logging buffer request.

  @param[in]     log_pkt_ptr                 pointer to the log packet

  @param[out]    g_dls_config_buffer_info    buffer configuration information

  @return        None

  @dependencies  Buffer configuration must be completed
 */
void dls_log_buf_free(void *log_pkt_ptr)
{
   if (NULL == log_pkt_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error! NULL pointer passed to dls_log_buf_free");
   }
   else
   {
      uint8_t *buf_ptr = (uint8_t *)log_pkt_ptr - (sizeof(dls_buf_hdr_t));

      // Access the buffer starting address, zero out and update the state to AVAILABLE
      memset(buf_ptr, 0, g_dls_config_buffer_info.max_log_pkt_size);
      dls_buf_hdr_t *dls_buffer = (dls_buf_hdr_t *)buf_ptr;
      dls_buffer->buf_state = (dls_buf_state_t)DLS_BUF_AVAILABLE;
   }
}

/**
  This utility function is used to support the gpr callback handling

  @param[in]     gpr_pkt_ptr  pointer to the GPR packet

  @param[out]    cb_ctx_ptr   pointer to the callback context

  @return        None

  @dependencies  Buffer configuration must be completed
 */
uint32_t dls_gpr_call_back_f(gpr_packet_t *gpr_pkt_ptr,
                             void *cb_ctx_ptr)
{
   uint32_t       result = AR_EOK;
   spf_msg_t      msg;
   uint32_t       cmd_opcode;
   spf_handle_t * dst_handle_ptr = NULL;
   posal_queue_t *temp_ptr       = NULL;

   /* Validate GPR packet pointer */
   if (!gpr_pkt_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS GPR CB: GPR pkt ptr is NULL");
      return AR_EFAILED;
   }

   /* Get the GPR command opcode */
   cmd_opcode           = gpr_pkt_ptr->opcode;
   uint32_t opcode_type = ((cmd_opcode & AR_GUID_TYPE_MASK) >> AR_GUID_TYPE_SHIFT);
   AR_MSG(DBG_HIGH_PRIO, "DLS GPR CB, rcvd cmd opcode[0x%08lX]", cmd_opcode);

   /* Validate GPR callback context pointer */
   if (!cb_ctx_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS GPR CB: CB ctxt ptr is NULL");
      goto __dls_gpr_call_back_bailout;
   }

   /* Get the destination module handle */
   dst_handle_ptr = (spf_handle_t *)cb_ctx_ptr;
   temp_ptr       = dst_handle_ptr->cmd_handle_ptr->cmd_q_ptr;

   if ((((opcode_type & AR_GUID_TYPE_CONTROL_CMD_RSP) == AR_GUID_TYPE_CONTROL_CMD_RSP)) &&
       ((posal_queue_t *)g_dls_info_ptr->p_dls_cmd_q == dst_handle_ptr->cmd_handle_ptr->cmd_q_ptr))
   {
      dst_handle_ptr->cmd_handle_ptr->cmd_q_ptr = (posal_queue_t *)g_dls_info_ptr->p_dls_rsp_q;
   }

   /** Compose the GK message payload to be routed to
    *  destination module */
   msg.msg_opcode  = SPF_MSG_CMD_GPR;
   msg.payload_ptr = gpr_pkt_ptr;

   /* Push msg to the destination module queue */
   if (AR_DID_FAIL(result = spf_msg_send_cmd(&msg, dst_handle_ptr)))
   {
      AR_MSG(DBG_HIGH_PRIO, "Failed to push gpr msg to DLS cmd_q");
      goto __dls_gpr_call_back_bailout;
   }

   dst_handle_ptr->cmd_handle_ptr->cmd_q_ptr = temp_ptr; // replace it with the actual qptr
   return result;

__dls_gpr_call_back_bailout:
   /* End the GPR command */
   __gpr_cmd_end_command(gpr_pkt_ptr, result);

   return AR_EOK;
}

/**
  This function handles the command queue and route the commands to GPR command
  handler

  @param[in]     dls_info_ptr    pointer to the DLS service information

  @param[out]    None

  @return        result          status error code

  @dependencies  DLS service must be created
 */
ar_result_t dls_process_cmd_q(dls_t *dls_info_ptr)
{
   ar_result_t result;
   spf_msg_t   msg_pkt;

   /** Pop the message buffer from the cmd queue */
   if (AR_EOK !=
       (result = posal_queue_pop_front((posal_queue_t *)dls_info_ptr->p_dls_cmd_q, (posal_queue_element_t *)&msg_pkt)))
   {
      AR_MSG(DBG_ERROR_PRIO, "dls_process_cmd_q(): Failed to pop buf from svc cmd_q, result: 0x%lx", result);

      return result;
   }

   /** Call the DLS svc command queue handler */
   return dls_cmdq_gpr_cmd_handler(dls_info_ptr, &msg_pkt);
}

/**
  This function handles the response queue

  @param[in]     dls_info_ptr    pointer to the DLS service information

  @param[out]    None

  @return        result          status error code

  @dependencies  DLS service must be created
 */
ar_result_t dls_process_rsp_q(dls_t *dls_info_ptr)
{
   AR_MSG(DBG_ERROR_PRIO, "In Process DLS Response Q. Not expected to be here. returing");
   return AR_EUNEXPECTED;
}

/**
  This function receives the command and identify them to route it to command
  queue or response queue

  @param[in]     dls_info_ptr    pointer to the DLS service information

  @param[out]    None

  @return        result          status error code

  @dependencies  DLS service must be created
 */
ar_result_t dls_process_q(dls_t *dls_info_ptr, uint32_t channel_status)
{
   ar_result_t result = AR_EOK;

   if (!channel_status)
   {
      return AR_EOK;
   }

   /** Process service command Q */
   if (channel_status & dls_cmd_q_mask)
   {
      result |= dls_process_cmd_q(dls_info_ptr);
   }

   /** Process service response Q */
   if (channel_status & dls_rsp_q_mask)
   {
      result |= dls_process_rsp_q(dls_info_ptr);
   }

   return result;
}

static ar_result_t dls_work_loop(void *arg_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    kill_sig_mask, curr_wait_mask;
   uint32_t    channel_status;

   /** Service Instance */
   dls_t *dls_info_ptr = (dls_t *)arg_ptr;

   /** Get the signal bit for kill signal */
   kill_sig_mask = posal_signal_get_channel_bit(dls_info_ptr->kill_signal_ptr);

   /** Set up mask for listening to the service cmd,rsp queues and
    *  kill signal for DLS thread */
   curr_wait_mask = (dls_cmd_q_mask | dls_rsp_q_mask | kill_sig_mask);

   AR_MSG(DBG_HIGH_PRIO, "Entering DLS workloop...");

   /** Enter forever loop */
   for (;;)
   {
      /** Block on any one or more of selected queues to get a msg */
      channel_status = posal_channel_wait(dls_info_ptr->channel_ptr, curr_wait_mask);

      /** Check if the KILL signal is received */
      if (channel_status & kill_sig_mask)
      {
         posal_signal_clear(dls_info_ptr->kill_signal_ptr);

         /** Return from the workloop */
         return AR_EOK;
      }

      result = dls_process_q(dls_info_ptr, channel_status);
   } /** forever loop */

   return result;
}

ar_result_t dls_deinit()
{
#ifndef DISABLE_DEINIT
   ar_result_t result      = AR_EOK;
   ar_result_t exit_status = 0;

   dls_t *dls_info_ptr = &g_dls_info;

   /** destroy the mutex */
   posal_mutex_destroy(&g_dls_info_ptr->buf_acquire_mutex);
   posal_mutex_destroy(&g_dls_info_ptr->buf_commit_mutex);
   posal_mutex_destroy(&g_dls_info_ptr->buf_return_mutex);

   /** Send Kill Signal to DLS service workloop */
   posal_signal_send(dls_info_ptr->kill_signal_ptr);

   /** Wait for the thread join */
   posal_thread_join(dls_info_ptr->dls_cmd_handle.thread_id, &exit_status);

   /** De-init the service CMD queue */
   posal_queue_destroy((posal_queue_t *)dls_info_ptr->p_dls_cmd_q);

   /** De-init the service RSP queue */
   posal_queue_destroy((posal_queue_t *)dls_info_ptr->p_dls_rsp_q);

   /** Destroy the service Kill signal */
   posal_signal_destroy(&dls_info_ptr->kill_signal_ptr);

   /** Destroy the channel */
   posal_channel_destroy(&dls_info_ptr->channel_ptr);

   /** De-register with  GPR    */
   if (AR_EOK != (result = __gpr_cmd_deregister(DLS_MODULE_INSTANCE_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to de-register with GPR, result: %lu", result);
   }

   AR_MSG(DBG_HIGH_PRIO, "Completed dls_deinit() ...");
#endif //#ifndef DISABLE_DEINIT
   return AR_EOK;
}

ar_result_t dls_init()
{
   ar_result_t result       = AR_EOK;
   ar_result_t gpr_result   = AR_EOK;
   dls_t *     dls_info_ptr = &g_dls_info;

   AR_MSG(DBG_HIGH_PRIO, "Entering dls_init() ...");

   /** Clear the global structure */
   memset(dls_info_ptr, 0, sizeof(dls_t));

   /** Init the DLS global pointer */
   g_dls_info_ptr = dls_info_ptr;
   posal_queue_init_attr_t q_attr;

   /** create the mutex */
   posal_mutex_create(&g_dls_info_ptr->buf_acquire_mutex, POSAL_HEAP_DEFAULT);
   posal_mutex_create(&g_dls_info_ptr->buf_commit_mutex, POSAL_HEAP_DEFAULT);
   posal_mutex_create(&g_dls_info_ptr->buf_return_mutex, POSAL_HEAP_DEFAULT);

   /** Create service DLS cmd queue */
   posal_queue_attr_init(&q_attr);
   posal_queue_attr_set_heap_id(&q_attr, POSAL_HEAP_DEFAULT);
   posal_queue_attr_set_max_nodes(&q_attr, DLS_MAX_CMD_Q_ELEMENTS);
   posal_queue_attr_set_prealloc_nodes(&q_attr, DLS_MAX_CMD_Q_ELEMENTS);
   posal_queue_attr_set_name(&q_attr, dls_cmd_q_name);

   /** Create service cmd queue */
   if (AR_DID_FAIL(result = posal_queue_create_v1(&(dls_info_ptr->p_dls_cmd_q), &q_attr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS: Failed to init DLS cmd Q, result: %lu", result);
      return result;
   }

   /** Create service DLS rsp queue */
   posal_queue_attr_init(&q_attr);
   posal_queue_attr_set_heap_id(&q_attr, POSAL_HEAP_DEFAULT);
   posal_queue_attr_set_max_nodes(&q_attr, DLS_MAX_RSP_Q_ELEMENTS);
   posal_queue_attr_set_prealloc_nodes(&q_attr, DLS_MAX_RSP_Q_ELEMENTS);
   posal_queue_attr_set_name(&q_attr, dls_rsp_q_name);

   /** Create service response queue */
   if (AR_DID_FAIL(result = posal_queue_create_v1(&(dls_info_ptr->p_dls_rsp_q), &q_attr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS: Failed to init DLS rsp Q, result: %lu", result);
      return result;
   }

   /** Create kill signal */
   if (AR_EOK != (result = posal_signal_create(&dls_info_ptr->kill_signal_ptr, POSAL_HEAP_DEFAULT)))
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS: Failed to create Kill Signal: %lu", result);
      return result;
   }

   /** Set up channel */
   if (AR_DID_FAIL(result = posal_channel_create(&dls_info_ptr->channel_ptr, POSAL_HEAP_DEFAULT)))
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS: Failed to create channel, result: %lu", result);
      return result;
   }

   /** Add service dls command queue to the channel */
   if (AR_DID_FAIL(
          result =
             posal_channel_addq(dls_info_ptr->channel_ptr, (posal_queue_t *)dls_info_ptr->p_dls_cmd_q, dls_cmd_q_mask)))
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS: Failed to add cmdq to channel, result: %lu", result);
      return result;
   }

   /** Update the dls command Q mask */
   dls_info_ptr->dls_cmd_q_wait_mask = dls_cmd_q_mask;

   /** Add service dls rsp queue to the channel */
   if (AR_DID_FAIL(
          result =
             posal_channel_addq(dls_info_ptr->channel_ptr, (posal_queue_t *)dls_info_ptr->p_dls_rsp_q, dls_rsp_q_mask)))
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS: Failed to add DLS rsp to channel, result: %lu", result);
      return result;
   }

   /** Update the dls rsp Q mask */
   dls_info_ptr->dls_rsp_q_wait_mask = dls_cmd_q_mask;

   /** Add kill signal to the channel */
   if (AR_EOK != (result = posal_channel_add_signal(dls_info_ptr->channel_ptr,
                                                    dls_info_ptr->kill_signal_ptr,
                                                    POSAL_CHANNEL_MASK_DONT_CARE)))
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS: Failed to add kill signal to channel, result: %lu", result);

      return result;
   }

   posal_thread_prio_t dls_thread_prio = 0;
   prio_query_t        query_tbl;

   // frame duration is dont care since its static thread prio
   query_tbl.frame_duration_us = 0;
   query_tbl.is_interrupt_trig = FALSE;
   query_tbl.static_req_id     = SPF_THREAD_STAT_DLS_ID;

   if (AR_DID_FAIL(result = posal_thread_calc_prio(&query_tbl, &dls_thread_prio)))
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS: Failed to get thread priority, result: %lu", result);
      return result;
   }

   /** Launch the thread */
   if (AR_DID_FAIL(result = posal_thread_launch(&dls_info_ptr->dls_cmd_handle.thread_id,
                                                DLS_THREAD_NAME,
                                                DLS_THREAD_STACK_SIZE,
                                                dls_thread_prio,
                                                dls_work_loop,
                                                (void *)dls_info_ptr,
                                                POSAL_HEAP_DEFAULT)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to launch DLS Thread, result: %lu", result);

      goto __bail_out_create;
   }

   /** Update command handle */
   spf_msg_init_cmd_handle(&dls_info_ptr->dls_cmd_handle,
                           dls_info_ptr->dls_cmd_handle.thread_id,
                           (posal_queue_t *)dls_info_ptr->p_dls_cmd_q,
                           NULL /*sys_cmd_q_ptr - not needed */);

   /** Update DLS handle */
   spf_msg_init_handle(&dls_info_ptr->dls_handle,
                       &dls_info_ptr->dls_cmd_handle,
                       (posal_queue_t *)dls_info_ptr->p_dls_rsp_q);

   /** Initialize DLS module - Register with GPR */
   if (AR_EOK !=
       (gpr_result = __gpr_cmd_register(DLS_MODULE_INSTANCE_ID, dls_gpr_call_back_f, &dls_info_ptr->dls_handle)))
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS INIT: Failed to register with GPR, result: 0x%8x", gpr_result);

      goto __bail_out_create;
   }

   /** Register with IRM */
   if (AR_EOK != (result = irm_register_static_module(DLS_MODULE_INSTANCE_ID,
                                                      POSAL_HEAP_DEFAULT,
                                                      posal_thread_get_tid_v2(dls_info_ptr->dls_cmd_handle.thread_id))))
   {
      AR_MSG(DBG_ERROR_PRIO, "DLS: Failed to register with IRM, result: 0x%8x", result);
   }

   AR_MSG(DBG_HIGH_PRIO, "DLS thread launched successfully");

   return result;

__bail_out_create:

   AR_MSG(DBG_ERROR_PRIO, "DLS create failed, destroying ...");

   /** Destroy DLS and free-up all partially allocated resource */
   dls_deinit();

   return result;
}
