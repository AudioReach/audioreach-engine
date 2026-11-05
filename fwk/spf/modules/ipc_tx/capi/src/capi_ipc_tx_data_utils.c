/* =========================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
 * =========================================================================*/

/**
 * \file capi_ipc_tx_fwk_extn_handlers.cpp
 *
 * C source file to implement the framework extension handlers for IPC_TX module
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------

 * =========================================================================*/

/**----------------------------------------------------------------------------
** Include Files
** -------------------------------------------------------------------------*/

#include "capi_ipc_tx_utils.h"

/**----------------------------------------------------------------------------
** Function Definitions
** -------------------------------------------------------------------------*/

static void ipc_tx_data_logging(capi_ipc_tx_t *me_ptr, int8_t *log_buf_ptr, uint32_t log_buf_fill_size);

/**
 * \brief Writes data from the input buffer to the shared buffer, handling different data interleaving formats.
 * This function checks the data interleaving format of the input buffer and writes the data to the shared buffer
 * accordingly. It supports three data interleaving formats: CAPI_INTERLEAVED, CAPI_DEINTERLEAVED_PACKED, and
 * CAPI_DEINTERLEAVED_UNPACKED_V2. For CAPI_DEINTERLEAVED_UNPACKED_V2 format, it converts the unpacked data to packed
 * format before writing it to the shared buffer. The function logs messages to indicate the progress and results of the
 * operation, and returns an error code if the data interleaving format is invalid.
 *
 * \param me_ptr Pointer to the capi_ipc_tx_t structure.
 * \param input Pointer to the input buffer.
 *
 * \return capi_err_t Result of the operation, which is either an error code or a success code.
 */
capi_err_t capi_ipc_tx_write_data(capi_ipc_tx_t *me_ptr, capi_stream_data_t *input[])
{
   capi_err_t result       = CAPI_EOK;

   me_ptr->sh_buf_info.actual_data_filled = 0;
   int8_t  *shm_data_buff_ptr   = (int8_t *)me_ptr->sh_buf_info.curr_buff;
   uint32_t shm_data_buff_size  = MIN(me_ptr->sh_buf_info.data_buff_size, me_ptr->sh_buf_info.frame_size_in_bytes);

   // check if fwk shared a buffer with the fwk.
   if(me_ptr->curr_shared_buf_ptr)
   {
      if(FALSE == me_ptr->is_mod_buf_access_enabled)
      {
         IPC_TX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                   "write_data: MOD_BUF_ACCESS: extn is not enabled, buffer should have already returned at this point.");
         capi_cmn_crash();
      }

      if(input[0]->buf_ptr[0].data_ptr != me_ptr->curr_shared_buf_ptr)
      {
         IPC_TX_MSG(me_ptr->miid,
                  DBG_ERROR_PRIO,
                  "write_data: MOD_BUF_ACCESS: Fwk returned an invalid buf ptr input[0]->buf_ptr[0].data_ptr = 0x%p != 0x%p",
                  input[0]->buf_ptr[0].data_ptr,
                  me_ptr->curr_shared_buf_ptr);
         capi_cmn_crash();
      }
#ifdef DEBUG_IPC_TX
      else
      {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                  "write_data: MOD_BUF_ACCESS: Fwk returned valid buf ptr input[0]->buf_ptr[0].data_ptr = 0x%p",
                  input[0]->buf_ptr[0].data_ptr);
      }
#endif
   }

   // mark the buffer as returned before continuing rest of the process.
   me_ptr->curr_shared_buf_ptr = NULL;

   //  (input[0]->buf_ptr->actual_data_len == input[0]->buf_ptr->max_data_len) If full frame is
   //  available - optimize the mem copy
   if ((CAPI_INTERLEAVED == me_ptr->inp_media_fmt.format.data_interleaving) ||
       (CAPI_DEINTERLEAVED_PACKED == me_ptr->inp_media_fmt.format.data_interleaving) ||
       (CAPI_RAW_COMPRESSED == me_ptr->inp_media_fmt.header.format_header.data_format))

   {
#ifdef DEBUG_IPC_TX
      IPC_TX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "data copy done, shm_data_buff_ptr= 0x%p, shm_data_buff_size = %d, "
                 "input[0]->buf_ptr->data_ptr = 0x%p, input[0]->buf_ptr->actual_data_len = %d",
                 shm_data_buff_ptr,
                 shm_data_buff_size,
                 input[0]->buf_ptr->data_ptr,
                 input[0]->buf_ptr->actual_data_len);
      IPC_TX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "data copy done, input[0]->buf_ptr->max_data_len = %d num_bufs = %d",
                 input[0]->buf_ptr->max_data_len,
                 1);
#endif
      if (shm_data_buff_ptr != input[0]->buf_ptr->data_ptr)
      {
         me_ptr->sh_buf_info.actual_data_filled = memscpy(shm_data_buff_ptr,
                                                          shm_data_buff_size,
                                                          input[0]->buf_ptr->data_ptr,
                                                          input[0]->buf_ptr->actual_data_len);
      }
      else
      {
#ifdef DEBUG_IPC_TX
         IPC_TX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "fwk reused CAPI ipc buffer shm_data_buff_ptr= 0x%p, shm_data_buff_size = %d, "
                    "input[0]->buf_ptr->data_ptr = 0x%p, input[0]->buf_ptr->actual_data_len = %d "
                    "input[0]->buf_ptr->max_data_len = %d",
                    shm_data_buff_ptr,
                    shm_data_buff_size,
                    input[0]->buf_ptr->data_ptr,
                    input[0]->buf_ptr->actual_data_len,
                    input[0]->buf_ptr->max_data_len);
#endif
         input[0]->buf_ptr->data_ptr = NULL;
         me_ptr->sh_buf_info.actual_data_filled = MIN(shm_data_buff_size, input[0]->buf_ptr->actual_data_len);
      }

      input[0]->buf_ptr->actual_data_len =  me_ptr->sh_buf_info.actual_data_filled;

#ifdef DEBUG_IPC_TX
      IPC_TX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "data copy done, shm_data_buff_ptr= 0x%p, shm_data_buff_size = %d, "
                 "input[0]->buf_ptr->data_ptr = 0x%p, input[0]->buf_ptr->actual_data_len = %d",
                 shm_data_buff_ptr,
                 shm_data_buff_size,
                 input[0]->buf_ptr->data_ptr,
                 input[0]->buf_ptr->actual_data_len);
      IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "data copy done, offset = %d", me_ptr->sh_buf_info.actual_data_filled);
#endif
   }
   // if module input unpacked, IPC module needs to convert to packed.
   else if (CAPI_DEINTERLEAVED_UNPACKED_V2 == me_ptr->inp_media_fmt.format.data_interleaving)
   {
      uint32_t num_channels = me_ptr->inp_media_fmt.format.num_channels;

#ifdef DEBUG_IPC_TX
      IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "CAPI_DEINTERLEAVED_UNPACKED_V2 data");

      IPC_TX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "data copy done, shm_data_buff_ptr= 0x%p, shm_data_buff_size = %d, "
                 "input[0]->buf_ptr->data_ptr = 0x%p, input[0]->buf_ptr->actual_data_len = %d",
                 shm_data_buff_ptr,
                 shm_data_buff_size,
                 input[0]->buf_ptr->data_ptr,
                 input[0]->buf_ptr->actual_data_len);
      IPC_TX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "data copy done, input[0]->buf_ptr->max_data_len = %d num_channels = %d",
                 input[0]->buf_ptr->max_data_len,
                 me_ptr->inp_media_fmt.format.num_channels);
#endif
      int8_t  *dest                    = (int8_t *)shm_data_buff_ptr;
      uint32_t sh_buf_max_bytes_per_ch = capi_cmn_divide(shm_data_buff_size, num_channels);
      uint32_t bytes_copied            = 0;

      uint32_t actual_data_len_ch = input[0]->buf_ptr[0].actual_data_len;
      if (input[0]->buf_ptr[0].data_ptr != dest)
      {
         for (uint32_t i = 0; i < input[0]->bufs_num && i < num_channels; i++)
         {
            // Update the destination pointer
            dest += bytes_copied;

            // Copy data from input buffer to shared data buffer in packed format
            bytes_copied = memscpy(dest,
                                   sh_buf_max_bytes_per_ch,
                                   input[0]->buf_ptr[i].data_ptr,
                                   actual_data_len_ch);

            me_ptr->sh_buf_info.actual_data_filled += bytes_copied;

#ifdef DEBUG_IPC_TX
            IPC_TX_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "buffer number = %d, num_channels = %d, bytes_copied = %d, "
                       "me_ptr->sh_buf_info.actual_data_filled "
                       "= %d, input[0]->buf_ptr[%d].data_ptr = 0x%p, dest = 0x%p",
                       i,
                       num_channels,
                       bytes_copied,
                       me_ptr->sh_buf_info.actual_data_filled,
                       i,
                       input[0]->buf_ptr[i].data_ptr,
                       dest);

            uint32_t *data = (uint32_t *)input[0]->buf_ptr[i].data_ptr;
            IPC_TX_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "write_data: input_data buf_ptr 0x%p, 0x%lX, 0x%lX, 0x%lX, 0x%lX",
                       data,
                       *data,
                       *(data + 1),
                       *(data + 2),
                       *(data + 3));

            data = (uint32_t *)dest;
            IPC_TX_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "write_data: output_data buf_ptr 0x%p, 0x%lX, 0x%lX, 0x%lX, 0x%lX",
                       data,
                       *data,
                       *(data + 1),
                       *(data + 2),
                       *(data + 3));

#endif
         }

         // for deinterleaved unpacked v2 only first ch needs to be updated
         input[0]->buf_ptr[0].actual_data_len = bytes_copied;
      }
      else // capi input == internal buf, extn case
      {
#ifdef DEBUG_IPC_TX_SAFE_MODE
         // iterate over input buffers and check if the buf ptrs are valid.
         for (uint32_t i = 0; i < input[0]->bufs_num; i++)
         {
            if ((dest != input[0]->buf_ptr[i].data_ptr) ||
                (sh_buf_max_bytes_per_ch < actual_data_len_ch))
            {
               IPC_TX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "write_data: Fwk returned an invalid buf ptr input[0]->buf_ptr[%lu].data_ptr = 0x%p != 0x%p, "
                          "actual len (%lu > %lu)",
                          i,
                          input[0]->buf_ptr[i].data_ptr,
                          dest,
                          actual_data_len_ch,
                          sh_buf_max_bytes_per_ch);
               result = CAPI_EBADPARAM;
            }

            dest += sh_buf_max_bytes_per_ch;
         }

         if(result == CAPI_EBADPARAM)
         {
            return(result);
         }
#endif
         // nothing to do if the buffer is filled
         // if the buffer is not filled, remove the holes between the channels to convert deinterleaved unpacked to deinterleaved packed.
         uint32_t actual_data_len_per_ch =input[0]->buf_ptr[0].actual_data_len;
         if(sh_buf_max_bytes_per_ch != input[0]->buf_ptr[0].actual_data_len)
         {
            for (uint32_t i = 0; i < input[0]->bufs_num && i < num_channels; i++)
            {
               dest +=  me_ptr->sh_buf_info.actual_data_filled;

               // Copy data from input buffer to shared data buffer in packed format
               memsmove(dest,
                        shm_data_buff_size - me_ptr->sh_buf_info.actual_data_filled,
                        input[0]->buf_ptr[i].data_ptr,
                        actual_data_len_per_ch);

               // entire input buffer is consumed hence not updating actual data len
               // input[0]->buf_ptr[i].actual_data_len = actual_data_len_per_ch;

               me_ptr->sh_buf_info.actual_data_filled += actual_data_len_per_ch;

#ifdef DEBUG_IPC_TX
               IPC_TX_MSG(me_ptr->miid,
                        DBG_HIGH_PRIO,
                        "buffer number = %d, num_channels = %d, bytes_copied = %d, me_ptr->sh_buf_info.actual_data_filled "
                        "= %d, input[0]->buf_ptr[%d].data_ptr = 0x%p, dest = 0x%p",
                        i,
                        num_channels,
                        actual_data_len_per_ch,
                        me_ptr->sh_buf_info.actual_data_filled,
                    i,
                    input[0]->buf_ptr[i].data_ptr,
                    dest);

         uint32_t *data = (uint32_t *)input[0]->buf_ptr[i].data_ptr;
         IPC_TX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "write_data: input_data buf_ptr 0x%p, 0x%lX, 0x%lX, 0x%lX, 0x%lX",
                    data,
                    *data,
                    *(data + 1),
                    *(data + 2),
                    *(data + 3));

         data = (uint32_t *)dest;
         IPC_TX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "write_data: output_data buf_ptr 0x%p, 0x%lX, 0x%lX, 0x%lX, 0x%lX",
                    data,
                    *data,
                    *(data + 1),
                    *(data + 2),
                    *(data + 3));

         #endif

               // clear the buffer from input
               input[0]->buf_ptr[i].data_ptr = NULL;
            }
         }
         else // buffer is full, hence it will be already packed no need to pack it
         {
            // clear the buf ptr for each channel
            for(uint32_t i = 0; i < input[0]->bufs_num; i++)
            {
               input[0]->buf_ptr[i].data_ptr = NULL;
            }
            me_ptr->sh_buf_info.actual_data_filled = shm_data_buff_size;

#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "write_data: data already packed, skipped packing in process context data copied: %lu ",
              me_ptr->sh_buf_info.actual_data_filled);
#endif
         }
      }
   }
   else
   {
      IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "write_data: invalid data interleaving");
      return CAPI_EBADPARAM;
   }

#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid, DBG_LOW_PRIO, "write_data: data copied: %lu ", me_ptr->sh_buf_info.actual_data_filled);
#endif

   if (CAPI_EOK !=
       (result = posal_cache_flush((uint32_t)me_ptr->sh_buf_info.curr_buff, me_ptr->sh_buf_info.data_buff_size)))
   {
      IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "write_data: failed to cache flush addr 0x%p size %lu");
      //return CAPI_EFAILED;
   }

   if(me_ptr->logging_info.cfg.log_code)
   {
      ipc_tx_data_logging(me_ptr, (int8_t *)me_ptr->sh_buf_info.curr_buff, me_ptr->sh_buf_info.actual_data_filled);
   }

   return CAPI_EOK;
}

/* Utility function to populate logging header and log data to diag */
static void ipc_tx_data_logging(capi_ipc_tx_t *me_ptr, int8_t *log_buf_ptr, uint32_t log_buf_fill_size)
{
   posal_data_log_info_t log_info_var;

   // skip calling data logger if log code is disabled
   uint32_t status = posal_data_log_code_status(me_ptr->logging_info.cfg.log_code);
   if (0 == status)
   {
      return;
   }

   log_info_var.log_code       = me_ptr->logging_info.cfg.log_code;
   log_info_var.buf_ptr        = log_buf_ptr;
   log_info_var.buf_size       = log_buf_fill_size;
   log_info_var.session_id     = ((me_ptr->logging_info.session_id << 16) | (me_ptr->miid & 0xFFFF));
   log_info_var.log_tap_id     = me_ptr->miid & 0x0FFF;
   log_info_var.log_time_stamp = posal_timer_get_time();

   if (!log_info_var.buf_size)
   {
      return;
   }

   switch (me_ptr->inp_media_fmt.header.format_header.data_format)
   {
      case CAPI_FIXED_POINT:
      case CAPI_FLOATING_POINT:
      {
         log_info_var.data_fmt = LOG_DATA_FMT_PCM;
#ifdef DEBUG_IPC_TX
         IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, " Logging as PCM");
#endif
         break;
      }
      case CAPI_DEINTERLEAVED_RAW_COMPRESSED:
      {
         log_info_var.data_fmt = LOG_DATA_FMT_PCM;
         IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, " Logging Deinterleaved Raw Compressed as PCM");
         break;
      }
      case CAPI_RAW_COMPRESSED:
      default:
      {
         log_info_var.data_fmt = LOG_DATA_FMT_BITSTREAM;
#ifdef DEBUG_IPC_TX
         IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, " Logging as BIN");
#endif
         break;
      }
   }

   log_info_var.seq_number_ptr = &(me_ptr->logging_info.seq_number);

   switch (me_ptr->inp_media_fmt.header.format_header.data_format)
   {
      case CAPI_RAW_COMPRESSED:
      {
         log_info_var.data_info.media_fmt_id = me_ptr->inp_media_fmt.format.bitstream_format;
         break;
      }
      default:
      {
         log_info_var.data_info.media_fmt_id = me_ptr->inp_media_fmt.format.bitstream_format;;
         posal_data_log_pcm_info_t *pcm_data = &(log_info_var.data_info.pcm_data_fmt);
         pcm_data->q_factor                  = me_ptr->inp_media_fmt.format.q_factor;
         pcm_data->data_format               = me_ptr->inp_media_fmt.header.format_header.data_format;
         pcm_data->num_channels              = me_ptr->inp_media_fmt.format.num_channels;
         pcm_data->sampling_rate             = me_ptr->inp_media_fmt.format.sampling_rate;
         pcm_data->bits_per_sample           = me_ptr->inp_media_fmt.format.bits_per_sample;
         pcm_data->interleaved               = me_ptr->inp_media_fmt.format.data_interleaving;
         pcm_data->channel_mapping           = (uint16_t *)(me_ptr->inp_media_fmt.channel_type);
         break;
      }
   }

#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, " logging one packet of data");
#endif

   /* Switch between Static or Dynamic PD */
   posal_data_log_alloc_commit(&log_info_var);

   return;
}
