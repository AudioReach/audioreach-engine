/**
 * \file spf_circular_buffer_utils.c
 * \brief
 *    This file contains implementation of circular buffering utilities.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*==============================================================================
   Headers and Includes.
==============================================================================*/

#include "ar_msg.h"
#include "spf_list_utils.h"
#include "spf_circular_buffer_i.h"

/*==============================================================================
   Local utility Functions
==============================================================================*/

/*==============================================================================
   Public Function Implementation
==============================================================================*/

// Helper function to advance clients read/write position forward by given bytes.
spf_circ_buf_result_t _circ_buf_inc_write_byte_counter(spf_circ_buf_t *circ_buf_ptr, uint32_t bytes)
{
   circ_buf_ptr->write_byte_counter += bytes;
   if (circ_buf_ptr->write_byte_counter > circ_buf_ptr->circ_buf_size_bytes)
   {
      circ_buf_ptr->write_byte_counter = circ_buf_ptr->circ_buf_size_bytes;
   }

   return SPF_CIRCBUF_SUCCESS;
}

// Helper function to advance clients read/write position forward by given bytes.
// Advances the the client currents position frame by frame.
// TODO:<RV> Do we need to handle metadata when moving read pointer due to overflow ?
spf_circ_buf_result_t _circ_buf_position_shift_forward(spf_circ_buf_client_t *client_ptr,
                                                       uint32_t               bytes_to_advance,
                                                       bool_t                 need_to_handle_metadata)
{

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "shift_forward: buf_id: 0x%x, bytes_to_advance: %lu",
          client_ptr->circ_buf_ptr->id,
          bytes_to_advance);
#endif

   // if bytes to advance is zero or circular buffer size. the position will remain the current position
   // in that case we dont need to move the pointer.
   if (bytes_to_advance == 0 || (bytes_to_advance == client_ptr->circ_buf_ptr->circ_buf_size_bytes))
   {
      return SPF_CIRCBUF_SUCCESS;
   }

   uint32_t bytes_left_to_advance = bytes_to_advance;
   while (bytes_left_to_advance > 0)
   {
      spf_circ_buf_frame_t *cur_frame_ptr = get_frame_ptr(&client_ptr->rw_pos, DEFAULT_CH_IDX);

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
      AR_MSG(DBG_HIGH_PRIO,
             "shift_forward: buf_id: 0x%lx, frame_position: 0x%x, frame_offset: %lu, bytes_left_to_advance: %lu, "
             "actual_data_len: %lu",
             client_ptr->circ_buf_ptr->id,
             client_ptr->rw_pos.frame_position,
             client_ptr->rw_pos.frame_offset,
             bytes_left_to_advance,
             cur_frame_ptr->actual_data_len);
#endif

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
      spf_circ_buf_chunk_t *chunk_ptr = (spf_circ_buf_chunk_t *)client_ptr->rw_pos.chunk_node_ptr->obj_ptr;
      AR_MSG(DBG_HIGH_PRIO,
             "shift_forward: buf_id: 0x%lx, chunk_ptr: 0x%x, chunk_id: 0x%lx, chunk_size: %lu",
             client_ptr->circ_buf_ptr->id,
             chunk_ptr,
             chunk_ptr->id,
             chunk_ptr->size);
#endif

      if (client_ptr->rw_pos.frame_offset > cur_frame_ptr->actual_data_len)
      {
         return SPF_CIRCBUF_FAIL;
      }

      /* If the bytes to advance is less than the remaining bytes in the chunk */
      uint32_t bytes_left_in_cur_frame = cur_frame_ptr->actual_data_len - client_ptr->rw_pos.frame_offset;
      if (bytes_left_to_advance < bytes_left_in_cur_frame)
      {
         /* Copy the less than chunk size counter */
         client_ptr->rw_pos.frame_offset += bytes_left_to_advance;
         bytes_left_to_advance = 0;
      }
      else
      {
         /*  */
         bytes_left_to_advance -= bytes_left_in_cur_frame;

         /* Advance the clients current position to next frame */
         _circ_buf_advance_to_next_frame(client_ptr->circ_buf_ptr, &client_ptr->rw_pos);
      }
   }

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
   spf_circ_buf_chunk_t *chunk_ptr = (spf_circ_buf_chunk_t *)client_ptr->rw_pos.chunk_node_ptr->obj_ptr;

   AR_MSG(DBG_HIGH_PRIO,
          "shift_forward: New clients position.chunk: 0x%x, chunkp: 0x%x, frame_position: 0x%x, frame_offset: %lu",
          chunk_ptr->id,
          chunk_ptr,
          client_ptr->rw_pos.frame_position,
          client_ptr->rw_pos.frame_offset);
#endif

   return SPF_CIRCBUF_SUCCESS;
}

// allocate chunk function
spf_circ_buf_chunk_t *_circ_buf_allocate_chunk(spf_circ_buf_t *        circ_buf_ptr,
                                               uint32_t                frame_data_size_in_bytes,
                                               uint32_t                chunk_size,
                                               spf_circ_buf_mf_info_t *mf_ptr)
{
   uint32_t chunk_hdr_size = sizeof(spf_circ_buf_chunk_t) + sizeof(int8_t *) * mf_ptr->num_channels;

   spf_circ_buf_chunk_t *chunk_hdr_ptr =
      (spf_circ_buf_chunk_t *)posal_memory_malloc(chunk_hdr_size, circ_buf_ptr->heap_id);
   if (NULL == chunk_hdr_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Chunk allocation is failed size: %lu ", chunk_hdr_size);
      return NULL;
   }
   memset(chunk_hdr_ptr, 0, chunk_hdr_size);

   chunk_hdr_ptr->num_channels = mf_ptr->num_channels;
   for (uint32_t ch_idx = 0; ch_idx < mf_ptr->num_channels; ch_idx++)
   {
      int8_t *ch_buf_ptr = (int8_t *)posal_memory_malloc(chunk_size, circ_buf_ptr->heap_id);
      if (NULL == ch_buf_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "Chunk allocation is failed size: %lu ", chunk_size);
         return NULL;
      }
      memset(ch_buf_ptr, 0, chunk_size);

      // assign ch buf ptr in the array
      chunk_hdr_ptr->buf_ptr[ch_idx] = ch_buf_ptr;
   }

   chunk_hdr_ptr->num_frames = chunk_size / GET_CHUNK_FRAME_SIZE(frame_data_size_in_bytes);

   chunk_hdr_ptr->frame_size = frame_data_size_in_bytes;

   chunk_hdr_ptr->id   = _circ_buf_get_new_chunk_id(circ_buf_ptr);
   chunk_hdr_ptr->size = chunk_size;

   // assign media format to the chunk.
   chunk_hdr_ptr->mf = mf_ptr;
   incr_mf_ref_count(circ_buf_ptr, chunk_hdr_ptr->mf);

   AR_MSG(DBG_HIGH_PRIO,
          "alloc_chunk: buf_id: 0x%lx chunk_ptr: 0x%x, size: %lu, frame_size: %lu, mf: 0x%lx, num_ch: 0x%lx ",
          circ_buf_ptr->id,
          chunk_hdr_ptr,
          chunk_hdr_ptr->size,
          chunk_hdr_ptr->frame_size,
          chunk_hdr_ptr->mf,
          chunk_hdr_ptr->num_channels);

   return chunk_hdr_ptr;
}

/*
 * Helper function to advance read pointer by some amount
 * Full documentation in circ_buf_utils.h
 */
spf_circ_buf_result_t _circ_buf_read_advance(spf_circ_buf_client_t *rd_client_ptr, uint32_t bytes_to_advance)
{
   spf_circ_buf_result_t result = SPF_CIRCBUF_SUCCESS;

   if ((NULL == rd_client_ptr) || (NULL == rd_client_ptr->rw_pos.chunk_node_ptr))
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_ERROR_PRIO, "read_advance: Reader client position not intialized.");
#endif
      return SPF_CIRCBUF_FAIL;
   }

   if (bytes_to_advance > rd_client_ptr->unread_bytes)
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_ERROR_PRIO,
             "read_advance:  Failed. needed_size = %u, unread bytes = %u",
             bytes_to_advance,
             rd_client_ptr->unread_bytes);
#endif
      return SPF_CIRCBUF_FAIL;
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "read_advance: needed_size = %u, unread bytes = %u",
          bytes_to_advance,
          rd_client_ptr->unread_bytes);
#endif

   // TODO: do we need to handle metadata when moving read pointer due to overflow or writer will take care of it ?
   result = _circ_buf_position_shift_forward(rd_client_ptr, bytes_to_advance, FALSE);
   if (SPF_CIRCBUF_FAIL == result)
   {
      return result;
   }

   // Update the client's unread bytes counter.
   rd_client_ptr->unread_bytes -= bytes_to_advance;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "read_advance: Done. current read position is (chunk= 0x%x, frame=0x%x offset= %u), unread_bytes=%u",
          rd_client_ptr->rw_pos.chunk_node_ptr->obj_ptr,
          rd_client_ptr->rw_pos.frame_position,
          rd_client_ptr->rw_pos.frame_offset,
          rd_client_ptr->unread_bytes);
#endif

   return result;
}

void _circ_buf_compute_chunk_info(spf_circ_buf_t *        circ_buf_ptr,
                                  spf_circ_buf_mf_info_t *mf_ptr,
                                  uint32_t                buffer_size_in_us,
                                  uint32_t *              total_num_frames_ptr,
                                  uint32_t *              frame_data_size_in_bytes_ptr,
                                  uint32_t *              total_num_chunks_ptr,
                                  uint32_t *              actual_chunk_size_ptr,
                                  uint32_t *              last_chunk_size_ptr)
{
   uint32_t total_num_chunks         = 0;
   uint32_t actual_chunk_size        = 0;
   uint32_t last_chunk_size          = 0;
   uint32_t frame_data_size_in_bytes = 0;
   uint32_t total_num_frames         = 0;

   uint32_t buffer_size_in_bytes = convert_us_to_bytes(buffer_size_in_us, mf_ptr);

   // Compute frame size and additional size based writers mf.
   frame_data_size_in_bytes = convert_us_to_bytes(circ_buf_ptr->wr_client_ptr->container_frame_size_in_us, mf_ptr);
   uint32_t total_frame_size_in_bytes = GET_CHUNK_FRAME_SIZE(frame_data_size_in_bytes);

   // convert the additional size into ceil of container frame size. since buffer is operated at frame boundary.
   total_num_frames = _CEIL(buffer_size_in_bytes, frame_data_size_in_bytes);

   // compute num frames per chunk
   uint32_t num_frames_per_chunk = _CEIL(circ_buf_ptr->preferred_chunk_size, total_frame_size_in_bytes);

   // get actual chunk size
   actual_chunk_size = num_frames_per_chunk * total_frame_size_in_bytes;

   // compute number of chunks needed to be added from total number of frames.
   total_num_chunks = _CEIL(total_num_frames, num_frames_per_chunk);

   if (total_num_frames % num_frames_per_chunk)
   {
      last_chunk_size = total_frame_size_in_bytes * (total_num_frames % num_frames_per_chunk);
   }
   else
   {
      last_chunk_size = actual_chunk_size;
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "compute_chunk_info: actual_chunk_size: %lu no new_chunks: %lu "
          "num new frames: %lu, last_chunk_size: %lu frame_size: %lu, frames_per_chunk: %lu",
          actual_chunk_size,
          total_num_chunks,
          total_num_frames,
          last_chunk_size,
          frame_data_size_in_bytes,
          num_frames_per_chunk);
#endif

   if (total_num_frames_ptr)
   {
      *total_num_frames_ptr = total_num_frames;
   }

   if (frame_data_size_in_bytes_ptr)
   {
      *frame_data_size_in_bytes_ptr = frame_data_size_in_bytes;
   }

   if (total_num_chunks_ptr)
   {
      *total_num_chunks_ptr = total_num_chunks;
   }

   if (actual_chunk_size_ptr)
   {
      *actual_chunk_size_ptr = actual_chunk_size;
   }

   if (last_chunk_size_ptr)
   {
      *last_chunk_size_ptr = last_chunk_size;
   }
}

spf_circ_buf_result_t _circ_buf_expand_buffer(spf_circ_buf_t *circ_buf_ptr, uint32_t additional_size_in_us)
{

   spf_circ_buf_result_t res = SPF_CIRCBUF_SUCCESS;

   if (additional_size_in_us == 0)
   {
      return res;
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "add_chunks: Add chunks. buf_id = 0x%x,  additional_size: %u, prev_circ_buf_size: %u, prev_num_chunks: %u ",
          circ_buf_ptr->id,
          additional_size_in_us,
          circ_buf_ptr->circ_buf_size_bytes,
          circ_buf_ptr->num_chunks);
#endif

   // Create chunks based on the current writers media format.
   spf_circ_buf_mf_info_t *wr_mf_ptr = circ_buf_ptr->wr_client_ptr->operating_mf;

   // compute chunk info
   uint32_t num_additional_chunks    = 0;
   uint32_t actual_chunk_size        = 0;
   uint32_t first_chunk_size         = 0;
   uint32_t frame_data_size_in_bytes = 0;
   uint32_t num_additional_frames    = 0;
   _circ_buf_compute_chunk_info(circ_buf_ptr,
                                wr_mf_ptr,
                                additional_size_in_us,
                                &num_additional_frames,
                                &frame_data_size_in_bytes,
                                &num_additional_chunks,
                                &actual_chunk_size,
                                &first_chunk_size);

   // Adds chunks to the circular buffer after current writers position.
   res = _circ_buf_add_chunks(circ_buf_ptr,
                        num_additional_chunks,
                        num_additional_frames,
                        frame_data_size_in_bytes,
                        wr_mf_ptr,
                        first_chunk_size,
                        actual_chunk_size);
   if (SPF_CIRCBUF_SUCCESS != res)
   {
#ifdef DEBUG_CIRC_BUF_UTILS
     AR_MSG(DBG_ERROR_PRIO, "add_chunks: error while adding chunks, result %lu ", res);
#endif
       return res;
   }
   circ_buf_ptr->circ_buf_size_in_us +=
      (num_additional_frames * circ_buf_ptr->wr_client_ptr->container_frame_size_in_us);

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "add_chunks: Done. buf_id = 0x%x, additional_size: %u, circ_buf_size_bytes:%lu, circ_buf_size_us: %lu, "
          "num_chunks: %u ",
          circ_buf_ptr->id,
          additional_size_in_us,
          circ_buf_ptr->circ_buf_size_bytes,
          circ_buf_ptr->circ_buf_size_in_us,
          circ_buf_ptr->num_chunks);
#endif

   return res;
}

spf_circ_buf_result_t _circ_buf_shrink_buffer(spf_circ_buf_t *circ_buf_ptr, uint32_t removable_size_in_us)
{

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "CIRC_BUF: Remove chunks. buf_id = 0x%x, removable_size: %u, new_circ_buf_size: %u, num_chunks: %u ",
          circ_buf_ptr->id,
          removable_size_in_us,
          circ_buf_ptr->circ_buf_size_bytes,
          circ_buf_ptr->num_chunks);
#endif

   if (removable_size_in_us == 0)
   {
      return SPF_CIRCBUF_SUCCESS;
   }

   uint32_t num_chunks_removed_till_now = 0;
   while (removable_size_in_us)
   {
      spf_list_node_t *temp_ptr = NULL;

      // Remove the chunk after the current write pointer.
      if (circ_buf_ptr->wr_client_ptr && circ_buf_ptr->wr_client_ptr->rw_pos.chunk_node_ptr)
      {
         spf_list_node_t *cur_wr_chunk_ptr = circ_buf_ptr->wr_client_ptr->rw_pos.chunk_node_ptr;

         if (cur_wr_chunk_ptr->next_ptr)
         {
            temp_ptr = cur_wr_chunk_ptr->next_ptr;
         }
      }

      // If the current write node is the tail, assign the head node as temp.
      if (NULL == temp_ptr)
      {
         temp_ptr = circ_buf_ptr->head_chunk_ptr;
      }

      spf_circ_buf_chunk_t *rem_chunk_ptr = (spf_circ_buf_chunk_t *)temp_ptr->obj_ptr;

      uint32_t num_frames_in_cur_chunk = rem_chunk_ptr->size / GET_CHUNK_FRAME_SIZE(rem_chunk_ptr->frame_size);

      uint32_t cur_chunk_frame_size_in_us = convert_bytes_to_us(rem_chunk_ptr->frame_size, rem_chunk_ptr->mf);
      uint32_t cur_chunk_size_in_us       = num_frames_in_cur_chunk * cur_chunk_frame_size_in_us;

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
      AR_MSG(DBG_HIGH_PRIO,
             "remove_chunks: buf_id = 0x%x, removable_size_in_us: %lu, new_circ_buf_size: %u, num_chunks: %u "
             "num_chunks_removed_till_now: %lu chunk_size_in_us: %lu chunk_frame_sz : %lu",
             circ_buf_ptr->id,
             removable_size_in_us,
             circ_buf_ptr->circ_buf_size_bytes,
             circ_buf_ptr->num_chunks,
             num_chunks_removed_till_now,
             cur_chunk_size_in_us,
             cur_chunk_frame_size_in_us);
#endif

      if (removable_size_in_us >= cur_chunk_size_in_us)
      {
         // remove only data frame size from the removable size.
         removable_size_in_us -= cur_chunk_size_in_us;

         // Update circular buffer size.
         circ_buf_ptr->circ_buf_size_bytes -= (num_frames_in_cur_chunk * rem_chunk_ptr->frame_size);
         circ_buf_ptr->circ_buf_size_in_us -= cur_chunk_size_in_us;

         if (circ_buf_ptr->num_chunks)
         {
            circ_buf_ptr->num_chunks--;
         }

         // Delete the node from the list
         spf_list_find_delete_node(&circ_buf_ptr->head_chunk_ptr, rem_chunk_ptr, TRUE);

         // Free metadata in the chunk
         _circ_buf_free_chunk(circ_buf_ptr, rem_chunk_ptr);

         num_chunks_removed_till_now++;
      }
      else // else is for the last chunk being removed.
      {
         // if number of frames to remove is less than the frames in chunk then reallocate the chunk with
         // Remaining frames.
         // TODO:<RV> Currently destroying all the metadata in the chunk do we need to destroy keep the
         //       metadata from remaining frames ? maybe useful in DAM module where clients dynamically resizing buffers
         //       ?
         uint32_t size_left_in_us          = cur_chunk_size_in_us - removable_size_in_us;
         uint32_t num_frames_left_in_chunk = _CEIL(size_left_in_us, cur_chunk_frame_size_in_us);
         uint32_t num_frames_removed       = (num_frames_in_cur_chunk - num_frames_left_in_chunk);
         uint32_t replaced_chunk_size      = num_frames_left_in_chunk * GET_CHUNK_FRAME_SIZE(rem_chunk_ptr->frame_size);

         // Remove the size corresponding to removed frames in the chunk
         circ_buf_ptr->circ_buf_size_bytes -= (num_frames_removed * rem_chunk_ptr->frame_size);
         circ_buf_ptr->circ_buf_size_in_us -= (num_frames_removed * cur_chunk_frame_size_in_us);

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
         AR_MSG(DBG_HIGH_PRIO,
                "remove_chunks: buf_id = 0x%x. Partial chunk removal. removable_size_in_us: %lu, new_circ_buf_size: "
                "%u, num_chunks: %u "
                " num_frames_removed: %lu num_frames_left_in_chunk : %lu",
                circ_buf_ptr->id,
                removable_size_in_us,
                circ_buf_ptr->circ_buf_size_bytes,
                circ_buf_ptr->num_chunks,
                num_frames_removed,
                num_frames_left_in_chunk);
#endif

         // cache mf this will replaced in the new chunk, dont decrement ref count since it will used in
         // in the replaced chunk
         spf_circ_buf_mf_info_t *prev_mf_ptr = rem_chunk_ptr->mf;

         // allocate chunk function
         spf_circ_buf_chunk_t *new_chunk_ptr =
            _circ_buf_allocate_chunk(circ_buf_ptr, rem_chunk_ptr->frame_size, replaced_chunk_size, prev_mf_ptr);
         if (!new_chunk_ptr)
         {
            return SPF_CIRCBUF_FAIL;
         }

         // Replace the new chunk pointer in the list node.
         temp_ptr->obj_ptr = (spf_circ_buf_chunk_t *)new_chunk_ptr;

         // frees metadata and also the chunk buffer.
         _circ_buf_free_chunk(circ_buf_ptr, rem_chunk_ptr);

         // No need to update chunk count, no of chunks remain same.
      }
   }

   // Reset write position if the new circular buffer size is zero
   if ((0 == circ_buf_ptr->circ_buf_size_bytes) && circ_buf_ptr->wr_client_ptr)
   {
      _circ_buf_write_client_reset(circ_buf_ptr->wr_client_ptr);
   }

   // Adjust write byte count based on new size
   if (circ_buf_ptr->write_byte_counter > circ_buf_ptr->circ_buf_size_bytes)
   {
      circ_buf_ptr->write_byte_counter = circ_buf_ptr->circ_buf_size_bytes;
   }

   // Check and update the read positions to avoid reading from the removed chunks.
   for (spf_list_node_t *client_list_ptr = circ_buf_ptr->rd_client_list_ptr; (NULL != client_list_ptr);
        LIST_ADVANCE(client_list_ptr))
   {
      spf_circ_buf_client_t *temp_client_ptr = (spf_circ_buf_client_t *)client_list_ptr->obj_ptr;

      if ((temp_client_ptr->unread_bytes > circ_buf_ptr->circ_buf_size_bytes) ||
          (temp_client_ptr->unread_bytes > circ_buf_ptr->write_byte_counter))
      {
         _circ_buf_read_client_reset(temp_client_ptr);
      }
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "CIRC_BUF: Remove chunks. buf_id = 0x%x, removable_size: %u, new_circ_buf_size: %u, num_chunks: %u ",
          circ_buf_ptr->id,
          removable_size_in_us,
          circ_buf_ptr->circ_buf_size_bytes,
          circ_buf_ptr->num_chunks);
#endif

   return SPF_CIRCBUF_SUCCESS;
}

spf_circ_buf_result_t _circ_buf_client_resize(spf_circ_buf_t *circ_buf_ptr)
{
   spf_circ_buf_result_t result = SPF_CIRCBUF_SUCCESS;
   if (NULL == circ_buf_ptr)
   {
      return SPF_CIRCBUF_FAIL;
   }

   if (!circ_buf_ptr->wr_client_ptr || NULL == circ_buf_ptr->wr_client_ptr->operating_mf ||
       0 == circ_buf_ptr->wr_client_ptr->container_frame_size_in_us)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "CIRC_BUF_%lu: Not ready to resize, writer client 0x%lx may not be registered yet or mf is not received. ",
             circ_buf_ptr->id,
             circ_buf_ptr->wr_client_ptr);

      return SPF_CIRCBUF_SUCCESS;
   }

   // Find the max alloc size from the current client list.
   uint32_t max_read_client_req_size_us = 0;

   for (spf_list_node_t *client_list_ptr = circ_buf_ptr->rd_client_list_ptr; (NULL != client_list_ptr);
        LIST_ADVANCE(client_list_ptr))
   {
      spf_circ_buf_client_t *temp_client_ptr = (spf_circ_buf_client_t *)client_list_ptr->obj_ptr;

      if (NULL == temp_client_ptr)
      {
         return SPF_CIRCBUF_FAIL;
      }

      if (!temp_client_ptr->is_read_client)
      {
         continue;
      }

      uint32_t total_client_requested_size_us =
         temp_client_ptr->init_base_buffer_size_in_us + temp_client_ptr->dynamic_resize_req_in_us;

      if (total_client_requested_size_us > max_read_client_req_size_us)
      {
         max_read_client_req_size_us = total_client_requested_size_us;
      }
   }

   // Find the max alloc size from the current client list.
   uint32_t write_client_req_size_us = 0;
   if (circ_buf_ptr->wr_client_ptr)
   {
      write_client_req_size_us = circ_buf_ptr->wr_client_ptr->init_base_buffer_size_in_us +
                                 circ_buf_ptr->wr_client_ptr->dynamic_resize_req_in_us;
   }

   uint32_t total_required_buf_size = write_client_req_size_us + max_read_client_req_size_us;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "resize: buf_id = 0x%x Found max of resize requests.  circ_buf_size_in_us: %u, "
          "max_read_client_req_size_us: %u, write_client_req_size_us: %lu ",
          circ_buf_ptr->id,
          circ_buf_ptr->circ_buf_size_in_us,
          max_read_client_req_size_us,
          write_client_req_size_us);
#endif

   if (total_required_buf_size == circ_buf_ptr->circ_buf_size_in_us)
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO, "resize: buf_id = 0x%x, No need to resize the buffer, returning.", circ_buf_ptr->id);
#endif
      return SPF_CIRCBUF_SUCCESS;
   }

   if (total_required_buf_size > circ_buf_ptr->circ_buf_size_in_us)
   {
      uint32_t additional_size_in_us = total_required_buf_size - circ_buf_ptr->circ_buf_size_in_us;
      result                         = _circ_buf_expand_buffer(circ_buf_ptr, additional_size_in_us);
   }
   else
   {
      uint32_t removable_size_in_us = total_required_buf_size - circ_buf_ptr->circ_buf_size_in_us;
      result                        = _circ_buf_shrink_buffer(circ_buf_ptr, removable_size_in_us);
   }

   return result;
}

spf_circ_buf_result_t _circ_buf_write_one_frame(spf_circ_buf_client_t *wr_client_ptr,
                                                capi_stream_data_t *   in_sdata_ptr,
                                                bool_t                 allow_overflow,
                                                uint32_t *             memeset_value_ptr)
{
   spf_circ_buf_result_t res = SPF_CIRCBUF_SUCCESS;

   if (!wr_client_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Null write client ");
      return SPF_CIRCBUF_FAIL;
   }

   // get the chunk buffer address and the offset
   spf_circ_buf_chunk_t *chunk_ptr    = (spf_circ_buf_chunk_t *)wr_client_ptr->rw_pos.chunk_node_ptr->obj_ptr;
   spf_circ_buf_t *      circ_buf_ptr = wr_client_ptr->circ_buf_ptr;
   uint32_t              actual_data_len_consumed    = 0;
   capi_buf_t *          buf_ptr                     = in_sdata_ptr->buf_ptr;
   uint32_t              bytes_to_write              = buf_ptr[DEFAULT_CH_IDX].actual_data_len;
   uint32_t              initial_len_per_ch_in_bytes = buf_ptr[DEFAULT_CH_IDX].actual_data_len;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "write_one_frame: buf_id=0x%x capi_sdata_info: flags: 0x%lx timestamp: %lu num_bufs: %lu buf_size: %lu",
          circ_buf_ptr->id,
          in_sdata_ptr->flags.word,
          in_sdata_ptr->timestamp,
          in_sdata_ptr->bufs_num,
          bytes_to_write);
#endif

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "write_one_frame: buf_id=0x%x chunk: %d, chunk_ptr: 0x%x, frame_position:%lu  chunk_size: %lu "
          "bytes, chunk_num_ch: "
          "%lu",
          circ_buf_ptr->id,
          chunk_ptr->id,
          chunk_ptr,
          wr_client_ptr->rw_pos.frame_position,
          chunk_ptr->size,
          chunk_ptr->num_channels);
#endif

   bool_t did_recreate_buf = FALSE;

   // If there's multiple chunks marked for destroy/recreate in a row, we need to recreate all of them before writing
   // any data.
   do
   {
      // Check and recreate the chunk if required.
      res = _circ_buf_check_and_recreate(wr_client_ptr, allow_overflow, &did_recreate_buf);
   } while (did_recreate_buf);

   // Update chunk ptr after check and recreate.
   chunk_ptr = (spf_circ_buf_chunk_t *)wr_client_ptr->rw_pos.chunk_node_ptr->obj_ptr;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "write_one_frame: buf_id=0x%x chunk: %d, chunk_ptr: 0x%x, frame_position:%lu  chunk_size: %lu "
          "bytes, chunk_num_ch: "
          "%lu",
          circ_buf_ptr->id,
          chunk_ptr->id,
          chunk_ptr,
          wr_client_ptr->rw_pos.frame_position,
          chunk_ptr->size,
          chunk_ptr->num_channels);
#endif

   // Consider that the writer is faster than the reader. If media format changes, the writer might catch up to
   // reader and
   // try to recreate a chunk which reader is still reading. This would cause a data drop for the reader. Therefore
   // for the
   // allow_overflow is FALSE case, when the writer catches up to the reader's chunk (and chunk needs to be
   // recreated), wait
   // for the reader to move to the next chunk before writing more data.
   // If the reader as depleted all data, we don't care if it's in the same chunk as the writer.
   if ((!allow_overflow) && (SPF_CIRCBUF_OVERRUN == res))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "write_one_frame: buf_id=0x%x overflow was found when checking to recreate buffers. not consuming input.",
             circ_buf_ptr->id);
      res = SPF_CIRCBUF_SUCCESS;

      for (uint32_t ch_idx = 0; ch_idx < in_sdata_ptr->bufs_num; ch_idx++)
      {
         buf_ptr[ch_idx].actual_data_len = 0;
      }

      return res;
   }

   // Since writes are per frame, we always write into new frame, so current write frame position offset should be
   // zero. we never write partial data into a frame.
   if (wr_client_ptr->rw_pos.frame_offset > 0)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "write_one_frame: buf_id=0x%x chunk= %d, chunk_ptr= 0x%x, offset= %lu, chunk_size= %lu",
             circ_buf_ptr->id,
             chunk_ptr->id,
             chunk_ptr,
             wr_client_ptr->rw_pos.frame_offset,
             chunk_ptr->size);

      return SPF_CIRCBUF_FAIL;
   }

   // Check if the frame can be fit in the current chunk frame.
   if (bytes_to_write > chunk_ptr->frame_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "write_one_frame: buf_id=0x%x actual_data_len=%lu, chunk_frame_size=%lu ",
             circ_buf_ptr->id,
             bytes_to_write,
             chunk_ptr->frame_size);

      bytes_to_write = chunk_ptr->frame_size;
   }

   /* Write data to the circular buffer frame */
   {
      // Detect overflow and move readers to next frame.
      res = _circ_buf_detect_and_handle_overflow(wr_client_ptr, allow_overflow);

      // If we would overflow but don't allow it, mark all data lengths as unconsumed.
      if ((!allow_overflow) && (SPF_CIRCBUF_OVERRUN == res))
      {
         for (uint32_t ch_idx = 0; ch_idx < chunk_ptr->num_channels; ch_idx++)
         {
            buf_ptr[ch_idx].actual_data_len = 0;
         }
      }
      else
      {
         // Copy data from capi buffer to chunk frame, update actual data length of the frame.
         for (uint32_t ch_idx = 0; ch_idx < chunk_ptr->num_channels; ch_idx++)
         {
            spf_circ_buf_frame_t *ch_frame_ptr = get_frame_ptr(&wr_client_ptr->rw_pos, ch_idx);

            if (buf_ptr[ch_idx].data_ptr)
            {
               actual_data_len_consumed =
                  memscpy(&ch_frame_ptr->data[0], chunk_ptr->frame_size, buf_ptr[ch_idx].data_ptr, bytes_to_write);

               // update actual data length consumed from capi buffer.
               buf_ptr[ch_idx].actual_data_len = actual_data_len_consumed;
            }
            else if (memeset_value_ptr) // if inp buffer is not present assuming its memset.
            {
               memset(&ch_frame_ptr->data[0], *memeset_value_ptr, bytes_to_write);

               // update actual data length consumed from capi buffer.
               buf_ptr[ch_idx].actual_data_len = bytes_to_write;

               actual_data_len_consumed = bytes_to_write;
            }
            else
            {
               AR_MSG(DBG_HIGH_PRIO,
                      "write_one_frame: buf_id=0x%x Receieved null buffers nothing to write. ",
                      circ_buf_ptr->id);
               return res;
            }

            // update the current frames actual data length
            ch_frame_ptr->actual_data_len = actual_data_len_consumed;

#ifdef DEBUG_CIRC_BUF_UTILS
            AR_MSG(DBG_HIGH_PRIO,
                   "write_one_frame: ch_idx=%lu ch_frame_ptr: 0x%lx, actual_data_len: %lu ",
                   ch_idx,
                   ch_frame_ptr,
                   ch_frame_ptr->actual_data_len);
#endif
         }

         {
            // Move metadata from capi to circular buffer only for the first channel.
            res |= _move_metadata_from_input_sdata_to_cur_wr_frame(wr_client_ptr,
                                                                   buf_ptr,
                                                                   in_sdata_ptr,
                                                                   actual_data_len_consumed,
                                                                   initial_len_per_ch_in_bytes);

            // Move the writer position to next frame position
            _circ_buf_advance_to_next_frame(wr_client_ptr->circ_buf_ptr, &wr_client_ptr->rw_pos);

            // Update the write byte count in the circular buffer handle.
            res |= _circ_buf_inc_write_byte_counter(circ_buf_ptr, actual_data_len_consumed);
         }
      }
   }

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
   spf_circ_buf_chunk_t *temp_chunk_ptr = (spf_circ_buf_chunk_t *)wr_client_ptr->rw_pos.chunk_node_ptr->obj_ptr;
   AR_MSG(DBG_HIGH_PRIO,
          "write_one_frame: Write done. buf_id=0x%x chunk_ptr= 0x%x, next_wr_frame_position= %lu,  next_wr_offset=%lu, "
          "write_byte_counter= %lu actual_data_len_consumed:%lu ",
          circ_buf_ptr->id,
          temp_chunk_ptr,
          wr_client_ptr->rw_pos.frame_position,
          wr_client_ptr->rw_pos.frame_offset,
          circ_buf_ptr->write_byte_counter,
          actual_data_len_consumed);
#endif

   // Iterate through all the reader clients and update unread byte count.
   for (spf_list_node_t *rd_client_list_ptr = wr_client_ptr->circ_buf_ptr->rd_client_list_ptr;
        (NULL != rd_client_list_ptr);
        LIST_ADVANCE(rd_client_list_ptr))
   {
      spf_circ_buf_client_t *temp_rd_client_ptr = (spf_circ_buf_client_t *)rd_client_list_ptr->obj_ptr;

      // Update the unread byte count,
      // Saturate unread_bytes count on buffer overflow. This can happen during steady state. TODO: do we have to
      // print over flow messages ?
      // TODO: move reader to the next frame,
      temp_rd_client_ptr->unread_bytes += actual_data_len_consumed;
      if (temp_rd_client_ptr->unread_bytes > temp_rd_client_ptr->circ_buf_ptr->circ_buf_size_bytes)
      {
#ifdef DEBUG_CIRC_BUF_UTILS
         AR_MSG(DBG_HIGH_PRIO,
                "write_one_frame: Buffer overflow detected, buf_id=0x%x, rd_client_id=0x%x ",
                temp_rd_client_ptr->circ_buf_ptr->id,
                temp_rd_client_ptr);
#endif
         temp_rd_client_ptr->unread_bytes = temp_rd_client_ptr->circ_buf_ptr->circ_buf_size_bytes;
      }

#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO,
             "write_one_frame: buf_id=0x%x Updating rd_client_id=0x%x unread_bytes to %lu ",
             temp_rd_client_ptr->circ_buf_ptr->id,
             temp_rd_client_ptr,
             temp_rd_client_ptr->unread_bytes);
#endif
   }

   return res;
}

/* Write utility for buffering data without metadata handling. */
spf_circ_buf_result_t _circ_buf_write_data(spf_circ_buf_client_t *wr_handle,
                                           capi_stream_data_t *   in_sdata_ptr,
                                           bool_t                 allow_overflow,
                                           uint32_t *             memset_value_ptr,
                                           uint32_t *             num_memset_bytes)
{

   if ((NULL == wr_handle) || (NULL == wr_handle->circ_buf_ptr)) {
      return SPF_CIRCBUF_FAIL;
   }

   spf_circ_buf_t *circ_buf_ptr = wr_handle->circ_buf_ptr;

   // Data pointer can be null only for memset operation.
   if (!memset_value_ptr && !in_sdata_ptr) {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO, "write_data: buf_id: 0x%x capi buf ptr is NULL. ", wr_handle->circ_buf_ptr->id);
#endif // DEBUG_CIRC_BUF_UTILS
      return SPF_CIRCBUF_FAIL;
   }
   uint32_t    bytes_to_write = 0;
   capi_buf_t *buf_ptr        = NULL;
   if (in_sdata_ptr)
   {
      buf_ptr        = in_sdata_ptr->buf_ptr;
      bytes_to_write = buf_ptr[DEFAULT_CH_IDX].actual_data_len;
   }
   else
   {
      bytes_to_write = *num_memset_bytes;
   }

   spf_circ_buf_result_t res = SPF_CIRCBUF_SUCCESS;

   // Circular buffer support writing only one frame at a time.
   // if a big write buffer needs to be written into circular buffer then,
   // split the big buffer by container frame sizes.
   capi_stream_data_t in_sdata;
   capi_buf_t *cur_wr_frame_buf = circ_buf_ptr->scratch_buf_arr;
   uint32_t bytes_left_to_write = bytes_to_write;
   while (bytes_left_to_write)
   {
      memset(&in_sdata, 0, sizeof(in_sdata));
      spf_circ_buf_chunk_t *cur_wr_chunk_ptr = (spf_circ_buf_chunk_t *)wr_handle->rw_pos.chunk_node_ptr->obj_ptr;
      uint32_t              bytes_written    = bytes_to_write - bytes_left_to_write;

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
      AR_MSG(DBG_HIGH_PRIO,
             "write_data: bytes_to_write: %lu bytes_left_to_write: %lu num_ch: %lu, frame_size: %lu",
             bytes_to_write,
             bytes_left_to_write,
             cur_wr_chunk_ptr->num_channels,
             cur_wr_chunk_ptr->frame_size);
#endif

      // setup capi buffers
      for (uint32_t ch_idx = 0; ch_idx < cur_wr_chunk_ptr->num_channels; ch_idx++)
      {
         if (in_sdata_ptr)
         {
            cur_wr_frame_buf[ch_idx].data_ptr = &buf_ptr[ch_idx].data_ptr[bytes_written];
         }
         else // if data pointer is not given then memset is performed.
         {
            cur_wr_frame_buf[ch_idx].data_ptr = NULL;
         }

         // limit frame length by the container frame size.
         if (bytes_left_to_write > cur_wr_chunk_ptr->frame_size)
         {
            cur_wr_frame_buf[ch_idx].actual_data_len = cur_wr_chunk_ptr->frame_size;
            cur_wr_frame_buf[ch_idx].max_data_len    = cur_wr_chunk_ptr->frame_size;
         }
         else
         {
            cur_wr_frame_buf[ch_idx].actual_data_len = bytes_left_to_write;
            cur_wr_frame_buf[ch_idx].max_data_len    = bytes_left_to_write;
         }
      }

      in_sdata.buf_ptr  = &cur_wr_frame_buf[0];
      in_sdata.bufs_num = cur_wr_chunk_ptr->num_channels;

      // write the frame into circular buffer
      res = _circ_buf_write_one_frame(wr_handle, &in_sdata, allow_overflow, memset_value_ptr);
      if (res == SPF_CIRCBUF_FAIL)
      {

         AR_MSG(DBG_ERROR_PRIO,
                "write_data: Failed. bytes_to_write: %lu, bytes_left_to_write: %lu  ",
                bytes_to_write,
                bytes_left_to_write);
         break;
      }

      // Decrement bytes written from the counter, based on actual data len from first channel
      bytes_left_to_write -= cur_wr_frame_buf[DEFAULT_CH_IDX].actual_data_len;
   }

   // update actual data length consumed for all the channel buffers.
   if (in_sdata_ptr)
   {
      // setup capi buffers
      for (uint32_t ch_idx = 0; ch_idx < in_sdata_ptr->bufs_num; ch_idx++)
      {
         buf_ptr[ch_idx].actual_data_len = bytes_to_write - bytes_left_to_write;
      }
   }

   return res;
}

/*
 * Resets the client write chunk position based on current buffer state.
 * Full documentation in circ_buf_utils.h
 */
spf_circ_buf_result_t _circ_buf_write_client_reset(spf_circ_buf_client_t *client_ptr)
{
   if (!client_ptr)
   {
      return SPF_CIRCBUF_FAIL;
   }

   spf_circ_buf_t *circ_buf_ptr = client_ptr->circ_buf_ptr;

   if (!circ_buf_ptr)
   {
      return SPF_CIRCBUF_FAIL;
   }

   // Reset to head chunk if the write client is not registered yet.
   client_ptr->rw_pos.chunk_node_ptr = circ_buf_ptr->head_chunk_ptr;

   // reset to first frame in the chunk
   client_ptr->rw_pos.frame_position = 0;
   client_ptr->rw_pos.frame_offset   = 0;
   circ_buf_ptr->write_byte_counter  = 0;

   // TODO: handle input port close->open sequence, in that case

   return SPF_CIRCBUF_SUCCESS;
}

/*
 * Resets the client read chunk position based on current buffer state.
 * Full documentation in circ_buf_utils.h
 */
spf_circ_buf_result_t _circ_buf_read_client_reset(spf_circ_buf_client_t *rd_client_ptr)
{
   spf_circ_buf_result_t result       = SPF_CIRCBUF_SUCCESS;
   spf_circ_buf_t *      circ_buf_ptr = rd_client_ptr->circ_buf_ptr;
   // Set the read position same as write position
   if (!rd_client_ptr->is_read_client)
   {
      return result;
   }

   // Assign the last reader behind the writer position to the new reader.
   spf_list_node_t *      list_ptr                   = circ_buf_ptr->rd_client_list_ptr;
   uint32_t               max_unread_bytes           = 0;
   spf_circ_buf_client_t *last_readers_in_the_buffer = NULL;
   while (list_ptr)
   {
      spf_circ_buf_client_t *temp_client_ptr = (spf_circ_buf_client_t *)list_ptr->obj_ptr;

      // skip if its new readers node.
      if (temp_client_ptr == rd_client_ptr)
      {
         LIST_ADVANCE(list_ptr);
         continue;
      }

      // get the reader which is most behind the writer.
      if (temp_client_ptr->unread_bytes > max_unread_bytes)
      {
         max_unread_bytes           = temp_client_ptr->unread_bytes;
         last_readers_in_the_buffer = temp_client_ptr;
      }

      LIST_ADVANCE(list_ptr);
   }

   // if the last reader is found assign last readers position to new reader.
   if (last_readers_in_the_buffer)
   {
      memscpy(&rd_client_ptr->rw_pos,
              sizeof(spf_circ_buf_position_t),
              &last_readers_in_the_buffer->rw_pos,
              sizeof(spf_circ_buf_position_t));

      // metadata ptr cannot be directly copied.
      rd_client_ptr->rw_pos.readers_sdata.metadata_list_ptr = NULL;

      // update unread bytes.
      rd_client_ptr->unread_bytes = last_readers_in_the_buffer->unread_bytes;

      // NOTE: If the last reader has non zero offset, clone metadata from last reader to
      // the new reader for non zero offsets.
      if (last_readers_in_the_buffer->rw_pos.frame_offset)
      {
         spf_circ_buf_frame_t *temp_frame_ptr = get_frame_ptr(&rd_client_ptr->rw_pos, DEFAULT_CH_IDX);
         temp_frame_ptr->reader_ref_count++;

         result =
            _circ_buf_clone_or_move_metadata_list(circ_buf_ptr,
                                                  &last_readers_in_the_buffer->rw_pos.readers_sdata.metadata_list_ptr,
                                                  &rd_client_ptr->rw_pos.readers_sdata.metadata_list_ptr,
                                                  TRUE);
      }
      else
      {
         // metadata will be cloned when reading the frame
      }
   }
   else if (circ_buf_ptr->wr_client_ptr) // first reader
   {

      // If this is the first reader, initialize the reader to
      // the oldest byte frame buffered by writer.

      spf_circ_buf_client_t *wr_client_ptr = circ_buf_ptr->wr_client_ptr;
      // Initialize the read position same as the current write position.
      rd_client_ptr->rw_pos.chunk_node_ptr = wr_client_ptr->rw_pos.chunk_node_ptr;
      rd_client_ptr->rw_pos.frame_position = wr_client_ptr->rw_pos.frame_position;
      rd_client_ptr->rw_pos.frame_offset   = wr_client_ptr->rw_pos.frame_offset;

      // If the write byte counter is equal to buf size, then read & write have same position.
      if (circ_buf_ptr->write_byte_counter >= circ_buf_ptr->circ_buf_size_bytes)
      {
         rd_client_ptr->unread_bytes = circ_buf_ptr->circ_buf_size_bytes;
      }
      else
      {
         rd_client_ptr->unread_bytes = circ_buf_ptr->write_byte_counter;

         // Move the read position behind the write position by write_byte_counter bytes.
         // TODO: make to move backwards, forward will not work in the case when there is no data ahead of writer.
         result = _circ_buf_position_shift_forward(rd_client_ptr,
                                                   circ_buf_ptr->circ_buf_size_bytes - circ_buf_ptr->write_byte_counter,
                                                   FALSE // need_to_handle_metadata
         );
         if (SPF_CIRCBUF_FAIL == result)
         {
            return result;
         }
      }
   }

   /*detect and handle change in media format of the reader based on new read position.*/
   if (rd_client_ptr->rw_pos.chunk_node_ptr)
   {
      spf_circ_buf_chunk_t *cur_rd_chunk = (spf_circ_buf_chunk_t *)rd_client_ptr->rw_pos.chunk_node_ptr->obj_ptr;
      _circ_buf_check_and_handle_change_in_mf(rd_client_ptr, cur_rd_chunk->mf);
   }

   return result;
}

void _circ_buf_destroy_client_hdl(spf_circ_buf_t *circ_buf_ptr, spf_circ_buf_client_t **client_pptr)
{
   spf_circ_buf_client_t *client_ptr = *client_pptr;
   // destroy media format handle
   if (client_ptr->operating_mf)
   {
      decr_mf_ref_count(circ_buf_ptr, &client_ptr->operating_mf);
   }

   // destroy any metadata in the handle.
   if (client_ptr->rw_pos.readers_sdata.metadata_list_ptr)
   {
      _circ_buf_free_sdata(circ_buf_ptr, &client_ptr->rw_pos.readers_sdata, TRUE, NULL);
   }

   memset(client_ptr, 0, sizeof(spf_circ_buf_client_t));

   // free chunk object
   posal_memory_free(client_ptr);

   *client_pptr = NULL;
}

// Function to free the chunk.
//  1. Iterates through each frame and frees any metadata if present.
spf_circ_buf_result_t _circ_buf_free_chunk(spf_circ_buf_t *circ_buf_ptr, spf_circ_buf_chunk_t *chunk_hdr_ptr)
{
   AR_MSG(DBG_MED_PRIO,
          "free_chunk: buf_id: 0x%lx chunk_ptr: 0x%x, mf: 0x%lx num_ch: 0x%lx",
          circ_buf_ptr->id,
          chunk_hdr_ptr,
          chunk_hdr_ptr->mf,
          chunk_hdr_ptr->num_channels);

   // Free all the metadata in the chunk if any present.
   _circ_buf_free_chunk_metadata(circ_buf_ptr, chunk_hdr_ptr);

   // free all the channel buffers.
   for (uint32_t ch_idx = 0; ch_idx < chunk_hdr_ptr->num_channels; ch_idx++)
   {
      if (chunk_hdr_ptr->buf_ptr[ch_idx])
      {
         // free channel buffer pointer.
         posal_memory_free(chunk_hdr_ptr->buf_ptr[ch_idx]);
         chunk_hdr_ptr->buf_ptr[ch_idx] = NULL;
      }
   }

   if (chunk_hdr_ptr->mf)
   {
      decr_mf_ref_count(circ_buf_ptr, &chunk_hdr_ptr->mf);
   }

   // free chunk object
   posal_memory_free(chunk_hdr_ptr);

   return SPF_CIRCBUF_SUCCESS;
}

// Function detects the overflow in the read clients of the buffer and adjusts the buffer accordingly
spf_circ_buf_result_t _circ_buf_detect_and_handle_overflow(spf_circ_buf_client_t *wr_client_ptr, bool_t allow_overflow)
{
   spf_circ_buf_result_t result       = SPF_CIRCBUF_SUCCESS;
   spf_circ_buf_t *      circ_buf_ptr = wr_client_ptr->circ_buf_ptr;

   // Iterate through readers and check if the readers are reading from the current write frame.
   // If true, move reader to next frame and drop metadata in the readers handle.
   for (spf_list_node_t *rd_client_list_ptr = circ_buf_ptr->rd_client_list_ptr; (NULL != rd_client_list_ptr);
        LIST_ADVANCE(rd_client_list_ptr))
   {
      spf_circ_buf_client_t *cur_rd_client_ptr = (spf_circ_buf_client_t *)rd_client_list_ptr->obj_ptr;

      if (0 == cur_rd_client_ptr->unread_bytes)
      {
         // not overrun
         continue;
      }

      if (cur_rd_client_ptr->rw_pos.chunk_node_ptr != wr_client_ptr->rw_pos.chunk_node_ptr)
      {
         // not overrun
         continue;
      }

      // if frames are not same, check next reader
      if (cur_rd_client_ptr->rw_pos.frame_position != wr_client_ptr->rw_pos.frame_position)
      {
         // not overrun
         continue;
      }

      // Detected overrun on this reader
      // 1. Drop metadata in current handle.
      // 2. Update unread bytes.
      // 3. Move to the next frame

      spf_circ_buf_frame_t *cur_ch0_frame_ptr = get_frame_ptr(&cur_rd_client_ptr->rw_pos, DEFAULT_CH_IDX);
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO,
             "overrun: buf_id: 0x%lx rd client: 0x%x, rd frame: 0x%lx, frame unread bytes: %lu  total unread_bytes: "
             "%lu ",
             circ_buf_ptr->id,
             cur_rd_client_ptr->id,
             cur_ch0_frame_ptr,
             cur_ch0_frame_ptr->actual_data_len - cur_rd_client_ptr->rw_pos.frame_offset,
             cur_rd_client_ptr->unread_bytes);
#endif

      // Don't drop data if we don't allow overflow. Instead input will remain unconsumed.
      if (!allow_overflow)
      {
         return SPF_CIRCBUF_OVERRUN;
      }

      // 1. drop metadata in the current readers handle.
      // TODO: stream metadata needs to be moved to next frame.
      result |= _circ_buf_free_sdata(circ_buf_ptr, &cur_rd_client_ptr->rw_pos.readers_sdata, TRUE, NULL);

      // 2. Deduct data thats being overun from readers unreadbytes.
      if (cur_rd_client_ptr->rw_pos.frame_offset > cur_ch0_frame_ptr->actual_data_len)
      {
         return SPF_CIRCBUF_FAIL;
      }

      cur_rd_client_ptr->unread_bytes -= cur_ch0_frame_ptr->actual_data_len - cur_rd_client_ptr->rw_pos.frame_offset;

      // 3. Move reader to next frame.
      _circ_buf_advance_to_next_frame(cur_rd_client_ptr->circ_buf_ptr, &cur_rd_client_ptr->rw_pos);

      result |= SPF_CIRCBUF_OVERRUN;
   }

   // check if metadata is already present on the frame.
   // In steady state it should not happen. It may happen in Overrun scenario.
   spf_circ_buf_frame_t *ch_ch0_frame_ptr = get_frame_ptr(&wr_client_ptr->rw_pos, DEFAULT_CH_IDX);
   result |= _circ_buf_free_frame_metadata(circ_buf_ptr,
                                           ch_ch0_frame_ptr,
                                           TRUE, // TODO: make it stream associated metadata
                                           NULL);

   return result;
}

// Returns new chunk ID.
bool_t _circ_buf_is_valid_mf(spf_circ_buf_client_t *client_ptr, capi_media_fmt_v2_t *inp_mf)
{
   if (inp_mf->header.format_header.data_format != CAPI_FIXED_POINT)
   {
      return FALSE;
   }

   return TRUE;
}

// Returns new chunk ID.
bool_t _is_mf_unchanged(spf_circ_buf_client_t *client_ptr, capi_media_fmt_v2_t *inp_mf)
{
   if (!client_ptr->operating_mf)
   {
      return FALSE;
   }

   // supports only PCM
   if (client_ptr->operating_mf->bits_per_sample == inp_mf->format.bits_per_sample &&
       (client_ptr->operating_mf->data_is_signed == inp_mf->format.data_is_signed) &&
       (client_ptr->operating_mf->q_factor == inp_mf->format.q_factor) &&
       (client_ptr->operating_mf->sampling_rate == inp_mf->format.sampling_rate))
   {
      return TRUE;
   }

   return FALSE;
}

// Returns new chunk ID.
uint16_t _circ_buf_get_new_chunk_id(spf_circ_buf_t *circ_buf_ptr)
{
   return circ_buf_ptr->unique_id_counter++;
}

spf_circ_buf_result_t _circ_buf_add_chunks(spf_circ_buf_t *        circ_buf_ptr,
                                           uint32_t                num_additional_chunks,
                                           uint32_t                num_additional_frames,
                                           uint32_t                frame_data_size_in_bytes,
                                           spf_circ_buf_mf_info_t *operating_mf_ptr,
                                           uint32_t                first_chunk_size,
                                           uint32_t                actual_chunk_size)
{

   spf_circ_buf_result_t res = SPF_CIRCBUF_SUCCESS;

   if (!circ_buf_ptr->wr_client_ptr)
   {

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
      AR_MSG(DBG_HIGH_PRIO,
             "add_chunks: buf_id = 0x%x, Failed to add chunks, writer not registered.",
             circ_buf_ptr->id);
#endif
      return SPF_CIRCBUF_FAIL;
   }
   spf_circ_buf_client_t *wr_client_ptr = circ_buf_ptr->wr_client_ptr;

   ar_result_t ar_res             = AR_EOK;
   uint32_t    prev_circ_buf_size = circ_buf_ptr->circ_buf_size_bytes;

   // Create the additional new chunks and add them to a temporary list
   spf_list_node_t *temp_new_chunk_list_ptr = NULL;
   for (uint32_t iter = 0; iter < num_additional_chunks; ++iter)
   {
      uint32_t chunk_size = (0 == iter) ? first_chunk_size : actual_chunk_size;

      // allocate chunk function
      spf_circ_buf_chunk_t *chunk_ptr =
         _circ_buf_allocate_chunk(circ_buf_ptr, frame_data_size_in_bytes, chunk_size, operating_mf_ptr);
      if (!chunk_ptr)
      {
         // free the temp list
         spf_list_delete_list_and_free_objs(&temp_new_chunk_list_ptr, TRUE /* pool_used */);
         return SPF_CIRCBUF_FAIL;
      }

      // Push the buffer to the tail of the list
      ar_res =
         spf_list_insert_tail(&temp_new_chunk_list_ptr, (void *)chunk_ptr, circ_buf_ptr->heap_id, TRUE /* use_pool*/);
      if (AR_EOK != ar_res)
      {
         // free the temp list
         spf_list_delete_list_and_free_objs(&temp_new_chunk_list_ptr, TRUE /* pool_used */);
         return SPF_CIRCBUF_FAIL;
      }

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
      AR_MSG(DBG_HIGH_PRIO,
             "add_chunks: Allocated new chunk. buf_id = 0x%x, chunk_ptr: 0x%lx, chunk_id: 0x%lx, chunk_size = %lu, "
             "frame_size: %lu",
             circ_buf_ptr->id,
             chunk_ptr,
             chunk_ptr->id,
             chunk_size,
             chunk_ptr->frame_size);
#endif
   }

   // Return failure if the new chunks list could not be created.
   if (NULL == temp_new_chunk_list_ptr)
   {
      return SPF_CIRCBUF_FAIL;
   }

   // Insert the chunk list after the current write chunk exists, else merge the new list
   // with the previous list.
   if (wr_client_ptr->rw_pos.chunk_node_ptr)
   {
      spf_list_node_t *cur_wr_chunk_ptr  = wr_client_ptr->rw_pos.chunk_node_ptr;
      spf_list_node_t *next_wr_chunk_ptr = cur_wr_chunk_ptr->next_ptr;

      cur_wr_chunk_ptr->next_ptr        = temp_new_chunk_list_ptr;
      temp_new_chunk_list_ptr->prev_ptr = cur_wr_chunk_ptr;

      // Get the tail node of the new chunk list.
      spf_list_node_t *temp_tail_chunk_ptr = NULL;

      ar_res = spf_list_get_tail_node(temp_new_chunk_list_ptr, &temp_tail_chunk_ptr);
      if (NULL == temp_tail_chunk_ptr || (AR_EOK != ar_res))
      {
#ifdef DEBUG_CIRC_BUF_UTILS
         AR_MSG(DBG_ERROR_PRIO, "add_chunks: Tail node not found.");
#endif
         // free the temp list
         spf_list_delete_list_and_free_objs(&temp_new_chunk_list_ptr, TRUE /* pool_used */);
         return SPF_CIRCBUF_FAIL;
      }

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

   // Update chunk number.
   circ_buf_ptr->num_chunks = circ_buf_ptr->num_chunks + num_additional_chunks;

   // get the new circular buffer size by addition number of additional frames.
   circ_buf_ptr->circ_buf_size_bytes += (num_additional_frames * frame_data_size_in_bytes);

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "add_chunks: buf_id = 0x%x, circ_buf_size: %u, circ_buf_size_in_us: %u, num_chunks: %u cur_wr_chunk_ptr: "
          "0x%lx",
          circ_buf_ptr->id,
          circ_buf_ptr->circ_buf_size_bytes,
          circ_buf_ptr->circ_buf_size_in_us,
          circ_buf_ptr->num_chunks,
          wr_client_ptr->rw_pos.chunk_node_ptr);
#endif

   // Reset the write client position if we are adding the chunks for first time.
   if ((0 == prev_circ_buf_size) && (circ_buf_ptr->circ_buf_size_bytes > 0))
   {
      // Reset write client position first.
      res |= _circ_buf_write_client_reset(circ_buf_ptr->wr_client_ptr);

      // If the chunks are added for the first time, earlier read position would have been NULL.
      // update read chunks to start of the buffer.
      for (spf_list_node_t *client_list_ptr = circ_buf_ptr->rd_client_list_ptr; (NULL != client_list_ptr);
           LIST_ADVANCE(client_list_ptr))
      {
         spf_circ_buf_client_t *temp_client_ptr = (spf_circ_buf_client_t *)client_list_ptr->obj_ptr;

         res |= _circ_buf_read_client_reset(temp_client_ptr);
      }
   }

   return res;
}

/* Keeping track of the total number of raw data / raw max data that has been written */
spf_circ_buf_result_t _circ_buf_raw_inc_write_byte_counter(spf_circ_buf_raw_t *circ_buf_raw_ptr, uint32_t bytes)
{
#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO, "write_byte_counter = %u + bytes = %u", circ_buf_raw_ptr->write_byte_counter, bytes);
#endif
   circ_buf_raw_ptr->write_byte_counter_max += circ_buf_raw_ptr->wr_raw_client_ptr->raw_frame_len;

   if (circ_buf_raw_ptr->write_byte_counter_max > circ_buf_raw_ptr->circ_buf_size)
   {
      circ_buf_raw_ptr->write_byte_counter_max = circ_buf_raw_ptr->circ_buf_size;
   }
   else
   {
      circ_buf_raw_ptr->write_byte_counter += bytes;
   }

   return SPF_CIRCBUF_SUCCESS;
}

/* Helper function to advance clients read/write position forward by given bytes. */
// Advances the the client currents position frame by frame.
// TODO: Do we need to handle metadata when moving read pointer due to overflow ?
spf_circ_buf_result_t _circ_buf_raw_position_shift_forward(spf_circ_buf_raw_client_t *client_ptr,
                                                           uint32_t                   bytes_to_advance,
                                                           bool_t                     need_to_handle_metadata)
{

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "shift_forward: buf_id: 0x%x, bytes_to_advance: %lu",
          client_ptr->circ_buf_raw_ptr->id,
          bytes_to_advance);
#endif

   // if bytes to advance is zero or circular buffer size. the position will remain the current position
   // in that case we dont need to move the pointer.
   if (bytes_to_advance == 0 || (bytes_to_advance == client_ptr->circ_buf_raw_ptr->circ_buf_size))
   {
      return SPF_CIRCBUF_SUCCESS;
   }

   uint32_t bytes_left_to_advance = bytes_to_advance;
   while (bytes_left_to_advance > 0)
   {
      spf_circ_buf_frame_t *cur_frame_ptr = get_frame_ptr(&client_ptr->rw_pos, DEFAULT_CH_IDX);

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
      AR_MSG(DBG_HIGH_PRIO,
             "shift_forward: buf_id: 0x%lx, frame_position: 0x%x, frame_offset: %lu, "
             "bytes_left_to_advance: %lu, actual_data_len: %lu",
             client_ptr->circ_buf_raw_ptr->id,
             client_ptr->rw_pos.frame_position,
             client_ptr->rw_pos.frame_offset,
             bytes_left_to_advance,
             cur_frame_ptr->actual_data_len);
#endif

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
      spf_circ_buf_chunk_t *chunk_ptr = (spf_circ_buf_chunk_t *)client_ptr->rw_pos.chunk_node_ptr->obj_ptr;
      AR_MSG(DBG_HIGH_PRIO,
             "shift_forward: buf_id: 0x%lx, chunk_ptr: 0x%x, chunk_id: 0x%lx, chunk_size: %lu",
             client_ptr->circ_buf_raw_ptr->id,
             chunk_ptr,
             chunk_ptr->id,
             chunk_ptr->size);
#endif

      /* If the bytes to advance is less than the remaining bytes in the chunk */
      uint32_t bytes_left_in_cur_frame = cur_frame_ptr->actual_data_len - client_ptr->rw_pos.frame_offset;
      if (bytes_left_to_advance < bytes_left_in_cur_frame)
      {
         /* Copy the less than chunk size counter */
         client_ptr->rw_pos.frame_offset += bytes_left_to_advance;
         bytes_left_to_advance = 0;
      }
      else
      {
         /*  */
         bytes_left_to_advance -= bytes_left_in_cur_frame;

         /* Advance the clients current position to next frame */
         _circ_buf_raw_advance_to_next_frame(client_ptr->circ_buf_raw_ptr, &client_ptr->rw_pos);
      }
   }

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
   spf_circ_buf_chunk_t *chunk_ptr = (spf_circ_buf_chunk_t *)client_ptr->rw_pos.chunk_node_ptr->obj_ptr;

   AR_MSG(DBG_HIGH_PRIO,
          "shift_forward: New clients position.chunk: 0x%x, chunkp: 0x%x, frame_position: 0x%x, frame_offset: %lu",
          chunk_ptr->id,
          chunk_ptr,
          client_ptr->rw_pos.frame_position,
          client_ptr->rw_pos.frame_offset);
#endif

   return SPF_CIRCBUF_SUCCESS;
}

/* This function allocates memory for one chunk */
spf_circ_buf_chunk_t *_circ_buf_raw_allocate_chunk(spf_circ_buf_raw_t *circ_buf_raw_ptr,
                                                   uint32_t            frame_data_size_in_bytes,
                                                   uint32_t            chunk_size)
{
   uint32_t chunk_hdr_size = sizeof(spf_circ_buf_chunk_t) + sizeof(int8_t *);

   spf_circ_buf_chunk_t *chunk_hdr_ptr =
      (spf_circ_buf_chunk_t *)posal_memory_malloc(chunk_hdr_size, circ_buf_raw_ptr->heap_id);

   if (NULL == chunk_hdr_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Chunk allocation is failed size: %lu ", chunk_hdr_size);
      return NULL;
   }
   memset(chunk_hdr_ptr, 0, chunk_hdr_size);

   chunk_hdr_ptr->num_channels = 1;
   for (uint32_t ch_idx = 0; ch_idx < 1; ch_idx++)
   {
      int8_t *ch_buf_ptr = (int8_t *)posal_memory_malloc(chunk_size, circ_buf_raw_ptr->heap_id);
      if (NULL == ch_buf_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "Chunk allocation is failed size: %lu ", chunk_size);
         return NULL;
      }
      memset(ch_buf_ptr, 0, chunk_size);

      // assign ch buf ptr in the array
      chunk_hdr_ptr->buf_ptr[ch_idx] = ch_buf_ptr;
   }

   chunk_hdr_ptr->num_frames = chunk_size / GET_CHUNK_FRAME_SIZE(frame_data_size_in_bytes);

   chunk_hdr_ptr->frame_size = frame_data_size_in_bytes;

   chunk_hdr_ptr->id   = _circ_buf_raw_get_new_chunk_id(circ_buf_raw_ptr);
   chunk_hdr_ptr->size = chunk_size;

   AR_MSG(DBG_HIGH_PRIO,
          "alloc_chunk: buf_id: 0x%lx chunk_ptr: 0x%x, size: %lu, frame_size: %lu, num_ch: 0x%lx ",
          circ_buf_raw_ptr->id,
          chunk_hdr_ptr,
          chunk_hdr_ptr->size,
          chunk_hdr_ptr->frame_size,
          chunk_hdr_ptr->num_channels);

   return chunk_hdr_ptr;
}

/* This function computes the size of the chunk and frame information
 * for a given request size. It is to be noted that the requested size
 * is only a suggestion for raw compressed data since the frame len MTU need
 * not be the actual data len and partial raw data does not hold meaning */
void _circ_buf_raw_compute_chunk_info(spf_circ_buf_raw_t *circ_buf_raw_ptr,
                                      uint32_t            buffer_size_in_bytes,
                                      uint32_t *          frame_data_size_in_bytes_ptr,
                                      uint32_t *          actual_chunk_size_ptr,
                                      uint32_t *          actual_num_frames_per_chunk_ptr)
{
   uint32_t actual_chunk_size        = 0;
   uint32_t frame_data_size_in_bytes = 0;
   uint32_t num_frames_per_chunk     = 0;

   /* This is the MTU size in bytes */
   frame_data_size_in_bytes = circ_buf_raw_ptr->wr_raw_client_ptr->raw_frame_len;

   /* total frame bytes = MTU size + frame header */
   uint32_t total_frame_size_in_bytes = GET_CHUNK_FRAME_SIZE(frame_data_size_in_bytes);

   /* Num of frame units (MTU + header) that can be held in one chunk */
   num_frames_per_chunk = _CEIL(circ_buf_raw_ptr->preferred_chunk_size, total_frame_size_in_bytes);

   /* Actual size of the chunk that can hold said # of frames (MTU + header) */
   actual_chunk_size = num_frames_per_chunk * total_frame_size_in_bytes;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "compute_chunk_info: actual_chunk_size: %lu "
          "num new frames per chunk : %lu, "
          "frame_size: %lu",
          actual_chunk_size,
          num_frames_per_chunk,
          frame_data_size_in_bytes);
#endif

   if (frame_data_size_in_bytes_ptr)
   {
      *frame_data_size_in_bytes_ptr = frame_data_size_in_bytes;
   }

   if (actual_chunk_size_ptr)
   {
      *actual_chunk_size_ptr = actual_chunk_size;
   }

   if (actual_num_frames_per_chunk_ptr)
   {
      *actual_num_frames_per_chunk_ptr = num_frames_per_chunk;
   }
}

/* Expands the raw compressed circular buffer to the addition size requested.
 * With raw compressed the additional size requested is only a suggestion and we
 * ceil based on max num frames as we do not values partial data frames. The
 * chunk size is only a recommendation or preference. */
spf_circ_buf_result_t _circ_buf_raw_expand_buffer(spf_circ_buf_raw_t *circ_buf_raw_ptr, uint32_t additional_size_bytes)
{

   spf_circ_buf_result_t res = SPF_CIRCBUF_SUCCESS;

   if (additional_size_bytes == 0)
   {
      return res;
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "add_chunks: Add chunks. buf_id = 0x%x,  additional_size_req: %u, "
          "prev_circ_buf_size: %u, prev_num_chunks: %u ",
          circ_buf_raw_ptr->id,
          additional_size_bytes,
          circ_buf_raw_ptr->circ_buf_size_bytes,
          circ_buf_raw_ptr->num_chunks);
#endif

   /* Compute actual size of the chunk and number of raw compressed frames per chunk */
   uint32_t actual_chunk_size           = 0;
   uint32_t frame_data_size_in_bytes    = 0;
   uint32_t actual_num_frames_per_chunk = 0;

   _circ_buf_raw_compute_chunk_info(circ_buf_raw_ptr,
                                    additional_size_bytes,
                                    &frame_data_size_in_bytes,
                                    &actual_chunk_size,
                                    &actual_num_frames_per_chunk);

   /* Based on # frames per chunk and size of a chunk determine how many chunks to add.
    * In case of raw compressed buffer we have all chunks of same size. A last chunk
    * size might be lesser than MTU and partial raw compressed data holds no meaning. */

   uint32_t requested_num_chunks = _CEIL(additional_size_bytes, circ_buf_raw_ptr->preferred_chunk_size);

   additional_size_bytes =
      requested_num_chunks * actual_num_frames_per_chunk * GET_CHUNK_FRAME_SIZE(frame_data_size_in_bytes);

   /* Adds chunks to the circular buffer after current writers position based on
    * the ceiled num frames and additional size requested */
   _circ_buf_raw_add_chunks(circ_buf_raw_ptr,
                            requested_num_chunks,
                            requested_num_chunks * actual_num_frames_per_chunk,
                            frame_data_size_in_bytes,
                            actual_chunk_size);

   /* Updating the size of the circular buffer based on the size alloced after ceiling*/
   circ_buf_raw_ptr->circ_buf_size_bytes += additional_size_bytes;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "add_chunks: Done. buf_id = 0x%x, additional_size_alloced: %u, circ_buf_size_bytes:%lu, "
          "circ_buf_size_us: %lu, num_chunks: %u ",
          circ_buf_raw_ptr->id,
          additional_size_bytes,
          circ_buf_raw_ptr->circ_buf_size_bytes,
          circ_buf_raw_ptr->circ_buf_size,
          circ_buf_raw_ptr->num_chunks);
#endif

   return res;
}

/* Resize and Create the raw circular buffer */
spf_circ_buf_result_t _circ_buf_raw_client_resize(spf_circ_buf_raw_t *circ_buf_raw_ptr)
{
   spf_circ_buf_result_t result = SPF_CIRCBUF_SUCCESS;
   if (NULL == circ_buf_raw_ptr)
   {
      return SPF_CIRCBUF_FAIL;
   }

   if (!circ_buf_raw_ptr->wr_raw_client_ptr || (0 == circ_buf_raw_ptr->wr_raw_client_ptr->raw_frame_len))
   {
      AR_MSG(DBG_HIGH_PRIO,
             "CIRC_BUF_RAW_%lu: Not ready to resize, writer client 0x%lx may not be "
             "registered yet or raw frame len is unknown. ",
             circ_buf_raw_ptr->id,
             circ_buf_raw_ptr->wr_raw_client_ptr);

      return SPF_CIRCBUF_SUCCESS;
   }

   /* Raw Compressed Usecase currently supports only writer resize request*/

   uint32_t total_required_buf_size = 0;

   if (circ_buf_raw_ptr->wr_raw_client_ptr)
   {
      /* circ_buf_raw_ptr->wr_raw_client_ptr->init_base_buffer_size is not the same
       * as the raw circular buffer size alloced to support raw compressed. It is the
       * starting value provided to init the base buffer. As the buffer expands we use
       * the size of the buffer in bytes to help allocate. */

      if (0 == circ_buf_raw_ptr->circ_buf_size)
      {
         total_required_buf_size = circ_buf_raw_ptr->wr_raw_client_ptr->init_base_buffer_size;
      }
      else
      {
         total_required_buf_size = circ_buf_raw_ptr->circ_buf_size;
      }
   }

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "resize: buf_id = 0x%x Found max of resize requests.  "
          "circ_buf_size_bytes: %u, "
          "write_client_req_size_us: %lu ",
          circ_buf_raw_ptr->id,
          circ_buf_raw_ptr->circ_buf_size,
          total_required_buf_size);
#endif

   /* We do not shrink raw compressed buffers */
   if (total_required_buf_size <= circ_buf_raw_ptr->circ_buf_size)
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO, "resize: buf_id = 0x%x, No need to resize the buffer, returning.", circ_buf_raw_ptr->id);
#endif
      return SPF_CIRCBUF_SUCCESS;
   }
   else /* (total_required_buf_size > circ_buf_raw_ptr->circ_buf_size_bytes) */
   {
      uint32_t additional_size_bytes = total_required_buf_size - circ_buf_raw_ptr->circ_buf_size;

      result = _circ_buf_raw_expand_buffer(circ_buf_raw_ptr, additional_size_bytes);
   }

   return result;
}

/* Check if next write will cause overflow. If yes, check if the buffer is not full
 * capacity - if so expand by one chunk */
bool_t _circ_buf_raw_check_if_need_space(spf_circ_buf_raw_client_t *rd_raw_client_ptr,
                                         spf_circ_buf_raw_client_t *wr_raw_client_ptr,
                                         uint32_t                   actual_data_len_consumed)
{
   bool_t need_space = FALSE;

   spf_circ_buf_raw_t *circ_buf_raw_ptr = wr_raw_client_ptr->circ_buf_raw_ptr;

   spf_circ_buf_position_t writer;
   memset(&writer, 0, sizeof(spf_circ_buf_position_t));

   writer.chunk_node_ptr = wr_raw_client_ptr->rw_pos.chunk_node_ptr;
   writer.frame_position = wr_raw_client_ptr->rw_pos.frame_position;
   _circ_buf_raw_advance_to_next_frame(wr_raw_client_ptr->circ_buf_raw_ptr, &writer);

   if (writer.chunk_node_ptr == rd_raw_client_ptr->rw_pos.chunk_node_ptr)
   {
      if (writer.frame_position == rd_raw_client_ptr->rw_pos.frame_position)
      {
         need_space = TRUE;
      }
   }

   if (!need_space)
   {
      return FALSE;
   }

   AR_MSG(DBG_HIGH_PRIO, "Requires space. ");

   bool_t have_to_expand = FALSE;

   if (circ_buf_raw_ptr->num_frames_max)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "unread_num_frames %u num current buf %u num_frames_max %u",
             rd_raw_client_ptr->unread_num_frames,
             wr_raw_client_ptr->num_frames_in_cur_buf,
             circ_buf_raw_ptr->num_frames_max);

      if (rd_raw_client_ptr->unread_num_frames + wr_raw_client_ptr->num_frames_in_cur_buf <
          circ_buf_raw_ptr->num_frames_max)
      {
         have_to_expand = TRUE;
         AR_MSG(DBG_HIGH_PRIO, "Requires space and has to expand. ");
      }
   }
   else
   {

      AR_MSG(DBG_HIGH_PRIO,
             "unread_bytes %u actual_data_len_consumed %u circ_buf_raw_size_max %u",
             rd_raw_client_ptr->unread_bytes,
             actual_data_len_consumed,
             circ_buf_raw_ptr->circ_buf_raw_size_max);

      if ((rd_raw_client_ptr->unread_bytes + actual_data_len_consumed) < (circ_buf_raw_ptr->circ_buf_raw_size_max))
      {
#ifdef DEBUG_CIRC_BUF_UTILS
         AR_MSG(DBG_HIGH_PRIO,
                "circ_buf_size %u preferred_chunk_size %u CIRC_BUF_RAW_BUF_MAX_MEMORY %u",
                circ_buf_raw_ptr->circ_buf_size,
                circ_buf_raw_ptr->preferred_chunk_size,
                circ_buf_raw_ptr->max_raw_circ_buf_bytes_limit);
#endif
         if ((circ_buf_raw_ptr->circ_buf_size + circ_buf_raw_ptr->preferred_chunk_size) <
             circ_buf_raw_ptr->max_raw_circ_buf_bytes_limit)
         {
            AR_MSG(DBG_HIGH_PRIO, "Requires space and has to expand. ");
            have_to_expand = TRUE;
         }
      }
   }

   return have_to_expand;
}

/* Write utility for buffering one MTU at a time - currently used . */
spf_circ_buf_result_t _circ_buf_raw_write_one_frame(spf_circ_buf_raw_client_t *wr_raw_client_ptr,
                                                    capi_stream_data_t *       in_sdata_ptr,
                                                    bool_t                     allow_overflow,
                                                    uint32_t *                 memeset_value_ptr)
{
   spf_circ_buf_result_t res = SPF_CIRCBUF_SUCCESS;

   if (!wr_raw_client_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Null write client ");
      return SPF_CIRCBUF_FAIL;
   }

   // get the chunk buffer address and the offset
   spf_circ_buf_chunk_t *chunk_ptr = (spf_circ_buf_chunk_t *)wr_raw_client_ptr->rw_pos.chunk_node_ptr->obj_ptr;

   spf_circ_buf_raw_t *circ_buf_raw_ptr = wr_raw_client_ptr->circ_buf_raw_ptr;

   uint32_t    actual_data_len_consumed    = 0;
   capi_buf_t *buf_ptr                     = in_sdata_ptr->buf_ptr;
   uint32_t    bytes_to_write              = buf_ptr[DEFAULT_CH_IDX].actual_data_len;
   uint32_t    initial_len_per_ch_in_bytes = buf_ptr[DEFAULT_CH_IDX].actual_data_len;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "write_one_frame: buf_id=0x%x capi_sdata_info: flags: 0x%lx timestamp: %lu num_bufs: %lu buf_size: %lu",
          circ_buf_raw_ptr->id,
          in_sdata_ptr->flags.word,
          in_sdata_ptr->timestamp,
          in_sdata_ptr->bufs_num,
          bytes_to_write);
#endif

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "write_one_frame: buf_id=0x%x chunk: %d, chunk_ptr: 0x%x, frame_position:%lu  chunk_size: %lu "
          "bytes, chunk_num_ch: "
          "%lu",
          circ_buf_raw_ptr->id,
          chunk_ptr->id,
          chunk_ptr,
          wr_raw_client_ptr->rw_pos.frame_position,
          chunk_ptr->size,
          chunk_ptr->num_channels);
#endif

   // Update chunk ptr after check and recreate.
   chunk_ptr = (spf_circ_buf_chunk_t *)wr_raw_client_ptr->rw_pos.chunk_node_ptr->obj_ptr;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "write_one_frame: buf_id=0x%x chunk: %d, chunk_ptr: 0x%x, frame_position:%lu  chunk_size: %lu "
          "bytes, chunk_num_ch: "
          "%lu",
          circ_buf_raw_ptr->id,
          chunk_ptr->id,
          chunk_ptr,
          wr_raw_client_ptr->rw_pos.frame_position,
          chunk_ptr->size,
          chunk_ptr->num_channels);
#endif

   // Consider that the writer is faster than the reader. If media format changes, the writer might catch up to
   // reader and
   // try to recreate a chunk which reader is still reading. This would cause a data drop for the reader. Therefore
   // for the
   // allow_overflow is FALSE case, when the writer catches up to the reader's chunk (and chunk needs to be
   // recreated), wait
   // for the reader to move to the next chunk before writing more data.
   // If the reader as depleted all data, we don't care if it's in the same chunk as the writer.
   if ((!allow_overflow) && (SPF_CIRCBUF_OVERRUN == res))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "write_one_frame: buf_id=0x%x overflow was found when checking to recreate buffers. "
             "not consuming input.",
             circ_buf_raw_ptr->id);
      res = SPF_CIRCBUF_SUCCESS;

      for (uint32_t ch_idx = 0; ch_idx < chunk_ptr->num_channels; ch_idx++)
      {
         buf_ptr[ch_idx].actual_data_len = 0;
      }

      return res;
   }

   // Since writes are per frame, we always write into new frame, so current write frame position offset should be
   // zero. we never write partial data into a frame.
   if (wr_raw_client_ptr->rw_pos.frame_offset > 0)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "write_one_frame: buf_id=0x%x chunk= %d, chunk_ptr= 0x%x, offset= %lu, chunk_size= %lu",
             circ_buf_raw_ptr->id,
             chunk_ptr->id,
             chunk_ptr,
             wr_raw_client_ptr->rw_pos.frame_offset,
             chunk_ptr->size);

      return SPF_CIRCBUF_FAIL;
   }

   // Check if the frame can be fit in the current chunk frame.
   if (bytes_to_write > chunk_ptr->frame_size)
   {
      // TODO: Should not hit this case ideally ?
      AR_MSG(DBG_ERROR_PRIO,
             "write_one_frame: buf_id=0x%x actual_data_len=%lu, chunk_frame_size=%lu ",
             circ_buf_raw_ptr->id,
             bytes_to_write,
             chunk_ptr->frame_size);

      bytes_to_write = chunk_ptr->frame_size;
   }

   /* Write data to the circular buffer frame */

   // Detect overflow and move readers to next frame.
   res = _circ_buf_raw_detect_and_handle_overflow(wr_raw_client_ptr, allow_overflow);

   // If we would overflow but don't allow it, mark all data lengths as unconsumed.
   if ((!allow_overflow) && (SPF_CIRCBUF_OVERRUN == res))
   {
      for (uint32_t ch_idx = 0; ch_idx < chunk_ptr->num_channels; ch_idx++)
      {
         buf_ptr[ch_idx].actual_data_len = 0;
      }
   }
   else
   {
      // Copy data from capi buffer to chunk frame, update actual data length of the frame.
      for (uint32_t ch_idx = 0; ch_idx < chunk_ptr->num_channels; ch_idx++)
      {
         spf_circ_buf_frame_t *ch_frame_ptr = get_frame_ptr(&wr_raw_client_ptr->rw_pos, ch_idx);

         if (buf_ptr[ch_idx].data_ptr)
         {
            actual_data_len_consumed =
               memscpy(&ch_frame_ptr->data[0], chunk_ptr->frame_size, buf_ptr[ch_idx].data_ptr, bytes_to_write);

            // update actual data length consumed from capi buffer.
            buf_ptr[ch_idx].actual_data_len = actual_data_len_consumed;
         }
         else if (memeset_value_ptr) // if inp buffer is not present assuming its memset.
         {
            memset(&ch_frame_ptr->data[0], *memeset_value_ptr, bytes_to_write);

            // update actual data length consumed from capi buffer.
            buf_ptr[ch_idx].actual_data_len = bytes_to_write;

            actual_data_len_consumed = bytes_to_write;
         }
         else
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "write_one_frame: buf_id=0x%x Receieved null buffers nothing to write. ",
                   circ_buf_raw_ptr->id);
            return res;
         }

         // update the current frames actual data length
         ch_frame_ptr->actual_data_len = actual_data_len_consumed;

         // update frame header with num encoded frames in current buffer to subtract from total during read/delete.
         ch_frame_ptr->num_encoded_frames_in_cur_buf = wr_raw_client_ptr->num_frames_in_cur_buf;

#ifdef DEBUG_CIRC_BUF_UTILS
         AR_MSG(DBG_HIGH_PRIO,
                "write_one_frame: ch_idx=%lu ch_frame_ptr: 0x%lx, actual_data_len: %lu ",
                ch_idx,
                ch_frame_ptr,
                ch_frame_ptr->actual_data_len);
#endif
      }

      {
         // Move metadata from capi to circular buffer only for the first channel.
         res |= _move_metadata_from_input_sdata_to_cur_wr_raw_frame(wr_raw_client_ptr,
                                                                    buf_ptr,
                                                                    in_sdata_ptr,
                                                                    actual_data_len_consumed,
                                                                    initial_len_per_ch_in_bytes);

         // check if next frame written might cause overflow
         for (spf_list_node_t *rd_client_list_ptr = wr_raw_client_ptr->circ_buf_raw_ptr->rd_client_list_ptr;
              (NULL != rd_client_list_ptr);
              LIST_ADVANCE(rd_client_list_ptr))
         {

            spf_circ_buf_raw_client_t *temp_rd_client_ptr = (spf_circ_buf_raw_client_t *)rd_client_list_ptr->obj_ptr;

            bool_t have_to_expand =
               _circ_buf_raw_check_if_need_space(temp_rd_client_ptr, wr_raw_client_ptr, actual_data_len_consumed);

            if (have_to_expand)
            {
               posal_island_trigger_island_exit();
               _circ_buf_raw_expand_buffer_by_chunk(wr_raw_client_ptr);
            }
            else
            {
               _circ_buf_raw_advance_to_next_frame(wr_raw_client_ptr->circ_buf_raw_ptr, &wr_raw_client_ptr->rw_pos);
            }

            res |= _circ_buf_raw_inc_write_byte_counter(circ_buf_raw_ptr, actual_data_len_consumed);
         }
      }
   }

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
   spf_circ_buf_chunk_t *temp_chunk_ptr = (spf_circ_buf_chunk_t *)wr_raw_client_ptr->rw_pos.chunk_node_ptr->obj_ptr;
   AR_MSG(DBG_HIGH_PRIO,
          "write_one_frame: Write done. buf_id=0x%x chunk_ptr= 0x%x, next_wr_frame_position= %lu,  "
          "next_wr_offset=%lu, write_byte_counter= %lu actual_data_len_consumed:%lu ",
          circ_buf_raw_ptr->id,
          temp_chunk_ptr,
          wr_raw_client_ptr->rw_pos.frame_position,
          wr_raw_client_ptr->rw_pos.frame_offset,
          circ_buf_raw_ptr->write_byte_counter,
          actual_data_len_consumed);
#endif

   // Iterate through all the reader clients and update unread byte count.
   for (spf_list_node_t *rd_client_list_ptr = wr_raw_client_ptr->circ_buf_raw_ptr->rd_client_list_ptr;
        (NULL != rd_client_list_ptr);
        LIST_ADVANCE(rd_client_list_ptr))
   {
      spf_circ_buf_raw_client_t *temp_rd_client_ptr = (spf_circ_buf_raw_client_t *)rd_client_list_ptr->obj_ptr;

      // Update the unread byte count,
      // Saturate unread_bytes count on buffer overflow. This can happen during steady state. TODO: do we have to
      // print over flow messages ?
      temp_rd_client_ptr->unread_bytes += actual_data_len_consumed;
      temp_rd_client_ptr->unread_num_frames += wr_raw_client_ptr->num_frames_in_cur_buf;
      temp_rd_client_ptr->unread_bytes_max += wr_raw_client_ptr->raw_frame_len;

#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO,
             "write_one_frame: Unread bytes updated to unread bytes = %u / %u = %u and cur buf # frame = %u",
             temp_rd_client_ptr->unread_bytes,
             temp_rd_client_ptr->unread_bytes_max,
             temp_rd_client_ptr->unread_num_frames,
             wr_raw_client_ptr->num_frames_in_cur_buf);
#endif

      if (temp_rd_client_ptr->unread_bytes_max > temp_rd_client_ptr->circ_buf_raw_ptr->circ_buf_size)
      {

         AR_MSG(DBG_HIGH_PRIO,
                "write_one_frame: Buffer overflow detected, unreadmax = %d circ max = %d",
                temp_rd_client_ptr->unread_bytes_max,
                temp_rd_client_ptr->circ_buf_raw_ptr->circ_buf_size);

         temp_rd_client_ptr->unread_bytes_max = temp_rd_client_ptr->circ_buf_raw_ptr->circ_buf_size;
         temp_rd_client_ptr->unread_bytes -= actual_data_len_consumed;
         temp_rd_client_ptr->unread_num_frames -= wr_raw_client_ptr->num_frames_in_cur_buf;
      }
   }

   return res;
}

/* Write utility for buffering data without metadata handling. */
spf_circ_buf_result_t _circ_buf_raw_write_data(spf_circ_buf_raw_client_t *wr_handle,
                                               capi_stream_data_t *       in_sdata_ptr,
                                               bool_t                     allow_overflow,
                                               uint32_t *                 memset_value_ptr,
                                               uint32_t *                 num_memset_bytes)
{
   if ((NULL == wr_handle) || (NULL == wr_handle->circ_buf_raw_ptr))
   {
      return SPF_CIRCBUF_FAIL;
   }

   spf_circ_buf_raw_t *circ_buf_raw_ptr = wr_handle->circ_buf_raw_ptr;

   // Data pointer can be null only for memset operation.
   if (!memset_value_ptr && !in_sdata_ptr)
   {
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO, "write_data: buf_id: 0x%x capi buf ptr is NULL. ", wr_handle->circ_buf_raw_ptr->id);
#endif // DEBUG_CIRC_BUF_UTILS
      return SPF_CIRCBUF_FAIL;
   }
   uint32_t    bytes_to_write = 0;
   capi_buf_t *buf_ptr        = NULL;
   if (in_sdata_ptr)
   {
      buf_ptr        = in_sdata_ptr->buf_ptr;
      bytes_to_write = buf_ptr[DEFAULT_CH_IDX].actual_data_len;
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO, "write_data: bytes_to_write %u ", bytes_to_write);
#endif
   }
   else
   {
      bytes_to_write = *num_memset_bytes;
   }

   spf_circ_buf_result_t res = SPF_CIRCBUF_SUCCESS;

   // Circular buffer support writing only one frame at a time.
   // if a big write buffer needs to be written into circular buffer then,
   // split the big buffer by container frame sizes.
   capi_stream_data_t in_sdata;

   // TODO: ONLY ONE REQ NOW - MAYBE MORE FOR DEINT RAW COMPR
   capi_buf_t *cur_wr_frame_buf = circ_buf_raw_ptr->scratch_buf_arr;

   uint32_t bytes_left_to_write = bytes_to_write;

   uint32_t bytes_written = 0;

   memset(&in_sdata, 0, sizeof(in_sdata));

   spf_circ_buf_chunk_t *cur_wr_chunk_ptr = (spf_circ_buf_chunk_t *)wr_handle->rw_pos.chunk_node_ptr->obj_ptr;

   for (uint32_t ch_idx = 0; ch_idx < cur_wr_chunk_ptr->num_channels; ch_idx++)
   {
      if (in_sdata_ptr)
      {
         cur_wr_frame_buf[ch_idx].data_ptr        = &buf_ptr[ch_idx].data_ptr[bytes_written];
         cur_wr_frame_buf[ch_idx].actual_data_len = buf_ptr[ch_idx].actual_data_len;
         cur_wr_frame_buf[ch_idx].max_data_len    = buf_ptr[ch_idx].actual_data_len;
      }
      else // if data pointer is not given then memset is performed.
      {
         cur_wr_frame_buf[ch_idx].data_ptr        = NULL;
         cur_wr_frame_buf[ch_idx].actual_data_len = bytes_left_to_write;
         cur_wr_frame_buf[ch_idx].max_data_len    = bytes_left_to_write;
      }
   }

   in_sdata.buf_ptr  = &cur_wr_frame_buf[0];
   in_sdata.bufs_num = cur_wr_chunk_ptr->num_channels;

   res = _circ_buf_raw_write_one_frame(wr_handle, &in_sdata, allow_overflow, memset_value_ptr);

   if (res == SPF_CIRCBUF_FAIL)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "write_data: Failed. bytes_to_write: %lu, bytes_left_to_write: %lu  ",
             bytes_to_write,
             bytes_left_to_write);
   }

   bytes_left_to_write -= cur_wr_frame_buf[DEFAULT_CH_IDX].actual_data_len;

   // update actual data length consumed for all the channel buffers.
   if (in_sdata_ptr)
   {
      // setup capi buffers
      for (uint32_t ch_idx = 0; ch_idx < in_sdata_ptr->bufs_num; ch_idx++)
      {
         buf_ptr[ch_idx].actual_data_len = bytes_to_write - bytes_left_to_write;
      }
   }

   return res;
}

/* Resets the client write chunk position based on current buffer state. */
spf_circ_buf_result_t _circ_buf_raw_write_client_reset(spf_circ_buf_raw_client_t *client_ptr)
{
   if (!client_ptr)
   {
      return SPF_CIRCBUF_FAIL;
   }

   spf_circ_buf_raw_t *circ_buf_raw_ptr = client_ptr->circ_buf_raw_ptr;

   if (!circ_buf_raw_ptr)
   {
      return SPF_CIRCBUF_FAIL;
   }

   // Reset to head chunk if the write client is not registered yet.
   client_ptr->rw_pos.chunk_node_ptr = circ_buf_raw_ptr->head_chunk_ptr;

   /* reset to first frame in the chunk */
   client_ptr->rw_pos.frame_position        = 0;
   client_ptr->rw_pos.frame_offset          = 0;
   circ_buf_raw_ptr->write_byte_counter     = 0;
   circ_buf_raw_ptr->write_byte_counter_max = 0;

   /* Assume inititally that one buffer has one frame
    * This is to avoid error case of non-zero actual data len but zero frames
    * Metadata is rechecked once again before copying data */

   if (0 == client_ptr->num_frames_in_cur_buf)
   {
      client_ptr->num_frames_in_cur_buf = 1;
   }

   AR_MSG(DBG_HIGH_PRIO, "Reset Write Done.");
   return SPF_CIRCBUF_SUCCESS;
}

/* Resets the client read chunk position based on current buffer state. */
spf_circ_buf_result_t _circ_buf_raw_read_client_reset(spf_circ_buf_raw_client_t *rd_client_ptr)
{
   spf_circ_buf_result_t result           = SPF_CIRCBUF_SUCCESS;
   spf_circ_buf_raw_t *  circ_buf_raw_ptr = rd_client_ptr->circ_buf_raw_ptr;
   // Set the read position same as write position
   if (!rd_client_ptr->is_read_client)
   {
      return result;
   }

   // Assign the last reader behind the writer position to the new reader.
   spf_list_node_t *          list_ptr                   = circ_buf_raw_ptr->rd_client_list_ptr;
   uint32_t                   max_unread_bytes           = 0;
   spf_circ_buf_raw_client_t *last_readers_in_the_buffer = NULL;
   while (list_ptr)
   {
      spf_circ_buf_raw_client_t *temp_client_ptr = (spf_circ_buf_raw_client_t *)list_ptr->obj_ptr;

      // skip if its new readers node.
      if (temp_client_ptr == rd_client_ptr)
      {
         LIST_ADVANCE(list_ptr);
         continue;
      }

      // get the reader which is most behind the writer.
      if (temp_client_ptr->unread_bytes > max_unread_bytes)
      {
         max_unread_bytes           = temp_client_ptr->unread_bytes;
         last_readers_in_the_buffer = temp_client_ptr;
      }

      LIST_ADVANCE(list_ptr);
   }

   // if the last reader is found assign last readers position to new reader.
   if (last_readers_in_the_buffer)
   {
      memscpy(&rd_client_ptr->rw_pos,
              sizeof(spf_circ_buf_position_t),
              &last_readers_in_the_buffer->rw_pos,
              sizeof(spf_circ_buf_position_t));

      // metadata ptr cannot be directly copied.
      rd_client_ptr->rw_pos.readers_sdata.metadata_list_ptr = NULL;

      // update unread bytes.
      rd_client_ptr->unread_bytes = last_readers_in_the_buffer->unread_bytes;

      rd_client_ptr->unread_num_frames = last_readers_in_the_buffer->unread_num_frames;

      // NOTE: If the last reader has non zero offset, clone metadata from last reader to
      // the new reader for non zero offsets.
      if (last_readers_in_the_buffer->rw_pos.frame_offset)
      {
         spf_circ_buf_frame_t *temp_frame_ptr = get_frame_ptr(&rd_client_ptr->rw_pos, DEFAULT_CH_IDX);
         temp_frame_ptr->reader_ref_count++;

         result = _circ_buf_raw_clone_or_move_metadata_list(circ_buf_raw_ptr,
                                                            &last_readers_in_the_buffer->rw_pos.readers_sdata
                                                                .metadata_list_ptr,
                                                            &rd_client_ptr->rw_pos.readers_sdata.metadata_list_ptr,
                                                            TRUE);
      }
      else
      {
         // metadata will be cloned when reading the frame
      }
   }
   else if (circ_buf_raw_ptr->wr_raw_client_ptr) // first reader
   {

      // If this is the first reader, initialize the reader to
      // the oldest byte frame buffered by writer.

      spf_circ_buf_raw_client_t *wr_client_ptr = circ_buf_raw_ptr->wr_raw_client_ptr;
      // Initialize the read position same as the current write position.
      rd_client_ptr->rw_pos.chunk_node_ptr = wr_client_ptr->rw_pos.chunk_node_ptr;
      rd_client_ptr->rw_pos.frame_position = wr_client_ptr->rw_pos.frame_position;
      rd_client_ptr->rw_pos.frame_offset   = wr_client_ptr->rw_pos.frame_offset;

      // If the write byte counter is equal to buf size, then read & write have same position.
      if (circ_buf_raw_ptr->write_byte_counter_max >= circ_buf_raw_ptr->circ_buf_size)
      {
         // if maxed then have whole as such if equal
         rd_client_ptr->unread_bytes = circ_buf_raw_ptr->write_byte_counter;
         // circ_buf_raw_ptr->circ_buf_size_bytes;
      }
      else
      {
         rd_client_ptr->unread_bytes = circ_buf_raw_ptr->write_byte_counter;

         // Move the read position behind the write position by write_byte_counter bytes.
         // TODO: make to move backwards, forward will not work in the case when there is no data ahead of writer.
         result = _circ_buf_raw_position_shift_forward(rd_client_ptr,
                                                       circ_buf_raw_ptr->circ_buf_size -
                                                          circ_buf_raw_ptr->write_byte_counter_max,
                                                       FALSE // need_to_handle_metadata
         );
      }
   }

   return result;
}

/* Destroy the raw writer/reader client*/
void _circ_buf_raw_destroy_client_hdl(spf_circ_buf_raw_t *circ_buf_raw_ptr, spf_circ_buf_raw_client_t **client_pptr)
{
   spf_circ_buf_raw_client_t *client_ptr = *client_pptr;

   // destroy any metadata in the handle.
   if (client_ptr->rw_pos.readers_sdata.metadata_list_ptr)
   {
      _circ_buf_raw_free_sdata(circ_buf_raw_ptr, &client_ptr->rw_pos.readers_sdata, TRUE, NULL);
   }

   memset(client_ptr, 0, sizeof(spf_circ_buf_raw_client_t));

   // free chunk object
   posal_memory_free(client_ptr);

   *client_pptr = NULL;
}

/* Free the raw buffer chunk */
spf_circ_buf_result_t _circ_buf_raw_free_chunk(spf_circ_buf_raw_t *  circ_buf_raw_ptr,
                                               spf_circ_buf_chunk_t *chunk_hdr_ptr)
{
   AR_MSG(DBG_MED_PRIO,
          "free_chunk: buf_id: 0x%lx chunk_ptr: 0x%x, num_ch: 0x%lx",
          circ_buf_raw_ptr->id,
          chunk_hdr_ptr,
          chunk_hdr_ptr->num_channels);

   // Free all the metadata in the chunk if any present.
   _circ_buf_raw_free_chunk_metadata(circ_buf_raw_ptr, chunk_hdr_ptr);

   // free all the channel buffers.
   for (uint32_t ch_idx = 0; ch_idx < chunk_hdr_ptr->num_channels; ch_idx++)
   {
      if (chunk_hdr_ptr->buf_ptr[ch_idx])
      {
         // free channel buffer pointer.
         posal_memory_free(chunk_hdr_ptr->buf_ptr[ch_idx]);
         chunk_hdr_ptr->buf_ptr[ch_idx] = NULL;
      }
   }

   // free chunk object
   posal_memory_free(chunk_hdr_ptr);

   return SPF_CIRCBUF_SUCCESS;
}

/* Detect and handle overflow if the write and read pointers are at the same position */
spf_circ_buf_result_t _circ_buf_raw_detect_and_handle_overflow(spf_circ_buf_raw_client_t *wr_client_ptr,
                                                               bool_t                     allow_overflow)
{
   spf_circ_buf_result_t result           = SPF_CIRCBUF_SUCCESS;
   spf_circ_buf_raw_t *  circ_buf_raw_ptr = wr_client_ptr->circ_buf_raw_ptr;

   // Iterate through readers and check if the readers are reading from the current write frame.
   // If true, move reader to next frame and drop metadata in the readers handle.
   for (spf_list_node_t *rd_client_list_ptr = circ_buf_raw_ptr->rd_client_list_ptr; (NULL != rd_client_list_ptr);
        LIST_ADVANCE(rd_client_list_ptr))
   {
      spf_circ_buf_raw_client_t *cur_rd_client_ptr = (spf_circ_buf_raw_client_t *)rd_client_list_ptr->obj_ptr;

      if (0 == cur_rd_client_ptr->unread_bytes)
      {
         // not overrun
         continue;
      }

      if (cur_rd_client_ptr->rw_pos.chunk_node_ptr != wr_client_ptr->rw_pos.chunk_node_ptr)
      {
         // not overrun
         continue;
      }

      // if frames are not same, check next reader
      if (cur_rd_client_ptr->rw_pos.frame_position != wr_client_ptr->rw_pos.frame_position)
      {
         // not overrun
         continue;
      }

      // Detected overrun on this reader
      // 1. Drop metadata in current handle.
      // 2. Update unread bytes.
      // 3. Move to the next frame

      spf_circ_buf_frame_t *cur_ch0_frame_ptr = get_frame_ptr(&cur_rd_client_ptr->rw_pos, DEFAULT_CH_IDX);
#ifdef DEBUG_CIRC_BUF_UTILS
      AR_MSG(DBG_HIGH_PRIO,
             "overrun: buf_id: 0x%lx rd client: 0x%x, rd frame: 0x%lx, frame unread bytes: %lu  "
             "total unread_bytes: %lu ",
             circ_buf_raw_ptr->id,
             cur_rd_client_ptr->id,
             cur_ch0_frame_ptr,
             cur_ch0_frame_ptr->actual_data_len - cur_rd_client_ptr->rw_pos.frame_offset,
             cur_rd_client_ptr->unread_bytes);
#endif

      // Don't drop data if we don't allow overflow. Instead input will remain unconsumed.
      if (!allow_overflow)
      {
         return SPF_CIRCBUF_OVERRUN;
      }

      // 1. drop metadata in the current readers handle.
      // TODO: stream metadata needs to be moved to next frame.
      result |= _circ_buf_raw_free_sdata(circ_buf_raw_ptr, &cur_rd_client_ptr->rw_pos.readers_sdata, TRUE, NULL);

      // 2. Deduct data thats being overun from readers unreadbytes.
      cur_rd_client_ptr->unread_bytes -= cur_ch0_frame_ptr->actual_data_len - cur_rd_client_ptr->rw_pos.frame_offset;

      cur_rd_client_ptr->unread_num_frames -= cur_ch0_frame_ptr->num_encoded_frames_in_cur_buf;

      cur_rd_client_ptr->unread_bytes_max -= wr_client_ptr->raw_frame_len;

      // 3. Move reader to next frame.
      _circ_buf_raw_advance_to_next_frame(cur_rd_client_ptr->circ_buf_raw_ptr, &cur_rd_client_ptr->rw_pos);

      result |= SPF_CIRCBUF_OVERRUN;
   }

   // check if metadata is already present on the frame.
   // In steady state it should not happen. It may happen in Overrun scenario.
   spf_circ_buf_frame_t *ch_ch0_frame_ptr = get_frame_ptr(&wr_client_ptr->rw_pos, DEFAULT_CH_IDX);

   result |= _circ_buf_raw_free_frame_metadata(circ_buf_raw_ptr,
                                               ch_ch0_frame_ptr,
                                               TRUE, // TODO: make it stream associated metadata
                                               NULL);

   return result;
}

/* Verify if the media format is raw data for the raw circular buffer to operate*/
bool_t _circ_buf_raw_is_valid_mf(spf_circ_buf_raw_client_t *client_ptr, capi_cmn_raw_media_fmt_t *inp_mf)
{
   if ((inp_mf->header.format_header.data_format != CAPI_RAW_COMPRESSED) &&
       (inp_mf->header.format_header.data_format != CAPI_DEINTERLEAVED_RAW_COMPRESSED))
   {
      return FALSE;
   }

   return TRUE;
}

/* Get the next chunk id for the newly created chunk */
uint16_t _circ_buf_raw_get_new_chunk_id(spf_circ_buf_raw_t *circ_buf_raw_ptr)
{
   return circ_buf_raw_ptr->unique_id_counter++;
}

/* Adds the specified num of chunks with actual_chunk_size because for compressed data
 * the num frames are ceiled to keep the data stored as a frame with lesser holes */
spf_circ_buf_result_t _circ_buf_raw_add_chunks(spf_circ_buf_raw_t *circ_buf_raw_ptr,
                                               uint32_t            num_additional_chunks,
                                               uint32_t            num_additional_frames,
                                               uint32_t            frame_data_size_in_bytes,
                                               uint32_t            chunk_size)
{

   spf_circ_buf_result_t res = SPF_CIRCBUF_SUCCESS;

   if (!circ_buf_raw_ptr->wr_raw_client_ptr)
   {

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
      AR_MSG(DBG_HIGH_PRIO,
             "add_chunks: buf_id = 0x%x, Failed to add chunks, writer not registered.",
             circ_buf_raw_ptr->id);
#endif
      return SPF_CIRCBUF_FAIL;
   }

   spf_circ_buf_raw_client_t *wr_raw_client_ptr = circ_buf_raw_ptr->wr_raw_client_ptr;

   ar_result_t ar_res = AR_EOK;

   uint32_t prev_circ_buf_size = circ_buf_raw_ptr->circ_buf_size;

   // Create the additional new chunks and add them to a temporary list
   spf_list_node_t *temp_new_chunk_list_ptr = NULL;
   for (uint32_t iter = 0; iter < num_additional_chunks; ++iter)
   {
      // allocate chunk function
      spf_circ_buf_chunk_t *chunk_ptr =
         _circ_buf_raw_allocate_chunk(circ_buf_raw_ptr, frame_data_size_in_bytes, chunk_size);
      if (!chunk_ptr)
      {
         // free the temp list
         spf_list_delete_list_and_free_objs(&temp_new_chunk_list_ptr, TRUE /* pool_used */);
         return SPF_CIRCBUF_FAIL;
      }

      // Push the buffer to the tail of the list
      ar_res = spf_list_insert_tail(&temp_new_chunk_list_ptr, (void *)chunk_ptr, circ_buf_raw_ptr->heap_id, TRUE);
      if (AR_EOK != ar_res)
      {
         // free the temp list
         spf_list_delete_list_and_free_objs(&temp_new_chunk_list_ptr, TRUE /* pool_used */);
         return SPF_CIRCBUF_FAIL;
      }

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
      AR_MSG(DBG_HIGH_PRIO,
             "add_chunks: Allocated new chunk. buf_id = 0x%x, chunk_ptr: 0x%lx, chunk_id: 0x%lx, "
             "chunk_size = %lu, frame_size: %lu",
             circ_buf_raw_ptr->id,
             chunk_ptr,
             chunk_ptr->id,
             chunk_size,
             chunk_ptr->frame_size);
#endif
   }

   // Return failure if the new chunks list could not be created.
   if (NULL == temp_new_chunk_list_ptr)
   {
      return SPF_CIRCBUF_FAIL;
   }

   // Insert the chunk list after the current write chunk exists, else merge the new list
   // with the previous list.
   if (wr_raw_client_ptr->rw_pos.chunk_node_ptr)
   {
      spf_list_node_t *cur_wr_chunk_ptr  = wr_raw_client_ptr->rw_pos.chunk_node_ptr;
      spf_list_node_t *next_wr_chunk_ptr = cur_wr_chunk_ptr->next_ptr;

      cur_wr_chunk_ptr->next_ptr        = temp_new_chunk_list_ptr;
      temp_new_chunk_list_ptr->prev_ptr = cur_wr_chunk_ptr;

      // Get the tail node of the new chunk list.
      spf_list_node_t *temp_tail_chunk_ptr = NULL;

      ar_res = spf_list_get_tail_node(temp_new_chunk_list_ptr, &temp_tail_chunk_ptr);
      if (NULL == temp_tail_chunk_ptr || (AR_EOK != ar_res))
      {
#ifdef DEBUG_CIRC_BUF_UTILS
         AR_MSG(DBG_ERROR_PRIO, "add_chunks: Tail node not found.");
#endif
         // free the temp list
         spf_list_delete_list_and_free_objs(&temp_new_chunk_list_ptr, TRUE /* pool_used */);
         return SPF_CIRCBUF_FAIL;
      }

      temp_tail_chunk_ptr->next_ptr = next_wr_chunk_ptr;
      if (next_wr_chunk_ptr)
      {
         next_wr_chunk_ptr->prev_ptr = temp_tail_chunk_ptr;
      }
   }
   else
   {
      // Else merge the new chunk list with the previous chunk list.
      spf_list_merge_lists(&circ_buf_raw_ptr->head_chunk_ptr, &temp_new_chunk_list_ptr);
   }

   // Update chunk number.
   circ_buf_raw_ptr->num_chunks = circ_buf_raw_ptr->num_chunks + num_additional_chunks;

   // get the new circular buffer size by addition number of additional frames.
   circ_buf_raw_ptr->circ_buf_size += (num_additional_frames * frame_data_size_in_bytes);

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO,
          "add_chunks: buf_id = 0x%x, circ_buf_size: %u, num_chunks: %u cur_wr_chunk_ptr: 0x%lx",
          circ_buf_raw_ptr->id,
          circ_buf_raw_ptr->circ_buf_size,
          circ_buf_raw_ptr->num_chunks,
          wr_raw_client_ptr->rw_pos.chunk_node_ptr);
#endif

   // Reset the write client position if we are adding the chunks for first time.
   if ((0 == prev_circ_buf_size) && (circ_buf_raw_ptr->circ_buf_size > 0))
   {
      // Reset write client position first.
      res |= _circ_buf_raw_write_client_reset(circ_buf_raw_ptr->wr_raw_client_ptr);

      // If the chunks are added for the first time, earlier read position would have been NULL.
      // update read chunks to start of the buffer.
      for (spf_list_node_t *client_list_ptr = circ_buf_raw_ptr->rd_client_list_ptr; (NULL != client_list_ptr);
           LIST_ADVANCE(client_list_ptr))
      {
         spf_circ_buf_raw_client_t *temp_client_ptr = (spf_circ_buf_raw_client_t *)client_list_ptr->obj_ptr;

         res |= _circ_buf_raw_read_client_reset(temp_client_ptr);
      }
   }

   return res;
}

/* Is used to expand an existing raw circular buffer by one chunk at a time. Moves unread data in current chunk
 * to the next chunk. */
spf_circ_buf_result_t _circ_buf_raw_expand_buffer_by_chunk(spf_circ_buf_raw_client_t *wr_raw_handle)
{
   spf_circ_buf_result_t result = SPF_CIRCBUF_SUCCESS;

   if (!wr_raw_handle)
   {
      AR_MSG(DBG_ERROR_PRIO, "Null write pointer ");
      return SPF_CIRCBUF_FAIL;
   }

   spf_circ_buf_raw_t *circ_buf_raw_ptr = wr_raw_handle->circ_buf_raw_ptr;

   if (NULL == circ_buf_raw_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Null circular buffer.");
      return SPF_CIRCBUF_SUCCESS;
   }

   if (!wr_raw_handle->rw_pos.chunk_node_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Null chunk node ptr.");
      return SPF_CIRCBUF_SUCCESS;
   }

   spf_circ_buf_chunk_t *cur_wr_chunk_ptr = (spf_circ_buf_chunk_t *)wr_raw_handle->rw_pos.chunk_node_ptr->obj_ptr;

   AR_MSG(DBG_HIGH_PRIO,
          "Expand Request : cur_chunk_ptr = 0x%lx, frame_position: %lu",
          cur_wr_chunk_ptr,
          wr_raw_handle->rw_pos.frame_position);

   /* We expand only chunk by chunk */
   /* First create the new chunk and append it to the chunk after current write chunk*/
   _circ_buf_raw_add_chunks(circ_buf_raw_ptr,
                            1,
                            cur_wr_chunk_ptr->num_frames,
                            cur_wr_chunk_ptr->frame_size,
                            cur_wr_chunk_ptr->size);

   /* Update size of the circular buffer by one chunk */
   circ_buf_raw_ptr->circ_buf_size_bytes += cur_wr_chunk_ptr->size;

   /* get new chunk pointer by going to next chunk after current write chunk*/
   spf_list_node_t *new_chunk_node_ptr = wr_raw_handle->rw_pos.chunk_node_ptr;
   _circ_buf_raw_next_chunk_node(circ_buf_raw_ptr, &new_chunk_node_ptr);
   spf_circ_buf_chunk_t *new_chunk_ptr = (spf_circ_buf_chunk_t *)new_chunk_node_ptr->obj_ptr;

   /* If there is a reader after the writer in the current chunk move all the unread
    * data to new chunk in the same position so that reader can read all the unread data
    * and circle back while writer fills the gap between reader and writer. */
   bool_t move_unread_data_to_a_new_chunk = FALSE;

   /* check if there is any readers ahead of writer in this chunk, check amount of unread data */
   for (spf_list_node_t *rd_client_list_ptr = wr_raw_handle->circ_buf_raw_ptr->rd_client_list_ptr;
        (NULL != rd_client_list_ptr);
        LIST_ADVANCE(rd_client_list_ptr))
   {
      spf_circ_buf_raw_client_t *cur_rd_client_ptr = (spf_circ_buf_raw_client_t *)rd_client_list_ptr->obj_ptr;

      if (cur_rd_client_ptr->rw_pos.chunk_node_ptr->obj_ptr != (void *)cur_wr_chunk_ptr)
      {
         /* not same chunk */
         continue;
      }

      if (cur_rd_client_ptr->rw_pos.frame_position >= wr_raw_handle->rw_pos.frame_position &&
          (cur_rd_client_ptr->unread_bytes > 0))
      {
         move_unread_data_to_a_new_chunk = TRUE;
         AR_MSG(DBG_HIGH_PRIO,
                "Expand Request : Reader must be moved to next chunk. cur_chunk_ptr = 0x%lx,"
                "reader frame_position: %lu written position: %lu",
                cur_wr_chunk_ptr,
                cur_rd_client_ptr->rw_pos.frame_position,
                wr_raw_handle->rw_pos.frame_position);
      }
   }

   /* The writer should continue writing in the original next location. Unread data will
    * move to next chunk if reader is ahead. Otherwise writer will move to next chunk. */
   _circ_buf_raw_advance_to_next_frame(circ_buf_raw_ptr, &wr_raw_handle->rw_pos);

   /* Move unread data if required */
   if (move_unread_data_to_a_new_chunk)
   {
      /* The case here is [--|--|Last Written Frame|Next Write Frame / Current Read Frame|--|-- ]
       * Unread data in chunk starts from reader current location / writer updated location */
      spf_circ_buf_position_t src_chunk_pos;
      memset(&src_chunk_pos, 0, sizeof(spf_circ_buf_position_t));
      src_chunk_pos.chunk_node_ptr = wr_raw_handle->rw_pos.chunk_node_ptr;
      src_chunk_pos.frame_position = wr_raw_handle->rw_pos.frame_position;

      /* Data is copied to new chunk same frame position */
      spf_circ_buf_position_t dest_chunk_pos;
      memset(&dest_chunk_pos, 0, sizeof(spf_circ_buf_position_t));
      dest_chunk_pos.chunk_node_ptr = new_chunk_node_ptr;
      dest_chunk_pos.frame_position = wr_raw_handle->rw_pos.frame_position;

      /* move data from prev chunk to new chunk */
      uint32_t cur_chunk_frame_size = cur_wr_chunk_ptr->frame_size;

      /* From updated new write position all the way till the end of the chunk */
      while (src_chunk_pos.chunk_node_ptr == wr_raw_handle->rw_pos.chunk_node_ptr)
      {
         for (uint32_t ch_idx = 0; ch_idx < new_chunk_ptr->num_channels; ch_idx++)
         {
            spf_circ_buf_frame_t *src_ch_frame_ptr  = get_frame_ptr(&src_chunk_pos, ch_idx);
            spf_circ_buf_frame_t *dest_ch_frame_ptr = get_frame_ptr(&dest_chunk_pos, ch_idx);

            /* copy frame info and data in the frame */
            memscpy(dest_ch_frame_ptr, sizeof(spf_circ_buf_frame_t), src_ch_frame_ptr, sizeof(spf_circ_buf_frame_t));

            memscpy(&dest_ch_frame_ptr->data[0],
                    cur_chunk_frame_size,
                    &src_ch_frame_ptr->data[0],
                    cur_chunk_frame_size);

#ifdef DEBUG_CIRC_BUF_UTILS
            AR_MSG(DBG_HIGH_PRIO,
                   "copied_data: ch_idx=%lu dest_ch_frame_ptr: 0x%lx, src_ch_frame_ptr: 0x%lx,"
                   " actual_data_len: %lu",
                   ch_idx,
                   dest_ch_frame_ptr,
                   src_ch_frame_ptr,
                   src_ch_frame_ptr->actual_data_len);

            AR_MSG(DBG_HIGH_PRIO,
                   "copied_data: ch_idx=%lu num_encoded_frames_in_cur_buf:%lu, src_flags:0x%lx,"
                   " src_md_list:0x%lx",
                   ch_idx,
                   src_ch_frame_ptr->num_encoded_frames_in_cur_buf,
                   src_ch_frame_ptr->sdata.flags.word,
                   src_ch_frame_ptr->sdata.metadata_list_ptr);
#endif

            /* reset Source as writer can now write into this frame */
            memset(src_ch_frame_ptr, 0, sizeof(spf_circ_buf_frame_t));
         }

         /* Advance the src and dest frame positions */
         _circ_buf_raw_advance_to_next_frame(circ_buf_raw_ptr, &src_chunk_pos);
         _circ_buf_raw_advance_to_next_frame(circ_buf_raw_ptr, &dest_chunk_pos);
      }

      /* update the readers chunk node as the next chunk, no need to update other field */
      for (spf_list_node_t *rd_client_list_ptr = wr_raw_handle->circ_buf_raw_ptr->rd_client_list_ptr;
           (NULL != rd_client_list_ptr);
           LIST_ADVANCE(rd_client_list_ptr))
      {
         spf_circ_buf_client_t *cur_rd_client_ptr = (spf_circ_buf_client_t *)rd_client_list_ptr->obj_ptr;

         /* check if the chunk pointer is same. */
         if (cur_rd_client_ptr->rw_pos.chunk_node_ptr->obj_ptr != (void *)cur_wr_chunk_ptr)
         {
            // not same chunk.
            continue;
         }

         /* update the reader position */
         if (cur_rd_client_ptr->rw_pos.frame_position >= wr_raw_handle->rw_pos.frame_position)
         {
            cur_rd_client_ptr->rw_pos.chunk_node_ptr = new_chunk_node_ptr;
#ifdef DEBUG_CIRC_BUF_UTILS
            AR_MSG(DBG_HIGH_PRIO,
                   "set_mf: Reader moved to chunk cur_chunk_ptr = 0x%lx, frame_position: %lu, unread bytes %lu",
                   cur_rd_client_ptr->rw_pos.chunk_node_ptr->obj_ptr,
                   cur_rd_client_ptr->rw_pos.frame_position,
                   cur_rd_client_ptr->unread_bytes);
#endif
         }
      }
   }

   AR_MSG(DBG_HIGH_PRIO,
          "Expand Request : Writer moved to chunk cur_chunk_ptr = 0x%lx, frame_position: %lu, unread bytes %lu",
          wr_raw_handle->rw_pos.chunk_node_ptr->obj_ptr,
          wr_raw_handle->rw_pos.frame_position,
          wr_raw_handle->unread_bytes);

   return result;
}

#if 0
/*This utility doesn't support metadata handling, max data length doesn't have to be within container frame size */
spf_circ_buf_result_t _circ_buf_read_data(spf_circ_buf_client_t *read_handle, capi_buf_t *buf_ptr)
{
   if ((NULL == read_handle) || (NULL == buf_ptr) || (NULL == buf_ptr->data_ptr) || (0 == buf_ptr->max_data_len))
   {
      return SPF_CIRCBUF_FAIL;
   }

   if (buf_ptr->actual_data_len > 0)
   {
      return SPF_CIRCBUF_FAIL;
   }

   uint32_t             bytes_req_to_read  = buf_ptr->max_data_len;
   uint32_t             bytes_left_to_read = bytes_req_to_read;
   spf_circ_buf_result_t res;

#ifdef DEBUG_CIRC_BUF_UTILS
   AR_MSG(DBG_HIGH_PRIO, "read_data: bytes_req_to_read: %lu ", bytes_req_to_read);
#endif // DEBUG_CIRC_BUF_UTILS

   // Circular buffer support reading one frame at a time.
   // if the read is done without metadata propagation its possible to read more than a frame
   // split the big read buffer by container frame sizes.
   capi_stream_data_t out_sdata;
   capi_buf_t         cur_wr_frame_buf[16];
   while (bytes_left_to_read)
   {
      capi_buf_t           cur_rd_frame_buf;
      spf_circ_buf_chunk_t *cur_rd_chunk_ptr    = (spf_circ_buf_chunk_t *)read_handle->rw_pos.chunk_node_ptr->obj_ptr;
      uint32_t             bytes_read_till_now = bytes_req_to_read - bytes_left_to_read;

#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
      AR_MSG(DBG_HIGH_PRIO,
             "read_data: bytes_read_till_now: %lu bytes_left_to_read: %lu",
             bytes_read_till_now,
             bytes_left_to_read);
#endif

      cur_rd_frame_buf.data_ptr = &buf_ptr->data_ptr[bytes_read_till_now];

      // limit frame length by the container frame size.
      if (bytes_left_to_read > cur_rd_chunk_ptr->frame_size)
      {
         cur_rd_frame_buf.actual_data_len = 0;
         cur_rd_frame_buf.max_data_len    = cur_rd_chunk_ptr->frame_size;
      }
      else
      {
         cur_rd_frame_buf.actual_data_len = 0;
         cur_rd_frame_buf.max_data_len    = bytes_left_to_read;
      }

      // Read the frame into circular buffer
      res = _circ_buf_read_one_frame(read_handle, &cur_rd_frame_buf, NULL);
      if (res == SPF_CIRCBUF_FAIL)
      {
#ifdef DEBUG_CIRC_BUF_UTILS_VERBOSE
         AR_MSG(DBG_ERROR_PRIO,
                "read_data: bytes_read_till_now: %lu bytes_left_to_read: %lu",
                bytes_read_till_now,
                bytes_left_to_read);
#endif
         return SPF_CIRCBUF_FAIL;
      }

      // Decrement bytes written from the counter.
      bytes_left_to_read -= cur_rd_frame_buf.actual_data_len;
   }

   return res;
}
#endif
