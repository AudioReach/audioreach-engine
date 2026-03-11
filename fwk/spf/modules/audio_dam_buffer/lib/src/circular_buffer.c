/**
 *   \file cicular_buffer.c
 *   \brief
 *        This file contains implementation of circular buffering
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "circular_buffer_i.h"

/*==============================================================================
   Local utility Functions
==============================================================================*/

//#define DEBUG_CIRC_BUF_UTILS

#define ALIGN_8_BYTES(a) (((a) & (~(uint32_t)0x7)) + 8)

// Utility to add chunks based on the increased buffer size.
static circbuf_result_t _circ_buf_add_chunks(circ_buf_t *  circ_buf_ptr,
                                             uint32_t      new_buf_size,
                                             POSAL_HEAP_ID chunk_heap_id);

// Utility to remove chunks based on the decreased buffer size.
static circbuf_result_t _circ_buf_remove_chunks(circ_buf_t *circ_buf_ptr, uint32_t new_buf_size);

// Utility to resize the buffer based on the update read clients.
static circbuf_result_t _circ_buf_client_resize(circ_buf_t *circ_buf_ptr);

// Utility to reset the client write chunk position based on current buffer state.
static circbuf_result_t _circ_buf_write_client_reset(circ_buf_client_t *wr_client_ptr);

// Utility to check if the buffer is corrupted.
// static circbuf_result_t _circ_buf_check_if_corrupted(circ_buf_t *frag_circ_buf_ptr);

/*==============================================================================
   Local Function Implementation
==============================================================================*/

/*==============================================================================
   Public Function Implementation
==============================================================================*/

/* Initializes the Circular buffer */
circbuf_result_t circ_buf_alloc(circ_buf_t *  circ_buf_ptr,
                                uint32_t      circ_buf_size,
                                uint32_t      preferred_chunk_size,
                                uint32_t      buf_id,
                                POSAL_HEAP_ID heap_id)
{
   if ((NULL == circ_buf_ptr) || (0 == preferred_chunk_size))
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_ERROR_PRIO, "CIRC_BUF: Bad init params. Allocation failed.");
#endif
      return CIRCBUF_FAIL;
   }

   memset(circ_buf_ptr, 0, sizeof(circ_buf_t));

   /* Initialize the read and write chunks to the head of the chunk list */
   circ_buf_ptr->preferred_chunk_size = preferred_chunk_size;
   circ_buf_ptr->heap_id              = heap_id;
   circ_buf_ptr->id                   = buf_id;

   circ_buf_ptr->write_byte_counter = 0;
   circ_buf_ptr->num_read_clients   = 0;
   circ_buf_ptr->wr_client_ptr      = NULL;
   circ_buf_ptr->max_req_buf_resize = 0;

   // allocate chunks based on the requested circular buffer size.
   if (CIRCBUF_SUCCESS != _circ_buf_add_chunks(circ_buf_ptr, circ_buf_size, heap_id))
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_ERROR_PRIO, "CIRC_BUF: Chunk allocation failed.");
#endif
      return CIRCBUF_FAIL;
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "CIRC_BUF: Allocated Circ buf size = %lu, preferred_chunk_size: %lu, num_chunks: %lu heap_id:%lu",
          circ_buf_ptr->circ_buf_size,
          circ_buf_ptr->preferred_chunk_size,
          circ_buf_ptr->num_chunks,
          heap_id);
#endif

   return CIRCBUF_SUCCESS;
}

/* Initializes the Circular buffer */
circbuf_result_t circ_buf_realloc_chunks(circ_buf_t *circ_buf_ptr, POSAL_HEAP_ID chunk_heap_id)
{
   if (NULL == circ_buf_ptr)
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_ERROR_PRIO, "CIRC_BUF: Bad init params. Rellocation failed.");
#endif
      return CIRCBUF_FAIL;
   }

   AR_MSG(DBG_HIGH_PRIO,
          "realloc_chunks: buf_id=0x%x re-allocation in new heap_id: 0x%lx prev heap id %lu",
          circ_buf_ptr->id,
          chunk_heap_id,
          circ_buf_ptr->chunk_heap_id);

   // Create the additional new chunks and add them to a temporary list
   for (spf_list_node_t *cur_chunk_node_ptr = circ_buf_ptr->head_chunk_ptr; (NULL != cur_chunk_node_ptr);
        LIST_ADVANCE(cur_chunk_node_ptr))
   {
      // get old chunk info
      chunk_buffer_t *old_chunk_ptr        = (chunk_buffer_t *)cur_chunk_node_ptr->obj_ptr;
      uint32_t        new_chunk_alloc_size = old_chunk_ptr->size + sizeof(chunk_buffer_t);

      // allocate new chunk
      chunk_buffer_t *new_chunk_ptr = (chunk_buffer_t *)posal_memory_malloc(new_chunk_alloc_size, chunk_heap_id);
      if (NULL == new_chunk_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "Circular Buffer re-allocation is failed ");
         // free the partial list of chunks that have been created until not
         spf_list_delete_list_and_free_objs(&circ_buf_ptr->head_chunk_ptr, TRUE /* use_pool */);
         return CIRCBUF_FAIL;
      }
      memset(new_chunk_ptr, 0, new_chunk_alloc_size);

      new_chunk_ptr->id         = old_chunk_ptr->id;
      new_chunk_ptr->size       = old_chunk_ptr->size;
      new_chunk_ptr->buffer_ptr = (int8_t *)new_chunk_ptr + sizeof(chunk_buffer_t);

      // Copy data from old to new chunk
      memscpy(new_chunk_ptr->buffer_ptr, new_chunk_ptr->size, old_chunk_ptr->buffer_ptr, old_chunk_ptr->size);

      // free old chunk obj ptr
      posal_memory_free(cur_chunk_node_ptr->obj_ptr);

      // update chunk in the list node
      cur_chunk_node_ptr->obj_ptr = (int8_t *)new_chunk_ptr;

      AR_MSG(DBG_HIGH_PRIO,
             "realloc_chunks: buf_id=0x%x New chunk_id:%lu chunk_size:%lu chunk_ptr:0x%lx heap_id:%lx",
             circ_buf_ptr->id,
             new_chunk_ptr->id,
             new_chunk_ptr->size,
             new_chunk_ptr->buffer_ptr,
             chunk_heap_id);
   }

   // cache the heap used for reallocating the chunks
   circ_buf_ptr->chunk_heap_id = chunk_heap_id;

   return CIRCBUF_SUCCESS;
}

circbuf_result_t circ_buf_register_client(circ_buf_t *       circ_buf_struct_ptr,
                                          bool_t             is_read_client,
                                          POSAL_HEAP_ID      chunk_heap_id,
                                          uint32_t           req_base_buffer_size,
                                          circ_buf_client_t *client_hdl_ptr)
{
   // Return if the pointers are null or if the num_write_clients is
   if ((NULL == circ_buf_struct_ptr) || (NULL == client_hdl_ptr))
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO,
             "circ_buf_register_client: Failed to register. is_read_client = %d, wr_client_ptr=0x%x ",
             is_read_client,
             circ_buf_struct_ptr->wr_client_ptr);
#endif

      return CIRCBUF_FAIL;
   }

   if (!is_read_client && (circ_buf_struct_ptr->wr_client_ptr))
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO, "circ_buf_register_client: Failed to register writer client");
#endif
      return CIRCBUF_FAIL;
   }

   // Init base params.
   client_hdl_ptr->is_read_client       = is_read_client;
   client_hdl_ptr->circ_buf_ptr         = circ_buf_struct_ptr;
   client_hdl_ptr->req_base_buffer_size = req_base_buffer_size;
   client_hdl_ptr->req_buffer_resize    = 0;
   client_hdl_ptr->chunk_heap_id        = chunk_heap_id;

   if (is_read_client)
   {
      // Add the client to the client list.
      spf_list_insert_tail(&circ_buf_struct_ptr->rd_client_list_ptr,
                           client_hdl_ptr,
                           circ_buf_struct_ptr->heap_id,
                           TRUE /* use_pool*/);
      circ_buf_struct_ptr->num_read_clients++;

      // Resize the circular buffer based on new client request.
      _circ_buf_client_resize(circ_buf_struct_ptr);

      // Resets the read/write position of the client.
      add_circ_buf_read_client_reset(client_hdl_ptr, TRUE /** force reset*/);
   }
   else
   {
      circ_buf_struct_ptr->wr_client_ptr = client_hdl_ptr;

      // Resets the read/write position of the client.
      _circ_buf_write_client_reset(client_hdl_ptr);
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "circ_buf_register_client: Done. is_read_client = %d, req_alloc_size = %lu, circ buf sz = %lu, "
          "chunk_heap_id:%lu"
          "num_read_clients= %u ",
          is_read_client,
          req_base_buffer_size,
          circ_buf_struct_ptr->circ_buf_size,
          circ_buf_struct_ptr->num_read_clients,
          chunk_heap_id);
#endif

   return CIRCBUF_SUCCESS;
}

circbuf_result_t circ_buf_read_client_resize(circ_buf_client_t *rd_client_ptr,
                                             uint32_t           req_buffer_resize,
                                             POSAL_HEAP_ID      chunk_heap_id)
{
   if (NULL == rd_client_ptr)
   {
      return CIRCBUF_FAIL;
   }

   // Resize request, appends to the current size.
   rd_client_ptr->req_buffer_resize = req_buffer_resize;
   rd_client_ptr->chunk_heap_id     = chunk_heap_id;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "CIRC_BUF: Received client resize request. buf_id = 0x%x, client_request_size: %u",
          rd_client_ptr->circ_buf_ptr->id,
          req_buffer_resize);
#endif

   // Request to resize the circ buf
   return _circ_buf_client_resize(rd_client_ptr->circ_buf_ptr);
}

circbuf_result_t circ_buf_deregister_client(circ_buf_client_t *client_hdl_ptr)
{
   if ((NULL == client_hdl_ptr) || (NULL == client_hdl_ptr->circ_buf_ptr))
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO, "circ_buf_deregister_client: null arguments. Failed to De-register.");
#endif
      return CIRCBUF_FAIL;
   }

   circ_buf_t *circ_buf_struct_ptr = client_hdl_ptr->circ_buf_ptr;
   bool_t      is_read_client      = client_hdl_ptr->is_read_client;

   // Resize the buffer based on the new client list.
   if (is_read_client)
   {
      // Delete only the list node corresponding to the client.
      // Client memory must not be deallocated though.
      spf_list_find_delete_node(&circ_buf_struct_ptr->rd_client_list_ptr, client_hdl_ptr, TRUE /*pool_used*/);

      if (circ_buf_struct_ptr->num_read_clients > 0)
      {
         circ_buf_struct_ptr->num_read_clients--;
      }

      // Resize the buffer based on the updated client list.
      _circ_buf_client_resize(circ_buf_struct_ptr);
   }
   else
   {
      circ_buf_struct_ptr->wr_client_ptr = NULL;
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "circ_buf_deregister_client: Done. is_read_client = %d, circ buf size = %d, "
          "num_read_clients=%lu ",
          is_read_client,
          circ_buf_struct_ptr->circ_buf_size,
          circ_buf_struct_ptr->num_read_clients);
#endif

   // Reset the client pointer.
   memset(client_hdl_ptr, 0, sizeof(circ_buf_client_t));

   return CIRCBUF_SUCCESS;
}

/*
 * Dealloc and Deinit the circular buffer
 * Full documentation in circ_buf_utils.h
 */
circbuf_result_t circ_buf_free(circ_buf_t *circ_buf_ptr)
{
   // Gk list delete frees all the list nodes and chunk buffer memory as well.
   if (NULL != circ_buf_ptr->head_chunk_ptr)
   {
      spf_list_delete_list_and_free_objs(&circ_buf_ptr->head_chunk_ptr, TRUE /* pool_used */);
   }

   // Reset the write client handle.
   if (circ_buf_ptr->wr_client_ptr)
   {
      memset(circ_buf_ptr->wr_client_ptr, 0, sizeof(circ_buf_client_t));
   }

   // Reset all the read client handles.
   circ_buf_client_t *temp_rd_client_ptr =
      (circ_buf_client_t *)spf_list_pop_head(&circ_buf_ptr->rd_client_list_ptr, TRUE /* pool_used */);
   while (temp_rd_client_ptr)
   {
      memset(temp_rd_client_ptr, 0, sizeof(circ_buf_client_t));

      temp_rd_client_ptr =
         (circ_buf_client_t *)spf_list_pop_head(&circ_buf_ptr->rd_client_list_ptr, TRUE /* pool_used */);
   }

   // Delete all the client node handles.
   if (NULL != circ_buf_ptr->rd_client_list_ptr)
   {
      spf_list_delete_list(&circ_buf_ptr->rd_client_list_ptr, TRUE /* pool_used */);
   }

   memset(circ_buf_ptr, 0, sizeof(circ_buf_t));

   return CIRCBUF_SUCCESS;
}

circbuf_result_t circ_buf_memset(circ_buf_t *frag_circ_buf_ptr, uint8_t write_value, uint32_t num_bytes)
{
   return add_circ_buf_write_util(frag_circ_buf_ptr->wr_client_ptr, NULL, write_value, num_bytes, 0, 0);
}

/*
 * Resets the client write chunk position based on current buffer state.
 * Full documentation in circ_buf_utils.h
 */
static circbuf_result_t _circ_buf_write_client_reset(circ_buf_client_t *client_ptr)
{
   if (!client_ptr)
   {
      return CIRCBUF_SUCCESS;
   }

   circ_buf_t *circ_buf_ptr = client_ptr->circ_buf_ptr;

   // Reset to head chunk if the write client is not registered yet.
   client_ptr->rw_chunk_node_ptr    = circ_buf_ptr->head_chunk_ptr;
   client_ptr->rw_chunk_offset      = 0;
   circ_buf_ptr->write_byte_counter = 0;

   return CIRCBUF_SUCCESS;
}

static circbuf_result_t _circ_buf_add_chunks(circ_buf_t *  circ_buf_ptr,
                                             uint32_t      requested_additional_size,
                                             POSAL_HEAP_ID chunk_heap_id)
{
   ar_result_t      result                  = AR_EOK;
   uint32_t         num_additional_chunks   = 0;
   uint32_t         last_chunk_size         = 0;
   uint32_t         preferred_chunk_size    = circ_buf_ptr->preferred_chunk_size;
   uint32_t         prev_circ_buf_size      = circ_buf_ptr->circ_buf_size;
   spf_list_node_t *new_lists_tail_node_ptr = NULL;

   AR_MSG(DBG_HIGH_PRIO,
          "add_chunks: buf_id = 0x%lx, additional_size: %lu, prev_circ_buf_size: %lu, prev_num_chunks: %lu ",
          circ_buf_ptr->id,
          requested_additional_size,
          circ_buf_ptr->circ_buf_size,
          circ_buf_ptr->num_chunks);

   if (requested_additional_size == 0)
   {
      return CIRCBUF_SUCCESS;
   }

   // First allocate the array of chunk addresses, then allocate the individual buffer chunks
   last_chunk_size = (requested_additional_size % preferred_chunk_size);

   if (0 == last_chunk_size)
   {
      // All chunks are same size
      num_additional_chunks = requested_additional_size / preferred_chunk_size;
      last_chunk_size       = preferred_chunk_size;
   }
   else
   {
      // Last chunk size differs
      num_additional_chunks = requested_additional_size / preferred_chunk_size + 1;
   }

   // Create the additional new chunks and add them to a temporary list
   spf_list_node_t *temp_new_chunk_list_ptr = NULL;
   uint32_t         chunk_index             = circ_buf_ptr->num_chunks;
   for (uint32_t iter = 0; iter < num_additional_chunks; ++iter)
   {
      uint32_t chunk_size = (num_additional_chunks - 1 == iter) ? last_chunk_size : preferred_chunk_size;

      uint32_t alloc_size = chunk_size + sizeof(chunk_buffer_t);

      chunk_buffer_t *buf_ptr = (chunk_buffer_t *)posal_memory_malloc(sizeof(uint8_t) * alloc_size, chunk_heap_id);

      if (NULL == buf_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "Circular Buffer allocation is failed ");
         // free the partial list of chunks that have been created until not
         spf_list_delete_list_and_free_objs(&temp_new_chunk_list_ptr, TRUE /* use_pool */);
         return CIRCBUF_FAIL;
      }
      memset(buf_ptr, 0, alloc_size);

      buf_ptr->id         = chunk_index++;
      buf_ptr->size       = chunk_size;
      buf_ptr->buffer_ptr = (int8_t *)buf_ptr + sizeof(chunk_buffer_t);

      // Push the buffer to the tail of the list
      if (AR_EOK !=
          (result = spf_list_insert_tail(&temp_new_chunk_list_ptr, (void *)buf_ptr, chunk_heap_id, TRUE /* use_pool*/)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Failed to add chunk buffer to the list ");

         // free chunk buffer
         posal_memory_free(buf_ptr);

         // free the partial list of chunks that have been created until not
         spf_list_delete_list_and_free_objs(&temp_new_chunk_list_ptr, TRUE /* use_pool */);
         return CIRCBUF_FAIL;
      }

      AR_MSG(DBG_LOW_PRIO,
             "add_chunks: buf_id=0x%x chunk_id:%lu chunk_size:%lu chunk_ptr:0x%lx counter:%lu heap_id:%lx",
             circ_buf_ptr->id,
             buf_ptr->id,
             buf_ptr->size,
             buf_ptr->buffer_ptr,
             iter + 1,
             chunk_heap_id);
   }
   circ_buf_ptr->chunk_heap_id = chunk_heap_id;

   // Return failure if the new chunks list could not be created.
   if (NULL == temp_new_chunk_list_ptr)
   {
      return CIRCBUF_FAIL;
   }

   /** Insert the chunk list after the current write chunk, else merge the new list
       with the previous list.

       * SCENARIO=Adding chunks after current write position. Note that last chunk can be smaller.

         ### BEFORE:
               size=4K            size=4K
         -> [ OldChunk_N-1 ] -> [OldChunk_N]
               |
               |
          Wr offset= 1k

         ### AFTER:
               size=4K                size=4K           size=4K           size=1k          size=4K
         -> [ OldChunk_N-1 ] -> [ NewChunk_1 ] -> [NewChunk_2] -> [NewChunk_M (last)] -> [OldChunk_N]
               |
             wr_offset=1k
    */
   if (circ_buf_ptr->wr_client_ptr && circ_buf_ptr->wr_client_ptr->rw_chunk_node_ptr)
   {
      /* add chunks after write position*/

      spf_list_node_t *cur_wr_chunk_ptr  = circ_buf_ptr->wr_client_ptr->rw_chunk_node_ptr;
      spf_list_node_t *next_wr_chunk_ptr = cur_wr_chunk_ptr->next_ptr;

      cur_wr_chunk_ptr->next_ptr        = temp_new_chunk_list_ptr;
      temp_new_chunk_list_ptr->prev_ptr = cur_wr_chunk_ptr;

      // Get the tail node of the new chunk list.
      spf_list_node_t *temp_tail_chunk_ptr = NULL;
      spf_list_get_tail_node(temp_new_chunk_list_ptr, &temp_tail_chunk_ptr);
      if (NULL == temp_tail_chunk_ptr)
      {
#ifdef DEBUG_CIRC_BUF_UTILS
         AR_MSG(DBG_ERROR_PRIO, "CIRC_BUF: Tail node not found.");
#endif
         return CIRCBUF_FAIL;
      }
      new_lists_tail_node_ptr = temp_tail_chunk_ptr;

      temp_tail_chunk_ptr->next_ptr = next_wr_chunk_ptr;
      if (next_wr_chunk_ptr)
      {
         next_wr_chunk_ptr->prev_ptr = temp_tail_chunk_ptr;
      }
   }
   else
   {
      // Else merge the new chunk list with the previous chunk list.
      spf_list_merge_lists(&circ_buf_ptr->head_chunk_ptr, &temp_new_chunk_list_ptr);
   }

   /* Update the circular buffer size and chunk count */
   circ_buf_ptr->num_chunks = circ_buf_ptr->num_chunks + num_additional_chunks;
   circ_buf_ptr->circ_buf_size += requested_additional_size;

   // Reset the write/read client position if we are adding the chunks for first time.
   if ((0 == prev_circ_buf_size) && (circ_buf_ptr->circ_buf_size > 0))
   {
      // Reset write client position first.
      _circ_buf_write_client_reset(circ_buf_ptr->wr_client_ptr);

      // Reset read client position. Must make sure valid write client position is set before calling this function
      // since read position is dependent on current write position.
      for (spf_list_node_t *client_list_ptr = circ_buf_ptr->rd_client_list_ptr; (NULL != client_list_ptr);
           LIST_ADVANCE(client_list_ptr))
      {
         circ_buf_client_t *temp_client_ptr = (circ_buf_client_t *)client_list_ptr->obj_ptr;

         add_circ_buf_read_client_reset(temp_client_ptr, TRUE /** force reset*/);
      }
      goto __return_result;
   }
   else if (circ_buf_ptr->wr_client_ptr && circ_buf_ptr->wr_client_ptr->rw_chunk_node_ptr &&
            new_lists_tail_node_ptr) // checking for corner case, if needed to move read pointer.
   {
      /** Ideally distance between read and write pointers must remain the same even after adding new chunks (in other
       * words on increasing the buffer size).
       *
       * But there is an exception scenario, i.e when read and write are in the same chunk and read is ahead of the
       * write.
       * In this case, if we added new chunks (of len "X") then the distance between read and write abruptly
       * increases by "X". This will make distance between read/write positions goes out of sync with unread data len.
       *
       * To fix this, we need to move read position by newly added size "X", so that distance between read and write
       * reamins same.
       *
       * Additional to moving read pointer, we need to copy remaining data from the old read chunk to new read chunk
       * position so that data remains continous.
       *
       * SCENARIO:
         ### BEFORE: Distance between read and write is increased by (4+4+1k)
                  size=4K                size=4K           size=4K           size=1k              size=4K
            -> [      OldChunk_N-1    ] -> [ NewChunk_1 ] -> [NewChunk_2] -> [NewChunk_M (last)] -> [OldChunk_N]
                  |    |<.............>                                                             <.............>
         Wr offset|    |
            1k        |
                     Rd offset =2k

         ### AFTER: Moved rd by (4+4+1k) to keep the distance same
                  size=4K                size=4K           size=4K           size=1k              size=4K
            -> [ OldChunk_N-1 ] -> [ NewChunk_1 ] -> [NewChunk_2] -> [NewChunk_M (last)] -> [OldChunk_N]
                  |                                         |<........copied data......>    <.............>
                  wr_offset=1k                         Rd_offset =1k
      */
      circ_buf_client_t *wr_client_ptr        = circ_buf_ptr->wr_client_ptr;
      spf_list_node_t *  write_chunk_node_ptr = wr_client_ptr->rw_chunk_node_ptr;
      chunk_buffer_t *   cur_wr_chunk_ptr     = (chunk_buffer_t *)write_chunk_node_ptr->obj_ptr;
      for (spf_list_node_t *client_list_ptr = circ_buf_ptr->rd_client_list_ptr; (NULL != client_list_ptr);
           LIST_ADVANCE(client_list_ptr))
      {
         circ_buf_client_t *temp_client_ptr = (circ_buf_client_t *)client_list_ptr->obj_ptr;

         /*Ideally distance between read and */
         /* If rd and wr chunk node pointers falls in same chunk and rd offset is greater than wr offset, then i*/
         if (0 == temp_client_ptr->unread_bytes)
         {
            // if there is no data to read, rd/wr ptrs are same and the reader point doesnt need to be moved.
            continue;
         }

         if (temp_client_ptr->rw_chunk_node_ptr != write_chunk_node_ptr)
         {
            /* If rd and wr pointers falls in different chunk nodes, then no need to do
             * anything, because, inserted link won't affect rd pointers and unread bytes */
            continue;
         }

         if (wr_client_ptr->rw_chunk_offset > temp_client_ptr->rw_chunk_offset)
         {
            /* If write point is ahead of read, then we dont need to handle. since distance between
             *  them is unaffected on adding new chunks after write.
             */
            continue;
         }

         /* Hit corner case, rd and write are same chunk, and rd is ahead of write */

         // cache the current read pointer position and amount of data left to read in the chunk
         int8_t *prev_rd_buf_ptr =
            (cur_wr_chunk_ptr->buffer_ptr + temp_client_ptr->rw_chunk_offset); /* rd and wr are in same chunk*/
         uint32_t prev_rd_buf_bytes_to_move = cur_wr_chunk_ptr->size - temp_client_ptr->rw_chunk_offset;

         AR_MSG(DBG_HIGH_PRIO,
                "add_chunks: buf_id=0x%x, Rd,Wr are in same chunk, hit corner case. Unread:%lu bytes_to_move:%lu  ",
                circ_buf_ptr->id,
                temp_client_ptr->unread_bytes,
                prev_rd_buf_bytes_to_move);

         // move the read pointer by newly added size, so that the distance between read and write remains
         // same.
         circbuf_result_t result = add_circ_buf_position_shift_forward(circ_buf_ptr,
                                                                       &temp_client_ptr->rw_chunk_node_ptr,
                                                                       &temp_client_ptr->rw_chunk_offset,
                                                                       requested_additional_size);
         if (CIRCBUF_FAIL == result)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "add_chunks: buf_id=0x%x, Failed to handle corner case and adjust read position, result %lu",
                   circ_buf_ptr->id,
                   result);
            return result;
         }

         AR_MSG(DBG_HIGH_PRIO,
                "add_chunks: buf_id=0x%x, Handling corner case, moved rd by %lu bytes new rd position (,) ",
                circ_buf_ptr->id,
                requested_additional_size,
                temp_client_ptr->rw_chunk_node_ptr,
                temp_client_ptr->rw_chunk_offset);

         // Copy data from prev read chunk to new read chunks position, note we are only copying data
         // at the new read position and not updating read ptr in this step.
         // This step makes sure that unread data that unread data remains same between read and write chunks.
         spf_list_node_t *dest_chunk_node_ptr = temp_client_ptr->rw_chunk_node_ptr;
         uint32_t         dest_chunk_offset   = temp_client_ptr->rw_chunk_offset;
         add_circ_buf_data_copy_util(circ_buf_ptr,
                                     prev_rd_buf_bytes_to_move,
                                     prev_rd_buf_ptr,
                                     0, // dont care
                                     &dest_chunk_node_ptr,
                                     &dest_chunk_offset);
      }
   }

__return_result:
   AR_MSG(DBG_HIGH_PRIO,
          "add_chunks: buf_id = 0x%x, requested_additional_size: %lu, new_circ_buf_size: %lu, num_chunks: %lu ",
          circ_buf_ptr->id,
          requested_additional_size,
          circ_buf_ptr->circ_buf_size,
          circ_buf_ptr->num_chunks);

#ifdef DEBUG_CIRC_BUF_UTILS
   print_chunk_list(circ_buf_ptr);
#endif

   return CIRCBUF_SUCCESS;
}

static circbuf_result_t _circ_buf_remove_chunks(circ_buf_t *circ_buf_ptr, uint32_t removable_size)
{
   circbuf_result_t result = CIRCBUF_SUCCESS;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "remove: buf_id = 0x%x, removable_size: %u, new_circ_buf_size: %u, num_chunks: %u ",
          circ_buf_ptr->id,
          removable_size,
          circ_buf_ptr->circ_buf_size,
          circ_buf_ptr->num_chunks);
#endif

   if (removable_size == 0)
   {
      return CIRCBUF_SUCCESS;
   }

   spf_list_node_t *cur_wr_chunk_ptr = NULL;
   while (removable_size)
   {
      spf_list_node_t *rm_node_ptr = NULL;

      // Remove after the current write chunks pointer if a write client exist.
      if (circ_buf_ptr->wr_client_ptr && circ_buf_ptr->wr_client_ptr->rw_chunk_node_ptr)
      {
         cur_wr_chunk_ptr = circ_buf_ptr->wr_client_ptr->rw_chunk_node_ptr;

         if (cur_wr_chunk_ptr->next_ptr)
         {
            rm_node_ptr = cur_wr_chunk_ptr->next_ptr;
         }
      }

      // If the current write node is the tail, assign the head node as temp
      if (NULL == rm_node_ptr)
      {
         rm_node_ptr = circ_buf_ptr->head_chunk_ptr;
      }

      // chunk ptr cant be null since removable_size is non zero
      if (NULL == rm_node_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "remove: Unexpected null chunk! buf_id = 0x%x, removable_size: %u, circ_buf_size: %u, num_chunks: %u ",
                circ_buf_ptr->id,
                removable_size,
                circ_buf_ptr->circ_buf_size,
                circ_buf_ptr->num_chunks);
         return CIRCBUF_FAIL;
      }

      chunk_buffer_t *rem_chunk_ptr = (chunk_buffer_t *)rm_node_ptr->obj_ptr;

      AR_MSG(DBG_HIGH_PRIO,
             "remove: buf_id=0x%x, removable_size:%lu, cur_chunk_size:%lu circ_buf_size:%lu, num_chunks: %lu ",
             circ_buf_ptr->id,
             removable_size,
             rem_chunk_ptr->size,
             circ_buf_ptr->circ_buf_size,
             circ_buf_ptr->num_chunks);

      // remove entire chunk if the removable size is bigger
      if (removable_size >= rem_chunk_ptr->size)
      {
         // Update circular buffer size.
         circ_buf_ptr->circ_buf_size -= rem_chunk_ptr->size;
         if (circ_buf_ptr->num_chunks)
         {
            circ_buf_ptr->num_chunks--;
         }
         removable_size -= rem_chunk_ptr->size;

         // Deletes both the chunk node and chunk buffer from the list
         spf_list_delete_node_and_free_obj(&rm_node_ptr, &circ_buf_ptr->head_chunk_ptr, TRUE /* pool_used */);
      }
      else // Remove chunk buffer and replace with a smaller size chunk
      {
         // note that chunk node remains same, just the chunk buffer is replaced with smaller chunk.
         // wr position needs to be updated based on the new chunk size. Unread length and read position will be updated
         // towards the end based on the resultant buffer size and updated write position.

         // for the last chunk, recreate if required
         uint32_t new_chunk_size = rem_chunk_ptr->size - removable_size;

         AR_MSG(DBG_HIGH_PRIO,
                "remove: Replacing last chunk! buf_id=0x%x, removable_size:%lu, cur_chunk_size:%lu, new_chunk_size "
                ":%lu heap_id:%lx",
                circ_buf_ptr->id,
                removable_size,
                rem_chunk_ptr->size,
                new_chunk_size,
                circ_buf_ptr->chunk_heap_id);

         uint32_t        alloc_size = new_chunk_size + sizeof(chunk_buffer_t);
         chunk_buffer_t *new_chunk_ptr =
            (chunk_buffer_t *)posal_memory_malloc(sizeof(uint8_t) * alloc_size, circ_buf_ptr->chunk_heap_id);
         if (NULL == new_chunk_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "Circular Buffer allocation is failed. ");
            result |= CIRCBUF_FAIL;
            break;
         }
         memset(new_chunk_ptr, 0, alloc_size);
         new_chunk_ptr->id         = rem_chunk_ptr->id;
         new_chunk_ptr->size       = new_chunk_size;
         new_chunk_ptr->buffer_ptr = (int8_t *)new_chunk_ptr + sizeof(chunk_buffer_t);

         // reduce the size by only removable_size
         // note that number of chunks remains same
         circ_buf_ptr->circ_buf_size -= removable_size;

         // while loop ends
         removable_size = 0;

         // Need to check if the removed chunk is current chunk or not.
         if (cur_wr_chunk_ptr && cur_wr_chunk_ptr->obj_ptr != rem_chunk_ptr)
         {
            // retain the latest data in the realloacted chunk.
            memscpy(new_chunk_ptr->buffer_ptr,
                    new_chunk_ptr->size,
                    rem_chunk_ptr->buffer_ptr + (rem_chunk_ptr->size - new_chunk_ptr->size),
                    new_chunk_ptr->size);
         }
         else
         {
            // at this point, write offset needs to be adjusted to be within the newly replaced smaller chunk.
            if (circ_buf_ptr->wr_client_ptr && circ_buf_ptr->wr_client_ptr->rw_chunk_offset >= new_chunk_ptr->size)
            {
               /** For example in the below case when chunk size reduces from 4k to 1.9k then
                wr offset=3960 becomes invalid.
                Hence reset the wr offset=0, and copy the latest 1920 bytes behind wr ptr to the new chunk.
                Towards end, rd pointers will be adjusted relatively to the new wr offset.
                     [old chunk 4096: wr offset=3960]
                     [new chunk 1920: wr offset=0] -> need to copy data from old chunk offset [2040, 3960]
               */

               memscpy(new_chunk_ptr->buffer_ptr,
                       new_chunk_ptr->size,
                       rem_chunk_ptr->buffer_ptr + (circ_buf_ptr->wr_client_ptr->rw_chunk_offset - new_chunk_ptr->size),
                       new_chunk_ptr->size);
               // Need to move to next chunk, as replaced chunk has valid data.
               add_circ_buf_next_chunk_node(circ_buf_ptr, &circ_buf_ptr->wr_client_ptr->rw_chunk_node_ptr);
               circ_buf_ptr->wr_client_ptr->rw_chunk_offset = 0;
            }
            else
            {
               // if wr offset is within new chunk size, then no need to update the offset.
               memscpy(new_chunk_ptr->buffer_ptr, new_chunk_ptr->size, rem_chunk_ptr->buffer_ptr, new_chunk_ptr->size);
            }
         }

         // free the current chunk and replace the new chunk in the list
         posal_memory_free(rem_chunk_ptr);
         rm_node_ptr->obj_ptr = new_chunk_ptr;
      }
   } // while()

   // Adjust write byte count based on new size
   if (circ_buf_ptr->write_byte_counter > circ_buf_ptr->circ_buf_size)
   {
      circ_buf_ptr->write_byte_counter = circ_buf_ptr->circ_buf_size;
   }

   // Reset write position if the new circular buffer size is zero
   if ((0 == circ_buf_ptr->circ_buf_size) && circ_buf_ptr->wr_client_ptr)
   {
      _circ_buf_write_client_reset(circ_buf_ptr->wr_client_ptr);
   }

   // Check and update the read positions to avoid reading from the removed chunks.
   for (spf_list_node_t *client_list_ptr = circ_buf_ptr->rd_client_list_ptr; (NULL != client_list_ptr);
        LIST_ADVANCE(client_list_ptr))
   {
      circ_buf_client_t *temp_client_ptr = (circ_buf_client_t *)client_list_ptr->obj_ptr;

      /** adjust the read pointers based on the resultant reduced buffer size and unread bytes*/
      add_circ_buf_read_client_reset(temp_client_ptr, FALSE /** force reset*/);
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "remove: buf_id = 0x%x, removable_size: %u, new circ_buf_size: %u, num_chunks: %u ",
          circ_buf_ptr->id,
          removable_size,
          circ_buf_ptr->circ_buf_size,
          circ_buf_ptr->num_chunks);
#endif

   return CIRCBUF_SUCCESS;
}

static circbuf_result_t _circ_buf_client_resize(circ_buf_t *circ_buf_ptr)
{
   circbuf_result_t result = CIRCBUF_SUCCESS;
   if (NULL == circ_buf_ptr)
   {
      return CIRCBUF_FAIL;
   }

   // Find the max alloc size from the current client list.
   // Note: Read client list is iterated based on FCFS list basis, hence if there are two clients with
   // same buffering requirement but different heap ID, the first client is picked for allocating the buffer.
   uint32_t      max_client_req_size    = 0;
   POSAL_HEAP_ID max_client_req_heap_id = circ_buf_ptr->chunk_heap_id;
   for (spf_list_node_t *client_list_ptr = circ_buf_ptr->rd_client_list_ptr; (NULL != client_list_ptr);
        LIST_ADVANCE(client_list_ptr))
   {
      circ_buf_client_t *temp_client_ptr = (circ_buf_client_t *)client_list_ptr->obj_ptr;

      if (NULL == temp_client_ptr)
      {
         return CIRCBUF_FAIL;
      }

      if (!temp_client_ptr->is_read_client)
      {
         continue;
      }

      uint32_t total_client_requested_size = temp_client_ptr->req_base_buffer_size + temp_client_ptr->req_buffer_resize;

      if (total_client_requested_size > max_client_req_size)
      {
         max_client_req_size    = total_client_requested_size;
         max_client_req_heap_id = temp_client_ptr->chunk_heap_id;
      }
   }

   AR_MSG(DBG_HIGH_PRIO,
          "CIRC_BUF: Found max of resize requests. buf_id = 0x%x, prev_max_req_alloc_size: %u, max_req_alloc_size: %u "
          "heap_id:%lu",
          circ_buf_ptr->id,
          circ_buf_ptr->max_req_buf_resize,
          max_client_req_size,
          max_client_req_heap_id);

   if (max_client_req_size == circ_buf_ptr->max_req_buf_resize)
   {
      // There is no change in client size requriement but there could be change in heap ID.
      // Example: (2 sec, heap_0) client goes away and (2 sec, heap_1) client open
      if (max_client_req_heap_id != circ_buf_ptr->chunk_heap_id)
      {
         // recreate circular buffer in the new heap
         result = circ_buf_realloc_chunks(circ_buf_ptr, max_client_req_heap_id);
         if (CIRCBUF_SUCCESS != result)
         {
            return result;
         }
      }
      return CIRCBUF_SUCCESS;
   }
   else if (max_client_req_size > circ_buf_ptr->max_req_buf_resize)
   {
      // Increase circular buffer,
      //      1. First check if clients heap id is different, recreate current chunks with new clients heap ID.
      //      2. Copy data from prev to new chunks.
      //      3. Allocate additional chunks in the new heap ID
      //
      //  Eg: Consider in dual VA case (2 sec, heap_0) client goes away and (5 sec, heap_1) client opens.
      //      1  First allocate chunks in (2 sec, heap_1)
      //      2. Copy data from chunks of (2 sec, heap_0) to (2 sec, heap_1).
      //      2. Add new chunks of worth (3 sec, heap_1), to make total buffer size (5 sec, heap_1).
      if (max_client_req_heap_id != circ_buf_ptr->chunk_heap_id)
      {
         // recreate circular buffer in the new heap
         result = circ_buf_realloc_chunks(circ_buf_ptr, max_client_req_heap_id);
         if (CIRCBUF_SUCCESS != result)
         {
            return result;
         }
      }

      // add additional chunks with client heap id
      result = _circ_buf_add_chunks(circ_buf_ptr,
                                    max_client_req_size - circ_buf_ptr->max_req_buf_resize,
                                    max_client_req_heap_id);
      if (CIRCBUF_SUCCESS == result)
      {
         circ_buf_ptr->max_req_buf_resize = max_client_req_size;
      }
   }
   else
   {
      // Decrease in circular buffer,
      //      1. Remove chunks as required for the new client.
      //      2. If heap ID for new client is different, recreate the left over chunks in new heap.
      //      3. copy data from prev to new chunks.
      //
      //  Eg: Consider in dual VA case (5 sec, heap_1) client goes away and (2 sec, heap_0) client reamains.
      //      1  First reduce the chunks to make buffer size (2 sec, heap_1).
      //      2. Create new circular buffer chunks (2 sec, heap_0).
      //      2. Copy data from (2 sec, heap_1) to (2 sec, heap_0).
      result = _circ_buf_remove_chunks(circ_buf_ptr, circ_buf_ptr->max_req_buf_resize - max_client_req_size);

      if (CIRCBUF_SUCCESS == result)
      {
         // recreate left over chunks based on the clients heap id
         if (max_client_req_heap_id != circ_buf_ptr->chunk_heap_id)
         {
            // recreate circular buffer in the new heap
            result = circ_buf_realloc_chunks(circ_buf_ptr, max_client_req_heap_id);
            if (CIRCBUF_SUCCESS != result)
            {
               return result;
            }
         }
         circ_buf_ptr->max_req_buf_resize = max_client_req_size;
      }
   }

   return result;
}


///*
// * This function checks if the gap between write_index and read_index is valid
// * Full documentation in circ_buf_utils.h
// */
// static circbuf_result_t _circ_buf_check_if_corrupted(circ_buf_t *frag_circ_buf_ptr)
//{
//   //   int32_t  write_read_gap       = 0;
//   //   uint32_t preferred_chunk_size = frag_circ_buf_ptr->preferred_chunk_size;
//   //   uint32_t circ_buf_read_offset =
//   //      frag_circ_buf_ptr->read_index.chunk_index * preferred_chunk_size +
//   //      frag_circ_buf_ptr->read_index.rw_chunk_offset;
//   //   uint32_t circ_buf_write_offset = frag_circ_buf_ptr->write_index.chunk_index * preferred_chunk_size +
//   //                                    frag_circ_buf_ptr->write_index.rw_chunk_offset;
//   //
//   //   write_read_gap = circ_buf_write_offset - circ_buf_read_offset;
//   //   /* Circ buf read/write is corrupted if the number of bytes between the
//   //    * circ_buf_write_offset and the circ_buf_read_offset is greater than the
//   //    * total circ_buf_size */
//   //   if (write_read_gap < 0)
//   //   {
//   //      write_read_gap += frag_circ_buf_ptr->circ_buf_size;
//   //      if (write_read_gap < 0)
//   //      {
//   //#ifdef DEBUG_CIRC_BUF_UTILS
//   //         AR_MSG(DBG_ERROR_PRIO, "Circ buf read/write corrupted");
//   //#endif
//   //         return CIRCBUF_FAIL;
//   //      }
//   //   }
//   //   if (write_read_gap > (int32_t)frag_circ_buf_ptr->circ_buf_size)
//   //   {
//   //#ifdef DEBUG_CIRC_BUF_UTILS
//   //      AR_MSG(DBG_ERROR_PRIO, "Circ buf read/write corrupted");
//   //#endif
//   //      return CIRCBUF_FAIL;
//   //   }
//   return CIRCBUF_SUCCESS;
//}
