/**
 *  \file posal_data_log.c
 * \brief
 *  	This file contains utilities for data logging for on-target
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "posal_data_log_i.h"
#include "ar_error_codes.h"
#include "posal.h"
#include "capi_types.h"
#include "dls_log_pkt_hdr_api.h"
#if defined(DLS_DATA_LOGGING)
#include "dls.h"
#else
#include "log.h"
#endif //defined DLS_DATA_LOGGING
/*==========================================================================
  Structure definitions
  ========================================================================== */
typedef union log_pkt_header_internal_t
{
   dls_log_pkt_pcm_data_t       pcm;
   dls_log_pkt_bitstream_data_t bitstream;
} log_pkt_header_internal_t;

// Enable DLS_DATA_LOGGING on platforms where DIAG is not supported and Data Logging Service (DLS) is used.
#if defined(DLS_DATA_LOGGING)
#define log_alloc dls_acquire_buffer
#define log_commit dls_commit_buffer
#define log_free dls_log_buf_free
#define log_status dls_log_code_status
#endif //defined DLS_DATA_LOGGING

static void *      posal_data_alloc_log_buffer_internal(uint32_t                buf_size,
                                                        uint32_t                log_code,
                                                        posal_data_log_format_t data_fmt,
                                                        bool_t                  is_data_logging,
                                                        uint32_t                num_channels);
static ar_result_t posal_data_log_buffer(void *log_pkt_ptr, posal_data_log_info_t *log_info_ptr);

/* ----------------------------------------------------------------------------
 * Function Definitions
 * ------------------------------------------------------------------------- */
/**
  This is an utility function to find the status of a valid log code

  @param[in]     log_code  log code

  @param[out]    None

  @return        bool      TRUE/FALSE

  @dependencies  None
 */
bool_t posal_data_log_code_status(uint32_t log_code)
{
   return log_status(log_code);
}

dls_audiolog_data_format_t posal_data_log_get_audiolog_data_format_type(data_format_t capi_data_format)
{
   dls_audiolog_data_format_t audiolog_data_format = AUDIOLOG_FIXED_POINT_DATA_FORMAT;
#ifdef DATA_LOGGING_DBG
   AR_MSG(DBG_HIGH_PRIO, "capi_data_format: %lu", capi_data_format);
#endif
   switch (capi_data_format)
   {
      case CAPI_FIXED_POINT:
      {
         audiolog_data_format = AUDIOLOG_FIXED_POINT_DATA_FORMAT;
         break;
      }
      case CAPI_FLOATING_POINT:
      {
         audiolog_data_format = AUDIOLOG_FLOATING_POINT_DATA_FORMAT;
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "Unsupported capi_data_format: %lu - returning default (0)", capi_data_format);
         break;
      }
   }

   return audiolog_data_format;
}

/**
  This is an utility function used to find the maximum available buffer size

  @param[in]     None

  @param[out]    max_data_buf_size  Maximum buffer size

  @return        status             error code

  @dependencies  None
 */
uint32_t posal_data_log_get_max_buf_size(void)
{
   /* -1, as the payload is declared as 1 byte in the pkt struct*/
   uint32_t max_data_buf_size = (MAX_LOG_PKT_SIZE - (sizeof(log_pkt_header_internal_t) - 1));
   return max_data_buf_size;
}

/**
  Allocate the log packet for logging PCM data

  @param[in]  buf_Size      Size of data to be logged in BYTES
  @param[in]  log_code      posal log code
  @param[in]  log_code      data type, PCM or bitstream
  @return
  Void pointer to the log packet, if log allocation is
  successful

  @dependencies
  None.
 */
void *posal_data_log_alloc(uint32_t buf_size, uint32_t log_code, posal_data_log_format_t data_fmt)
{
   return posal_data_alloc_log_buffer_internal(buf_size, log_code, data_fmt, FALSE, 0);
}

// new param - num_channels: number of channels of PCM data format type
void *posal_data_log_alloc_v2(uint32_t                buf_size,
                              uint32_t                log_code,
                              posal_data_log_format_t data_fmt,
                              uint32_t                num_channels)
{
   return posal_data_alloc_log_buffer_internal(buf_size, log_code, data_fmt, FALSE, num_channels);
}

/**
  Configure the audio log header with stream parameters and
  commit the packet for logging.

  param[in]  log_info_ptr  : Pointer to struct containing common
  information for populating the log header for both PCM and BS
  data

  @result
  graphite_result_t

  @dependencies
  None.
 */
ar_result_t posal_data_log_alloc_commit(posal_data_log_info_t *log_info_ptr)
{
   ar_result_t             result = AR_EOK;
   void *                  log_pkt_ptr;
   uint32_t                rem_log_buf_size, log_buf_size;
   int8_t *                curr_buf_ptr;
   dls_header_cmn_t *      log_header_cmn_ptr;
   dls_log_pkt_pcm_data_t *pcm_data_pkt_ptr;
   uint32_t                total_log_buf_size;
   bool_t                  is_first_seg                    = TRUE;
   uint32_t                num_channels                    = 0;
   uint32_t                max_log_packet_size_based_on_mf = 0;
   uint8_t                 interleaved;

   if (NULL == log_info_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Log info ptr is NULL");
      return AR_EFAILED;
   }

   if (LOG_DATA_FMT_PCM == log_info_ptr->data_fmt)
   {
      uint32_t bits_per_sample        = 0;
      uint32_t unit_size              = 0;
      num_channels                    = log_info_ptr->data_info.pcm_data_fmt.num_channels;
      bits_per_sample                 = log_info_ptr->data_info.pcm_data_fmt.bits_per_sample;
      unit_size                       = ((bits_per_sample >> 3) * num_channels);
      max_log_packet_size_based_on_mf = posal_cmn_divide((posal_data_log_get_max_buf_size()), unit_size);
      max_log_packet_size_based_on_mf = max_log_packet_size_based_on_mf * unit_size;
   }
   else
   {
      max_log_packet_size_based_on_mf = posal_data_log_get_max_buf_size();
   }

   curr_buf_ptr       = log_info_ptr->buf_ptr;
   rem_log_buf_size   = log_info_ptr->buf_size;
   total_log_buf_size = log_info_ptr->buf_size;

   /*  If the buffer size to log is > max_log_packet_size_based_on_mf, logging per iteration
         is restricted to this size.
    */
   while (rem_log_buf_size > 0)
   {
      if (rem_log_buf_size < max_log_packet_size_based_on_mf)
      {
         log_buf_size = rem_log_buf_size;
      }
      else
      {
         log_buf_size = max_log_packet_size_based_on_mf;
      }
      // For data logging
      log_pkt_ptr = posal_data_alloc_log_buffer_internal(log_buf_size,
                                                         log_info_ptr->log_code,
                                                         log_info_ptr->data_fmt,
                                                         TRUE,
                                                         num_channels);

      if (NULL == log_pkt_ptr)
      {
#ifdef DATA_LOGGING_DBG
         AR_MSG(DBG_HIGH_PRIO,
                "Log buffer allocation failed for log code 0x%X, bufsize : %u "
                "bytes",
                log_info_ptr->log_code,
                log_buf_size);
#endif
         /* Skip these sequence count to detect packet drops */
         uint32_t skipped_log_seq_count =
            posal_cmn_divide((rem_log_buf_size + max_log_packet_size_based_on_mf - 1), max_log_packet_size_based_on_mf);
         *(log_info_ptr->seq_number_ptr) += skipped_log_seq_count;
         return AR_EFAILED;
      }

#ifdef DATA_LOGGING_DBG
      AR_MSG(DBG_HIGH_PRIO,
             "Log buffer alloc SUCCESS for log code 0x%X, bufsize : %u bytes",
             log_info_ptr->log_code,
             log_buf_size);
#endif

      log_info_ptr->buf_ptr  = curr_buf_ptr;
      log_info_ptr->buf_size = log_buf_size;

      pcm_data_pkt_ptr   = (dls_log_pkt_pcm_data_t *)log_pkt_ptr;
      log_header_cmn_ptr = &(pcm_data_pkt_ptr->log_header_cmn);

      /* For PCM format, with deinterleaved data if log buffer size is more than
              max value, it is segmented and each segment is logged individually.
       */
      interleaved = log_info_ptr->data_info.pcm_data_fmt.interleaved;

      if ((max_log_packet_size_based_on_mf == log_buf_size) && (total_log_buf_size > max_log_packet_size_based_on_mf) &&
          (TRUE == is_first_seg) && (LOG_DATA_FMT_PCM == log_info_ptr->data_fmt) && (!interleaved))
      {
         is_first_seg = FALSE;
      }

      if (FALSE == is_first_seg)
      {
         num_channels                            = log_info_ptr->data_info.pcm_data_fmt.num_channels;
         log_header_cmn_ptr->log_seg_number      = SEG_PKT;
         log_header_cmn_ptr->log_fragment_offset = posal_cmn_divide(total_log_buf_size, (num_channels));
      }

      result = posal_data_log_buffer(log_pkt_ptr, log_info_ptr);

      if (AR_EOK != result)
      {
         AR_MSG(DBG_HIGH_PRIO, "Error occured while populating log packet for log code 0x%X", log_info_ptr->log_code);
         /* Freeing up the allocated log buffer */
         log_free(log_pkt_ptr);
         return AR_EFAILED;
      }

      /* Increment the read/write pointers to buffer to be logged */

      rem_log_buf_size -= log_buf_size;

      if (FALSE == is_first_seg)
      {
         curr_buf_ptr += posal_cmn_divide(log_buf_size, num_channels);
      }
      else
      {
         curr_buf_ptr += log_buf_size;
      }

   } /* while loop */

   return result;
}

/**
  Commit the log packet for logging PCM data

  @param[in]  buf_Size      Size of data to be logged in BYTES
  @param[in]  log_info_ptr  Logging info
  @return
   result

  @dependencies
  None.
 */
ar_result_t posal_data_log_commit(void *log_pkt_payload_ptr, posal_data_log_info_t *log_info_ptr)
{
   if (NULL == log_pkt_payload_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error! NULL pointer passed to posal_data_log_commit");
      return AR_EBADPARAM;
   }

#ifdef DATA_LOGGING_DBG
   AR_MSG(DBG_HIGH_PRIO, "posal_data_log_commit: log_pkt_payload_ptr: 0x%x", log_pkt_payload_ptr);
#endif

   switch (log_info_ptr->data_fmt)
   {
      case LOG_DATA_FMT_RAW:
      case LOG_DATA_FMT_BITSTREAM:
      {
         // Get pointer of packet, the function takes in pointer to payload
         dls_log_pkt_bitstream_data_t *log_pkt_ptr =
            (dls_log_pkt_bitstream_data_t *)((uint8_t *)log_pkt_payload_ptr - sizeof(dls_log_pkt_bitstream_data_t) +
                                             sizeof(log_pkt_ptr->payload));

         uint64_t log_time_stamp  = (log_info_ptr->log_time_stamp) ? log_info_ptr->log_time_stamp : posal_timer_get_time();

         log_pkt_ptr->version = POSAL_PINE_VERSION_LATEST;
         log_pkt_ptr->data_format_tag = AUDIOLOG_BS_DATA_FORMAT;

         log_pkt_ptr->log_header_cmn.log_session_id = log_info_ptr->session_id;
         log_pkt_ptr->log_header_cmn.log_tap_id     = log_info_ptr->log_tap_id;
         log_pkt_ptr->log_header_cmn.log_seg_number = 0;
         log_pkt_ptr->log_header_cmn.log_ts_lsw     = ((uint32_t) log_time_stamp);
         log_pkt_ptr->log_header_cmn.log_ts_msw     = ((uint32_t) (log_time_stamp >> 32));
         log_pkt_ptr->log_header_cmn.log_segment_size    = log_info_ptr->buf_size;
         log_pkt_ptr->log_header_cmn.log_fragment_offset = 0;

         log_pkt_ptr->log_header_bs.media_fmt_id = log_info_ptr->data_info.media_fmt_id;

         log_commit(log_pkt_ptr);
         break;
      }
      case LOG_DATA_FMT_PCM:
      {
         // Get pointer of packet, the function takes in pointer to payload
         dls_log_pkt_pcm_data_t *log_pkt_ptr =
            (dls_log_pkt_pcm_data_t *)((uint8_t *)log_pkt_payload_ptr - sizeof(dls_log_pkt_pcm_data_t) +
                                       sizeof(log_pkt_ptr->payload));

         uint64_t log_time_stamp  = (log_info_ptr->log_time_stamp) ? log_info_ptr->log_time_stamp : posal_timer_get_time();

         log_pkt_ptr->version = POSAL_PINE_VERSION_LATEST;
         log_pkt_ptr->data_format_tag = AUDIOLOG_PCM_DATA_FORMAT;

         log_pkt_ptr->log_header_cmn.log_session_id = log_info_ptr->session_id;
         log_pkt_ptr->log_header_cmn.log_tap_id     = log_info_ptr->log_tap_id;
         log_pkt_ptr->log_header_cmn.log_seg_number = 0;
         log_pkt_ptr->log_header_cmn.log_ts_lsw     = ((uint32_t) log_time_stamp);
         log_pkt_ptr->log_header_cmn.log_ts_msw     = ((uint32_t) (log_time_stamp >> 32));

         log_pkt_ptr->log_header_cmn.log_segment_size    = log_info_ptr->buf_size;
         log_pkt_ptr->log_header_cmn.log_fragment_offset = 0;

         log_pkt_ptr->log_header_pcm.sampling_rate   = log_info_ptr->data_info.pcm_data_fmt.sampling_rate;
         log_pkt_ptr->log_header_pcm.num_channels    = log_info_ptr->data_info.pcm_data_fmt.num_channels;
         log_pkt_ptr->log_header_pcm.bits_per_sample = log_info_ptr->data_info.pcm_data_fmt.bits_per_sample;
         log_pkt_ptr->log_header_pcm.interleaved     = log_info_ptr->data_info.pcm_data_fmt.interleaved;
         log_pkt_ptr->log_header_pcm.q_factor        = log_info_ptr->data_info.pcm_data_fmt.q_factor;
         log_pkt_ptr->log_header_pcm.data_format     = log_info_ptr->data_info.pcm_data_fmt.data_format;

         for (int i = 0; i < log_pkt_ptr->log_header_pcm.num_channels; i++)
         {
            log_pkt_ptr->log_header_pcm.channel_mapping[i] =
               (uint8_t)log_info_ptr->data_info.pcm_data_fmt.channel_mapping[i];
         }

         log_commit(log_pkt_ptr);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Invalid data format : %u for log code 0x%X",
                log_info_ptr->data_fmt,
                log_info_ptr->log_code);
         return AR_EFAILED;
      }
      break;
   }

   return AR_EOK;
}

/**
  Free the log packet

  @param[in]  log_pkt_payload_ptr  Log packet pointer
  @return
   None

  @dependencies
  None.
 */
void posal_data_log_free(void *log_pkt_payload_ptr)
{
#ifdef DATA_LOGGING_DBG
   AR_MSG(DBG_HIGH_PRIO, "posal_data_log_free: 0x%lx", log_pkt_payload_ptr);
#endif

   if (NULL == log_pkt_payload_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error! NULL pointer passed to posal_data_log_free");
   }
   else
   {
      dls_log_pkt_bitstream_data_t *log_pkt_ptr =
         (dls_log_pkt_bitstream_data_t *)((uint8_t *)log_pkt_payload_ptr - sizeof(dls_log_pkt_bitstream_data_t) +
                                          sizeof(log_pkt_ptr->payload));
      log_free(log_pkt_ptr);
   }
}

/**
  Utility function used to find the size of the header of the log data format

  @param[in]     log_code            Log code of the module
  @param[in]     data_fmt            Data format
  @param[in]     num_channels        Number of channels to be logged
  @param[out]    log_header_size_ptr Ptr to the Header size of the log based on data format

  @return        error code

  @dependencies  None
 */
ar_result_t posal_data_get_log_packet_header_size(uint32_t                log_code,
                                                  posal_data_log_format_t data_fmt,
                                                  uint32_t                num_channels,
                                                  uint32_t               *log_header_size_ptr)
{
   switch (data_fmt)
   {
      case LOG_DATA_FMT_PCM:
      {
         /* -1, as the payload is declared as 1 byte in the pkt struct*/
         if (0 == num_channels)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "num_channels is 0 - LOG_DATA_FMT_PCM not supported with posal_data_log_alloc() - use "
                   "posal_data_log_alloc_v2() with valid num_channels");
            return AR_EFAILED;
         }
         *log_header_size_ptr = (sizeof(dls_log_pkt_pcm_data_t) + (sizeof(uint8_t) * (num_channels - 1)) - 1);
         break;
      }
      case LOG_DATA_FMT_RAW:
      case LOG_DATA_FMT_BITSTREAM:
      {
         *log_header_size_ptr = (sizeof(dls_log_pkt_bitstream_data_t) - 1);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "Invalid data format : %u for log code 0x%X", data_fmt, log_code);
         return AR_EFAILED;
      }
      break;
   }
   return AR_EOK;
}

static void *posal_data_alloc_log_buffer_internal(uint32_t                buf_size,
                                                  uint32_t                log_code,
                                                  posal_data_log_format_t data_fmt,
                                                  bool_t                  is_data_logging,
                                                  uint32_t                num_channels)
{
   ar_result_t result;
   uint32_t log_header_size;
   uint32_t    log_pkt_size;
   void *   log_pkt_ptr         = NULL;
   void *   log_pkt_payload_ptr = NULL;

   /* First calculate the log header size */
   result = posal_data_get_log_packet_header_size(log_code, data_fmt, num_channels, &log_header_size);
   if (AR_EOK != result)
   {
      return NULL;
   }

   /* Caclulate total packet size including the data payload */
   log_pkt_size = (log_header_size + buf_size);

   /* Allocate the log packet
    * log_alloc() returns pointer to the allocated log packet
    * Returns NULL if log code is disabled on the GUI
    */

   log_pkt_ptr = (void *)(uintptr_t) log_alloc(log_code, log_pkt_size);
   if (NULL == log_pkt_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "posal_data_alloc(): Invalid log ptr allocation log_pkt_ptr = %p", log_pkt_ptr);
      return NULL;
   }

#ifdef DATA_LOGGING_DBG
   AR_MSG(DBG_LOW_PRIO,
          "posal_data_alloc(): log_pkt_ptr= %p, Req Pkt Size= %ubytes log_code= 0x%X",
          log_pkt_ptr,
          log_pkt_size,
          log_code);
#endif
   // return pkt pointer for data logging
   if (is_data_logging)
   {
      return log_pkt_ptr;
   }

   if (LOG_DATA_FMT_PCM == data_fmt)
   {
      log_pkt_payload_ptr = &(((dls_log_pkt_pcm_data_t *)log_pkt_ptr)->payload);
   }
   else
   {
      log_pkt_payload_ptr = &(((dls_log_pkt_bitstream_data_t *)log_pkt_ptr)->payload);
   }

#ifdef DATA_LOGGING_DBG
   AR_MSG(DBG_MED_PRIO, "posal_data_alloc(): log_pkt_payload_ptr = %p", log_pkt_payload_ptr);
#endif

   return log_pkt_payload_ptr;
}

/**
  Utility function used to copy data to the payload buffer

  @param[in]     log_pkt_ptr     Buffer containing the payload pointer
  @param[in]     log_info_ptr    Pointer to struct containing common
  information for populating the log header for both PCM and BS
  data

  @return        error code

  @dependencies  None
 */
ar_result_t posal_data_fill_log_pkt_buffer(void *log_pkt_ptr, posal_data_log_info_t *log_info_ptr)
{
   ar_result_t                   result = AR_EOK;
   dls_header_cmn_t *            log_header_cmn_ptr;
   dls_header_pcm_data_t *       log_header_pcm_data_ptr;
   dls_header_bitstream_data_t * log_header_bs_data_ptr;
   posal_data_log_pcm_info_t *   pcm_data_info_ptr;
   uint32_t                      i;
   uint8_t *                     log_dst_ptr = NULL;
   dls_log_pkt_pcm_data_t *      pcm_data_pkt_ptr;
   dls_log_pkt_bitstream_data_t *bs_data_pkt_ptr;
   bool_t                        is_seg_pkt                  = FALSE;
   uint32_t                      num_channels                = 0;
   uint32_t                      num_bytes_per_channel       = 0;
   uint32_t                      total_num_bytes_per_channel = 0;

   if ((NULL == log_info_ptr) || (NULL == log_pkt_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "Log pkt ptr or log info ptr is NULL");
      return AR_EFAILED;
   }

   /* Check if the data format is either PCM or BITSTREAM */
   if ((log_info_ptr->data_fmt != LOG_DATA_FMT_PCM) && (log_info_ptr->data_fmt != LOG_DATA_FMT_BITSTREAM))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Invalid data format : %u for log code 0x%X",
             log_info_ptr->data_fmt,
             log_info_ptr->log_code);
      return AR_EFAILED;
   }

   pcm_data_pkt_ptr = (dls_log_pkt_pcm_data_t *)log_pkt_ptr;
   bs_data_pkt_ptr  = (dls_log_pkt_bitstream_data_t *)log_pkt_ptr;

   if (log_info_ptr->data_fmt == LOG_DATA_FMT_PCM)
   {
      log_header_cmn_ptr = &(pcm_data_pkt_ptr->log_header_cmn);
   }
   else
   {
      log_header_cmn_ptr = &(bs_data_pkt_ptr->log_header_cmn);
   }

   if (SEG_PKT == log_header_cmn_ptr->log_seg_number)
   {
#ifdef DATA_LOGGING_DBG
      AR_MSG(DBG_HIGH_PRIO, "Segmented Packet");
#endif
      is_seg_pkt = TRUE;

      total_num_bytes_per_channel = log_header_cmn_ptr->log_fragment_offset;
   }

   /* Fill in the common header for PCM and bitstream data */

   /*************** AUDIO_LOG_HEADER ***************/

   uint64_t log_time_stamp =
      (log_info_ptr->log_time_stamp) ? log_info_ptr->log_time_stamp : posal_timer_get_time();

   pcm_data_pkt_ptr->version = POSAL_PINE_VERSION_LATEST;
   /* Session ID is any random but unique value corresponding to a log code.
      For same log code and same tapping point, parser generates a new file
      with change in session ID. In general, session ID should be made function
      of stream/device parameters, so that a new log files gets generated if any of the parameter changes.
   */
   log_header_cmn_ptr->log_session_id = log_info_ptr->session_id; //
   log_header_cmn_ptr->log_tap_id     = log_info_ptr->log_tap_id;

   log_header_cmn_ptr->log_ts_lsw     = ((uint32_t) log_time_stamp);
   log_header_cmn_ptr->log_ts_msw     = ((uint32_t) (log_time_stamp >> 32));

   log_header_cmn_ptr->log_seg_number   = (*(log_info_ptr->seq_number_ptr))++; // Increment sequence count on every log
   log_header_cmn_ptr->log_segment_size = log_info_ptr->buf_size; // Size (in BYTES) of actual buffer to be logged
   log_header_cmn_ptr->log_fragment_offset = 0;

   switch (log_info_ptr->data_fmt)
   {
      case LOG_DATA_FMT_PCM:
      {
         /*************** AUDIOLOG_PCM_DATA_FORAMT ***************/
         pcm_data_pkt_ptr->data_format_tag = AUDIOLOG_PCM_DATA_FORMAT;

         log_header_pcm_data_ptr = (dls_header_pcm_data_t *)(&(pcm_data_pkt_ptr->log_header_pcm));

         pcm_data_info_ptr = (posal_data_log_pcm_info_t *)(&log_info_ptr->data_info.pcm_data_fmt);

         log_header_pcm_data_ptr->sampling_rate   = pcm_data_info_ptr->sampling_rate;
         log_header_pcm_data_ptr->num_channels    = pcm_data_info_ptr->num_channels;
         log_header_pcm_data_ptr->bits_per_sample = pcm_data_info_ptr->bits_per_sample;
         log_header_pcm_data_ptr->interleaved     = (pcm_data_info_ptr->interleaved == CAPI_INTERLEAVED) ? TRUE : FALSE;
         log_header_pcm_data_ptr->q_factor        = pcm_data_info_ptr->q_factor;
         log_header_pcm_data_ptr->data_format =
            posal_data_log_get_audiolog_data_format_type(pcm_data_info_ptr->data_format);
         if (NULL != pcm_data_info_ptr->channel_mapping)
         {
            for (i = 0; i < pcm_data_info_ptr->num_channels; i++)
            {
               log_header_pcm_data_ptr->channel_mapping[i] = (uint8_t)(pcm_data_info_ptr->channel_mapping[i]);
            }
         }
         else /* Provide the default mapping */
         {
            for (i = 0; i < pcm_data_info_ptr->num_channels; i++)
            {
               log_header_pcm_data_ptr->channel_mapping[i] = i + 1;
            }
         }

         log_dst_ptr = ((dls_log_pkt_pcm_data_t *)log_pkt_ptr)->payload +
                       (sizeof(uint8_t) * (pcm_data_info_ptr->num_channels - 1));
         break;
      }
      case LOG_DATA_FMT_RAW:
      case LOG_DATA_FMT_BITSTREAM:
      {
         /********** AUDIOLOG_BITSTREAM_DATA_FORAMT *************/
         bs_data_pkt_ptr->data_format_tag = AUDIOLOG_BS_DATA_FORMAT;

         log_header_bs_data_ptr = (dls_header_bitstream_data_t *)(&(bs_data_pkt_ptr->log_header_bs));

         log_header_bs_data_ptr->media_fmt_id = log_info_ptr->data_info.media_fmt_id;

         log_dst_ptr = ((dls_log_pkt_bitstream_data_t *)log_pkt_ptr)->payload;
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "Unsupported data format: %lu", log_info_ptr->data_fmt);
      }
      break;
   }

   /* Populate the log packet payload with the buffer to be logged */

   if (FALSE == is_seg_pkt)
   {
      memscpy(log_dst_ptr, log_info_ptr->buf_size, log_info_ptr->buf_ptr, log_info_ptr->buf_size);
   }
   else /* True */
   {
      pcm_data_info_ptr = (posal_data_log_pcm_info_t *)(&log_info_ptr->data_info.pcm_data_fmt);

      num_channels          = pcm_data_info_ptr->num_channels;
      num_bytes_per_channel = (log_info_ptr->buf_size / num_channels);

      for (i = 0; i < num_channels; i++)
      {
         memscpy(log_dst_ptr, num_bytes_per_channel, log_info_ptr->buf_ptr, num_bytes_per_channel);
         log_dst_ptr += num_bytes_per_channel;
         log_info_ptr->buf_ptr += total_num_bytes_per_channel;
      }
   }
   return result;
}

static ar_result_t posal_data_log_buffer(void *log_pkt_ptr, posal_data_log_info_t *log_info_ptr)
{
   ar_result_t result = AR_EOK;

   /* Copy data to the payload buffer */
   result = posal_data_fill_log_pkt_buffer(log_pkt_ptr, log_info_ptr);
   if (AR_EOK != result)
   {
      return AR_EFAILED;
   }
   /* Call the API to commit the log packet */
   log_commit(log_pkt_ptr);

   return result;
}
