/*========================================================================

 */ /**
This file contains function definitions internal to CAPI Pull
mode and Push mode module

  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
  SPDX-License-Identifier: BSD-3-Clause-Clear

  */
/*====================================================================== */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */

#include "posal_thread_prio.h"
#include "sh_mem_pull_push_mode_api.h"

#include "ar_msg.h"
#include "apm.h"
#include "pull_push_mode.h"
#ifndef PM_UNIT_TEST
#include "posal_timer.h"
#include "posal_memorymap.h"
#include "posal_memory.h"
#include "spf_interleaver.h"
#else
#include "test_util.h"
#endif
#include "dam_batch_metadata_api.h"

// Important: Disabling code to bump up thread priority whenever position buffer gets updated.
// Initially priority change was introduced for the thread to be able to update position buffer
// as soon as possible and avoid any performance issues due to thread premptions. But in actuality
// its unlikely to cause any issue even if there is a premption.
// This logic is causing overhead in push pull modules in the steady state and contrubuting to
// higher MPPS in case of low frame sizes, hence avoiding this call.
#define DISABLE_PM_THREAD_PRIO

static void pull_mode_update_pos_buffer(sh_mem_pull_push_mode_position_buffer_t *pos_buf_ptr,
                                        posal_thread_prio_t                      ist_priority,
                                        uint32_t                                 read_index,
                                        uint64_t                                 timestamp)
{
   // Update the position buffer
   uint32_t prev_frame_counter = pos_buf_ptr->frame_counter;

#ifndef DISABLE_PM_THREAD_PRIO
   /** below is a critical section which is executed at high prio so that if the client is also trying to read
    *  we will try to complete is ASAP & they don't have to wait for longer
    */
   posal_thread_prio_t priority = posal_thread_prio_get();

   // set IST thread priority
   posal_thread_set_prio(ist_priority);
#endif

   pos_buf_ptr->frame_counter    = 0;
   pos_buf_ptr->index            = read_index;
   pos_buf_ptr->timestamp_us_lsw = (uint32_t)timestamp;
   pos_buf_ptr->timestamp_us_msw = (uint32_t)(timestamp >> 32);
   pos_buf_ptr->frame_counter    = prev_frame_counter + 1;

#ifndef DISABLE_PM_THREAD_PRIO
   // revert to original thread priority
   posal_thread_set_prio(priority);
#endif
}

capi_err_t pull_push_mode_watermark_levels_init(pull_push_mode_t *pm_ptr,
                                                uint32_t          num_water_mark_levels,
                                                event_cfg_sh_mem_pull_push_mode_watermark_level_t *water_mark_levels,
                                                uint32_t                                           heap_id)
{
   uint32_t watermark_level_bytes = 0;
   pm_ptr->num_water_mark_levels  = num_water_mark_levels;
   uint32_t miid                  = pm_ptr->miid;

   if (NULL != pm_ptr->water_mark_levels_ptr)
   {
      posal_memory_free(pm_ptr->water_mark_levels_ptr);
   }

   if (0 < pm_ptr->num_water_mark_levels)
   {
      pm_ptr->water_mark_levels_ptr =
         (pull_push_mode_watermark_level_t *)posal_memory_malloc(pm_ptr->num_water_mark_levels *
                                                                    sizeof(pull_push_mode_watermark_level_t),
                                                                 (POSAL_HEAP_ID)heap_id);

      if (NULL == pm_ptr->water_mark_levels_ptr)
      {
         PULL_PUSH_MSG(miid,
                       DBG_ERROR_PRIO,
                       "pull_push_mode_watermark_levels_init: Failed to allocate memory for water mark events,"
                       "num_water_mark_levels: %d",
                       pm_ptr->num_water_mark_levels);
         return CAPI_ENOMEMORY;
      }

      for (uint32_t i = 0; i < pm_ptr->num_water_mark_levels; i++)
      {
         PULL_PUSH_MSG(miid,
                       DBG_HIGH_PRIO,
                       "pull_push_mode_watermark_levels_init: Water Mark Level %lu bytes",
                       water_mark_levels[i].watermark_level_bytes);

         watermark_level_bytes = water_mark_levels[i].watermark_level_bytes;

         if (watermark_level_bytes <= pm_ptr->shared_circ_buf_size)
         {
            pm_ptr->water_mark_levels_ptr[i].watermark_level_bytes = watermark_level_bytes;
         }
         else
         {
            PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "pull_push_mode_watermark_levels_init: Invalid water mark level.");
            return CAPI_EFAILED;
         }
      }
   }

   return CAPI_EOK;
}

capi_err_t pull_push_mode_init(pull_push_mode_t *pm_ptr, sh_mem_pull_push_mode_cfg_t *init_ptr)
{
   capi_err_t result = CAPI_EOK;

   if ((NULL == pm_ptr) || (NULL == init_ptr))
   {
      PULL_PUSH_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "pull_push_mode_init: Invalid params!");
      return CAPI_EBADPARAM;
   }
   uint32_t miid = pm_ptr->miid;

   if ((0 != pm_ptr->pos_buf_mem_map_handle) || (0 != pm_ptr->circ_buf_mem_map_handle))
   {
      PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "pull_push_mode_init: already configured with circ buf or pos buf memories");
      return CAPI_EBADPARAM;
   }

   pm_ptr->mem_map_client = apm_get_mem_map_client();

   pm_ptr->pos_buf_mem_map_handle  = init_ptr->pos_buf_mem_map_handle;
   pm_ptr->circ_buf_mem_map_handle = init_ptr->circ_buf_mem_map_handle;

   if (CAPI_FAILED(
          result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(pm_ptr->mem_map_client,
                                                                       pm_ptr->pos_buf_mem_map_handle,
                                                                       init_ptr->shared_pos_buf_addr_lsw,
                                                                       init_ptr->shared_pos_buf_addr_msw,
                                                                       sizeof(sh_mem_pull_push_mode_position_buffer_t),
                                                                       TRUE, // decrement ref count in deinit
                                                                       &(pm_ptr->shared_pos_buf_ptr))))
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "pull_push_mode_init: Pos Buf Phy to Virt Failed, physical addr: %lx%lx, result: %d \n",
                    init_ptr->shared_pos_buf_addr_lsw,
                    init_ptr->shared_pos_buf_addr_msw,
                    result);
      return CAPI_EFAILED;
   }

   if (CAPI_FAILED(result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(pm_ptr->mem_map_client,
                                                                                pm_ptr->circ_buf_mem_map_handle,
                                                                                init_ptr->shared_circ_buf_addr_lsw,
                                                                                init_ptr->shared_circ_buf_addr_msw,
                                                                                init_ptr->shared_circ_buf_size,
                                                                                TRUE, // Need to decrement ref count to
                                                                                      // release the handle
                                                                                &(pm_ptr->shared_circ_buf_start_ptr))))
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "pull_push_mode_init: Circ Buf Phy to Virt Failed, physical addr: %lx%lx, result: %d \n",
                    init_ptr->shared_circ_buf_addr_msw,
                    init_ptr->shared_circ_buf_addr_lsw,
                    result);
      return CAPI_EFAILED;
   }

   pm_ptr->shared_circ_buf_size = init_ptr->shared_circ_buf_size;

   PULL_PUSH_MSG(miid,
                 DBG_HIGH_PRIO,
                 "pull_push_mode_init: pos buf addr: %lx, miid: %d",
                 (uint32_t *)pm_ptr->shared_pos_buf_ptr,
                 pm_ptr->miid);

   PULL_PUSH_MSG(miid,
                 DBG_HIGH_PRIO,
                 "pull_push_mode_init: circ buf addr: %lx",
                 (uint32_t *)pm_ptr->shared_circ_buf_start_ptr);

   PULL_PUSH_MSG(miid, DBG_HIGH_PRIO, "pull_push_mode_init: circ buf size: %lu", pm_ptr->shared_circ_buf_size);

   // initialize the position buffer to zero
   memset(pm_ptr->shared_pos_buf_ptr, 0, sizeof(sh_mem_pull_push_mode_position_buffer_t));

   pm_ptr->num_water_mark_levels = 0;

   if (CAPI_FAILED(result = pull_push_mode_check_buf_size(pm_ptr, &pm_ptr->media_fmt)))
   {
      PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "pull_push_mode_deinit: releasing shared buffer.");
      pull_push_mode_deinit(pm_ptr);
      return result;
   }

   return result;
}

capi_err_t pull_push_mode_set_fwk_ext_inp_media_fmt(pull_push_mode_t                       *pm_ptr,
                                                    fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr)
{

   uint32_t miid = pm_ptr->miid;
   if ((16 != extn_ptr->bit_width) && (24 != extn_ptr->bit_width) && (32 != extn_ptr->bit_width))
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "set param received bad param for bit_width %d",
                    extn_ptr->bit_width);
      return CAPI_EBADPARAM;
   }
   if ((PCM_LSB_ALIGNED != extn_ptr->alignment) && (PCM_MSB_ALIGNED != extn_ptr->alignment))
   {
      PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "set param received bad param for alignment");
      return CAPI_EBADPARAM;
   }

   if ((PCM_LITTLE_ENDIAN != extn_ptr->endianness) && (PCM_BIG_ENDIAN != extn_ptr->endianness))
   {
      PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "set param received bad param for endianness");
      return CAPI_EBADPARAM;
   }

   pm_ptr->media_fmt.bit_width  = extn_ptr->bit_width;
   pm_ptr->media_fmt.alignment  = extn_ptr->alignment;
   pm_ptr->media_fmt.endianness = extn_ptr->endianness;

   return CAPI_EOK;
}

capi_err_t pull_push_mode_check_buf_size(pull_push_mode_t *pm_ptr, pm_media_fmt_t *media_fmt_ptr)
{
   uint32_t unit_size = (media_fmt_ptr->bits_per_sample >> 3) * media_fmt_ptr->num_channels;
   // if media format is not received then unit_size will be zero.
   // if shared buffer info is not received that buffer size will be zero.
   if ((0 != unit_size) && (0 != pm_ptr->shared_circ_buf_size) && (pm_ptr->shared_circ_buf_size % unit_size))
   {
      PULL_PUSH_MSG(pm_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Circular buffer size %lu is not a multiple of unit size %lu ",
                    pm_ptr->shared_circ_buf_size,
                    unit_size);
      pm_ptr->is_disabled = TRUE;
      return CAPI_EFAILED;
   }

   return CAPI_EOK;
}

capi_err_t pull_push_mode_set_inp_media_fmt(pull_push_mode_t *pm_ptr,
                                            media_format_t   *media_fmt_header,
                                            pm_media_fmt_t   *dst_media_fmt_ptr)
{
   payload_media_fmt_pcm_t *media_fmt = (payload_media_fmt_pcm_t *)(media_fmt_header + 1);

   if (CAPI_FAILED(capi_cmn_validate_client_pcm_media_format(media_fmt)))
   {
      PULL_PUSH_MSG(pm_ptr->miid, DBG_ERROR_PRIO, "input media format has errors");
      return CAPI_EBADPARAM;
   }

   dst_media_fmt_ptr->fmt_id            = media_fmt_header->fmt_id;
   dst_media_fmt_ptr->num_channels      = media_fmt->num_channels;
   dst_media_fmt_ptr->sample_rate       = media_fmt->sample_rate;
   dst_media_fmt_ptr->Q_format          = media_fmt->q_factor;
   dst_media_fmt_ptr->bit_width         = media_fmt->bit_width;
   dst_media_fmt_ptr->bits_per_sample   = media_fmt->bits_per_sample;
   dst_media_fmt_ptr->data_interleaving = pm_ptr->media_fmt.data_interleaving;
   if (CAPI_FAILED(pull_push_mode_check_buf_size(pm_ptr, dst_media_fmt_ptr)))
   {
      return CAPI_EFAILED;
   }
   dst_media_fmt_ptr->endianness = media_fmt->endianness;
   dst_media_fmt_ptr->alignment  = media_fmt->alignment;

   uint8_t *channel_mapping = (uint8_t *)(media_fmt + 1);

   memscpy(dst_media_fmt_ptr->channel_map,
           sizeof(dst_media_fmt_ptr->channel_map),
           channel_mapping,
           media_fmt->num_channels * sizeof(uint8_t));

   return CAPI_EOK;
}

bool_t pull_push_check_media_fmt_validity(pull_push_mode_t *pm_ptr)
{
   pm_media_fmt_t *media_fmt = &pm_ptr->media_fmt;
   uint32_t        miid      = pm_ptr->miid;
   if (((0 == media_fmt->num_channels) || (CAPI_MAX_CHANNELS_V2 < media_fmt->num_channels)))
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "Check media fmt validity Fail, num_channels %d. Max channels: %lu",
                    media_fmt->num_channels,
                    CAPI_MAX_CHANNELS_V2);
      return FALSE;
   }

   if ((0 == media_fmt->sample_rate) || (SAMPLE_RATE_384K < media_fmt->sample_rate))
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "Check media fmt validity Fail sample rate %d",
                    media_fmt->sample_rate);
      return FALSE;
   }

   if (((16 != media_fmt->bit_width) && (24 != media_fmt->bit_width) && (32 != media_fmt->bit_width)))
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "Check media fmt validity Fail bits_per_sample %d",
                    media_fmt->bit_width);
      return FALSE;
   }

   if ((PCM_LITTLE_ENDIAN != media_fmt->endianness) && (PCM_BIG_ENDIAN != media_fmt->endianness))
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "Check media fmt validity Fail endianness %d",
                    media_fmt->endianness);
      return FALSE;
   }

   if ((16 != media_fmt->bits_per_sample) && (24 != media_fmt->bits_per_sample) && (32 != media_fmt->bits_per_sample))
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "Check media fmt validity Fail sample_word_size %d",
                    media_fmt->bits_per_sample);
      return FALSE;
   }

   if ((PCM_LSB_ALIGNED != media_fmt->alignment) && (PCM_MSB_ALIGNED != media_fmt->alignment))
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "Check media fmt validity Fail alignment  %d ",
                    media_fmt->alignment);
      return FALSE;
   }
   PULL_PUSH_MSG(miid, DBG_HIGH_PRIO, "Check media fmt validity Pass");
   return TRUE;
}

void pull_push_mode_deinit(pull_push_mode_t *pm_ptr)
{
   if (NULL != pm_ptr)
   {
      // Release the shm by decrementing the ref count

      if (0 != pm_ptr->mem_map_client)
      {
         posal_memorymap_shm_decr_refcount(pm_ptr->mem_map_client, pm_ptr->pos_buf_mem_map_handle);
         posal_memorymap_shm_decr_refcount(pm_ptr->mem_map_client, pm_ptr->circ_buf_mem_map_handle);
         pm_ptr->pos_buf_mem_map_handle  = 0;
         pm_ptr->circ_buf_mem_map_handle = 0;
         pm_ptr->mem_map_client          = 0;
      }

      if (pm_ptr->num_water_mark_levels > 0)
      {
         posal_memory_free(pm_ptr->water_mark_levels_ptr);
      }
   }
}

/**
 * copies 1 ms or 5 ms worth of data into first module internal buffer.
 */
capi_err_t pull_mode_read_input(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t                               result         = CAPI_EOK;
   capi_pm_t                               *capi_ptr       = (capi_pm_t *)_pif;
   capi_buf_t                              *module_buf_ptr = (capi_buf_t *)(*output)->buf_ptr;
   pull_push_mode_t                        *me_ptr         = &(capi_ptr->pull_push_mode_info);
   sh_mem_pull_push_mode_position_buffer_t *pos_buf_ptr    = me_ptr->shared_pos_buf_ptr;
   int8_t                                  *read_ptr;
   uint32_t                                 miid = me_ptr->miid;

   // module is disabled if the module driver was not initilized properly.
   // or if the shared buffer configuration is not set
   if (capi_ptr->pull_push_mode_info.is_disabled)
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "Module disabled circ buf addr:0x%lx pos buf addr:0x%lx",
                    me_ptr->shared_circ_buf_start_ptr,
                    pos_buf_ptr);
      return CAPI_EFAILED;
   }

   // Share module buffer with the fwk only if the fwk doesnt provide the buffer.
   if (me_ptr->is_mod_buf_access_enabled && (NULL == module_buf_ptr[0].data_ptr))
   {
      /*updating position now indicating the frame is processsed */
      uint32_t curr_read_index = me_ptr->next_read_index;

      // in module buffer access mode, position buffer for the earlier read frame is updated in the next process
      // since the module buffer is expected to be held for an entire full.
      // todo: check if ok to raise event at this point. potentially hold the buffer for 1ms.
      if (me_ptr->curr_shared_buf_ptr)
      {
         pull_push_mode_check_send_watermark_event(capi_ptr, pos_buf_ptr->index, curr_read_index);
         if (curr_read_index == me_ptr->shared_circ_buf_size)
         {
            curr_read_index = 0;
         }

         pull_mode_update_pos_buffer(pos_buf_ptr, me_ptr->ist_priority, curr_read_index, posal_timer_get_time());
      }

      // safe mode checks for the if the buf size is aligned to the actual data len of the input buffer
      // circ buf size should be integral multiple of the input frame size else it wont work.
      // TODO:  we can do early validation and disable the extension if we know the frame size ahead as well.
      uint32_t bytes_available_to_output = me_ptr->shared_circ_buf_size - curr_read_index;
      if (module_buf_ptr[0].max_data_len > bytes_available_to_output)
      {
         PULL_PUSH_MSG(miid,
                       DBG_ERROR_PRIO,
                       "pull_mode_read_input: Invalid buf size %lu, cannot find a contiguous frame circular buf size"
                       "%lu and read offset %lu",
                       module_buf_ptr[0].max_data_len,
                       me_ptr->shared_circ_buf_size,
                       curr_read_index);

         // not assigning any buffer to the output
         me_ptr->curr_shared_buf_ptr = NULL;
         return CAPI_EFAILED;
      }

      read_ptr = (int8_t *)(me_ptr->shared_circ_buf_start_ptr + curr_read_index);

#ifndef DISABLE_CACHE_OPERATIONS
      if (CAPI_FAILED(result = posal_cache_invalidate_v2(&read_ptr, bytes_available_to_output)))
      {
         PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "pull_mode_read_input: Failure cache invalidate.");
         return result;
      }
#endif

      module_buf_ptr[0].data_ptr        = read_ptr;
      module_buf_ptr[0].actual_data_len = module_buf_ptr[0].max_data_len;

      // cache the source data buffer ptr shared with the fwk
      me_ptr->curr_shared_buf_ptr = (void *)read_ptr;
      me_ptr->next_read_index     = curr_read_index + module_buf_ptr[0].actual_data_len;

      /** important: Should update position buffer only when the fwk returns the buffer back
         to the module, else client will assume that frame processing is complete and may overwrite
         the data.*/
      return result;
   }

   /**
    ** Enters here only if the fwk extension is disabled
    **/

   if (NULL == module_buf_ptr[0].data_ptr)
   {
      return CAPI_EFAILED;
   }

   uint32_t   read_index = pos_buf_ptr->index, temp_rd_ind;
   uint32_t   rem_lin_size; // remaining linear size.
   capi_buf_t input_scratch_buf;
   uint32_t   bytes_to_copy_now      = 0;
   uint32_t   num_bytes_copied       = 0;
   module_buf_ptr[0].actual_data_len = 0;
   uint32_t num_channels             = me_ptr->media_fmt.num_channels;

   uint32_t word_size               = me_ptr->media_fmt.bits_per_sample;
   uint32_t empty_inp_bytes_per_buf = module_buf_ptr[0].max_data_len;
   uint32_t total_copy_size         = (CAPI_INTERLEAVED == me_ptr->media_fmt.data_interleaving)
                                         ? empty_inp_bytes_per_buf
                                         : empty_inp_bytes_per_buf * num_channels;

   while (1)
   {
      read_ptr          = (int8_t *)(me_ptr->shared_circ_buf_start_ptr + read_index);
      temp_rd_ind       = read_index;
      rem_lin_size      = me_ptr->shared_circ_buf_size - read_index;
      bytes_to_copy_now = MIN((total_copy_size - num_bytes_copied), rem_lin_size);

#ifndef DISABLE_CACHE_OPERATIONS
      if (CAPI_FAILED(result = posal_cache_invalidate_v2(&read_ptr, bytes_to_copy_now)))
      {
         PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "pull_mode_read_input: Failure cache invalidate.");
         return result;
      }
#endif

      if (CAPI_INTERLEAVED == me_ptr->media_fmt.data_interleaving)
      {
         // copy if framework provides an output buffer else populate the buffer pointer if extension is enabled
         memscpy((int8_t *)(module_buf_ptr[0].data_ptr + module_buf_ptr[0].actual_data_len),
                 module_buf_ptr[0].max_data_len - module_buf_ptr[0].actual_data_len,
                 (int8_t *)read_ptr,
                 bytes_to_copy_now);
         module_buf_ptr[0].actual_data_len +=
            MIN(module_buf_ptr[0].max_data_len - module_buf_ptr[0].actual_data_len, bytes_to_copy_now);
      }
      else
      {
         input_scratch_buf.max_data_len    = bytes_to_copy_now;
         input_scratch_buf.actual_data_len = bytes_to_copy_now;
         input_scratch_buf.data_ptr        = read_ptr;

         uint32_t filled_bytes_per_ch = module_buf_ptr[0].actual_data_len;
         uint32_t empty_bytes_per_ch  = module_buf_ptr[0].max_data_len - module_buf_ptr[0].actual_data_len;
         for (int32_t i = 0; i < num_channels; i++)
         {
            me_ptr->scratch_buf[i].data_ptr     = module_buf_ptr[i].data_ptr + filled_bytes_per_ch;
            me_ptr->scratch_buf[i].max_data_len = empty_bytes_per_ch;
         }

         if (CAPI_FAILED(result =
                            spf_intlv_to_deintlv(&input_scratch_buf, me_ptr->scratch_buf, num_channels, word_size)))
         {
            PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "pull_mode_read_input: Int - De-Int conversion failed");
            return result;
         }

         // update only first ch buffer len for unpacked v2.
         module_buf_ptr[0].actual_data_len += me_ptr->scratch_buf[0].actual_data_len;

         empty_inp_bytes_per_buf -= me_ptr->scratch_buf[0].actual_data_len;
      }

      num_bytes_copied += bytes_to_copy_now;
      read_index += bytes_to_copy_now;

      pull_push_mode_check_send_watermark_event(capi_ptr, temp_rd_ind, read_index);
      if (read_index == me_ptr->shared_circ_buf_size)
      {
         read_index = 0;
      }

      if (num_bytes_copied == total_copy_size)
      {
         break;
      }
   }

   /** update position buffer with new index*/
   pull_mode_update_pos_buffer(pos_buf_ptr, me_ptr->ist_priority, read_index, posal_timer_get_time());

   return result;
}

static capi_err_t push_mode_write_batch_header(capi_pm_t *me_ptr,
                                               uint32_t  *write_index_ptr,
                                               uint64_t   timestamp)
{
   capi_err_t result = CAPI_EOK;

   // Validate input parameters
   if (NULL == me_ptr || NULL == write_index_ptr)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Invalid parameters for header write");
      return CAPI_EBADPARAM;
   }

   uint32_t   miid   = me_ptr->pull_push_mode_info.miid;

   // Validate header buffer is allocated
   if (NULL == me_ptr->header_buffer_ptr || 0 == me_ptr->header_buffer_size)
   {
      PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "Header buffer not allocated");
      return CAPI_EFAILED;
   }

   // Use pre-allocated buffer
   uint8_t *local_header_buf_ptr = me_ptr->header_buffer_ptr;
   uint32_t max_size = me_ptr->header_buffer_size;
   uint32_t local_offset = 0;

   // Clear buffer
   memset(local_header_buf_ptr, 0, max_size);

   // Save the starting write index to calculate DDR addresses later
   uint32_t header_start_index = *write_index_ptr;

   // 1. Write Batch Header - Sync Word
   sh_mem_push_mode_batch_header_t *batch_hdr_ptr =
      (sh_mem_push_mode_batch_header_t *)&local_header_buf_ptr[local_offset];
   batch_hdr_ptr->sync_word = SH_MEM_PUSH_MODE_HEADER_SYNC_WORD;
   local_offset += sizeof(sh_mem_push_mode_batch_header_t);

   PULL_PUSH_MSG(miid, DBG_LOW_PRIO,
                 "Batch header: sync_word=0x%x", batch_hdr_ptr->sync_word);

   // 2. Write UTC Timestamp Parameter (if enabled)
   if (me_ptr->header_type_flags & HEADER_TYPE_UTC_TIMESTAMP)
   {
      // Write parameter header
      sh_mem_push_mode_param_header_t *ts_param_hdr_ptr =
         (sh_mem_push_mode_param_header_t *)&local_header_buf_ptr[local_offset];
      ts_param_hdr_ptr->param_id = PARAM_ID_HEADER_TYPE_UTC_TIMESTAMP;
      ts_param_hdr_ptr->actual_size = sizeof(sh_mem_push_mode_timestamp_payload_t);
      ts_param_hdr_ptr->padding_size = 0;  // No padding needed for timestamp
      local_offset += sizeof(sh_mem_push_mode_param_header_t);

      // Write timestamp payload
      sh_mem_push_mode_timestamp_payload_t *ts_payload_ptr = 
         (sh_mem_push_mode_timestamp_payload_t *)&local_header_buf_ptr[local_offset];

      uint32_t utc_ts_lsw = (uint32_t)(timestamp & 0xFFFFFFFF);
      uint32_t utc_ts_msw = (uint32_t)(timestamp >> 32);
      PULL_PUSH_MSG(miid, DBG_LOW_PRIO,
                    "PRE UTC timestamp: 0x%08x%08x",
                    utc_ts_msw,
                    utc_ts_lsw);

      posal_query_utc_time_from_nw((void *)me_ptr->ts_data.utc_time_module_ptr, TIME_USEC);

      posal_date_time_get_utc_time((void *)me_ptr->ts_data.utc_time_module_ptr, (posal_time)timestamp,TIME_USEC, &utc_ts_msw, &utc_ts_lsw);
      ts_payload_ptr->timestamp_us_lsw = utc_ts_lsw;
      ts_payload_ptr->timestamp_us_msw = utc_ts_msw;
      local_offset += sizeof(sh_mem_push_mode_timestamp_payload_t);

      PULL_PUSH_MSG(miid, DBG_LOW_PRIO,
                    "UTC timestamp: 0x%08x%08x",
                    ts_payload_ptr->timestamp_us_msw,
                    ts_payload_ptr->timestamp_us_lsw);
   }

   // 3. Write PCM Data Parameter Header (size will be updated later)
   sh_mem_push_mode_param_header_t *pcm_param_hdr_ptr =
      (sh_mem_push_mode_param_header_t *)&local_header_buf_ptr[local_offset];
   pcm_param_hdr_ptr->param_id = PARAM_ID_HEADER_TYPE_PCM_DATA;
   pcm_param_hdr_ptr->actual_size = 0;  // Will be updated when batch ends
   pcm_param_hdr_ptr->padding_size = 0; // Will be updated when batch ends


   uint32_t pcm_param_actual_size_offset = (uint8_t*)&pcm_param_hdr_ptr->actual_size - local_header_buf_ptr;
   uint32_t pcm_param_padding_size_offset = (uint8_t*)&pcm_param_hdr_ptr->padding_size - local_header_buf_ptr;

   local_offset += sizeof(sh_mem_push_mode_param_header_t);

   // Sanity check: ensure we didn't overflow the buffer
   if (local_offset > max_size)
   {
      PULL_PUSH_MSG(miid, DBG_ERROR_PRIO,
                    "Header size %d exceeds allocated size %d",
                    local_offset, max_size);
      return CAPI_EFAILED;
   }

   // 4. Write header to DDR with wrap-around handling
   pull_push_mode_t *pm_ptr = &me_ptr->pull_push_mode_info;
   uint32_t circ_buf_size = pm_ptr->shared_circ_buf_size;
   uint32_t bytes_to_write = local_offset;
   uint32_t current_write_index = *write_index_ptr;
   uint32_t bytes_written = 0;

   while (bytes_to_write)
   {
      // Calculate space available till end of buffer
      uint32_t rem_lin_size = circ_buf_size - current_write_index;
      uint32_t bytes_to_copy = MIN(bytes_to_write, rem_lin_size);

      // Copy to DDR
      memscpy(pm_ptr->shared_circ_buf_start_ptr + current_write_index,
              bytes_to_copy,
              &local_header_buf_ptr[bytes_written],
              bytes_to_copy);

      bytes_to_write -= bytes_to_copy;
      bytes_written  += bytes_to_copy;

      current_write_index = current_write_index + bytes_to_copy;
      if (current_write_index >= pm_ptr->shared_circ_buf_size)
      {
         current_write_index = 0;
      }
   }

   // 5. Calculate DDR indices with wrap-around
   uint32_t param_actual_size_sh_mem_index =
      (header_start_index + pcm_param_actual_size_offset) % circ_buf_size;
   me_ptr->pcm_param_actual_size_ptr =
      (uint32_t *)(pm_ptr->shared_circ_buf_start_ptr + param_actual_size_sh_mem_index);

   uint32_t param_padding_sh_mem_index =
      (header_start_index + pcm_param_padding_size_offset) % circ_buf_size;
   me_ptr->pcm_param_padding_size_ptr =
      (uint32_t *)(pm_ptr->shared_circ_buf_start_ptr + param_padding_sh_mem_index);


   PULL_PUSH_MSG(miid, DBG_HIGH_PRIO,
                 "Batch header written to DDR: size=%d bytes, start_index=%d, end_index=%d",
                 bytes_written, *write_index_ptr, current_write_index);

   PULL_PUSH_MSG(miid, DBG_LOW_PRIO,
              "SH MEM: actual_size=0x%lx (index=%d), padding_size=0x%lx (index=%d)",
              *me_ptr->pcm_param_actual_size_ptr, param_actual_size_sh_mem_index,
              *me_ptr->pcm_param_padding_size_ptr, param_padding_sh_mem_index);

   *write_index_ptr = current_write_index;
   me_ptr->batch_bytes_written = local_offset;

   return result;
}

capi_err_t push_mode_end_header_batch(capi_pm_t *_pif, uint64_t timestamp)
{
   capi_pm_t                               *capi_ptr       = (capi_pm_t *)_pif;
   pull_push_mode_t                        *me_ptr         = &(capi_ptr->pull_push_mode_info);
   uint32_t                                 rem_lin_size, temp_wr_ind, padding_bytes = 0;
   int8_t                                  *write_ptr      = NULL;
   sh_mem_pull_push_mode_position_buffer_t *pos_buf_ptr    = me_ptr->shared_pos_buf_ptr;
   uint32_t                                 miid = me_ptr->miid;

   // add padding bytes if write_index is not 4-byte aligned
   uint32_t misaligned_bytes = capi_ptr->pcm_bytes_written % BYTE_FOR_HEADER_ALIGNMENT;

   if(misaligned_bytes)
   {
      padding_bytes = BYTE_FOR_HEADER_ALIGNMENT - misaligned_bytes;

      rem_lin_size = me_ptr->shared_circ_buf_size - capi_ptr->batch_write_index;

      write_ptr = (int8_t *)(me_ptr->shared_circ_buf_start_ptr + capi_ptr->batch_write_index);
      if(padding_bytes <= rem_lin_size)
      {
         memset(write_ptr, 0, padding_bytes);
         capi_ptr->batch_write_index += padding_bytes;
      }
      else
      {
         PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "Padding bytes %d exceeds remaining linear memory %d, shared memory is not 4-byte aligned",
                                              padding_bytes, rem_lin_size);

         memset(write_ptr, 0, rem_lin_size);
         capi_ptr->batch_write_index += rem_lin_size;
      }

      if (capi_ptr->batch_write_index >= me_ptr->shared_circ_buf_size)
      {
         capi_ptr->batch_write_index = 0;
      }
   }


   // Update header size
   *capi_ptr->pcm_param_actual_size_ptr = capi_ptr->pcm_bytes_written;
   *capi_ptr->pcm_param_padding_size_ptr = padding_bytes;
   capi_ptr->batch_bytes_written += (capi_ptr->pcm_bytes_written + padding_bytes);

   temp_wr_ind = pos_buf_ptr->index;
   write_ptr   = (int8_t *)(me_ptr->shared_circ_buf_start_ptr + temp_wr_ind);
   rem_lin_size = me_ptr->shared_circ_buf_size - temp_wr_ind;

   //cache flush now, check for watermark event and update index (Handle the wrap around case here)
   if(capi_ptr->batch_bytes_written < rem_lin_size)
   {
#ifndef DISABLE_CACHE_OPERATIONS
      posal_cache_flush_v2(&write_ptr, capi_ptr->batch_bytes_written);
#endif
      pull_push_mode_check_send_watermark_event(capi_ptr, temp_wr_ind, capi_ptr->batch_write_index);
   }
   else
   {
#ifndef DISABLE_CACHE_OPERATIONS
      posal_cache_flush_v2(&write_ptr, rem_lin_size);
#endif
      pull_push_mode_check_send_watermark_event(capi_ptr, temp_wr_ind, me_ptr->shared_circ_buf_size);

      if(0 != capi_ptr->batch_write_index)
      {
         write_ptr   = (int8_t *)(me_ptr->shared_circ_buf_start_ptr);
         temp_wr_ind = 0;
#ifndef DISABLE_CACHE_OPERATIONS
         posal_cache_flush_v2(&write_ptr, capi_ptr->batch_bytes_written - rem_lin_size);
#endif
         pull_push_mode_check_send_watermark_event(capi_ptr, temp_wr_ind, capi_ptr->batch_write_index);
      }
   }

   /** update position buffer with new index*/
   pull_mode_update_pos_buffer(pos_buf_ptr, me_ptr->ist_priority, capi_ptr->batch_write_index , timestamp);

   PULL_PUSH_MSG(miid, DBG_HIGH_PRIO,
                 "Batch written actual bytes %lu, padding bytes %lu, next batch starts at index %d",
                 *capi_ptr->pcm_param_actual_size_ptr, *capi_ptr->pcm_param_padding_size_ptr, capi_ptr->batch_write_index);

   capi_ptr->is_update_header = TRUE;
   capi_ptr->pcm_bytes_written = 0;
   return CAPI_EOK;
}

capi_err_t push_mode_write_output(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t                               result = CAPI_EOK;
   uint32_t                                 rem_lin_size;
   int8_t                                  *write_ptr      = NULL;
   capi_pm_t                               *capi_ptr       = (capi_pm_t *)_pif;
   capi_buf_t                              *module_buf_ptr = (capi_buf_t *)(*input)->buf_ptr;
   pull_push_mode_t                        *me_ptr         = &(capi_ptr->pull_push_mode_info);
   sh_mem_pull_push_mode_position_buffer_t *pos_buf_ptr    = me_ptr->shared_pos_buf_ptr;
   capi_buf_t                               output_scratch_buf;
   uint32_t                                 miid = me_ptr->miid;

   // module is disabled if the module driver was not initilized properly.
   // or if the shared buffer configuration is not set
   // todo: raise disable event and disable module.
   if (capi_ptr->pull_push_mode_info.is_disabled)
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "Module disabled circ buf addr:0x%lx pos buf addr:0x%lx",
                    me_ptr->shared_circ_buf_start_ptr,
                    pos_buf_ptr);
      return CAPI_EFAILED;
   }
   
   // Deferred from algo reset: reset pos buffer now
   if (capi_ptr->is_pending_pos_buf_reset)
   {
      PULL_PUSH_MSG(miid, DBG_HIGH_PRIO, "Applying deferred position buffer reset before new write");
      if (NULL != me_ptr->shared_pos_buf_ptr) {
         memset(me_ptr->shared_pos_buf_ptr, 0, sizeof(sh_mem_pull_push_mode_position_buffer_t));
      }
      me_ptr->next_read_index            = 0;
      capi_ptr->is_pending_pos_buf_reset = FALSE;
   }

    // Track last valid timestamp for invalid timestamp handling
   if ((*input)->flags.is_timestamp_valid)
   {
      capi_ptr->last_valid_timestamp = (*input)->timestamp;
   }

   if (capi_ptr->is_header_enabled)
   {
      if (!(*input)->flags.is_timestamp_valid)
      {
         PULL_PUSH_MSG(miid, DBG_ERROR_PRIO,
                     "Invalid timestamp detected, dropping frame with %lu bytes",
                     module_buf_ptr[0].actual_data_len);

         if (!capi_ptr->is_update_header && (capi_ptr->pcm_bytes_written > 0))
         {
            PULL_PUSH_MSG(miid, DBG_HIGH_PRIO,
                        "Finalizing in-progress batch (pcm_bytes=%lu) before dropping invalid timestamp frame",
                        capi_ptr->pcm_bytes_written);
            push_mode_end_header_batch(capi_ptr, capi_ptr->last_valid_timestamp);
         }

         return CAPI_EOK;
      }
   }

   uint64_t timestamp = (*input)->flags.is_timestamp_valid ? (*input)->timestamp : posal_timer_get_time();
   uint32_t write_index = pos_buf_ptr->index, temp_wr_ind;

   // input data not is not available, nothing to do
   if (0 == module_buf_ptr[0].actual_data_len)
   {
      if (me_ptr->curr_shared_buf_ptr)
      {
         me_ptr->curr_shared_buf_ptr = NULL;
         module_buf_ptr[0].data_ptr  = NULL;
      }
   }
   else if (me_ptr->curr_shared_buf_ptr)
   {
      // Enters here if the module buffer access extension is enabled.

      write_ptr             = (int8_t *)(me_ptr->shared_circ_buf_start_ptr + write_index);
      uint32_t bytes_copied = module_buf_ptr[0].actual_data_len;

#ifdef PM_SAFE_MODE
      // check if fwk passed a diff buffer than shared by the module.
      if (module_buf_ptr[0].data_ptr != me_ptr->curr_shared_buf_ptr)
      {
         PULL_PUSH_MSG(miid,
                       DBG_ERROR_PRIO,
                       "push_mode_write_output: Either fwk has returned a diff buffer 0x%lX than shared frame ptr "
                       "0X%lx",
                       module_buf_ptr[0].data_ptr,
                       me_ptr->curr_shared_buf_ptr);
         return CAPI_EFAILED;
      }

      rem_lin_size  = me_ptr->shared_circ_buf_size - write_index;
      bytes_to_copy = MIN(module_buf_ptr[0].actual_data_len, rem_lin_size);
      if (bytes_to_copy < module_buf_ptr[0].actual_data_len)
      {
         PULL_PUSH_MSG(miid,
                       DBG_ERROR_PRIO,
                       "push_mode_write_output: current wr position %lu doesnt fit the input frame size %lu in circ "
                       "buf "
                       "size %lu",
                       write_index,
                       bytes_to_copy,
                       me_ptr->shared_circ_buf_size);
         return CAPI_EFAILED;
      }
#endif

      // check if the current write position doesnt align with the input buffer pointer
      // can happend only if there is some bug in the module
      if (write_ptr != module_buf_ptr[0].data_ptr)
      {
         PULL_PUSH_MSG(miid,
                       DBG_ERROR_PRIO,
                       "push_mode_write_output: input ptr 0x%lx is different than shared frame ptr 0X%lx ",
                       module_buf_ptr[0].data_ptr,
                       write_ptr);
         return CAPI_EFAILED;
      }

      // nothing to copy, data is already expected to be prepared by the fwk
      // reset the input buffer ptr to indicate that its freed by the module.
      // and prevents fwk from accessing further.
      me_ptr->curr_shared_buf_ptr = NULL;
      module_buf_ptr[0].data_ptr  = NULL;

#ifndef DISABLE_CACHE_OPERATIONS
      posal_cache_flush_v2(&write_ptr, bytes_copied);
#endif

      temp_wr_ind = write_index;
      write_index += bytes_copied;
      pull_push_mode_check_send_watermark_event(capi_ptr, temp_wr_ind, write_index);
      if (write_index >= me_ptr->shared_circ_buf_size)
      {
         write_index = 0;
      }
   }
   else // buffer access extension is disabled.
   {
      // Use batch_write_index for tracking within batch when headers enabled
      if (capi_ptr->is_header_enabled && 0 != module_buf_ptr[0].actual_data_len)
      {
         // Write batch header at the start of a new batch (when headers enabled and not in middle of batch)
         if (capi_ptr->is_update_header)
         {
            capi_ptr->pcm_bytes_written = 0;
            // Initialize batch tracking
            capi_ptr->batch_write_index = write_index;
            if (CAPI_FAILED(result = push_mode_write_batch_header(capi_ptr, &capi_ptr->batch_write_index, timestamp)))
            {
               PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "Failed to write batch header");
               return result;
            }

            capi_ptr->is_update_header = FALSE;

            PULL_PUSH_MSG(miid, DBG_HIGH_PRIO,
                          "Batch header written at index %d, PCM data starts at index %d",
                          write_index, capi_ptr->batch_write_index);
         }

         write_index = capi_ptr->batch_write_index;

      }

      uint32_t bytes_to_copy, bytes_copied = 0, bytes_copied_per_channel = 0, bytes_copied_per_channel_now = 0;
      while (module_buf_ptr[0].actual_data_len > 0)
      {
         write_ptr = (int8_t *)(me_ptr->shared_circ_buf_start_ptr + write_index);

         rem_lin_size = me_ptr->shared_circ_buf_size - write_index;

         if (CAPI_INTERLEAVED == me_ptr->media_fmt.data_interleaving)
         {
            bytes_to_copy = MIN(module_buf_ptr[0].actual_data_len, rem_lin_size);

            memscpy(write_ptr, bytes_to_copy, (module_buf_ptr[0].data_ptr + bytes_copied), bytes_to_copy);

#ifndef DISABLE_CACHE_OPERATIONS
            if (!capi_ptr->is_header_enabled)
            {
               posal_cache_flush_v2(&write_ptr, bytes_to_copy);
            }
#endif

            module_buf_ptr[0].actual_data_len -= bytes_to_copy;
            bytes_copied += bytes_to_copy;
         }
         else
         {
            uint32_t input_copy_size = (module_buf_ptr[0].actual_data_len * me_ptr->media_fmt.num_channels);
            bytes_to_copy            = MIN(input_copy_size, rem_lin_size);

            for (int32_t i = 0; i < me_ptr->media_fmt.num_channels; i++)
            {
               me_ptr->scratch_buf[i].data_ptr = module_buf_ptr[i].data_ptr + bytes_copied_per_channel;

               // all chs must have same len, as an optimization just access first ch lens only
               me_ptr->scratch_buf[i].actual_data_len = module_buf_ptr[0].actual_data_len;
            }

            output_scratch_buf.max_data_len    = rem_lin_size;
            output_scratch_buf.actual_data_len = 0;
            output_scratch_buf.data_ptr        = write_ptr;

            if (CAPI_FAILED(result = spf_deintlv_to_intlv(me_ptr->scratch_buf,
                                                          &output_scratch_buf,
                                                          me_ptr->media_fmt.num_channels,
                                                          me_ptr->media_fmt.bits_per_sample)))
            {
               PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "push_mode_write_output: De-Int - Int conversion failed");
               return result;
            }

#ifndef DISABLE_CACHE_OPERATIONS
            if (!capi_ptr->is_header_enabled)
            {
               posal_cache_flush_v2(&write_ptr, bytes_to_copy);
            }
#endif

            bytes_copied_per_channel_now = bytes_to_copy / me_ptr->media_fmt.num_channels;

            // update remaining bytes per ch, note that consumed bytes are updated later outside the while loop
            // for unpacked v2/interleaved data only first ch buffer lens need to be updated.
            module_buf_ptr[0].actual_data_len -= bytes_copied_per_channel_now;

            bytes_copied_per_channel += bytes_copied_per_channel_now;
         }

         // Only check watermark if headers disabled (will check at batch end if enabled)
         if (!capi_ptr->is_header_enabled)
         {
            temp_wr_ind = write_index;
            write_index += bytes_to_copy;
            pull_push_mode_check_send_watermark_event(capi_ptr, temp_wr_ind, write_index);
         }
         else
         {
            write_index += bytes_to_copy;
         }
         if (write_index >= me_ptr->shared_circ_buf_size)
         {
            write_index = 0;
         }
      }

      // in CAPI we need to report number of bytes consumed (when we return)
      if (CAPI_INTERLEAVED == me_ptr->media_fmt.data_interleaving)
      {
         module_buf_ptr[0].actual_data_len = bytes_copied;
         capi_ptr->pcm_bytes_written += bytes_copied;
      }
      else
      {
         // for unpacked v2 only first ch buffer lens need to be used.
         module_buf_ptr[0].actual_data_len = bytes_copied_per_channel;
         capi_ptr->pcm_bytes_written += (bytes_copied_per_channel * me_ptr->media_fmt.num_channels);
      }
   }

   if (!capi_ptr->is_header_enabled)
   {
      /** update position buffer with new index*/
      pull_mode_update_pos_buffer(pos_buf_ptr, me_ptr->ist_priority, write_index, timestamp);
   }
   else
   {
      capi_ptr->batch_write_index = write_index;
      capi_stream_data_v2_t *inputv2  = (capi_stream_data_v2_t *)input[0];
      bool_t is_batch_completed =  (input[0]->flags.marker_eos || input[0]->flags.end_of_frame) ? TRUE: FALSE;

      //check for dam batch marker md
      if (NULL != inputv2->metadata_list_ptr && !is_batch_completed)
      {
         module_cmn_md_list_t *node_ptr = inputv2->metadata_list_ptr;
         module_cmn_md_list_t *next_ptr = NULL;
         while (node_ptr)
         {
            module_cmn_md_t *      md_ptr              = (module_cmn_md_t *)node_ptr->obj_ptr;
            next_ptr = node_ptr->next_ptr;
            if (DAM_BATCH_END_MD_ID_MARKER == md_ptr->metadata_id)
            {
               is_batch_completed = TRUE;
               break;
            }

            node_ptr = next_ptr;
         }
      }

      if(is_batch_completed)
      {
         push_mode_end_header_batch(capi_ptr, timestamp);
      }
   }

   // raise EOS marker event
   if ((*input)->flags.marker_eos)
   {
      PULL_PUSH_MSG(miid, DBG_HIGH_PRIO, "Push module received marker EOS");
      event_sh_mem_push_mode_eos_marker_t payload;
      payload.index            = write_index;
      payload.timestamp_us_lsw = (uint32_t)timestamp;
      payload.timestamp_us_msw = (uint32_t)(timestamp >> 32);
      result                   = capi_pm_raise_event_to_clients(capi_ptr,
                                              EVENT_ID_SH_MEM_PUSH_MODE_EOS_MARKER,
                                              &payload,
                                              sizeof(event_sh_mem_push_mode_eos_marker_t));
      if (CAPI_FAILED(result))
      {
         PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "pm_check_send_eos_marker_event: Failed to send water mark event!");
      }
      else
      {
         PULL_PUSH_MSG(miid,
                       DBG_HIGH_PRIO,
                       "pm_check_send_eos_marker_event: Sent EOS Marker event to the client wrtie_index %lu, "
                       "timestamp_us_lsw "
                       "%lu, timestamp_us_msw %lu",
                       write_index,
                       (uint32_t)timestamp,
                       (uint32_t)(timestamp >> 32));
      }
   }

   return result;
}

capi_err_t pull_push_mode_check_send_watermark_event_util_(capi_pm_t *capi_ptr, uint32_t startLevel, uint32_t endLevel)
{
   capi_err_t        result = CAPI_EOK;
   pull_push_mode_t *pm_ptr = &(capi_ptr->pull_push_mode_info);

   uint32_t level;
   for (uint32_t i = 0; i < pm_ptr->num_water_mark_levels; i++)
   {
      level = pm_ptr->water_mark_levels_ptr[i].watermark_level_bytes;

      /** If a certain level is within start and end, then we are crossing the level, hence raise an event*/
      if ((level > startLevel) && (level <= endLevel))
      {
         // form the payload for the event
         event_sh_mem_pull_push_mode_watermark_level_t water_mark_event;
         water_mark_event.watermark_level_bytes = level;

         result = capi_pm_raise_event_to_clients(capi_ptr,
                                                 EVENT_ID_SH_MEM_PULL_PUSH_MODE_WATERMARK,
                                                 &water_mark_event,
                                                 sizeof(event_sh_mem_pull_push_mode_watermark_level_t));

         if (CAPI_FAILED(result))
         {
            PULL_PUSH_MSG(pm_ptr->miid,
                          DBG_ERROR_PRIO,
                          "pm_check_send_watermark_event: Failed to send water mark event!");
         }
         else
         {
            PULL_PUSH_MSG(pm_ptr->miid,
                          DBG_LOW_PRIO,
                          "pm_check_send_watermark_event: Sent Water Mark event to the client start %lu, level %lu, "
                          "end %lu",
                          startLevel,
                          level,
                          endLevel);
         }
      }
   }
   return result;
}

/** This is a callback function used by fwk to return the output buffer shared by module with the framework. */
capi_err_t pull_module_buf_mgr_extn_return_output_buf(uint32_t    handle,
                                                      uint32_t    port_index,
                                                      uint32_t   *num_bufs,
                                                      capi_buf_t *buffer_ptr)
{
   pull_push_mode_t *me_ptr = (pull_push_mode_t *)handle;
   if (buffer_ptr->data_ptr != me_ptr->curr_shared_buf_ptr)
   {
      PULL_PUSH_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "pull_module_buf_mgr_extn_return_output_buf: Trying to return a diff buffer 0x%lx expected 0x%lx",
                    buffer_ptr->data_ptr,
                    me_ptr->curr_shared_buf_ptr);
      return CAPI_EFAILED;
   }

   buffer_ptr->data_ptr = NULL;

   return CAPI_EOK;
}

/** This is a callback function used by fwk to get the input buffer from the module before calling modules process. */
capi_err_t push_module_buf_mgr_extn_get_input_buf(uint32_t    handle,
                                                  uint32_t    port_index,
                                                  uint32_t   *num_bufs,
                                                  capi_buf_t *buffer_ptr)
{
   pull_push_mode_t *me_ptr = (pull_push_mode_t *)handle;
   // is get is called only for input ports
   sh_mem_pull_push_mode_position_buffer_t *pos_buf_ptr = me_ptr->shared_pos_buf_ptr;
   if (me_ptr->curr_shared_buf_ptr)
   {
      PULL_PUSH_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "push_module_buf_mgr_extn_get_input_buf: Cannot query another buf without returning prev buffer "
                    "0x%lx",
                    me_ptr->curr_shared_buf_ptr);
      return CAPI_EFAILED;
   }

   uint32_t write_index = pos_buf_ptr->index;
   int8_t  *write_ptr   = (int8_t *)(me_ptr->shared_circ_buf_start_ptr + write_index);

   uint32_t available_contig_frame_size = me_ptr->shared_circ_buf_size - write_index;
   if (buffer_ptr->max_data_len > available_contig_frame_size)
   {
      PULL_PUSH_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "push_module_buf_mgr_extn_get_input_buf: Invalid buf size %lu, cannot find a contiguous frame "
                    "circular "
                    "buf "
                    "size %lu and wr offset %lu",
                    buffer_ptr->max_data_len,
                    me_ptr->shared_circ_buf_size,
                    write_index);
      return CAPI_EFAILED;
   }

   /** populate requested buffer ptr */
   buffer_ptr->data_ptr        = (int8_t *)write_ptr;
   buffer_ptr->actual_data_len = 0;
   me_ptr->curr_shared_buf_ptr = write_ptr;
   return CAPI_EOK;
}
