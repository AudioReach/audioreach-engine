/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/*______________________________________________________________________________________________________________________
Edit History


when       who     what, where, why
--------   ---     -------------------------------------------------------
7/15/26   vasharma    Added file for opensource resampler based MFC
______________________________________________________________________________________________________________________*/

/*______________________________________________________________________________________________________________________
INCLUDE FILES FOR MODULE
______________________________________________________________________________________________________________________*/

#include "mfc_osrc_converter.h"

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   1. Calculates the memory required for storing
      a. Channel map data for input and output media formats
      b. storing remap capi buffers * number of channels for input output
scratch buffer 1 and scratch buffer 2 c. Channel mixer library
   2. Frees if any memory is allocated already. It is better to free and relloc
instead of tracking.
   3. Assign memory to the corresponding pointers
______________________________________________________________________________________________________________________*/
ar_result_t mfc_osrc_realloc_chmixer_memory(mfc_osrc_lib_t *pc_ptr,
                                      uint32_t *ch_mixer_lib_size_ptr,
                                      uint16_t  max_num_of_channels,
                                      uint16_t  input_num_of_channels,
                                      uint16_t  output_num_of_channels,
                                      uint32_t  heap_id)
{
   ar_result_t         result           = AR_EOK;
   uint16_t           *channel_type_ptr = NULL;
   ChMixerStateStruct *ch_lib_ptr       = NULL;
   capi_buf_t         *buf_ptr          = NULL;
   uint8_t            *mem_ptr          = NULL;

   // Just need to allocate for the input and output, the proc media fmt can copy
   // the channel map from the input ptr or output ptr
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

// IIR resampler functions removed - OSResampler is always used

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   1. Allocates OSResampler lib ptr memory if resampling is needed
   2. Frees memory if resampling is not needed
______________________________________________________________________________________________________________________*/
ar_result_t mfc_osrc_check_alloc_resampler_memory(mfc_osrc_lib_t       *pc_ptr,
                                            uint32_t        heap_id,
                                            bool_t         *fir_rs_reinit_ptr,
                                            mfc_media_fmt_t *input_media_fmt_ptr,
                                            mfc_media_fmt_t *output_media_fmt_ptr,
                                            bool_t          need_for_resampler)
{
   ar_result_t result = AR_EOK;
   
   if (need_for_resampler)
   {
#ifdef MFC_OSRC_LIB_DEBUG
      CNV_MSG(pc_ptr->miid, DBG_HIGH_PRIO, "OSResampler to be used, allocating memory");
#endif
      if (NULL == pc_ptr->osrc_rs_lib_ptr)
      {
         pc_ptr->osrc_rs_lib_ptr = (osrc_resampler_lib_t *)
            posal_memory_malloc(sizeof(osrc_resampler_lib_t), (POSAL_HEAP_ID)heap_id);
         if (NULL == pc_ptr->osrc_rs_lib_ptr)
         {
            CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "OSResampler allocation failed");
            return AR_ENOMEMORY;
         }
         memset(pc_ptr->osrc_rs_lib_ptr, 0, sizeof(osrc_resampler_lib_t));
         osrc_rs_lib_init(pc_ptr->osrc_rs_lib_ptr);
      }
   }
   else
   {
      // Free resampler if allocated
      if (pc_ptr->osrc_rs_lib_ptr)
      {
         osrc_rs_lib_deinit(pc_ptr->osrc_rs_lib_ptr);
         posal_memory_free(pc_ptr->osrc_rs_lib_ptr);
         pc_ptr->osrc_rs_lib_ptr = NULL;
      }
   }

   return result;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Creates OSResampler instance with proper media format configuration
______________________________________________________________________________________________________________________*/
ar_result_t mfc_osrc_check_create_resampler_instance(mfc_osrc_lib_t *pc_ptr, uint32_t heap_id, bool_t fir_rs_reinit)
{
   ar_result_t result = AR_EOK;
   
   if ((pc_ptr->core_lib.flags.RESAMPLER_POST) || (pc_ptr->core_lib.flags.RESAMPLER_PRE))
   {
      // Determine input media format for resampler
      mfc_media_fmt_t *rs_imf_ptr = NULL;
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
      else // RESAMPLER_POST
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
              "OSResampler IN sampling_rate %lu, out sampling_rate %lu, ch = %lu, bw=%lu",
              rs_imf_ptr->sampling_rate,
              pc_ptr->core_lib.output_media_fmt.sampling_rate,
              rs_imf_ptr->num_channels,
              rs_imf_ptr->bit_width);

      // Set media format for OSResampler
      osrc_rs_media_fmt_t mf;
      mf.inp_sample_rate = rs_imf_ptr->sampling_rate;
      mf.output_sample_rate = pc_ptr->core_lib.output_media_fmt.sampling_rate;
      mf.num_channels = rs_imf_ptr->num_channels;
      mf.bits_per_sample = rs_imf_ptr->word_size;
      mf.q_factor = rs_imf_ptr->q_factor;
      
      osrc_rs_set_lib_mf(pc_ptr->osrc_rs_lib_ptr, &mf);

      // Create resampler instance
      result = osrc_rs_lib_check_create_resampler_instance(pc_ptr->osrc_rs_lib_ptr, heap_id);
      if (result != AR_EOK)
      {
         CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "OSResampler check create failed");
      }
   }

   return result;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Checks if reordering is needed
   1. Whenever update_rs_config is set, resampler will reinit and cause glitch,
hence reordering is allowed
   2. Else
      a. If sw resampler is being used in dynamic mode and sampling rate
changes, we don't want to reorder and cause a glitch b. If pre resampler is used
and input num ch remains same, don't reorder since out num ch for resampler
won't change c. If post resampler is used and output num ch remains same, don't
reorder since inp num ch for resampler won't change
______________________________________________________________________________________________________________________*/
bool_t mfc_osrc_is_reorder_allowed(mfc_osrc_lib_t       *pc_ptr,
                             mfc_media_fmt_t *input_media_fmt_ptr,
                             mfc_media_fmt_t *output_media_fmt_ptr,
                             bool_t          rs_reinit)
{
   if (rs_reinit)
   {
      return TRUE;
   }
   else if (((RESAMPLER_POSITION_PRE == pc_ptr->resampler_used) &&
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
   If resampler config is updated there will be a re-init so can skip the
reorder check
______________________________________________________________________________________________________________________*/
void mfc_osrc_copy_media_fmt(mfc_osrc_lib_t *pc_ptr, mfc_media_fmt_t *input_media_fmt_ptr, mfc_media_fmt_t *output_media_fmt_ptr)
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
}

/* When the inp/out data format is different and either one of the input or out
byte combo is MFC_BW32_W32_Q31, it translates to a data format conversion ONLY.
In such cases byte conv should not be enabled. */
bool_t mfc_osrc_byte_combo_change_handled_by_dfc(mfc_mf_combo_t input_byte_combo, mfc_mf_combo_t output_byte_combo)
{
   if (((MFC_BW32_W32_FLOAT == input_byte_combo) && (MFC_BW32_W32_Q31 == output_byte_combo)) ||
       ((MFC_BW32_W32_FLOAT == output_byte_combo) && (MFC_BW32_W32_Q31 == input_byte_combo)) ||
       ((MFC_BW64_W64_DOUBLE == input_byte_combo) && (MFC_BW32_W32_Q31 == output_byte_combo)) ||
       ((MFC_BW64_W64_DOUBLE == output_byte_combo) && (MFC_BW32_W32_Q31 == input_byte_combo)))
   {
      return TRUE;
   }

   return FALSE;
}
/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   ALGORITHM to create the process pipeline
   CH - channel mixer, B1/B2 - byte converters, I1/I2 -
interleaving/deinterleaving, E - endianness I - Interleaved D - Deinterleaved
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
         ii. else, if input is D and output is I - >  use I2 to convert from D
to I iii. for I to I, do nothing, for D to D do nothing b. If CH is needed i.
for I to I, enable both I1(does I to D) and I2(does D to I) ii. For D to D, do
nothing iii. for I to D and D to I follow same approach as id CH is not present
   4. If IIR resampler is needed and BW is not 16, use B1 to convert to BW16 and
B2 to convert to out BW.
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
   8. Now we know the values of I1, B1, R1, Ch, R2, B2, I2 . Make sure to record
the correct values of conversion during the above phase
______________________________________________________________________________________________________________________*/
static void mfc_osrc_identify_required_processes(mfc_osrc_lib_t       *pc_ptr,
                                           mfc_media_fmt_t *input_media_fmt_ptr,
                                           mfc_media_fmt_t *output_media_fmt_ptr,
                                           bool_t         *need_for_resampler_ptr,
                                           bool_t         *enable_for_deint_packed_unpacked_cnv,
                                           int16_t        *coef_set_ptr,
                                           uint8_t         type)
{
   *need_for_resampler_ptr               = FALSE;
   *enable_for_deint_packed_unpacked_cnv = FALSE;
   // set the flags to zero before assigning values to them. This should not be
   // done in pc_realloc_memory as flags will already be set
   pc_ptr->core_lib.flags.word = 0;
   if (input_media_fmt_ptr->sampling_rate != output_media_fmt_ptr->sampling_rate)
   {
      *need_for_resampler_ptr = TRUE;
   }

   if (MFC_BIG_ENDIAN == input_media_fmt_ptr->endianness)
   {
      pc_ptr->core_lib.flags.ENDIANNESS_PRE = TRUE;
   }
   else
   {
      if (MFC_BIG_ENDIAN == output_media_fmt_ptr->endianness)
      {
         pc_ptr->core_lib.flags.ENDIANNESS_POST = TRUE;
      }
   }

   pc_ptr->core_lib.flags.CHANNEL_MIXER =
      mfc_osrc_is_ch_mixer_needed(pc_ptr, coef_set_ptr, input_media_fmt_ptr, output_media_fmt_ptr);
   if (pc_ptr->core_lib.flags.CHANNEL_MIXER)
   {
      if (MFC_INTERLEAVED == input_media_fmt_ptr->interleaving)
      {
         pc_ptr->core_lib.flags.INT_DEINT_PRE = TRUE;
      }
      if (MFC_INTERLEAVED == output_media_fmt_ptr->interleaving)
      {
         pc_ptr->core_lib.flags.INT_DEINT_POST = TRUE;
      }
   }
   // if the num of channels is 1 and type is mfc and pcm converter we need to
   // skip enablng the interleaving flags needed when we do not need ch mixer
   else if (!((input_media_fmt_ptr->num_channels == 1) && ((type == MFC) || (type == PCM_CNV))))
   {
      if ((MFC_INTERLEAVED == input_media_fmt_ptr->interleaving) &&
          (MFC_INTERLEAVED != output_media_fmt_ptr->interleaving))
      {
         pc_ptr->core_lib.flags.INT_DEINT_PRE = TRUE;
      }
      if ((MFC_INTERLEAVED != input_media_fmt_ptr->interleaving) &&
          (MFC_INTERLEAVED == output_media_fmt_ptr->interleaving))
      {
         pc_ptr->core_lib.flags.INT_DEINT_POST = TRUE;
      }
      // If num channels are not equal module will anyway be enabled due to
      // channel mixer check If num channels are equal and inp and out
      // interleaving conv is needed for deint packed to deint unpacked or vice
      // versa we need to keep the module enabled. Pack-unpack conv happens
      // through buffer sizing and not by enabling the I1 and I2 processes in the
      // pcm cnv If num ch is 1, fwk takes care of propagating interleaving flag
      // to downstream modules and pcm cnv/mfc can be bypassed
      if ((input_media_fmt_ptr->interleaving != output_media_fmt_ptr->interleaving) &&
          (MFC_INTERLEAVED != input_media_fmt_ptr->interleaving) &&
          (MFC_INTERLEAVED != output_media_fmt_ptr->interleaving))
      {
         *enable_for_deint_packed_unpacked_cnv = TRUE;
      }
   }
   if (input_media_fmt_ptr->byte_combo == output_media_fmt_ptr->byte_combo)
   {
      if (((TRUE == pc_ptr->core_lib.flags.CHANNEL_MIXER) || (TRUE == *need_for_resampler_ptr)))
      {
         // Up conversion is needed
         if (MFC_BW24_W24_Q23 == input_media_fmt_ptr->byte_combo)
         {
            pc_ptr->core_lib.flags.BYTE_CNV_PRE  = TRUE;
            pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
         }
      }
   }
   /* else if case translates to byte combo being different.
    * When we convert for certain fixed to float formats or vice versa,
    * "byte_combo" notation in pcm converter is different but it actually
    * translates to a data format conversion ONLY. In such cases byte conv should
    * not be enabled. */
   else if (!mfc_osrc_byte_combo_change_handled_by_dfc(input_media_fmt_ptr->byte_combo, output_media_fmt_ptr->byte_combo))
   {
      if (FALSE == pc_ptr->core_lib.flags.CHANNEL_MIXER)
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
            if ((MFC_BW24_W24_Q23 != input_media_fmt_ptr->byte_combo) &&
                (MFC_BW24_W24_Q23 != output_media_fmt_ptr->byte_combo))
            {
               pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
            }
            else if ((MFC_BW24_W24_Q23 != input_media_fmt_ptr->byte_combo) &&
                     (MFC_BW24_W24_Q23 == output_media_fmt_ptr->byte_combo))
            {
               pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
            }
            else if ((MFC_BW24_W24_Q23 == input_media_fmt_ptr->byte_combo) &&
                     (MFC_BW24_W24_Q23 != output_media_fmt_ptr->byte_combo))
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
            if ((MFC_BW24_W24_Q23 != input_media_fmt_ptr->byte_combo) &&
                (MFC_BW24_W24_Q23 != output_media_fmt_ptr->byte_combo))
            {
               pc_ptr->core_lib.flags.BYTE_CNV_PRE = TRUE;
            }
            else if ((MFC_BW24_W24_Q23 != input_media_fmt_ptr->byte_combo) &&
                     (MFC_BW24_W24_Q23 == output_media_fmt_ptr->byte_combo))
            {
               pc_ptr->core_lib.flags.BYTE_CNV_POST = TRUE;
            }
            else if ((MFC_BW24_W24_Q23 == input_media_fmt_ptr->byte_combo) &&
                     (MFC_BW24_W24_Q23 != output_media_fmt_ptr->byte_combo))
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
      if (MFC_FLOATING_FORMAT == input_media_fmt_ptr->data_format)
      {
         pc_ptr->core_lib.flags.DATA_CNV_FLOAT_TO_FIXED = TRUE;
      }
      else if (MFC_FIXED_FORMAT == input_media_fmt_ptr->data_format)
      {
         pc_ptr->core_lib.flags.DATA_CNV_FIXED_TO_FLOAT = TRUE;
      }
   }
   return;
}

void mfc_osrc_update_resampler_pos_and_buff_stages(mfc_osrc_lib_t *pc_ptr, bool_t need_for_resampler, bool_t reorder_allowed)
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

#ifdef MFC_OSRC_LIB_DEBUG
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
   Sets lib mf for OSResampler lib
______________________________________________________________________________________________________________________*/
void mfc_osrc_fill_resampler_lib_mf(mfc_osrc_lib_t *pc_ptr, mfc_media_fmt_t *inp_mf, uint32_t out_sample_rate)
{
   osrc_rs_media_fmt_t rs_lib_media_fmt;
   rs_lib_media_fmt.bits_per_sample    = inp_mf->word_size;
   rs_lib_media_fmt.inp_sample_rate    = inp_mf->sampling_rate;
   rs_lib_media_fmt.num_channels       = inp_mf->num_channels;
   rs_lib_media_fmt.output_sample_rate = out_sample_rate;
   rs_lib_media_fmt.q_factor           = inp_mf->q_factor;
   
   CNV_MSG(pc_ptr->miid,
              DBG_HIGH_PRIO,
              "OSResampler mfc_osrc_fill_resampler_lib_mf - in sampling_rate %lu, out sampling_rate %lu, ch = %lu, bw=%lu",
              rs_lib_media_fmt.inp_sample_rate,
              rs_lib_media_fmt.output_sample_rate,
              rs_lib_media_fmt.num_channels,
              rs_lib_media_fmt.bits_per_sample);

   if (NULL == pc_ptr->osrc_rs_lib_ptr)
   {
      CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "osrc_rs_lib_ptr is NULL");
   }
   else
   {
      osrc_rs_set_lib_mf(pc_ptr->osrc_rs_lib_ptr, &rs_lib_media_fmt);
   }
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Check mfc_osrc_identify_required_processes function before reading this
   NBS -> number of buffering stages
   1. For E1. if E1 is enabled, and NBS = 0, make output buffer = fw output
buffer else, do in-place
   2. For I1. If it enabled, it is converting from D to I, so hard code the
value.
   3. For B1. if both B1 and B2 are enabled -> B1 should up-convert to support
ch-mixer bit-width, Else, take the out media fmt from the main output media
format
   4. For R1, update output sampling rate and depending on type of resampler
call process function
   5. For CH mixer, take media fmt from main output media fmt
   6. For R2, update output sampling rate and depending on type of resampler
call process function
   7. For B1, take media fmt from main output media fmt
   8. For I2, if enabled, it does D to I. get the media fmt from output anyway
   9. For E2, output buffer is always fw output buffer
______________________________________________________________________________________________________________________*/
void mfc_osrc_fill_proc_info(mfc_osrc_lib_t *pc_ptr)
{
   capi_buf_t *buffers[MAX_NUMBER_OF_BUFFERING_PROCESS] = {
      &pc_ptr->core_lib.remap_scratch_buf1_ptr[0], &pc_ptr->core_lib.remap_scratch_buf2_ptr[0],
      &pc_ptr->core_lib.remap_scratch_buf1_ptr[0], &pc_ptr->core_lib.remap_scratch_buf2_ptr[0],
      &pc_ptr->core_lib.remap_scratch_buf1_ptr[0], &pc_ptr->core_lib.remap_scratch_buf2_ptr[0],
      &pc_ptr->core_lib.remap_output_buf_ptr[0]
   };

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

   mfc_media_fmt_t *current_output_media_fmt_ptr = &pc_ptr->core_lib.input_media_fmt;

   if (pc_ptr->core_lib.flags.ENDIANNESS_PRE)
   {
      pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].process = mfc_osrc_endianness_process;
      if (0 == pc_ptr->core_lib.no_of_buffering_stages)
      {
         pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_buffer_ptr = pc_ptr->core_lib.remap_output_buf_ptr;
      }
      else
      {
         pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_buffer_ptr = pc_ptr->core_lib.remap_input_buf_ptr;
      }

      pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_media_fmt            = pc_ptr->core_lib.input_media_fmt;
      pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_media_fmt.endianness = MFC_LITTLE_ENDIAN;
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[ENDIANNESS_PRE].output_media_fmt;
   }

   /* The output Q factor for float to fixed conversion is assumed to be 31 word
    size =32 and if the required output q factor of pc converter is different,
    byte morph process would take care of that*/
   if ((pc_ptr->core_lib.flags.DATA_CNV_FLOAT_TO_FIXED) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].process           = mfc_osrc_float_to_fixed_conv_process;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt  = *current_output_media_fmt_ptr;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt.data_format = MFC_FIXED_FORMAT;

      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt.q_factor   = 31;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt.word_size  = 32;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt.bit_width  = 32;
      CNV_MSG(pc_ptr->miid, DBG_HIGH_PRIO, "bit_width = 32");
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt.byte_combo = MFC_BW32_W32_Q31;
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[DATA_CNV_FLOAT_TO_FIXED].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.INT_DEINT_PRE) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].process                       = mfc_osrc_interleaving_process;
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_buffer_ptr             = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_media_fmt              = *current_output_media_fmt_ptr;
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_media_fmt.interleaving = MFC_DEINTERLEAVED_UNPACKED_V2;
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[INT_DEINT_PRE].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.BYTE_CNV_PRE) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].process           = mfc_osrc_byte_morph_process;
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt  = *current_output_media_fmt_ptr;

      if (pc_ptr->core_lib.flags.BYTE_CNV_POST)
      {
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.bit_width  = 24;
         CNV_MSG(pc_ptr->miid, DBG_HIGH_PRIO, "bit_width = 24");
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.word_size  = 32;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.q_factor   = 23;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.byte_combo = MFC_BW24_W32_Q23;
      }
      else if (pc_ptr->core_lib.flags.DATA_CNV_FIXED_TO_FLOAT)
      {
         // Added else if logic to handle Fixed to float conversions
         // otherwise it could lead to large shift value in mfc_osrc_byte_morph_process.
         // where Q factor for float is -1
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.bit_width  = 32;
         CNV_MSG(pc_ptr->miid, DBG_HIGH_PRIO, "bit_width = CNV 32");
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.word_size  = 32;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.q_factor   = 31;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.byte_combo = MFC_BW32_W32_Q31;
      }
      else
      {
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.bit_width =
            pc_ptr->core_lib.output_media_fmt.bit_width;
         CNV_MSG(pc_ptr->miid, DBG_HIGH_PRIO, "bit_width else");
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.word_size =
            pc_ptr->core_lib.output_media_fmt.word_size;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.q_factor =
            pc_ptr->core_lib.output_media_fmt.q_factor;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt.byte_combo =
            pc_ptr->core_lib.output_media_fmt.byte_combo;
      }
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.RESAMPLER_PRE) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].output_media_fmt  = *current_output_media_fmt_ptr;
      pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].output_media_fmt.sampling_rate =
         pc_ptr->core_lib.output_media_fmt.sampling_rate;

      // Use OSResampler process function
      pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].process = mfc_osrc_resampler_process;
      mfc_osrc_fill_resampler_lib_mf(pc_ptr,
                                     current_output_media_fmt_ptr,
                                     pc_ptr->core_lib.output_media_fmt.sampling_rate);

      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[RESAMPLER_PRE].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.CHANNEL_MIXER) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].process           = mfc_osrc_channel_mix_process;
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

      // Use OSResampler process function
      pc_ptr->core_lib.pc_proc_info[RESAMPLER_POST].process = mfc_osrc_resampler_process;
      mfc_osrc_fill_resampler_lib_mf(pc_ptr,
                                     current_output_media_fmt_ptr,
                                     pc_ptr->core_lib.output_media_fmt.sampling_rate);

      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[RESAMPLER_POST].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.BYTE_CNV_POST) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].process           = mfc_osrc_byte_morph_process;
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt  = *current_output_media_fmt_ptr;

      // Since floating point supports only 32 and 64 bit data,
      // Data_cnv_fixed_to_float always converts from 32 bit, hence byte conv is
      // needed to get the input data ready in 32 bit format
      if (pc_ptr->core_lib.flags.DATA_CNV_FIXED_TO_FLOAT)
      {
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.bit_width  = 32;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.word_size  = 32;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.q_factor   = 31;
         pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.byte_combo = MFC_BW32_W32_Q31;
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
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].process           = mfc_osrc_interleaving_process;
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_buffer_ptr = buffers[current_index++];
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_media_fmt  = *current_output_media_fmt_ptr;
      pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_media_fmt.interleaving =
         pc_ptr->core_lib.output_media_fmt.interleaving;
      current_output_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_media_fmt;
   }

   if ((pc_ptr->core_lib.flags.DATA_CNV_FIXED_TO_FLOAT) && (MAX_NUMBER_OF_BUFFERING_PROCESS > current_index))
   {
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].process           = mfc_osrc_fixed_to_float_conv_process;
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].output_buffer_ptr = buffers[current_index];
      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].output_media_fmt  = *current_output_media_fmt_ptr;

      pc_ptr->core_lib.pc_proc_info[DATA_CNV_FIXED_TO_FLOAT].output_media_fmt.data_format = MFC_FLOATING_FORMAT;
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
      pc_ptr->core_lib.pc_proc_info[ENDIANNESS_POST].process           = mfc_osrc_endianness_process;
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
         // Ignore the ENDIANNESS_POST since it will have correct output
         // interleaving If only ENDIANNESS_POST is enabled, it will not be in
         // place, it will do DUP to DP or DP to DUP conversions If only
         // ENDIANNESS_PRE is enabled, this logic will provide correct interleaving
         // to ENDIANNESS_PRE If one of the buffering modules is enabled, this
         // logic will provide correct out interleaving info to it
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
   1. If the co-efficient pointer is not null, that means there is a Client
configuration for the given ch-map
   2. If the number of channels or ch-map from input to output doesn't match.
This means ch-mixer should be enabled with default co-efficient values
______________________________________________________________________________________________________________________*/
bool_t mfc_osrc_is_ch_mixer_needed(mfc_osrc_lib_t       *pc_ptr,
                             int16_t        *coef_set_ptr,
                             mfc_media_fmt_t *input_media_fmt_ptr,
                             mfc_media_fmt_t *output_media_fmt_ptr)
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

      // inp chmap == out chmap and coef ptr is not null, check if we need to
      // enable
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
ar_result_t mfc_osrc_reinit_ch_mixer(mfc_osrc_lib_t *pc_ptr, uint32_t channel_mixer_lib_size, void *coef_set_ptr)
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
   output_media_fmt_ptr - added additional param, to handle data format check
                                       in case of fixed to floating point conversion
______________________________________________________________________________________________________________________*/
ar_result_t mfc_osrc_check_lib_enable(mfc_osrc_lib_t       *pc_ptr,
                                bool_t         *enable,
                                bool_t          need_for_resampler,
                                bool_t          enable_for_deint_packed_unpacked_cnv,
                                mfc_media_fmt_t *input_media_fmt_ptr,
                                mfc_media_fmt_t *output_media_fmt_ptr)
{
   // If input data format is floating point and float_to_fixed conversion
   // process is not enabled and channel mixer and resampler flags are true, then
   // disable the module since these processes do not support floating point
   // data.
   if ((MFC_FLOATING_FORMAT == input_media_fmt_ptr->data_format) &&
       (TRUE != pc_ptr->core_lib.flags.DATA_CNV_FLOAT_TO_FIXED) &&
       (pc_ptr->core_lib.flags.CHANNEL_MIXER || need_for_resampler))
   {
      CNV_MSG(pc_ptr->miid,
              DBG_ERROR_PRIO,
              "Floating point data format is not supported by channel mixer %d "
              "or resampler %d",
              pc_ptr->core_lib.flags.CHANNEL_MIXER,
              need_for_resampler);
      *enable = FALSE;
      return AR_EOK;
   }

   // Diff byte_combo conversions are not allowed within floating point format.
   if ((MFC_FLOATING_FORMAT == input_media_fmt_ptr->data_format) &&
       (MFC_FLOATING_FORMAT == output_media_fmt_ptr->data_format) &&
       (input_media_fmt_ptr->byte_combo != output_media_fmt_ptr->byte_combo))
   {
      CNV_MSG(pc_ptr->miid,
              DBG_ERROR_PRIO,
              "Floating point data format conversion of diff bit width's is not "
              "allowed");
      *enable = FALSE;
      return AR_EOK;
   }

   // if any of the flags such as
   // ENDIANNESS_PRE,INT_DEINT_PRE,BYTE_CNV_PRE,CHANNEL_MIXER,BYTE_CNV_POST,INT_DEINT_POST
   // ,ENDIANNESS_POST or if we require a resampler then the module has to be
   // enabled
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
   4. If resampler is needed, allocates lib ptr memory for either of the
resamplers, frees other if present
   6. Based on enabled modules and media fmt, fill in the module process info
table.
   5. Creates or re-inits resampler if needed
   7. Reints ch-mixer library if needed
______________________________________________________________________________________________________________________*/

ar_result_t mfc_osrc_init(mfc_osrc_lib_t       *pc_ptr,
                    mfc_media_fmt_t *input_media_fmt_ptr,
                    mfc_media_fmt_t *output_media_fmt_ptr,
                    void           *coef_set_ptr,
                    uint32_t        heap_id,
                    bool_t         *lib_enable,
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
   // free the allocated mem and memset pc_ptr->core_lib to 0 so that the
   // pointers do not hold information about previous instance
   if (NULL != pc_ptr->core_lib.pc_mem_ptr)
   {
      posal_memory_aligned_free((void *)pc_ptr->core_lib.pc_mem_ptr);
      pc_ptr->core_lib.pc_mem_ptr = NULL;
   }
   memset(&(pc_ptr->core_lib), 0, sizeof(mfc_core_lib_t));

   mfc_osrc_identify_required_processes(pc_ptr,
                                  input_media_fmt_ptr,
                                  output_media_fmt_ptr,
                                  &need_for_resampler,
                                  &enable_for_deint_packed_unpacked_cnv,
                                  (int16_t *)coef_set_ptr,
                                  type);
   // passing additional param to handle format conversion check with in Floating point format
   mfc_osrc_check_lib_enable(pc_ptr,
                                lib_enable,
                                need_for_resampler,
                                enable_for_deint_packed_unpacked_cnv,
                                input_media_fmt_ptr,
                                output_media_fmt_ptr);
   if (*lib_enable == FALSE)
   {
      // if module is not enabled exit mfc_osrc_init
      return result;
   }

   result = mfc_osrc_realloc_chmixer_memory(pc_ptr,
                                      &channel_mixer_lib_size,
                                      max_num_of_channels,
                                      input_media_fmt_ptr->num_channels,
                                      output_media_fmt_ptr->num_channels,
                                      heap_id);
   if (result != AR_EOK)
   {
      return result;
   }

   result = mfc_osrc_check_alloc_resampler_memory(pc_ptr,
                                            heap_id,
                                            &fir_rs_reinit,
                                            input_media_fmt_ptr,
                                            output_media_fmt_ptr,
                                            need_for_resampler);

   if (result != AR_EOK)
   {
      return result;
   }

   bool_t reorder_allowed =
      mfc_osrc_is_reorder_allowed(pc_ptr, input_media_fmt_ptr, output_media_fmt_ptr, fir_rs_reinit);

   mfc_osrc_copy_media_fmt(pc_ptr, input_media_fmt_ptr, output_media_fmt_ptr);

   /* update resampler pre and post flags based the need for resampler and
    reorder allowed. Num of bufferring stages is also calculated.*/
   mfc_osrc_update_resampler_pos_and_buff_stages(pc_ptr, need_for_resampler, reorder_allowed);

   mfc_osrc_fill_proc_info(pc_ptr);

   result = mfc_osrc_check_create_resampler_instance(pc_ptr, heap_id, fir_rs_reinit);

   mfc_osrc_reinit_ch_mixer(pc_ptr, channel_mixer_lib_size, coef_set_ptr);

   return result;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Does PCM CONVERTER library deinit.
______________________________________________________________________________________________________________________*/

void mfc_osrc_deinit(mfc_osrc_lib_t *pc_ptr)
{
   if (NULL != pc_ptr->core_lib.pc_mem_ptr)
   {
      posal_memory_aligned_free(pc_ptr->core_lib.pc_mem_ptr);
      pc_ptr->core_lib.pc_mem_ptr = NULL;
   }

   if (NULL != pc_ptr->osrc_rs_lib_ptr)
   {
      osrc_rs_lib_deinit(pc_ptr->osrc_rs_lib_ptr);
      posal_memory_free(pc_ptr->osrc_rs_lib_ptr);
      pc_ptr->osrc_rs_lib_ptr = NULL;
   }
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Aggregates KPPS based on number of enabled modules.
   1. KPPS tables are calculated per channel at 48KHz.
   2. So KPPS scaling is needed before the aggregation.
   3. Added fw kpps for re-mapping buffer as well. This is scaled based on
number of active modules in the chain
______________________________________________________________________________________________________________________*/
void mfc_osrc_get_kpps_and_bw(mfc_osrc_lib_t *pc_ptr, uint32_t *kpps, uint32_t *bw)
{
   mfc_media_fmt_t *current_input_media_fmt_ptr = &pc_ptr->core_lib.input_media_fmt;
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
      // for endinaess and interleaving we calculate bandwidth based on num of
      // channels, num of bytes, sampling rate Bw calculation is multiplied by 2
      // because for every data copy we do a read+write operation
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
      // for byte conversion bandwidth based on num of channels, sampling rate and
      // sum of input and output num of bytes
      new_bw += (sr * num_channels *
                 ((pc_ptr->core_lib.input_media_fmt.bit_width + pc_ptr->core_lib.output_media_fmt.bit_width) >> 3));

      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.RESAMPLER_PRE)
   {
      new_kpps += osrc_rs_lib_get_kpps(pc_ptr->osrc_rs_lib_ptr);
      new_bw += osrc_rs_lib_get_bw(pc_ptr->osrc_rs_lib_ptr);

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
      // out ch * (-8 + num_samples(4 + (5* in_ch* num_active_coeff/(in_ch *
      // out_ch)))) + c
      // => out_ch * (-8 + (num_samples * 4) + (num_samples * 5*
      // num_coeff/out_ch)) + c sr/1000 translates to num samples
      uint64_t chmixer_kpps = (out_ch * (-8 + ((sr * 4) + (sr * 5 * num_active_coeff / out_ch)) / 1000)) + 225;

      new_kpps += chmixer_kpps;
      // for ch mixer, bandwidth based on num of bytes, sampling rate and sum of
      // input and output channels
      new_bw += (sr * num_bytes *
                 (pc_ptr->core_lib.output_media_fmt.num_channels + pc_ptr->core_lib.input_media_fmt.num_channels));

      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[CHANNEL_MIXER].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.RESAMPLER_POST)
   {
      new_kpps += osrc_rs_lib_get_kpps(pc_ptr->osrc_rs_lib_ptr);
      new_bw += osrc_rs_lib_get_bw(pc_ptr->osrc_rs_lib_ptr);
      
      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[RESAMPLER_POST].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.BYTE_CNV_POST)
   {
      uint32_t word_size    = pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.word_size;
      uint32_t num_channels = pc_ptr->core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt.num_channels;
      uint32_t sr           = current_input_media_fmt_ptr->sampling_rate;

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
      uint32_t sr           = current_input_media_fmt_ptr->sampling_rate;
      uint32_t num_bytes    = (pc_ptr->core_lib.input_media_fmt.bit_width >> 3);

      new_kpps += ((intl_dintl_kpps_per_ch[(word_size >> 3) - 2] * num_channels) * sr) / base_sample_rate;
      new_bw += (2 * sr * num_channels * num_bytes);

      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[INT_DEINT_POST].output_media_fmt;
   }

   if (pc_ptr->core_lib.flags.ENDIANNESS_POST)
   {
      uint32_t word_size    = pc_ptr->core_lib.pc_proc_info[ENDIANNESS_POST].output_media_fmt.word_size;
      uint32_t num_channels = pc_ptr->core_lib.pc_proc_info[ENDIANNESS_POST].output_media_fmt.num_channels;
      uint32_t sr           = current_input_media_fmt_ptr->sampling_rate;
      uint32_t num_bytes    = (pc_ptr->core_lib.input_media_fmt.bit_width >> 3);

      new_kpps += ((endianness_kpps_per_ch[(word_size >> 3) - 2] * num_channels) * sr) / base_sample_rate;
      new_bw += (2 * sr * num_channels * num_bytes);

      current_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[ENDIANNESS_POST].output_media_fmt;
   }

   uint32_t no_of_stages = pc_ptr->core_lib.flags.ENDIANNESS_PRE + pc_ptr->core_lib.flags.ENDIANNESS_POST +
                           pc_ptr->core_lib.flags.INT_DEINT_PRE + pc_ptr->core_lib.flags.INT_DEINT_POST +
                           pc_ptr->core_lib.flags.BYTE_CNV_PRE + pc_ptr->core_lib.flags.BYTE_CNV_POST +
                           pc_ptr->core_lib.flags.CHANNEL_MIXER;

   if ((0 == no_of_stages) &&
       (!pc_ptr->core_lib.flags.RESAMPLER_PRE) &&
       (!pc_ptr->core_lib.flags.RESAMPLER_POST))
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
uint32_t mfc_osrc_get_algo_delay(mfc_osrc_lib_t *pc_ptr)
{
   uint32_t algo_delay = 0;
   if ((pc_ptr->core_lib.flags.RESAMPLER_PRE) || (pc_ptr->core_lib.flags.RESAMPLER_POST))
   {
      algo_delay += osrc_rs_lib_get_alg_delay(pc_ptr->osrc_rs_lib_ptr);
   }
   return algo_delay;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Classifies the bit-width, word-size and q-factor to an enum
______________________________________________________________________________________________________________________*/
mfc_mf_combo_t mfc_osrc_classify_mf(mfc_media_fmt_t *mf_ptr)
{
   if (NULL == mf_ptr)
   {
      return MFC_INVALID_CMBO;
   }
   uint16_t word_size   = mf_ptr->word_size;
   uint16_t bit_width   = mf_ptr->bit_width;
   uint16_t q_factor    = mf_ptr->q_factor;
   uint16_t data_format = mf_ptr->data_format;

   if (MFC_FLOATING_FORMAT == data_format)
   {
      if ((32 == word_size) && (32 == bit_width))
      {
         return MFC_BW32_W32_FLOAT;
      }
      else if ((64 == word_size) && (64 == bit_width))
      {
         return MFC_BW64_W64_DOUBLE;
      }
   }

   if ((16 == word_size) && (PCM_Q_FACTOR_15 == q_factor) && (16 == bit_width))
   {
      return MFC_BW16_W16_Q15;
   }
   else if ((24 == word_size) && (PCM_Q_FACTOR_23 == q_factor) && (24 == bit_width))
   {
      return MFC_BW24_W24_Q23;
   }
   else if (32 == word_size)
   {
      if (PCM_Q_FACTOR_23 == q_factor)
      {
         if (24 == bit_width)
         {
            return MFC_BW24_W32_Q23;
         }
      }

      if (PCM_Q_FACTOR_27 == q_factor)
      {
         if (24 == bit_width)
         {
            return MFC_BW24_W32_Q27;
         }
      }
      else if (PCM_Q_FACTOR_31 == q_factor)
      {
         if (32 == bit_width)
         {
            return MFC_BW32_W32_Q31;
         }
         else if (24 == bit_width)
         {
            return MFC_BW24_W32_Q31;
         }
      }
   }
   return MFC_INVALID_CMBO;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Sets the container frame size on the library instance.
   The OSResampler does not have IIR-specific frame-size locking, so this
   simply stores the value for use by the CAPI layer (e.g. DM fractional check).
______________________________________________________________________________________________________________________*/
void mfc_osrc_set_cntr_frame_size(mfc_osrc_lib_t *pc_ptr, uint32_t cntr_frame_size_us)
{
   pc_ptr->cntr_frame_size_us = cntr_frame_size_us;
   pc_ptr->frame_size_us      = cntr_frame_size_us;

   CNV_MSG(pc_ptr->miid,
           DBG_HIGH_PRIO,
           "OSResampler: cntr_frame_size_us set to %lu us",
           pc_ptr->cntr_frame_size_us);

   /* Propagate frame size to the OSResampler wrapper so the multi-frame
    * processing loop can consume all available complete frames per call,
    * matching the IIR resampler looping behaviour in MODULE_ID_MFC. */
   if (pc_ptr->osrc_rs_lib_ptr &&
       pc_ptr->core_lib.input_media_fmt.sampling_rate > 0 &&
       pc_ptr->core_lib.output_media_fmt.sampling_rate > 0 &&
       cntr_frame_size_us > 0)
   {
      uint32_t in_sr  = pc_ptr->core_lib.input_media_fmt.sampling_rate;
      uint32_t out_sr = pc_ptr->core_lib.output_media_fmt.sampling_rate;
      uint32_t in_fs  = (in_sr  * cntr_frame_size_us + 999999) / 1000000;  /* ceil */
      uint32_t out_fs = (out_sr * cntr_frame_size_us + 999999) / 1000000;

      osrc_rs_lib_set_frame_size(pc_ptr->osrc_rs_lib_ptr, in_fs, out_fs);

      CNV_MSG(pc_ptr->miid,
              DBG_HIGH_PRIO,
              "OSResampler: frame_size in=%lu out=%lu samples/ch",
              in_fs, out_fs);
   }
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Sets the consume-partial-input flag on the library instance.
   The OSResampler does not need IIR-specific minimum-frame-size switching,
   so only the flag is stored for the CAPI process path to read.
______________________________________________________________________________________________________________________*/
void mfc_osrc_set_consume_partial_input(mfc_osrc_lib_t *pc_ptr, bool_t consume_partial_input)
{
   pc_ptr->consume_partial_input = consume_partial_input;

   CNV_MSG(pc_ptr->miid,
           DBG_HIGH_PRIO,
           "OSResampler: consume_partial_input set to %d",
           (int)consume_partial_input);
}
