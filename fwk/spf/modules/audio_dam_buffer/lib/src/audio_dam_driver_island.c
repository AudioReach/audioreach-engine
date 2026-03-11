/**
 *   \file audio_dam_driver_island.c
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

// writes data into the stream buffer and caches the timestamp of the latest sample written.
ar_result_t audio_dam_stream_write(audio_dam_stream_writer_t *writer_handle,
                                   uint32_t                   input_bufs_num,
                                   capi_buf_t *               input_buf_arr,
                                   uint32_t                   is_valid_timestamp,
                                   int64_t                    input_buf_timestamp)
{
   ar_result_t         result       = AR_EOK;
   //audio_dam_driver_t *drv_ptr      = writer_handle->driver_ptr;
   circbuf_result_t    circ_buf_res = CIRCBUF_SUCCESS;
   // Iterate through all the channel write clients and write the input data
   for (uint32_t iter = 0; iter < input_bufs_num; iter++)
   {
      uint32_t bytes_to_consume = input_buf_arr[0].actual_data_len;
      if (0 == bytes_to_consume)
      {
         continue;
      }

      int8_t * frame_ptr = NULL;
      uint32_t frame_len = 0;
      if (writer_handle->driver_ptr->is_raw_compressed)
      {
         if (bytes_to_consume > writer_handle->driver_ptr->frame_max_data_len_in_bytes)
         {
#ifdef DEBUG_AUDIO_DAM_DRIVER
            DAM_MSG_ISLAND(writer_handle->driver_ptr->iid,
                           DBG_LOW_PRIO,
                           "DAM_DRIVER: ch_id=%d, iter = %d, bytes_to_consume = %lu consuming only %lu",
                           writer_handle->wr_client_arr_ptr[iter].circ_buf_ptr->id,
                           iter,
                           bytes_to_consume,
                           writer_handle->driver_ptr->frame_max_data_len_in_bytes);
#endif
            // consume only one frame
            bytes_to_consume = writer_handle->driver_ptr->frame_max_data_len_in_bytes;
         }

         // write data to local ch buffer
         audio_dam_raw_comp_frame_header_t *scratch_frame_ptr =
            (audio_dam_raw_comp_frame_header_t *)writer_handle->driver_ptr->ch_frame_scratch_buf_ptr;
         scratch_frame_ptr->magic_word = AUDIO_DAM_RAW_COMPRESSED_FRAME_MAGIC_WORD;

         // copy data from capi to scrtch frame
         memscpy(&scratch_frame_ptr->data[0],
                 writer_handle->driver_ptr->frame_max_data_len_in_bytes,
                 input_buf_arr[iter].data_ptr,
                 bytes_to_consume);
         scratch_frame_ptr->actual_data_len = bytes_to_consume;

         frame_ptr = (int8_t *)scratch_frame_ptr;
         frame_len = AUDIO_DAM_GET_TOTAL_FRAME_LEN(writer_handle->driver_ptr->frame_max_data_len_in_bytes);

         // set bytes consumed
         input_buf_arr[iter].actual_data_len = bytes_to_consume;
      }
      else // pcm data
      {
         frame_ptr = input_buf_arr[iter].data_ptr;
         frame_len = bytes_to_consume;

         // for PCM case we consume all the input available hence no need to update actual data len
      }

      // compute timestamp of the latest sample
      int64_t latest_sample_offset_us =
         audio_dam_compute_buffer_size_in_us(writer_handle->driver_ptr, bytes_to_consume, FALSE);

      int64_t latest_sample_ts = input_buf_timestamp + latest_sample_offset_us;

      circ_buf_res = circ_buf_write(&writer_handle->wr_client_arr_ptr[iter],
                                    frame_ptr,
                                    frame_len,
                                    is_valid_timestamp,
                                    latest_sample_ts);

      if (CIRCBUF_OVERRUN == circ_buf_res)
      {
         result = AR_EOK;
      }
      else if (CIRCBUF_SUCCESS != circ_buf_res)
      {
         // Channel buffer will not be created only if an inp channel is not routed to any of the output ports,
         // its a valid case, so data will be dropped for that ch, no need to return/print an error.
         if (0 == writer_handle->wr_client_arr_ptr[iter].circ_buf_ptr->circ_buf_size)
         {
#ifdef DEBUG_AUDIO_DAM_DRIVER
            DAM_MSG_ISLAND(writer_handle->driver_ptr->iid,
                           DBG_ERROR_PRIO,
                           "DAM_DRIVER: Stream writer buffer not created for ch_id=%d, iter = %d skipping it ",
                           writer_handle->wr_client_arr_ptr[iter].circ_buf_ptr->id,
                           iter);
#endif
            result = AR_EOK;
         }
         else
         {
#ifdef DEBUG_AUDIO_DAM_DRIVER
            DAM_MSG_ISLAND(writer_handle->driver_ptr->iid,
                           DBG_ERROR_PRIO,
                           "DAM_DRIVER: Stream writer buffering failed. ch_id=%d, iter = %d",
                           writer_handle->wr_client_arr_ptr[iter].circ_buf_ptr->id,
                           iter);
#endif
            result |= AR_EFAILED;
         }
      }
#ifdef DEBUG_AUDIO_DAM_DRIVER
      DAM_MSG_ISLAND(writer_handle->driver_ptr->iid,
                     DBG_HIGH_PRIO,
                     "DAM_DRIVER: Stream writer buffering done. ch_id=%d, iter = %d, actual_data_len = %d",
                     writer_handle->wr_client_arr_ptr[iter].circ_buf_ptr->id,
                     iter,
                     input_buf_arr[iter].actual_data_len);
#endif
   }
   return result;
}

/** returns byes per channel */
uint32_t audio_dam_compute_buffer_size_in_us(audio_dam_driver_t *drv_ptr,
                                             uint32_t            buffer_size_in_bytes,
                                             bool_t              includes_frame_header)
{
   uint32_t buffer_size_in_us = 0;
   uint32_t num_frames        = 0;
   if (drv_ptr->is_raw_compressed)
   {
      uint32_t frame_max_data_len_in_bytes = includes_frame_header
                                                ? AUDIO_DAM_GET_TOTAL_FRAME_LEN(drv_ptr->frame_max_data_len_in_bytes)
                                                : drv_ptr->frame_max_data_len_in_bytes;
      if (frame_max_data_len_in_bytes == buffer_size_in_bytes)
      {
         num_frames        = 1;
         buffer_size_in_us = drv_ptr->frame_max_data_len_in_us;
      }
      else
      {
         num_frames        = (buffer_size_in_bytes + frame_max_data_len_in_bytes - 1) / frame_max_data_len_in_bytes;
         buffer_size_in_us = num_frames * drv_ptr->frame_max_data_len_in_us;
      }
   }
   else
   {
      buffer_size_in_us = (buffer_size_in_bytes / drv_ptr->bytes_per_one_ms) * 1000;
   }

#ifdef DEBUG_AUDIO_DAM_DRIVER
   DAM_MSG_ISLAND(drv_ptr->iid,
                  DBG_LOW_PRIO,
                  "covert_to_us: size_in_bytes:%lu, num_frames=0x%lu, frame_size:%lu buffer_size_us:%lu",
                  buffer_size_in_bytes,
                  num_frames,
                  drv_ptr->frame_max_data_len_in_us,
                  buffer_size_in_us);
#endif

   return buffer_size_in_us;
}


uint32_t audio_dam_compute_buffer_size_in_bytes(audio_dam_driver_t *drv_ptr, uint32_t buffer_size_in_us)
{
   uint32_t buffer_size_in_bytes = 0;
   if (drv_ptr->is_raw_compressed)
   {
      uint32_t num_frames =
         (buffer_size_in_us + (drv_ptr->frame_max_data_len_in_us - 1)) / drv_ptr->frame_max_data_len_in_us;
      buffer_size_in_bytes = num_frames * AUDIO_DAM_GET_TOTAL_FRAME_LEN(drv_ptr->frame_max_data_len_in_bytes);
#ifdef DEBUG_AUDIO_DAM_DRIVER
      DAM_MSG_ISLAND(drv_ptr->iid,
                     DBG_HIGH_PRIO,
                     "covert_to_bytes: size_in_us:%lu, num_frames=%lu, frame_size:%lu buffer_size_bytes:%lu",
                     buffer_size_in_us,
                     num_frames,
                     AUDIO_DAM_GET_TOTAL_FRAME_LEN(drv_ptr->frame_max_data_len_in_bytes),
                     buffer_size_in_bytes);
#endif
   }
   else
   {
      uint32_t buffer_size_in_ms = (buffer_size_in_us / 1000);
      buffer_size_in_bytes       = buffer_size_in_ms * drv_ptr->bytes_per_one_ms;
   }

   return buffer_size_in_bytes;
}

ar_result_t audio_dam_stream_reader_enable_batching_mode(audio_dam_stream_reader_t *reader_handle,
                                                         bool_t                     is_batch_streaming,
                                                         uint32_t                   data_batching_us)
{

   if (!reader_handle)
   {
      return AR_EFAILED;
   }

   reader_handle->is_batch_streaming = is_batch_streaming;
   reader_handle->data_batching_us   = data_batching_us;
   /** reset pending batch bytes*/
   reader_handle->pending_batch_bytes = 0;

   return AR_EOK;
}
