/*==============================================================================
@file capi_ipc_rx_data_utils.c
@brief This file implements data processing and utility functions for the IPC RX CAPI module.

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/
/* clang-format off */
/* No Edit History for automated refactoring. */
/* clang-format on */

/*------------------------------------------------------------------------
 * Include Files
 * -----------------------------------------------------------------------*/
#include "capi_fwk_extns_ipc_port_handler.h"
#include "capi_ipc_rx_utils.h"

static capi_err_t capi_ipc_rx_process_share_capi_buffer_to_fwk(capi_ipc_rx_t         *me_ptr,
                                                               capi_stream_data_v2_t *output_strm_ptr);
static capi_err_t capi_ipc_rx_process_copy_data_to_fwk_buffer(capi_ipc_rx_t *me_ptr, capi_stream_data_t *output[]);
/*------------------------------------------------------------------------
 * Macros
 * -----------------------------------------------------------------------*/
/**<
 * \brief Macro to check if the interleaving is deinterleaved unpacked.
 *
 * This macro determines if the given interleaving type corresponds to
 * deinterleaved unpacked or deinterleaved unpacked v2 formats.
 */
#define IPC_RX_IS_ANY_DEINTERLEAVED_UNPACKED(interleaving)                                                             \
   ((TOPO_DEINTERLEAVED_UNPACKED | TOPO_DEINTERLEAVED_UNPACKED_V2) & interleaving)

#define CAPI_IS_PCM(data_format) ((CAPI_FIXED_POINT == data_format) || (CAPI_FLOATING_POINT == data_format))

/*------------------------------------------------------------------------
 * Static Function Definitions
 * -----------------------------------------------------------------------*/

/**
 * \brief Converts length per buffer to length per channel based on media format.
 *
 * This function calculates the data length per channel. For PCM/packetized
 * data that is deinterleaved packed or interleaved, it divides the buffer length
 * by the number of channels. For other formats (raw compressed, PCM unpacked),
 * the length per buffer is used directly. This is crucial for accurate
 * metadata propagation and timestamp calculations.
 *
 * \param[in] media_fmt_ptr Pointer to the media format structure.
 * \param[in] len_per_buf   Length of data per buffer in bytes.
 *
 * \return Length of data per channel in bytes. Returns 0 if `num_channels` is zero
 *         to prevent division by zero errors.
 */
static inline uint32_t ipc_rx_convert_len_per_buf_to_len_per_ch(capi_media_fmt_v2_t *media_fmt_ptr,
                                                                uint32_t             len_per_buf)
{
   if (NULL == media_fmt_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: NULL media_fmt_ptr in ipc_rx_convert_len_per_buf_to_len_per_ch.");
      return 0; /* Return 0 to indicate error or inability to calculate */
   }

   // Check if data is PCM/packetized and not deinterleaved unpacked
   if (CAPI_IS_PCM(media_fmt_ptr->header.format_header.data_format) &&
       (CAPI_DEINTERLEAVED_UNPACKED_V2 != media_fmt_ptr->format.data_interleaving))
   {
      /* Avoid division by zero if num_channels is zero */
      if (0 == media_fmt_ptr->format.num_channels)
      {
         IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: Number of channels is zero for PCM/packetized data.");
         return 0;
      }
      return capi_cmn_div_num(len_per_buf, media_fmt_ptr->format.num_channels);
   }
   return len_per_buf; /* For other formats, length per buffer is same as length per channel */
}

/**
 * \brief Converts bytes per channel to number of samples per channel.
 *
 * This function computes the number of samples from the given number of bytes
 * per channel and the media format's bits per sample. It includes an optimization
 * for power-of-2 bit samples for efficiency.
 *
 * \param[in] bytes_per_ch  Number of bytes per channel.
 * \param[in] media_fmt_ptr Pointer to the media format structure.
 *
 * \return Number of samples per channel. Returns 0 if `bytes_per_sample` is zero
 *         to prevent division by zero errors.
 */
static inline uint32_t ipc_rx_bytes_per_ch_to_samples(uint32_t bytes_per_ch, capi_media_fmt_v2_t *media_fmt_ptr)
{
   if (NULL == media_fmt_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL media_fmt_ptr in ipc_rx_bytes_per_ch_to_samples.");
      return 0; /* Return 0 to indicate error or inability to calculate */
   }

   uint32_t samples;
   uint32_t bytes_per_sample = CAPI_CMN_BITS_TO_BYTES(media_fmt_ptr->format.bits_per_sample);

   /* Avoid division by zero */
   if (0 == bytes_per_sample)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: Bytes per sample is zero in ipc_rx_bytes_per_ch_to_samples.");
      return 0;
   }

   /* Optimized for power of 2 bit samples for faster calculation */
   if (CAPI_CMN_IS_POW_OF_2(media_fmt_ptr->format.bits_per_sample))
   {
      /* s32_shr_s32_sat for bits_per_sample and then -3 for bits to bytes conversion.
       * Equivalent to (bytes_per_ch >> (log2(bits_per_sample) - 3)) */
      samples = s32_shr_s32_sat(bytes_per_ch, (s32_get_lsb_s32(media_fmt_ptr->format.bits_per_sample) - 3));
   }
   else
   {
      samples = bytes_per_ch / bytes_per_sample;
   }
   return samples;
}

/*------------------------------------------------------------------------
 * Function Definitions
 * -----------------------------------------------------------------------*/

/**
 * \brief Handles flush operations for the IPC_RX module.
 *
 * This function acknowledges any pending IPC buffers and media format packets,
 * clears the internal buffer, and handles newly received GPR packets (e.g.,
 * data commands or media format commands) by acknowledging them. It ensures
 * all pending data and commands are properly cleared from the module's state.
 *
 * \param[in] me_ptr  Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] pkt_ptr Optional. Pointer to a new GPR packet to be flushed.
 *                    If provided, this packet is also acknowledged and freed.
 *
 * \return AR_EOK on success. Returns AR_EBADPARAM if `me_ptr` is NULL,
 *         or other `AR_DID_FAIL` codes if GPR command ending or data freeing fails.
 */
ar_result_t ipc_rx_flush_handling(capi_ipc_rx_t *me_ptr, gpr_packet_t *pkt_ptr, bool_t is_ref_counted)
{
   ar_result_t result = AR_EOK;

   if (NULL == me_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL me_ptr in ipc_rx_flush_handling.");
      return AR_EBADPARAM;
   }

   /* Acknowledge and clear pending IPC buffer if any */
   if (NULL != me_ptr->ipc_buf_packet_ptr)
   {
      ar_result_t free_result =
         ipc_rx_free_input_data_cmd(me_ptr, me_ptr->ipc_buf_packet_ptr, AR_EOK, FALSE, is_ref_counted);
      if (AR_DID_FAIL(free_result))
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Failed to free IPC buffer packet during flush: %lu.",
                    free_result);

         result |= free_result; /* Continue processing, but indicate failure */
      }
      me_ptr->ipc_buf_packet_ptr      = NULL;
      me_ptr->ipc_buf_actual_data_len = 0;
      me_ptr->ipc_buf_size            = 0;
      me_ptr->ipc_buf_virtual_addr    = NULL;
      ipc_rx_destroy_all_md(me_ptr, &me_ptr->md_buf_info.md_list_ptr);
   }

   /* Acknowledge and clear pending media format packet if any */
   if (NULL != me_ptr->mf_packet_ptr)
   {
      ar_result_t gpr_result = __gpr_cmd_end_command(me_ptr->mf_packet_ptr, AR_EOK);
      if (AR_DID_FAIL(gpr_result))
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Failed to end media format packet during flush: %lu.",
                    gpr_result);

         result |= gpr_result; /* Propagate error if ending MF packet failed */
      }
      me_ptr->mf_packet_ptr = NULL;
      me_ptr->is_pending_mf = FALSE;
   }

   /* Clear internal buffer if it contains data */
   if ((NULL != me_ptr->int_buf_ptr) && (me_ptr->int_buf_actual_data_len > 0))
   {
      memset(me_ptr->int_buf_ptr, 0, me_ptr->int_buf_max_data_len);
      me_ptr->int_buf_actual_data_len = 0;
      ipc_rx_destroy_all_md(me_ptr, &me_ptr->int_buf_md_list_ptr);
   }

   /* If a new GPR packet is provided for flush, handle it */
   if (NULL != pkt_ptr)
   {
      ar_result_t pkt_handle_result = AR_EOK;
      switch (pkt_ptr->opcode)
      {
         case DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2:
         {
            pkt_handle_result = ipc_rx_free_input_data_cmd(me_ptr, pkt_ptr, AR_EOK, FALSE, is_ref_counted);
            if (AR_DID_FAIL(pkt_handle_result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to free data buffer command during flush: %lu.",
                          pkt_handle_result);

               result |= pkt_handle_result; /* Propagate error */
            }
            break;
         }
         case DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT:
         {
            pkt_handle_result = __gpr_cmd_end_command(pkt_ptr, AR_EOK);
            if (AR_DID_FAIL(pkt_handle_result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to end media format command during flush: %lu.",
                          pkt_handle_result);

               result |= pkt_handle_result; /* Propagate error */
            }
            break;
         }
         default:
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_LOW_PRIO,
                       "INFO: Unsupported GPR opcode 0x%lX for flush handling. Ending command.",
                       pkt_ptr->opcode);

            pkt_handle_result = __gpr_cmd_end_command(pkt_ptr, AR_EOK); /* Always end the command to prevent leakage */
            if (AR_DID_FAIL(pkt_handle_result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to end unsupported command during flush: %lu.",
                          pkt_handle_result);
               result |= pkt_handle_result; /* Propagate error */
            }
            break;
         }
      }
   }

   return result;
}

/**
 * \brief Copies data from the IPC buffer to the internal module buffer.
 *
 * This function handles both interleaved and deinterleaved packed PCM data formats.
 * It copies data from `me_ptr->ipc_buf_virtual_addr` to `me_ptr->int_buf_ptr`,
 * managing read/write indices and updating `ipc_buf_actual_data_len` and
 * `int_buf_actual_data_len`. It ensures safe memory copy operations within
 * buffer boundaries.
 *
 * \param[in,out] me_ptr Pointer to the CAPI IPC_RX instance. Must not be NULL.
 *
 * \return AR_EOK on success.
 *         AR_EBADPARAM if `me_ptr` or associated buffer pointers are invalid,
 *                      or if media format parameters (bytes_per_sample, num_channels) are zero.
 *         AR_EFAILED if a buffer overflow is detected during memory copy.
 */
ar_result_t ipc_rx_copy_ipc_buffer_to_int_buffer(capi_ipc_rx_t *me_ptr)
{
   // Validate input pointers
   if (NULL == me_ptr || NULL == me_ptr->ipc_buf_virtual_addr || NULL == me_ptr->int_buf_ptr)
   {
      IPC_RX_MSG(me_ptr ? me_ptr->miid : MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: NULL pointer detected in ipc_rx_copy_ipc_buffer_to_int_buffer.");
      return AR_EBADPARAM;
   }

   capi_media_fmt_v2_t input_media_fmt = me_ptr->input_media_fmt;
   uint32_t            bytes_to_copy   = 0;
   int8_t             *src_ptr         = NULL;
   int8_t             *dst_ptr         = NULL;

   if (CAPI_FIXED_POINT == input_media_fmt.header.format_header.data_format)
   {
      uint32_t bytes_per_sample = TOPO_BITS_TO_BYTES(input_media_fmt.format.bits_per_sample);
      uint32_t num_channels     = input_media_fmt.format.num_channels;

      /* Validate media format parameters */
      if ((0 == bytes_per_sample) || (0 == num_channels))
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Invalid media format for PCM: bytes_per_sample=%lu, num_channels=%lu.",
                    bytes_per_sample,
                    num_channels);
         return AR_EBADPARAM;
      }

      /* Calculate samples available in IPC buffer and space available in internal buffer */
      uint32_t in_num_samples_per_chan =
         topo_div_num(me_ptr->ipc_buf_actual_data_len, (bytes_per_sample * num_channels));
      uint32_t out_num_samples_per_chan = topo_div_num((me_ptr->int_buf_max_data_len - me_ptr->int_buf_actual_data_len),
                                                       (bytes_per_sample * num_channels));
      uint32_t samples_to_copy_per_chan = MIN(in_num_samples_per_chan, out_num_samples_per_chan);

      if (0 == samples_to_copy_per_chan && (NULL == me_ptr->md_buf_info.md_list_ptr))
      {
         IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: No samples or MD to copy from IPC to internal buffer.");

         return AR_EOK; /* Nothing to copy */
      }

      uint32_t out_num_bytes_per_ch = samples_to_copy_per_chan * bytes_per_sample;
      bytes_to_copy                 = out_num_bytes_per_ch * num_channels;

      src_ptr = me_ptr->ipc_buf_virtual_addr + (me_ptr->ipc_buf_size - me_ptr->ipc_buf_actual_data_len);
      dst_ptr = me_ptr->int_buf_ptr + me_ptr->int_buf_actual_data_len;

      if (CAPI_INTERLEAVED == input_media_fmt.format.data_interleaving)
      {
         /* Perform bounds checking for memscpy to prevent buffer overflows */
         if (bytes_to_copy > me_ptr->ipc_buf_actual_data_len ||                                /* Check source bounds */
             bytes_to_copy > (me_ptr->int_buf_max_data_len - me_ptr->int_buf_actual_data_len)) /* Check dest bounds */
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Interleaved copy to internal buffer out of bounds. bytes_to_copy=%lu, "
                       "ipc_buf_actual_data_len=%lu, int_buf_max_data_len=%lu, int_buf_actual_data_len=%lu.",
                       bytes_to_copy,
                       me_ptr->ipc_buf_actual_data_len,
                       me_ptr->int_buf_max_data_len,
                       me_ptr->int_buf_actual_data_len);
            return AR_EFAILED;
         }
         memscpy(dst_ptr, bytes_to_copy, src_ptr, bytes_to_copy);
      }
      else /* Deinterleaved packed case (handled as deinterleaved unpacked for module's internal buffer) */
      {
         uint32_t current_src_offset = (me_ptr->ipc_buf_size - me_ptr->ipc_buf_actual_data_len) / num_channels;
         uint32_t current_dst_offset = me_ptr->int_buf_actual_data_len / num_channels;

         for (uint32_t ch_idx = 0; ch_idx < num_channels; ch_idx++)
         {
            uint32_t src_channel_ptr_offset = current_src_offset + (ch_idx * (me_ptr->ipc_buf_size / num_channels));
            uint32_t dst_channel_ptr_offset =
               current_dst_offset + (ch_idx * (me_ptr->int_buf_max_data_len / num_channels));

            /* Check for buffer overflow before copying each channel */
            if ((src_channel_ptr_offset + out_num_bytes_per_ch > me_ptr->ipc_buf_size) ||
                (dst_channel_ptr_offset + out_num_bytes_per_ch > me_ptr->int_buf_max_data_len))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Buffer overflow detected during deinterleaved channel copy to internal buffer. "
                          "ch_idx=%lu.",
                          ch_idx);
               return AR_EFAILED;
            }
            memscpy(me_ptr->int_buf_ptr + dst_channel_ptr_offset,
                    out_num_bytes_per_ch,
                    me_ptr->ipc_buf_virtual_addr + src_channel_ptr_offset,
                    out_num_bytes_per_ch);
         }
      }

      // for both interleaved and deinterleaved MD offset is relative to the per ch offset, hence
      // MD propagation parameters are same.
      if (me_ptr->md_buf_info.md_list_ptr || me_ptr->int_buf_md_list_ptr)
      {
         uint32_t ipc_buf_len_per_ch_before = in_num_samples_per_chan * bytes_per_sample;
         uint32_t ipc_buf_len_per_ch_after  = (ipc_buf_len_per_ch_before - out_num_bytes_per_ch);

         uint32_t int_buf_len_per_ch_before = topo_div_num(me_ptr->int_buf_actual_data_len, num_channels);
         uint32_t int_buf_len_per_ch_after  = int_buf_len_per_ch_before + out_num_bytes_per_ch;

         capi_stream_data_v2_t src_stream;
         capi_stream_data_v2_t dst_stream;
         src_stream.metadata_list_ptr = me_ptr->md_buf_info.md_list_ptr;
         dst_stream.metadata_list_ptr = me_ptr->int_buf_md_list_ptr;
         ar_result_t result           = ipc_rx_prop_md_from_src_buf_to_dst_buf(me_ptr,
                                                                     &me_ptr->input_media_fmt,
                                                                     ipc_buf_len_per_ch_before,
                                                                     ipc_buf_len_per_ch_after,
                                                                     &src_stream,
                                                                     int_buf_len_per_ch_before,
                                                                     int_buf_len_per_ch_after,
                                                                     &dst_stream);
         if (AR_FAILED(result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Failed to propagate MD 0x%lx from IPC to internal buf 0x%lx",
                       src_stream.metadata_list_ptr,
                       dst_stream.metadata_list_ptr);
            return AR_EFAILED;
         }

         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Propagated MD from IPC md list 0x%lx to internal buf md list 0x%lx",
                    src_stream.metadata_list_ptr,
                    dst_stream.metadata_list_ptr);

         me_ptr->md_buf_info.md_list_ptr = src_stream.metadata_list_ptr;
         me_ptr->int_buf_md_list_ptr     = dst_stream.metadata_list_ptr;
      }

      me_ptr->int_buf_actual_data_len += bytes_to_copy;
      me_ptr->ipc_buf_actual_data_len -= bytes_to_copy;
   }
   else /* Non-PCM data formats: direct copy */
   {
      bytes_to_copy =
         MIN(me_ptr->ipc_buf_actual_data_len, (me_ptr->int_buf_max_data_len - me_ptr->int_buf_actual_data_len));

      if (0 == bytes_to_copy)
      {
         IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: No bytes to copy from IPC to internal buffer (non-PCM).");
         return AR_EOK; /* Nothing to copy */
      }

      src_ptr = me_ptr->ipc_buf_virtual_addr + (me_ptr->ipc_buf_size - me_ptr->ipc_buf_actual_data_len);
      dst_ptr = me_ptr->int_buf_ptr + me_ptr->int_buf_actual_data_len;

      /* Perform bounds checking for memscpy */
      if (bytes_to_copy > me_ptr->ipc_buf_actual_data_len ||                                /* Check source bounds */
          bytes_to_copy > (me_ptr->int_buf_max_data_len - me_ptr->int_buf_actual_data_len)) /* Check dest bounds */
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Non-PCM copy to internal buffer out of bounds. bytes_to_copy=%lu, "
                    "ipc_buf_actual_data_len=%lu, int_buf_max_data_len=%lu, int_buf_actual_data_len=%lu.",
                    bytes_to_copy,
                    me_ptr->ipc_buf_actual_data_len,
                    me_ptr->int_buf_max_data_len,
                    me_ptr->int_buf_actual_data_len);
         return AR_EFAILED;
      }
      memscpy(dst_ptr, bytes_to_copy, src_ptr, bytes_to_copy);

      if (me_ptr->md_buf_info.md_list_ptr)
      {
         // for raw case per ch is nothin but the lenght of the buffer
         uint32_t ipc_buf_len_before = me_ptr->ipc_buf_actual_data_len;
         uint32_t ipc_buf_len_after  = (ipc_buf_len_before - bytes_to_copy);

         uint32_t int_buf_len_before = me_ptr->int_buf_actual_data_len;
         uint32_t int_buf_len_after  = int_buf_len_before + bytes_to_copy;

         capi_stream_data_v2_t src_stream;
         capi_stream_data_v2_t dst_stream;
         src_stream.metadata_list_ptr = me_ptr->md_buf_info.md_list_ptr;
         dst_stream.metadata_list_ptr = me_ptr->int_buf_md_list_ptr;
         ar_result_t result           = ipc_rx_prop_md_from_src_buf_to_dst_buf(me_ptr,
                                                                     &me_ptr->input_media_fmt,
                                                                     ipc_buf_len_before,
                                                                     ipc_buf_len_after,
                                                                     &src_stream,
                                                                     int_buf_len_before,
                                                                     int_buf_len_after,
                                                                     &dst_stream);

         me_ptr->md_buf_info.md_list_ptr = src_stream.metadata_list_ptr;
         me_ptr->int_buf_md_list_ptr     = dst_stream.metadata_list_ptr;

         if (AR_FAILED(result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Failed to propagate MD 0x%lx from IPC to internal buf 0x%lx",
                       me_ptr->md_buf_info.md_list_ptr,
                       me_ptr->int_buf_md_list_ptr);
            return AR_EFAILED;
         }

         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Propagated MD from IPC md list 0x%lx to internal buf md list 0x%lx",
                    me_ptr->md_buf_info.md_list_ptr,
                    me_ptr->int_buf_md_list_ptr);
      }

      me_ptr->ipc_buf_actual_data_len -= bytes_to_copy;
      me_ptr->int_buf_actual_data_len += bytes_to_copy;
   }

   IPC_RX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "INFO: Copied data from IPC buffer to internal buffer. IPC buffer remaining: %lu bytes, copied: %lu "
              "int_buf_actual_data_len %lu "
              "bytes.",
              me_ptr->ipc_buf_actual_data_len,
              bytes_to_copy,
              me_ptr->int_buf_actual_data_len);

   return AR_EOK;
}

/**
 * \brief Copies data directly from the IPC buffer to the CAPI output buffer.
 *
 * This function handles both interleaved and deinterleaved packed PCM data formats.
 * It directly copies data from `me_ptr->ipc_buf_virtual_addr` to
 * `output[0]->buf_ptr->data_ptr` (or individual channel buffers for deinterleaved).
 * It ensures safe memory copy operations within buffer boundaries.
 *
 * \param[in,out] me_ptr  Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[out] output Array of pointers to output stream data buffers. Must not be NULL.
 *
 * \return AR_EOK on success.
 *         AR_EBADPARAM if `me_ptr` or `output` pointers are invalid,
 *                      or if media format parameters are zero, or output buffer for channel is invalid.
 *         AR_EFAILED if a buffer overflow is detected during memory copy.
 */
ar_result_t ipc_rx_copy_ipc_buffer_to_output_buffer(capi_ipc_rx_t *me_ptr, capi_stream_data_t *output[])
{
   // Validate input pointers
   if (NULL == me_ptr || NULL == output || NULL == output[0] || NULL == output[0]->buf_ptr ||
       NULL == me_ptr->ipc_buf_virtual_addr)
   {
      IPC_RX_MSG(me_ptr ? me_ptr->miid : MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: NULL pointer detected in ipc_rx_copy_ipc_buffer_to_output_buffer.");
      return AR_EBADPARAM;
   }

   capi_media_fmt_v2_t input_media_fmt = me_ptr->input_media_fmt;
   uint32_t            bytes_to_copy   = 0;
   int8_t             *src_ptr         = NULL;
   int8_t             *dst_ptr         = NULL;

   if (CAPI_FIXED_POINT == input_media_fmt.header.format_header.data_format)
   {
      uint32_t bytes_per_sample = TOPO_BITS_TO_BYTES(input_media_fmt.format.bits_per_sample);
      uint32_t num_channels     = input_media_fmt.format.num_channels;

      /* Validate media format parameters */
      if ((0 == bytes_per_sample) || (0 == num_channels))
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Invalid media format for PCM: bytes_per_sample=%lu, num_channels=%lu.",
                    bytes_per_sample,
                    num_channels);
         return AR_EBADPARAM;
      }

      /* Calculate samples available in IPC buffer and space available in output buffer */
      uint32_t in_num_samples_per_chan =
         topo_div_num(me_ptr->ipc_buf_actual_data_len, (bytes_per_sample * num_channels));
      uint32_t out_num_bytes_per_buf_max = (output[0]->buf_ptr->max_data_len - output[0]->buf_ptr->actual_data_len);

      uint32_t out_num_bytes_per_ch_from_buf =
         ipc_rx_convert_len_per_buf_to_len_per_ch(&me_ptr->input_media_fmt, out_num_bytes_per_buf_max);
      uint32_t out_num_samples_per_chan =
         ipc_rx_bytes_per_ch_to_samples(out_num_bytes_per_ch_from_buf, &me_ptr->input_media_fmt);

      uint32_t samples_to_copy_per_chan = MIN(in_num_samples_per_chan, out_num_samples_per_chan);

      if (0 == samples_to_copy_per_chan && (NULL == me_ptr->md_buf_info.md_list_ptr))
      {
         IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: No samples to copy from IPC to output buffer.");
         return AR_EOK; /* Nothing to copy */
      }

      uint32_t out_num_bytes_per_ch = samples_to_copy_per_chan * bytes_per_sample;
      bytes_to_copy                 = out_num_bytes_per_ch * num_channels;

      src_ptr = me_ptr->ipc_buf_virtual_addr + (me_ptr->ipc_buf_size - me_ptr->ipc_buf_actual_data_len);
      dst_ptr = output[0]->buf_ptr->data_ptr + output[0]->buf_ptr->actual_data_len;

      uint32_t out_buf_len_per_ch_before = 0;
      if (CAPI_INTERLEAVED == input_media_fmt.format.data_interleaving)
      {
         out_buf_len_per_ch_before = topo_div_num(output[0]->buf_ptr->actual_data_len, num_channels);

         /* Perform bounds checking for memscpy */
         if (bytes_to_copy > me_ptr->ipc_buf_actual_data_len || /* Check source bounds */
             bytes_to_copy >
                (output[0]->buf_ptr->max_data_len - output[0]->buf_ptr->actual_data_len)) /* Check dest bounds */
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Interleaved copy to output buffer out of bounds. bytes_to_copy=%lu, "
                       "ipc_buf_actual_data_len=%lu, output_buf_max_data_len=%lu, output_buf_actual_data_len=%lu.",
                       bytes_to_copy,
                       me_ptr->ipc_buf_actual_data_len,
                       output[0]->buf_ptr->max_data_len,
                       output[0]->buf_ptr->actual_data_len);
            return AR_EFAILED;
         }
         memscpy(dst_ptr, bytes_to_copy, src_ptr, bytes_to_copy);
         output[0]->buf_ptr->actual_data_len += bytes_to_copy;
      }
      else /* Deinterleaved packed case (handled as deinterleaved unpacked for output buffer) */
      {
         out_buf_len_per_ch_before = output[0]->buf_ptr->actual_data_len;

         uint32_t current_src_offset = (me_ptr->ipc_buf_size - me_ptr->ipc_buf_actual_data_len) / num_channels;
         uint32_t current_dst_offset =
            output[0]->buf_ptr->actual_data_len; /* This is the base offset for channels in the output */

         for (uint32_t ch_idx = 0; ch_idx < num_channels; ch_idx++)
         {
            uint32_t src_channel_ptr_offset = current_src_offset + (ch_idx * (me_ptr->ipc_buf_size / num_channels));

            /* Validate output buffer for the current channel */
            if (ch_idx >= output[0]->bufs_num || NULL == output[0]->buf_ptr[ch_idx].data_ptr)
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Output buffer for channel %lu is invalid in ipc_rx_copy_ipc_buffer_to_output_buffer.",
                          ch_idx);
               return AR_EBADPARAM;
            }

            /* Check for buffer overflow before copying each channel */
            if ((src_channel_ptr_offset + out_num_bytes_per_ch > me_ptr->ipc_buf_size) ||
                (current_dst_offset + out_num_bytes_per_ch > output[0]->buf_ptr[0].max_data_len))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Buffer overflow detected during deinterleaved channel copy to output. ch_idx=%lu.",
                          ch_idx);
               return AR_EFAILED;
            }
            memscpy(output[0]->buf_ptr[ch_idx].data_ptr + current_dst_offset,
                    out_num_bytes_per_ch,
                    me_ptr->ipc_buf_virtual_addr + src_channel_ptr_offset,
                    out_num_bytes_per_ch);
         }
         /* For deinterleaved, only the first buffer's actual_data_len typically reflects total samples */
         output[0]->buf_ptr[0].actual_data_len += out_num_bytes_per_ch;
      }
      me_ptr->ipc_buf_actual_data_len -= bytes_to_copy;

      if (me_ptr->md_buf_info.md_list_ptr)
      {
         capi_stream_data_v2_t *output_strm_ptr = (capi_stream_data_v2_t *)output[0];

         uint32_t ipc_buf_len_per_ch_before = in_num_samples_per_chan * bytes_per_sample;
         uint32_t ipc_buf_len_per_ch_after  = (ipc_buf_len_per_ch_before - out_num_bytes_per_ch);

         uint32_t out_buf_len_per_ch_after = out_buf_len_per_ch_before + out_num_bytes_per_ch;

         capi_stream_data_v2_t src_stream;
         src_stream.metadata_list_ptr = me_ptr->md_buf_info.md_list_ptr;
         ar_result_t result = ipc_rx_prop_md_from_src_buf_to_dst_buf(me_ptr,
                                                                     &me_ptr->input_media_fmt,
                                                                     ipc_buf_len_per_ch_before,
                                                                     ipc_buf_len_per_ch_after,
                                                                     &src_stream,
                                                                     out_buf_len_per_ch_before,
                                                                     out_buf_len_per_ch_after,
                                                                     output_strm_ptr);

         me_ptr->md_buf_info.md_list_ptr = src_stream.metadata_list_ptr;
         if (AR_FAILED(result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Failed to propagate MD 0x%lx from IPC buf to capi output buf 0x%lx",
                       me_ptr->md_buf_info.md_list_ptr,
                       output_strm_ptr->metadata_list_ptr);
            return AR_EFAILED;
         }

         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Propagated MD from IPC buf md list 0x%lx to capi output buf md list 0x%lx",
                    me_ptr->md_buf_info.md_list_ptr,
                    output_strm_ptr->metadata_list_ptr);
      }
   }
   else /* Non-PCM data formats: direct copy */
   {
      bytes_to_copy = MIN(me_ptr->ipc_buf_actual_data_len,
                          (output[0]->buf_ptr[0].max_data_len - output[0]->buf_ptr[0].actual_data_len));

      uint32_t out_buf_len_before = output[0]->buf_ptr[0].actual_data_len;

      if (0 == bytes_to_copy)
      {
         IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: No bytes to copy from IPC to output buffer (non-PCM).");
         return AR_EOK; /* Nothing to copy */
      }

      src_ptr = me_ptr->ipc_buf_virtual_addr + (me_ptr->ipc_buf_size - me_ptr->ipc_buf_actual_data_len);
      dst_ptr = output[0]->buf_ptr[0].data_ptr + output[0]->buf_ptr[0].actual_data_len;

      /* Perform bounds checking for memscpy */
      if (bytes_to_copy > me_ptr->ipc_buf_actual_data_len || /* Check source bounds */
          bytes_to_copy >
             (output[0]->buf_ptr[0].max_data_len - output[0]->buf_ptr[0].actual_data_len)) /* Check dest bounds */
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Non-PCM copy to output buffer out of bounds. bytes_to_copy=%lu, "
                    "ipc_buf_actual_data_len=%lu, output_buf_max_data_len=%lu, output_buf_actual_data_len=%lu.",
                    bytes_to_copy,
                    me_ptr->ipc_buf_actual_data_len,
                    output[0]->buf_ptr[0].max_data_len,
                    output[0]->buf_ptr[0].actual_data_len);
         return AR_EFAILED;
      }
      memscpy(dst_ptr, bytes_to_copy, src_ptr, bytes_to_copy);

      me_ptr->ipc_buf_actual_data_len -= bytes_to_copy;
      output[0]->buf_ptr[0].actual_data_len += bytes_to_copy;

      if (me_ptr->md_buf_info.md_list_ptr)
      {
         capi_stream_data_v2_t *output_strm_ptr = (capi_stream_data_v2_t *)output[0];

         uint32_t ipc_buf_len_before = me_ptr->ipc_buf_actual_data_len;
         uint32_t ipc_buf_len_after  = (ipc_buf_len_before - bytes_to_copy);

         uint32_t out_buf_len_after = out_buf_len_before + bytes_to_copy;

         capi_stream_data_v2_t src_stream;
         src_stream.metadata_list_ptr = me_ptr->md_buf_info.md_list_ptr;
         ar_result_t result = ipc_rx_prop_md_from_src_buf_to_dst_buf(me_ptr,
                                                                     &me_ptr->input_media_fmt,
                                                                     ipc_buf_len_before,
                                                                     ipc_buf_len_after,
                                                                     &src_stream,
                                                                     out_buf_len_before,
                                                                     out_buf_len_after,
                                                                     output_strm_ptr);

         me_ptr->md_buf_info.md_list_ptr = src_stream.metadata_list_ptr;

         if (AR_FAILED(result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Failed to propagate MD 0x%lx from IPC buf to capi output buf 0x%lx",
                       me_ptr->md_buf_info.md_list_ptr,
                       output_strm_ptr->metadata_list_ptr);
            return AR_EFAILED;
         }

         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Propagated MD from IPC buf md list 0x%lx to capi output buf md list 0x%lx",
                    me_ptr->md_buf_info.md_list_ptr,
                    output_strm_ptr->metadata_list_ptr);
      }
   }

   IPC_RX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "INFO: Copied data from IPC buffer to output buffer. IPC buffer remaining: %lu bytes, copied: %lu out "
              "actual len %lu bytes.",
              me_ptr->ipc_buf_actual_data_len,
              bytes_to_copy,
              output[0]->buf_ptr[0].actual_data_len);
   return AR_EOK;
}

/**
 * \brief Copies data from the internal module buffer to the CAPI output buffer.
 *
 * This function handles both interleaved and deinterleaved packed PCM data formats.
 * It copies data from `me_ptr->int_buf_ptr` to `output[0]->buf_ptr->data_ptr`
 * (or individual channel buffers for deinterleaved). It ensures safe memory
 * copy operations within buffer boundaries.
 *
 * \param[in,out] me_ptr  Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[out] output Array of pointers to output stream data buffers. Must not be NULL.
 *
 * \return AR_EOK on success.
 *         AR_EBADPARAM if `me_ptr` or `output` pointers are invalid,
 *                      or if media format parameters are zero, or output buffer for channel is invalid.
 *         AR_EFAILED if a buffer overflow is detected during memory copy.
 */
ar_result_t ipc_rx_copy_int_buffer_to_output_buffer(capi_ipc_rx_t *me_ptr, capi_stream_data_t *output[])
{
   // Validate input pointers
   if (NULL == me_ptr || NULL == output || NULL == output[0] || NULL == output[0]->buf_ptr ||
       NULL == me_ptr->int_buf_ptr)
   {
      IPC_RX_MSG(me_ptr ? me_ptr->miid : MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: NULL pointer detected in ipc_rx_copy_int_buffer_to_output_buffer.");
      return AR_EBADPARAM;
   }

   capi_media_fmt_v2_t input_media_fmt = me_ptr->input_media_fmt;
   uint32_t            bytes_to_copy   = 0;
   int8_t             *src_ptr         = NULL;
   int8_t             *dst_ptr         = NULL;

   if (CAPI_FIXED_POINT == input_media_fmt.header.format_header.data_format)
   {
      uint32_t bytes_per_sample = TOPO_BITS_TO_BYTES(input_media_fmt.format.bits_per_sample);
      uint32_t num_channels     = input_media_fmt.format.num_channels;

      /* Validate media format parameters */
      if ((0 == bytes_per_sample) || (0 == num_channels))
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Invalid media format for PCM: bytes_per_sample=%lu, num_channels=%lu.",
                    bytes_per_sample,
                    num_channels);
         return AR_EBADPARAM;
      }

      /* Calculate samples available in internal buffer and space available in output buffer */
      uint32_t in_num_samples_per_chan =
         topo_div_num(me_ptr->int_buf_actual_data_len, (bytes_per_sample * num_channels));
      uint32_t out_num_bytes_per_buf_max = (output[0]->buf_ptr->max_data_len - output[0]->buf_ptr->actual_data_len);

      uint32_t out_num_bytes_per_ch_from_buf =
         ipc_rx_convert_len_per_buf_to_len_per_ch(&me_ptr->input_media_fmt, out_num_bytes_per_buf_max);
      uint32_t out_num_samples_per_chan =
         ipc_rx_bytes_per_ch_to_samples(out_num_bytes_per_ch_from_buf, &me_ptr->input_media_fmt);

      uint32_t samples_to_copy_per_chan = MIN(in_num_samples_per_chan, out_num_samples_per_chan);

      if (0 == samples_to_copy_per_chan && (NULL == me_ptr->int_buf_md_list_ptr))
      {
         IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: No samples to copy from internal to output buffer.");
         return AR_EOK; /* Nothing to copy */
      }

      uint32_t out_num_bytes_per_ch = samples_to_copy_per_chan * bytes_per_sample;
      bytes_to_copy                 = out_num_bytes_per_ch * num_channels;

      src_ptr = me_ptr->int_buf_ptr +
                (me_ptr->int_buf_max_data_len - me_ptr->int_buf_actual_data_len); /* Data starts from end of buffer */
      dst_ptr = output[0]->buf_ptr->data_ptr + output[0]->buf_ptr->actual_data_len;

      uint32_t out_buf_len_per_ch_before = 0;
      if (CAPI_INTERLEAVED == input_media_fmt.format.data_interleaving)
      {
         out_buf_len_per_ch_before = topo_div_num(output[0]->buf_ptr->actual_data_len, num_channels);

         /* Perform bounds checking for memscpy */
         if (bytes_to_copy > me_ptr->int_buf_actual_data_len || /* Check source bounds */
             bytes_to_copy >
                (output[0]->buf_ptr->max_data_len - output[0]->buf_ptr->actual_data_len)) /* Check dest bounds */
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Interleaved copy to output buffer out of bounds. bytes_to_copy=%lu, "
                       "int_buf_actual_data_len=%lu, output_buf_max_data_len=%lu, output_buf_actual_data_len=%lu.",
                       bytes_to_copy,
                       me_ptr->int_buf_actual_data_len,
                       output[0]->buf_ptr->max_data_len,
                       output[0]->buf_ptr->actual_data_len);
            return AR_EFAILED;
         }
         memscpy(dst_ptr, bytes_to_copy, src_ptr, bytes_to_copy);
         output[0]->buf_ptr->actual_data_len += bytes_to_copy;
      }
      else /* Deinterleaved packed case (handled as deinterleaved unpacked for output buffer) */
      {
         out_buf_len_per_ch_before = output[0]->buf_ptr->actual_data_len;

         uint32_t current_src_offset = (me_ptr->int_buf_max_data_len - me_ptr->int_buf_actual_data_len) / num_channels;
         uint32_t current_dst_offset =
            output[0]->buf_ptr->actual_data_len; /* This is the base offset for channels in the output */

         for (uint32_t ch_idx = 0; ch_idx < num_channels; ch_idx++)
         {
            uint32_t src_channel_ptr_offset =
               current_src_offset + (ch_idx * (me_ptr->int_buf_max_data_len / num_channels));

            /* Validate output buffer for the current channel */
            if (ch_idx >= output[0]->bufs_num || NULL == output[0]->buf_ptr[ch_idx].data_ptr)
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Output buffer for channel %lu is invalid in ipc_rx_copy_int_buffer_to_output_buffer.",
                          ch_idx);
               return AR_EBADPARAM;
            }

            /* Check for buffer overflow before copying each channel */
            if ((src_channel_ptr_offset + out_num_bytes_per_ch > me_ptr->int_buf_max_data_len) ||
                (current_dst_offset + out_num_bytes_per_ch > output[0]->buf_ptr[0].max_data_len))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Buffer overflow detected during deinterleaved channel copy to output. ch_idx=%lu.",
                          ch_idx);
               return AR_EFAILED;
            }
            memscpy(output[0]->buf_ptr[ch_idx].data_ptr + current_dst_offset,
                    out_num_bytes_per_ch,
                    me_ptr->int_buf_ptr + src_channel_ptr_offset,
                    out_num_bytes_per_ch);
         }

         if (me_ptr->int_buf_md_list_ptr)
         {
            capi_stream_data_v2_t *output_strm_ptr = (capi_stream_data_v2_t *)output[0];

            uint32_t int_buf_len_per_ch_before = in_num_samples_per_chan * bytes_per_sample;
            uint32_t int_buf_len_per_ch_after  = (int_buf_len_per_ch_before - out_num_bytes_per_ch);

            uint32_t out_buf_len_per_ch_after = out_buf_len_per_ch_before + out_num_bytes_per_ch;

            capi_stream_data_v2_t src_stream;
            src_stream.metadata_list_ptr = me_ptr->int_buf_md_list_ptr;

            ar_result_t result          = ipc_rx_prop_md_from_src_buf_to_dst_buf(me_ptr,
                                                                        &me_ptr->input_media_fmt,
                                                                        int_buf_len_per_ch_before,
                                                                        int_buf_len_per_ch_after,
                                                                        &src_stream,
                                                                        out_buf_len_per_ch_before,
                                                                        out_buf_len_per_ch_after,
                                                                        output_strm_ptr);
            me_ptr->int_buf_md_list_ptr = src_stream.metadata_list_ptr;
            if (AR_FAILED(result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Failed to propagate MD 0x%lx from internal buf to capi output buf 0x%lx",
                          me_ptr->int_buf_md_list_ptr,
                          output_strm_ptr->metadata_list_ptr);
               return AR_EFAILED;
            }

            IPC_RX_MSG(me_ptr->miid,
                       DBG_LOW_PRIO,
                       "Propagated MD from internal buf md list 0x%lx to capi output buf md list 0x%lx",
                       me_ptr->int_buf_md_list_ptr,
                       output_strm_ptr->metadata_list_ptr);
         }

         /* For deinterleaved v2, only the first buffer's actual_data_len typically reflects total samples */
         output[0]->buf_ptr[0].actual_data_len += out_num_bytes_per_ch;
      }
      me_ptr->int_buf_actual_data_len -= bytes_to_copy;
   }
   else /* Non-PCM data formats: direct copy */
   {
      bytes_to_copy = MIN(me_ptr->int_buf_actual_data_len,
                          (output[0]->buf_ptr[0].max_data_len - output[0]->buf_ptr[0].actual_data_len));

      if (0 == bytes_to_copy)
      {
         IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: No bytes to copy from internal to output buffer (non-PCM).");
         return AR_EOK; /* Nothing to copy */
      }

      src_ptr = me_ptr->int_buf_ptr + (me_ptr->int_buf_max_data_len - me_ptr->int_buf_actual_data_len);
      dst_ptr = output[0]->buf_ptr[0].data_ptr + output[0]->buf_ptr[0].actual_data_len;

      /* Perform bounds checking for memscpy */
      if (bytes_to_copy > me_ptr->int_buf_actual_data_len || /* Check source bounds */
          bytes_to_copy >
             (output[0]->buf_ptr[0].max_data_len - output[0]->buf_ptr[0].actual_data_len)) /* Check dest bounds */
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Non-PCM copy to output buffer out of bounds. bytes_to_copy=%lu, "
                    "int_buf_actual_data_len=%lu, output_buf_max_data_len=%lu, output_buf_actual_data_len=%lu.",
                    bytes_to_copy,
                    me_ptr->int_buf_actual_data_len,
                    output[0]->buf_ptr[0].max_data_len,
                    output[0]->buf_ptr[0].actual_data_len);
         return AR_EFAILED;
      }
      memscpy(dst_ptr, bytes_to_copy, src_ptr, bytes_to_copy);

      if (me_ptr->int_buf_md_list_ptr)
      {
         capi_stream_data_v2_t *output_strm_ptr = (capi_stream_data_v2_t *)output[0];

         uint32_t int_buf_len_before = me_ptr->int_buf_actual_data_len;
         uint32_t int_buf_len_after  = (int_buf_len_before - bytes_to_copy);

         uint32_t out_buf_len_before = output[0]->buf_ptr[0].actual_data_len;
         uint32_t out_buf_len_after  = out_buf_len_before + bytes_to_copy;

         capi_stream_data_v2_t src_stream;
         src_stream.metadata_list_ptr = me_ptr->int_buf_md_list_ptr;

         ar_result_t result          = ipc_rx_prop_md_from_src_buf_to_dst_buf(me_ptr,
                                                                     &me_ptr->input_media_fmt,
                                                                     int_buf_len_before,
                                                                     int_buf_len_after,
                                                                     &src_stream,
                                                                     out_buf_len_before,
                                                                     out_buf_len_after,
                                                                     output_strm_ptr);
         me_ptr->int_buf_md_list_ptr = src_stream.metadata_list_ptr;

         if (AR_FAILED(result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Failed to propagate MD 0x%lx from internal buf to capi output buf 0x%lx",
                       me_ptr->int_buf_md_list_ptr,
                       output_strm_ptr->metadata_list_ptr);
            return AR_EFAILED;
         }

         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Propagated MD from internal buf md list 0x%lx to capi output buf md list 0x%lx",
                    me_ptr->int_buf_md_list_ptr,
                    output_strm_ptr->metadata_list_ptr);
      }

      me_ptr->int_buf_actual_data_len -= bytes_to_copy;
      output[0]->buf_ptr[0].actual_data_len += bytes_to_copy;
   }

   IPC_RX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "INFO: Copied data from internal buffer to output buffer. Internal buffer remaining: %lu bytes, copied: "
              "%lu out actual len %lu bytes.",
              me_ptr->int_buf_actual_data_len,
              bytes_to_copy,
              output[0]->buf_ptr[0].actual_data_len);
   return AR_EOK;
}

/**
 * \brief Reads data for the IPC_RX module, managing internal and IPC buffers.
 *
 * This function is responsible for moving data from the IPC buffer (received
 * from GPR) to the module's internal buffer, and then to the CAPI output
 * buffer. It handles scenarios where the internal buffer has pending data,
 * or where the IPC buffer can be directly copied to the output buffer.
 * It also manages pending media format updates. The function prioritizes
 * draining existing internal buffer data before processing new IPC data.
 *
 * \param[in,out] me_ptr  Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[out] output Array of pointers to output stream data. Must not be NULL.
 *
 * \return CAPI_EOK on success.
 *         CAPI_EBADPARAM if `me_ptr` or `output` pointers are NULL.
 *         Other `CAPI_FAILED` codes if any data copy or command handling operations fail.
 */
capi_err_t capi_ipc_rx_read_data(capi_ipc_rx_t *me_ptr, capi_stream_data_t *output[])
{
   capi_err_t capi_result = CAPI_EOK;

   // Validate input pointers
   if (NULL == me_ptr || NULL == output || NULL == output[0])
   {
      IPC_RX_MSG(me_ptr ? me_ptr->miid : MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: NULL pointer detected in capi_ipc_rx_read_data.");
      return CAPI_EBADPARAM;
   }

   if (me_ptr->is_mod_buf_access_enabled && output[0]->buf_ptr && (NULL == output[0]->buf_ptr->data_ptr))
   {
      capi_result = capi_ipc_rx_process_share_capi_buffer_to_fwk(me_ptr, (capi_stream_data_v2_t *)output[0]);
   }
   else
   {
      capi_result = capi_ipc_rx_process_copy_data_to_fwk_buffer(me_ptr, output);
   }

   /* Handle pending media format updates */
   if (TRUE == me_ptr->is_pending_mf)
   {
      /* If both IPC and internal buffers are empty, it's safe to apply the pending media format */
      if ((0 == me_ptr->ipc_buf_actual_data_len) && (0 == me_ptr->int_buf_actual_data_len))
      {
         me_ptr->is_pending_mf = FALSE; /* Clear pending MF flag */

         // set the trigger as buffer not needed to ensure fwk calls the process and module can raise the mf then
         me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED; /* Set trigger for MF processing */
      }
   }
   else /* No pending media format, check if mf_packet_ptr needs processing */
   {
      if (NULL != me_ptr->mf_packet_ptr)
      {
         capi_result = ipc_rx_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client(me_ptr,
                                                                                me_ptr->mf_packet_ptr,
                                                                                FALSE /* data_cmd */);
         if (CAPI_FAILED(capi_result))
         {
            IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Failed to handle pending media format: %lu.", capi_result);
            /* Don't return here, attempt to end the command anyway to prevent leaks. */
         }

         ar_result_t ack_res = __gpr_cmd_end_command(me_ptr->mf_packet_ptr, AR_EOK);
         if (AR_DID_FAIL(ack_res))
         {
            IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Failed to acknowledge pending MF packet: %lu.", ack_res);
            capi_result |= (capi_err_t)ack_res; /* Propagate if primary op failed */
         }
         me_ptr->mf_packet_ptr = NULL; /* Clear the pointer after handling */
      }
      else
      {
         if ((me_ptr->int_buf_actual_data_len + me_ptr->ipc_buf_actual_data_len) >= me_ptr->out_buf_size)
         {
            me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED;
         }
         else if (ipc_rx_check_if_there_is_a_flushing_eos(me_ptr, me_ptr->md_buf_info.md_list_ptr))
         {
            me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED_OPTIONALLY;
         }
         else if (IPC_RX_DFS_AT_GAP == me_ptr->dfs)
         {
            me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED_OPTIONALLY;
         }
         else
         {
            me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED;
         }
      }
   }

#ifdef DEBUG_IPC_RX
   IPC_RX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "INFO: Read_data complete: output[0]->buf_ptr->actual_data_len: %lu. trigger %lu",
              output[0]->buf_ptr->actual_data_len,
              me_ptr->input_trigger_info);
#endif

   return capi_result; /* Return aggregated result */
}

static capi_err_t capi_ipc_rx_process_copy_data_to_fwk_buffer(capi_ipc_rx_t *me_ptr, capi_stream_data_t *output[])
{
   capi_err_t capi_result = CAPI_EOK;
   /* Prioritize draining data from the internal buffer to the output buffer */
   if (me_ptr->int_buf_actual_data_len > 0)
   {
      /* input trigger is properly updated such that, during this time,
       * int buffer has full data to copy whole output buffer.
       */
      capi_result = (capi_err_t)ipc_rx_copy_int_buffer_to_output_buffer(me_ptr, output);
      if (CAPI_FAILED(capi_result))
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Failed to copy from internal buffer to output: %lu.",
                    capi_result);
         return capi_result;
      }

      /* If IPC buffer also has data, copy it to the internal buffer to prevent fragmentation */
      if (me_ptr->ipc_buf_actual_data_len > 0)
      {
         capi_result = (capi_err_t)ipc_rx_copy_ipc_buffer_to_int_buffer(me_ptr);
         if (CAPI_FAILED(capi_result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Failed to copy from IPC buffer to internal: %lu.",
                       capi_result);
            return capi_result;
         }

         /* If IPC buffer is completely consumed after copying to internal, acknowledge and update trigger */
         if (0 == me_ptr->ipc_buf_actual_data_len)
         {
            capi_result =
               (capi_err_t)ipc_rx_free_input_data_cmd(me_ptr, me_ptr->ipc_buf_packet_ptr, AR_EOK, FALSE, TRUE);
            if (CAPI_FAILED(capi_result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to free IPC input data command: %lu.",
                          capi_result);
               return capi_result;
            }
            me_ptr->ipc_buf_packet_ptr = NULL;
         }
      }
   }
   else /* Internal buffer has no data, attempt to copy directly from IPC buffer to output */
   {
      if (me_ptr->ipc_buf_actual_data_len > 0)
      {
         capi_result = (capi_err_t)ipc_rx_copy_ipc_buffer_to_output_buffer(me_ptr, output);
         if (CAPI_FAILED(capi_result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Failed to copy from IPC buffer to output: %lu.",
                       capi_result);
            return capi_result;
         }

         /* If IPC buffer is completely consumed, acknowledge and update trigger */
         if (0 == me_ptr->ipc_buf_actual_data_len)
         {
            capi_result =
               (capi_err_t)ipc_rx_free_input_data_cmd(me_ptr, me_ptr->ipc_buf_packet_ptr, AR_EOK, FALSE, TRUE);
            if (CAPI_FAILED(capi_result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to free IPC input data command (direct copy): %lu.",
                          capi_result);
               return capi_result;
            }
            me_ptr->ipc_buf_packet_ptr = NULL;
         }
         /* If IPC buffer is not completely consumed but remaining data can fit into internal buffer,
          * copy to internal buffer to prevent fragmentation and await next process call. */
         else if (me_ptr->ipc_buf_actual_data_len <= (me_ptr->int_buf_max_data_len - me_ptr->int_buf_actual_data_len))
         {
            capi_result = (capi_err_t)ipc_rx_copy_ipc_buffer_to_int_buffer(me_ptr);
            if (CAPI_FAILED(capi_result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to copy remaining IPC data to internal buffer: %lu.",
                          capi_result);
               return capi_result;
            }

            /* If IPC buffer is now completely consumed, acknowledge and update trigger */
            if (0 == me_ptr->ipc_buf_actual_data_len)
            {
               capi_result =
                  (capi_err_t)ipc_rx_free_input_data_cmd(me_ptr, me_ptr->ipc_buf_packet_ptr, AR_EOK, FALSE, TRUE);
               if (CAPI_FAILED(capi_result))
               {
                  IPC_RX_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "Error: Failed to free IPC input data command (remaining copy): %lu.",
                             capi_result);
                  return capi_result;
               }
               me_ptr->ipc_buf_packet_ptr = NULL;
            }
         }
         /* If IPC buffer actual data length is greater than internal buffer capacity,
          * retain BUFFER_NOT_NEEDED to continue consuming it in the next process call. */
         else
         {
            /* No change to trigger info, process will be called again to drain more data. */
         }
      }
   }
   return capi_result;
}

static capi_err_t capi_ipc_rx_process_share_capi_buffer_to_fwk(capi_ipc_rx_t         *me_ptr,
                                                               capi_stream_data_v2_t *output_strm_ptr)
{
   capi_err_t  capi_result   = CAPI_EOK;
   capi_buf_t *bufs_ptr      = output_strm_ptr->buf_ptr;
   uint32_t    num_bufs      = ipc_rx_get_num_bufs_from_mf(me_ptr);

   // if internal buffer is not full, first move data into int buf and then share the buffer,
   // or if ipc buffer length is not sufficient to share with the fwk, then move data to int buf and then share.
   // Underrun scenario: if IPC buf len < frame len, then copy the partial frame to internal buffer and then process.
   if ((me_ptr->int_buf_actual_data_len > 0 && (me_ptr->int_buf_actual_data_len != me_ptr->int_buf_max_data_len)) ||
       (me_ptr->ipc_buf_actual_data_len > 0 && (me_ptr->ipc_buf_actual_data_len < me_ptr->int_buf_max_data_len)))
   {
      // first move data from IPC buffer to internal buffer
      // and then share the internal buffer with the fwk

      // print message that int buf is being shared with fwk
      IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "mod_buf_extn: Copy data from ipc to internal buffer and share int buf");

      /* If IPC buffer also has data, copy it to the internal buffer to prevent fragmentation */
      if (me_ptr->ipc_buf_actual_data_len > 0)
      {
         capi_result = (capi_err_t)ipc_rx_copy_ipc_buffer_to_int_buffer(me_ptr);
         if (CAPI_FAILED(capi_result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Failed to copy from IPC buffer to internal: %lu.",
                       capi_result);
            return capi_result;
         }

         /* If IPC buffer is completely consumed after copying to internal, acknowledge and update trigger */
         if (0 == me_ptr->ipc_buf_actual_data_len)
         {
            capi_result =
               (capi_err_t)ipc_rx_free_input_data_cmd(me_ptr, me_ptr->ipc_buf_packet_ptr, AR_EOK, FALSE, TRUE);
            if (CAPI_FAILED(capi_result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to free IPC input data command: %lu.",
                          capi_result);
               return capi_result;
            }
            me_ptr->ipc_buf_packet_ptr = NULL;
         }
      }
   }

   // if internal buffer is full share the internal buffer,
   // note that internal buffer cannot be partially filled at this point.
   if (me_ptr->int_buf_actual_data_len > 0)
   {
      uint32_t int_max_len_per_buf = me_ptr->int_buf_max_data_len_per_buf;
      uint32_t int_actual_len_per_buf;
      if ((me_ptr->int_buf_actual_data_len != me_ptr->int_buf_max_data_len))
      {
         // print message that int buf is being shared with fwk
         IPC_RX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "mod_buf_extn: Unexpected! internal buffer not filled %lu < %lu, may be due to "
                    "eos/eof/disc/ or need_to_underrun ? %lu",
                    me_ptr->int_buf_actual_data_len,
                    me_ptr->int_buf_max_data_len,
                    me_ptr->need_to_underrun);

         int_actual_len_per_buf = topo_div_num(me_ptr->int_buf_actual_data_len, num_bufs);
      }
      else // if equal avoid division
      {
         int_actual_len_per_buf = int_max_len_per_buf;
      }

#ifdef DEBUG_IPC_RX
      // print message that int buf is being shared with fwk
      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "mod_buf_extn: Sharing the internal buffer %p int_actual_len_per_buf %lu int_max_len_per_buf %lu "
                 "num_bufs %lu"
                 "with the fwk",
                 me_ptr->int_buf_ptr,
                 int_actual_len_per_buf,
                 int_max_len_per_buf,
                 num_bufs);
#endif

      if (bufs_ptr[0].max_data_len != int_actual_len_per_buf)
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Warning! max data len %lu != int buf len %lu",
                    bufs_ptr[0].max_data_len,
                    int_actual_len_per_buf);
      }

      uint32_t bytes_to_copy_per_buf = MIN(int_actual_len_per_buf, bufs_ptr[0].max_data_len);

      int8_t *src_ptr = (int8_t *)me_ptr->int_buf_ptr;
      for (uint32_t i = 0; i < num_bufs; i++)
      {
         bufs_ptr[i].data_ptr = src_ptr + (int_max_len_per_buf - int_actual_len_per_buf);
         src_ptr += int_max_len_per_buf;

         me_ptr->int_buf_actual_data_len -= bytes_to_copy_per_buf;

#ifdef DEBUG_IPC_RX
         // print all the variable update above
         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "mod_buf_extn: buf[%d] data_ptr=0x%p, actual_data_len=%lu, max_data_len=%lu",
                    i,
                    bufs_ptr[i].data_ptr,
                    bufs_ptr[i].actual_data_len,
                    bufs_ptr[i].max_data_len);
#endif
      }

      bufs_ptr[0].actual_data_len = bytes_to_copy_per_buf;

#ifdef DEBUG_IPC_RX
      // print all the variable update above
      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "mod_buf_extn: internal buffer remaining data %lu",
                 me_ptr->int_buf_actual_data_len);
#endif

      // move md from int buffer to output buffer
      if (me_ptr->int_buf_md_list_ptr)
      {
         uint32_t int_buf_len_per_ch_before = int_actual_len_per_buf;
         uint32_t int_buf_len_per_ch_after  = (int_buf_len_per_ch_before - bytes_to_copy_per_buf);

         // note that out buf len is assumed to be zero always in the context of extension
         uint32_t out_buf_len_per_ch_before = 0;
         uint32_t out_buf_len_per_ch_after  = bytes_to_copy_per_buf;

         capi_stream_data_v2_t src_stream;
         src_stream.metadata_list_ptr = me_ptr->int_buf_md_list_ptr;

         ar_result_t result          = ipc_rx_prop_md_from_src_buf_to_dst_buf(me_ptr,
                                                                     &me_ptr->input_media_fmt,
                                                                     int_buf_len_per_ch_before,
                                                                     int_buf_len_per_ch_after,
                                                                     &src_stream,
                                                                     out_buf_len_per_ch_before,
                                                                     out_buf_len_per_ch_after,
                                                                     output_strm_ptr);

         me_ptr->int_buf_md_list_ptr = src_stream.metadata_list_ptr;
         if (AR_FAILED(result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Failed to propagate MD 0x%lx from internal buf to capi output buf 0x%lx",
                       me_ptr->int_buf_md_list_ptr,
                       output_strm_ptr->metadata_list_ptr);
            return AR_EFAILED;
         }

         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Propagated MD from internal buf md list 0x%lx to capi output buf md list 0x%lx",
                    me_ptr->int_buf_md_list_ptr,
                    output_strm_ptr->metadata_list_ptr);
      }
   }
   else if (me_ptr->ipc_buf_actual_data_len > 0)
   {
      // share the IPC buffer directly with the fwk
      // print message that int buf is being shared with fwk
#ifdef DEBUG_IPC_RX
      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "mod_buf_extn: Sharing the IPC buffer 0x%p actual len %lu with the fwk",
                 me_ptr->ipc_buf_virtual_addr,
                 me_ptr->ipc_buf_actual_data_len);
#endif

      // todo_mdf: optimize divisions if actual == max len then use max len per buf instead of doing division.
      // do this everywhere in rx/tx process context
      uint32_t ipc_buf_actual_len_per_buf = topo_div_num(me_ptr->ipc_buf_actual_data_len, num_bufs);
      uint32_t ipc_buf_max_len_per_buf    = me_ptr->ipc_buf_size_per_buf;

      if (bufs_ptr[0].max_data_len > ipc_buf_actual_len_per_buf)
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Unexpected! max data len %lu > int buf len %lu, may be due to eos/eof/disc",
                    bufs_ptr[0].max_data_len,
                    ipc_buf_actual_len_per_buf);
      }

      uint32_t bytes_to_copy_per_buf = MIN(ipc_buf_actual_len_per_buf, bufs_ptr[0].max_data_len);

      int8_t *src_ptr = (int8_t *)me_ptr->ipc_buf_virtual_addr;
      for (uint32_t i = 0; i < num_bufs; i++)
      {
         bufs_ptr[i].data_ptr = src_ptr + (ipc_buf_max_len_per_buf - ipc_buf_actual_len_per_buf);
         src_ptr += ipc_buf_max_len_per_buf;

         me_ptr->ipc_buf_actual_data_len -= bytes_to_copy_per_buf;

#ifdef DEBUG_IPC_RX
         // print all the variable update above
         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "mod_buf_extn: buf[%d] data_ptr=0x%p, actual_data_len=%lu, max_data_len=%lu",
                    i,
                    bufs_ptr[i].data_ptr,
                    bytes_to_copy_per_buf,
                    ipc_buf_max_len_per_buf);
#endif
      }
      // since RX is processing unpacked v2, only first buffers actual len needs to be udpated
      bufs_ptr[0].actual_data_len = bytes_to_copy_per_buf;

#ifdef DEBUG_IPC_RX
      // print all the variable update above
      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "mod_buf_extn: IPC buffer remaining data %lu",
                 me_ptr->ipc_buf_actual_data_len);
#endif
      // move md from int buffer to output buffer
      // for both interleaved and deinterleaved MD offset is relative to the per ch offset, hence
      // MD propagation parameters are same.
      if (me_ptr->md_buf_info.md_list_ptr)
      {
         uint32_t ipc_buf_len_per_ch_before = ipc_buf_actual_len_per_buf;
         uint32_t ipc_buf_len_per_ch_after  = (ipc_buf_len_per_ch_before - bytes_to_copy_per_buf);

         // output buf is not expected to have any data before calling the process under the buffer access extension.
         uint32_t out_buf_len_per_ch_before = 0;
         uint32_t out_buf_len_per_ch_after  = bytes_to_copy_per_buf;

         capi_stream_data_v2_t src_stream;
         src_stream.metadata_list_ptr = me_ptr->md_buf_info.md_list_ptr;

         ar_result_t result = ipc_rx_prop_md_from_src_buf_to_dst_buf(me_ptr,
                                                                     &me_ptr->input_media_fmt,
                                                                     ipc_buf_len_per_ch_before,
                                                                     ipc_buf_len_per_ch_after,
                                                                     &src_stream,
                                                                     out_buf_len_per_ch_before,
                                                                     out_buf_len_per_ch_after,
                                                                     output_strm_ptr);

         me_ptr->md_buf_info.md_list_ptr = src_stream.metadata_list_ptr;

         if (AR_FAILED(result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Failed to propagate MD 0x%lx from IPC buf to capi output buf 0x%lx",
                       me_ptr->md_buf_info.md_list_ptr,
                       output_strm_ptr->metadata_list_ptr);
            return AR_EFAILED;
         }

         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Propagated MD from IPC buf md list 0x%lx to capi output buf md list 0x%lx",
                    me_ptr->md_buf_info.md_list_ptr,
                    output_strm_ptr->metadata_list_ptr);
      }

      /** Important: do not free IPC buffer here since it shared with fwk. Return when the fwk
         returns the buffer in capi_ipc_rx_intf_extn_return_mod_output_buf() */
   }
   else // both ipc and int buffers do not data
   {
      // print message that int buf is being shared with fwk
      IPC_RX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "mod_buf_extn: Unexpected! int buffer and ipc buffer are empty, may be due to eos/eof/disc");

      uint32_t max_len_per_buf = me_ptr->int_buf_max_data_len_per_buf;

      if (bufs_ptr[0].max_data_len != max_len_per_buf)
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Unexpected! output max data len %lu > int buf max len per buf %lu",
                    bufs_ptr[0].max_data_len,
                    max_len_per_buf);
         return CAPI_EFAILED;
      }

      int8_t *src_ptr = (int8_t *)me_ptr->int_buf_ptr;
      for (uint32_t i = 0; i < num_bufs; i++)
      {
         bufs_ptr[i].data_ptr        = src_ptr;
         bufs_ptr[i].actual_data_len = 0;
         src_ptr += max_len_per_buf;

#ifdef DEBUG_IPC_RX
         // print all the variable update above
         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "mod_buf_extn: buf[%d] data_ptr=0x%p, actual_data_len=%lu, max_data_len=%lu",
                    i,
                    bufs_ptr[i].data_ptr,
                    bufs_ptr[i].actual_data_len,
                    bufs_ptr[i].max_data_len);
#endif
      }

      // move md from IPC buffer to output buffer if there is any
      if (me_ptr->md_buf_info.md_list_ptr || me_ptr->int_buf_md_list_ptr)
      {
         // merge MD in both internal and IPC buffer context and propagate it to output buffer
         spf_list_merge_lists((spf_list_node_t **)&me_ptr->int_buf_md_list_ptr,
                              (spf_list_node_t **)&me_ptr->md_buf_info.md_list_ptr);

         uint32_t ipc_buf_len_per_ch_before = 0;
         uint32_t ipc_buf_len_per_ch_after  = 0;
         uint32_t out_buf_len_per_ch_before = 0;
         uint32_t out_buf_len_per_ch_after  = 0;

         capi_stream_data_v2_t src_stream;
         src_stream.metadata_list_ptr = me_ptr->int_buf_md_list_ptr;

         ar_result_t result          = ipc_rx_prop_md_from_src_buf_to_dst_buf(me_ptr,
                                                                     &me_ptr->input_media_fmt,
                                                                     ipc_buf_len_per_ch_before,
                                                                     ipc_buf_len_per_ch_after,
                                                                     &src_stream,
                                                                     out_buf_len_per_ch_before,
                                                                     out_buf_len_per_ch_after,
                                                                     output_strm_ptr);

         me_ptr->int_buf_md_list_ptr = src_stream.metadata_list_ptr;
         if (AR_FAILED(result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Failed to propagate MD 0x%lx from internal buf to capi output buf md list 0x%lx",
                       me_ptr->int_buf_md_list_ptr,
                       output_strm_ptr->metadata_list_ptr);
            return AR_EFAILED;
         }

         if (me_ptr->int_buf_md_list_ptr)
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Pending MD 0x%lx found in internal buf, unable to propagate to capi output buf md list 0x%lx, "
                       "dropping it",
                       me_ptr->int_buf_md_list_ptr,
                       output_strm_ptr->metadata_list_ptr);
            ipc_rx_destroy_all_md(me_ptr, &me_ptr->int_buf_md_list_ptr);
         }

         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Propagated MD from internal buf md list 0x%lx to capi output buf md list 0x%lx",
                    me_ptr->int_buf_md_list_ptr,
                    output_strm_ptr->metadata_list_ptr);
      }

      /* Recevived IPC buffer only with MD, freeing it since it not shared it with the fwk. */
      if (0 == me_ptr->ipc_buf_actual_data_len && me_ptr->ipc_buf_packet_ptr)
      {
         capi_result = (capi_err_t)ipc_rx_free_input_data_cmd(me_ptr, me_ptr->ipc_buf_packet_ptr, AR_EOK, FALSE, TRUE);
         if (CAPI_FAILED(capi_result))
         {
            IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Failed to free IPC input data command: %lu.", capi_result);
            return capi_result;
         }
         me_ptr->ipc_buf_packet_ptr = NULL;
      }
   }

   me_ptr->curr_shared_buf_ptr = bufs_ptr[0].data_ptr;

   return CAPI_EOK;
}