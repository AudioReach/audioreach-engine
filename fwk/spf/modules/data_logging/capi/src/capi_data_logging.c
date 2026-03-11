/**
 *   \file capi_data_logging.c
 *   \brief
 *        This file contains CAPI implementation of Data logging module
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_data_logging_i.h"

/*==============================================================================
   Local Defines
==============================================================================*/
/* Stack size of AUDIO LOGGING module */ // TODO: profile and update stack
                                         // usage
#define DATA_LOGGING_MODULE_STACK_SIZE 2048

#define CAPI_DATA_LOGGING_MIN_KPPS (100)
#define CAPI_DATA_LOGGING_MIN_BW (100 * 1024)

#define QFORMAT_TO_BIT_WIDTH(q) ((PCM_Q_FACTOR_15 == q) ? 16 : ((PCM_Q_FACTOR_27 == q) ? 24 : 32))

#define NUM_BYTES_IN_16_BIT 2
#define NUM_BYTES_IN_32_BIT 4

#define DATA_LOGGING_MODE_BUFFERING 0

#define CAPI_DATA_LOGGING_MAX_IN_PORTS 1
#define CAPI_DATA_LOGGING_MAX_OUT_PORTS 1

#define LOG_CODE_AFE_RX_TX_OUT 0x1586

#define QFORMAT_SHIFT_FACTOR (PCM_Q_FACTOR_31 - PCM_Q_FACTOR_27) // used only for 1586 logging for Q27 to Q31 conversion

extern capi_vtbl_t capi_data_log_vtbl;

static inline uint32_t align_to_8_byte(const uint32_t num)
{
   return ((num + 7) & (0xFFFFFFF8));
}

/*==============================================================================
   Local Function forward declaration
==============================================================================*/

void incr_log_id(capi_data_logging_t *me_ptr)
{
   uint32_t n = me_ptr->nlpi_me_ptr->log_id & me_ptr->nlpi_me_ptr->log_id_reserved_mask;
   me_ptr->nlpi_me_ptr->log_id &= ~(me_ptr->nlpi_me_ptr->log_id_reserved_mask); // clear last few bits.
   n = (n + 1) & me_ptr->nlpi_me_ptr->log_id_reserved_mask;
   me_ptr->nlpi_me_ptr->log_id |= n;
}
/**
 * checks if interleaving matches intlv_to_check
 */
static bool_t check_interleaving(capi_media_fmt_v2_t *media_fmt_ptr, capi_interleaving_t intlv_to_check)
{
   bool_t result = FALSE;
   switch (media_fmt_ptr->header.format_header.data_format)
   {
      case CAPI_RAW_COMPRESSED:
      case CAPI_MAX_FORMAT_TYPE:
      {
         result = FALSE;
         break;
      }
      default:
      {
         result = (intlv_to_check == media_fmt_ptr->format.data_interleaving);
         break;
      }
   }
   return result;
}

static bool_t check_endianess(capi_media_fmt_v2_t *                   media_fmt_ptr,
                              fwk_extn_pcm_param_id_media_fmt_extn_t *ext_fmt_ptr,
                              uint32_t                                endianness_to_check)
{
   bool_t result = FALSE;
   switch (media_fmt_ptr->header.format_header.data_format)
   {
      case CAPI_MAX_FORMAT_TYPE:
      case CAPI_RAW_COMPRESSED:
      case CAPI_DEINTERLEAVED_RAW_COMPRESSED:
      {
         result = FALSE;
         break;
      }
      default:
      {
         result = (endianness_to_check == ext_fmt_ptr->endianness);
      }
   }
   return result;
}

static bool_t capi_data_logging_has_mf_changed(capi_media_fmt_v2_t *old_fmt, capi_media_fmt_v2_t *new_fmt)
{

   // bitstream_format,data_is_signed,data_interleaving,q_factor don't change,
   // hence not compared here.
   if (old_fmt->format.bits_per_sample != new_fmt->format.bits_per_sample)
   {
      return TRUE;
   }
   if ((old_fmt->format.num_channels != new_fmt->format.num_channels) ||
       (old_fmt->format.sampling_rate != new_fmt->format.sampling_rate))
   {
      return TRUE;
   }
   if (old_fmt->header.format_header.data_format != new_fmt->header.format_header.data_format)
   {
      return TRUE;
   }

   for (uint32_t j = 0; (j < new_fmt->format.num_channels) && (j < CAPI_MAX_CHANNELS_V2); j++)
   {
      if (old_fmt->format.channel_type[j] != new_fmt->format.channel_type[j])
      {
         return TRUE;
      }
   }
   return FALSE;
}

/* Check if media format is valid */
bool_t capi_data_logging_media_fmt_is_valid(capi_media_fmt_v2_t *media_fmt_ptr)
{
   return (CAPI_DATA_FORMAT_INVALID_VAL != media_fmt_ptr->format.num_channels &&
           CAPI_DATA_FORMAT_INVALID_VAL != media_fmt_ptr->format.bits_per_sample &&
           CAPI_DATA_FORMAT_INVALID_VAL != media_fmt_ptr->format.sampling_rate);
}

/* Utility to allocate internal log buffer */
capi_err_t check_alloc_log_buf(capi_data_logging_t *me_ptr)
{
   uint32_t       log_buf_size   = posal_data_log_get_max_buf_size();
   uint32_t       num_channels   = me_ptr->media_format.format.num_channels;
   const uint32_t num_chs_to_log = me_ptr->nlpi_me_ptr->number_of_channels_to_log;

   DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                    DBG_HIGH_PRIO,
                    "Entering check_alloc_log_buf, number of buffers to log %lu",num_chs_to_log);

   if (num_chs_to_log)
   {
      // for PCM media format try to allocate log buffer for 10ms.
      if (capi_data_logging_media_fmt_is_valid(&me_ptr->media_format))
      {
         DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                    DBG_HIGH_PRIO,
                    "check_alloc_log_buf, media_fmt_ptr->format.num_channels %lu "
                    "media_fmt_ptr->format.bits_per_sample %lu media_fmt_ptr->format.sampling_rate %lu",
                    me_ptr->media_format.format.num_channels,
                    me_ptr->media_format.format.bits_per_sample,
                    me_ptr->media_format.format.sampling_rate);

         const uint32_t NUM_10MS_IN_ONE_SEC = 100;
         uint32_t       bytes_per_sample    = (me_ptr->media_format.format.bits_per_sample / 8) * num_chs_to_log;
         uint32_t       frame_size_10ms =
            (me_ptr->media_format.format.sampling_rate * bytes_per_sample) / NUM_10MS_IN_ONE_SEC;

         log_buf_size = MIN(log_buf_size, frame_size_10ms);

         // normalize the buffer size.
         log_buf_size = (log_buf_size / bytes_per_sample) * bytes_per_sample;

         // assign the channel type for the logged channels
         int       j                   = 0;
         uint32_t *enabled_ch_mask_ptr = &me_ptr->nlpi_me_ptr->enabled_channel_mask_array[0];
         memset(me_ptr->nlpi_me_ptr->log_channel_type, 0, sizeof(me_ptr->nlpi_me_ptr->log_channel_type));
         uint32_t max_loop_count = CAPI_CMN_GET_MAX_CHANNEL_GROUPS_NEEDED(num_channels);

         for (uint32_t ch_index_grp_no = 0; ch_index_grp_no < max_loop_count; ch_index_grp_no++)
         {
            uint32_t start_ch_index = ch_index_grp_no * CAPI_CMN_CHANNELS_PER_MASK;
            uint32_t end_ch_index   = (ch_index_grp_no + 1) * CAPI_CMN_CHANNELS_PER_MASK;
            end_ch_index            = MIN(end_ch_index, num_channels);
            for (uint32_t i = start_ch_index; i < end_ch_index; i++)
            {
               uint32_t ch_mask = (1 << CAPI_CMN_MOD_WITH_32(i));
               if (ch_mask & enabled_ch_mask_ptr[ch_index_grp_no])
               {
                  me_ptr->nlpi_me_ptr->log_channel_type[j++] = me_ptr->media_format.format.channel_type[i];
#ifdef DATA_LOGGING_DBG
                  DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                   DBG_HIGH_PRIO,
                                   " Logging log_channel_type[%lu] =  "
                                   "%lu ",
                                   j - 1,
                                   me_ptr->nlpi_me_ptr->log_channel_type[j - 1]);
#endif
               }
            }
         }
      }
   }
   else
   {
      // no channels are required to log, free the buffer.
      log_buf_size = 0;
   }

   // reset in case of early return (failure) from this function.
   me_ptr->nlpi_me_ptr->per_ch_buf_part_size = 0;

   // If the log buf is already allocated and size is different then free it.
   if (me_ptr->nlpi_me_ptr->log_buf_ptr && (log_buf_size != me_ptr->nlpi_me_ptr->log_buf_size))
   {
      posal_memory_aligned_free(me_ptr->nlpi_me_ptr->log_buf_ptr);
      me_ptr->nlpi_me_ptr->log_buf_ptr  = NULL;
      me_ptr->nlpi_me_ptr->log_buf_size = 0;
#ifdef DATA_LOGGING_DBG
      DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_LOW_PRIO, "freed log buffer");
#endif
   }

   /* Allocating log buffer */
   if (NULL == me_ptr->nlpi_me_ptr->log_buf_ptr && 0 != log_buf_size)
   {
      /* override the module heap ID to Non LPI heap id i.e default heap id.
       *
       * 1) We allow placing data logging module in LPI graphs mainly because some LPI graphs (HW-EP), are used for
       * non-LPI use cases as well. E.g. playback of non-VOLTE voice calls. 2) Data logging is possible in
       * non-island-mode due to diag limitation & memory constraints. 3) For batch-logging of NLPI use cases we allocate
       * this buffer always in default heap. */

      me_ptr->nlpi_me_ptr->log_buf_ptr =
         (int8_t *)posal_memory_aligned_malloc(log_buf_size,
                                               LOG_BUFFER_ALIGNMENT,
                                               (POSAL_HEAP_ID)me_ptr->nlpi_me_ptr->nlpi_heap_id.heap_id);
      if (NULL == me_ptr->nlpi_me_ptr->log_buf_ptr)
      {
         DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_ERROR_PRIO, "log buffer allocation failed");
         return CAPI_ENOMEMORY;
      }

      me_ptr->nlpi_me_ptr->log_buf_size = log_buf_size;

      DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                       DBG_LOW_PRIO,
                       " allocated log buf size: %d, log_buf_ptr: 0x%lx",
                       me_ptr->nlpi_me_ptr->log_buf_size,
                       me_ptr->nlpi_me_ptr->log_buf_ptr);
   }
   else
   {
      DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                       DBG_HIGH_PRIO,
                       "log_buf_ptr: 0x%lx, log_buf_size %d",
                       me_ptr->nlpi_me_ptr->log_buf_ptr,
                       log_buf_size);
   }

   if (me_ptr->nlpi_me_ptr->log_buf_ptr && num_chs_to_log)
   {
      me_ptr->nlpi_me_ptr->per_ch_buf_part_size = me_ptr->nlpi_me_ptr->log_buf_size / num_chs_to_log;
   }

   return CAPI_EOK;
}

/* Utility function to pack the de-interleaved data in partial buffer case */
void data_logging_pack_data(capi_data_logging_t *me_ptr, int8_t *log_buf_ptr)
{
   uint32_t num_ch = me_ptr->nlpi_me_ptr->number_of_channels_to_log;

   if ((check_interleaving(&me_ptr->media_format, CAPI_DEINTERLEAVED_PACKED) ||
        check_interleaving(&me_ptr->media_format, CAPI_DEINTERLEAVED_UNPACKED) ||
        check_interleaving(&me_ptr->media_format, CAPI_DEINTERLEAVED_UNPACKED_V2)) &&
       (num_ch > 1))
   {
      if (me_ptr->nlpi_me_ptr->per_channel_log_buf_offset < me_ptr->nlpi_me_ptr->per_ch_buf_part_size)
      {
         for (uint32_t ch = 1; ch < num_ch; ch++)
         {
            memsmove(log_buf_ptr + ch * me_ptr->nlpi_me_ptr->per_channel_log_buf_offset,
                     me_ptr->nlpi_me_ptr->per_channel_log_buf_offset,
                     log_buf_ptr + ch * me_ptr->nlpi_me_ptr->per_ch_buf_part_size,
                     me_ptr->nlpi_me_ptr->per_ch_buf_part_size);
         }
      }
   }
}

/* Utility function to populate logging header and log data to diag */
void data_logging(capi_data_logging_t *me_ptr, int8_t *log_buf_ptr, bool_t is_full_frame)
{
   posal_data_log_info_t log_info_var;

   log_info_var.log_code       = me_ptr->nlpi_me_ptr->log_code;
   log_info_var.buf_ptr        = log_buf_ptr;
   log_info_var.buf_size       = me_ptr->nlpi_me_ptr->log_buf_fill_size;
   log_info_var.session_id     = me_ptr->nlpi_me_ptr->log_id;
   log_info_var.log_tap_id     = me_ptr->nlpi_me_ptr->log_tap_point_id;
   log_info_var.log_time_stamp = posal_timer_get_time();

   if (!log_info_var.buf_size)
   {
      return;
   }

   // pack the data for partial log buffer case (EOS, EOF), for de-interleaved unpacked data
   if (FALSE == is_full_frame)
   {
      data_logging_pack_data(me_ptr, log_buf_ptr);
   }

   // Big endian is logged as bin file.
   if (check_endianess(&me_ptr->media_format, &me_ptr->nlpi_me_ptr->extn_params, PCM_BIG_ENDIAN))
   {
      log_info_var.data_fmt = LOG_DATA_FMT_BITSTREAM;
#ifdef DATA_LOGGING_DBG
      DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_HIGH_PRIO, " Logging as BIN");
#endif
   }
   else
   {
      switch (me_ptr->media_format.header.format_header.data_format)
      {
         case CAPI_FIXED_POINT:
         case CAPI_FLOATING_POINT:
         {
            log_info_var.data_fmt = LOG_DATA_FMT_PCM;
#ifdef DATA_LOGGING_DBG
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_HIGH_PRIO, " Logging as PCM");
#endif
            break;
         }
         case CAPI_DEINTERLEAVED_RAW_COMPRESSED:
         {
            log_info_var.data_fmt = LOG_DATA_FMT_PCM;
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_HIGH_PRIO, " Logging Deinterleaved Raw Compressed as PCM");
            break;
         }
         case CAPI_RAW_COMPRESSED:
         default:
         {
            log_info_var.data_fmt = LOG_DATA_FMT_BITSTREAM;
#ifdef DATA_LOGGING_DBG
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_HIGH_PRIO, " Logging as BIN");
#endif
            break;
         }
      }
   }

   log_info_var.seq_number_ptr = &(me_ptr->nlpi_me_ptr->seq_number);

   switch (me_ptr->media_format.header.format_header.data_format)
   {
      case CAPI_RAW_COMPRESSED:
      {
         log_info_var.data_info.media_fmt_id = me_ptr->nlpi_me_ptr->bitstream_format;
         break;
      }
      case CAPI_DEINTERLEAVED_RAW_COMPRESSED:
      {
         log_info_var.data_info.media_fmt_id = me_ptr->nlpi_me_ptr->bitstream_format;
         posal_data_log_pcm_info_t *pcm_data = &(log_info_var.data_info.pcm_data_fmt);
         pcm_data->q_factor                  = me_ptr->media_format.format.q_factor;
         pcm_data->data_format               = me_ptr->media_format.header.format_header.data_format;
         pcm_data->num_channels              = me_ptr->nlpi_me_ptr->number_of_channels_to_log;
         pcm_data->interleaved               = me_ptr->media_format.format.data_interleaving;
         pcm_data->channel_mapping           = (uint16_t *)(me_ptr->nlpi_me_ptr->log_channel_type);

         // To extract on target logging of deint raw compr data, tool needs valid bps and sample rate to interpret as
         // pcm data, assuming some defaults below
         pcm_data->sampling_rate   = 48000;
         pcm_data->bits_per_sample = 16;

         for (uint32_t i = 0; i < pcm_data->num_channels; i++)
         {
            me_ptr->nlpi_me_ptr->log_channel_type[i] = i; // Interpreted as buf0, buf1 etc.
         }
         break;
      }
      default:
      {
         log_info_var.data_info.media_fmt_id = me_ptr->nlpi_me_ptr->bitstream_format;
         posal_data_log_pcm_info_t *pcm_data = &(log_info_var.data_info.pcm_data_fmt);
         pcm_data->q_factor                  = me_ptr->media_format.format.q_factor;
         pcm_data->data_format               = me_ptr->media_format.header.format_header.data_format;
         pcm_data->num_channels              = me_ptr->nlpi_me_ptr->number_of_channels_to_log;
         pcm_data->sampling_rate             = me_ptr->media_format.format.sampling_rate;
         pcm_data->bits_per_sample           = me_ptr->media_format.format.bits_per_sample;
         pcm_data->interleaved               = me_ptr->media_format.format.data_interleaving;
         pcm_data->channel_mapping           = (uint16_t *)(me_ptr->nlpi_me_ptr->log_channel_type);
         break;
      }
   }

#ifdef DATA_LOGGING_DBG
   DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_HIGH_PRIO, " logging one packet of data");
#endif

   posal_data_log_alloc_commit(&log_info_var);

   /* resetting fill size as the buffer is submitted to diag */
   me_ptr->nlpi_me_ptr->log_buf_fill_size = 0;

   me_ptr->nlpi_me_ptr->per_channel_log_buf_offset = 0;

   return;
}

capi_err_t pcm_fwk_ext_util(capi_data_logging_t *me_ptr, fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr)
{
   if ((16 != extn_ptr->bit_width) && (24 != extn_ptr->bit_width) && (32 != extn_ptr->bit_width) &&
       (64 != extn_ptr->bit_width))
   {
      DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                       DBG_ERROR_PRIO,
                       " set param received bad param for "
                       "sample_word_size %d",
                       extn_ptr->bit_width);
      return CAPI_EBADPARAM;
   }
   if ((PCM_LSB_ALIGNED != extn_ptr->alignment) && (PCM_MSB_ALIGNED != extn_ptr->alignment))
   {
      DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_ERROR_PRIO, " set param received bad param for alignment");
      return CAPI_EBADPARAM;
   }

   if ((PCM_LITTLE_ENDIAN != extn_ptr->endianness) && (PCM_BIG_ENDIAN != extn_ptr->endianness))
   {
      DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_ERROR_PRIO, " set param received bad param for endianness");
      return CAPI_EBADPARAM;
   }

   me_ptr->nlpi_me_ptr->extn_params.bit_width  = extn_ptr->bit_width;
   me_ptr->nlpi_me_ptr->extn_params.alignment  = extn_ptr->alignment;
   me_ptr->nlpi_me_ptr->extn_params.endianness = extn_ptr->endianness;

   return CAPI_EOK;
}

static void capi_data_logging_configure_data_scaling(capi_data_logging_t *me_ptr)
{
   me_ptr->nlpi_me_ptr->is_data_scaling_enabled = FALSE;

   if (capi_data_logging_media_fmt_is_valid(&me_ptr->media_format))
   {
      if ((LOG_CODE_AFE_RX_TX_OUT == me_ptr->nlpi_me_ptr->log_code) &&
          (CAPI_FIXED_POINT == me_ptr->media_format.header.format_header.data_format) &&
          check_endianess(&me_ptr->media_format, &me_ptr->nlpi_me_ptr->extn_params, PCM_LITTLE_ENDIAN))
      {
         if ((32 == me_ptr->media_format.format.bits_per_sample) && (27 == me_ptr->media_format.format.q_factor))
         {
            me_ptr->nlpi_me_ptr->is_data_scaling_enabled = TRUE;

            AR_MSG(DBG_LOW_PRIO,
                   SPF_LOG_PREFIX "CAPI Data Logging: data scaling enabled, "
                                  "module-id 0x%08lX, instance-id "
                                  "0x%08lX, log-id 0x%08lX, log-mask 0x%08lX, "
                                  "log-code 0x%lx, tap-point 0x%lx, logging_mode %d",
                   me_ptr->nlpi_me_ptr->mid,
                   me_ptr->nlpi_me_ptr->iid,
                   me_ptr->nlpi_me_ptr->log_id,
                   me_ptr->nlpi_me_ptr->log_id_reserved_mask,
                   me_ptr->nlpi_me_ptr->log_code,
                   me_ptr->nlpi_me_ptr->log_tap_point_id,
                   me_ptr->nlpi_me_ptr->logging_mode);
         }
      }
   }
   return;
}

static capi_err_t capi_data_logging_raise_output_media_fmt_event(capi_data_logging_t *me_ptr, capi_buf_t *payload_ptr)
{
   capi_err_t        result = CAPI_EOK;
   capi_event_info_t event_info;
   event_info.port_info.is_valid      = true;
   event_info.port_info.is_input_port = false;
   event_info.port_info.port_index    = 0;

   event_info.payload.actual_data_len = payload_ptr->actual_data_len;
   event_info.payload.data_ptr        = payload_ptr->data_ptr;
   event_info.payload.max_data_len    = payload_ptr->max_data_len;

   result = me_ptr->event_cb_info.event_cb(me_ptr->event_cb_info.event_context,
                                           CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED_V2,
                                           &event_info);
   if (CAPI_EOK != result)
   {
      DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                       DBG_ERROR_PRIO,
                       " Failed to raise "
                       "event for output media format");
   }
   return result;
}

capi_err_t capi_data_logging_raise_process_state_event(capi_data_logging_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   bool_t     is_enabled  = FALSE;
   if (0 != me_ptr->nlpi_me_ptr->log_code && me_ptr->nlpi_me_ptr->is_enabled)
   {
      is_enabled = TRUE;
   }

   // If no channels are required to log then disable the module.
   is_enabled = (me_ptr->nlpi_me_ptr->number_of_channels_to_log) ? is_enabled : FALSE;

   if (is_enabled && !check_interleaving(&me_ptr->media_format, CAPI_INTERLEAVED) &&
       check_endianess(&me_ptr->media_format, &me_ptr->nlpi_me_ptr->extn_params, PCM_BIG_ENDIAN))
   { // Big Endian logging is supported only if it interleaved, It is logged as bitstream.
      DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                       DBG_ERROR_PRIO,
                       " Warning! logging is disabled for big-endian non-interleaved data.");
      is_enabled = FALSE;
   }

   // not raising process state to the fwk. module must be enabled always. it can act as sink as well.
   me_ptr->is_process_state = is_enabled;

   return capi_result;
}

static bool_t data_logging_is_pcm_or_packetized(data_format_t format)
{
   bool_t is_pcm_or_pack = FALSE;
   switch (format)
   {
      case CAPI_FIXED_POINT:
      case CAPI_IEC61937_PACKETIZED:
      case CAPI_IEC60958_PACKETIZED:
      case CAPI_IEC60958_PACKETIZED_NON_LINEAR:
      case CAPI_DSD_DOP_PACKETIZED:
      case CAPI_GENERIC_COMPRESSED:
      case CAPI_COMPR_OVER_PCM_PACKETIZED:
      case CAPI_FLOATING_POINT:
      {
         is_pcm_or_pack = TRUE;
         break;
      }
      default:
         break;
   }

   return is_pcm_or_pack;
}

capi_err_t capi_data_logging_raise_kpps_bw_event(capi_data_logging_t *me_ptr)
{
   capi_err_t result = CAPI_EOK;
   bool_t     is_inp_mf_received =
      (me_ptr->media_format.format.bitstream_format != CAPI_DATA_FORMAT_INVALID_VAL) ? TRUE : FALSE;
   uint32_t kpps_estimate = 0;
   uint32_t bw_estimate   = CAPI_DATA_LOGGING_MIN_BW;
   uint32_t sampling_rate = 0;

   if ((!is_inp_mf_received) || (!me_ptr->nlpi_me_ptr->is_enabled))
   {
      return result;
   }

   // For PCM or packetized data formats, use the same formula to estimate kpps/bw
   if (data_logging_is_pcm_or_packetized(me_ptr->media_format.header.format_header.data_format))
   {
      uint32_t enabled_num_channels = me_ptr->nlpi_me_ptr->number_of_channels_to_log;
      uint32_t bytes_per_second     = me_ptr->media_format.format.sampling_rate * enabled_num_channels *
                                  (me_ptr->media_format.format.bits_per_sample >> 3);
      sampling_rate = me_ptr->media_format.format.sampling_rate;

      bw_estimate = 2 * bytes_per_second; // input and output stream
      // killo words (4 bytes) per second.
      kpps_estimate = (bytes_per_second >> 2) / 1000;
   }

   if (posal_data_log_code_status(me_ptr->nlpi_me_ptr->log_code))
   {
      kpps_estimate *= 4; // profiled
      bw_estimate *= 2;
      if (sampling_rate > 192000)
      {
         bw_estimate *= 2;
      }
   }

   kpps_estimate = (kpps_estimate < CAPI_DATA_LOGGING_MIN_KPPS) ? CAPI_DATA_LOGGING_MIN_KPPS : kpps_estimate;

   result |= capi_cmn_update_kpps_event(&me_ptr->event_cb_info, kpps_estimate);
   result |= capi_cmn_update_bandwidth_event(&me_ptr->event_cb_info, 0, bw_estimate);
   return result;
}

// function to return the channel_index_mask for the channels which are selected for logging.
uint32_t capi_data_logging_get_channel_index_mask_to_log(capi_data_logging_t *           me_ptr,
                                                         data_logging_select_channels_t *ch_mask_cfg_ptr)
{
   uint32_t ch_index_mask_to_log = DATA_LOGGING_ALL_CH_LOGGING_MASK;

   if (CAPI_DEINTERLEAVED_RAW_COMPRESSED == me_ptr->media_format.header.format_header.data_format)
   {
      // for raw compressed, logging all channels, ignoring configuration.
      uint64_t ch_index_mask_for_num_channels = ((uint64_t)1 << me_ptr->media_format.format.num_channels) - 1;
      ch_index_mask_to_log                    = ch_index_mask_for_num_channels;
   }
   else if (CAPI_DATA_FORMAT_INVALID_VAL != me_ptr->media_format.format.num_channels)
   {
      // configuration is based on channel type mask.
      if (DATA_LOGGING_SELECT_CHANNEL_TYPE_MASK == ch_mask_cfg_ptr->mode)
      {
         // get the 64 bit channel type mask configured for selective channel logging.
         uint64_t ch_type_mask_to_log_msw = ch_mask_cfg_ptr->channel_type_mask_msw;
         uint64_t ch_type_mask_to_log_lsw = ch_mask_cfg_ptr->channel_type_mask_lsw;
         uint64_t ch_type_mask_to_log     = (ch_type_mask_to_log_msw << 32) | ch_type_mask_to_log_lsw;

         // initialize the channel index mask to zero.
         ch_index_mask_to_log = 0;

         // iterate through each channel and check if a channel type is present in ch_type_mask_to_log.
         // if it is present then update the ch_index_mask_to_log
         for (int i = 0; i < me_ptr->media_format.format.num_channels; i++)
         {
            uint64_t ch_type_mask = 1 << me_ptr->media_format.format.channel_type[i];

            if (ch_type_mask_to_log & ch_type_mask)
            {
               ch_index_mask_to_log |= (1 << i);
            }
         }
      }
      // configuration is based on channel index mask already
      else if (DATA_LOGGING_SELECT_CHANNEL_INDEX_MASK == ch_mask_cfg_ptr->mode)
      {
         uint64_t ch_index_mask_for_num_channels = ((uint64_t)1 << me_ptr->media_format.format.num_channels) - 1;
         ch_index_mask_to_log                    = ch_index_mask_for_num_channels & ch_mask_cfg_ptr->channel_index_mask;
      }
      else
      {
         ch_index_mask_to_log = 0;
         DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                          DBG_ERROR_PRIO,
                          "Wrong configuration of ch_mask mode %lu",
                          ch_mask_cfg_ptr->mode);
      }
   }

   return ch_index_mask_to_log;
}

capi_err_t capi_data_logging_process_get_properties(capi_data_logging_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == proplist_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CAPI Data Logging: Get property received "
             "null property array");
      return CAPI_EBADPARAM;
   }

   uint32_t          fwk_extn_ids_arr[] = { FWK_EXTN_PCM };
   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req    = align_to_8_byte(sizeof(capi_data_logging_t));
   mod_prop.stack_size         = DATA_LOGGING_MODULE_STACK_SIZE;
   mod_prop.num_fwk_extns      = sizeof(fwk_extn_ids_arr) / sizeof(fwk_extn_ids_arr[0]);
   mod_prop.fwk_extn_ids_arr   = fwk_extn_ids_arr;
   mod_prop.is_inplace         = TRUE;
   mod_prop.req_data_buffering = FALSE;
   mod_prop.max_metadata_size  = 0;
   uint32_t miid               = me_ptr ? me_ptr->nlpi_me_ptr->iid : MIID_UNKNOWN;

   capi_result = capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      DATA_LOGGING_MSG(miid,
                       DBG_ERROR_PRIO,
                       " Get common basic properties "
                       "failed with capi_result %lu",
                       capi_result);
      return capi_result;
   }

   uint16_t     i;
   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      miid = me_ptr ? me_ptr->nlpi_me_ptr->iid : MIID_UNKNOWN;
      switch (prop_ptr[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_STACK_SIZE:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_MAX_METADATA_SIZE:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         {
            break;
         }
         case CAPI_MIN_PORT_NUM_INFO:
         {
            capi_buf_t *payload_ptr = &prop_ptr[i].payload;
            if (payload_ptr->max_data_len >= sizeof(capi_min_port_num_info_t))
            {
               capi_min_port_num_info_t *data_ptr = (capi_min_port_num_info_t *)payload_ptr->data_ptr;
               data_ptr->num_min_input_ports      = 1; // always needs input
               data_ptr->num_min_output_ports     = 0; // can act as sink
               payload_ptr->actual_data_len       = sizeof(capi_min_port_num_info_t);
            }
            else
            {
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr || ((prop_ptr[i].port_info.is_valid) && (0 != prop_ptr[i].port_info.port_index)))
            {
               DATA_LOGGING_MSG(miid,
                                DBG_ERROR_PRIO,
                                " Get property id 0x%lx "
                                "failed due to invalid/unexpected values",
                                (uint32_t)prop_ptr[i].id);
               CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
               break;
            }

            capi_result |= capi_cmn_handle_get_output_media_fmt_v2(&prop_ptr[i], &me_ptr->media_format);
            break;
         }
         case CAPI_PORT_DATA_THRESHOLD:
         {
            uint32_t threshold = 1;

            capi_result |= capi_cmn_handle_get_port_threshold(&prop_ptr[i], threshold);
            break;
         }
         case CAPI_IS_ELEMENTARY:
         {
            capi_buf_t *payload_ptr = &prop_ptr[i].payload;
            if (payload_ptr->max_data_len >= sizeof(capi_is_elementary_t))
            {
               capi_is_elementary_t *data_ptr = (capi_is_elementary_t *)payload_ptr->data_ptr;
               data_ptr->is_elementary        = TRUE;
               payload_ptr->actual_data_len   = sizeof(capi_is_elementary_t);
            }
            else
            {
               DATA_LOGGING_MSG(miid,
                                DBG_ERROR_PRIO,
                                " Get basic property id 0x%lx Bad param size %lu",
                                (uint32_t)prop_ptr[i].id,
                                payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         {
            capi_buf_t *                 payload_ptr   = &prop_ptr[i].payload;
            capi_interface_extns_list_t *intf_ext_list = (capi_interface_extns_list_t *)payload_ptr->data_ptr;
            capi_result |=
               ((payload_ptr->max_data_len < sizeof(capi_interface_extns_list_t)) ||
                (payload_ptr->max_data_len < (sizeof(capi_interface_extns_list_t) +
                                              (intf_ext_list->num_extensions * sizeof(capi_interface_extn_desc_t)))))
                  ? CAPI_ENEEDMORE
                  : capi_result;

            if (CAPI_FAILED(capi_result))
            {
               payload_ptr->actual_data_len = 0;
               DATA_LOGGING_MSG(miid, DBG_ERROR_PRIO, "Insufficient get property size.");
               break;
            }

            capi_interface_extn_desc_t *curr_intf_extn_desc_ptr =
               (capi_interface_extn_desc_t *)(payload_ptr->data_ptr + sizeof(capi_interface_extns_list_t));

            for (uint32_t j = 0; j < intf_ext_list->num_extensions; j++)
            {
               switch (curr_intf_extn_desc_ptr->id)
               {
                  /**
                   * data-logging module can act as pseudo sink. (0 == min-out-port)
                   * state propagation is blocked for pseudo sink module by framework.
                   * data-logging is a special case where we need to override the behavior of fwk for pseudo sink
                   * module. We don't want data-logging alone to keep the graph active if downstream is stopped.
                   * Therefore all state should be propagated backward.
                   */
                  case INTF_EXTN_PROP_PORT_DS_STATE:
                  case INTF_EXTN_DATA_PORT_OPERATION:
                  {
                     curr_intf_extn_desc_ptr->is_supported = TRUE;
                     break;
                  }
                  default:
                  {
                     curr_intf_extn_desc_ptr->is_supported = FALSE;
                     break;
                  }
               }
               curr_intf_extn_desc_ptr++;
            }

            break;
         }
         default:
         {
            capi_result |= CAPI_EUNSUPPORTED;
         }
      }
   }

   return capi_result;
}

/*
  This function is used to query the static properties to create the CAPI.

  param[in] init_set_prop_ptr: Pointer to the initializing property list
  param[in, out] static_prop_ptr: Pointer to the static property list

  return: CAPI_EOK(0) on success else failure error code
 */
capi_err_t capi_data_logging_get_static_properties(capi_proplist_t *init_set_prop_ptr, capi_proplist_t *static_prop_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == static_prop_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CAPI Data Logging: FAILED received bad property pointer for get "
             "property, %p",
             static_prop_ptr);
      return (CAPI_EFAILED);
   }

   result = capi_data_logging_process_get_properties((capi_data_logging_t *)NULL, static_prop_ptr);

   return result;
}

/*
  This function is used init the CAPI lib.

  param[in] capi_ptr: Pointer to the CAPI lib.
  param[in] init_set_prop_ptr: Pointer to the property list that needs to be
  initialized

  return: CAPI_EOK(0) on success else failure error code
 */
capi_err_t capi_data_logging_init(capi_t *capi_ptr, capi_proplist_t *init_set_prop_ptr)
{
   capi_err_t capi_result;

   if (NULL == capi_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI Data Logging: Init received bad pointer, %p", capi_ptr);
      return (CAPI_EFAILED);
   }
   if (NULL == init_set_prop_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CAPI Data Logging: FAILED received bad property pointer for get "
             "property, %p",
             init_set_prop_ptr);
      return CAPI_EOK;
   }

   capi_data_logging_t *me_ptr = (capi_data_logging_t *)(capi_ptr);

   memset(me_ptr, 0, sizeof(capi_data_logging_t));

   me_ptr->vtbl = &capi_data_log_vtbl;

   capi_cmn_init_media_fmt_v2(&me_ptr->media_format);

   capi_result = capi_cmn_set_basic_properties(init_set_prop_ptr, &me_ptr->heap_mem, &me_ptr->event_cb_info, 0);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI Data Logging: Set basic properties failed with result %lu", capi_result);
      return capi_result;
   }

   if (CAPI_SUCCEEDED(capi_result))
   {
      uint32_t nlpi_heap_id = MODIFY_HEAP_ID_FOR_MEM_TRACKING(me_ptr->heap_mem.heap_id, POSAL_HEAP_DEFAULT);

      me_ptr->nlpi_me_ptr =
         (capi_data_logging_non_island_t *)posal_memory_malloc(sizeof(capi_data_logging_non_island_t),
                                                               (POSAL_HEAP_ID)nlpi_heap_id);
      if (NULL == me_ptr->nlpi_me_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "MALLOC FAILED!");
         return CAPI_ENOMEMORY;
      }
      else
      {
         memset(me_ptr->nlpi_me_ptr, 0, sizeof(capi_data_logging_non_island_t));
         me_ptr->nlpi_me_ptr->nlpi_heap_id.heap_id = nlpi_heap_id;

         // by default enable all channel logging
         // me_ptr->nlpi_me_ptr->selective_ch_logging_cfg.mode               = DATA_LOGGING_SELECT_CHANNEL_INDEX_MASK;
         // me_ptr->nlpi_me_ptr->selective_ch_logging_cfg.channel_index_mask = DATA_LOGGING_ALL_CH_LOGGING_MASK;

         /* Initializing endianness to little endian */
         me_ptr->nlpi_me_ptr->extn_params.endianness = PCM_LITTLE_ENDIAN;

         /* Get a unique id for this instance and reset sequence number */
         me_ptr->nlpi_me_ptr->seq_number      = 0;
         me_ptr->nlpi_me_ptr->log_code_status = FALSE;
         me_ptr->nlpi_me_ptr->counter         = 0;

         // module is enabled by default
         me_ptr->nlpi_me_ptr->is_enabled = TRUE;
         me_ptr->is_process_state        = TRUE;

         // setting default config as config not recieved
         me_ptr->nlpi_me_ptr->selective_ch_logging_cfg_state = SELECTVE_CH_LOGGING_CFG_NOT_PRESENT;
      }
   }

   capi_result |= capi_cmn_raise_deinterleaved_unpacked_v2_supported_event(&me_ptr->event_cb_info);

   uint16_t     i;
   capi_prop_t *prop_ptr = init_set_prop_ptr->prop_ptr;

   for (i = 0; i < init_set_prop_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;

      switch (prop_ptr[i].id)
      {
         case CAPI_HEAP_ID:
         case CAPI_EVENT_CALLBACK_INFO:
         {
            break;
         }
         case CAPI_PORT_NUM_INFO:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_port_num_info_t))
            {
               capi_port_num_info_t *data_ptr = (capi_port_num_info_t *)payload_ptr->data_ptr;

               if ((data_ptr->num_input_ports > CAPI_DATA_LOGGING_MAX_IN_PORTS) ||
                   (data_ptr->num_output_ports > CAPI_DATA_LOGGING_MAX_OUT_PORTS))
               {
                  DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                   DBG_ERROR_PRIO,
                                   " Set property id 0x%lx "
                                   "number of input and output ports cannot be "
                                   "more "
                                   "than 1",
                                   (uint32_t)prop_ptr[i].id);
                  CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
               }
            }
            else
            {
               DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                DBG_ERROR_PRIO,
                                " Set, Param id 0x%lx Bad param size %lu",
                                (uint32_t)prop_ptr[i].id,
                                payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_MODULE_INSTANCE_ID:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_module_instance_id_t))
            {
               capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
               me_ptr->nlpi_me_ptr->iid            = data_ptr->module_instance_id;
               me_ptr->nlpi_me_ptr->mid            = data_ptr->module_id;
               AR_MSG(DBG_LOW_PRIO,
                      SPF_LOG_PREFIX "CAPI Data Logging: module-id "
                                     "0x%08lX, instance-id 0x%08lX, "
                                     "log-id 0x%08lX, log-mask 0x%08lX, "
                                     "log-code 0x%lX, tap-point 0x%lX",
                      me_ptr->nlpi_me_ptr->mid,
                      me_ptr->nlpi_me_ptr->iid,
                      me_ptr->nlpi_me_ptr->log_id,
                      me_ptr->nlpi_me_ptr->log_id_reserved_mask,
                      me_ptr->nlpi_me_ptr->log_code,
                      me_ptr->nlpi_me_ptr->log_tap_point_id);
            }
            else
            {
               DATA_LOGGING_MSG(MIID_UNKNOWN,
                                DBG_ERROR_PRIO,
                                " Set, Param id 0x%lx Bad param size %lu",
                                (uint32_t)prop_ptr[i].id,
                                payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_LOGGING_INFO:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_logging_info_t))
            {
               capi_logging_info_t *data_ptr             = (capi_logging_info_t *)payload_ptr->data_ptr;
               me_ptr->nlpi_me_ptr->log_id               = data_ptr->log_id;
               me_ptr->nlpi_me_ptr->log_id_reserved_mask = data_ptr->log_id_mask;
               AR_MSG(DBG_LOW_PRIO,
                      SPF_LOG_PREFIX "CAPI Data Logging: module-id "
                                     "0x%08lX, instance-id 0x%08lX, "
                                     "log-id 0x%08lX, log-mask 0x%08lX, "
                                     "log-code 0x%lx, tap-point 0x%lx",
                      me_ptr->nlpi_me_ptr->mid,
                      me_ptr->nlpi_me_ptr->iid,
                      me_ptr->nlpi_me_ptr->log_id,
                      me_ptr->nlpi_me_ptr->log_id_reserved_mask,
                      me_ptr->nlpi_me_ptr->log_code,
                      me_ptr->nlpi_me_ptr->log_tap_point_id);
            }
            else
            {
               DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                DBG_ERROR_PRIO,
                                " Set, Param id 0x%lx Bad param size %lu",
                                (uint32_t)prop_ptr[i].id,
                                payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         default:
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_HIGH_PRIO,
                             " FAILED Set Property for 0x%x. Not "
                             "supported.",
                             prop_ptr[i].id);
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         }
      }
   }

   return capi_result;
}

/*==============================================================================
   Local Function Implementation
==============================================================================*/
capi_err_t capi_data_logging_set_properties(capi_t *capi_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if ((NULL == capi_ptr) || (NULL == proplist_ptr))
   {
      DATA_LOGGING_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, " FAILED received bad pointer in set_property");
      return CAPI_EFAILED;
   }

   capi_data_logging_t *me_ptr = (capi_data_logging_t *)capi_ptr;

   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;
   if (NULL == prop_ptr)
   {
      DATA_LOGGING_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, " FAILED received bad pointer in prop_ptr");
      return CAPI_EFAILED;
   }

   capi_result = capi_cmn_set_basic_properties(proplist_ptr, NULL, &me_ptr->event_cb_info, TRUE);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI Data Logging: Set basic properties failed with result %lu", capi_result);
      return capi_result;
   }

   uint32_t i;

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;

      switch (prop_ptr[i].id)
      {
         case CAPI_ALGORITHMIC_RESET:
         {
            // Flushing logs
            data_logging(me_ptr, me_ptr->nlpi_me_ptr->log_buf_ptr, FALSE);

            // Do only once (for input)
            if (prop_ptr->port_info.is_valid && prop_ptr->port_info.is_input_port)
            {
               incr_log_id(me_ptr);
            }
            break;
         }
         case CAPI_CUSTOM_PROPERTY:
         {
            capi_custom_property_t *cust_prop_ptr = (capi_custom_property_t *)payload_ptr->data_ptr;
            switch (cust_prop_ptr->secondary_prop_id)
            {
               default:
               {
                  DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                   DBG_ERROR_PRIO,
                                   " Unknown Sec Prop[%d]",
                                   cust_prop_ptr->secondary_prop_id);
                  capi_result |= CAPI_EUNSUPPORTED;
                  break;
               }
            } /* inner switch - CUSTOM Properties */
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            if (payload_ptr->actual_data_len < sizeof(capi_data_format_header_t))
            {
               DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                DBG_ERROR_PRIO,
                                " received bad prop_id 0x%x",
                                (uint32_t)prop_ptr[i].id);
               DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                DBG_ERROR_PRIO,
                                " payload_actual_data len: "
                                "%d, required size at least: %d",
                                payload_ptr->actual_data_len,
                                sizeof(capi_data_format_header_t));
               return CAPI_EBADPARAM;
            }

            switch (((capi_data_format_header_t *)(payload_ptr->data_ptr))->data_format)
            {
               case CAPI_RAW_COMPRESSED:
               {
                  if (payload_ptr->actual_data_len <
                      (sizeof(capi_data_format_header_t) + sizeof(capi_raw_compressed_data_format_t)))
                  {
                     DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                      DBG_ERROR_PRIO,
                                      " received bad prop_id 0x%x",
                                      (uint32_t)prop_ptr[i].id);
                     DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                      DBG_ERROR_PRIO,
                                      " payload_actual_data len: "
                                      "%d, required size at least: %d",
                                      payload_ptr->actual_data_len,
                                      sizeof(capi_data_format_header_t) + sizeof(capi_standard_data_format_v2_t));
                     return CAPI_EBADPARAM;
                  }
                  capi_raw_compressed_data_format_t *media_fmt_ptr =
                     (capi_raw_compressed_data_format_t *)(payload_ptr->data_ptr + sizeof(capi_data_format_header_t));
                  me_ptr->media_format.header.format_header.data_format =
                     ((capi_data_format_header_t *)(payload_ptr->data_ptr))->data_format;
                  me_ptr->nlpi_me_ptr->bitstream_format              = media_fmt_ptr->bitstream_format;
                  me_ptr->nlpi_me_ptr->number_of_channels_to_log     = 1;
                  me_ptr->nlpi_me_ptr->enabled_channel_mask_array[0] = 0x1;
                  DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                   DBG_LOW_PRIO,
                                   " raw media fmt, fmt_id 0x%lx",
                                   me_ptr->nlpi_me_ptr->bitstream_format);
                  break;
               }
               case CAPI_DEINTERLEAVED_RAW_COMPRESSED:
               {
                  if (payload_ptr->actual_data_len <
                      (sizeof(capi_data_format_header_t) + sizeof(capi_deinterleaved_raw_compressed_data_format_t)))
                  {
                     DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                      DBG_ERROR_PRIO,
                                      " received bad prop_id 0x%x",
                                      (uint32_t)prop_ptr[i].id);
                     DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                      DBG_ERROR_PRIO,
                                      " payload_actual_data len: "
                                      "%d, required size at least: %d",
                                      payload_ptr->actual_data_len,
                                      sizeof(capi_data_format_header_t) +
                                         sizeof(capi_deinterleaved_raw_compressed_data_format_t));
                     return CAPI_EBADPARAM;
                  }

                  capi_set_get_media_format_t *main_ptr = (capi_set_get_media_format_t *)(payload_ptr->data_ptr);
                  capi_deinterleaved_raw_compressed_data_format_t *data_ptr =
                     (capi_deinterleaved_raw_compressed_data_format_t *)(main_ptr + 1);

                  me_ptr->media_format.header.format_header.data_format =
                     ((capi_data_format_header_t *)(payload_ptr->data_ptr))->data_format;

                  if (data_ptr->bufs_num < 1)
                  {
                     DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                      DBG_ERROR_PRIO,
                                      " received bad prop_id 0x%x",
                                      data_ptr->bufs_num);
                     return CAPI_EBADPARAM;
                  }

                  me_ptr->media_format.format.bitstream_format  = data_ptr->bitstream_format;
                  me_ptr->media_format.format.num_channels      = data_ptr->bufs_num;
                  me_ptr->media_format.format.data_interleaving = CAPI_DEINTERLEAVED_UNPACKED;
                  memset(me_ptr->media_format.format.channel_type,
                         0,
                         sizeof(uint16_t) * me_ptr->media_format.format.num_channels);
                  capi_result = capi_data_logging_update_enabled_channels_to_log(me_ptr,
                                                                                 me_ptr->nlpi_me_ptr
                                                                                    ->selective_ch_logging_cfg_state);
                  if (CAPI_FAILED(capi_result))
                  {
                     return capi_result;
                  }
                  me_ptr->nlpi_me_ptr->number_of_channels_to_log = data_ptr->bufs_num;
                  me_ptr->nlpi_me_ptr->bitstream_format          = data_ptr->bitstream_format;

                  DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                   DBG_LOW_PRIO,
                                   " deinterleaved raw media fmt, num_bufs 0x%lx",
                                   me_ptr->media_format.format.num_channels);
                  break;
               }
               default:
               {
                  if (payload_ptr->actual_data_len <
                      (sizeof(capi_data_format_header_t) + sizeof(capi_standard_data_format_v2_t)))
                  {
                     DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                      DBG_ERROR_PRIO,
                                      " received bad prop_id 0x%x",
                                      (uint32_t)prop_ptr[i].id);
                     DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                      DBG_ERROR_PRIO,
                                      " payload_actual_data len: "
                                      "%d, required size at least: %d",
                                      payload_ptr->actual_data_len,
                                      sizeof(capi_data_format_header_t) + sizeof(capi_standard_data_format_v2_t));
                     return CAPI_EBADPARAM;
                  }

                  /* If the media format has changed and the previous media format is
                     valid, log the existing data in
                     internal log buffer */
                  if ((capi_data_logging_media_fmt_is_valid(&me_ptr->media_format)) &&
                      capi_data_logging_has_mf_changed(&me_ptr->media_format,
                                                       (capi_media_fmt_v2_t *)payload_ptr->data_ptr))
                  {
                     DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_HIGH_PRIO, " MF Changed: Logging old data");
                     data_logging(me_ptr, me_ptr->nlpi_me_ptr->log_buf_ptr, FALSE);
                  }

                  capi_set_get_media_format_t *   main_ptr = (capi_set_get_media_format_t *)(payload_ptr->data_ptr);
                  capi_standard_data_format_v2_t *data_ptr = (capi_standard_data_format_v2_t *)(main_ptr + 1);
                  /* Cache media format */
                  uint32_t size = sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t) +
                                  (sizeof(capi_channel_type_t) * data_ptr->num_channels);

                  memscpy(&me_ptr->media_format, size, payload_ptr->data_ptr, size);
                  capi_result = capi_data_logging_update_enabled_channels_to_log(me_ptr,
                                                                                 me_ptr->nlpi_me_ptr
                                                                                    ->selective_ch_logging_cfg_state);
                  if (CAPI_FAILED(capi_result))
                  {
                     return capi_result;
                  }
                  me_ptr->nlpi_me_ptr->number_of_channels_to_log = get_number_of_channels_to_log(me_ptr);

                  if (!me_ptr->nlpi_me_ptr->is_media_fmt_extn_received)
                  {
                     me_ptr->nlpi_me_ptr->extn_params.bit_width =
                        QFORMAT_TO_BIT_WIDTH(me_ptr->media_format.format.q_factor);
                  }
                  break;
               }
            }

            capi_result = check_alloc_log_buf(me_ptr);
            if (CAPI_FAILED(capi_result))
            {
               return capi_result;
            }
            capi_data_logging_raise_kpps_bw_event(me_ptr);
            capi_data_logging_raise_process_state_event(me_ptr);
            capi_cmn_update_algo_delay_event(&me_ptr->event_cb_info, 0);
            capi_data_logging_raise_output_media_fmt_event(me_ptr, payload_ptr);
            capi_data_logging_configure_data_scaling(me_ptr);
            incr_log_id(me_ptr);
            break;
         }
         default:
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             " received bad prop_id 0x%x",
                             (uint32_t)prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
         }
      }
   }

   return capi_result;
}

capi_err_t capi_data_logging_get_properties(capi_t *capi_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t rc = CAPI_EOK;
   if (NULL == capi_ptr || NULL == proplist_ptr)
   {
      DATA_LOGGING_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, " NULL capi_ptr[%p], proplist_ptr[%p]", capi_ptr, proplist_ptr);
      return CAPI_EFAILED;
   }

   capi_data_logging_t *me_ptr = (capi_data_logging_t *)capi_ptr;

   rc = capi_data_logging_process_get_properties(me_ptr, proplist_ptr);

   return rc;
}

capi_err_t capi_data_logging_set_param(capi_t *                capi_ptr,
                                       uint32_t                param_id,
                                       const capi_port_info_t *port_info_ptr,
                                       capi_buf_t *            params_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == capi_ptr || NULL == params_ptr)
   {
      DATA_LOGGING_MSG(MIID_UNKNOWN,
                       DBG_ERROR_PRIO,
                       " FAILED received bad property pointer for "
                       "param_id property, 0x%x",
                       param_id);
      return (CAPI_EFAILED);
   }

   capi_data_logging_t *me_ptr = (capi_data_logging_t *)((capi_ptr));

   switch (param_id)
   {
      case PARAM_ID_MODULE_ENABLE:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_module_enable_t))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             " Enable SetParam 0x%lx, invalid param size %lx ",
                             param_id,
                             params_ptr->actual_data_len);
            result = CAPI_ENEEDMORE;
            break;
         }
         param_id_module_enable_t log_state = *((param_id_module_enable_t *)params_ptr->data_ptr);

         // if the state is going from enabled to disabled && if we are in buffering
         // mode, we
         // need to log the buffered packets.

         if ((me_ptr->nlpi_me_ptr->is_enabled && (FALSE == log_state.enable)) &&
             (me_ptr->nlpi_me_ptr->log_buf_fill_size &&
              (DATA_LOGGING_MODE_BUFFERING == me_ptr->nlpi_me_ptr->logging_mode)))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_HIGH_PRIO,
                             " State Changed from EN to DIS "
                             "able: Logging old data");
            data_logging(me_ptr, me_ptr->nlpi_me_ptr->log_buf_ptr, FALSE);
         }

         me_ptr->nlpi_me_ptr->is_enabled = log_state.enable;

         DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                          DBG_HIGH_PRIO,
                          " Enable/Disable (1/0) = %u - Param Set Sucessfully",
                          me_ptr->nlpi_me_ptr->is_enabled);

         capi_data_logging_raise_kpps_bw_event(me_ptr);

         capi_data_logging_raise_process_state_event(me_ptr);

         break;
      }

      case PARAM_ID_DATA_LOGGING_ISLAND_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(data_logging_island_t))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             " Island Logging allow SetParam 0x%lx, invalid param size %lx ",
                             param_id,
                             params_ptr->actual_data_len);
            result = CAPI_ENEEDMORE;
            break;
         }

         data_logging_island_t *cfg_ptr     = (data_logging_island_t *)params_ptr->data_ptr;
         bool_t                 island_vote = FALSE;

         if (me_ptr->forced_logging != cfg_ptr->forced_logging)
         {
            if (!(cfg_ptr->forced_logging))
            {
               island_vote = CAPI_CMN_ISLAND_VOTE_ENTRY;
            }
            else if (cfg_ptr->forced_logging)
            {
               island_vote = CAPI_CMN_ISLAND_VOTE_EXIT;
            }

            me_ptr->forced_logging = cfg_ptr->forced_logging;

            result = capi_cmn_raise_island_vote_event(&me_ptr->event_cb_info, island_vote);

            if (CAPI_FAILED(result))
            {
               break;
            }

            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_HIGH_PRIO,
                             " Island Logging Allowed/Disallowed (1/0) = %u - Param Set Sucessfully",
                             me_ptr->forced_logging);
         }
         else
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_HIGH_PRIO,
                             " Island Logging Allowed/Disallowed (1/0) = %u - Param Set Sucessfully"
                             "but did not vote, as current config and sent are same",
                             me_ptr->forced_logging);
         }

         break;
      }
      case PARAM_ID_DATA_LOGGING_SELECT_CHANNELS:
      {
         if (params_ptr->actual_data_len < sizeof(data_logging_select_channels_t))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             " Set "
                             "PARAM_ID_DATA_LOGGING_SELECT_CHANNELS fail: Invalid "
                             "payload size: "
                             "%u",
                             params_ptr->actual_data_len);
            return (CAPI_EBADPARAM);
         }

         data_logging_select_channels_t *ch_mask_cfg_ptr = (data_logging_select_channels_t *)params_ptr->data_ptr;

         DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                          DBG_HIGH_PRIO,
                          "Selective channel logging config"
                          "mode %lu, "
                          "channel index mask 0x%x, "
                          "channel type mask [0x%x-0x%x]",
                          ch_mask_cfg_ptr->mode,
                          ch_mask_cfg_ptr->channel_index_mask,
                          ch_mask_cfg_ptr->channel_type_mask_msw,
                          ch_mask_cfg_ptr->channel_type_mask_lsw);

         // copy the configuration
         memscpy(&me_ptr->nlpi_me_ptr->selective_ch_logging_cfg,
                 sizeof(data_logging_select_channels_t),
                 params_ptr->data_ptr,
                 params_ptr->actual_data_len);

         /* If the media format is valid and enable channel mask is changed then
          * 1. log the existing data
          * 2. increment the counter
          * 3. realloc the log buffer.*/
         if (CAPI_DATA_FORMAT_INVALID_VAL != me_ptr->media_format.format.num_channels)
         {
            uint32_t new_enabled_ch_mask = capi_data_logging_get_channel_index_mask_to_log(me_ptr, ch_mask_cfg_ptr);

            if (new_enabled_ch_mask != me_ptr->nlpi_me_ptr->enabled_channel_mask_array[0])
            {
               data_logging(me_ptr, me_ptr->nlpi_me_ptr->log_buf_ptr, FALSE);
               incr_log_id(me_ptr);
               DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                DBG_HIGH_PRIO,
                                "data logging channel mask changed. prev_mask 0x%x, new_mask 0x%x",
                                me_ptr->nlpi_me_ptr->enabled_channel_mask_array[0],
                                new_enabled_ch_mask);

               me_ptr->nlpi_me_ptr->enabled_channel_mask_array[0] = new_enabled_ch_mask;
               me_ptr->nlpi_me_ptr->number_of_channels_to_log     = get_number_of_channels_to_log(me_ptr);

               check_alloc_log_buf(me_ptr);

               // update votes because number of channels to log are changed
               capi_data_logging_raise_kpps_bw_event(me_ptr);
               capi_data_logging_raise_process_state_event(me_ptr);
            }
         }
         me_ptr->nlpi_me_ptr->selective_ch_logging_cfg_state = SELECTVE_CH_LOGGING_CFG_V1;
         break;
      }

      case PARAM_ID_DATA_LOGGING_SELECT_CHANNELS_V2:
      {
         result = capi_data_logging_set_selective_channels_v2_pid(me_ptr, params_ptr);
         if (CAPI_FAILED(result))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             "Set PARAM_ID_DATA_LOGGING_SELECT_CHANNELS_V2 failed!");
            return result;
         }

         break;
      }

      case PARAM_ID_DATA_LOGGING_CONFIG:
      {
         if (params_ptr->actual_data_len < sizeof(data_logging_config_t))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             " Set "
                             "PARAM_ID_DATA_LOGGING_CONFIG fail: Invalid "
                             "payload size: "
                             "%u",
                             params_ptr->actual_data_len);
            return (CAPI_EBADPARAM);
         }

         data_logging_config_t *buf_cfg_ptr = (data_logging_config_t *)params_ptr->data_ptr;

         me_ptr->nlpi_me_ptr->log_code = buf_cfg_ptr->log_code;
         // In case the user does not provide a tap point id, put a sensible default:
         // the module instance id
         // As long as the tap point id does not conflict with one that exists
         // already
         if (0 == buf_cfg_ptr->log_tap_point_id)
         {
            me_ptr->nlpi_me_ptr->log_tap_point_id = me_ptr->nlpi_me_ptr->iid;
         }
         else
         {
            me_ptr->nlpi_me_ptr->log_tap_point_id = buf_cfg_ptr->log_tap_point_id;
         }
         me_ptr->nlpi_me_ptr->logging_mode = buf_cfg_ptr->mode;

         AR_MSG(DBG_LOW_PRIO,
                SPF_LOG_PREFIX "CAPI Data Logging: module-id 0x%08lX, instance-id "
                               "0x%08lX, log-id 0x%08lX, log-mask 0x%08lX, "
                               "log-code 0x%lx, tap-point 0x%lx, logging_mode %d",
                me_ptr->nlpi_me_ptr->mid,
                me_ptr->nlpi_me_ptr->iid,
                me_ptr->nlpi_me_ptr->log_id,
                me_ptr->nlpi_me_ptr->log_id_reserved_mask,
                me_ptr->nlpi_me_ptr->log_code,
                me_ptr->nlpi_me_ptr->log_tap_point_id,
                me_ptr->nlpi_me_ptr->logging_mode);

         capi_data_logging_raise_kpps_bw_event(me_ptr);

         capi_data_logging_raise_process_state_event(me_ptr);
         capi_data_logging_configure_data_scaling(me_ptr);
      }
      break;
      case FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN:
      {
         if ((params_ptr->actual_data_len >= sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t)) &&
             (TRUE == port_info_ptr->is_input_port))
         {
            fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr =
               (fwk_extn_pcm_param_id_media_fmt_extn_t *)(params_ptr->data_ptr);

            capi_err_t local_result = pcm_fwk_ext_util(me_ptr, extn_ptr);
            if (CAPI_EOK == local_result)
            {
               // capi_data_logging_raise_output_media_fmt_event(me_ptr);
               capi_data_logging_raise_process_state_event(me_ptr);
               me_ptr->nlpi_me_ptr->is_media_fmt_extn_received = true;
            }
            else
            {
               result |= local_result;
               DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                                DBG_ERROR_PRIO,
                                " Set param failed for param id %d, result %d",
                                param_id,
                                result);
            }
         }
         else
         {
            CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             " set param failed because of "
                             "length issues, 0x%p, in_len = %d, "
                             "needed_len = %d",
                             param_id,
                             params_ptr->actual_data_len,
                             sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t));
         }
         break;
      }
      case INTF_EXTN_PARAM_ID_PORT_DS_STATE:
      {
         intf_extn_param_id_port_ds_state_t *port_state_ptr =
            (intf_extn_param_id_port_ds_state_t *)(params_ptr->data_ptr);

         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_port_ds_state_t))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             "Insufficient size for port downstream state setparam.");
            return CAPI_ENEEDMORE;
         }
         else
         {
            // if module is in sink mode then block the state propagation.
            if (NULL == me_ptr->event_cb_info.event_cb || me_ptr->nlpi_me_ptr->is_sink_mode)
            {
               return CAPI_EOK;
            }

            intf_extn_event_id_port_ds_state_t event;
            event.input_port_index = 0;
            event.port_state       = port_state_ptr->port_state;

            capi_event_info_t event_info;
            event_info.port_info.is_valid = FALSE;

            // Package the fwk event within the data_to_dsp capi event.
            capi_event_data_to_dsp_service_t evt = { 0 };

            evt.param_id                = INTF_EXTN_EVENT_ID_PORT_DS_STATE;
            evt.token                   = 0;
            evt.payload.actual_data_len = sizeof(event);
            evt.payload.data_ptr        = (int8_t *)&event;
            evt.payload.max_data_len    = sizeof(event);

            event_info.payload.actual_data_len = sizeof(capi_event_data_to_dsp_service_t);
            event_info.payload.data_ptr        = (int8_t *)&evt;
            event_info.payload.max_data_len    = sizeof(capi_event_data_to_dsp_service_t);
            result                             = me_ptr->event_cb_info.event_cb(me_ptr->event_cb_info.event_context,
                                                    CAPI_EVENT_DATA_TO_DSP_SERVICE,
                                                    &event_info);

            return result;
         }
         break;
      }

      case INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION:
      {
         if (params_ptr->actual_data_len < sizeof(intf_extn_data_port_operation_t))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_ERROR_PRIO, "Insufficient size for data port op setparam.");
            return CAPI_ENEEDMORE;
         }

         intf_extn_data_port_operation_t *data_ptr = (intf_extn_data_port_operation_t *)params_ptr->data_ptr;
         if (params_ptr->actual_data_len <
             sizeof(intf_extn_data_port_operation_t) + (data_ptr->num_ports * sizeof(intf_extn_data_port_id_idx_map_t)))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_ERROR_PRIO, "Insufficient size for data port op setparam.");
            return CAPI_ENEEDMORE;
         }

         if (FALSE == data_ptr->is_input_port) // OUTPUT PORT OPERATION
         {
            if (INTF_EXTN_DATA_PORT_OPEN == data_ptr->opcode)
            {
               me_ptr->nlpi_me_ptr->is_sink_mode = FALSE;
            }
            else if (INTF_EXTN_DATA_PORT_CLOSE == data_ptr->opcode)
            {
               me_ptr->nlpi_me_ptr->is_sink_mode = TRUE;
            }
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_HIGH_PRIO,
                             "sink mode %d",
                             me_ptr->nlpi_me_ptr->is_sink_mode);
         }
         break;
      }

      default:
      {
         DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_HIGH_PRIO, " Unsupported Param id ::0x%x \n", param_id);
         result = CAPI_EUNSUPPORTED;
      }
      break;
   }

   return result;
}

/*------------------------------------------------------------------------
  Function name: capi_audio_logging_get_param
  Gets either a parameter value or a parameter structure containing multiple
  parameters. In the event of a failure, the appropriate error code is
  returned.
 * -----------------------------------------------------------------------*/
capi_err_t capi_data_logging_get_param(capi_t *                capi_ptr,
                                       uint32_t                param_id,
                                       const capi_port_info_t *port_info_ptr,
                                       capi_buf_t *            params_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == capi_ptr || NULL == params_ptr)
   {
      DATA_LOGGING_MSG(MIID_UNKNOWN,
                       DBG_ERROR_PRIO,
                       " FAILED received bad property pointer for "
                       "param_id property, 0x%x",
                       param_id);
      return (CAPI_EFAILED);
   }

   capi_data_logging_t *me_ptr = (capi_data_logging_t *)((capi_ptr));

   switch (param_id)
   {
      case PARAM_ID_MODULE_ENABLE:
      {
         /*check if available size is large enough*/
         if (params_ptr->max_data_len < sizeof(param_id_module_enable_t))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             " Get PARAM_ID_MODULE_ENABLE, size received %ld, size expected %ld",
                             params_ptr->max_data_len,
                             sizeof(param_id_module_enable_t));
            return (CAPI_EBADPARAM);
         }

         param_id_module_enable_t *param_payload_ptr = (param_id_module_enable_t *)params_ptr->data_ptr;
         param_payload_ptr->enable                   = me_ptr->nlpi_me_ptr->is_enabled;
         params_ptr->actual_data_len                 = sizeof(param_id_module_enable_t);

         DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                          DBG_HIGH_PRIO,
                          " Get PARAM_ID_MODULE_ENABLE, enable %ld",
                          me_ptr->nlpi_me_ptr->is_enabled);
      }
      break;

      case PARAM_ID_DATA_LOGGING_SELECT_CHANNELS:
      {
         if (params_ptr->max_data_len < sizeof(data_logging_select_channels_t))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             " Get PARAM_ID_DATA_LOGGING_SELECT_CHANNELS, size received %ld, size expected %ld",
                             params_ptr->max_data_len,
                             sizeof(data_logging_select_channels_t));
            return CAPI_EBADPARAM;
         }

         params_ptr->actual_data_len = memscpy(params_ptr->data_ptr,
                                               params_ptr->max_data_len,
                                               &me_ptr->nlpi_me_ptr->selective_ch_logging_cfg,
                                               sizeof(me_ptr->nlpi_me_ptr->selective_ch_logging_cfg));

         break;
      }

      case PARAM_ID_DATA_LOGGING_SELECT_CHANNELS_V2:
      {
         result = capi_data_logging_get_selective_channels_v2_pid(me_ptr, param_id, params_ptr);
         if (CAPI_FAILED(result))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             "Setting Selective Channels V2 Param ID failed!");
            return result;
         }
         break;
      }

      case PARAM_ID_DATA_LOGGING_CONFIG:
      {
         /*check if available size is large enough*/
         if (params_ptr->max_data_len < sizeof(data_logging_config_t))
         {
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             " Get "
                             "PARAM_ID_DATA_LOGGING_DATA_CONFIG failed: "
                             "Insufficient "
                             "container size: %u",
                             params_ptr->max_data_len);
            return (CAPI_EBADPARAM);
         }

         data_logging_config_t *param_payload_ptr = (data_logging_config_t *)params_ptr->data_ptr;
         param_payload_ptr->log_code              = me_ptr->nlpi_me_ptr->log_code;
         param_payload_ptr->log_tap_point_id      = me_ptr->nlpi_me_ptr->log_tap_point_id;
         param_payload_ptr->mode                  = me_ptr->nlpi_me_ptr->logging_mode;

         params_ptr->actual_data_len = sizeof(data_logging_config_t);
      }
      break;
      case FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN:
      {
         if ((params_ptr->actual_data_len >= sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t)))
         {
            fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr =
               (fwk_extn_pcm_param_id_media_fmt_extn_t *)(params_ptr->data_ptr);

            extn_ptr->alignment  = me_ptr->nlpi_me_ptr->extn_params.alignment;
            extn_ptr->bit_width  = me_ptr->nlpi_me_ptr->extn_params.bit_width;
            extn_ptr->endianness = me_ptr->nlpi_me_ptr->extn_params.endianness;
         }
         else
         {
            CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
            DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid,
                             DBG_ERROR_PRIO,
                             " get param failed because of "
                             "length issues, 0x%p, in_len = %d, "
                             "needed_len = %d",
                             param_id,
                             params_ptr->actual_data_len,
                             sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t));
         }
         break;
      }
      default:
      {
         DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_HIGH_PRIO, " Unsupported Param id ::0x%x \n", param_id);
         result = CAPI_EUNSUPPORTED;
      }
      break;
   }
   return result;
}

capi_err_t capi_data_logging_scale_data_to_log(int32_t *in_ptr, int32_t *out_ptr, uint32_t num_samples)
{

#if ((defined __hexagon__) || (defined __qdsp6__))

   for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++)
   {
      out_ptr[sample_index] = Q6_R_asl_RI_sat(in_ptr[sample_index], QFORMAT_SHIFT_FACTOR);
   }
#else

   static const uint32_t MAX_Q27 = ((1 << PCM_Q_FACTOR_27) - 1);
   static const uint32_t MIN_Q27 = (-(1 << PCM_Q_FACTOR_27));

   // will be executed for non QDSP6 platforms
   for (uint32_t sample_index = 0; sample_index < num_samples; sample_index++)
   {
      if (in_ptr[sample_index] >= 0)
      {
         out_ptr[sample_index] = (in_ptr[sample_index] >= MAX_Q27) ? MAX_Q27 : in_ptr[sample_index];
      }
      else
      {
         out_ptr[sample_index] = (in_ptr[sample_index] < MIN_Q27) ? MIN_Q27 : in_ptr[sample_index];
      }
      out_ptr[sample_index] = out_ptr[sample_index] << QFORMAT_SHIFT_FACTOR;
   }

#endif
   return CAPI_EOK;
}

static capi_err_t capi_data_logging_log_data(capi_data_logging_t *me_ptr,
                                             capi_stream_data_t * input[],
                                             capi_stream_data_t * output[])
{

   capi_data_logging_non_island_t *nlpi_me_ptr = me_ptr->nlpi_me_ptr;

   uint32_t in_actual_data_len = input[0]->buf_ptr[0].actual_data_len;
   uint32_t out_max_data_len   = in_actual_data_len; // same as input, in case output buffer is not present.
   uint32_t num_channels       = me_ptr->media_format.format.num_channels;
   if (output && (output[0] != NULL) && (output[0]->buf_ptr[0].data_ptr != NULL))
   {
      out_max_data_len = output[0]->buf_ptr[0].max_data_len;
   }

   int8_t *in_buf_ptr = input[0]->buf_ptr[0].data_ptr;

   int8_t *in_bufs_ptr[CAPI_MAX_CHANNELS_V2] = { NULL };

   const uint32_t num_ch_to_log = nlpi_me_ptr->number_of_channels_to_log;

   const uint32_t bytes_per_sample_per_channel =
      (64 == me_ptr->media_format.format.bits_per_sample)
         ? 8
         : (32 == me_ptr->media_format.format.bits_per_sample)
              ? 4
              : ((24 == me_ptr->media_format.format.bits_per_sample) ? 3 : 2);

   if (0 == num_ch_to_log)
   {
      DATA_LOGGING_MSG(nlpi_me_ptr->iid, DBG_HIGH_PRIO, "All channels logging disabled.");
      return CAPI_EOK;
   }

   memset(in_bufs_ptr, 0, sizeof(int8_t *) * CAPI_MAX_CHANNELS_V2);

   uint32_t capi_buf_bytes_per_channel = 0;

   if((CAPI_RAW_COMPRESSED != me_ptr->media_format.header.format_header.data_format) &&
         ((CAPI_MAX_CHANNELS_V2 < num_channels) || (CAPI_MAX_CHANNELS_V2 < num_ch_to_log))) //added this to resolve kw issues

   {
      DATA_LOGGING_MSG(me_ptr->nlpi_me_ptr->iid, DBG_ERROR_PRIO, "Unsupported number of channels");
      return CAPI_EFAILED;
   }

   if ( check_interleaving(&me_ptr->media_format, CAPI_DEINTERLEAVED_UNPACKED_V2) ||
        check_interleaving(&me_ptr->media_format, CAPI_DEINTERLEAVED_UNPACKED))
   { // separate buffer for each channel
      uint32_t *enabled_ch_mask_ptr = &me_ptr->nlpi_me_ptr->enabled_channel_mask_array[0];
      uint32_t  max_loop_count      = CAPI_CMN_GET_MAX_CHANNEL_GROUPS_NEEDED(num_channels);

      for (uint32_t ch_index_grp_no = 0; ch_index_grp_no < max_loop_count; ch_index_grp_no++)
      {
         uint32_t start_ch_index = ch_index_grp_no * CAPI_CMN_CHANNELS_PER_MASK;
         uint32_t end_ch_index   = (ch_index_grp_no + 1) * CAPI_CMN_CHANNELS_PER_MASK;
         end_ch_index            = MIN(end_ch_index, num_channels);

         for (uint32_t i = start_ch_index; i < end_ch_index; i++)
         {

            uint32_t curr_ch_mask = (1 << CAPI_CMN_MOD_WITH_32(i));
            if (0 == (enabled_ch_mask_ptr[ch_index_grp_no] & curr_ch_mask))
            {
               // skip the channel which is not enabled for logging.
               continue;
            }

            in_bufs_ptr[i] = input[0]->buf_ptr[i].data_ptr;
         }
      }

      capi_buf_bytes_per_channel = MIN(in_actual_data_len, out_max_data_len);
   }
   else if (check_interleaving(&me_ptr->media_format, CAPI_DEINTERLEAVED_PACKED) ||
            check_interleaving(&me_ptr->media_format, CAPI_INTERLEAVED))
   { // signle buffer for all channel
      uint32_t in_actual_bytes_per_channel = (in_actual_data_len / me_ptr->media_format.format.num_channels);
      uint32_t out_max_bytes_per_channel   = (out_max_data_len / me_ptr->media_format.format.num_channels);

      capi_buf_bytes_per_channel = MIN(in_actual_bytes_per_channel, out_max_bytes_per_channel);
   }
   else if (!check_interleaving(&me_ptr->media_format, CAPI_INTERLEAVED) &&
            check_endianess(&me_ptr->media_format, &nlpi_me_ptr->extn_params, PCM_BIG_ENDIAN))
   { // Big Endian logging is supported only if it interleaved, It is logged as bitstream.
      DATA_LOGGING_MSG(nlpi_me_ptr->iid,
                       DBG_ERROR_PRIO,
                       " Warning! logging is disabled for big-endian non-interleaved data.");
      return CAPI_EOK;
   }

   if (check_interleaving(&me_ptr->media_format, CAPI_DEINTERLEAVED_UNPACKED_V2) ||
       check_interleaving(&me_ptr->media_format, CAPI_DEINTERLEAVED_UNPACKED))
   {
      do
      {
         /* Calculate available size in log buffer per channel*/
         uint32_t log_buf_avail_bytes_per_channel =
            nlpi_me_ptr->per_ch_buf_part_size - nlpi_me_ptr->per_channel_log_buf_offset;

         /* Choose minimum of input size and available size */
         uint32_t num_copy_bytes_per_ch = MIN(capi_buf_bytes_per_channel, log_buf_avail_bytes_per_channel);

         int8_t *log_buf_ptr = nlpi_me_ptr->log_buf_ptr + nlpi_me_ptr->per_channel_log_buf_offset;

         for (uint32_t i = 0; i < me_ptr->media_format.format.num_channels; i++)
         {
            if (NULL == in_bufs_ptr[i])
            {
               // channel skipped for logging.
               continue;
            }

            if (nlpi_me_ptr->is_data_scaling_enabled)
            {
               capi_data_logging_scale_data_to_log((int32_t *)in_bufs_ptr[i],
                                                   (int32_t *)log_buf_ptr,
                                                   (num_copy_bytes_per_ch >> 2));
            }
            else
            {
               memscpy(log_buf_ptr, log_buf_avail_bytes_per_channel, in_bufs_ptr[i], num_copy_bytes_per_ch);
            }

            /* update the log buf fill size with copy size */
            nlpi_me_ptr->log_buf_fill_size += num_copy_bytes_per_ch;

            // default mode: logging will be done when buffer is full.
            log_buf_ptr += (LOG_DEFAULT == nlpi_me_ptr->logging_mode)
                              ? nlpi_me_ptr->per_ch_buf_part_size
                              : (num_copy_bytes_per_ch + nlpi_me_ptr->per_channel_log_buf_offset);

            /* Advance input buffer with copy size */
            in_bufs_ptr[i] += num_copy_bytes_per_ch;
         }

         capi_buf_bytes_per_channel -= num_copy_bytes_per_ch;

         nlpi_me_ptr->per_channel_log_buf_offset += num_copy_bytes_per_ch;

         /* Submit to diag if it is filled or if eos is present */
         if ((nlpi_me_ptr->log_buf_fill_size == nlpi_me_ptr->log_buf_size) || (input[0]->flags.marker_eos) ||
             LOG_IMMEDIATE == nlpi_me_ptr->logging_mode)
         {
            // If EOS comes in buffering mode then repacking may be required.
            bool_t is_full_frame =
               (LOG_DEFAULT == nlpi_me_ptr->logging_mode && nlpi_me_ptr->log_buf_fill_size != nlpi_me_ptr->log_buf_size)
                  ? FALSE
                  : TRUE;
            data_logging(me_ptr, nlpi_me_ptr->log_buf_ptr, is_full_frame);
         }

#ifdef DATA_LOGGING_DBG
         DATA_LOGGING_MSG(nlpi_me_ptr->iid,
                          DBG_HIGH_PRIO,
                          " Deinterleaved unpacked data, "
                          "bytes copied per channel: %d",
                          num_copy_bytes_per_ch);
#endif
      } while (capi_buf_bytes_per_channel);
   }
   else if ((check_interleaving(&me_ptr->media_format, CAPI_INTERLEAVED) &&
             (num_ch_to_log == me_ptr->media_format.format.num_channels)) ||
            (CAPI_RAW_COMPRESSED == me_ptr->media_format.header.format_header.data_format))
   {
      uint32_t capi_buf_avail_bytes = MIN(in_actual_data_len, out_max_data_len);

      do
      {
         /* Calculate available size in log buffer*/
         uint32_t log_buf_avail_bytes = nlpi_me_ptr->log_buf_size - nlpi_me_ptr->log_buf_fill_size;

         /* Choose minimum of input size and available size */
         uint32_t num_copy_bytes = MIN(log_buf_avail_bytes, capi_buf_avail_bytes);

         int8_t *log_buf_ptr      = nlpi_me_ptr->log_buf_ptr + nlpi_me_ptr->log_buf_fill_size;
         int8_t *log_capi_buf_ptr = NULL;

         if (nlpi_me_ptr->is_data_scaling_enabled)
         {
            capi_data_logging_scale_data_to_log((int32_t *)in_buf_ptr, (int32_t *)log_buf_ptr, (num_copy_bytes >> 2));
         }
         else if (0 == nlpi_me_ptr->log_buf_fill_size &&
                  ((LOG_IMMEDIATE == nlpi_me_ptr->logging_mode) || (num_copy_bytes == nlpi_me_ptr->log_buf_size)))
         {
            // if log buffer is empty and it can be filled with the capi buffer then log the capi buffer directly
            // without copying data to log buffer.
            log_capi_buf_ptr = in_buf_ptr;
         }
         else
         {
            memscpy(log_buf_ptr, log_buf_avail_bytes, in_buf_ptr, num_copy_bytes);
         }

         /* update the log buf fill size with copy size */
         nlpi_me_ptr->log_buf_fill_size += num_copy_bytes;

         /* Advance input buffer with copy size */
         in_buf_ptr += num_copy_bytes;

         capi_buf_avail_bytes -= num_copy_bytes;

         /* Submit to diag if it is filled or if eos is present */
         if ((nlpi_me_ptr->log_buf_fill_size == nlpi_me_ptr->log_buf_size) || (input[0]->flags.marker_eos) ||
             LOG_IMMEDIATE == nlpi_me_ptr->logging_mode)
         {
            data_logging(me_ptr, ((log_capi_buf_ptr) ? log_capi_buf_ptr : nlpi_me_ptr->log_buf_ptr), TRUE);
         }

#ifdef DATA_LOGGING_DBG
         DATA_LOGGING_MSG(nlpi_me_ptr->iid,
                          DBG_HIGH_PRIO,
                          " interleaved or raw compressed data, "
                          "bytes copied: %d",
                          num_copy_bytes);
#endif
      } while (capi_buf_avail_bytes);
   }
   else if (check_interleaving(&me_ptr->media_format, CAPI_INTERLEAVED))
   { // interleaved data with channels to skip
      do
      {
         uint32_t bytes_per_sample_in_log_buf = bytes_per_sample_per_channel * num_ch_to_log;
         uint32_t bytes_per_sample_in_capi_buf =
            bytes_per_sample_per_channel * me_ptr->media_format.format.num_channels;

         uint32_t sample_to_copy_in_log_buf =
            (nlpi_me_ptr->log_buf_size - nlpi_me_ptr->log_buf_fill_size) / bytes_per_sample_in_log_buf;
         uint32_t sample_to_copy_from_capi_buf = capi_buf_bytes_per_channel / bytes_per_sample_per_channel;

         uint32_t sample_to_copy = MIN(sample_to_copy_in_log_buf, sample_to_copy_from_capi_buf);

         uint32_t *enabled_ch_mask_ptr = &me_ptr->nlpi_me_ptr->enabled_channel_mask_array[0];

         for (int i = 0, j = 0; i < num_channels; i++)
         {
            uint32_t curr_ch_mask    = (1 << CAPI_CMN_MOD_WITH_32(i));
            uint32_t ch_index_grp_no = CAPI_CMN_DIVIDE_WITH_32(i);
            if (0 == (enabled_ch_mask_ptr[ch_index_grp_no] & curr_ch_mask))
            {
               // skip the channel which is not enabled for logging.
               continue;
            }

            if (16 == me_ptr->media_format.format.bits_per_sample)
            {
               int16_t *src_buf_ptr = (int16_t *)(in_buf_ptr + (2 * i));
               int16_t *dst_buf_ptr = (int16_t *)(nlpi_me_ptr->log_buf_ptr + nlpi_me_ptr->log_buf_fill_size + (2 * j));

               for (int k = 0; k < sample_to_copy; k++)
               {
                  dst_buf_ptr[k * num_ch_to_log] = src_buf_ptr[k * me_ptr->media_format.format.num_channels];
               }
            }
            else if (32 == me_ptr->media_format.format.bits_per_sample)
            {
               int32_t *src_buf_ptr = (int32_t *)(in_buf_ptr + (4 * i));
               int32_t *dst_buf_ptr = (int32_t *)(nlpi_me_ptr->log_buf_ptr + nlpi_me_ptr->log_buf_fill_size + (4 * j));

               for (int k = 0; k < sample_to_copy; k++)
               {
                  dst_buf_ptr[k * num_ch_to_log] = src_buf_ptr[k * me_ptr->media_format.format.num_channels];
               }
            }
            else if (64 == me_ptr->media_format.format.bits_per_sample)
            {

               int64_t *src_buf_ptr = (int64_t *)(in_buf_ptr + (8 * i));
               int64_t *dst_buf_ptr = (int64_t *)(nlpi_me_ptr->log_buf_ptr + nlpi_me_ptr->log_buf_fill_size + (8 * j));

               for (int k = 0; k < sample_to_copy; k++)
               {
                  dst_buf_ptr[k * num_ch_to_log] = src_buf_ptr[k * me_ptr->media_format.format.num_channels];
               }
            }
            else if (24 == me_ptr->media_format.format.bits_per_sample)
            {
               int8_t *src_buf_ptr_1 = (int8_t *)(in_buf_ptr + (3 * i));
               int8_t *dst_buf_ptr_1 = (int8_t *)(nlpi_me_ptr->log_buf_ptr + nlpi_me_ptr->log_buf_fill_size + (3 * j));
               int8_t *src_buf_ptr_2 = (int8_t *)(in_buf_ptr + (3 * i) + 1);
               int8_t *dst_buf_ptr_2 =
                  (int8_t *)(nlpi_me_ptr->log_buf_ptr + nlpi_me_ptr->log_buf_fill_size + (3 * j) + 1);
               int8_t *src_buf_ptr_3 = (int8_t *)(in_buf_ptr + (3 * i) + 2);
               int8_t *dst_buf_ptr_3 =
                  (int8_t *)(nlpi_me_ptr->log_buf_ptr + nlpi_me_ptr->log_buf_fill_size + (3 * j) + 2);

               for (int k = 0; k < sample_to_copy; k++)
               {
                  dst_buf_ptr_1[k * num_ch_to_log] = src_buf_ptr_1[k * me_ptr->media_format.format.num_channels];
                  dst_buf_ptr_2[k * num_ch_to_log] = src_buf_ptr_2[k * me_ptr->media_format.format.num_channels];
                  dst_buf_ptr_3[k * num_ch_to_log] = src_buf_ptr_3[k * me_ptr->media_format.format.num_channels];
               }
            }
            else
            {
               DATA_LOGGING_MSG(nlpi_me_ptr->iid, DBG_ERROR_PRIO, "unexpected error !!!!!");
               return CAPI_EFAILED;
            }

            j++;
         }

         if (nlpi_me_ptr->is_data_scaling_enabled)
         {
            capi_data_logging_scale_data_to_log((int32_t *)(nlpi_me_ptr->log_buf_ptr + nlpi_me_ptr->log_buf_fill_size),
                                                (int32_t *)(nlpi_me_ptr->log_buf_ptr + nlpi_me_ptr->log_buf_fill_size),
                                                (sample_to_copy * num_ch_to_log));
         }

         // update the capi input buf ptr
         in_buf_ptr += (sample_to_copy * bytes_per_sample_in_capi_buf);

         // Update log buf fill size
         nlpi_me_ptr->log_buf_fill_size += (sample_to_copy * bytes_per_sample_in_log_buf);

         // update the remaining bytes per channel in capi buffer.
         capi_buf_bytes_per_channel -= (sample_to_copy * bytes_per_sample_per_channel);

         /* Submit to diag if it is filled or if eos is present */
         if ((nlpi_me_ptr->log_buf_fill_size == nlpi_me_ptr->log_buf_size) || (input[0]->flags.marker_eos) ||
             LOG_IMMEDIATE == nlpi_me_ptr->logging_mode)
         {
            data_logging(me_ptr, nlpi_me_ptr->log_buf_ptr, TRUE);
         }

#ifdef DATA_LOGGING_DBG
         DATA_LOGGING_MSG(nlpi_me_ptr->iid,
                          DBG_HIGH_PRIO,
                          " interleaved data, "
                          "bytes copied per channel: %d",
                          (sample_to_copy * bytes_per_sample_per_channel));
#endif
      } while (capi_buf_bytes_per_channel);
   }
   else if (check_interleaving(&me_ptr->media_format, CAPI_DEINTERLEAVED_PACKED))
   {
      do
      {
         /* Calculate available size in log buffer*/
         uint32_t log_buf_avail_bytes_per_channel =
            (nlpi_me_ptr->per_ch_buf_part_size - nlpi_me_ptr->per_channel_log_buf_offset);

         uint32_t capi_buf_channel_offset =
            ((input[0]->buf_ptr[0].actual_data_len) / me_ptr->media_format.format.num_channels);

         /* Choose minimum of input size and available size */
         uint32_t num_copy_bytes_per_ch = MIN(log_buf_avail_bytes_per_channel, capi_buf_bytes_per_channel);

         int8_t *log_buf_ptr = nlpi_me_ptr->log_buf_ptr + nlpi_me_ptr->per_channel_log_buf_offset;

         uint32_t *enabled_ch_mask_ptr = &me_ptr->nlpi_me_ptr->enabled_channel_mask_array[0];

         for (uint32_t i = 0; i < me_ptr->media_format.format.num_channels; i++)
         {
            uint32_t curr_ch_mask    = (1 << CAPI_CMN_MOD_WITH_32(i));
            uint32_t ch_index_grp_no = CAPI_CMN_DIVIDE_WITH_32(i);
            if (0 == (enabled_ch_mask_ptr[ch_index_grp_no] & curr_ch_mask))
            {
               // skip the channel which is not enabled for logging.
               continue;
            }

            int8_t *capi_in_buf_ptr = &in_buf_ptr[i * capi_buf_channel_offset];

            if (nlpi_me_ptr->is_data_scaling_enabled)
            {
               capi_data_logging_scale_data_to_log((int32_t *)capi_in_buf_ptr,
                                                   (int32_t *)log_buf_ptr,
                                                   (num_copy_bytes_per_ch >> 2));
            }
            else
            {
               memscpy(log_buf_ptr, log_buf_avail_bytes_per_channel, capi_in_buf_ptr, num_copy_bytes_per_ch);
            }

            /* update the log buf fill size with copy size */
            nlpi_me_ptr->log_buf_fill_size += num_copy_bytes_per_ch;

            // increment the log-buf-ptr for next channel.
            log_buf_ptr += (LOG_DEFAULT == nlpi_me_ptr->logging_mode)
                              ? nlpi_me_ptr->per_ch_buf_part_size
                              : (num_copy_bytes_per_ch + nlpi_me_ptr->per_channel_log_buf_offset);
         }

         capi_buf_bytes_per_channel -= num_copy_bytes_per_ch;

         /* Advance input buffer with copy size per channel*/
         in_buf_ptr += num_copy_bytes_per_ch;

         /* Advance log buffer with copy size per channel*/
         nlpi_me_ptr->per_channel_log_buf_offset += num_copy_bytes_per_ch;

         /* Submit to diag if it is filled or if eos is present */
         if ((nlpi_me_ptr->log_buf_fill_size == nlpi_me_ptr->log_buf_size) || (input[0]->flags.marker_eos) ||
             LOG_IMMEDIATE == nlpi_me_ptr->logging_mode)
         {
            // If EOS comes in buffering mode then repacking may be required.
            bool_t is_full_frame =
               (LOG_DEFAULT == nlpi_me_ptr->logging_mode && nlpi_me_ptr->log_buf_fill_size != nlpi_me_ptr->log_buf_size)
                  ? FALSE
                  : TRUE;

            data_logging(me_ptr, nlpi_me_ptr->log_buf_ptr, is_full_frame);
         }

#ifdef DATA_LOGGING_DBG
         DATA_LOGGING_MSG(nlpi_me_ptr->iid,
                          DBG_HIGH_PRIO,
                          " Deinterleaved packed data, "
                          "bytes copied per channel: %d",
                          num_copy_bytes_per_ch);
#endif
      } while (capi_buf_bytes_per_channel);
   }

   return CAPI_EOK;
}

capi_err_t capi_data_logging_process_nlpi(capi_data_logging_t *me_ptr,
                                          capi_stream_data_t * input[],
                                          capi_stream_data_t * output[])
{
   capi_err_t result = CAPI_EOK;

#ifdef LOG_SAFE_MODE
   if ((NULL == capi_ptr) || (NULL == input) || (NULL == input[0]) || (NULL == input[0]->buf_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI Data Logging: received bad input pointers");
      return (CAPI_EFAILED);
   }
#endif

#ifdef DATA_LOGGING_DBG
   if ((check_interleaving(&me_ptr->media_format, CAPI_DEINTERLEAVED_UNPACKED_V2) ||
        check_interleaving(&me_ptr->media_format, CAPI_DEINTERLEAVED_UNPACKED)) &&
       (input[0]->bufs_num != me_ptr->media_format.format.num_channels))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CAPI Data Logging: number for buffer pointers not equal to "
             "number of channels for "
             "deinterleaved unpacked data");
      return CAPI_EFAILED;
   }

   AR_MSG(DBG_LOW_PRIO,
          "CAPI Data Logging: process counter %ld, log status %ld",
          me_ptr->nlpi_me_ptr->counter,
          me_ptr->nlpi_me_ptr->log_code_status);
#endif

   // increment log id for every EoS. Only increment after logging the current
   // input data.
   bool_t inc_log_id = input[0]->flags.marker_eos;

   if (me_ptr->nlpi_me_ptr->log_code_status || (me_ptr->nlpi_me_ptr->counter == 0))
   {
#ifdef DATA_LOGGING_DBG
      AR_MSG(DBG_LOW_PRIO, "CAPI Data Logging: checking log status");
#endif

      me_ptr->nlpi_me_ptr->log_code_status = posal_data_log_code_status(me_ptr->nlpi_me_ptr->log_code);
   }

   me_ptr->nlpi_me_ptr->counter =
      (me_ptr->nlpi_me_ptr->counter >= LOG_STATUS_QUERY_PERIOD) ? 0 : (me_ptr->nlpi_me_ptr->counter + 1);

   if (me_ptr->nlpi_me_ptr->log_code_status)
   {
      capi_data_logging_log_data(me_ptr, input, output);
   }
#ifdef DATA_LOGGING_DBG
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI Data Logging: log code 0x%lx not enabled", me_ptr->nlpi_me_ptr->log_code);
   }
#endif

   if (inc_log_id)
   {
      incr_log_id(me_ptr);
   }

   return result;
}

/*------------------------------------------------------------------------
  Function name: capi_audio_logging_end
  Returns the library to the uninitialized state and frees the memory that
  was allocated by Init(). This function also frees the virtual function
  table.
 * -----------------------------------------------------------------------*/
capi_err_t capi_data_logging_end(capi_t *capi_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == capi_ptr)
   {
      DATA_LOGGING_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, " Reinit received bad pointer, %p", capi_ptr);
      return (CAPI_EFAILED);
   }
   capi_data_logging_t *me_ptr = (capi_data_logging_t *)((capi_ptr));

   if (NULL != me_ptr->nlpi_me_ptr)
   {
      if (NULL != me_ptr->nlpi_me_ptr->log_buf_ptr)
      {
         posal_memory_aligned_free(me_ptr->nlpi_me_ptr->log_buf_ptr);
         me_ptr->nlpi_me_ptr->log_buf_ptr = NULL;
      }
      if (NULL != me_ptr->nlpi_me_ptr->channel_logging_cfg.cache_channel_logging_cfg_ptr)
      {
         posal_memory_aligned_free(me_ptr->nlpi_me_ptr->channel_logging_cfg.cache_channel_logging_cfg_ptr);
         me_ptr->nlpi_me_ptr->channel_logging_cfg.cache_channel_logging_cfg_ptr  = NULL;
         me_ptr->nlpi_me_ptr->channel_logging_cfg.cache_channel_logging_cfg_size = 0;
      }
      posal_memory_free(me_ptr->nlpi_me_ptr);
      me_ptr->nlpi_me_ptr = NULL;
   }

   me_ptr->vtbl = NULL;
   return result;
}