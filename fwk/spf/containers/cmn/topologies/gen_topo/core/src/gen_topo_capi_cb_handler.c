/**
 * \file gen_topo_capi_cb_handler.c
 * \brief
 *     This file contains functions for GEN_CNTR
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"
#include "gen_topo_capi.h"

/** GPR/API related */
#include "gpr_api_inline.h"
#include "gpr_ids_domains.h"
#include "thin_topo_inline.h"

/** port field is invalid if its zero. */
#define COMPARE_FMT_AND_SET(                                                                                           \
   capi_med_fmt_ptr, field1, port_fmt_ptr, field2, is_mf_valid_and_changed_ptr, num_invalid_values)                    \
   if (IS_VALID_CAPI_VALUE(capi_med_fmt_ptr->field1))                                                                  \
   {                                                                                                                   \
      if (port_fmt_ptr->pcm.field2 != capi_med_fmt_ptr->field1)                                                        \
      {                                                                                                                \
         *is_mf_valid_and_changed_ptr = TRUE;                                                                          \
         port_fmt_ptr->pcm.field2     = capi_med_fmt_ptr->field1;                                                      \
      }                                                                                                                \
   }                                                                                                                   \
   else                                                                                                                \
   {                                                                                                                   \
      if (0 == port_fmt_ptr->pcm.field2)                                                                               \
      {                                                                                                                \
         num_invalid_values++;                                                                                         \
      }                                                                                                                \
   }

static capi_err_t gen_topo_handle_pcm_packetized_out_mf_event(capi_buf_t *       payload,
                                                              topo_media_fmt_t * port_fmt_ptr,
                                                              capi_event_info_t *event_info_ptr,
                                                              bool_t *           is_mf_valid_and_changed_ptr)
{
   uint32_t num_invalid_values = 0;
   if ((NULL == payload) || (NULL == port_fmt_ptr) || (NULL == is_mf_valid_and_changed_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "received null pointer(s).");
      return AR_EBADPARAM;
   }
   capi_set_get_media_format_t *data_ptr = (capi_set_get_media_format_t *)(payload->data_ptr);

   if (payload->actual_data_len < (sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_t)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Payload size %lu is lesser than the min required size %lu",
             payload->actual_data_len,
             sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_t));
      return CAPI_ENEEDMORE;
   }

   if (!event_info_ptr->port_info.is_valid || event_info_ptr->port_info.is_input_port)
   {
      AR_MSG(DBG_ERROR_PRIO, "Port is not valid or is an input port");
      return CAPI_EBADPARAM;
   }

   capi_standard_data_format_t *capi_med_fmt_ptr =
      (capi_standard_data_format_t *)(payload->data_ptr + sizeof(capi_set_get_media_format_t));

   COMPARE_FMT_AND_SET(capi_med_fmt_ptr,
                       bits_per_sample,
                       port_fmt_ptr,
                       bits_per_sample,
                       is_mf_valid_and_changed_ptr,
                       num_invalid_values);

   // q factor check not applicable for floating pt
   if (CAPI_FLOATING_POINT != data_ptr->format_header.data_format)
   {
      if (IS_VALID_CAPI_VALUE(capi_med_fmt_ptr->q_factor))
      {
         if (port_fmt_ptr->pcm.q_factor != capi_med_fmt_ptr->q_factor)
         {
            port_fmt_ptr->pcm.q_factor   = capi_med_fmt_ptr->q_factor;
            port_fmt_ptr->pcm.bit_width  = TOPO_QFORMAT_TO_BIT_WIDTH(port_fmt_ptr->pcm.q_factor);
            *is_mf_valid_and_changed_ptr = TRUE;
         }
      }
      else
      {
         if (0 == port_fmt_ptr->pcm.q_factor)
         {
            num_invalid_values++;
         }
      }
   }
   if (IS_VALID_CAPI_VALUE(capi_med_fmt_ptr->num_channels) && (capi_med_fmt_ptr->num_channels <= CAPI_MAX_CHANNELS))
   {
      if (port_fmt_ptr->pcm.num_channels != capi_med_fmt_ptr->num_channels)
      {
         port_fmt_ptr->pcm.num_channels = capi_med_fmt_ptr->num_channels;
         *is_mf_valid_and_changed_ptr   = TRUE;
      }

      for (uint32_t i = 0; ((i < SIZE_OF_ARRAY(port_fmt_ptr->pcm.chan_map)) && (i < capi_med_fmt_ptr->num_channels));
           i++)
      {
         if (port_fmt_ptr->pcm.chan_map[i] != capi_med_fmt_ptr->channel_type[i])
         {
            port_fmt_ptr->pcm.chan_map[i] = capi_med_fmt_ptr->channel_type[i];
            *is_mf_valid_and_changed_ptr  = TRUE;
         }
      }
   }
   else
   {
      if (0 == port_fmt_ptr->pcm.num_channels)
      {
         num_invalid_values++;
      }
   }

   COMPARE_FMT_AND_SET(capi_med_fmt_ptr,
                       sampling_rate,
                       port_fmt_ptr,
                       sample_rate,
                       is_mf_valid_and_changed_ptr,
                       num_invalid_values);

   if (CAPI_INVALID_INTERLEAVING != capi_med_fmt_ptr->data_interleaving)
   {
      uint16_t prev_interleaved = port_fmt_ptr->pcm.interleaving;
      port_fmt_ptr->pcm.interleaving =
         gen_topo_convert_capi_interleaving_to_gen_topo_interleaving(capi_med_fmt_ptr->data_interleaving);

      if (prev_interleaved != port_fmt_ptr->pcm.interleaving)
      {
         *is_mf_valid_and_changed_ptr = TRUE;
      }
   }
   else
   {
      if (TOPO_INTERLEAVING_UNKNOWN == port_fmt_ptr->pcm.interleaving)
      {
         num_invalid_values++;
      }
   }

   port_fmt_ptr->pcm.endianness = TOPO_LITTLE_ENDIAN;

   // many CAPIs don't initialize this. hence assume by default if the data format is FIXED_POINT
   if ((CAPI_FIXED_POINT == data_ptr->format_header.data_format) ||
       (CAPI_FLOATING_POINT == data_ptr->format_header.data_format))
   {
      if ((0 == capi_med_fmt_ptr->bitstream_format) ||
          (CAPI_DATA_FORMAT_INVALID_VAL == capi_med_fmt_ptr->bitstream_format))
      {
         capi_med_fmt_ptr->bitstream_format = MEDIA_FMT_ID_PCM;
      }
   }

   if (port_fmt_ptr->fmt_id != capi_med_fmt_ptr->bitstream_format)
   {
      *is_mf_valid_and_changed_ptr = TRUE;
      port_fmt_ptr->fmt_id         = capi_med_fmt_ptr->bitstream_format;
   }

   // if at least one value is invalid, then until that value becomes valid, don't propagate media fmt.
   if (num_invalid_values)
   {
      *is_mf_valid_and_changed_ptr = FALSE;
   }

   return AR_EOK;
}

static capi_err_t gen_topo_handle_pcm_packetized_v2_out_mf_event(capi_buf_t *       payload,
                                                                 topo_media_fmt_t * port_fmt_ptr,
                                                                 capi_event_info_t *event_info_ptr,
                                                                 bool_t *           is_mf_valid_and_changed_ptr)
{
   uint32_t num_invalid_values = 0;

   if ((NULL == payload) || (NULL == port_fmt_ptr) || (NULL == is_mf_valid_and_changed_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "received null pointer(s).");
      return AR_EBADPARAM;
   }
   capi_set_get_media_format_t *data_ptr = (capi_set_get_media_format_t *)(payload->data_ptr);

   uint32_t min_req_size = sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t);
   if (payload->actual_data_len < min_req_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Payload size %lu is lesser than the min required size %lu",
             payload->actual_data_len,
             min_req_size);
      return CAPI_ENEEDMORE;
   }

   capi_standard_data_format_v2_t *capi_med_fmt_ptr =
      (capi_standard_data_format_v2_t *)(payload->data_ptr + sizeof(capi_set_get_media_format_t));

   // another size validation
   if (IS_VALID_CAPI_VALUE(capi_med_fmt_ptr->num_channels) && (capi_med_fmt_ptr->num_channels <= CAPI_MAX_CHANNELS))
   {
      if (payload->actual_data_len < (min_req_size + (capi_med_fmt_ptr->num_channels * sizeof(capi_channel_type_t))))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Payload size %lu is lesser than the min required size %lu",
                payload->actual_data_len,
                min_req_size + (capi_med_fmt_ptr->num_channels * sizeof(capi_channel_type_t)));
         return CAPI_ENEEDMORE;
      }
   }

   if (!event_info_ptr->port_info.is_valid || event_info_ptr->port_info.is_input_port)
   {
      AR_MSG(DBG_ERROR_PRIO, "Port is not valid or is an input port");
      return CAPI_EBADPARAM;
   }

   COMPARE_FMT_AND_SET(capi_med_fmt_ptr,
                       bits_per_sample,
                       port_fmt_ptr,
                       bits_per_sample,
                       is_mf_valid_and_changed_ptr,
                       num_invalid_values);

   // q factor check not applicable for floating pt
   if (CAPI_FLOATING_POINT != data_ptr->format_header.data_format)
   {
      if (IS_VALID_CAPI_VALUE(capi_med_fmt_ptr->q_factor))
      {
         if (port_fmt_ptr->pcm.q_factor != capi_med_fmt_ptr->q_factor)
         {
            port_fmt_ptr->pcm.q_factor   = capi_med_fmt_ptr->q_factor;
            port_fmt_ptr->pcm.bit_width  = TOPO_QFORMAT_TO_BIT_WIDTH(port_fmt_ptr->pcm.q_factor);
            *is_mf_valid_and_changed_ptr = TRUE;
         }
      }
      else
      {
         if (0 == port_fmt_ptr->pcm.q_factor)
         {
            num_invalid_values++;
         }
      }
   }
   if (IS_VALID_CAPI_VALUE(capi_med_fmt_ptr->num_channels))
   {
      if (port_fmt_ptr->pcm.num_channels != capi_med_fmt_ptr->num_channels)
      {
         port_fmt_ptr->pcm.num_channels = capi_med_fmt_ptr->num_channels;
         *is_mf_valid_and_changed_ptr   = TRUE;
      }

      for (uint32_t i = 0; ((i < SIZE_OF_ARRAY(port_fmt_ptr->pcm.chan_map)) && (i < capi_med_fmt_ptr->num_channels));
           i++)
      {
         if (port_fmt_ptr->pcm.chan_map[i] != capi_med_fmt_ptr->channel_type[i])
         {
            port_fmt_ptr->pcm.chan_map[i] = capi_med_fmt_ptr->channel_type[i];
            *is_mf_valid_and_changed_ptr  = TRUE;
         }
      }
   }
   else
   {
      if (0 == port_fmt_ptr->pcm.num_channels)
      {
         num_invalid_values++;
      }
   }

   COMPARE_FMT_AND_SET(capi_med_fmt_ptr,
                       sampling_rate,
                       port_fmt_ptr,
                       sample_rate,
                       is_mf_valid_and_changed_ptr,
                       num_invalid_values);

   if (CAPI_INVALID_INTERLEAVING != capi_med_fmt_ptr->data_interleaving)
   {
      uint16_t prev_interleaved = port_fmt_ptr->pcm.interleaving;
      port_fmt_ptr->pcm.interleaving =
         gen_topo_convert_capi_interleaving_to_gen_topo_interleaving(capi_med_fmt_ptr->data_interleaving);

      if (prev_interleaved != port_fmt_ptr->pcm.interleaving)
      {
         *is_mf_valid_and_changed_ptr = TRUE;
      }
   }
   else
   {
      if (TOPO_INTERLEAVING_UNKNOWN == port_fmt_ptr->pcm.interleaving)
      {
         num_invalid_values++;
      }
   }

   port_fmt_ptr->pcm.endianness = TOPO_LITTLE_ENDIAN;

   // many CAPIs don't initialize this. hence assume by default if the data format is FIXED_POINT
   if ((CAPI_FIXED_POINT == data_ptr->format_header.data_format) ||
       (CAPI_FLOATING_POINT == data_ptr->format_header.data_format))
   {
      if ((0 == capi_med_fmt_ptr->bitstream_format) ||
          (CAPI_DATA_FORMAT_INVALID_VAL == capi_med_fmt_ptr->bitstream_format))
      {
         capi_med_fmt_ptr->bitstream_format = MEDIA_FMT_ID_PCM;
      }
   }

   if (port_fmt_ptr->fmt_id != capi_med_fmt_ptr->bitstream_format)
   {
      *is_mf_valid_and_changed_ptr = TRUE;
      port_fmt_ptr->fmt_id         = capi_med_fmt_ptr->bitstream_format;
   }

   // if at least one value is invalid, then until that value becomes valid, don't propagate media fmt.
   if (num_invalid_values)
   {
      *is_mf_valid_and_changed_ptr = FALSE;
   }

   return AR_EOK;
}

capi_err_t gen_topo_handle_output_media_format_event(void *             ctxt_ptr,
                                                     void *             module_ctxt_ptr,
                                                     capi_event_info_t *event_info_ptr,
                                                     bool_t             is_std_fmt_v2,
                                                     bool_t             is_pending_data_valid)
{
   capi_err_t                   result     = CAPI_EOK;
   gen_topo_t *                 topo_ptr   = (gen_topo_t *)ctxt_ptr;
   gen_topo_module_t *          module_ptr = (gen_topo_module_t *)module_ctxt_ptr;
   capi_buf_t *                 payload    = &event_info_ptr->payload;
   capi_set_get_media_format_t *data_ptr   = (capi_set_get_media_format_t *)(payload->data_ptr);
   uint32_t                     port_ind   = event_info_ptr->port_info.port_index;
   gen_topo_output_port_t *     out_port_ptr =
      (gen_topo_output_port_t *)gu_find_output_port_by_index(&module_ptr->gu, port_ind);

   // TRUE if mf changed and all fields are valid values.
   bool_t is_mf_valid_and_changed = FALSE;

   if (NULL == out_port_ptr)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Warning: Module 0x%lX doesnt have output port, but module raised output media format",
               module_ptr->gu.module_instance_id);
      return result;
   }

   bool_t            mf_event_pending = out_port_ptr->common.flags.media_fmt_event;
   topo_media_fmt_t  tmp_mf           = { 0 };
   topo_media_fmt_t *port_fmt_ptr     = &tmp_mf;

   // can't update the media_fmt_ptr directly, cache it in temp_mf and use.
   if (module_ptr->bypass_ptr)
   {
      tu_copy_media_fmt(port_fmt_ptr, module_ptr->bypass_ptr->media_fmt_ptr);
   }
   else
   {
      tu_copy_media_fmt(port_fmt_ptr, out_port_ptr->common.media_fmt_ptr);
   }

   /**
    * if a media format event is raised while there's pending data
    * return error to CAPI.
    * Pending data is always in the old media fmt. if new media fmt is raised, then ambiguity occurs.
    * in GEN_CNTR if there's pending data we don't call process on the module. hence it's not possible to
    * change media fmt due to process, however, for set-cfg or set-prop can cause media fmt event.
    * For now just print error and return.
    *
    * Another thing is if previous media fmt event is not handled yet, & a new media fmt is raised while data length is
    * zero then also it's an error. However, if there's no data, then it's ok to raise subsquent media fmts even if we
    * had not handled yet.
    */
   uint32_t old_bytes = topo_ptr->proc_context.process_info.is_in_mod_proc_context
                           ? topo_ptr->proc_context.out_port_scratch_ptr[port_ind].prev_actual_data_len[0]
                           : out_port_ptr->common.bufs_ptr && out_port_ptr->common.bufs_ptr[0].actual_data_len;
   if (!is_pending_data_valid && (0 != old_bytes))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX: Media format changed while there's pending data. Data pending %lu, pending media fmt "
               "event %u, process context %u",
               module_ptr->gu.module_instance_id,
               old_bytes,
               out_port_ptr->common.flags.media_fmt_event,
               topo_ptr->proc_context.process_info.is_in_mod_proc_context);
      return CAPI_EFAILED;
   }

   uint32_t new_output_fmt_id = CAPI_DATA_FORMAT_INVALID_VAL;

   if ((CAPI_MAX_FORMAT_TYPE == data_ptr->format_header.data_format) ||
       (CAPI_DATA_FORMAT_INVALID_VAL == (uint32_t)data_ptr->format_header.data_format))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Module 0x%lX: Port 0x%lx: module output media format: invalid data format 0x%lx. "
               "propagating? %u,",
               module_ptr->gu.module_instance_id,
               out_port_ptr->gu.cmn.id,
               data_ptr->format_header.data_format,
               out_port_ptr->common.flags.media_fmt_event);
      return CAPI_EFAILED;
   }

   spf_data_format_t gen_cntrdf =
      gen_topo_convert_capi_data_format_to_spf_data_format(data_ptr->format_header.data_format);

   if (gen_cntrdf != port_fmt_ptr->data_format)
   {
      is_mf_valid_and_changed   = TRUE;
      port_fmt_ptr->data_format = gen_cntrdf;

      if (!SPF_IS_PCM_DATA_FORMAT(gen_cntrdf))
      {
         topo_ptr->flags.simple_threshold_propagation_enabled = FALSE;
      }
   }

   switch (data_ptr->format_header.data_format)
   {
      case CAPI_FIXED_POINT:
      case CAPI_FLOATING_POINT:
      case CAPI_IEC61937_PACKETIZED:
      case CAPI_IEC60958_PACKETIZED:
      case CAPI_IEC60958_PACKETIZED_NON_LINEAR:
      case CAPI_DSD_DOP_PACKETIZED:
      case CAPI_GENERIC_COMPRESSED:
      case CAPI_COMPR_OVER_PCM_PACKETIZED:
      {
         if (is_std_fmt_v2)
         {
            if (AR_EOK != (result = gen_topo_handle_pcm_packetized_v2_out_mf_event(payload,
                                                                                   port_fmt_ptr,
                                                                                   event_info_ptr,
                                                                                   &is_mf_valid_and_changed)))
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Module 0x%lX: Error in output MF V2 handling function",
                        module_ptr->gu.module_instance_id);
               return result;
            }
         }
         else
         {
            if (AR_EOK != (result = gen_topo_handle_pcm_packetized_out_mf_event(payload,
                                                                                port_fmt_ptr,
                                                                                event_info_ptr,
                                                                                &is_mf_valid_and_changed)))
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Module 0x%lX: Error in output MF V1 handling function",
                        module_ptr->gu.module_instance_id);
               return result;
            }
         }

         // update the change status only if the media format has changed, otherwise, 2 times event can reset
         // media_fmt_event flag.
         if (is_mf_valid_and_changed)
         {
            out_port_ptr->common.flags.media_fmt_event = is_mf_valid_and_changed;
         }

         // is_mf_valid_and_changed must be TRUE, if not no point checking other fields.
         if (module_ptr->flags.need_pcm_extn && is_mf_valid_and_changed)
         {
            gen_topo_capi_get_fwk_ext_media_fmt(topo_ptr,
                                                module_ptr,
                                                FALSE /*is_input*/,
                                                out_port_ptr->gu.cmn.index,
                                                out_port_ptr,
                                                port_fmt_ptr);
         }

         break;
      }
      case CAPI_RAW_COMPRESSED:
      {
         if (payload->actual_data_len <
             (sizeof(capi_set_get_media_format_t) + sizeof(capi_raw_compressed_data_format_t)))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in media fmt function. The actual size %lu is less than the required size "
                     "for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     (uint32_t)(data_ptr->format_header.data_format));
            return CAPI_ENEEDMORE;
         }

         if (!event_info_ptr->port_info.is_valid || event_info_ptr->port_info.is_input_port)
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in media fmt function. port is not valid or is an input port.",
                     module_ptr->gu.module_instance_id);
            return CAPI_EBADPARAM;
         }

         capi_raw_compressed_data_format_t *pRawData =
            (capi_raw_compressed_data_format_t *)(payload->data_ptr + sizeof(capi_set_get_media_format_t));

         if (IS_VALID_CAPI_VALUE(pRawData->bitstream_format))
         {
            new_output_fmt_id       = pRawData->bitstream_format;
            port_fmt_ptr->fmt_id    = new_output_fmt_id;
            is_mf_valid_and_changed = TRUE;

            // total payload size - header
            uint32_t raw_fmt_size = payload->actual_data_len -
                                    (sizeof(capi_set_get_media_format_t) + sizeof(capi_raw_compressed_data_format_t));
            uint8_t *raw_fmt_ptr = NULL;
            if (raw_fmt_size)
            {
               raw_fmt_ptr = (uint8_t *)(pRawData + 1);
            }

            ar_result_t res = tu_capi_create_raw_compr_med_fmt(topo_ptr->gu.log_id,
                                                               raw_fmt_ptr,
                                                               raw_fmt_size,
                                                               new_output_fmt_id,
                                                               port_fmt_ptr,
                                                               TRUE /*with_header*/,
                                                               topo_ptr->heap_id);
            if (AR_DID_FAIL(res))
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Module 0x%lX: Error while allocating memory for raw media fmt, size %lu",
                        module_ptr->gu.module_instance_id,
                        payload->actual_data_len);
               return CAPI_ENOMEMORY;
            }
            is_mf_valid_and_changed = TRUE;
         }

         // update the change status only if the media format has changed, otherwise, 2 times output can reset
         // media_fmt_event flag
         if (is_mf_valid_and_changed)
         {
            out_port_ptr->common.flags.media_fmt_event = is_mf_valid_and_changed;
         }

         break;
      }
      case CAPI_DEINTERLEAVED_RAW_COMPRESSED:
      {
         if (payload->actual_data_len <
             (sizeof(capi_set_get_media_format_t) + sizeof(capi_deinterleaved_raw_compressed_data_format_t)))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in media fmt function. The actual size %lu is less than the required size "
                     "for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     (uint32_t)(data_ptr->format_header.data_format));
            return CAPI_ENEEDMORE;
         }

         if (!event_info_ptr->port_info.is_valid || event_info_ptr->port_info.is_input_port)
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in media fmt function. port is not valid or is an input port.",
                     module_ptr->gu.module_instance_id);
            return CAPI_EBADPARAM;
         }

         capi_deinterleaved_raw_compressed_data_format_t *pDeintRawData =
            (capi_deinterleaved_raw_compressed_data_format_t *)(payload->data_ptr +
                                                                sizeof(capi_set_get_media_format_t));

         if (IS_VALID_CAPI_VALUE(pDeintRawData->bitstream_format))
         {
            new_output_fmt_id = pDeintRawData->bitstream_format;
            if (port_fmt_ptr->fmt_id != new_output_fmt_id)
            {
               port_fmt_ptr->fmt_id    = new_output_fmt_id;
               is_mf_valid_and_changed = TRUE;
            }
         }

         if (IS_VALID_CAPI_VALUE(pDeintRawData->bufs_num))
         {
            uint32_t req_size = sizeof(capi_set_get_media_format_t) +
                                sizeof(capi_deinterleaved_raw_compressed_data_format_t) +
                                pDeintRawData->bufs_num * sizeof(capi_channel_mask_t);

            if (pDeintRawData->bufs_num != port_fmt_ptr->deint_raw.bufs_num)
            {
               if (payload->actual_data_len < req_size)
               {
                  TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_ERROR_PRIO,
                           "Module 0x%lX: Error in media fmt function. The actual size %lu is less than the required "
                           "size for id %lu.",
                           module_ptr->gu.module_instance_id,
                           payload->actual_data_len,
                           (uint32_t)(data_ptr->format_header.data_format));
                  return CAPI_ENEEDMORE;
               }

               port_fmt_ptr->deint_raw.bufs_num = pDeintRawData->bufs_num;
               is_mf_valid_and_changed          = TRUE;
            }

            capi_channel_mask_t *ch_mask_ptr =
               (capi_channel_mask_t *)(payload->data_ptr + sizeof(capi_set_get_media_format_t) +
                                       sizeof(capi_deinterleaved_raw_compressed_data_format_t));
            for (uint32_t b = 0; b < pDeintRawData->bufs_num; b++)
            {
               if (port_fmt_ptr->deint_raw.ch_mask[b].channel_mask_lsw != ch_mask_ptr[b].channel_mask_lsw)
               {
                  port_fmt_ptr->deint_raw.ch_mask[b].channel_mask_lsw = ch_mask_ptr[b].channel_mask_lsw;
                  is_mf_valid_and_changed                             = TRUE;
               }
               if (port_fmt_ptr->deint_raw.ch_mask[b].channel_mask_msw != ch_mask_ptr[b].channel_mask_msw)
               {
                  port_fmt_ptr->deint_raw.ch_mask[b].channel_mask_msw = ch_mask_ptr[b].channel_mask_msw;
                  is_mf_valid_and_changed                             = TRUE;
               }
            }
         }

         // update the change status only if the media format has changed, otherwise, 2 times output can reset
         // media_fmt_event flag
         if (is_mf_valid_and_changed)
         {
            out_port_ptr->common.flags.media_fmt_event = is_mf_valid_and_changed;
         }
         THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_unsupported_mf, TRUE);

         break;
      }
      case CAPI_MAX_FORMAT_TYPE:
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "Module 0x%lX: Port id 0x%lx: module output media format: invalid data format. "
                  "propagating? %u,",
                  module_ptr->gu.module_instance_id,
                  out_port_ptr->gu.cmn.id,
                  out_port_ptr->common.flags.media_fmt_event);
         return CAPI_EUNSUPPORTED;
      default:
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: Error in media format function. Changing media type not supported for format 0x%lX",
                  module_ptr->gu.module_instance_id,
                  data_ptr->format_header.data_format);
         return CAPI_EUNSUPPORTED;
   }

   TOPO_PRINT_MEDIA_FMT(topo_ptr->gu.log_id, module_ptr, out_port_ptr, port_fmt_ptr, "module output");

   if (module_ptr->bypass_ptr)
   {
      tu_set_media_fmt(&topo_ptr->mf_utils, &module_ptr->bypass_ptr->media_fmt_ptr, port_fmt_ptr, topo_ptr->heap_id);
      if (module_ptr->bypass_ptr->media_fmt_ptr == NULL)
      {
         return AR_EBADPARAM;
      }

      // clear MF event when module is bypassed if event was not already existing. Event needs to be set in case
      // interleaving value changes.
      bool_t mf_event = mf_event_pending;

      if ((SPF_IS_PACKETIZED_OR_PCM(gen_cntrdf)) && (1 == module_ptr->bypass_ptr->media_fmt_ptr->pcm.num_channels))
      {
         if (out_port_ptr->common.media_fmt_ptr->pcm.interleaving !=
             module_ptr->bypass_ptr->media_fmt_ptr->pcm.interleaving)
         {
            // if num ch is 1, output mf picks the interleaving field of the bypassed module
            // This is needed to ensure downstream modules receive inp mf acc to what they expect.
            // For 1ch, interleaving field is actually dont care
            out_port_ptr->common.media_fmt_ptr->pcm.interleaving =
               module_ptr->bypass_ptr->media_fmt_ptr->pcm.interleaving;
            mf_event = TRUE;
         }
      }
      out_port_ptr->common.flags.media_fmt_event = mf_event;
   }
   else
   {
      tu_set_media_fmt(&topo_ptr->mf_utils, &out_port_ptr->common.media_fmt_ptr, port_fmt_ptr, topo_ptr->heap_id);
      out_port_ptr->common.flags.is_mf_valid = topo_is_valid_media_fmt(out_port_ptr->common.media_fmt_ptr);

      /** Reset pcm unpacked mask*/
      gen_topo_reset_pcm_unpacked_mask(&out_port_ptr->common);
   }

   /*
    *  1. If module's SG is Stopped then events will be handled in the PREPARE command later.
    *  2. For SUSPEND since PREPARE is not going to come therefore try to handle the event now.
    *    */
   if (!gen_topo_is_module_sg_stopped(module_ptr) && out_port_ptr->common.flags.media_fmt_event)
   {
      gen_topo_capi_event_flag_t capi_event_flags = {.word = 0 };
      capi_event_flags.media_fmt_event = TRUE;
      capi_event_flags.port_thresh     = TRUE;
      GEN_TOPO_SET_CAPI_EVENT_FLAGS(topo_ptr, capi_event_flags);
   }

   // check and update dynamic inplace flag based on new media format.
   gen_topo_check_and_update_dynamic_inplace(module_ptr);

   // set med fmt to the attached module on this output port.
   gen_topo_set_med_fmt_to_attached_module(topo_ptr, out_port_ptr);

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG(topo_ptr->gu.log_id, DBG_MED_PRIO, "Total number of media format blocks %lu", topo_ptr->mf_utils.num_nodes);
#endif

   return result;
}

capi_err_t gen_topo_handle_process_state_event(gen_topo_t *       topo_ptr,
                                               gen_topo_module_t *module_ptr,
                                               capi_event_info_t *event_info_ptr)
{
   capi_err_t                  result   = CAPI_EOK;
   capi_buf_t *                payload  = &event_info_ptr->payload;
   capi_event_process_state_t *data_ptr = (capi_event_process_state_t *)payload->data_ptr;

   // Only update process state event if new process state is different than before.
   if (((!module_ptr->flags.disabled) && (FALSE == data_ptr->is_enabled)) ||
       ((module_ptr->flags.disabled) && (TRUE == data_ptr->is_enabled)))
   {
      module_ptr->flags.disabled = !data_ptr->is_enabled;
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               SPF_LOG_PREFIX "Module 0x%lX: Module process state set to %lu",
               module_ptr->gu.module_instance_id,
               (uint32_t)(data_ptr->is_enabled));

      bool_t is_run_time = !gen_topo_is_module_sg_stopped_or_suspended(module_ptr);

      if (is_run_time)
      {
         GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, process_state);
      }

      if (module_ptr->flags.disabled)
      {
         result = gen_topo_check_create_bypass_module(topo_ptr, module_ptr);
      }
      else
      {
         result = gen_topo_check_destroy_bypass_module(topo_ptr, module_ptr, FALSE);
      }
   }
   return result;
}

// id_not_handled [out param] is used to specify that that event id is not being handled by this function.
// Differentiates between cases of error and nonerror unhandled.
capi_err_t gen_topo_capi_callback_base(gen_topo_module_t *module_ptr,
                                       capi_event_id_t    id,
                                       capi_event_info_t *event_info_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (!module_ptr || !event_info_ptr)
   {
      return CAPI_EFAILED;
   }

   capi_buf_t *payload  = &event_info_ptr->payload;
   gen_topo_t *topo_ptr = module_ptr->topo_ptr;

   if (!topo_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error in callback function. Data possibly corrupted. ");
      return CAPI_EFAILED;
   }

   if (payload->actual_data_len > payload->max_data_len)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX: Error in callback function. The actual size %lu is greater than the max size %lu for "
               "id %lu.",
               module_ptr->gu.module_instance_id,
               payload->actual_data_len,
               payload->max_data_len,
               (uint32_t)(id));
      return CAPI_EBADPARAM;
   }

   switch (id)
   {
      case CAPI_EVENT_KPPS:
      {
         if (payload->actual_data_len < sizeof(capi_event_KPPS_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_event_bandwidth_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         capi_event_KPPS_t *kpps_ptr = (capi_event_KPPS_t *)(payload->data_ptr);

         bool_t kpps_changed = FALSE;

         if (module_ptr->bypass_ptr)
         {
            if (module_ptr->bypass_ptr->kpps != kpps_ptr->KPPS)
            {
               module_ptr->bypass_ptr->kpps = kpps_ptr->KPPS;
               kpps_changed                 = TRUE;
            }
         }
         else
         {
            if (module_ptr->kpps != kpps_ptr->KPPS)
            {
               module_ptr->kpps = kpps_ptr->KPPS;
               kpps_changed     = TRUE;
               if (!gen_topo_is_module_sg_stopped_or_suspended(module_ptr))
               {
                  GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, kpps);
               }
            }
         }

         if (kpps_changed)
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     SPF_LOG_PREFIX "Module 0x%lX KPPS  %lu",
                     module_ptr->gu.module_instance_id,
                     (uint32_t)(kpps_ptr->KPPS));
         }

         return CAPI_EOK;
      }
      case CAPI_EVENT_BANDWIDTH:
      {
         if (payload->actual_data_len < sizeof(capi_event_bandwidth_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_event_process_state_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         capi_event_bandwidth_t *bw_ptr     = (capi_event_bandwidth_t *)(payload->data_ptr);
         bool_t                  bw_changed = FALSE;
         if (module_ptr->bypass_ptr)
         {
            if (module_ptr->bypass_ptr->code_bw != bw_ptr->code_bandwidth)
            {
               bw_changed                      = TRUE;
               module_ptr->bypass_ptr->code_bw = bw_ptr->code_bandwidth;
            }
            if (module_ptr->bypass_ptr->data_bw != bw_ptr->data_bandwidth)
            {
               bw_changed                      = TRUE;
               module_ptr->bypass_ptr->data_bw = bw_ptr->data_bandwidth;
            }
         }
         else
         {
            if (module_ptr->code_bw != bw_ptr->code_bandwidth)
            {
               module_ptr->code_bw = bw_ptr->code_bandwidth;
               bw_changed          = TRUE;
            }
            if (module_ptr->data_bw != bw_ptr->data_bandwidth)
            {
               module_ptr->data_bw = bw_ptr->data_bandwidth;
               bw_changed          = TRUE;
            }

            if (bw_changed && !gen_topo_is_module_sg_stopped_or_suspended(module_ptr))
            {
               GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, bw);
            }
         }

         if (bw_changed)
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     SPF_LOG_PREFIX "Module 0x%lX Module BW (code,data) %lu %lu bytes per sec.",
                     module_ptr->gu.module_instance_id,
                     bw_ptr->code_bandwidth,
                     bw_ptr->data_bandwidth);
         }

         return CAPI_EOK;
      }
      case CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED_V2:
      {
         if (payload->actual_data_len < sizeof(capi_set_get_media_format_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_set_get_media_format_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         return gen_topo_handle_output_media_format_event(topo_ptr, module_ptr, event_info_ptr, TRUE, FALSE);
      }
      case CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED:
      {
         if (payload->actual_data_len < sizeof(capi_set_get_media_format_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_set_get_media_format_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         return gen_topo_handle_output_media_format_event(topo_ptr, module_ptr, event_info_ptr, FALSE, FALSE);
      }
      case CAPI_EVENT_PORT_DATA_THRESHOLD_CHANGE:
      {
         // A module implementing frame duration extension must always use 1 byte threshold. Not doing so can result in
         // inf loop.
         // container aggregates threshold, sets frame dur on module and then module raises threshold, which in turn
         // makes fwk set cntr frame len to module. For receiving configured threshold need_thresh_cfg ext should be
         // used. Test Module does this and hence not failing for now.
         if (module_ptr->flags.need_cntr_frame_dur_extn)
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Warning: A module that needs container frame duration extension must not raise "
                     "threshold.",
                     module_ptr->gu.module_instance_id);
         }

         if (payload->actual_data_len < sizeof(capi_port_data_threshold_change_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_port_data_threshold_change_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         if (!event_info_ptr->port_info.is_valid)
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. port is not valid ",
                     module_ptr->gu.module_instance_id);
            return CAPI_EBADPARAM;
         }

         capi_port_data_threshold_change_t *data_ptr = (capi_port_data_threshold_change_t *)(payload->data_ptr);
         uint32_t new_threshold = (data_ptr->new_threshold_in_bytes > 1) ? data_ptr->new_threshold_in_bytes : 0;

         if (event_info_ptr->port_info.is_input_port)
         {
            gen_topo_input_port_t *in_port_ptr =
               (gen_topo_input_port_t *)gu_find_input_port_by_index(&module_ptr->gu,
                                                                    event_info_ptr->port_info.port_index);

            if (!in_port_ptr)
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Warning: Module 0x%lX: Input port index 0x%lx not present",
                        module_ptr->gu.module_instance_id,
                        event_info_ptr->port_info.port_index);
               return CAPI_EOK; // don't return error. see reasoing below at output port.
            }

            if (module_ptr->bypass_ptr)
            {
               module_ptr->bypass_ptr->in_thresh_bytes_all_ch = new_threshold;

               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_HIGH_PRIO,
                        "Module 0x%lX: Input port id 0x%lx event with new port-threshold = %lu raised while module is "
                        "bypassed, caching to handle later.",
                        module_ptr->gu.module_instance_id,
                        in_port_ptr->gu.cmn.id,
                        data_ptr->new_threshold_in_bytes);
            }
            else
            {
               /** Mark threshold event asa */
               if (in_port_ptr->common.threshold_raised != new_threshold)
               {
                  if (in_port_ptr->common.bufs_ptr && in_port_ptr->common.bufs_ptr[0].actual_data_len != 0)
                  {
                     TOPO_MSG(topo_ptr->gu.log_id,
                              DBG_HIGH_PRIO,
                              "Module 0x%lX: Warning: Threshold changed when buffer had valid data %lu",
                              module_ptr->gu.module_instance_id,
                              in_port_ptr->common.bufs_ptr[0].actual_data_len);
                  }

                  in_port_ptr->common.threshold_raised = new_threshold;

                  // Reset threshold info
                  in_port_ptr->common.flags.port_has_threshold = (in_port_ptr->common.threshold_raised) ? TRUE : FALSE;
                  in_port_ptr->common.port_event_new_threshold = in_port_ptr->common.threshold_raised;

                  GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, port_thresh);

                  TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           "Module 0x%lX: Input port id 0x%lx event with new port-threshold = %lu bytes.",
                           module_ptr->gu.module_instance_id,
                           in_port_ptr->gu.cmn.id,
                           data_ptr->new_threshold_in_bytes);
               }
            }
         }
         else
         {
            gen_topo_output_port_t *out_port_ptr =
               (gen_topo_output_port_t *)gu_find_output_port_by_index(&module_ptr->gu,
                                                                      event_info_ptr->port_info.port_index);

            if (!out_port_ptr)
            {
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "Warning: Module 0x%lX: Output port index %lu not present",
                        module_ptr->gu.module_instance_id,
                        event_info_ptr->port_info.port_index);
               return CAPI_EOK; // don't return error.
                                // E.g. MFC/RAT output is not connected. When setting input-MF,
                                // they may raise threshold and returning error from here causes input MF to fail & we
                                // may try to bypass the module.  Implementing data-port-op in every modules is not
                                // practical.
            }

            if (module_ptr->bypass_ptr)
            {
               module_ptr->bypass_ptr->out_thresh_bytes_all_ch = new_threshold;

               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_HIGH_PRIO,
                        "Module 0x%lX: Output port id 0x%lx event with new port-threshold = %lu raised while module is "
                        "bypassed, caching to handle later.",
                        module_ptr->gu.module_instance_id,
                        out_port_ptr->gu.cmn.id,
                        data_ptr->new_threshold_in_bytes);
            }
            else
            {
               if (out_port_ptr->common.threshold_raised != new_threshold)
               {
                  uint32_t old_bytes =
                     topo_ptr->proc_context.process_info.is_in_mod_proc_context
                        ? topo_ptr->proc_context.out_port_scratch_ptr[event_info_ptr->port_info.port_index]
                             .prev_actual_data_len[0]
                        : (out_port_ptr->common.bufs_ptr && out_port_ptr->common.bufs_ptr[0].actual_data_len);
                  if (old_bytes != 0)
                  {
                     TOPO_MSG(topo_ptr->gu.log_id,
                              DBG_HIGH_PRIO,
                              "Module 0x%lX: Warning: Threshold changed when buffer had valid data %lu",
                              module_ptr->gu.module_instance_id,
                              old_bytes);
                  }

                  out_port_ptr->common.threshold_raised = new_threshold;

                  // Reset threshold info
                  out_port_ptr->common.flags.port_has_threshold =
                     (out_port_ptr->common.threshold_raised) ? TRUE : FALSE;
                  out_port_ptr->common.port_event_new_threshold = out_port_ptr->common.threshold_raised;

                  GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, port_thresh);

                  TOPO_MSG(topo_ptr->gu.log_id,
                           DBG_HIGH_PRIO,
                           "Module 0x%lX: Output port id 0x%lx event with new port-threshold = %lu bytes.",
                           module_ptr->gu.module_instance_id,
                           out_port_ptr->gu.cmn.id,
                           data_ptr->new_threshold_in_bytes);
               }
            }
         }

         return CAPI_EOK;
      }
      case CAPI_EVENT_METADATA_AVAILABLE:
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: Output port index %lu metadata_available event not supported",
                  module_ptr->gu.module_instance_id,
                  event_info_ptr->port_info.port_index);
         break;
      }
      case CAPI_EVENT_GET_LIBRARY_INSTANCE:
      {
         if (event_info_ptr->payload.actual_data_len < sizeof(capi_event_get_library_instance_t))
         {
            CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
            break;
         }

#if 0
      capi_event_get_library_instance_t *payload_ptr =
            (capi_event_get_library_instance_t *)(event_info_ptr->payload.data_ptr);

      result = capi_library_factory_get_instance(payload_ptr->id, &payload_ptr->ptr);
#else
         result = CAPI_EFAILED;
#endif

         if (result != CAPI_EOK)
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Failed to get an instance of the library to get capi v2 modules.",
                     module_ptr->gu.module_instance_id);
         }

         break;
      }
      case CAPI_EVENT_GET_DLINFO:
      {
         if (event_info_ptr->payload.actual_data_len < sizeof(capi_event_dlinfo_t))
         {
            CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
            break;
         }

         capi_event_dlinfo_t *payload_ptr = (capi_event_dlinfo_t *)(event_info_ptr->payload.data_ptr);

         uint32_t *start_addr = NULL;
         bool_t    is_dl      = FALSE;

         gen_topo_get_dl_info(topo_ptr->gu.log_id,
                              module_ptr->gu.amdb_handle,
                              &is_dl,
                              &start_addr,
                              &payload_ptr->load_size);

         uint64_t phy_start_addr    = posal_memorymap_get_physical_addr_v2(&start_addr);
         payload_ptr->is_dl         = is_dl;
         payload_ptr->load_addr_lsw = (uint32_t)phy_start_addr;
         payload_ptr->load_addr_msw = (uint32_t)(phy_start_addr >> 32);

         break;
      }
      case CAPI_EVENT_ALGORITHMIC_DELAY:
      {
         capi_event_algorithmic_delay_t *delay_ptr =
            (capi_event_algorithmic_delay_t *)(event_info_ptr->payload.data_ptr);

         bool_t algo_delay_changed = FALSE;
         if (module_ptr->bypass_ptr)
         {
            if (module_ptr->bypass_ptr->algo_delay != delay_ptr->delay_in_us)
            {
               module_ptr->bypass_ptr->algo_delay = delay_ptr->delay_in_us;
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_HIGH_PRIO,
                        SPF_LOG_PREFIX "Module 0x%lX: gen_topo_capi_cb_func (bypass): updated algo delay(%lu) us",
                        module_ptr->gu.module_instance_id,
                        module_ptr->bypass_ptr->algo_delay);
               algo_delay_changed = TRUE;
            }
         }
         else
         {
            if (module_ptr->algo_delay != delay_ptr->delay_in_us)
            {
               module_ptr->algo_delay = delay_ptr->delay_in_us;
               TOPO_MSG(topo_ptr->gu.log_id,
                        DBG_HIGH_PRIO,
                        SPF_LOG_PREFIX "Module 0x%lX: gen_topo_capi_cb_func: updated algo delay(%lu) us",
                        module_ptr->gu.module_instance_id,
                        module_ptr->algo_delay);
               algo_delay_changed = TRUE;
            }
         }

         if (algo_delay_changed)
         {
            GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, algo_delay_event);

            if (topo_ptr->topo_to_cntr_vtable_ptr->algo_delay_change_event)
            {
               topo_ptr->topo_to_cntr_vtable_ptr->algo_delay_change_event(module_ptr);
            }
         }
         break;
      }
      case CAPI_EVENT_DATA_TO_DSP_CLIENT:
      case CAPI_EVENT_DATA_TO_DSP_CLIENT_V2:
      {
         return ar_result_to_capi_err(
            topo_ptr->topo_to_cntr_vtable_ptr->handle_capi_event(module_ptr, id, event_info_ptr));
      }
      case CAPI_EVENT_GET_DATA_FROM_DSP_SERVICE:
      {
         if (payload->actual_data_len < sizeof(capi_event_get_data_from_dsp_service_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%lu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_event_get_data_from_dsp_service_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         return ar_result_to_capi_err(
            topo_ptr->topo_to_cntr_vtable_ptr->raise_data_from_dsp_service_event(module_ptr, event_info_ptr));
      }
      case CAPI_EVENT_DYNAMIC_INPLACE_CHANGE:
      {
         if (payload->actual_data_len < sizeof(capi_event_dynamic_inplace_change_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_event_dynamic_inplace_change_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         capi_event_dynamic_inplace_change_t *cfg_ptr = (capi_event_dynamic_inplace_change_t *)(payload->data_ptr);

         // first set the flag and change it if conditions are not met to be inplace
         bool_t new_dynamic_inplace = (cfg_ptr->is_inplace > 0);

         if (new_dynamic_inplace == module_ptr->flags.dynamic_inplace)
         {
            return CAPI_EOK;
         }
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "Module 0x%lX raised an event to change from is_inplace = %lu to new is_inplace = %lu",
                  module_ptr->gu.module_instance_id,
                  module_ptr->flags.dynamic_inplace,
                  (uint32_t)(cfg_ptr->is_inplace));

         // if dynamic inplace changed
         module_ptr->flags.pending_dynamic_inplace = new_dynamic_inplace;
         GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, dynamic_inplace_change);

         return CAPI_EOK;
      }
      case CAPI_EVENT_HW_ACCL_PROC_DELAY:
      {
         if (payload->actual_data_len < sizeof(capi_event_hw_accl_proc_delay_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_event_hw_accl_proc_delay_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         capi_event_hw_accl_proc_delay_t *delay_ptr = (capi_event_hw_accl_proc_delay_t *)(payload->data_ptr);
         bool_t                           hw_accel_proc_delay_changed = FALSE;
         if (module_ptr->bypass_ptr)
         {
            if (module_ptr->bypass_ptr->hw_acc_proc_delay != delay_ptr->delay_in_us)
            {
               hw_accel_proc_delay_changed               = TRUE;
               module_ptr->bypass_ptr->hw_acc_proc_delay = delay_ptr->delay_in_us;
            }
         }
         else
         {
            if (module_ptr->hw_acc_proc_delay != delay_ptr->delay_in_us)
            {
               module_ptr->hw_acc_proc_delay = delay_ptr->delay_in_us;
               hw_accel_proc_delay_changed   = TRUE;
               if (!gen_topo_is_module_sg_stopped_or_suspended(module_ptr))
               {
                  GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, hw_acc_proc_delay_event);
               }
            }
         }
         if (hw_accel_proc_delay_changed)
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_HIGH_PRIO,
                     "Module 0x%lX hardware accelerator proc delay %lu",
                     module_ptr->gu.module_instance_id,
                     (uint32_t)(delay_ptr->delay_in_us));
         }

         return CAPI_EOK;
      }
      case CAPI_EVENT_DEINTERLEAVED_UNPACKED_V2_SUPPORTED:
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "Module 0x%lX: Raised an capi event to inform it supports CAPI_DEINTERLEAVED_UNPACKED_V2",
                  module_ptr->gu.module_instance_id);

         module_ptr->flags.supports_deintlvd_unpacked_v2 = TRUE;
         return CAPI_EOK;
      }
      default:
      {
         // No print, since not all ids are expected to be handled by common handler.
         return CAPI_EUNSUPPORTED;
      }
   }
   return result;
}

capi_err_t gen_topo_event_is_signal_triggered_active_change(gen_topo_module_t *module_ptr, capi_buf_t *payload_ptr)
{
   gen_topo_t *topo_ptr = module_ptr->topo_ptr;
   if (sizeof(intf_extn_event_id_is_signal_triggered_active_t) > payload_ptr->actual_data_len)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX: Error in "
               "gen_topo_event_is_signal_triggered_active_change. The actual size "
               "%lu is greater than the expected ",
               module_ptr->gu.module_instance_id,
               payload_ptr->actual_data_len);
      return CAPI_EBADPARAM;
   }

   intf_extn_event_id_is_signal_triggered_active_t *event =
      (intf_extn_event_id_is_signal_triggered_active_t *)payload_ptr->data_ptr;

   if (module_ptr->flags.is_signal_trigger_deactivated == event->is_signal_triggered_active)
   {
      GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, is_signal_triggered_active_change);
      module_ptr->flags.is_signal_trigger_deactivated = event->is_signal_triggered_active ? FALSE : TRUE;
      gen_topo_update_is_signal_triggered_active_flag(topo_ptr);
   }

   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_HIGH_PRIO,
            "Module 0x%lX: is_signal_triggered_active:   %d",
            module_ptr->gu.module_instance_id,
            event->is_signal_triggered_active);

   return CAPI_EOK;
}

capi_err_t gen_topo_capi_callback_non_island(void *context_ptr, capi_event_id_t id, capi_event_info_t *event_info_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (!context_ptr || !event_info_ptr)
   {
      return CAPI_EFAILED;
   }

   capi_buf_t *       payload    = &event_info_ptr->payload;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)(context_ptr);
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;

   switch (id)
   {
      case CAPI_EVENT_KPPS:
      case CAPI_EVENT_DYNAMIC_INPLACE_CHANGE:
      case CAPI_EVENT_BANDWIDTH:
      case CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED_V2:
      case CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED:
      case CAPI_EVENT_PORT_DATA_THRESHOLD_CHANGE:
      case CAPI_EVENT_METADATA_AVAILABLE:
      case CAPI_EVENT_GET_LIBRARY_INSTANCE:
      case CAPI_EVENT_GET_DLINFO:
      case CAPI_EVENT_DATA_TO_DSP_CLIENT:
      case CAPI_EVENT_DATA_TO_DSP_CLIENT_V2:
      case CAPI_EVENT_ALGORITHMIC_DELAY:
      case CAPI_EVENT_GET_DATA_FROM_DSP_SERVICE:
      case CAPI_EVENT_HW_ACCL_PROC_DELAY:
      case CAPI_EVENT_DEINTERLEAVED_UNPACKED_V2_SUPPORTED:
      {
         result = gen_topo_capi_callback_base(module_ptr, id, event_info_ptr);
         if (CAPI_FAILED(result))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. Error in common callback "
                     "handling, err code: 0x%lx",
                     module_ptr->gu.module_instance_id,
                     result);
         }
         return result;
      }
      case CAPI_EVENT_PROCESS_STATE:
      {
         if (payload->actual_data_len < sizeof(capi_event_process_state_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for "
                     "id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_event_process_state_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }
         return gen_topo_handle_process_state_event(topo_ptr, module_ptr, event_info_ptr);
      }
      case CAPI_EVENT_DATA_TO_DSP_SERVICE:
      {
         if (payload->actual_data_len < sizeof(capi_event_data_to_dsp_service_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%lu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_event_data_to_dsp_service_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         // Some events can be handled within topo itself
         capi_buf_t *                      payload       = &event_info_ptr->payload;
         capi_event_data_to_dsp_service_t *dsp_event_ptr = (capi_event_data_to_dsp_service_t *)(payload->data_ptr);

         switch (dsp_event_ptr->param_id)
         {
            case INTF_EXTN_EVENT_ID_BLOCK_PORT_DS_STATE_PROP:
            case INTF_EXTN_EVENT_ID_PORT_DS_STATE:
            case INTF_EXTN_EVENT_ID_IS_RT_PORT_PROPERTY:
            {
               result = gen_topo_handle_port_propagated_capi_event(topo_ptr, module_ptr, event_info_ptr);
               break;
            }
            case INTF_EXTN_EVENT_ID_MODULE_BUFFER_ACCESS_ENABLE:
            {
               if (topo_ptr->topo_to_cntr_vtable_ptr->module_buffer_access_event)
               {
                  result = topo_ptr->topo_to_cntr_vtable_ptr->module_buffer_access_event(topo_ptr,
                                                                                         module_ptr,
                                                                                         event_info_ptr);
               }
               else
               {
                  return CAPI_EUNSUPPORTED;
               }
               break;
            }
            case FWK_EXTN_EVENT_ID_DATA_TRIGGER_IN_ST_CNTR:
            {
               // non-island call.
               result = gen_topo_trigger_policy_event_data_trigger_in_st_cntr(module_ptr, &dsp_event_ptr->payload);
               break;
            }
            case FWK_EXTN_EVENT_ID_IS_SIGNAL_TRIGGERED_ACTIVE:
            {
               result = gen_topo_event_is_signal_triggered_active_change(module_ptr, &dsp_event_ptr->payload);
               break;
            }
            default:
            {
               return ar_result_to_capi_err(
                  topo_ptr->topo_to_cntr_vtable_ptr->raise_data_to_dsp_service_event(module_ptr, event_info_ptr));
            }
         }
         break;
      }
      default:
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: Error in callback function. ID %lu not supported.",
                  module_ptr->gu.module_instance_id,
                  (uint32_t)(id));
         return CAPI_EUNSUPPORTED;
      }
   }

   return result;
}
