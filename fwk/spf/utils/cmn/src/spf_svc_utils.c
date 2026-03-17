/**
 * \file spf_svc_utils.c
 * \brief
 *     This file contains utilities to be used by typical services.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_svc_utils.h"

#include "ar_msg.h"
#include "gpr_api_inline.h"
#include "cntr_cntr_if.h"

/*--------------------------------------------------------------*/
/* Macro definitions                                            */
/* -------------------------------------------------------------*/

/* -----------------------------------------------------------------------
** Constant / Define Declarations
** ----------------------------------------------------------------------- */

/* =======================================================================
**                      Static Function Definitions
** ======================================================================= */
static void spf_svc_drain_data_queue(posal_queue_t *data_q_ptr)
{
   spf_msg_t   data_q_msg;
   ar_result_t result;

   // Clean up data queue.
   if (data_q_ptr)
   {
      // Drain any queued buffers.
      do
      {
         // Non-blocking MQ receive.
         result = posal_queue_pop_front(data_q_ptr, (posal_queue_element_t *)&data_q_msg);

         // Return the buffer.
         if (AR_EOK == result)
         {
            if (SPF_MSG_CMD_GPR == data_q_msg.msg_opcode)
            {
               gpr_packet_t *packet_ptr = (gpr_packet_t *)data_q_msg.payload_ptr;
               __gpr_cmd_free(packet_ptr);
            }
            else
            {
               spf_msg_return_msg(&data_q_msg);
            }
         }

      } while (AR_EOK == result);
   }
}

void spf_svc_drain_ctrl_msg_queue(posal_queue_t *cmd_q_ptr)
{
   return spf_svc_drain_cmd_queue(cmd_q_ptr);
}

void spf_svc_drain_cmd_queue(posal_queue_t *cmd_q_ptr)
{
   ar_result_t result;
   spf_msg_t   cmd_msg;

   // Clean up command queue.
   if (cmd_q_ptr)
   {
      // Drain any queued commands.
      do
      {
         if (AR_SUCCEEDED(result = posal_queue_pop_front(cmd_q_ptr, (posal_queue_element_t *)&cmd_msg)))
         {
            AR_MSG(DBG_HIGH_PRIO, "Got a message after DESTROY command!!");
            if (SPF_MSG_CMD_GPR == cmd_msg.msg_opcode)
            {
               gpr_packet_t *packet_ptr = (gpr_packet_t *)cmd_msg.payload_ptr;
               __gpr_cmd_end_command(packet_ptr, AR_ENOTREADY);
            }
            else
            {
               spf_msg_ack_msg(&cmd_msg, AR_ENOTREADY);
            }
         }
      } while (AR_EOK == result);
   }
}

/**
 * Pop a buffer from the buffer queue and free it.
 */
static void spf_svc_pop_and_free_buf(posal_queue_t *buf_q_ptr, uint32_t *num_bufs_allocated_ptr)
{
   ar_result_t result;

   // All nodes on buf_q_ptr will have the same structure {BufAddr, returnQ}, regardless
   // whether the actual buffer is malloced from heap or acquired from buffer manager
   // therefore, we can always use the buffer manager node to pop nodes on buf_q_ptr
   posal_bufmgr_node_t buf_node;
   result = posal_queue_pop_front(buf_q_ptr, (posal_queue_element_t *)&buf_node);

   // Retrieve the buffer.
   if (AR_EOK == result)
   {
      // Free the buffer.
      posal_memory_free(buf_node.buf_ptr);
      buf_node.buf_ptr        = NULL;
      *num_bufs_allocated_ptr = *num_bufs_allocated_ptr - 1;
   }

   // Shouldn't reach this point.
   else
   {
      AR_MSG(DBG_FATAL_PRIO, "Error %d in destroying buffers!!", result);
   }
}

/* =======================================================================
**                          Function Definitions
** ======================================================================= */
ar_result_t spf_svc_process_cmd_queue(void *                   me_ptr,
                                      spf_handle_t *           handle_ptr,
                                      spf_svc_msg_handler_func handlers[],
                                      uint32_t                 handler_table_size)
{
   spf_msg_t   msg;
   ar_result_t result;

   // Process all commands in the queue
   for (;;)
   {
      // Take next msg off the q
      posal_queue_t *cmd_q_ptr = handle_ptr->cmd_handle_ptr->cmd_q_ptr;
      result                   = posal_queue_pop_front(cmd_q_ptr, (posal_queue_element_t *)&msg);

      // Process the msg
      if (AR_EOK == result)
      {
         if (msg.msg_opcode >= handler_table_size)
         {
            if (AR_DID_FAIL(result = spf_svc_unsupported(me_ptr, &msg)))
            {
               // in case of errors, return them
               return result;
            }
         }
         // table lookup to call handling function, with FALSE to indicate processing of msg
         else if (AR_DID_FAIL(result = handlers[msg.msg_opcode](me_ptr, &msg)))
         {
            // in case of errors, return them
            return result;
         }
         // successfully processed msg, check for additional msgs
      }
      else if (AR_ENEEDMORE == result)
      {
         // if there are no more msgs in the q, return successfully
         return AR_EOK;
      }
      else
      {
         // all other results of QueuePopFront, return error code
         // actually QueuePopFront does not return other codes, keep it safe here
         return result;
      }

   } // for (;;)

   return result;
}

ar_result_t spf_svc_unsupported(void *me_ptr, spf_msg_t *msg_ptr)
{
   AR_MSG(DBG_HIGH_PRIO, "Unsupported message ID 0x%lx!!", msg_ptr->msg_opcode);
   return AR_EUNSUPPORTED;
}

ar_result_t spf_svc_return_success(void *me_ptr, spf_msg_t *msg_ptr)
{
   return AR_EOK;
}

void spf_svc_deinit_data_queue(posal_queue_t *data_q_ptr)
{
   // Drain any queued buffers.
   spf_svc_drain_data_queue(data_q_ptr);

   if (data_q_ptr)
   {
      // Deinit the q.
      posal_queue_deinit(data_q_ptr);
   }

   return;
}

void spf_svc_destroy_cmd_queue(posal_queue_t *cmd_q_ptr)
{
   // Drain any queued commands.
   spf_svc_drain_cmd_queue(cmd_q_ptr);
   if (cmd_q_ptr)
   {
      // Destroy the q.
      posal_queue_destroy(cmd_q_ptr);
   }

   return;
}

void spf_svc_deinit_cmd_queue(posal_queue_t *cmd_q_ptr)
{
   // Drain any queued commands.
   spf_svc_drain_cmd_queue(cmd_q_ptr);
   if (cmd_q_ptr)
   {
      // deinit the q.
      posal_queue_deinit(cmd_q_ptr);
   }

   return;
}

void spf_svc_destroy_buf_queue(posal_queue_t *buf_q_ptr, uint32_t *num_bufs_allocated_ptr)
{
   spf_svc_free_buffers_in_buf_queue_blocking(buf_q_ptr, num_bufs_allocated_ptr);

   if (buf_q_ptr)
   {
      posal_queue_destroy(buf_q_ptr);
   }
   return;
}

void spf_svc_deinit_buf_queue(posal_queue_t *buf_q_ptr, uint32_t *num_bufs_allocated_ptr)
{
   spf_svc_free_buffers_in_buf_queue_blocking(buf_q_ptr, num_bufs_allocated_ptr);

   if (buf_q_ptr)
   {
      posal_queue_deinit(buf_q_ptr);
   }
   return;
}

void spf_svc_free_ctrl_msgs_in_buf_queue_blocking(posal_queue_t *buf_q_ptr, uint32_t *num_bufs_allocated_ptr)
{
   return spf_svc_free_buffers_in_buf_queue_blocking(buf_q_ptr, num_bufs_allocated_ptr);
}

void spf_svc_free_buffers_in_buf_queue_blocking(posal_queue_t *buf_q_ptr, uint32_t *num_bufs_allocated_ptr)
{
   // Clean up buffer queue.
   if (buf_q_ptr && num_bufs_allocated_ptr)
   {
      // Drain the buffers, including ones that have yet to be returned.
      while (*num_bufs_allocated_ptr)
      {
         // Wait for buffers.
         (void)posal_channel_wait(posal_queue_get_channel(buf_q_ptr), posal_queue_get_channel_bit(buf_q_ptr));

         spf_svc_pop_and_free_buf(buf_q_ptr, num_bufs_allocated_ptr);
      }
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "buf_q or num_bufs_allocated is NULL");
   }
   return;
}

/** Free all buffers in the buffer queue without blocking. */
void spf_svc_free_ctrl_msgs_in_buf_queue_nonblocking(posal_queue_t *buf_q_ptr, uint32_t *num_bufs_allocated_ptr)
{
   return spf_svc_free_buffers_in_buf_queue_nonblocking(buf_q_ptr, num_bufs_allocated_ptr);
}

/**
 * Free all buffers in the buffer queue without blocking.
 */
void spf_svc_free_buffers_in_buf_queue_nonblocking(posal_queue_t *buf_q_ptr, uint32_t *num_bufs_allocated_ptr)
{
   // Clean up buffer queue.
   if (buf_q_ptr && num_bufs_allocated_ptr)
   {
      // Drain the buffers that are currently in the queue.
      while (posal_channel_poll(posal_queue_get_channel(buf_q_ptr), posal_queue_get_channel_bit(buf_q_ptr)))
      {
         if (!(*num_bufs_allocated_ptr))
         {
            AR_MSG(DBG_ERROR_PRIO, "freeing a buffer when num_bufs_allocated already reached zero");
         }

         spf_svc_pop_and_free_buf(buf_q_ptr, num_bufs_allocated_ptr);
      }
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "buf_q or num_bufs_allocated is NULL");
   }
   return;
}

/**
 * Creates a buffer of requested size and populated the gk msg with the allocated buffer
 */
ar_result_t spf_svc_create_buffer_for_buf_queue(posal_queue_t *buf_q_ptr,
                                                uint32_t       buf_size,
                                                spf_handle_t * dst_handle_ptr,
                                                POSAL_HEAP_ID  heap_id,
                                                spf_msg_t *    msg_ptr)
{

   if (0 == buf_size)
   {
      return AR_EFAILED;
   }
   uint32_t req_size = GET_SPF_INLINE_DATABUF_REQ_SIZE(buf_size);
   void *   buf_ptr  = posal_memory_malloc(req_size, heap_id);

   if (!buf_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Insufficient memory to create external out-buf. It requires %lu bytes", req_size);
      return AR_ENOMEMORY;
   }

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)(buf_ptr);
   memset(header_ptr, 0, sizeof(spf_msg_header_t) + sizeof(spf_msg_data_buffer_t));
   spf_msg_data_buffer_t *data_buf_ptr = (spf_msg_data_buffer_t *)&header_ptr->payload_start;

   header_ptr->return_q_ptr   = buf_q_ptr;
   header_ptr->dst_handle_ptr = dst_handle_ptr;

   data_buf_ptr->max_size = buf_size;
   msg_ptr->payload_ptr   = header_ptr;
   return AR_EOK;
}

/**
 * Creates a buffer of requested size and populated the gk msg with the allocated buffer
 */
ar_result_t spf_svc_create_ctrl_buffer_for_buf_queue(posal_queue_t *buf_q_ptr,
                                                     uint32_t       buf_size,
                                                     spf_handle_t * dst_handle_ptr,
                                                     POSAL_HEAP_ID  heap_id,
                                                     spf_msg_t *    msg_ptr)
{

   if (0 == buf_size)
   {
      return AR_EFAILED;
   }
   uint32_t req_size = GET_SPF_INLINE_CTRLBUF_REQ_SIZE(buf_size);
   void *   buf_ptr  = posal_memory_malloc(req_size, heap_id);

   if (!buf_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Insufficient memory to create external out-buf. It requires %lu bytes", req_size);
      return AR_ENOMEMORY;
   }
   memset(buf_ptr, 0, req_size);

   msg_ptr->payload_ptr = buf_ptr;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)(buf_ptr);

   header_ptr->return_q_ptr   = buf_q_ptr;
   header_ptr->dst_handle_ptr = dst_handle_ptr;

   spf_msg_ctrl_port_msg_t *ctrl_port_msg_ptr = (spf_msg_ctrl_port_msg_t *)&header_ptr->payload_start;
   ctrl_port_msg_ptr->max_size                = buf_size;

   return AR_EOK;
}

/**
 * Creates the number of buffers required and pushes them to the queue provided
 */
ar_result_t spf_svc_create_and_push_buffers_to_buf_queue_util(posal_queue_t *buf_q_ptr,
                                                              uint32_t       buf_size,
                                                              uint32_t       num_out_bufs,
                                                              spf_handle_t * dst_handle_ptr,
                                                              POSAL_HEAP_ID  heap_id,
                                                              uint32_t *     num_bufs_allocated_ptr,
                                                              bool_t         is_control_msg)
{
   ar_result_t result = AR_EOK;

   // Allocate and queue up the output buffers.
   while (*num_bufs_allocated_ptr < num_out_bufs)
   {
      spf_msg_t msg;

      if (is_control_msg)
      {
         result = spf_svc_create_ctrl_buffer_for_buf_queue(buf_q_ptr, buf_size, dst_handle_ptr, heap_id, &msg);
      }
      else
      {
         result = spf_svc_create_buffer_for_buf_queue(buf_q_ptr, buf_size, dst_handle_ptr, heap_id, &msg);
      }

      if (AR_DID_FAIL(result))
      {
         AR_MSG(DBG_ERROR_PRIO, "Failed to create buffer");
         // Note that we don't have to free any allocated buffers if this
         // happens halfway through the loop, because those buffers have
         // already been stored in the buffer queue and thus can be freed later.
         return result;
      }

      *num_bufs_allocated_ptr = *num_bufs_allocated_ptr + 1;

      // Immediately store the buffer in the buffer queue.
      if (AR_DID_FAIL(result = spf_msg_return_msg(&msg)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Failed to fill the buffer queue");
         *num_bufs_allocated_ptr = *num_bufs_allocated_ptr - 1;
         posal_memory_free(msg.payload_ptr);
         return result;
      }
   }

   return result;
}

/**
 * Creates a buffer of requested size and populated the gk msg with the allocated buffer
 */
ar_result_t spf_svc_create_buffer_v2_for_buf_queue(posal_queue_t *  buf_q_ptr,
                                                   uint32_t         bufs_num,
                                                   uint32_t         buf_size_per_data_buf,
                                                   spf_handle_t *   dst_handle_ptr,
                                                   POSAL_HEAP_ID    heap_id,
                                                   spf_msg_token_t *token_ptr,
                                                   spf_msg_t *      msg_ptr)
{

   if (0 == buf_size_per_data_buf)
   {
      return AR_EFAILED;
   }

   uint32_t req_size = GET_SPF_DATABUF_V2_REQ_SIZE(bufs_num, buf_size_per_data_buf);
   void *   buf_ptr  = posal_memory_malloc(req_size, heap_id);
   if (!buf_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Insufficient memory to create external out-buf. It requires %lu bytes", req_size);
      return AR_ENOMEMORY;
   }

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)(buf_ptr);
   memset(header_ptr, 0, req_size);

   spf_msg_data_buffer_v2_t *msg_data_buf_v2_ptr = (spf_msg_data_buffer_v2_t *)&header_ptr->payload_start;

   header_ptr->return_q_ptr   = buf_q_ptr;
   header_ptr->dst_handle_ptr = dst_handle_ptr;

   msg_data_buf_v2_ptr->bufs_num = bufs_num;
   memscpy(&header_ptr->token, sizeof(spf_msg_token_t), token_ptr, sizeof(spf_msg_token_t));

   spf_msg_single_buf_v2_t *data_buf_ptr = (spf_msg_single_buf_v2_t *)(msg_data_buf_v2_ptr + 1);

   uint8_t *data_ptr_created = (uint8_t *)(data_buf_ptr + bufs_num);

   for (uint32_t b = 0; b < bufs_num; b++)
   {
		data_buf_ptr[b].data_ptr = (int8_t*) data_ptr_created;
		data_buf_ptr[b].actual_size = 0;
		data_buf_ptr[b].max_size = buf_size_per_data_buf;
		data_ptr_created = data_ptr_created + buf_size_per_data_buf;
   }

   msg_ptr->payload_ptr = header_ptr;
   return AR_EOK;
}

/**
 * Creates the number of buffers required and pushes them to the queue provided
 */
ar_result_t spf_svc_create_and_push_buffers_v2_to_buf_queue(posal_queue_t *  buf_q_ptr,
                                                            uint32_t         bufs_num,
                                                            uint32_t         buf_size_per_data_buf,
                                                            uint32_t         num_out_bufs,
                                                            spf_handle_t *   dst_handle_ptr,
                                                            POSAL_HEAP_ID    heap_id,
                                                            spf_msg_token_t *token_ptr,
                                                            uint32_t *       num_bufs_allocated_ptr)
{

   ar_result_t result = AR_EOK;

   // it's okay for the dest_handle ptr to be null if it is a control message and if the link is inter-proc
   if (!dst_handle_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "destination not connected yet!");
      return AR_EFAILED;
   }

   // Allocate and queue up the output buffers.
   while (*num_bufs_allocated_ptr < num_out_bufs)
   {
      spf_msg_t msg;

      result = spf_svc_create_buffer_v2_for_buf_queue(buf_q_ptr,
                                                      bufs_num,
                                                      buf_size_per_data_buf,
                                                      dst_handle_ptr,
                                                      heap_id,
													  token_ptr,
                                                      &msg);

      if (AR_DID_FAIL(result))
      {
         AR_MSG(DBG_ERROR_PRIO, "Failed to create buffers");
         // Note that we don't have to free any allocated buffers if this
         // happens halfway through the loop, because those buffers have
         // already been stored in the buffer queue and thus can be freed later.
         return result;
      }

      *num_bufs_allocated_ptr = *num_bufs_allocated_ptr + 1;

      // Immediately store the buffer in the buffer queue.
      if (AR_DID_FAIL(result = spf_msg_return_msg(&msg)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Failed to fill the buffer queue");
         *num_bufs_allocated_ptr = *num_bufs_allocated_ptr - 1;
         posal_memory_free(msg.payload_ptr);
         return result;
      }
   }

   return result;
}

/**
 * Creates the number of buffers required and pushes them to the queue provided
 */
ar_result_t spf_svc_create_and_push_ctrl_msg_bufs_to_buf_queue(posal_queue_t *buf_q_ptr,
                                                               uint32_t       buf_size,
                                                               uint32_t       num_out_bufs,
                                                               spf_handle_t * dst_handle_ptr,
                                                               bool_t         is_inter_proc_link,
                                                               POSAL_HEAP_ID  heap_id,
                                                               uint32_t *     num_bufs_allocated_ptr)
{
   // it's okay for the dest_handle ptr to be null if it is a control message and if the link is inter-proc
   if ((!dst_handle_ptr) && (FALSE == is_inter_proc_link))
   {
      AR_MSG(DBG_ERROR_PRIO, "destination not connected yet!");
      return AR_EFAILED;
   }

   return spf_svc_create_and_push_buffers_to_buf_queue_util(buf_q_ptr,
                                                            buf_size,
                                                            num_out_bufs,
                                                            dst_handle_ptr,
                                                            heap_id,
                                                            num_bufs_allocated_ptr,
                                                            TRUE /*is_control_msg*/);
}

/**
 * Creates the number of buffers required and pushes them to the queue provided
 */
ar_result_t spf_svc_create_and_push_buffers_to_buf_queue(posal_queue_t *buf_q_ptr,
                                                         uint32_t       buf_size,
                                                         uint32_t       num_out_bufs,
                                                         spf_handle_t * dst_handle_ptr,
                                                         POSAL_HEAP_ID  heap_id,
                                                         uint32_t *     num_bufs_allocated_ptr)
{
   return spf_svc_create_and_push_buffers_to_buf_queue_util(buf_q_ptr,
                                                            buf_size,
                                                            num_out_bufs,
                                                            dst_handle_ptr,
                                                            heap_id,
                                                            num_bufs_allocated_ptr,
                                                            FALSE /*is_control_msg*/);
}
