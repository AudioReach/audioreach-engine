/*______________________________________________________________________________________________________________________

 file pc_process.cpp
This file contains functions for compression-decompression container

   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear
______________________________________________________________________________________________________________________*/

/*______________________________________________________________________________________________________________________
INCLUDE FILES FOR MODULE
______________________________________________________________________________________________________________________*/

#include "pc_converter.h"

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   1. Calculates the memory required for storing
      a. Channel map data for input and output media formats
      b. storing remap capi buffers * number of channels for input output scratch buffer 1 and scratch buffer 2
      c. Channel mixer library
   2. Frees if any memory is allocated already. It is better to free and relloc instead of tracking.
   3. Assign memory to the corresponding pointers
______________________________________________________________________________________________________________________*/
ar_result_t pc_realloc_chmixer_memory(pc_lib_t *pc_ptr,
                                      uint32_t *ch_mixer_lib_size_ptr,
                                      uint16_t  max_num_of_channels,
                                      uint16_t  input_num_of_channels,
                                      uint16_t  output_num_of_channels,
                                      uint32_t  heap_id)
{
   ar_result_t         result           = AR_EOK;
   uint16_t *          channel_type_ptr = NULL;
   ChMixerStateStruct *ch_lib_ptr       = NULL;
   capi_buf_t *        buf_ptr          = NULL;
   uint8_t *           mem_ptr          = NULL;

   // Just need to allocate for the input and output, the proc media fmt can copy the channel map
   // from the input ptr or output ptr
   uint32_t lib_size = 0;
   ChMixerGetInstanceSize(&lib_size, input_num_of_channels, output_num_of_channels);
   uint32_t size_to_malloc = sizeof(uint16_t) * max_num_of_channels * (NUMBER_OF_CHANNEL_MAPS);
   size_to_malloc += sizeof(capi_buf_t) * max_num_of_channels * NUMBER_OF_REMAP_BUFFERS;
   size_to_malloc += CAPI_ALIGN_8_BYTE(lib_size);

   if (NULL != pc_ptr->core_lib.pc_mem_ptr)
   {
      posal_memory_aligned_free((void *)pc_ptr->core_lib.pc_mem_ptr);
      pc_ptr->core_lib.pc_mem_ptr = NULL;
   }

   pc_ptr->core_lib.pc_mem_ptr =
      (void *)posal_memory_aligned_malloc(size_to_malloc, MEM_ALIGN_EIGHT_BYTE, (POSAL_HEAP_ID)heap_id);
   if (NULL == pc_ptr->core_lib.pc_mem_ptr)
   {
      return AR_ENOMEMORY;
   }

   memset(pc_ptr->core_lib.pc_mem_ptr, 0, size_to_malloc);

   ch_lib_ptr         = (ChMixerStateStruct *)pc_ptr->core_lib.pc_mem_ptr;
   mem_ptr            = (uint8_t *)pc_ptr->core_lib.pc_mem_ptr;
   pc_ptr->ch_lib_ptr = ch_lib_ptr;

   *ch_mixer_lib_size_ptr = lib_size;
   mem_ptr += CAPI_ALIGN_8_BYTE(lib_size);
   channel_type_ptr = (uint16_t *)mem_ptr;

   pc_ptr->core_lib.input_media_fmt.channel_type = channel_type_ptr;
   channel_type_ptr += max_num_of_channels;

   pc_ptr->core_lib.output_media_fmt.channel_type = channel_type_ptr;
   channel_type_ptr += max_num_of_channels;

   buf_ptr = (capi_buf_t *)channel_type_ptr;

   pc_ptr->core_lib.remap_input_buf_ptr = buf_ptr;
   buf_ptr += max_num_of_channels;

   pc_ptr->core_lib.remap_scratch_buf1_ptr = buf_ptr;
   buf_ptr += max_num_of_channels;

   pc_ptr->core_lib.remap_scratch_buf2_ptr = buf_ptr;
   buf_ptr += max_num_of_channels;

   pc_ptr->core_lib.remap_output_buf_ptr = buf_ptr;

   pc_ptr->core_lib.max_num_channels = max_num_of_channels;
   return result;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   1. Check if sample rate is supported by iir resampler
______________________________________________________________________________________________________________________*/
ar_result_t pc_iir_rs_validate_sample_rate(pc_lib_t *pc_ptr, uint32_t sample_rate)
{
   if (NULL == pc_ptr)
   {
      return AR_EBADPARAM;
   }

   if ((IIR_RS_8K_SAMPLING_RATE != sample_rate) && (IIR_RS_16K_SAMPLING_RATE != sample_rate) &&
       (IIR_RS_24K_SAMPLING_RATE != sample_rate) && (IIR_RS_32K_SAMPLING_RATE != sample_rate) &&
       (IIR_RS_48K_SAMPLING_RATE != sample_rate) && (IIR_RS_96K_SAMPLING_RATE != sample_rate) &&
       (IIR_RS_192K_SAMPLING_RATE != sample_rate) && (IIR_RS_384K_SAMPLING_RATE != sample_rate))
   {
      return AR_EBADPARAM;
   }

   return AR_EOK;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   1. Check if frame size and input media format are set. If so, set frame size in samples per channel.
______________________________________________________________________________________________________________________*/
void pc_iir_rs_check_set_frame_size_samples(pc_lib_t *pc_ptr)
{
   // As frame_size is only needed for IIR resampler, we can use this iir_rs input media format
   // validity check to verify that input media format was initialized by this time.
   uint32_t in_sr  = pc_ptr->core_lib.input_media_fmt.sampling_rate;
   uint32_t out_sr = pc_ptr->core_lib.output_media_fmt.sampling_rate;
   if ((0 != pc_ptr->frame_size_us) && (AR_EOK == pc_iir_rs_validate_sample_rate(pc_ptr, in_sr)) &&
       (AR_EOK == pc_iir_rs_validate_sample_rate(pc_ptr, out_sr)))
   {
      pc_ptr->in_frame_samples_per_ch  = capi_cmn_us_to_samples(pc_ptr->frame_size_us, in_sr);
      pc_ptr->out_frame_samples_per_ch = (pc_ptr->in_frame_samples_per_ch * out_sr) / in_sr;

      CNV_MSG(pc_ptr->miid,
              DBG_HIGH_PRIO,
              "Frame size samples set to %ld input, %ld output",
              pc_ptr->in_frame_samples_per_ch,
              pc_ptr->out_frame_samples_per_ch);
   }
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   1. API call for capi to send the iir resampler library the frame size.
______________________________________________________________________________________________________________________*/
void pc_set_frame_size(pc_lib_t *pc_ptr, uint32_t frame_size_us)
{
   pc_ptr->frame_size_us = frame_size_us;
   pc_iir_rs_check_set_frame_size_samples(pc_ptr);

   CNV_MSG(pc_ptr->miid,
           DBG_HIGH_PRIO,
           "Cntr frame size set to %ld us, lib frame size %ld us",
           pc_ptr->cntr_frame_size_us,
           pc_ptr->frame_size_us);
}

void pc_set_cntr_frame_size(pc_lib_t *pc_ptr, uint32_t cntr_frame_size_us)
{
   pc_ptr->cntr_frame_size_us = cntr_frame_size_us;

   CNV_MSG(pc_ptr->miid,
           DBG_HIGH_PRIO,
           "Cntr frame size set to %ld us, lib frame size %ld us",
           pc_ptr->cntr_frame_size_us,
           pc_ptr->frame_size_us);

   // If operating at min frame size, then we return to cntr frame size at process call context.
   if (CNV_IIR_RS_MIN_FRAME_SIZE_US != pc_ptr->frame_size_us)
   {
      pc_set_frame_size(pc_ptr, pc_ptr->cntr_frame_size_us);
   }
}

void pc_set_consume_partial_input(pc_lib_t *pc_ptr, bool_t consume_partial_input)
{
   pc_ptr->consume_partial_input = consume_partial_input;

   // If exiting consume partial input, we return to cntr frame size at process call context.
   if (consume_partial_input)
   {
      pc_set_frame_size(pc_ptr, CNV_IIR_RS_MIN_FRAME_SIZE_US);
   }
}

ar_result_t pc_check_alloc_fir_rs(pc_lib_t *pc_ptr, uint32_t heap_id, bool_t *fir_rs_reinit_ptr)
{
#ifdef PCM_CNV_LIB_DEBUG
   CNV_MSG(pc_ptr->miid, DBG_HIGH_PRIO, "FIR resampler to be used, allocating memory");
#endif
   if (NULL == pc_ptr->hwsw_rs_lib_ptr)
   {
      pc_ptr->hwsw_rs_lib_ptr =
         (hwsw_resampler_lib_t *)posal_memory_malloc(sizeof(hwsw_resampler_lib_t), (POSAL_HEAP_ID)heap_id);
      if (NULL == pc_ptr->hwsw_rs_lib_ptr)
      {
         return AR_ENOMEMORY;
      }
      memset(pc_ptr->hwsw_rs_lib_ptr, 0, sizeof(hwsw_resampler_lib_t));
      hwsw_rs_lib_init(pc_ptr->hwsw_rs_lib_ptr);
   }

   // if it is fir resampler cache and check if any related config has changed
   hwsw_rs_lib_set_config(pc_ptr->use_hw_rs,
                          pc_ptr->dynamic_mode,
                          pc_ptr->delay_type,
                          pc_ptr->hwsw_rs_lib_ptr,
                          fir_rs_reinit_ptr);

   // free iir resampler if allocated
   if (pc_ptr->iir_rs_lib_ptr)
   {
      iir_rs_lib_deinit(pc_ptr->iir_rs_lib_ptr);
      posal_memory_free(pc_ptr->iir_rs_lib_ptr);
      pc_ptr->iir_rs_lib_ptr = NULL;
   }
   return AR_EOK;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   1. Allocates resampler lib ptr memory if resamplers are needed depending on resampler type
   2. If the other type of resampler exists, de-allocate it
   3. Does lib struct initialization
   4. When allocating for iir resampler, checks if current input output sample rates are supported
   5. Errors out, if iir resampler has unsupported sampling rates,
      does not fall back on dynamic resampler
   6. Falls back on fir resampler if iir preferred is set
   7. If resampling is not needed, frees memory of resampler
______________________________________________________________________________________________________________________*/
ar_result_t pc_check_alloc_resampler_memory(pc_lib_t *      pc_ptr,
                                            uint32_t        heap_id,
                                            bool_t *        fir_rs_reinit_ptr,
                                            bool_t *        fir_iir_rs_switch_ptr,
                                            pc_media_fmt_t *input_media_fmt_ptr,
                                            pc_media_fmt_t *output_media_fmt_ptr,
                                            bool_t          need_for_resampler)

{
   ar_result_t result = AR_EOK;
   // resampler is needed
   if (need_for_resampler)
   {
      // If FIR Resampler has been configured via api PARAM_ID_MFC_RESAMPLER_CFG
      if ((FIR_RESAMPLER == pc_ptr->resampler_type) && (!pc_ptr->iir_pref_set))
      {
         // Function will check if current is also sw fir and wont reinit if so
         pc_check_alloc_fir_rs(pc_ptr, heap_id, fir_rs_reinit_ptr);
      }
      /* IF IIR Pref or IIR resampler has been configured via api PARAM_ID_MFC_RESAMPLER_CFG*/
      else
      {
         result |= pc_iir_rs_validate_sample_rate(pc_ptr, input_media_fmt_ptr->sampling_rate);
         result |= pc_iir_rs_validate_sample_rate(pc_ptr, output_media_fmt_ptr->sampling_rate);

         // If SR validation fails
         if (AR_EOK != result)
         {
            if (!pc_ptr->iir_pref_set) // If IIR resampler was configured via api PARAM_ID_MFC_RESAMPLER_CFG
            {
               CNV_MSG(pc_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error! Disabling module, IIR resampler cannot support either input SR %d or output SR %d, IIR "
                       "pref flag %d ",
                       input_media_fmt_ptr->sampling_rate,
                       output_media_fmt_ptr->sampling_rate,
                       pc_ptr->iir_pref_set);

               // This will disable module in capi layer if result is FAILED
               return AR_EFAILED;
            }
            else // If iir pref rs was configured, since sample rate validation failed final rs should be fir
            {
               CNV_MSG(pc_ptr->miid,
                       DBG_HIGH_PRIO,
                       "IIR resampler cannot support input SR %d or output SR %d, changing resampler type from current "
                       "type:"
                       "%d(0: FIR, 1: IIR) to FIR rs, IIR pref flag %d",
                       input_media_fmt_ptr->sampling_rate,
                       output_media_fmt_ptr->sampling_rate,
                       pc_ptr->resampler_type,
                       pc_ptr->iir_pref_set);

               if (IIR_RESAMPLER == pc_ptr->resampler_type)
               {
                  // Need to set switch flag now, if it was wrongly determined as IIR in set param context
                  *fir_iir_rs_switch_ptr = TRUE;
               }

               // Set final rs type and init
               pc_ptr->resampler_type = FIR_RESAMPLER;
               return pc_check_alloc_fir_rs(pc_ptr, heap_id, fir_rs_reinit_ptr);
            }
         }

         // Sr validation didnt fail, need to set final rs to IIR
         if (FIR_RESAMPLER == pc_ptr->resampler_type)
         {
            CNV_MSG(pc_ptr->miid,
                    DBG_HIGH_PRIO,
                    "IIR resampler can be used for current sample rates. Changing from current "
                    "type %d(0: FIR, 1: IIR) to IIR, IIR pref flag %d",
                    pc_ptr->resampler_type,
                    pc_ptr->iir_pref_set);

            // Need to set switch flag now, if it was wrongly determined as IIR in set param context
            *fir_iir_rs_switch_ptr = TRUE;
         }
         // Then change the current type and init
         pc_ptr->resampler_type = IIR_RESAMPLER;

         if (NULL == pc_ptr->iir_rs_lib_ptr)
         {
#ifdef PCM_CNV_LIB_DEBUG
            CNV_MSG(pc_ptr->miid, DBG_HIGH_PRIO, "IIR resampler to be used, allocating memory");
#endif
            pc_ptr->iir_rs_lib_ptr = (iir_rs_lib_t *)posal_memory_malloc(sizeof(iir_rs_lib_t), (POSAL_HEAP_ID)heap_id);
            if (NULL == pc_ptr->iir_rs_lib_ptr)
            {
               return AR_ENOMEMORY;
            }

            memset(pc_ptr->iir_rs_lib_ptr, 0, sizeof(iir_rs_lib_t));

            // free fir resampler if allocated
            if (pc_ptr->hwsw_rs_lib_ptr)
            {
               hwsw_rs_lib_deinit(pc_ptr->hwsw_rs_lib_ptr);
               posal_memory_free(pc_ptr->hwsw_rs_lib_ptr);
               pc_ptr->hwsw_rs_lib_ptr = NULL;
            }
         }
      }
   }
   else // do not need a resampler
   {
      // free fir resampler if allocated
      if (pc_ptr->hwsw_rs_lib_ptr)
      {
         hwsw_rs_lib_deinit(pc_ptr->hwsw_rs_lib_ptr);
         posal_memory_free(pc_ptr->hwsw_rs_lib_ptr);
         pc_ptr->hwsw_rs_lib_ptr = NULL;
      }

      // free iir resampler if allocated
      if (pc_ptr->iir_rs_lib_ptr)
      {
         iir_rs_lib_deinit(pc_ptr->iir_rs_lib_ptr);
         posal_memory_free(pc_ptr->iir_rs_lib_ptr);
         pc_ptr->iir_rs_lib_ptr = NULL;
      }
   }

   return result;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Creates or reinits resamplers depending on config, sampling rates, num channels and bitwidth
   Can be sw fir, hw fir or iir
______________________________________________________________________________________________________________________*/
ar_result_t pc_check_create_resampler_instance(pc_lib_t *pc_ptr, uint32_t heap_id, bool_t fir_rs_reinit)
{
   ar_result_t result = AR_EOK;
   if ((pc_ptr->core_lib.flags.RESAMPLER_POST) || (pc_ptr->core_lib.flags.RESAMPLER_PRE))
   {
      if (FIR_RESAMPLER == pc_ptr->resampler_type)
      {
         // mf checks already done
         result = hwsw_rs_lib_check_create_resampler_instance(pc_ptr->hwsw_rs_lib_ptr, heap_id);
         if (result != AR_EOK)
         {
            CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "Resampler check create failed");
         }
      }
      else if (IIR_RESAMPLER == pc_ptr->resampler_type)
      {
         pc_media_fmt_t *rs_imf_ptr = NULL;
         if (TRUE == pc_ptr->core_lib.flags.RESAMPLER_PRE)
         {
            if (TRUE == pc_ptr->core_lib.flags.BYTE_CNV_PRE)
            {
               rs_imf_ptr = &pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt;
            }
            else
            {
               rs_imf_ptr = &pc_ptr->core_lib.input_media_fmt;
            }
         }
         else // if (TRUE == pc_ptr->core_lib.flags.RESAMPLER_POST) -> omitted for static analysis
         {
            if (TRUE == pc_ptr->core_lib.flags.CHANNEL_MIXER)
            {
               rs_imf_ptr = &pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].output_media_fmt;
            }
            else if (TRUE == pc_ptr->core_lib.flags.BYTE_CNV_PRE)
            {
               rs_imf_ptr = &pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt;
            }
            else
            {
               rs_imf_ptr = &pc_ptr->core_lib.input_media_fmt;
            }
         }

         CNV_MSG(pc_ptr->miid,
                 DBG_HIGH_PRIO,
                 "IIR IN sampling_rate %lu, out sampling_rate %lu, ch = %lu",
                 rs_imf_ptr->sampling_rate,
                 pc_ptr->core_lib.output_media_fmt.sampling_rate,
                 rs_imf_ptr->num_channels);

         // Setting channel malloc flag since iir resampler needs to re-alloc memory
         result = iir_rs_lib_allocate_memory(pc_ptr->iir_rs_lib_ptr,
                                             rs_imf_ptr->sampling_rate,
                                             pc_ptr->core_lib.output_media_fmt.sampling_rate,
                                             rs_imf_ptr->num_channels,
                                             rs_imf_ptr->bit_width,
                                             IIR_RS_FRAME_LEN_MAX_MS,
                                             heap_id);
         if (AR_EOK == result)
         {
            result = iir_rs_lib_clear_algo_memory(pc_ptr->iir_rs_lib_ptr);
            if (AR_EOK != result)
            {
               CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "Failed to init IIR resampler");
            }
         }
         else
         {
            CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "Failed to allocate memory for IIR resampler");
         }
      }
      else
      {
         // By this time we should already figure out whether to use FIR or IIR resampler for IIR preferred option
         CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "Unsupported resampler type %d", pc_ptr->resampler_type);
         return AR_EUNSUPPORTED;
      }
   }

   return result;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Calculates expected input samples for a given number of out samples
______________________________________________________________________________________________________________________*/

uint32_t pc_get_fixed_out_samples(pc_lib_t *pc_ptr, uint32_t req_out_samples)
{
   if ((NULL != pc_ptr->hwsw_rs_lib_ptr->sw_rs_mem_ptr[STAGE_ZERO]) &&
       (!pc_ptr->hwsw_rs_lib_ptr->sw_rs_mem_ptr[STAGE_ZERO]->drs_mem_ptr.pStructMem))
   {
      CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "sw resampler struct is NULL");
      return 0;
   }

   uint32_t expected_in_samples;
   if (pc_ptr->hwsw_rs_lib_ptr->is_multi_stage_process)
   {
      uint32_t expected_in_samples_intm =
         resamp_calc_fixedout(pc_ptr->hwsw_rs_lib_ptr->sw_rs_mem_ptr[STAGE_ONE]->drs_mem_ptr.pStructMem,
                              req_out_samples);
      expected_in_samples =
         resamp_calc_fixedout(pc_ptr->hwsw_rs_lib_ptr->sw_rs_mem_ptr[STAGE_ZERO]->drs_mem_ptr.pStructMem,
                              expected_in_samples_intm);
   }
   else
   {
      expected_in_samples =
         resamp_calc_fixedout(pc_ptr->hwsw_rs_lib_ptr->sw_rs_mem_ptr[STAGE_ZERO]->drs_mem_ptr.pStructMem,
                              req_out_samples);
   }

   return expected_in_samples;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Checks if reordering is needed
   1. Whenever update_rs_config is set, resampler will reinit and cause glitch, hence reordering is allowed
   2. Else
      a. If sw resampler is being used in dynamic mode and sampling rate changes, we don't want to reorder
         and cause a glitch
      b. If pre resampler is used and input num ch remains same, don't reorder since out num ch for resampler won't
change
      c. If post resampler is used and output num ch remains same, don't reorder since inp num ch for resampler won't
change
______________________________________________________________________________________________________________________*/
bool_t pc_is_reorder_allowed(pc_lib_t *      pc_ptr,
                             pc_media_fmt_t *input_media_fmt_ptr,
                             pc_media_fmt_t *output_media_fmt_ptr,
                             bool_t          fir_iir_rs_switch,
                             bool_t          fir_rs_reinit)
{
   if (fir_iir_rs_switch || fir_rs_reinit)
   {
      return TRUE;
   }
   else if ((((FIR_RESAMPLER == pc_ptr->resampler_type) &&
              ((pc_ptr->hwsw_rs_lib_ptr) && (SW_RESAMPLER == pc_ptr->hwsw_rs_lib_ptr->this_resampler_instance_using) &&
               (pc_ptr->hwsw_rs_lib_ptr->sw_rs_config_param.dynamic_mode) &&
               (pc_ptr->core_lib.input_media_fmt.sampling_rate != input_media_fmt_ptr->sampling_rate)))) ||
            ((RESAMPLER_POSITION_PRE == pc_ptr->resampler_used) &&
             (input_media_fmt_ptr->num_channels == pc_ptr->core_lib.input_media_fmt.num_channels)) ||
            ((RESAMPLER_POSITION_POST == pc_ptr->resampler_used) &&
             (output_media_fmt_ptr->num_channels == pc_ptr->core_lib.output_media_fmt.num_channels)))
   {
      return FALSE;
   }

   return TRUE;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Copies the media format and checks if reorder is allowed
   If resampler config is updated there will be a re-init so can skip the reorder check
______________________________________________________________________________________________________________________*/
void pc_copy_media_fmt(pc_lib_t *pc_ptr, pc_media_fmt_t *input_media_fmt_ptr, pc_media_fmt_t *output_media_fmt_ptr)
{
   pc_ptr->core_lib.input_media_fmt.alignment     = input_media_fmt_ptr->alignment;
   pc_ptr->core_lib.input_media_fmt.bit_width     = input_media_fmt_ptr->bit_width;
   pc_ptr->core_lib.input_media_fmt.byte_combo    = input_media_fmt_ptr->byte_combo;
   pc_ptr->core_lib.input_media_fmt.endianness    = input_media_fmt_ptr->endianness;
   pc_ptr->core_lib.input_media_fmt.interleaving  = input_media_fmt_ptr->interleaving;
   pc_ptr->core_lib.input_media_fmt.num_channels  = input_media_fmt_ptr->num_channels;
   pc_ptr->core_lib.input_media_fmt.q_factor      = input_media_fmt_ptr->q_factor;
   pc_ptr->core_lib.input_media_fmt.word_size     = input_media_fmt_ptr->word_size;
   pc_ptr->core_lib.input_media_fmt.sampling_rate = input_media_fmt_ptr->sampling_rate;
   pc_ptr->core_lib.input_media_fmt.data_format   = input_media_fmt_ptr->data_format;

   for (uint32_t ch = 0; ch < pc_ptr->core_lib.input_media_fmt.num_channels; ch++)
   {
      pc_ptr->core_lib.input_media_fmt.channel_type[ch] = input_media_fmt_ptr->channel_type[ch];
   }

   pc_ptr->core_lib.output_media_fmt.alignment     = output_media_fmt_ptr->alignment;
   pc_ptr->core_lib.output_media_fmt.bit_width     = output_media_fmt_ptr->bit_width;
   pc_ptr->core_lib.output_media_fmt.byte_combo    = output_media_fmt_ptr->byte_combo;
   pc_ptr->core_lib.output_media_fmt.endianness    = output_media_fmt_ptr->endianness;
   pc_ptr->core_lib.output_media_fmt.interleaving  = output_media_fmt_ptr->interleaving;
   pc_ptr->core_lib.output_media_fmt.num_channels  = output_media_fmt_ptr->num_channels;
   pc_ptr->core_lib.output_media_fmt.q_factor      = output_media_fmt_ptr->q_factor;
   pc_ptr->core_lib.output_media_fmt.word_size     = output_media_fmt_ptr->word_size;
   pc_ptr->core_lib.output_media_fmt.sampling_rate = output_media_fmt_ptr->sampling_rate;
   pc_ptr->core_lib.output_media_fmt.data_format   = output_media_fmt_ptr->data_format;

   for (uint32_t ch = 0; ch < pc_ptr->core_lib.output_media_fmt.num_channels; ch++)
   {
      pc_ptr->core_lib.output_media_fmt.channel_type[ch] = output_media_fmt_ptr->channel_type[ch];
   }

   pc_iir_rs_check_set_frame_size_samples(pc_ptr);
}

/* When the inp/out data format is different and either one of the input or out byte combo is PC_BW32_W32_Q31,
it translates to a data format conversion ONLY. In such cases byte conv should not be enabled. */
bool_t pc_byte_combo_change_handled_by_dfc(pc_mf_combo_t input_byte_combo, pc_mf_combo_t output_byte_combo)
{
   if (((PC_BW32_W32_FLOAT == input_byte_combo) && (PC_BW32_W32_Q31 == output_byte_combo)) ||
       ((PC_BW32_W32_FLOAT == output_byte_combo) && (PC_BW32_W32_Q31 == input_byte_combo)) ||
       ((PC_BW64_W64_DOUBLE == input_byte_combo) && (PC_BW32_W32_Q31 == output_byte_combo)) ||
       ((PC_BW64_W64_DOUBLE == output_byte_combo) && (PC_BW32_W32_Q31 == input_byte_combo)))
   {
      return TRUE;
   }

   return FALSE;
}
/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   ALGORITHM to create the process pipeline
   CH - channel mixer, B1/B2 - byte converters, I1/I2 - interleaving/deinterleaving, E - endianness
   I - Interleaved
   D - Deinterleaved
   Vchi - Valid ch-mixer byte format at the input
   Vcho- Valid ch-mixer byte format at the output
   Chi - number channels at input
   Cho - number of channels at output

   1. Enable/disable E1/E2
      a. If Inp is BE -> enable E1
      b. If out is BE -> enable E2
   2. Find if CH is needed
   3. Based on CH and I info, find if I1 or/and I2 are needed
      a. If CH is not needed
         i. if input is I and output is D - > use I1 to convert from I to D
         ii. else, if input is D and output is I - >  use I2 to convert from D to I
         iii. for I to I, do nothing, for D to D do nothing
      b. If CH is needed
         i. for I to I, enable both I1(does I to D) and I2(does D to I)
         ii. For D to D, do nothing
         iii. for I to D and D to I follow same approach as id CH is not present
   4. If IIR resampler is needed and BW is not 16, use B1 to convert to BW16 and B2 to convert to out BW.
   5. If Ch is not needed
      a. enable B1 based on B
   6. If Ch is needed
      a. Enable B1 and B2 If !Vchi and !Vcho
      b. If just Vchi
         i. Don’t care if Byte conversion is not needed
         ii. B2 if needed
      c. If just Vcho
         i. Don’t care if byte conversion is not needed
         ii. B1 if needed
      d. If both Vcho and Vchi
         i. If byte conversion is TRUE
            1) B1 if Chi < Cho
            2) B2 if Chi >= Cho
   7. If resampler is needed
      a. reorder is allowed
          i. channel in >= channel out covers no ch mix needed case also
           ii. channel in < channel out
        b. reorder not allowed, use previous position
   8. Now we know the values of I1, B1, R1, Ch, R2, B2, I2 . Make sure to record the correct values of conversion during
the
above phase
______________________________________________________________________________________________________________________*/
static void pc_identify_required_processes(pc_lib_t *      pc_ptr,
                                           pc_media_fmt_t *input_media_fmt_ptr,
                                           pc_media_fmt_t *output_media_fmt_ptr,
                                           bool_t *        need_for_resampler_ptr,
                                           bool_t *        enable_for_deint_packed_unpacked_cnv,
                                           int16_t *       coef_set_ptr,
                                           uint8_t         type)
{
   *need_for_resampler_ptr               = FALSE;
   *enable_for_deint_packed_unpacked_cnv = FALSE;
   // set the flags to zero before assigning values to them. This should not be done in pc_realloc_memory as flags will
   // already be set
   pc_ptr->core_lib.flags.word = 0;
   if ((input_media_fmt_ptr->sampling_rate != output_media_fmt_ptr->sampling_rate) ||
       ((pc_ptr->dynamic_mode) && (pc_ptr->resampler_type == FIR_RESAMPLER)))
   {
      *need_for_resampler_ptr = TRUE;
   }

   if (PC_BIG_ENDIAN == input_media_fmt_ptr->endianness)
   {
      pc_ptr->core_lib.flags.ENDIANNESS_PRE = TRUE;
   }
   else
   {
      if (PC_BIG_ENDIAN == output_media_fmt_ptr->endianness)
      {
         pc_ptr->core_lib.flags.ENDIANNESS_POST = TRUE;
      }
   }

   pc_ptr->core_lib.flags.CHANNEL_MIXER =
      pc_is_ch_mixer_needed(pc_ptr, coef_set_ptr, input_media_fmt_ptr, output_media_fmt_ptr);
   if (pc_ptr->core_lib.flags.CHANNEL_MIXER)
   {
      if (PC_INTERLEAVED == input_media_fmt_ptr->interleaving)
      {
         pc_ptr->core_lib.flags.INT_DEINT_PRE = TRUE;
      }
      if (PC_INTERLEAVED == output_media_fmt_ptr->interleaving)
      {
         pc_ptr->core_lib.flags.INT_DEINT_POST = TRUE;
      }
   }
   // if the num of channels is 1 and type is mfc and pcm converter we need to skip enablng the interleaving flags
   // needed when we do not need ch mixer
   else if (!((input_media_fmt_ptr->num_channels == 1) && ((type == MFC) || (type == PCM_CNV))))
   {
      if ((PC_INTERLEAVED == input_media_fmt_ptr->interleaving) &&
          (PC_INTERLEAVED != output_media_fmt_ptr->interleaving))
      {
         pc_ptr->core_lib.flags.INT_DEINT_PRE = TRUE;
      }
      if ((PC_INTERLEAVED != input_media_fmt_ptr->interleaving) &&
          (PC_INTERLEAVED == output_media_fmt_ptr->interleaving))
      {
         pc_ptr->core_lib.flags.INT_DEINT_POST = TRUE;
      }
      // If num channels are not equal module will anyway be enabled due to channel mixer check
      // If num channels are equal and inp and out interleaving conv is needed for deint packed to deint unpacked or
      // vice versa
      // we need to keep the module enabled. Pack-unpack conv happens through buffer sizing and not by enabling the I1
      // and I2 processes in the pcm cnv
      // If num ch is 1, fwk takes care of propagating interleaving flag to downstream modules and pcm cnv/mfc can be
      // bypassed
      if ((input_media_fmt_ptr->interleaving != output_media_fmt_ptr->interleaving) &&
          (PC_INTERLEAVED != input_media_fmt_ptr->interleaving) &&
          (PC_INTERLEAVED != output_media_fmt_ptr->interleaving))
      {
         *enable_for_deint_packed_unpacked_cnv = TRUE;
      }
   }
   if (input_media_fmt_ptr->byte_combo == output_media_fmt_ptr->byte_combo)
   {
      if (((TRUE == pc_ptr->core_lib.flags.CHANNEL_MIXER) || (TRUE == *need_for_resampler_ptr)))
      {
         // Up conversion is needed
         if (PC_BW24_W24_Q23 == input_media_fmt_ptr->byte_combo)
         {
            pc_ptr->core_lib.flags.BYTE_CNV_PRE  = TRUE;
            pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
         }
         // Down conversion is needed
         if ((IIR_RESAMPLER == pc_ptr->resampler_type) && (PC_BW16_W16_Q15 != input_media_fmt_ptr->byte_combo))
         {
            pc_ptr->core_lib.flags.BYTE_CNV_PRE  = TRUE;
            pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
         }
      }
   }
   /* else if case translates to byte combo being different.
    * When we convert for certain fixed to float formats or vice versa, "byte_combo" notation in pcm converter is
    * different
    * but it actually translates to a data format conversion ONLY. In such cases byte conv should not be enabled. */
   else if (!pc_byte_combo_change_handled_by_dfc(input_media_fmt_ptr->byte_combo, output_media_fmt_ptr->byte_combo))
   {
      if ((*need_for_resampler_ptr) && (IIR_RESAMPLER == pc_ptr->resampler_type))
      {
         if (PC_BW16_W16_Q15 != input_media_fmt_ptr->byte_combo)
         {
            pc_ptr->core_lib.flags.BYTE_CNV_PRE = TRUE;
         }

         if (PC_BW16_W16_Q15 != output_media_fmt_ptr->byte_combo)
         {
            pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
         }
      }
      else if (FALSE == pc_ptr->core_lib.flags.CHANNEL_MIXER)
      {
         if (input_media_fmt_ptr->sampling_rate >= output_media_fmt_ptr->sampling_rate)
         {
            pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
         }
         else
         {
            pc_ptr->core_lib.flags.BYTE_CNV_PRE = TRUE;
         }
      }
      else
      {
         if (input_media_fmt_ptr->num_channels >= output_media_fmt_ptr->num_channels)
         {
            if ((PC_BW24_W24_Q23 != input_media_fmt_ptr->byte_combo) &&
                (PC_BW24_W24_Q23 != output_media_fmt_ptr->byte_combo))
            {
               pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
            }
            else if ((PC_BW24_W24_Q23 != input_media_fmt_ptr->byte_combo) &&
                     (PC_BW24_W24_Q23 == output_media_fmt_ptr->byte_combo))
            {
               pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
            }
            else if ((PC_BW24_W24_Q23 == input_media_fmt_ptr->byte_combo) &&
                     (PC_BW24_W24_Q23 != output_media_fmt_ptr->byte_combo))
            {
               pc_ptr->core_lib.flags.BYTE_CNV_PRE = TRUE;
            }
            else
            {
               pc_ptr->core_lib.flags.BYTE_CNV_PRE  = TRUE;
               pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
            }
         }
         else
         {
            if ((PC_BW24_W24_Q23 != input_media_fmt_ptr->byte_combo) &&
                (PC_BW24_W24_Q23 != output_media_fmt_ptr->byte_combo))
            {
               pc_ptr->core_lib.flags.BYTE_CNV_PRE = TRUE;
            }
            else if ((PC_BW24_W24_Q23 != input_media_fmt_ptr->byte_combo) &&
                     (PC_BW24_W24_Q23 == output_media_fmt_ptr->byte_combo))
            {
               pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
            }
            else if ((PC_BW24_W24_Q23 == input_media_fmt_ptr->byte_combo) &&
                     (PC_BW24_W24_Q23 != output_media_fmt_ptr->byte_combo))
            {
               pc_ptr->core_lib.flags.BYTE_CNV_PRE = TRUE;
            }
            else
            {
               pc_ptr->core_lib.flags.BYTE_CNV_PRE  = TRUE;
               pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
            }
         }
      }
   }

   if (input_media_fmt_ptr->data_format != output_media_fmt_ptr->data_format)
   {
      if (PC_FLOATING_FORMAT == input_media_fmt_ptr->data_format)
      {
         pc_ptr->core_lib.flags.DATA_CNV_FLOAT_TO_FIXED = TRUE;
      }
      else if (PC_FIXED_FORMAT == input_media_fmt_ptr->data_format)
      {
         pc_ptr->core_lib.flags.DATA_CNV_FIXED_TO_FLOAT = TRUE;
      }
   }
   return;
}

void pc_update_resampler_pos_and_buff_stages(pc_lib_t *pc_ptr, bool_t need_for_resampler, bool_t reorder_allowed)
{
   if ((need_for_resampler) && (reorder_allowed))
   {
      CNV_MSG(pc_ptr->miid, DBG_HIGH_PRIO, "MFC can get reordered");
      if (pc_ptr->core_lib.input_media_fmt.num_channels >= pc_ptr->core_lib.output_media_fmt.num_channels)
      {
         pc_ptr->core_lib.flags.RESAMPLER_POST = TRUE;
         pc_ptr->resampler_used                = RESAMPLER_POSITION_POST;
      }
      else
      {
         pc_ptr->core_lib.flags.RESAMPLER_PRE = TRUE;
         pc_ptr->resampler_used               = RESAMPLER_POSITION_PRE;
      }
   }
   else if ((need_for_resampler) && (!reorder_allowed)) // no re-ordering
   {
      if (RESAMPLER_POSITION_PRE == pc_ptr->resampler_used)
      {
         pc_ptr->core_lib.flags.RESAMPLER_PRE = TRUE;
      }
      else
      {
         pc_ptr->core_lib.flags.RESAMPLER_POST = TRUE;
      }
   }

   pc_ptr->core_lib.no_of_buffering_stages =
      pc_ptr->core_lib.flags.DATA_CNV_FLOAT_TO_FIXED + pc_ptr->core_lib.flags.DATA_CNV_FIXED_TO_FLOAT +
      pc_ptr->core_lib.flags.INT_DEINT_PRE + pc_ptr->core_lib.flags.INT_DEINT_POST +
      pc_ptr->core_lib.flags.BYTE_CNV_PRE + pc_ptr->core_lib.flags.BYTE_CNV_POST +
      pc_ptr->core_lib.flags.RESAMPLER_PRE + pc_ptr->core_lib.flags.RESAMPLER_POST +
      pc_ptr->core_lib.flags.CHANNEL_MIXER;

#ifdef PCM_CNV_LIB_DEBUG
   CNV_MSG(pc_ptr->miid,
           DBG_HIGH_PRIO,
           "Required Pre-Processes, E1-%lu, I1-%lu, B1-%lu, R1-%lu, C-%lu",
           pc_ptr->core_lib.flags.ENDIANNESS_PRE,
           pc_ptr->core_lib.flags.INT_DEINT_PRE,
           pc_ptr->core_lib.flags.BYTE_CNV_PRE,
           pc_ptr->core_lib.flags.RESAMPLER_PRE,
           pc_ptr->core_lib.flags.CHANNEL_MIXER);

   CNV_MSG(pc_ptr->miid,
           DBG_HIGH_PRIO,
           "Required Post-Processes , R2-%lu, B2-%lu, I2-%lu, E2-%lu ",
           pc_ptr->core_lib.flags.RESAMPLER_POST,
           pc_ptr->core_lib.flags.BYTE_CNV_POST,
           pc_ptr->core_lib.flags.INT_DEINT_POST,
           pc_ptr->core_lib.flags.ENDIANNESS_POST);
#endif
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Sets lib mf for hw sw resampler lib
______________________________________________________________________________________________________________________*/
void pc_fill_fir_resampler_lib_mf(pc_lib_t *pc_ptr, pc_media_fmt_t *inp_mf, uint32_t out_sample_rate)
{
   hwsw_rs_media_fmt_t rs_lib_media_fmt;
   rs_lib_media_fmt.bits_per_sample    = inp_mf->bit_width;
   rs_lib_media_fmt.inp_sample_rate    = inp_mf->sampling_rate;
   rs_lib_media_fmt.num_channels       = inp_mf->num_channels;
   rs_lib_media_fmt.output_sample_rate = out_sample_rate;
   rs_lib_media_fmt.q_factor           = inp_mf->q_factor;
   if (NULL == pc_ptr->hwsw_rs_lib_ptr)
   {
      CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "hwsw_rs_lib_ptr is NULL");
   }
   else
   {
      hwsw_rs_set_lib_mf(pc_ptr->hwsw_rs_lib_ptr, &rs_lib_media_fmt);
   }
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Check pc_identify_required_processes function before reading this
   NBS -> number of buffering stages
   1. For E1. if E1 is enabled, and NBS = 0, make output buffer = fw output buffer else, do in-place
   2. For I1. If it enabled, it is converting from D to I, so hard code the value.
   3. For B1. if both B1 and B2 are enabled -> B1 should up-convert to support ch-mixer bit-width,
      Else, take the out media fmt from the main output media format
   4. For R1, update output sampling rate and depending on type of resampler call process function
   5. For CH mixer, take media fmt from main output media fmt
   6. For R2, update output sampling rate and depending on type of resampler call process function
   7. For B1, take media fmt from main output media fmt
   8. For I2, if enabled, it does D to I. get the media fmt from output anyway
   9. For E2, output buffer is always fw output buffer
______________________________________________________________________________________________________________________*/
void pc_fill_proc_info(pc_lib_t *pc_ptr)
{
   capi_buf_t *buffers[MAX_NUMBER_OF_BUFFERING_PROCESS] = { &pc_ptr->core_lib.remap_scratch_buf1_ptr[0],
                                                            &pc_ptr->core_lib.remap_scratch_buf2_ptr[0],
                                                            &pc_ptr->core_lib.remap_scratch_buf1_ptr[0],
                                                            &pc_ptr->core_lib.remap_scratch_buf2_ptr[0],
                                                            &pc_ptr->core_lib.remap_scratch_buf1_ptr[0],
                                                            &pc_ptr->core_lib.remap_scratch_buf2_ptr[0],
                                                            &pc_ptr->core_lib.remap_output_buf_ptr[0] };

   uint32_t current_index = (MAX_NUMBER_OF_BUFFERING_PROCESS)-pc_ptr->core_lib.no_of_buffering_stages;

   if (MAX_NUMBER_OF_BUFFERING_PROCESS <= current_index)
   {
      CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "Invalid current index %d", current_index);
      return;
   }

   if ((pc_ptr->core_lib.flags.RESAMPLER_POST) && (pc_ptr->core_lib.flags.RESAMPLER_PRE))
   {
      CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "Both resamplers cannot be enabled");
      return;
   }

   pc_media_fmt_t *current_output_media_fmt_ptr = &pc_ptr->core_lib.input_media_fmt;

   if (pc_ptr->core_lib.flags.ENDIANNESS_PRE)
   {
      pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].process = pc_endianness_process;
      if (0 == pc_ptr->core_lib.no_of_buffering_stages)
      {
         pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_buffer_ptr = pc_ptr->core_lib.remap_output_buf_ptr;
      }
      else
      {
         pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_buffer_ptr = pc_ptr->core_lib.remap_input_buf_ptr;
      }

      pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_media_fmt            = pc_ptr->core_lib.input_media_fmt;
      pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_media_fmt.endianness = PC_LITTLE_ENDIAN;
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_media_fmt;
   }

   /* The output Q factor for float to fixed conversion is assumed to be 31 word size =32 and if the required output q
    factor of pc converter is different, byte morph process would take care of that*/
   if ((pc_ptr->core_lib.flags.DATA_CNV_FLOAT_TO_FIXED) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].process           = pc_float_to_fixed_conv_process;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt  = *current_output_media_fmt_ptr;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt.data_format = PC_FIXED_FORMAT;

      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt.q_factor   = 31;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt.word_size  = 32;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt.bit_width  = 32;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt.byte_combo = PC_BW32_W32_Q31;
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.INT_DEINT_PRE) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].process                       = pc_interleaving_process;
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_buffer_ptr             = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_media_fmt              = *current_output_media_fmt_ptr;
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_media_fmt.interleaving = PC_DEINTERLEAVED_UNPACKED_V2;
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.BYTE_CNV_PRE) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].process           = pc_byte_morph_process;
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt  = *current_output_media_fmt_ptr;

      // If IIR resampler is enabled, byte converter pre should convert to 16bit format
      if ((pc_ptr->core_lib.flags.RESAMPLER_PRE || pc_ptr->core_lib.flags.RESAMPLER_POST) &&
          (IIR_RESAMPLER == pc_ptr->resampler_type))
      {
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.bit_width  = 16;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.word_size  = 16;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.q_factor   = 15;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.byte_combo = PC_BW16_W16_Q15;
      }
      else
      {
         if (pc_ptr->core_lib.flags.BYTE_CNV_POST)
         {
            pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.bit_width  = 24;
            pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.word_size  = 32;
            pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.q_factor   = 23;
            pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.byte_combo = PC_BW24_W32_Q23;
         }
         else
         {
            pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.bit_width =
               pc_ptr->core_lib.output_media_fmt.bit_width;
            pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.word_size =
               pc_ptr->core_lib.output_media_fmt.word_size;
            pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.q_factor =
               pc_ptr->core_lib.output_media_fmt.q_factor;
            pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.byte_combo =
               pc_ptr->core_lib.output_media_fmt.byte_combo;
         }
      }
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.RESAMPLER_PRE) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].output_media_fmt  = *current_output_media_fmt_ptr;
      pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].output_media_fmt.sampling_rate =
         pc_ptr->core_lib.output_media_fmt.sampling_rate;

      if (FIR_RESAMPLER == pc_ptr->resampler_type)
      {
         pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].process = pc_dyn_resampler_process;
         pc_fill_fir_resampler_lib_mf(pc_ptr,
                                      current_output_media_fmt_ptr,
                                      pc_ptr->core_lib.output_media_fmt.sampling_rate);
      }
      else
      {
         pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].process = pc_iir_resampler_process;
      }

      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.CHANNEL_MIXER) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].process           = pc_channel_mix_process;
      pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].output_media_fmt  = *current_output_media_fmt_ptr;
      pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].output_media_fmt.num_channels =
         pc_ptr->core_lib.output_media_fmt.num_channels;
      pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].output_media_fmt.channel_type =
         pc_ptr->core_lib.output_media_fmt.channel_type;
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].output_media_fmt;
   }
   if ((pc_ptr->core_lib.flags.RESAMPLER_POST) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[RESAMPLER_POST].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[RESAMPLER_POST].output_media_fmt  = *current_output_media_fmt_ptr;
      pc_ptr->core_lib.pc_proc_info[RESAMPLER_POST].output_media_fmt.sampling_rate =
         pc_ptr->core_lib.output_media_fmt.sampling_rate;

      if (FIR_RESAMPLER == pc_ptr->resampler_type)
      {
         pc_ptr->core_lib.pc_proc_info[RESAMPLER_POST].process = pc_dyn_resampler_process;
         pc_fill_fir_resampler_lib_mf(pc_ptr,
                                      current_output_media_fmt_ptr,
                                      pc_ptr->core_lib.output_media_fmt.sampling_rate);
      }
      else
      {
         pc_ptr->core_lib.pc_proc_info[RESAMPLER_POST].process = pc_iir_resampler_process;
      }

      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[RESAMPLER_POST].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.BYTE_CNV_POST) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].process           = pc_byte_morph_process;
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt  = *current_output_media_fmt_ptr;

      // Since floating point supports only 32 and 64 bit data, Data_cnv_fixed_to_float always converts from 32 bit,
      // hence byte conv is needed to get the input data ready in 32 bit format
      if (pc_ptr->core_lib.flags.DATA_CNV_FIXED_TO_FLOAT)
      {
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.bit_width  = 32;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.word_size  = 32;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.q_factor   = 31;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.byte_combo = PC_BW32_W32_Q31;
      }
      else
      {
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.bit_width =
            pc_ptr->core_lib.output_media_fmt.bit_width;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.word_size =
            pc_ptr->core_lib.output_media_fmt.word_size;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.q_factor =
            pc_ptr->core_lib.output_media_fmt.q_factor;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.byte_combo =
            pc_ptr->core_lib.output_media_fmt.byte_combo;
      }
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.INT_DEINT_POST) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].process           = pc_interleaving_process;
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_media_fmt  = *current_output_media_fmt_ptr;
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_media_fmt.interleaving =
         pc_ptr->core_lib.output_media_fmt.interleaving;
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.DATA_CNV_FIXED_TO_FLOAT) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].process           = pc_fixed_to_float_conv_process;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].output_buffer_ptr = buffers[current_index];
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].output_media_fmt  = *current_output_media_fmt_ptr;

      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].output_media_fmt.data_format = PC_FLOATING_FORMAT;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].output_media_fmt.word_size =
         pc_ptr->core_lib.output_media_fmt.word_size;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].output_media_fmt.bit_width =
         pc_ptr->core_lib.output_media_fmt.bit_width;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].output_media_fmt.q_factor =
         pc_ptr->core_lib.output_media_fmt.q_factor;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].output_media_fmt.byte_combo =
         pc_ptr->core_lib.output_media_fmt.byte_combo;

      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.ENDIANNESS_POST)
   {
      pc_ptr->core_lib.pc_proc_info[ENDIANNESS_POST].process           = pc_endianness_process;
      pc_ptr->core_lib.pc_proc_info[ENDIANNESS_POST].output_buffer_ptr = pc_ptr->core_lib.remap_output_buf_ptr;
      pc_ptr->core_lib.pc_proc_info[ENDIANNESS_POST].output_media_fmt  = *current_output_media_fmt_ptr;
      pc_ptr->core_lib.pc_proc_info[ENDIANNESS_POST].output_media_fmt.endianness =
         pc_ptr->core_lib.output_media_fmt.endianness;
   }

   // Assign proper interleaving to the last process
   // This is to handle deinterleaved unpacked to deinterleaved packed case
   for (uint32_t i = NUMBER_OF_PROCESS - 1; i > 0; i--)
   {
      if (pc_ptr->core_lib.pc_proc_info[i - 1].process)
      {
         // Ignore the ENDIANNESS_POST since it will have correct output interleaving
         // If only ENDIANNESS_POST is enabled, it will not be in place, it will do DUP to DP or DP to DUP conversions
         // If only ENDIANNESS_PRE is enabled, this logic will provide correct interleaving to ENDIANNESS_PRE
         // If one of the buffering modules is enabled, this logic will provide correct out interleaving info to it
         pc_ptr->core_lib.pc_proc_info[i - 1].output_media_fmt.interleaving =
            pc_ptr->core_lib.output_media_fmt.interleaving;
         break;
      }
   }
}
/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Checks if the channel mixer is needed or not.
   Enable it if,
   1. If the co-efficient pointer is not null, that means there is a Client configuration for the given ch-map
   2. If the number of channels or ch-map from input to output doesn't match. This means ch-mixer should be enabled
     with default co-efficient values
______________________________________________________________________________________________________________________*/
bool_t pc_is_ch_mixer_needed(pc_lib_t *      pc_ptr,
                             int16_t *       coef_set_ptr,
                             pc_media_fmt_t *input_media_fmt_ptr,
                             pc_media_fmt_t *output_media_fmt_ptr)
{
   bool_t   ch_enable_flag = FALSE;
   uint32_t in_num_ch      = input_media_fmt_ptr->num_channels;
   uint32_t out_num_ch     = output_media_fmt_ptr->num_channels;
   if (in_num_ch == out_num_ch)
   {
      for (uint16_t i = 0; i < in_num_ch; i++)
      {
         if (input_media_fmt_ptr->channel_type[i] != output_media_fmt_ptr->channel_type[i])
         {
            ch_enable_flag = TRUE;
            break;
         }
      }

      // inp chmap == out chmap and coef ptr is not null, check if we need to enable
      if ((FALSE == ch_enable_flag) && (NULL != coef_set_ptr))
      {
         bool_t is_identity_matrix = TRUE;
         for (uint32_t row = 0; row < out_num_ch; row++)
         {
            for (uint32_t col = 0; col < in_num_ch; col++)
            {
               if ((row == col) && (*(coef_set_ptr + (row * in_num_ch + col)) != 0x4000))
               {
                  // If elements of main diagonal is not equal to 1
                  is_identity_matrix = FALSE;
                  break;
               }
               else if ((row != col) && *(coef_set_ptr + (row * in_num_ch + col)) != 0)
               {
                  // If other elements than main diagonal is not equal to 0
                  is_identity_matrix = FALSE;
                  break;
               }
            }
            if (!is_identity_matrix)
            {
               ch_enable_flag = TRUE;
               break; // break out of second for loop
            }
         }
      }
   }
   else
   {
      ch_enable_flag = TRUE;
   }

   return ch_enable_flag;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Does a set param to the channel mixer with the proper co-efficient ptr.
   IF the ptr is null, default value will be picked inside the ch-mixer library
______________________________________________________________________________________________________________________*/
ar_result_t pc_reinit_ch_mixer(pc_lib_t *pc_ptr, uint32_t channel_mixer_lib_size, void *coef_set_ptr)
{
   ChMixerChType in_ch_type[CAPI_MAX_CHANNELS_V2];
   for (uint32_t i = 0; i < pc_ptr->core_lib.input_media_fmt.num_channels; i++)
   {
      in_ch_type[i] = (ChMixerChType)pc_ptr->core_lib.input_media_fmt.channel_type[i];
   }

   ChMixerChType out_ch_type[CAPI_MAX_CHANNELS_V2];
   for (uint32_t i = 0; i < pc_ptr->core_lib.output_media_fmt.num_channels; i++)
   {
      out_ch_type[i] = (ChMixerChType)pc_ptr->core_lib.output_media_fmt.channel_type[i];
   }

   // Initialize channel mixer lib.
   ChMixerResultType result =
      ChMixerSetParam(pc_ptr->ch_lib_ptr,
                      channel_mixer_lib_size,
                      (uint32)pc_ptr->core_lib.input_media_fmt.num_channels,
                      in_ch_type,
                      (uint32)pc_ptr->core_lib.output_media_fmt.num_channels,
                      out_ch_type,
                      (uint32)pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].output_media_fmt.word_size,
                      coef_set_ptr);
   if (CH_MIXER_SUCCESS != result)
   {
      CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "CH-mixer reint failed");
      return AR_EFAILED;
   }
   return AR_EOK;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Does a set param to the channel mixer with the proper co-efficient ptr.
   IF the ptr is null, default value will be picked inside the ch-mixer library
______________________________________________________________________________________________________________________*/
ar_result_t pc_check_lib_enable(pc_lib_t *      pc_ptr,
                                bool_t *        enable,
                                bool_t          need_for_resampler,
                                bool_t          enable_for_deint_packed_unpacked_cnv,
                                pc_media_fmt_t *input_media_fmt_ptr)
{
   // If input data format is floating point and float_to_fixed conversion process is not enabled and channel mixer and
   // resampler flags are true, then disable the module since these processes do not support floating point data.
   if ((PC_FLOATING_FORMAT == input_media_fmt_ptr->data_format) &&
       (TRUE != pc_ptr->core_lib.flags.DATA_CNV_FLOAT_TO_FIXED) &&
       (pc_ptr->core_lib.flags.CHANNEL_MIXER || need_for_resampler))
   {
      CNV_MSG(pc_ptr->miid,
              DBG_ERROR_PRIO,
              "Floating point data format is not supported by channel mixer %d or resampler %d",
              pc_ptr->core_lib.flags.CHANNEL_MIXER,
              need_for_resampler);
      *enable = FALSE;
      return AR_EOK;
   }

   // if any of the flags such as ENDIANNESS_PRE,INT_DEINT_PRE,BYTE_CNV_PRE,CHANNEL_MIXER,BYTE_CNV_POST,INT_DEINT_POST
   // ,ENDIANNESS_POST or if we require a resampler then the module has to be enabled
   if (pc_ptr->core_lib.flags.word || (need_for_resampler == TRUE) || (enable_for_deint_packed_unpacked_cnv == TRUE))
   {
      *enable = TRUE;
   }
   else
   {
      *enable = FALSE;
   }
   return AR_EOK;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Does PCM CONVERTER library init.
   1. Reallocated the memory needed.
   2. Checks if resampler reordering is allowed
   2. Copies the media format.
   3. Based on the media formats, identifies the modules to be enabled.
   4. If resampler is needed, allocates lib ptr memory for either of the resamplers, frees other if present
   6. Based on enabled modules and media fmt, fill in the module process info table.
   5. Creates or re-inits resampler if needed
   7. Reints ch-mixer library if needed
______________________________________________________________________________________________________________________*/

ar_result_t pc_init(pc_lib_t *      pc_ptr,
                    pc_media_fmt_t *input_media_fmt_ptr,
                    pc_media_fmt_t *output_media_fmt_ptr,
                    void *          coef_set_ptr,
                    uint32_t        heap_id,
                    bool_t *        lib_enable_ptr,
                    bool_t          fir_iir_rs_switch,
                    uint32_t        miid,
                    uint8_t         type)
{

   ar_result_t result                               = AR_EOK;
   bool_t      fir_rs_reinit                        = FALSE;
   bool_t      need_for_resampler                   = FALSE;
   bool_t      enable_for_deint_packed_unpacked_cnv = FALSE;
   uint32_t    channel_mixer_lib_size               = 0;

   uint16_t max_num_of_channels = input_media_fmt_ptr->num_channels >= output_media_fmt_ptr->num_channels
                                     ? input_media_fmt_ptr->num_channels
                                     : output_media_fmt_ptr->num_channels;
   // free the allocated mem and memset pc_ptr->core_lib to 0 so that the pointers do not hold information about
   // previous instance
   if (NULL != pc_ptr->core_lib.pc_mem_ptr)
   {
      posal_memory_aligned_free((void *)pc_ptr->core_lib.pc_mem_ptr);
      pc_ptr->core_lib.pc_mem_ptr = NULL;
   }
   memset(&(pc_ptr->core_lib), 0, sizeof(pc_core_lib_t));

   pc_identify_required_processes(pc_ptr,
                                  input_media_fmt_ptr,
                                  output_media_fmt_ptr,
                                  &need_for_resampler,
                                  &enable_for_deint_packed_unpacked_cnv,
                                  (int16_t *)coef_set_ptr,
                                  type);

   pc_check_lib_enable(pc_ptr,
                       lib_enable_ptr,
                       need_for_resampler,
                       enable_for_deint_packed_unpacked_cnv,
                       input_media_fmt_ptr);
   if (*lib_enable_ptr == FALSE)
   {
      // if module is not enabled exit pc_init
      return result;
   }

   result = pc_realloc_chmixer_memory(pc_ptr,
                                      &channel_mixer_lib_size,
                                      max_num_of_channels,
                                      input_media_fmt_ptr->num_channels,
                                      output_media_fmt_ptr->num_channels,
                                      heap_id);
   if (result != AR_EOK)
   {
      return result;
   }

   result = pc_check_alloc_resampler_memory(pc_ptr,
                                            heap_id,
                                            &fir_rs_reinit,
                                            &fir_iir_rs_switch,
                                            input_media_fmt_ptr,
                                            output_media_fmt_ptr,
                                            need_for_resampler);

   if (result != AR_EOK)
   {
      return result;
   }

   bool_t reorder_allowed =
      pc_is_reorder_allowed(pc_ptr, input_media_fmt_ptr, output_media_fmt_ptr, fir_iir_rs_switch, fir_rs_reinit);

   pc_copy_media_fmt(pc_ptr, input_media_fmt_ptr, output_media_fmt_ptr);

   /* update resampler pre and post flags based the need for resampler and reorder allowed. Num of bufferring stages is
    also calculated.*/
   pc_update_resampler_pos_and_buff_stages(pc_ptr, need_for_resampler, reorder_allowed);

   pc_fill_proc_info(pc_ptr);

   result = pc_check_create_resampler_instance(pc_ptr, heap_id, fir_rs_reinit);

   pc_reinit_ch_mixer(pc_ptr, channel_mixer_lib_size, coef_set_ptr);

   return result;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Does PCM CONVERTER library deinit.
______________________________________________________________________________________________________________________*/

void pc_deinit(pc_lib_t *pc_ptr)
{
   if (NULL != pc_ptr->core_lib.pc_mem_ptr)
   {
      posal_memory_aligned_free(pc_ptr->core_lib.pc_mem_ptr);
      pc_ptr->core_lib.pc_mem_ptr = NULL;
   }

   if (NULL != pc_ptr->hwsw_rs_lib_ptr)
   {
      hwsw_rs_lib_deinit(pc_ptr->hwsw_rs_lib_ptr);
      posal_memory_free(pc_ptr->hwsw_rs_lib_ptr);
      pc_ptr->hwsw_rs_lib_ptr = NULL;
   }

   if (NULL != pc_ptr->iir_rs_lib_ptr)
   {
      iir_rs_lib_deinit(pc_ptr->iir_rs_lib_ptr);
      posal_memory_free(pc_ptr->iir_rs_lib_ptr);
      pc_ptr->iir_rs_lib_ptr = NULL;
   }
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Aggregates KPPS based on number of enabled modules.
   1. KPPS tables are calculated per channel at 48KHz.
   2. So KPPS scaling is needed before the aggregation.
   3. Added fw kpps for re-mapping buffer as well. This is scaled based on number of active modules in the chain
______________________________________________________________________________________________________________________*/
void pc_get_kpps_and_bw(pc_lib_t *pc_ptr, uint32_t *kpps, uint32_t *bw)
{
   pc_media_fmt_t *current_input_media_fmt_ptr = &pc_ptr->core_lib.input_media_fmt;
   uint64_t        new_kpps                    = 0;
   uint64_t        new_bw                      = 0;
   *kpps                                       = 0;
   *bw                                         = 0;

   if (pc_ptr->core_lib.flags.ENDIANNESS_PRE)
   {
      uint32_t word_size    = pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_media_fmt.word_size;
      uint32_t num_channels = pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_media_fmt.num_channels;
      uint32_t sr           = pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_media_fmt.sampling_rate;
      uint32_t num_bytes    = (pc_ptr->core_lib.input_media_fmt.bit_width >> 3);

      new_kpps += ((endianness_kpps_per_ch[(word_size >> 3) - 2] * num_channels) * sr) / base_sample_rate;
      // for endinaess and interleaving we calculate bandwidth based on num of channels, num of bytes, sampling rate
      // Bw calculation is multiplied by 2 because for every data copy we do a read+write operation
      new_bw += (2 * sr * num_channels * num_bytes);

      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.INT_DEINT_PRE)
   {
      uint32_t word_size    = pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_media_fmt.word_size;
      uint32_t num_channels = pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_media_fmt.num_channels;
      uint32_t sr           = pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_media_fmt.sampling_rate;
      uint32_t num_bytes    = (pc_ptr->core_lib.input_media_fmt.bit_width >> 3);

      new_kpps += ((intl_dintl_kpps_per_ch[(word_size >> 3) - 2] * num_channels) * sr) / base_sample_rate;
      new_bw += (2 * sr * num_channels * num_bytes);

      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.BYTE_CNV_PRE)
   {
      uint32_t word_size    = pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.word_size;
      uint32_t num_channels = pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.num_channels;
      uint32_t sr           = pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.sampling_rate;

      if ((24 == current_input_media_fmt_ptr->word_size) || (24 == word_size))
      {
         new_kpps += ((byte_cnv_kpps_per_ch[(24 >> 3) - 2] * num_channels) * sr) / base_sample_rate;
      }
      else
      {
         new_kpps += ((byte_cnv_kpps_per_ch[(word_size >> 3) - 2] * num_channels) * sr) / base_sample_rate;
      }
      // for byte conversion bandwidth based on num of channels, sampling rate and sum of input and output num of bytes
      new_bw += (sr * num_channels *
                 ((pc_ptr->core_lib.input_media_fmt.bit_width + pc_ptr->core_lib.output_media_fmt.bit_width) >> 3));

      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.RESAMPLER_PRE)
   {
      if (FIR_RESAMPLER == pc_ptr->resampler_type)
      {
         new_kpps += hwsw_rs_lib_get_kpps(pc_ptr->hwsw_rs_lib_ptr);
         new_bw += hwsw_rs_lib_get_bw(pc_ptr->hwsw_rs_lib_ptr);
      }
      else
      {
         new_kpps += iir_rs_lib_get_kpps(pc_ptr->iir_rs_lib_ptr,
                                         pc_ptr->core_lib.input_media_fmt.sampling_rate,
                                         pc_ptr->core_lib.output_media_fmt.sampling_rate);
         new_bw += iir_rs_lib_get_bw(pc_ptr->iir_rs_lib_ptr, pc_ptr->core_lib.input_media_fmt.sampling_rate);
      }

      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.CHANNEL_MIXER)
   {
      uint32_t sr               = pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].output_media_fmt.sampling_rate;
      uint32_t num_active_coeff = ChMixerGetNumActiveCoefficients(pc_ptr->ch_lib_ptr);
      uint32_t out_ch           = pc_ptr->core_lib.output_media_fmt.num_channels;
      uint32_t num_bytes        = (pc_ptr->core_lib.input_media_fmt.bit_width >> 3);
      // Using constants -8, 4, 5 through profiling, + 225 for capi kpps
      // Opening up the brackets for better div/mult:
      // out ch * (-8 + num_samples(4 + (5* in_ch* num_active_coeff/(in_ch * out_ch)))) + c
      // => out_ch * (-8 + (num_samples * 4) + (num_samples * 5* num_coeff/out_ch)) + c
      // sr/1000 translates to num samples
      uint64_t chmixer_kpps = (out_ch * (-8LL + ((sr * 4) + (sr * 5 * num_active_coeff / out_ch)) / 1000)) + 225;

      new_kpps += chmixer_kpps;
      // for ch mixer, bandwidth based on num of bytes, sampling rate and sum of input and output channels
      new_bw += (sr * num_bytes *
                 (pc_ptr->core_lib.output_media_fmt.num_channels + pc_ptr->core_lib.input_media_fmt.num_channels));

      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.RESAMPLER_POST)
   {
      if (FIR_RESAMPLER == pc_ptr->resampler_type)
      {
         new_kpps += hwsw_rs_lib_get_kpps(pc_ptr->hwsw_rs_lib_ptr);
         new_bw += hwsw_rs_lib_get_bw(pc_ptr->hwsw_rs_lib_ptr);
      }
      else
      {
         new_kpps += iir_rs_lib_get_kpps(pc_ptr->iir_rs_lib_ptr,
                                         pc_ptr->core_lib.input_media_fmt.sampling_rate,
                                         pc_ptr->core_lib.output_media_fmt.sampling_rate);
         new_bw += iir_rs_lib_get_bw(pc_ptr->iir_rs_lib_ptr, pc_ptr->core_lib.input_media_fmt.sampling_rate);
      }
      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[RESAMPLER_POST].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.BYTE_CNV_POST)
   {
      uint32_t word_size    = pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.word_size;
      uint32_t num_channels = pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.num_channels;
      uint32_t sr           = pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.sampling_rate;

      if ((24 == current_input_media_fmt_ptr->word_size) || (24 == word_size))
      {
         new_kpps += ((byte_cnv_kpps_per_ch[(24 >> 3) - 2] * num_channels) * sr) / base_sample_rate;
      }
      else
      {
         new_kpps += ((byte_cnv_kpps_per_ch[(word_size >> 3) - 2] * num_channels) * sr) / base_sample_rate;
      }

      new_bw += (sr * num_channels *
                 ((pc_ptr->core_lib.input_media_fmt.bit_width + pc_ptr->core_lib.output_media_fmt.bit_width) >> 3));
      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.INT_DEINT_POST)
   {
      uint32_t word_size    = pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_media_fmt.word_size;
      uint32_t num_channels = pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_media_fmt.num_channels;
      uint32_t sr           = pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.sampling_rate;
      uint32_t num_bytes    = (pc_ptr->core_lib.input_media_fmt.bit_width >> 3);

      new_kpps += ((intl_dintl_kpps_per_ch[(word_size >> 3) - 2] * num_channels) * sr) / base_sample_rate;
      new_bw += (2 * sr * num_channels * num_bytes);

      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.ENDIANNESS_POST)
   {
      uint32_t word_size    = pc_ptr->core_lib.pc_proc_info[ENDIANNESS_POST].output_media_fmt.word_size;
      uint32_t num_channels = pc_ptr->core_lib.pc_proc_info[ENDIANNESS_POST].output_media_fmt.num_channels;
      uint32_t sr           = pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.sampling_rate;
      uint32_t num_bytes    = (pc_ptr->core_lib.input_media_fmt.bit_width >> 3);

      new_kpps += ((endianness_kpps_per_ch[(word_size >> 3) - 2] * num_channels) * sr) / base_sample_rate;
      new_bw += (2 * sr * num_channels * num_bytes);

      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[ENDIANNESS_POST].output_media_fmt;
   }

   uint32_t no_of_stages = pc_ptr->core_lib.flags.ENDIANNESS_PRE + pc_ptr->core_lib.flags.ENDIANNESS_POST +
                           pc_ptr->core_lib.flags.INT_DEINT_PRE + pc_ptr->core_lib.flags.INT_DEINT_POST +
                           pc_ptr->core_lib.flags.BYTE_CNV_PRE + pc_ptr->core_lib.flags.BYTE_CNV_POST +
                           pc_ptr->core_lib.flags.CHANNEL_MIXER;

   if (0 == no_of_stages)
   {
      uint32_t index = (pc_ptr->core_lib.input_media_fmt.word_size >> 3) - 2;
      if (MAX_BW_IDX > index)
      {
         new_kpps += memcpy_kpps_per_ch[index] * pc_ptr->core_lib.input_media_fmt.num_channels;
      }
   }

   new_kpps += (fw_kpps * no_of_stages);
   *kpps = (uint32_t)new_kpps;
   *bw   = (uint32_t)new_bw;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Aggregates algo delay based on number of enabled modules.
______________________________________________________________________________________________________________________*/
uint32_t pc_get_algo_delay(pc_lib_t *pc_ptr)
{
   uint32_t algo_delay = 0;
   if ((pc_ptr->core_lib.flags.RESAMPLER_PRE) || (pc_ptr->core_lib.flags.RESAMPLER_POST))
   {
      if (FIR_RESAMPLER == pc_ptr->resampler_type)
      {
         algo_delay += hwsw_rs_lib_get_alg_delay(pc_ptr->hwsw_rs_lib_ptr);
      }
      else
      {
         algo_delay += iir_rs_lib_get_delay(pc_ptr->iir_rs_lib_ptr,
                                            pc_ptr->core_lib.input_media_fmt.sampling_rate,
                                            pc_ptr->core_lib.output_media_fmt.sampling_rate);
      }
   }
   return algo_delay;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Classifies the bit-width, word-size and q-factor to an enum
______________________________________________________________________________________________________________________*/
pc_mf_combo_t pc_classify_mf(pc_media_fmt_t *mf_ptr)
{
   if (NULL == mf_ptr)
   {
      return PC_INVALID_CMBO;
   }
   uint16_t word_size   = mf_ptr->word_size;
   uint16_t bit_width   = mf_ptr->bit_width;
   uint16_t q_factor    = mf_ptr->q_factor;
   uint16_t data_format = mf_ptr->data_format;

   if (PC_FLOATING_FORMAT == data_format)
   {
      if ((32 == word_size) && (32 == bit_width))
      {
         return PC_BW32_W32_FLOAT;
      }
      else if ((64 == word_size) && (64 == bit_width))
      {
         return PC_BW64_W64_DOUBLE;
      }
   }

   if ((16 == word_size) && (PCM_Q_FACTOR_15 == q_factor) && (16 == bit_width))
   {
      return PC_BW16_W16_Q15;
   }
   else if ((24 == word_size) && (PCM_Q_FACTOR_23 == q_factor) && (24 == bit_width))
   {
      return PC_BW24_W24_Q23;
   }
   else if (32 == word_size)
   {
      if (PCM_Q_FACTOR_23 == q_factor)
      {
         if (24 == bit_width)
         {
            return PC_BW24_W32_Q23;
         }
      }

      if (PCM_Q_FACTOR_27 == q_factor)
      {
         if (24 == bit_width)
         {
            return PC_BW24_W32_Q27;
         }
      }
      else if (PCM_Q_FACTOR_31 == q_factor)
      {
         if (32 == bit_width)
         {
            return PC_BW32_W32_Q31;
         }
         else if (24 == bit_width)
         {
            return PC_BW24_W32_Q31;
         }
      }
   }
   return PC_INVALID_CMBO;
}
