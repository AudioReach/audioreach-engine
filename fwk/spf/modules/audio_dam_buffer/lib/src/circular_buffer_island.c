/**
 *   \file circular_buffer_island.c
 *   \brief
 *        This file contains implementation of circular buffering
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "circular_buffer_i.h"

// Function detects the overflow in the read clients of the buffer and adjusts the buffer accordingly
static circbuf_result_t _circ_buf_detect_and_handle_overflow(circ_buf_t *circ_buf_ptr, uint32_t bytes_to_write);

/*
 * Helper function to advance read pointer by some amount
 * Full documentation in circ_buf_utils.h
 */
circbuf_result_t add_circ_buf_read_advance(circ_buf_client_t *rd_client_ptr, uint32_t bytes_to_advance)
{
   circbuf_result_t result = CIRCBUF_SUCCESS;

   if ((NULL == rd_client_ptr) || (NULL == rd_client_ptr->rw_chunk_node_ptr))
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_ERROR_PRIO,
             "_circ_buf_read_advance: Reader client position not intialized.",
             bytes_to_advance,
             rd_client_ptr->unread_bytes);
#endif
      return CIRCBUF_FAIL;
   }

   if (bytes_to_advance > rd_client_ptr->unread_bytes)
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_ERROR_PRIO,
             "_circ_buf_read_advance: circ_buf_read_advance fail, needed_size = %u, unread bytes = %u",
             bytes_to_advance,
             rd_client_ptr->unread_bytes);
#endif
      return CIRCBUF_FAIL;
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "circ_buf_read_advance: needed_size = %u, unread bytes = %u",
          bytes_to_advance,
          rd_client_ptr->unread_bytes);
#endif

   result = add_circ_buf_position_shift_forward(rd_client_ptr->circ_buf_ptr,
                                                &rd_client_ptr->rw_chunk_node_ptr,
                                                &rd_client_ptr->rw_chunk_offset,
                                                bytes_to_advance);
   if (CIRCBUF_FAIL == result)
   {
      return result;
   }

   // Update the client's unread bytes counter.
   rd_client_ptr->unread_bytes -= bytes_to_advance;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "circ_buf_read_advance: Done. current read position is (chunk= 0x%x, offset= %u), unread_bytes=%u",
          rd_client_ptr->rw_chunk_node_ptr,
          rd_client_ptr->rw_chunk_offset,
          rd_client_ptr->unread_bytes);
#endif

   return result;
}

/*
 * Write samples to the Circular buffer
 * Note: Since there is no space in circular buffer for writing samples_to_write
 * number of samples, create space by flushing samples_to_write-free_samples
 * number of samples
 * Full documentation in circ_buf_utils.h
 */
circbuf_result_t circ_buf_write(circ_buf_client_t *wr_client_ptr,
                                int8_t *           inp_ptr,
                                uint32_t           bytes_to_write,
                                uint32_t           is_valid_timestamp,
                                int64_t            timestamp)
{
   return add_circ_buf_write_util(wr_client_ptr, inp_ptr, 0, bytes_to_write, is_valid_timestamp, timestamp);
}

/*
 *  Utility to write data to fragmented circular buffer.
 */
circbuf_result_t add_circ_buf_write_util(circ_buf_client_t *wr_client_ptr,
                                         int8_t *           inp_ptr,
                                         uint8_t            memset_data,
                                         uint32_t           bytes_to_write,
                                         uint32_t           is_valid_timestamp,
                                         int64_t            timestamp)
{
   circbuf_result_t result = CIRCBUF_SUCCESS;

   // Check write client validity
   if ((NULL == wr_client_ptr) || (NULL == wr_client_ptr->rw_chunk_node_ptr) || (NULL == wr_client_ptr->circ_buf_ptr))
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_ERROR_PRIO, "_circ_buf_write_util: Writer client position not set.");
#endif
      return CIRCBUF_FAIL;
   }

   circ_buf_t *   circ_buf_ptr  = wr_client_ptr->circ_buf_ptr;
   const uint32_t circ_buf_size = wr_client_ptr->circ_buf_ptr->circ_buf_size;

   if ((bytes_to_write > circ_buf_size))
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_ERROR_PRIO,
             "_circ_buf_write_util: Received invalid bytes to write = %u, max buffer size = %lu ",
             bytes_to_write,
             circ_buf_size);
#endif
      return CIRCBUF_FAIL;
   }

   //   if (CIRCBUF_FAIL == circ_buf_check_if_corrupted(wr_client_ptr))
   //   {
   //#ifdef DEBUG_CIRC_BUF_UTILS
   //      AR_MSG(DBG_ERROR_PRIO, "Circ buf corrupted");
   //#endif
   //      return CIRCBUF_FAIL;
   //   }

   // Detect the move the read pointers in case of overflows.
   result = _circ_buf_detect_and_handle_overflow(circ_buf_ptr, bytes_to_write);

   // data is copied into circ buf and chunk node and offset are updated
   add_circ_buf_data_copy_util(circ_buf_ptr,
                               bytes_to_write,
                               inp_ptr,
                               memset_data,
                               &wr_client_ptr->rw_chunk_node_ptr,
                               &wr_client_ptr->rw_chunk_offset);

   // Update the write byte count in the circular buffer handle.
   wr_client_ptr->circ_buf_ptr->write_byte_counter += bytes_to_write;
   if (circ_buf_ptr->write_byte_counter > circ_buf_size)
   {
      circ_buf_ptr->write_byte_counter = circ_buf_size;
   }

   // Update the timestamp of the latest sample in the buffer.
   // Guard: only store a valid timestamp; zero out the cached value when the
   // incoming timestamp is not valid so that downstream readers never see garbage.
   if (bytes_to_write > 0)
   {
      circ_buf_ptr->is_valid_timestamp = is_valid_timestamp;
      circ_buf_ptr->timestamp          = is_valid_timestamp ? timestamp : 0;
   }

   // Iterate through all the reader clients and update unread byte count.
   for (spf_list_node_t *rd_client_list_ptr = wr_client_ptr->circ_buf_ptr->rd_client_list_ptr;
        (NULL != rd_client_list_ptr);
        LIST_ADVANCE(rd_client_list_ptr))
   {
      circ_buf_client_t *temp_rd_client_ptr = (circ_buf_client_t *)rd_client_list_ptr->obj_ptr;

      // Update the unread byte count,
      // Saturate un_read_bytes count on buffer overflow. This can happen during steady state
      temp_rd_client_ptr->unread_bytes += bytes_to_write;
      if (temp_rd_client_ptr->unread_bytes > temp_rd_client_ptr->circ_buf_ptr->circ_buf_size)
      {
#ifdef DEBUG_CIRC_BUF_UTILS
         AR_MSG(DBG_HIGH_PRIO,
                "_circ_buf_write_util: Buffer overflow detected, buf_id=0x%x, rd_client_id=0x%x ",
                temp_rd_client_ptr->circ_buf_ptr->id,
                temp_rd_client_ptr);
#endif
         temp_rd_client_ptr->unread_bytes = temp_rd_client_ptr->circ_buf_ptr->circ_buf_size;
      }
   }

   return result;
}

circbuf_result_t add_circ_buf_data_copy_util(circ_buf_t *      circ_buf_ptr,
                                             uint32_t          bytes_to_copy,
                                             int8_t *          src_buf_ptr,
                                             uint8_t           memset_data, // will be used only if src_buf_ptr is NULL
                                             spf_list_node_t **wr_chunk_node_pptr,
                                             uint32_t *        wr_chunk_offset_ptr)
{
   uint32_t counter = bytes_to_copy;
   while (counter > 0)
   {
      // get the chunk buffer address and the offset
      chunk_buffer_t *chunk_ptr = (chunk_buffer_t *)(*wr_chunk_node_pptr)->obj_ptr;

#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO,
             "_circ_buf_write_util: buf_id=0x%x chunk= %d, chunk_ptr= 0x%x, offset= %lu, chunk_size= %lu, counter= "
             "%lu ",
             circ_buf_ptr->id,
             chunk_ptr->id,
             chunk_ptr,
             *wr_chunk_offset_ptr,
             chunk_ptr->size,
             counter);
#endif

      // If the bytes to write is less than the remaining bytes in the chunk
      if (counter < (chunk_ptr->size - *wr_chunk_offset_ptr))
      {
         /* Copy the less than chunk size counter.
          * In case of Context hub usecase, write to un-cached address. */
         if (NULL != src_buf_ptr)
         {
            memscpy(chunk_ptr->buffer_ptr + *wr_chunk_offset_ptr,
                    counter,
                    &src_buf_ptr[bytes_to_copy - counter],
                    counter);
         }
         else
         {
            memset(chunk_ptr->buffer_ptr + *wr_chunk_offset_ptr, memset_data, counter);
         }

         *wr_chunk_offset_ptr += counter;

         counter = 0;
      }
      else
      {
         if (NULL != src_buf_ptr)
         {
            memscpy(chunk_ptr->buffer_ptr + *wr_chunk_offset_ptr,
                    (chunk_ptr->size - *wr_chunk_offset_ptr),
                    &src_buf_ptr[bytes_to_copy - counter],
                    (chunk_ptr->size - *wr_chunk_offset_ptr));
         }
         else
         {
            memset(chunk_ptr->buffer_ptr + *wr_chunk_offset_ptr, memset_data, chunk_ptr->size - *wr_chunk_offset_ptr);
         }

         counter -= (chunk_ptr->size - *wr_chunk_offset_ptr);

         // Advance to next chunk
         add_circ_buf_next_chunk_node(circ_buf_ptr, wr_chunk_node_pptr);
         *wr_chunk_offset_ptr = 0;
      }
   }

   return CIRCBUF_SUCCESS;
}

/*
 * Helper function to advance clients read/write position forward by given bytes.
 * Full documentation in circ_buf_utils.h
 */
circbuf_result_t add_circ_buf_position_shift_forward(circ_buf_t *      circ_buf_ptr,
                                                     spf_list_node_t **rd_chunk_list_pptr,
                                                     uint32_t *        rw_chunk_offset_ptr,
                                                     uint32_t          bytes_to_advance)
{

   // Fail if bytes to shift forward is greater than circular buffer size.
   if ((bytes_to_advance > circ_buf_ptr->circ_buf_size) || (NULL == rd_chunk_list_pptr) ||
       (NULL == rw_chunk_offset_ptr))
   {
      return CIRCBUF_FAIL;
   }

   // No need to shift if bytes to advance is zero or circ buf size.
   if (bytes_to_advance == circ_buf_ptr->circ_buf_size || (0 == bytes_to_advance))
   {
      return CIRCBUF_SUCCESS;
   }

   uint32_t counter = bytes_to_advance;
   while (counter > 0)
   {
      chunk_buffer_t *chunk_ptr = (chunk_buffer_t *)(*rd_chunk_list_pptr)->obj_ptr;

      // If the bytes to shift is less than the remaining bytes in the chunk
      if (counter < (chunk_ptr->size - *rw_chunk_offset_ptr))
      {
         // Copy the less than chunk size counter
         *rw_chunk_offset_ptr += counter;
         counter = 0;
      }
      else
      {
         // Decrement the bytes in the chunk
         counter -= (chunk_ptr->size - *rw_chunk_offset_ptr);

         // Move to next chunk
         add_circ_buf_next_chunk_node(circ_buf_ptr, rd_chunk_list_pptr);
         *rw_chunk_offset_ptr = 0;
      }
   }

   return CIRCBUF_SUCCESS;
}

// Function detects the overflow in the read clients of the buffer and adjusts the buffer accordingly
static circbuf_result_t _circ_buf_detect_and_handle_overflow(circ_buf_t *circ_buf_ptr, uint32_t bytes_to_write)
{
   circbuf_result_t result = CIRCBUF_SUCCESS;

   for (spf_list_node_t *rd_client_list_ptr = circ_buf_ptr->rd_client_list_ptr; (NULL != rd_client_list_ptr);
        LIST_ADVANCE(rd_client_list_ptr))
   {
      circ_buf_client_t *temp_rd_client_ptr = (circ_buf_client_t *)rd_client_list_ptr->obj_ptr;

      uint32_t free_bytes = circ_buf_ptr->circ_buf_size - temp_rd_client_ptr->unread_bytes;
      if (bytes_to_write > free_bytes)
      {
         /* Since there is no space in circular buffer for writing samples_to_write
          number of samples, create space by flushing samples_to_write-free_samples
          number of samples */
         add_circ_buf_read_advance(temp_rd_client_ptr, bytes_to_write - free_bytes);

// add a a flag to each reader check if the overrun happened when gate is opened.
#ifdef DEBUG_CIRC_BUF_UTILS
         AR_MSG(DBG_HIGH_PRIO,
                "CIRC_BUF: Detected Overrun. client: 0x%x, bytes to write = %u, free_bytes = %u",
                (uint32_t)temp_rd_client_ptr,
                bytes_to_write,
                free_bytes);
#endif
         result = CIRCBUF_OVERRUN;
      }
   }

   return result;
}

circbuf_result_t add_circ_buf_next_chunk_node(circ_buf_t *circ_buf_ptr, spf_list_node_t **chunk_ptr_ptr)
{
   if ((NULL == chunk_ptr_ptr) || (NULL == *chunk_ptr_ptr) || (NULL == circ_buf_ptr))
   {
      return CIRCBUF_FAIL;
   }

   // Move to the next chunk in the list if it exist. If the next pointer doesnt exist, it means the node is the tail
   // so assign the head as the chunk pointer

   if ((*chunk_ptr_ptr)->next_ptr)
   {
      (*chunk_ptr_ptr) = (*chunk_ptr_ptr)->next_ptr;
   }
   else
   {
      (*chunk_ptr_ptr) = circ_buf_ptr->head_chunk_ptr;
   }

   return CIRCBUF_SUCCESS;
}