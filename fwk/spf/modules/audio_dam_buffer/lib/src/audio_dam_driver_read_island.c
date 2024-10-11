/**
 *   \file audio_dam_driver_read_island.c
 *   \brief
 *        This file contains implementation of Audio Dam buffer driver
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "audio_dam_driver.h"
#include "audio_dam_driver_i.h"
#include "circular_buffer_i.h"

static ar_result_t audio_dam_stream_read_util_(audio_dam_stream_reader_t *reader_handle,   // in
                                               uint32_t                   num_chs_to_read, // in
                                               uint32_t    bytes_req_to_read, // in, considered valid only if non zero
                                               capi_buf_t *output_buf_arr,    // in/out
                                               bool_t     *output_buf_ts_is_valid, // out
                                               int64_t    *output_buf_ts,          // out
                                               uint32_t   *output_buf_len_in_us)     // out
{
   ar_result_t         result     = AR_EOK;
   circbuf_result_t    buf_result = CIRCBUF_SUCCESS;
   audio_dam_driver_t *drv_ptr    = reader_handle->driver_ptr;
   // Iterate through all the channel write clients and write the input data
   for (uint32_t iter = 0; iter < num_chs_to_read; iter++)
   {
      int8_t  *frame_ptr          = NULL;
      uint32_t max_read_frame_len = 0;
      uint32_t actual_data_len    = 0;
      if (reader_handle->driver_ptr->is_raw_compressed)
      {
         audio_dam_raw_comp_frame_header_t *scratch_frame_ptr =
            (audio_dam_raw_comp_frame_header_t *)reader_handle->driver_ptr->ch_frame_scratch_buf_ptr;
         scratch_frame_ptr->magic_word      = 0;
         scratch_frame_ptr->actual_data_len = 0;

         frame_ptr          = (int8_t *)scratch_frame_ptr;
         max_read_frame_len = AUDIO_DAM_GET_TOTAL_FRAME_LEN(reader_handle->driver_ptr->frame_max_data_len_in_bytes);
      }
      else
      {
         frame_ptr          = output_buf_arr[iter].data_ptr + output_buf_arr[iter].actual_data_len;
         max_read_frame_len = (bytes_req_to_read)
                                 ? bytes_req_to_read
                                 : output_buf_arr[iter].max_data_len - output_buf_arr[iter].actual_data_len;
      }

      buf_result =
         circ_buf_read(reader_handle->rd_client_ptr_arr[iter], frame_ptr, max_read_frame_len, &actual_data_len);
      if (CIRCBUF_UNDERRUN == buf_result)
      {
         return AR_ENEEDMORE;
      }

      /*  Copies from scratch to capi buffer (for Raw only) */
      if (reader_handle->driver_ptr->is_raw_compressed)
      {
         // write data to local ch buffer
         audio_dam_raw_comp_frame_header_t *scratch_frame_ptr = (audio_dam_raw_comp_frame_header_t *)frame_ptr;

#ifdef DEBUG_AUDIO_DAM_DRIVER
         DAM_MSG_ISLAND(drv_ptr->iid,
                        DBG_HIGH_PRIO,
                        "read: magic_word:%lu, frame_len:%lu actual_len:%lu",
                        scratch_frame_ptr->magic_word,
                        max_read_frame_len,
                        scratch_frame_ptr->actual_data_len);
#endif

         if (max_read_frame_len != actual_data_len)
         {
            DAM_MSG_ISLAND(drv_ptr->iid,
                           DBG_ERROR_PRIO,
                           "read: Error reading compressed frame! magic_word:%lu, actual_len:%lu expected_len:%lu",
                           scratch_frame_ptr->magic_word,
                           actual_data_len,
                           max_read_frame_len);
            return AR_EFAILED;
         }

         if (AUDIO_DAM_RAW_COMPRESSED_FRAME_MAGIC_WORD != scratch_frame_ptr->magic_word)
         {
            DAM_MSG_ISLAND(drv_ptr->iid,
                           DBG_ERROR_PRIO,
                           "read: Error reading compressed frame! magic_word:%lu, actual_len:%lu",
                           scratch_frame_ptr->magic_word,
                           scratch_frame_ptr->actual_data_len);
            return AR_EFAILED;
         }

         if (scratch_frame_ptr->actual_data_len >
             (output_buf_arr[iter].max_data_len - output_buf_arr[iter].actual_data_len))
         {
            DAM_MSG_ISLAND(drv_ptr->iid,
                           DBG_ERROR_PRIO,
                           "read: Error copying compressed frame from scratch to capi output!  actual_len:%lu, "
                           "free_space:%lu",
                           scratch_frame_ptr->actual_data_len,
                           (output_buf_arr[iter].max_data_len - output_buf_arr[iter].actual_data_len));
            return AR_EFAILED;
         }

         memscpy(output_buf_arr[iter].data_ptr + output_buf_arr[iter].actual_data_len,
                 output_buf_arr[iter].max_data_len - output_buf_arr[iter].actual_data_len,
                 &scratch_frame_ptr->data[0],
                 scratch_frame_ptr->actual_data_len);

         output_buf_arr[iter].actual_data_len += scratch_frame_ptr->actual_data_len;
      }
      else // PCM data
      {
         output_buf_arr[iter].actual_data_len += actual_data_len;
      }

#ifdef DEBUG_AUDIO_DAM_DRIVER
      DAM_MSG_ISLAND(drv_ptr->iid,
                     DBG_LOW_PRIO,
                     "DAM_DRIVER: Stream reader data read done. ch_id=%d, iter = %d, actual_data_len=%d ",
                     reader_handle->rd_client_ptr_arr[iter]->circ_buf_ptr->id,
                     iter,
                     output_buf_arr[iter].actual_data_len);
#endif
   }

   // Update Output buffer timestamp based on the amount of data read
   uint32_t remaining_unread_len_in_us =
      audio_dam_compute_buffer_size_in_us(reader_handle->driver_ptr,
                                          reader_handle->rd_client_ptr_arr[0]->unread_bytes,
                                          TRUE);

   uint32_t output_frame_len_us =
      audio_dam_compute_buffer_size_in_us(reader_handle->driver_ptr, output_buf_arr[0].actual_data_len, FALSE);

   *output_buf_ts_is_valid = reader_handle->rd_client_ptr_arr[0]->circ_buf_ptr->is_valid_timestamp;

   int64_t cur_writer_ts = reader_handle->rd_client_ptr_arr[0]->circ_buf_ptr->timestamp;
   *output_buf_ts        = cur_writer_ts - (remaining_unread_len_in_us + output_frame_len_us);

   *output_buf_len_in_us = output_frame_len_us;

#ifdef DEBUG_AUDIO_DAM_DRIVER
   DAM_MSG_ISLAND(drv_ptr->iid,
                  DBG_HIGH_PRIO,
                  "DAM_DRIVER: Output Buffer ts: (%ld, %lu) out_len_us:%lu writer_ts:%lu unread_len:(%lu, %lu)",
                  *output_buf_ts_is_valid,
                  *output_buf_ts,
                  output_frame_len_us,
                  cur_writer_ts,
                  reader_handle->rd_client_ptr_arr[0]->unread_bytes,
                  remaining_unread_len_in_us);
#endif
   return result;
}

ar_result_t audio_dam_force_set_pending_bytes(audio_dam_stream_reader_t *reader_handle)
{
   ar_result_t result = AR_EOK;

   // drain the buffers immediately, send an EOS
   uint32_t requested_batch_us        = reader_handle->data_batching_us;
   uint32_t requested_batch           = audio_dam_compute_buffer_size_in_bytes(reader_handle->driver_ptr, requested_batch_us);

   reader_handle->pending_batch_bytes = MIN(requested_batch, reader_handle->rd_client_ptr_arr[0]->unread_bytes);
   return result;
}

bool_t audio_dam_if_batching_req_met(audio_dam_stream_reader_t *reader_handle          // in
                                    )
{
   uint32_t    requested_batch_us = reader_handle->data_batching_us;
   uint32_t requested_batch = audio_dam_compute_buffer_size_in_bytes(reader_handle->driver_ptr, requested_batch_us);
   if (0 == reader_handle->pending_batch_bytes)
   {
      if (reader_handle->rd_client_ptr_arr[0]->unread_bytes >= requested_batch)
      {
         return TRUE;
      }
   }
   return FALSE;
}

static ar_result_t audio_dam_stream_batch_read(audio_dam_stream_reader_t *reader_handle,          // in
                                               uint32_t                   num_chs_to_read,        // in
                                               capi_buf_t *               output_buf_arr,         // in/out
                                               bool_t *                   output_buf_ts_is_valid, // out
                                               int64_t *                  output_buf_ts,          // out
                                               uint32_t *                 output_buf_len_in_us)                    // out
{
   ar_result_t result = AR_EOK;
   uint32_t    requested_batch_us = reader_handle->data_batching_us;

   uint32_t requested_batch = audio_dam_compute_buffer_size_in_bytes(reader_handle->driver_ptr, requested_batch_us);

   // setting pending data : pending_batch_bytes = pending_data
   if (0 == reader_handle->pending_batch_bytes)
   {
      if (reader_handle->rd_client_ptr_arr[0]->unread_bytes >= requested_batch)
      {
         reader_handle->pending_batch_bytes = requested_batch;
#ifdef DEBUG_AUDIO_DAM_DRIVER
         AR_MSG_ISLAND(DBG_MED_PRIO, "DAM_DRIVER: pending_batch_bytes = %lu ", reader_handle->pending_batch_bytes);
#endif
      }
      else
      {
         /** batching requirement not met, need more data to be buffer */
         return result;
      }
   }

   uint32_t pending_batch_bytes  = reader_handle->pending_batch_bytes;
   uint32_t actual_bytes_to_read = 0;

   // if pending bytes are less than a frame length (not full output buffer) then send partial data
   if (pending_batch_bytes >= (output_buf_arr[0].max_data_len - output_buf_arr[0].actual_data_len))
   {
      actual_bytes_to_read = output_buf_arr[0].max_data_len - output_buf_arr[0].actual_data_len;
   }
   else
   {
      actual_bytes_to_read = pending_batch_bytes;
   }

   result = audio_dam_stream_read_util_(reader_handle,
                                        num_chs_to_read,
                                        actual_bytes_to_read,
                                        output_buf_arr,
                                        output_buf_ts_is_valid,
                                        output_buf_ts,
                                        output_buf_len_in_us);

   // update bytes to read
   if (AR_EOK == result)
   {
      reader_handle->pending_batch_bytes -= output_buf_arr[0].actual_data_len;
   }

   return result;
}

ar_result_t audio_dam_stream_read(audio_dam_stream_reader_t *reader_handle,          // in
                                  uint32_t                   num_chs_to_read,        // in
                                  capi_buf_t                *output_buf_arr,         // in/out
                                  bool_t                    *output_buf_ts_is_valid, // out
                                  int64_t                   *output_buf_ts,          // out
                                  uint32_t                  *output_buf_len_in_us)   // out
{
   ar_result_t result = AR_EOK;

   if (0 == num_chs_to_read)
   {
      return AR_EFAILED;
   }

   if (audio_dam_driver_is_virtual_writer_mode(reader_handle))
   {
      return audio_dam_stream_read_from_virtual_buf(reader_handle,
                                                    num_chs_to_read,
                                                    output_buf_arr,
                                                    output_buf_ts_is_valid,
                                                    output_buf_ts,
                                                    output_buf_len_in_us);
   }
   else if (reader_handle->is_batch_streaming)
   {
      return audio_dam_stream_batch_read(reader_handle,
                                         num_chs_to_read,
                                         output_buf_arr,
                                         output_buf_ts_is_valid,
                                         output_buf_ts,
                                         output_buf_len_in_us);
   }
   else
   {
      return audio_dam_stream_read_util_(reader_handle,
                                         num_chs_to_read,
                                         0, /** zero means read as much as that fits in output*/
                                         output_buf_arr,
                                         output_buf_ts_is_valid,
                                         output_buf_ts,
                                         output_buf_len_in_us);
   }

   return result;
}

ar_result_t audio_dam_get_stream_reader_unread_bytes(audio_dam_stream_reader_t *reader_handle, uint32_t *unread_bytes)
{
   ar_result_t result = AR_EOK;

   if (NULL == reader_handle)
   {
      return AR_ENOTREADY;
   }

   if (reader_handle->virt_buf_ptr)
   {
      // for virtual buffer
      *unread_bytes = 0;
   }
   else
   {
      *unread_bytes = reader_handle->rd_client_ptr_arr[0]->unread_bytes;
   }

   return result;
}

ar_result_t audio_dam_get_stream_reader_pending_bytes(audio_dam_stream_reader_t *reader_handle, uint32_t *unread_bytes)
{
   ar_result_t result = AR_EOK;

   if (NULL == reader_handle)
   {
      return AR_ENOTREADY;
   }

   if (reader_handle->virt_buf_ptr)
   {
      // for virtual buffer
      *unread_bytes = 0;
   }
   else
   {
      *unread_bytes = reader_handle->pending_batch_bytes;
   }

   return result;
}

ar_result_t audio_dam_stream_read_adjust(audio_dam_stream_reader_t *reader_handle,
                                         uint32_t                   requested_read_offset_in_us,
                                         uint32_t *                 actual_read_offset_in_us,
                                         bool_t                     force_adjust)
{
   ar_result_t result = AR_EOK;
   if (NULL == reader_handle)
   {
#ifdef DEBUG_AUDIO_DAM_DRIVER
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "DAM_DRIVER: Stream Reader adjust failed. Bad input params.");
#endif
      return AR_EFAILED;
   }
   audio_dam_driver_t *drv_ptr = reader_handle->driver_ptr;

   if (audio_dam_driver_is_virtual_writer_mode(reader_handle))
   {
      return audio_dam_stream_read_adjust_virt_wr_mode(reader_handle, requested_read_offset_in_us, actual_read_offset_in_us);
   }

   /** If not virtual writer mode, adjust the circular buffer pointers */
   uint32_t requested_read_offset =
      audio_dam_compute_buffer_size_in_bytes(reader_handle->driver_ptr, requested_read_offset_in_us);

   uint32_t max_possible_read_offset = requested_read_offset;

#ifdef FEAT_RAW_PROCESSED_DATA_SYNC
   // Find the oldest time stamp among all the circular buffers.
   int64_t oldest_timestamp = INT_MAX_64;
   // Find the minimum unread/written bytes of all the channel buffers.
   uint32_t min_unread_bytes    = 0xFFFFFFFF;
   uint32_t min_written_bytes   = 0xFFFFFFFF;
   bool_t   is_ch_sync_possible = reader_handle->driver_ptr->is_raw_compressed ? FALSE : TRUE;
   for (uint32_t iter = 0; iter < reader_handle->num_channels; iter++)
   {
      circ_buf_client_t *rd_client_ptr = reader_handle->rd_client_ptr_arr[iter];
      if (!rd_client_ptr->circ_buf_ptr)
      {
         continue;
      }

#ifdef DEBUG_AUDIO_DAM_DRIVER
      DAM_MSG_ISLAND(drv_ptr->iid,
                     DBG_HIGH_PRIO,
                     "DAM_DRIVER: channel TS info. ch_id=0x%lx, timestamp=%ld, oldest_timestamp= %ld,",
                     reader_handle->rd_client_ptr_arr[iter]->circ_buf_ptr->id,
                     reader_handle->rd_client_ptr_arr[iter]->circ_buf_ptr->timestamp,
                     oldest_timestamp);
#endif

      // Check if its the latest time stamp
      if (rd_client_ptr->circ_buf_ptr->is_valid_timestamp == FALSE)
      {
#ifdef DEBUG_AUDIO_DAM_DRIVER
         DAM_MSG_ISLAND(drv_ptr->iid,
                        DBG_HIGH_PRIO,
                        "DAM_DRIVER: channel TS not valid. cannot be synchronized. ch_id=0x%lx, is_valid_timestamp=%ld",
                        reader_handle->rd_client_ptr_arr[iter]->circ_buf_ptr->id,
                        reader_handle->rd_client_ptr_arr[iter]->circ_buf_ptr->is_valid_timestamp);
#endif
         is_ch_sync_possible = FALSE;
      }
      else
      {
         if (oldest_timestamp > rd_client_ptr->circ_buf_ptr->timestamp)
         {
            oldest_timestamp = rd_client_ptr->circ_buf_ptr->timestamp;
         }
      }

      if (rd_client_ptr->unread_bytes < min_unread_bytes)
      {
         min_unread_bytes = rd_client_ptr->unread_bytes;
      }

      // track min write byte count among all channels.
      if (rd_client_ptr->circ_buf_ptr->write_byte_counter < min_written_bytes)
      {
         min_written_bytes = rd_client_ptr->circ_buf_ptr->write_byte_counter;
      }
   }
#endif

#ifdef DEBUG_AUDIO_DAM_DRIVER
   DAM_MSG_ISLAND(drv_ptr->iid,
                  DBG_HIGH_PRIO,
                  "DAM_DRIVER: Stream reader read adjust, requested_read_offset=%lu, min_unread_bytes = %lu, "
                  "min_written_bytes = %lu",
                  requested_read_offset,
                  min_unread_bytes,
                  min_written_bytes);
#endif

   // Maximum read offset possible for all the channels is same as
   // minimum unread or written bytes of all the channels.
   if (force_adjust)
   {
      if (max_possible_read_offset > min_written_bytes)
      {
         max_possible_read_offset = min_written_bytes;
      }
   }
   else // force_adjust == false
   {
      if (max_possible_read_offset > min_unread_bytes)
      {
         max_possible_read_offset = min_unread_bytes;
      }
   }

   // Iterate through all the Readers channel buffers and adjust the read pointer.
   for (uint32_t iter = 0; iter < reader_handle->num_channels; iter++)
   {
      circ_buf_client_t *rd_client_ptr = reader_handle->rd_client_ptr_arr[iter];

      if (!rd_client_ptr->circ_buf_ptr)
      {
         continue;
      }

      uint32_t sync_offset_in_bytes = 0;

#ifdef FEAT_RAW_PROCESSED_DATA_SYNC
      if (is_ch_sync_possible)
      {
         int64_t sync_offset_in_us = rd_client_ptr->circ_buf_ptr->timestamp - oldest_timestamp;
         if (sync_offset_in_us > 0)
         {
            // find floor of the offset, ceil is not recommended it may result in 1 ms error in offset.
            int64_t sync_offset_in_ms = (sync_offset_in_us) / 1000;

            // convert offset from ms to bytes
            sync_offset_in_bytes = sync_offset_in_ms * reader_handle->driver_ptr->bytes_per_one_ms;
         }
      }
#endif

#ifdef DEBUG_AUDIO_DAM_DRIVER
      DAM_MSG_ISLAND(drv_ptr->iid,
                     DBG_HIGH_PRIO,
                     "DAM_DRIVER: channel TS not valid. cannot be synchronized. ch_id=0x%lx, sync_offset_in_bytes=%ld",
                     reader_handle->rd_client_ptr_arr[iter]->circ_buf_ptr->id,
                     sync_offset_in_bytes);
#endif

      if (CIRCBUF_SUCCESS !=
          circ_buf_read_adjust(rd_client_ptr, max_possible_read_offset + sync_offset_in_bytes, NULL, force_adjust))
      {
#ifdef DEBUG_AUDIO_DAM_DRIVER
         DAM_MSG_ISLAND(drv_ptr->iid,
                        DBG_ERROR_PRIO,
                        "DAM_DRIVER: Stream reader, read adjust failed. ch_id=%d, iter = %d",
                        rd_client_ptr->circ_buf_ptr->id,
                        iter);
#endif
         result |= AR_EFAILED;
      }

#ifdef DEBUG_AUDIO_DAM_DRIVER
      DAM_MSG_ISLAND(drv_ptr->iid,
                     DBG_HIGH_PRIO,
                     "DAM_DRIVER: Stream reader, read adjust successful. ch_id=%d, min_unread_bytes =%lu",
                     rd_client_ptr->circ_buf_ptr->id,
                     min_unread_bytes);
#endif
   }

   uint32_t max_possible_read_offset_in_us =
      audio_dam_compute_buffer_size_in_us(reader_handle->driver_ptr, max_possible_read_offset, TRUE);

   if (actual_read_offset_in_us)
   {
      *actual_read_offset_in_us = max_possible_read_offset_in_us;
   }

   DAM_MSG_ISLAND(drv_ptr->iid,
                  DBG_HIGH_PRIO,
                  "Read adjust done, result%lx, requested_read_offset:(%lu, %lu), actual_read_offset:(%lu, %lu) "
                  "(us,bytes)",
                  result,
                  requested_read_offset_in_us,
                  requested_read_offset,
                  max_possible_read_offset_in_us,
                  max_possible_read_offset);

   return result;
}