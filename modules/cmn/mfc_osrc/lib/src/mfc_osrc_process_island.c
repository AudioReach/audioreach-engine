/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "mfc_osrc_converter.h"

// Use to override AR_MSG with AR_MSG_ISLAND. Always include this after ar_msg.h
// #ifdef AR_MSG_IN_ISLAND
// #include "ar_msg_island_override.h"
// #endif

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   OSResampler process function — installed as the vtable entry for
   RESAMPLER_PRE (stage 4) and RESAMPLER_POST (stage 6).
   Replaces the resampler stage of the original pcm_mf_cnv pipeline.
______________________________________________________________________________________________________________________*/
ar_result_t mfc_osrc_resampler_process(void           *_pif,
                                      capi_buf_t     *input_buf_ptr,
                                      capi_buf_t     *output_buf_ptr,
                                      mfc_media_fmt_t *input_media_fmt_ptr,
                                      mfc_media_fmt_t *output_media_fmt_ptr)
{
   ar_result_t result  = AR_EOK;
   mfc_osrc_lib_t   *me_ptr  = (mfc_osrc_lib_t *)_pif;
   
   #ifdef PCM_CNV_DEBUG
   CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
		  "process mfc_osrc_resampler_process pre: in_actual_after=%lu out_actual_after=%lu",
		  input_buf_ptr->actual_data_len,
		  output_buf_ptr->actual_data_len);
   #endif

   /* Prepare buffer length fields per-format before calling the lib.
    *
    * INTERLEAVED / DEINTERLEAVED_PACKED:
    *   bufs_num == 1 — nothing to synchronise.
    *
    * DEINTERLEAVED_UNPACKED (V1):
    *   All buf[ch].actual_data_len and max_data_len must be equal.
    *   Sync buf[1..N-1] from buf[0] so the lib sees consistent values.
    *
    * DEINTERLEAVED_UNPACKED_V2:
    *   Only buf[0] carries valid lengths; buf[1..N-1] must be zero.
    *   The lib reads data_ptr per channel without consulting their lengths,
    *   so no sync is needed — leave buf[1..N-1] at whatever the framework set.
    *   The wrapper (osrc_rs_lib_process) will zero them back on return.
    */
   /*if (MFC_DEINTERLEAVED_UNPACKED_V2 != input_media_fmt_ptr->interleaving &&
       input_media_fmt_ptr->num_channels > 1)
   {
      for (uint32_t i = 1; i < input_media_fmt_ptr->num_channels; i++)
      {
         input_buf_ptr[i].actual_data_len = input_buf_ptr[0].actual_data_len;
         input_buf_ptr[i].max_data_len    = input_buf_ptr[0].max_data_len;
      }
   }
   if (MFC_DEINTERLEAVED_UNPACKED_V2 != output_media_fmt_ptr->interleaving &&
       output_media_fmt_ptr->num_channels > 1)
   {
      for (uint32_t i = 1; i < output_media_fmt_ptr->num_channels; i++)
      {
         output_buf_ptr[i].actual_data_len = output_buf_ptr[0].actual_data_len;
         output_buf_ptr[i].max_data_len    = output_buf_ptr[0].max_data_len;
      }
   }*/

   /* Call OSResampler wrapper — passes capi_buf_t arrays directly;
    * wrapper sets actual_data_len on each output channel buffer. */
   result = osrc_rs_lib_process(me_ptr->osrc_rs_lib_ptr,
                                input_buf_ptr,
                                output_buf_ptr,
                                input_media_fmt_ptr->num_channels,
                                output_media_fmt_ptr->num_channels,
                                me_ptr->heap_id);

   /* Post-call length fixup — enforce CAPI buf_ptr length contract per format.
    *
    * DEINTERLEAVED_UNPACKED_V2:
    *   The wrapper updated only buf[0]. buf[1..N-1] must be 0 per V2 contract.
    *   They were already 0 entering the wrapper (pre-call sync skipped for V2)
    *   and the wrapper no longer touches them, so no action needed for V2.
    *
    * DEINTERLEAVED_UNPACKED (V1):
    *   The wrapper updated only buf[0]. Propagate buf[0]'s updated lengths back
    *   to buf[1..N-1] so all channels reflect the same consumed/produced bytes.
    *
    * INTERLEAVED / DEINTERLEAVED_PACKED:
    *   bufs_num == 1 — no secondary channels exist.
    */
   if (MFC_DEINTERLEAVED_UNPACKED_V2 != input_media_fmt_ptr->interleaving &&
       input_media_fmt_ptr->num_channels > 1)
   {
      for (uint32_t i = 1; i < input_media_fmt_ptr->num_channels; i++)
      {
         input_buf_ptr[i].actual_data_len = input_buf_ptr[0].actual_data_len;
      }
   }
   else if (MFC_DEINTERLEAVED_UNPACKED_V2 == input_media_fmt_ptr->interleaving)
   {
      /*for (uint32_t i = 1; i < input_media_fmt_ptr->num_channels; i++)
      {
         input_buf_ptr[i].actual_data_len = 0;
      }*/
	  // Do Nothing
   }

   if (MFC_DEINTERLEAVED_UNPACKED_V2 != output_media_fmt_ptr->interleaving &&
       output_media_fmt_ptr->num_channels > 1)
   {
      for (uint32_t i = 1; i < output_media_fmt_ptr->num_channels; i++)
      {
         output_buf_ptr[i].actual_data_len = output_buf_ptr[0].actual_data_len;
      }
   }
   else if (MFC_DEINTERLEAVED_UNPACKED_V2 == output_media_fmt_ptr->interleaving)
   {
      /*for (uint32_t i = 1; i < output_media_fmt_ptr->num_channels; i++)
      {
         output_buf_ptr[i].actual_data_len = 0;
      }*/
	  // Do Nothing
   }
   
   #ifdef PCM_CNV_DEBUG
   CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
		  "process mfc_osrc_resampler_process post: in_actual_after=%lu out_actual_after=%lu",
		  input_buf_ptr->actual_data_len,
		  output_buf_ptr->actual_data_len);
   #endif

   return result;
}

ar_result_t mfc_osrc_interleaving_process(void           *me_ptr,
                                         capi_buf_t     *input_buf_ptr,
                                         capi_buf_t     *output_buf_ptr,
                                         mfc_media_fmt_t *input_media_fmt_ptr,
                                         mfc_media_fmt_t *output_media_fmt_ptr)
{
   ar_result_t result = AR_EOK;

   if (MFC_INTERLEAVED == input_media_fmt_ptr->interleaving)
   {
      uint32_t num_samp_per_ch =
         input_buf_ptr->actual_data_len / (input_media_fmt_ptr->num_channels * input_media_fmt_ptr->word_size >> 3);
      uint32_t bytes_per_samp = input_media_fmt_ptr->word_size >> 3;

      result = spf_intlv_to_deintlv_unpacked_v2(input_buf_ptr,
                                                output_buf_ptr,
                                                input_media_fmt_ptr->num_channels,
                                                bytes_per_samp,
                                                num_samp_per_ch);
   }
   else
   {
      result = spf_deintlv_to_intlv(input_buf_ptr,
                                    output_buf_ptr,
                                    input_media_fmt_ptr->num_channels,
                                    input_media_fmt_ptr->word_size);
   }

   return result;
}

ar_result_t mfc_osrc_byte_morph_process(void           *me_ptr,
                                       capi_buf_t     *input_buf_ptr,
                                       capi_buf_t     *output_buf_ptr,
                                       mfc_media_fmt_t *input_media_fmt_ptr,
                                       mfc_media_fmt_t *output_media_fmt_ptr)
{
   ar_result_t    result = AR_EOK;
   mfc_osrc_lib_t *pc_ptr = (mfc_osrc_lib_t *)me_ptr;

   if (MFC_INTERLEAVED == input_media_fmt_ptr->interleaving)
   {
      if (MFC_BW16_W16_Q15 == output_media_fmt_ptr->byte_combo)
      {
         result = mfc_osrc_intlv_16_out(input_buf_ptr,
                                       output_buf_ptr,
                                       input_media_fmt_ptr->word_size,
                                       input_media_fmt_ptr->q_factor);
      }
      else if (MFC_BW24_W24_Q23 == output_media_fmt_ptr->byte_combo)
      {
         result = mfc_osrc_intlv_24_out(input_buf_ptr,
                                       output_buf_ptr,
                                       input_media_fmt_ptr->word_size,
                                       input_media_fmt_ptr->q_factor);
      }
      else if ((MFC_BW24_W32_Q23 == output_media_fmt_ptr->byte_combo) ||
               (MFC_BW24_W32_Q27 == output_media_fmt_ptr->byte_combo) ||
               (MFC_BW24_W32_Q31 == output_media_fmt_ptr->byte_combo) ||
               (MFC_BW32_W32_Q31 == output_media_fmt_ptr->byte_combo))
      {
         result = mfc_osrc_intlv_32_out(input_buf_ptr,
                                       output_buf_ptr,
                                       input_media_fmt_ptr->word_size,
                                       input_media_fmt_ptr->q_factor,
                                       output_media_fmt_ptr->q_factor);
      }
      else
      {
         CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "Unsupported byte combo ");
      }
   }
   else
   {
      if (MFC_BW16_W16_Q15 == output_media_fmt_ptr->byte_combo)
      {
         result = mfc_osrc_deintlv_unpacked_v2_16_out(input_buf_ptr,
                                                     output_buf_ptr,
                                                     input_media_fmt_ptr->num_channels,
                                                     input_media_fmt_ptr->word_size,
                                                     input_media_fmt_ptr->q_factor);
      }
      else if (MFC_BW24_W24_Q23 == output_media_fmt_ptr->byte_combo)
      {
         result = mfc_osrc_deintlv_unpacked_v2_24_out(input_buf_ptr,
                                                     output_buf_ptr,
                                                     input_media_fmt_ptr->num_channels,
                                                     input_media_fmt_ptr->word_size,
                                                     input_media_fmt_ptr->q_factor);
      }
      else if ((MFC_BW24_W32_Q23 == output_media_fmt_ptr->byte_combo) ||
               (MFC_BW24_W32_Q27 == output_media_fmt_ptr->byte_combo) ||
               (MFC_BW24_W32_Q31 == output_media_fmt_ptr->byte_combo) ||
               (MFC_BW32_W32_Q31 == output_media_fmt_ptr->byte_combo))
      {
         result = mfc_osrc_deintlv_unpacked_v2_32_out(input_buf_ptr,
                                                     output_buf_ptr,
                                                     input_media_fmt_ptr->num_channels,
                                                     input_media_fmt_ptr->word_size,
                                                     input_media_fmt_ptr->q_factor,
                                                     output_media_fmt_ptr->q_factor);
      }
      else
      {
         CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "Unsupported byte combo ");
      }
   }

   return result;
}

ar_result_t mfc_osrc_channel_mix_process(void           *me_ptr,
                                        capi_buf_t     *input_buf_ptr,
                                        capi_buf_t     *output_buf_ptr,
                                        mfc_media_fmt_t *input_media_fmt_ptr,
                                        mfc_media_fmt_t *output_media_fmt_ptr)
{
   ar_result_t    result                                     = AR_EOK;
   mfc_osrc_lib_t *pc_ptr                                     = (mfc_osrc_lib_t *)me_ptr;
   void          *local_input_buf_ptr[CAPI_MAX_CHANNELS_V2]  = { NULL };
   void          *local_output_buf_ptr[CAPI_MAX_CHANNELS_V2] = { NULL };
   uint32_t       bytes_to_process                           = 0;
   uint32_t       samples_to_process                         = 0;
   uint32_t       byte_sample_convert                        = 0;

   for (uint32_t i = 0; i < input_media_fmt_ptr->num_channels; i++)
   {
      local_input_buf_ptr[i] = (void *)input_buf_ptr[i].data_ptr;
   }
   for (uint32_t ch = 0; ch < output_media_fmt_ptr->num_channels; ch++)
   {
      local_output_buf_ptr[ch] = (void *)output_buf_ptr[ch].data_ptr;
   }

   bytes_to_process    = input_buf_ptr[0].actual_data_len;
   byte_sample_convert = (input_media_fmt_ptr->word_size == 16) ? 1 : 2;
   samples_to_process  = bytes_to_process >> byte_sample_convert;

   ChMixerProcess(pc_ptr->ch_lib_ptr, local_output_buf_ptr, local_input_buf_ptr, samples_to_process);

   /* Update buf[0] lengths for all formats. For V1 sync to all channels; for V2
    * leave buf[1..N-1] at 0 per CAPI contract. */
   input_buf_ptr[0].actual_data_len  = samples_to_process << byte_sample_convert;
   output_buf_ptr[0].actual_data_len = samples_to_process << byte_sample_convert;
   if (MFC_DEINTERLEAVED_UNPACKED_V2 != input_media_fmt_ptr->interleaving)
   {
      for (uint32_t ch = 1; ch < input_media_fmt_ptr->num_channels; ch++)
      {
         input_buf_ptr[ch].actual_data_len  = input_buf_ptr[0].actual_data_len;
         output_buf_ptr[ch].actual_data_len = output_buf_ptr[0].actual_data_len;
      }
   }

   return result;
}

static void mfc_osrc_remap_from_main_buf(capi_buf_t     *main_buf_ptr,
                                        capi_buf_t     *remapped_buf_ptr,
                                        capi_buf_t     *remapped_input_ref_buf_ptr,
                                        mfc_media_fmt_t *media_fmt_ptr,
                                        mfc_media_fmt_t *ref_input_media_fmt_ptr,
                                        bool_t          is_input_side,
                                        bool_t          is_in_or_out_buf)
{
   uint32_t ch_spacing        = 0;
   uint32_t alignement_offset = 0;

   if (MFC_INTERLEAVED == media_fmt_ptr->interleaving)
   {
      remapped_buf_ptr->data_ptr        = main_buf_ptr->data_ptr;
      remapped_buf_ptr->actual_data_len = main_buf_ptr->actual_data_len;
      remapped_buf_ptr->max_data_len    = main_buf_ptr->max_data_len;
   }
   else if (MFC_DEINTERLEAVED_PACKED == media_fmt_ptr->interleaving)
   {
      if (is_input_side)
      {
         ch_spacing = main_buf_ptr->actual_data_len / media_fmt_ptr->num_channels;
      }
      else
      {
         if (MFC_INTERLEAVED == ref_input_media_fmt_ptr->interleaving)
         {
            ch_spacing = (remapped_input_ref_buf_ptr->actual_data_len * (media_fmt_ptr->word_size >> 3) *
                          (media_fmt_ptr->num_channels)) /
                         ((ref_input_media_fmt_ptr->word_size >> 3) * (ref_input_media_fmt_ptr->num_channels));
         }
         else
         {
            ch_spacing = remapped_input_ref_buf_ptr->actual_data_len * (media_fmt_ptr->word_size >> 3) /
                         (ref_input_media_fmt_ptr->word_size >> 3);
         }
      }
      alignement_offset = ch_spacing % 4;
      ch_spacing        = ch_spacing - alignement_offset;
      for (uint32_t ch = 0; ch < media_fmt_ptr->num_channels; ch++)
      {
         remapped_buf_ptr[ch].data_ptr = main_buf_ptr->data_ptr + (ch_spacing * ch);
      }
      remapped_buf_ptr[0].actual_data_len = is_input_side ? ch_spacing : 0;
      remapped_buf_ptr[0].max_data_len    = ch_spacing;
   }
   else if (MFC_DEINTERLEAVED_UNPACKED_V2 == media_fmt_ptr->interleaving)
   {
      if (is_in_or_out_buf)
      {
         for (uint32_t ch = 0; ch < media_fmt_ptr->num_channels; ch++)
         {
            remapped_buf_ptr[ch].data_ptr = main_buf_ptr[ch].data_ptr;
         }
         remapped_buf_ptr[0].actual_data_len = main_buf_ptr[0].actual_data_len;
         remapped_buf_ptr[0].max_data_len    = main_buf_ptr[0].max_data_len;
      }
      else
      {
         ch_spacing = is_input_side ? main_buf_ptr->actual_data_len / media_fmt_ptr->num_channels
                                    : main_buf_ptr->max_data_len / media_fmt_ptr->num_channels;
         alignement_offset = ch_spacing % 4;
         ch_spacing        = ch_spacing - alignement_offset;
         for (uint32_t ch = 0; ch < media_fmt_ptr->num_channels; ch++)
         {
            remapped_buf_ptr[ch].data_ptr = main_buf_ptr->data_ptr + (ch_spacing * ch);
         }
         remapped_buf_ptr[0].actual_data_len = is_input_side ? ch_spacing : 0;
         remapped_buf_ptr[0].max_data_len    = ch_spacing;
      }
   }
}

static void mfc_osrc_remap_buffer(mfc_osrc_lib_t  *pc_ptr,
                                 capi_buf_t     *input_buf_ptr,
                                 capi_buf_t     *output_buf_ptr,
                                 capi_buf_t     *scratch_buf_ptr_1,
                                 capi_buf_t     *scratch_buf_ptr_2,
                                 capi_buf_t     *remapped_ib_ptr,
                                 capi_buf_t     *remapped_ob_ptr,
                                 mfc_media_fmt_t *input_media_fmt_ptr,
                                 mfc_media_fmt_t *output_media_fmt_ptr,
                                 bool_t          is_input)
{
   capi_buf_t     *main_buf_ptr     = NULL;
   bool_t          is_in_out_buf    = FALSE;
   capi_buf_t     *remapped_buf_ptr = NULL;
   mfc_media_fmt_t *media_fmt_ptr    = NULL;

   if (is_input)
   {
      remapped_buf_ptr = remapped_ib_ptr;
      media_fmt_ptr    = input_media_fmt_ptr;
   }
   else
   {
      remapped_buf_ptr = remapped_ob_ptr;
      media_fmt_ptr    = output_media_fmt_ptr;
   }

   if (pc_ptr->core_lib.remap_input_buf_ptr == remapped_buf_ptr)
   {
      is_in_out_buf = TRUE;
      main_buf_ptr  = input_buf_ptr;
   }
   else if (pc_ptr->core_lib.remap_scratch_buf1_ptr == remapped_buf_ptr)
   {
      main_buf_ptr = scratch_buf_ptr_1;
   }
   else if (pc_ptr->core_lib.remap_scratch_buf2_ptr == remapped_buf_ptr)
   {
      main_buf_ptr = scratch_buf_ptr_2;
   }
   else if (pc_ptr->core_lib.remap_output_buf_ptr == remapped_buf_ptr)
   {
      is_in_out_buf = TRUE;
      main_buf_ptr  = output_buf_ptr;
   }
   else
   {
      CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO, "mfc_osrc_remap_buffer: WARNING! BUFFER NOT MATCHED");
      return;
   }

   mfc_osrc_remap_from_main_buf(main_buf_ptr,
                               remapped_buf_ptr,
                               remapped_ib_ptr,
                               media_fmt_ptr,
                               input_media_fmt_ptr,
                               is_input,
                               is_in_out_buf);
}

static ar_result_t mfc_osrc_calc_and_update_max_in_len(mfc_osrc_lib_t *pc_ptr,
                                                      capi_buf_t    *input_buf_ptr,
                                                      capi_buf_t    *output_buf_ptr,
                                                      capi_buf_t    *remapped_ib_ptr)
{
   uint32_t        in_len_per_ch  = 0;
   uint32_t        out_len_per_ch = 0;
   mfc_media_fmt_t *imf_ptr        = &pc_ptr->core_lib.input_media_fmt;
   mfc_media_fmt_t *omf_ptr        = &pc_ptr->core_lib.output_media_fmt;

   if ((MFC_INTERLEAVED == imf_ptr->interleaving) || (MFC_DEINTERLEAVED_PACKED == imf_ptr->interleaving))
   {
      in_len_per_ch = input_buf_ptr->actual_data_len / imf_ptr->num_channels;
   }
   else
   {
      in_len_per_ch = input_buf_ptr->actual_data_len;
   }

   if ((MFC_INTERLEAVED == omf_ptr->interleaving) || (MFC_DEINTERLEAVED_PACKED == omf_ptr->interleaving))
   {
      out_len_per_ch = output_buf_ptr->max_data_len / omf_ptr->num_channels;
   }
   else
   {
      out_len_per_ch = output_buf_ptr->max_data_len;
   }

   uint32_t out_bytes_per_sample  = omf_ptr->word_size >> 3;
   uint32_t in_bytes_per_sample   = imf_ptr->word_size >> 3;
   uint32_t output_samples_per_ch = out_len_per_ch / out_bytes_per_sample;

   /* Mirror the original MFC's pc_calc_and_update_max_in_len multi-frame logic:
    * if a per-call frame size has been set (via osrc_rs_lib_set_frame_size), compute
    * proc_multiplier = min(in_frames_avail, out_frames_avail) and allow the full
    * multiplied frame count to be fed to the resampler in one process() call.
    * This matches the IIR resampler's internal while-loop behaviour and ensures
    * a 4096-byte write buffer is drained in a single capi process() call. */
   uint32_t max_in_bytes_per_ch;
   if (pc_ptr->osrc_rs_lib_ptr &&
       pc_ptr->osrc_rs_lib_ptr->in_frame_samples_per_ch > 0 &&
       pc_ptr->osrc_rs_lib_ptr->out_frame_samples_per_ch > 0)
   {
      uint32_t in_fs  = pc_ptr->osrc_rs_lib_ptr->in_frame_samples_per_ch;
      uint32_t out_fs = pc_ptr->osrc_rs_lib_ptr->out_frame_samples_per_ch;
      uint32_t in_len_samp = in_len_per_ch / in_bytes_per_sample;

      /* Must have at least one complete input frame. */
      if (in_len_samp < in_fs || output_samples_per_ch < out_fs)
      {
         return AR_ENEEDMORE;
      }

      uint32_t in_proc_mult  = in_len_samp      / in_fs;
      uint32_t out_proc_mult = output_samples_per_ch / out_fs;
      uint32_t proc_mult     = (in_proc_mult < out_proc_mult) ? in_proc_mult : out_proc_mult;

      max_in_bytes_per_ch = proc_mult * in_fs * in_bytes_per_sample;
   }
   else
   {
      /* Frame size not yet known — cap to whatever the output buffer can hold. */
      max_in_bytes_per_ch = output_samples_per_ch * in_bytes_per_sample;
   }

   if (in_len_per_ch > max_in_bytes_per_ch)
   {
      if ((MFC_INTERLEAVED == imf_ptr->interleaving) || (MFC_DEINTERLEAVED_PACKED == imf_ptr->interleaving))
      {
         input_buf_ptr->actual_data_len = max_in_bytes_per_ch * imf_ptr->num_channels;
      }
      else
      {
         input_buf_ptr->actual_data_len = max_in_bytes_per_ch;
      }
   }

   /* Remap input pointer to the internal scratch/remap buffer. */
   mfc_osrc_remap_from_main_buf(input_buf_ptr,
                               remapped_ib_ptr,
                               remapped_ib_ptr,
                               imf_ptr,
                               imf_ptr,
                               TRUE,
                               TRUE);

   return AR_EOK;
}

ar_result_t mfc_osrc_process(mfc_osrc_lib_t *pc_ptr,
                            capi_buf_t    *input_buf_ptr,
                            capi_buf_t    *output_buf_ptr,
                            capi_buf_t    *scratch_buf_ptr_1,
                            capi_buf_t    *scratch_buf_ptr_2)
{
   ar_result_t     result                   = AR_EOK;
   capi_buf_t     *next_input_buf_ptr       = pc_ptr->core_lib.remap_input_buf_ptr;
   mfc_media_fmt_t *next_input_media_fmt_ptr = &pc_ptr->core_lib.input_media_fmt;
   bool_t          any_process_done         = FALSE;

   mfc_osrc_remap_buffer(pc_ptr,
                        input_buf_ptr, output_buf_ptr,
                        scratch_buf_ptr_1, scratch_buf_ptr_2,
                        next_input_buf_ptr, NULL,
                        next_input_media_fmt_ptr, NULL,
                        TRUE);

   if (AR_ENEEDMORE == mfc_osrc_calc_and_update_max_in_len(pc_ptr,
                                                          input_buf_ptr,
                                                          output_buf_ptr,
                                                          next_input_buf_ptr))
   {
      return AR_ENEEDMORE;
   }

   for (uint32_t i = 0; i < NUMBER_OF_PROCESS; i++)
   {
      if (NULL != pc_ptr->core_lib.pc_proc_info[i].process)
      {
         any_process_done                       = TRUE;
         capi_buf_t     *current_input_buf_ptr  = next_input_buf_ptr;
         capi_buf_t     *current_output_buf_ptr = pc_ptr->core_lib.pc_proc_info[i].output_buffer_ptr;
         mfc_media_fmt_t *output_media_fmt_ptr   = &pc_ptr->core_lib.pc_proc_info[i].output_media_fmt;

         mfc_osrc_remap_buffer(pc_ptr,
                              input_buf_ptr, output_buf_ptr,
                              scratch_buf_ptr_1, scratch_buf_ptr_2,
                              current_input_buf_ptr, current_output_buf_ptr,
                              next_input_media_fmt_ptr, output_media_fmt_ptr,
                              FALSE);

         result = pc_ptr->core_lib.pc_proc_info[i].process((void *)pc_ptr,
                                                           current_input_buf_ptr,
                                                           current_output_buf_ptr,
                                                           next_input_media_fmt_ptr,
                                                           output_media_fmt_ptr);

         if (current_output_buf_ptr == pc_ptr->core_lib.remap_output_buf_ptr)
         {
            output_buf_ptr->actual_data_len = current_output_buf_ptr->actual_data_len;
         }

         next_input_media_fmt_ptr = &pc_ptr->core_lib.pc_proc_info[i].output_media_fmt;
         next_input_buf_ptr       = current_output_buf_ptr;
      }
   }

   if (FALSE == any_process_done)
   {
      /* No pipeline stages active — copy input to output if not already in-place. */
      if (MFC_INTERLEAVED == pc_ptr->core_lib.input_media_fmt.interleaving ||
          MFC_DEINTERLEAVED_PACKED == pc_ptr->core_lib.input_media_fmt.interleaving)
      {
         if (input_buf_ptr->data_ptr != output_buf_ptr->data_ptr)
         {
            output_buf_ptr->actual_data_len = memscpy(output_buf_ptr->data_ptr,
                                                      output_buf_ptr->max_data_len,
                                                      input_buf_ptr->data_ptr,
                                                      input_buf_ptr->actual_data_len);
         }
         else
         {
            output_buf_ptr->actual_data_len = input_buf_ptr->actual_data_len;
         }
      }
      else
      {
         /* DEINTERLEAVED_UNPACKED_V2 — copy per-channel. */
         uint32_t num_bytes_to_copy = MIN(output_buf_ptr[0].max_data_len, input_buf_ptr[0].actual_data_len);
         for (uint32_t ch = 0; ch < pc_ptr->core_lib.input_media_fmt.num_channels; ch++)
         {
            memscpy(output_buf_ptr[ch].data_ptr, num_bytes_to_copy,
                    input_buf_ptr[ch].data_ptr, num_bytes_to_copy);
         }
         output_buf_ptr[0].actual_data_len = num_bytes_to_copy;
      }
   }

   /* Do NOT modify input_buf_ptr->actual_data_len here.
    * For requires-data-buffering=FALSE modules (e.g. MFC_OSRC interleaving-only path)
    * the framework computes remaining_size_after = prev_actual - after_actual.
    * If we zero actual_data_len the framework sees remaining=original and triggers
    * an unconsumed-input drop + buffer restore, blocking write-buffer returns.
    * The convention (matching pcm_mf_cnv's pc_process) is to leave input actual_data_len
    * untouched — the framework manages the input buffer lifecycle for non-buffering modules. */

   return result;
}

