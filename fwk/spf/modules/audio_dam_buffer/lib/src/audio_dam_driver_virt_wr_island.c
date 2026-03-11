/**
 *   \file audio_dam_driver_virt_buf_island.c
 *   \brief
 *        This file contains implementation of Audio Dam buffer driver for virtual writer mode
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "audio_dam_driver.h"
#include "audio_dam_driver_i.h"
#include "circular_buffer_i.h"
#include "math.h"

/** Rounds of the time to nearest ms. This function must be used when calculating TS difference between reader and
   writer, because sometimes due to writers TS jitters/drift the distance between them can be not aligned to discrete ms
   intervals. That can mess up the read pointer adjustment or overflow detection. For example,
   if cur readers ts = 12000us and writer TS = 12000us, on writing next 40ms frame lets say writer updates the TS to
   16001us to some jitter. then the distance between writer and reader will be calculated as 40001us. but in terms of data
   its only 40000us, so its better to round off the TS to nearset MS to avoid the effects of jitter/drift in the unread len
   calculation.
   */
#define ROUND_OFF_TIMESTAMP_TO_MS_BOUNDARY(x) (((x + 500) / 1000) * 1000)

static ar_result_t audio_dam_stream_get_cur_wr_position(audio_dam_stream_reader_virtual_buf_info_t *virt_buf_ptr,
                                                        virt_wr_position_info_t                    *wr_pos)
{
   ar_result_t result = AR_EOK;
   memset(wr_pos, 0, sizeof(virt_wr_position_info_t));
   if (AR_FAILED(result = virt_buf_ptr->cfg_ptr->get_writer_ptr_fn(virt_buf_ptr->cfg_ptr->writer_handle, wr_pos)))
   {
      AR_MSG_ISLAND(DBG_HIGH_PRIO, "DAM_DRIVER: Failed to get the writer position result 0x%lx");
      return AR_EFAILED;
   }

   return AR_EOK;
}

static ar_result_t audio_dam_stream_read_check_virt_buf_overrun(audio_dam_stream_reader_t *reader_handle,
                                                                virt_wr_position_info_t    wr_pos,
                                                                uint32_t                  *bytes_to_read_per_ch_ptr,
                                                                uint32_t                  *data_to_read_in_us_ptr,
                                                                int64_t                   *unread_len_in_us_ptr)
{
   ar_result_t                                 result              = AR_EOK;
   audio_dam_stream_reader_virtual_buf_info_t *virt_buf_ptr        = reader_handle->virt_buf_ptr;
   uint32_t                                    circ_buf_size_in_us = virt_buf_ptr->cfg_ptr->circular_buffer_size_in_us;

   /** if writer position timestmap is invalid it means that writer is not ready/reset/not started writing any data,
    * hence defer reading data until writer is updated. */
   if (!wr_pos.is_ts_valid)
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO,
                    "virt_buf_read: Unexpected writer TS, cannot read data ! read_ptr:%lx reader_ts:(msw 0x%lx, lsw "
                    "0x%lx), "
                    "cur_wr_ts(valid:%lu msw 0x%lx, lsw 0x%lx), ",
                    virt_buf_ptr->read_ptr,
                    (uint32_t)(virt_buf_ptr->reader_ts >> 32),
                    (uint32_t)virt_buf_ptr->reader_ts,
                    wr_pos.is_ts_valid,
                    (uint32_t)(wr_pos.latest_write_sample_ts >> 32),
                    (uint32_t)wr_pos.latest_write_sample_ts);

      /** reader cannot have a valid TS without writers timestamp*/
      virt_buf_ptr->is_reader_ts_valid = FALSE;
      virt_buf_ptr->reader_ts          = 0;
      *unread_len_in_us_ptr            = 0;
      return AR_ENEEDMORE;
   }

   // set reader ts based on the new writer timestamp.
   if (!virt_buf_ptr->is_reader_ts_valid)
   {
      int32_t dist_btwn_in_bytes = wr_pos.latest_write_addr - (uint32_t)virt_buf_ptr->read_ptr;
      if (dist_btwn_in_bytes < 0)
      {
         dist_btwn_in_bytes += virt_buf_ptr->cfg_ptr->circular_buffer_size_in_bytes;
      }
      uint32_t dist_btwn_in_bytes_per_ch = dist_btwn_in_bytes / virt_buf_ptr->cfg_ptr->num_channels;

      // convert to us
      uint32_t dist_btwn_in_us =
         audio_dam_compute_buffer_size_in_us(reader_handle->driver_ptr, dist_btwn_in_bytes_per_ch, FALSE);

      virt_buf_ptr->is_reader_ts_valid = TRUE;
      virt_buf_ptr->reader_ts          = wr_pos.latest_write_sample_ts - dist_btwn_in_us;

      AR_MSG_ISLAND(DBG_HIGH_PRIO,
                    "virt_buf_read: UPDATING rd timestamp w.r.t to new wr TS, rd addr:0x%lx, ts:(valid: %lu, msw "
                    "0x%lx, lsw 0x%lx), "
                    "unread_len_in_us (%luus)",
                    virt_buf_ptr->read_ptr,
                    virt_buf_ptr->is_reader_ts_valid,
                    (uint32_t)(virt_buf_ptr->reader_ts >> 32),
                    (uint32_t)virt_buf_ptr->reader_ts,
                    (uint32_t)dist_btwn_in_us);
   }

   /** calculate read offset relative to the writers timestamp, and check if overflow happened based on the offset*/
   *unread_len_in_us_ptr = ROUND_OFF_TIMESTAMP_TO_MS_BOUNDARY(wr_pos.latest_write_sample_ts - virt_buf_ptr->reader_ts);

#ifdef DEBUG_AUDIO_DAM_DRIVER
   AR_MSG_ISLAND(DBG_HIGH_PRIO,
                 "virt_buf_read: CUR READ  position addr:0x%lx, ts:(valid: %lu, msw 0x%lx, lsw 0x%lx), "
                 "unread_len_in_us (msw %lu, lsw %lu)",
                 virt_buf_ptr->read_ptr,
                 virt_buf_ptr->is_reader_ts_valid,
                 (uint32_t)(virt_buf_ptr->reader_ts >> 32),
                 (uint32_t)virt_buf_ptr->reader_ts,
                 (uint32_t)(*unread_len_in_us_ptr >> 32),
                 (uint32_t)*unread_len_in_us_ptr);
#endif

   // good case
   if (*unread_len_in_us_ptr >= 0)
   {
      if (*unread_len_in_us_ptr > circ_buf_size_in_us) // overflow occured
      {
         /** if overflow occured, adjust read pointer to the oldest sample in the circular buffer i.e offset by the size
          * of the circular buffer*/
         AR_MSG_ISLAND(DBG_HIGH_PRIO,
                       "virt_buf_read: Detected overflow! reader addr:0x%lx, reader_ts:(msw 0x%lx, lsw 0x%lx), "
                       "unread_len_in_us  (msw 0x%lu, lsw 0x%lu). Need to readjust read pointer!",
                       virt_buf_ptr->read_ptr,
                       (uint32_t)(virt_buf_ptr->reader_ts >> 32),
                       (uint32_t)virt_buf_ptr->reader_ts,
                       (uint32_t)(*unread_len_in_us_ptr >> 32),
                       (uint32_t)*unread_len_in_us_ptr);

         /** Reset reader position to the oldest sample in the circular buffer */
         audio_dam_stream_read_adjust_virt_wr_mode(reader_handle, circ_buf_size_in_us, NULL);
         *unread_len_in_us_ptr = circ_buf_size_in_us;
         return AR_EOK;
      }
      else if (*unread_len_in_us_ptr > *data_to_read_in_us_ptr) // full frame available to read
      {
         /** distance between read and write is greater than required read len, nothing to to*/
         return AR_EOK;
      }
      else if (0 == *unread_len_in_us_ptr) // no frame available to read
      {
#ifdef DEBUG_AUDIO_DAM_DRIVER
         AR_MSG_ISLAND(DBG_HIGH_PRIO,
                       "virt_buf_read: Detected underflow! addr:0x%lx, ts:(msw 0x%lx, lsw 0x%lx), cur_wr_ts(msw 0x%lx, "
                       "lsw 0x%lx), ",
                       virt_buf_ptr->read_ptr,
                       (uint32_t)(virt_buf_ptr->reader_ts >> 32),
                       (uint32_t)virt_buf_ptr->reader_ts,
                       (uint32_t)(wr_pos.latest_write_sample_ts >> 32),
                       (uint32_t)wr_pos.latest_write_sample_ts);
#endif
         return AR_ENEEDMORE;
      }
      else // (*unread_len_in_us_ptr < *data_to_read_in_us_ptr) // partial frame available to read
      {
         // not enough data present to fill the entire output buffer
         *data_to_read_in_us_ptr = *unread_len_in_us_ptr;
         *bytes_to_read_per_ch_ptr =
            audio_dam_compute_buffer_size_in_bytes(reader_handle->driver_ptr, *unread_len_in_us_ptr);
         return AR_EOK;
      }
   }
   else // *unread_len_in_us_ptr < 0
   {
      /** reader TS cannot be greater than writer TS, invalid */
      AR_MSG_ISLAND(DBG_ERROR_PRIO,
                    "virt_buf_read: Unexpected, reader TS > writer TS ! reader_ts:(msw 0x%lx, lsw 0x%lx), "
                    "cur_wr_ts(msw 0x%lx, lsw 0x%lx), ",
                    virt_buf_ptr->read_ptr,
                    (uint32_t)(virt_buf_ptr->reader_ts >> 32),
                    (uint32_t)virt_buf_ptr->reader_ts,
                    (uint32_t)(wr_pos.latest_write_sample_ts >> 32),
                    (uint32_t)wr_pos.latest_write_sample_ts);
      return AR_EFAILED;
   }

   return result;
}

ar_result_t audio_dam_stream_read_from_virtual_buf(audio_dam_stream_reader_t *reader_handle,          // in
                                                   uint32_t                   num_chs_to_read,        // in
                                                   capi_buf_t                *output_buf_arr,         // in/out
                                                   bool_t                    *output_buf_ts_is_valid, // out
                                                   int64_t                   *output_buf_ts,          // out
                                                   uint32_t                  *output_buf_len_in_us)                    // out
{
   ar_result_t result = AR_EOK;

   audio_dam_stream_reader_virtual_buf_info_t    *virt_buf_ptr     = reader_handle->virt_buf_ptr;
   param_id_audio_dam_imcl_virtual_writer_info_t *cfg_ptr          = virt_buf_ptr->cfg_ptr;
   uint32_t                                       num_src_channels = cfg_ptr->num_channels;

   /** note that 24bit samples are expected to be packed always in 32bit.
    * Thats why assuming 4 bytes for 24bit as well.*/
   uint32_t bytes_per_sample = (cfg_ptr->bits_per_sample == 16) ? 2 : 4;

   // calc bytes to read
   uint32_t bytes_to_read_per_ch = output_buf_arr[0].max_data_len;

   uint32_t data_to_read_in_us =
      audio_dam_compute_buffer_size_in_us(reader_handle->driver_ptr, bytes_to_read_per_ch, FALSE);

   /** Checks,
    * 1. if read position and data available to read in the circular buffer.
    * 2. If enough data not avaiable, adjust the data to be read.
    * 3. Handle overflow if occured.
    * 4. Checks santiy of the read/write pointers.
    * */
   // get th current wr pointer addr, TS
   virt_wr_position_info_t wr_pos;
   if (AR_FAILED(result = audio_dam_stream_get_cur_wr_position(virt_buf_ptr, &wr_pos)))
   {
      AR_MSG_ISLAND(DBG_HIGH_PRIO,
                    "DAM_DRIVER: Virt buf reader check overrun, Failed to get the writer position result 0x%lx");
      return AR_EFAILED;
   }

#ifdef DEBUG_AUDIO_DAM_DRIVER
   AR_MSG_ISLAND(DBG_HIGH_PRIO,
                 "virt_buf_read: CUR WRITE position addr:0x%lx, ts:(valid: %lu, msw 0x%lx, lsw 0x%lx)",
                 wr_pos.latest_write_addr,
                 wr_pos.is_ts_valid,
                 (uint32_t)(wr_pos.latest_write_sample_ts >> 32),
                 (uint32_t)wr_pos.latest_write_sample_ts);
#endif

   int64_t unread_len_in_us = 0;
   if (AR_FAILED(result = audio_dam_stream_read_check_virt_buf_overrun(reader_handle,
                                                                       wr_pos,
                                                                       &bytes_to_read_per_ch,
                                                                       &data_to_read_in_us,
                                                                       &unread_len_in_us)))
   {
      return result;
   }

   // scale it to num channels present in the virtual buffer
   uint32_t bytes_left_to_read_from_virt_buf = bytes_to_read_per_ch * num_src_channels;

#ifdef DEBUG_AUDIO_DAM_DRIVER
   AR_MSG_ISLAND(DBG_HIGH_PRIO,
                 "virt_buf_read: unread_len_in_us=%lu, bytes_to_read_per_ch=[requested=%lu vs available=%lu] "
                 "num_src_chs=%lu num_ds_chs=%lu",
                 (uint32_t)unread_len_in_us,
                 output_buf_arr[0].max_data_len,
                 bytes_to_read_per_ch,
                 num_src_channels,
                 num_chs_to_read);
#endif

   // This loop makes handles the circular buffer wrap around logic. Ideally virtual circular buffers are sized based on
   // the integral number of frames, if thats not the case - this loop makes sure that the circ buffer is wrapped around
   // when the read pointer reaches the end.
   while (bytes_left_to_read_from_virt_buf)
   {
      int32_t bytes_to_read_in_cur_iter = bytes_left_to_read_from_virt_buf;
      int32_t  read_offset_from_base     = (uint32_t)virt_buf_ptr->read_ptr - cfg_ptr->circular_buffer_base_address;

      /**read position sanity checks*/
      if (bytes_to_read_in_cur_iter < 0 || bytes_to_read_in_cur_iter > cfg_ptr->circular_buffer_size_in_bytes)
      {
         AR_MSG_ISLAND(DBG_ERROR_PRIO,
                       "virt_buf_read: buf_base_addr=0x%lx read_ptr=0x%lx read_offset_from_base=%lu "
                       "bytes_left_to_read_from_virt_buf=%lu",
                       cfg_ptr->circular_buffer_base_address,
                       (uint32_t)virt_buf_ptr->read_ptr,
                       read_offset_from_base,
                       bytes_left_to_read_from_virt_buf);
         return AR_EFAILED;
      }

      // check if there is enough data till buffer end addr, or need to wrap around
      // bytes that can be copied till the end of the circular buffer, else need to wrap around read in next iteration
      uint32_t max_bytes_possible_to_read_in_cur_iter = cfg_ptr->circular_buffer_size_in_bytes - read_offset_from_base;
      if (bytes_to_read_in_cur_iter > max_bytes_possible_to_read_in_cur_iter)
      {
         bytes_to_read_in_cur_iter = max_bytes_possible_to_read_in_cur_iter;
      }

#ifdef DEBUG_AUDIO_DAM_DRIVER
      AR_MSG_ISLAND(DBG_HIGH_PRIO,
                    "virt_buf_read: bytes_left_to_read_from_virt_buf=%lu bytes_to_read_in_cur_iter=%lu "
                    "read_offset{from_base:%lu, to_end:%lu}",
                    bytes_left_to_read_from_virt_buf,
                    bytes_to_read_in_cur_iter,
                    read_offset_from_base,
                    max_bytes_possible_to_read_in_cur_iter);
#endif

      /**
       * directly copies required number of channels from src to output buffers in deinleaved format.
       * */
      // todo: check for 24bit usage
      capi_buf_t src_buf;
      src_buf.data_ptr        = (int8_t *)virt_buf_ptr->read_ptr;
      src_buf.actual_data_len = bytes_to_read_in_cur_iter;
      src_buf.max_data_len    = bytes_to_read_in_cur_iter;

      // invalidate the amount of data read for this iteration
      if (AR_FAILED(result = posal_cache_invalidate((uint32_t)src_buf.data_ptr, bytes_to_read_in_cur_iter)))
      {
         AR_MSG_ISLAND(DBG_ERROR_PRIO,
                       "Failed to cache invalidate addr:0x%lx for size %lu",
                       src_buf.data_ptr,
                       bytes_to_read_in_cur_iter);
         return result;
      }
#ifdef DEBUG_AUDIO_DAM_DRIVER
      else
      {
         AR_MSG_ISLAND(DBG_HIGH_PRIO,
                       "Success to cache invalidate addr:0x%lx for size %lu",
                       src_buf.data_ptr,
                       bytes_to_read_in_cur_iter);
      }
#endif

      uint32_t num_samp_per_ch = bytes_to_read_in_cur_iter / (bytes_per_sample * num_src_channels);

      /** setup capi output buffer in scratch to pass to the deinterleaver utility*/
      for (uint32_t i = 0; i < num_chs_to_read; i++)
      {
         virt_buf_ptr->out_scratch_bufs[i].data_ptr = output_buf_arr[i].data_ptr + output_buf_arr[i].actual_data_len;
         virt_buf_ptr->out_scratch_bufs[i].actual_data_len = 0;
         virt_buf_ptr->out_scratch_bufs[i].max_data_len =
            output_buf_arr[i].max_data_len - output_buf_arr[i].actual_data_len;
      }

      if (AR_FAILED(result = spf_intlv_to_deintlv_v3(&src_buf,
                                                     virt_buf_ptr->out_scratch_bufs,
                                                     num_src_channels,
                                                     num_chs_to_read,
                                                     bytes_per_sample,
                                                     num_samp_per_ch)))
      {
         AR_MSG_ISLAND(DBG_ERROR_PRIO,
                       "Failed to read deintlvd data from virtual buffer actual len%lu, max len %lu",
                       output_buf_arr[0].actual_data_len,
                       output_buf_arr[0].max_data_len);
         return AR_EFAILED;
      }

      for (uint32_t i = 0; i < num_chs_to_read; i++)
      {
         output_buf_arr[i].actual_data_len += virt_buf_ptr->out_scratch_bufs[i].actual_data_len;
      }

      // advance read pointer, wrap around if reached end of the buffer
      virt_buf_ptr->read_ptr += bytes_to_read_in_cur_iter;
      if (((uint32_t)virt_buf_ptr->read_ptr) >=
          (cfg_ptr->circular_buffer_base_address + cfg_ptr->circular_buffer_size_in_bytes))
      {
         virt_buf_ptr->read_ptr = (int8_t *)cfg_ptr->circular_buffer_base_address;
      }

      // decrement bytes left to read for next cycle.
      bytes_left_to_read_from_virt_buf -= bytes_to_read_in_cur_iter;
   }

   // convert byte read to micro seconds,
   if (output_buf_len_in_us)
   {
      *output_buf_len_in_us = data_to_read_in_us;
   }

   // Return if the output TS is valid
   if (output_buf_ts_is_valid)
   {
      *output_buf_ts_is_valid = wr_pos.is_ts_valid ? TRUE : FALSE;
   }

   // update read TS to the next unread sample
   if (wr_pos.is_ts_valid)
   {
      if (output_buf_ts)
      {
         // TS for the buffer generated in this process cycle
         *output_buf_ts = (wr_pos.latest_write_sample_ts - unread_len_in_us);
      }

      // unread buffer len remaining in the circular buffer
      unread_len_in_us = unread_len_in_us - data_to_read_in_us;

      // TS for the read position relative to the current write position, read ts must be adjusted w.r.t to write Ts
      // every to avoid any issues due to drift
      virt_buf_ptr->reader_ts = wr_pos.latest_write_sample_ts - unread_len_in_us;
   }

   return result;
}

ar_result_t audio_dam_stream_read_adjust_virt_wr_mode(audio_dam_stream_reader_t *reader_handle,
                                                      uint32_t                   requested_read_offset_in_us,
                                                      uint32_t                  *actual_read_offset_in_us_ptr)
{
   ar_result_t                                 result                   = AR_EOK;
   audio_dam_stream_reader_virtual_buf_info_t *virt_buf_ptr             = reader_handle->virt_buf_ptr;
   uint32_t                                    actual_read_offset_in_us = requested_read_offset_in_us;

   // get wr pointer addr, TS
   virt_wr_position_info_t wr_pos;
   if (AR_FAILED(result = audio_dam_stream_get_cur_wr_position(virt_buf_ptr, &wr_pos)))
   {
      AR_MSG_ISLAND(DBG_HIGH_PRIO,
                    "DAM_DRIVER: Virt buf reader adjust, Failed to get the writer position result 0x%lx");
      goto __err_bailout;
   }

   if (actual_read_offset_in_us > virt_buf_ptr->cfg_ptr->circular_buffer_size_in_us)
   {
      actual_read_offset_in_us = virt_buf_ptr->cfg_ptr->circular_buffer_size_in_us;
   }

   AR_MSG_ISLAND(DBG_HIGH_PRIO,
                 "virt_buf: adjust, cur writer addr:0x%lx, ts:(valid: %lu, msw 0x%lx, lsw 0x%lx)",
                 wr_pos.latest_write_addr,
                 wr_pos.is_ts_valid,
                 (uint32_t)(wr_pos.latest_write_sample_ts >> 32),
                 (uint32_t)wr_pos.latest_write_sample_ts);

   // first assing read ptr same as write and move the rd pointer behind by the required offset
   virt_buf_ptr->read_ptr = (int8_t *)wr_pos.latest_write_addr;

   // instead of moving forward by actual_read_offset_in_us, move the rd pointer forward by
   // circular_buffer_size_in_us - actual_read_offset_in_us, result will be same since it circ buffer
   uint32_t frwd_offset_in_bytes_per_ch =
      audio_dam_compute_buffer_size_in_bytes(reader_handle->driver_ptr,
                                             virt_buf_ptr->cfg_ptr->circular_buffer_size_in_us -
                                                actual_read_offset_in_us);
   uint32_t frwd_offset_in_bytes = frwd_offset_in_bytes_per_ch * virt_buf_ptr->cfg_ptr->num_channels;

   // current rd offset from the base addr
   uint32_t cur_rd_offset = (uint32_t)virt_buf_ptr->read_ptr - virt_buf_ptr->cfg_ptr->circular_buffer_base_address;

   // calc new rd offset from the base addr, if its out of bounds wrap it around the buffer.
   uint32_t new_rd_offset = cur_rd_offset + frwd_offset_in_bytes;
   if (new_rd_offset >= virt_buf_ptr->cfg_ptr->circular_buffer_size_in_bytes)
   {
      new_rd_offset = new_rd_offset - virt_buf_ptr->cfg_ptr->circular_buffer_size_in_bytes;
   }

   // adjust ptr based on new offset
   virt_buf_ptr->read_ptr = (int8_t *)(virt_buf_ptr->cfg_ptr->circular_buffer_base_address + new_rd_offset);

#ifdef DEBUG_AUDIO_DAM_DRIVER
   AR_MSG_ISLAND(DBG_HIGH_PRIO,
                 "virt_buf: reader adjust, new rd addr:0x%lx, ts:(frwd_offset_in_bytes: %lu, cur_rd_offset 0x%lx, "
                 "new_rd_offset 0x%lx)",
                 virt_buf_ptr->read_ptr,
                 frwd_offset_in_bytes,
                 cur_rd_offset,
                 new_rd_offset);
#endif

   /** set rd timestamp if writer timestamp is valid */
   if (wr_pos.is_ts_valid)
   {
      virt_buf_ptr->is_reader_ts_valid = wr_pos.is_ts_valid;
      virt_buf_ptr->reader_ts          = wr_pos.latest_write_sample_ts - actual_read_offset_in_us;
   }

   if (actual_read_offset_in_us_ptr)
   {
      *actual_read_offset_in_us_ptr = actual_read_offset_in_us;
   }

__err_bailout:

   AR_MSG_ISLAND(DBG_HIGH_PRIO,
                 "virt_buf: reader adjust, cur reader addr:0x%lx, ts:(valid: %lu, msw 0x%lx, lsw 0x%lx), "
                 "rd_offset:(%luus, %luus)",
                 virt_buf_ptr->read_ptr,
                 virt_buf_ptr->is_reader_ts_valid,
                 (uint32_t)(virt_buf_ptr->reader_ts >> 32),
                 (uint32_t)virt_buf_ptr->reader_ts,
                 requested_read_offset_in_us,
                 actual_read_offset_in_us);

   return result;
}
