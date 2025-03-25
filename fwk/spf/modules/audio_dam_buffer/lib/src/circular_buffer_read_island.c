/**
 *   \file circular_buffer_read_island.c
 *   \brief
 *        This file contains implementation of circular buffering
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "circular_buffer_i.h"
/*
 * Read samples from the Circular buffer
 * Full documentation in circ_buf_utils.h
 */
circbuf_result_t circ_buf_read(circ_buf_client_t *rd_client_ptr,
                               int8_t *           out_ptr,
                               uint32_t           bytes_to_read,
                               uint32_t *         actual_read_len_ptr)
{
   if ((NULL == out_ptr) || (NULL == rd_client_ptr->rw_chunk_node_ptr) || (NULL == rd_client_ptr->circ_buf_ptr) ||
       (bytes_to_read > rd_client_ptr->circ_buf_ptr->circ_buf_size))
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO,
                    "CIRC_BUF: Read failed. Invalid input params. Rcvd bytes to read = %u",
                    bytes_to_read);
      return CIRCBUF_FAIL;
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG_ISLAND(DBG_HIGH_PRIO,
                 "CIRC_BUF: Circ Buf read, bytes_to_read= %u ; unread_bytes = %u",
                 bytes_to_read,
                 rd_client_ptr->unread_bytes);
#endif

   if (bytes_to_read > rd_client_ptr->unread_bytes)
   {
      if (rd_client_ptr->unread_bytes)
      {
         bytes_to_read = rd_client_ptr->unread_bytes;
         AR_MSG_ISLAND(DBG_LOW_PRIO,
                       "bytes_to_read: %lu greater than unread_bytes: %lu",
                       bytes_to_read,
                       rd_client_ptr->unread_bytes);
      }
      else
      {
#ifdef DEBUG_CIRC_BUF_UTILS
         AR_MSG_ISLAND(DBG_ERROR_PRIO,
                       "Circ Buf underrun, bytes to read = %u, unread bytes = %u",
                       bytes_to_read,
                       rd_client_ptr->unread_bytes);
#endif
         return CIRCBUF_UNDERRUN;
      }
   }

   uint32_t          counter             = bytes_to_read;
   spf_list_node_t **rd_chunk_ptr_ptr    = &rd_client_ptr->rw_chunk_node_ptr;
   uint32_t *        rw_chunk_offset_ptr = &rd_client_ptr->rw_chunk_offset;
   while (counter > 0)
   {
      chunk_buffer_t *chunk_ptr = (chunk_buffer_t *)(*rd_chunk_ptr_ptr)->obj_ptr;

#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG_ISLAND(DBG_HIGH_PRIO,
                    "circ_buf_read: buf_id: 0x%x, chunk:%d, chunk_ptr: 0x%x, offset: %lu, buf_size: %lu, counter: %lu",
                    rd_client_ptr->circ_buf_ptr->id,
                    chunk_ptr->id,
                    chunk_ptr,
                    rd_client_ptr->rw_chunk_offset,
                    chunk_ptr->size,
                    counter);
#endif

      /* If the bytes to read is less than the remaining bytes in the chunk */
      if (counter < (chunk_ptr->size - *rw_chunk_offset_ptr))
      {
         /* Copy the less than chunk size counter */
         *rw_chunk_offset_ptr +=
            memscpy(&out_ptr[bytes_to_read - counter], counter, chunk_ptr->buffer_ptr + *rw_chunk_offset_ptr, counter);
         counter = 0;
      }
      else
      {
         /* Write after the previously read bytes (bytes_to_read - counter) */
         /* Assumes that out_ptr is trusted input with sufficient length */
         counter -= memscpy(&out_ptr[bytes_to_read - counter],
                            counter,
                            chunk_ptr->buffer_ptr + *rw_chunk_offset_ptr,
                            chunk_ptr->size - *rw_chunk_offset_ptr);

         /* Advance to next chunk */
         add_circ_buf_next_chunk_node(rd_client_ptr->circ_buf_ptr, rd_chunk_ptr_ptr);
         *rw_chunk_offset_ptr = 0;
      }
   }

   // Update the client's unread bytes counter.
   rd_client_ptr->unread_bytes -= bytes_to_read;

   *actual_read_len_ptr = bytes_to_read;
   return CIRCBUF_SUCCESS;
}

/*
 * Resets the client read chunk position based on current buffer state.
 * Full documentation in circ_buf_utils.h
 */
circbuf_result_t add_circ_buf_read_client_reset(circ_buf_client_t *client_ptr, bool_t force_reset)
{
   circbuf_result_t result       = CIRCBUF_SUCCESS;
   circ_buf_t *     circ_buf_ptr = client_ptr->circ_buf_ptr;

#ifdef DEBUG_CIRC_BUF_UTILS
   print_chunk_list(circ_buf_ptr);
   print_client_chunk_positions(circ_buf_ptr);
#endif

   // Set the read position same as write position
   if (client_ptr->is_read_client)
   {
      circ_buf_client_t *wr_client_ptr = circ_buf_ptr->wr_client_ptr;
      if (wr_client_ptr)
      {
         // Firstly initialize the read position same as the current write position.
         // And then update the rd position behind wr position by unread_len, which is done by moving rd ptr forward buy
         // (circ buf size - unread_len).
         client_ptr->rw_chunk_node_ptr = wr_client_ptr->rw_chunk_node_ptr;
         client_ptr->rw_chunk_offset   = wr_client_ptr->rw_chunk_offset;

         uint32_t max_data_available_in_buffer = MIN(circ_buf_ptr->write_byte_counter, circ_buf_ptr->circ_buf_size);
         if (force_reset)
         {
            // keeps read and write at the same position.
            client_ptr->unread_bytes = max_data_available_in_buffer;
         }
         else
         {
            // reset unread length if its beyond max data buffered. This can happend if the circular buffer
            // size has been reduced.
            client_ptr->unread_bytes = MIN(client_ptr->unread_bytes, max_data_available_in_buffer);
         }

         // Move the read position behind the write position by unread_bytes.
         // instead of traversing backwards by 'unread_bytes' bytes, traverse forward by 'size - unread_bytes' bytes
         // in circular buffer for ease of implementation.
         if (client_ptr->unread_bytes < circ_buf_ptr->circ_buf_size)
         {
            result =
               add_circ_buf_position_shift_forward(circ_buf_ptr,
                                                   &client_ptr->rw_chunk_node_ptr,
                                                   &client_ptr->rw_chunk_offset,
                                                   circ_buf_ptr->circ_buf_size - circ_buf_ptr->write_byte_counter);
         }
      }
      else
      {
         // Initialize the read position same as the current write position.
         client_ptr->rw_chunk_node_ptr = NULL;
         client_ptr->rw_chunk_offset   = NULL;
      }
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   print_chunk_list(circ_buf_ptr);
   print_client_chunk_positions(circ_buf_ptr);
#endif

   return result;
}

/*
 * Adjusts the read pointer
 * Full documentation in circ_buf_utils.h
 */
circbuf_result_t circ_buf_read_adjust(circ_buf_client_t *rd_client_ptr,
                                      uint32_t           read_offset,
                                      uint32_t *         actual_unread_bytes_ptr,
                                      bool_t             force_adjust)
{
   if (FALSE == rd_client_ptr->is_read_client)
   {
      return CIRCBUF_FAIL;
   }

   // force adjust to be able to read based on data written into buffer, than whats already read.
   // for example, buf size = 2 sec, and rd offset is at 1 sec, now if the force adjust is requested
   // for 1.5 seconds set the read offset to 1.5 seconds.
   //
   // If force adjust is false, read offset will only be 1 seconds even though requested is 1.5 seconds
   if (force_adjust)
   {
      // reset read pointer to write position, and updates unread bytes.
      add_circ_buf_read_client_reset(rd_client_ptr, TRUE /** force reset*/);
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG_ISLAND(DBG_HIGH_PRIO,
                 "circ_buf_read_adjust: Requested read adjust, read_offset= %u, unread_bytes=%u force_adjust=%lu",
                 read_offset,
                 rd_client_ptr->unread_bytes,
                 force_adjust);
#endif

   // shifts from current read position.
   if (read_offset > rd_client_ptr->unread_bytes)
   {
      read_offset = rd_client_ptr->unread_bytes;
   }

   if (actual_unread_bytes_ptr)
   {
      *actual_unread_bytes_ptr = read_offset;
   }

   return add_circ_buf_read_advance(rd_client_ptr, rd_client_ptr->unread_bytes - read_offset);
}
#ifdef DEBUG_CIRC_BUF_UTILS
void print_chunk_list(circ_buf_t *circ_buf_ptr)
{
   spf_list_node_t *head_chunk_ptr = circ_buf_ptr->head_chunk_ptr;

   for (spf_list_node_t *temp_node_chunk_ptr = head_chunk_ptr; (NULL != temp_node_chunk_ptr);
        LIST_ADVANCE(temp_node_chunk_ptr))
   {

      chunk_buffer_t *chunk_ptr = (chunk_buffer_t *)(temp_node_chunk_ptr)->obj_ptr;
      AR_MSG(DBG_MED_PRIO,
             "Circular buffer info: buf_idx = 0x%lx,(chunk= 0x%x, id= %d, size=%d)",
             circ_buf_ptr->id,
             temp_node_chunk_ptr,
             chunk_ptr->id,
             chunk_ptr->size);
   }
}

void print_client_chunk_positions(circ_buf_t *circ_buf_ptr)
{

   for (spf_list_node_t *client_list_ptr = circ_buf_ptr->rd_client_list_ptr; (NULL != client_list_ptr);
        LIST_ADVANCE(client_list_ptr))
   {

      circ_buf_client_t *rd_client_ptr = (circ_buf_client_t *)client_list_ptr->obj_ptr;
      circ_buf_client_t *wr_client_ptr = rd_client_ptr->circ_buf_ptr->wr_client_ptr;

      AR_MSG(DBG_HIGH_PRIO,
             "Reader client:0x%X, current read position is rd(chunk= 0x%x, offset= %u), unread_bytes=%u,"
             "wr(chunk= 0x%x, offset= %u,  write_byte_counter:%d)",
             rd_client_ptr,
             rd_client_ptr->rw_chunk_node_ptr,
             rd_client_ptr->rw_chunk_offset,
             rd_client_ptr->unread_bytes,
             wr_client_ptr->rw_chunk_node_ptr,
             wr_client_ptr->rw_chunk_offset,
             circ_buf_ptr->write_byte_counter);
   }
}
#endif