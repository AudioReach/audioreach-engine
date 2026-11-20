/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/*========================================================================
Edit History


when       who     what, where, why
--------   ---     -------------------------------------------------------
7/15/26   vasharma    Added file for opensource resampler based MFC

========================================================================== */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */

#include "mfc_osrc_converter.h"

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
Convert floating point input data to 32 bit (Q31) fixed point data, conversion to other bit widths is subsequently taken
care by byte morph process.
______________________________________________________________________________________________________________________*/

ar_result_t mfc_osrc_float_to_fixed_conv_process(void *          me_ptr,
                                           capi_buf_t *    input_buf_ptr,
                                           capi_buf_t *    output_buf_ptr,
                                           mfc_media_fmt_t *input_media_fmt_ptr,
                                           mfc_media_fmt_t *output_media_fmt_ptr)
{
   ar_result_t result        = AR_EOK;
   uint16_t    bit_width_in  = input_media_fmt_ptr->bit_width;
   uint16_t    word_size_in  = input_media_fmt_ptr->word_size;
   uint16_t    word_size_out = output_media_fmt_ptr->word_size;
   uint32_t    samp          = 0;

   // num_samples_in represents total samples for interleaved data, and samples per ch for deint data
   uint32_t num_samples_in =
      input_buf_ptr->actual_data_len / (word_size_in >> 3); // Calculate number of samples of input data

#ifdef MFC_OSRC_LIB_DEBUG
   mfc_osrc_lib_t *pc_ptr = (mfc_osrc_lib_t *)me_ptr;
   CNV_MSG(pc_ptr->miid,
           DBG_HIGH_PRIO,
           "Float to fixed: Word_size_in %d num_samples_in %d word_size_out %d, interleaving "
           "%d",
           word_size_in,
           num_samples_in,
           word_size_out,
           input_media_fmt_ptr->interleaving);
#endif

   const uint32_t iq31   = (1 << PCM_Q_FACTOR_31);
   const uint64_t iq31_1 = (1ll << PCM_Q_FACTOR_31);

   if (BIT_WIDTH_32 == bit_width_in)
   {
      int32_t         num32      = 0;
      float32_t *     buf_ptr_32 = NULL; // Type cast input buffer to float data format
      int32_t *       dst_ptr_32 = NULL;
      const float32_t fq31       = (float32_t)(iq31);

      if (MFC_INTERLEAVED == input_media_fmt_ptr->interleaving)
      {
         buf_ptr_32 = (float32_t *)(input_buf_ptr->data_ptr); /* Input buffer  */
         dst_ptr_32 = (int32_t *)(output_buf_ptr->data_ptr);  /* Output buffer */

         for (samp = 0; samp < num_samples_in; samp++)
         {
            // Operate on each input sample and converting to fixed point Q31 format
            num32 = (int32_t)(*buf_ptr_32++ * fq31);
            // Write converted sample to output buffer
            *dst_ptr_32++ = num32;
         }
         output_buf_ptr->actual_data_len = num_samples_in * (word_size_out >> 3);
      }
      else
      {
         for (uint32_t i = 0; i < input_media_fmt_ptr->num_channels; i++)
         {
            buf_ptr_32 = (float32_t *)(input_buf_ptr[i].data_ptr); /* Input buffer  */
            dst_ptr_32 = (int32_t *)(output_buf_ptr[i].data_ptr);  /* Output buffer */

            for (samp = 0; samp < num_samples_in; samp++)
            {
               // Operate on each input sample and converting to fixed point Q31 format
               num32 = (int32_t)(*buf_ptr_32++ * fq31);
               // Write converted sample to output buffer
               *dst_ptr_32++ = num32;
            }
         }

         // optimization: write/read only first ch lens, and assume same lens for rest of the chs in the PC library.
         output_buf_ptr[0].actual_data_len = num_samples_in * (word_size_out >> 3);
      }
   }
   else if (BIT_WIDTH_64 == bit_width_in)
   {
      int32_t         num32      = 0;
      float64_t *     buf_ptr_64 = NULL;
      int32_t *       dst_ptr_32 = NULL;
      const float64_t dq31       = (float64_t)(iq31_1);

      if (MFC_INTERLEAVED == input_media_fmt_ptr->interleaving)
      {
         buf_ptr_64 = (float64_t *)(input_buf_ptr->data_ptr); /* Input buffer  */
         dst_ptr_32 = (int32_t *)(output_buf_ptr->data_ptr);  /* Output buffer */

         for (samp = 0; samp < num_samples_in; samp++)
         {
            // Operate on each input sample and converting to fixed point Q31 format
            num32 = (int32_t)(*buf_ptr_64++ * dq31);
            // Write converted sample to output buffer
            *dst_ptr_32++ = num32;
         }
         output_buf_ptr->actual_data_len = num_samples_in * (word_size_out >> 3);
      }
      else
      {
         for (uint32_t i = 0; i < input_media_fmt_ptr->num_channels; i++)
         {
            buf_ptr_64 = (float64_t *)(input_buf_ptr[i].data_ptr); /* Input buffer  */
            dst_ptr_32 = (int32_t *)(output_buf_ptr[i].data_ptr);  /* Output buffer */

            for (samp = 0; samp < num_samples_in; samp++)
            {
               // Operate on each input sample and converting to fixed point Q31 format
               num32 = (int32_t)(*buf_ptr_64++ * dq31);
               // Write converted sample to output buffer
               *dst_ptr_32++ = num32;
            }
         }
         // optimization: write/read only first ch lens, and assume same lens for rest of the chs in the PC library.
         output_buf_ptr[0].actual_data_len = num_samples_in * (word_size_out >> 3);
      }
   }
   else
   {
      result = AR_EUNSUPPORTED;
      return result;
   }

   return result;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
Convert 32 bit (Q31) Fixed point input data to Floating point, conversion from any other bit width to 32 bit fixed point
data is done by byte morph process before this function is called.
______________________________________________________________________________________________________________________*/

ar_result_t mfc_osrc_fixed_to_float_conv_process(void *          me_ptr,
                                           capi_buf_t *    input_buf_ptr,
                                           capi_buf_t *    output_buf_ptr,
                                           mfc_media_fmt_t *input_media_fmt_ptr,
                                           mfc_media_fmt_t *output_media_fmt_ptr)
{
   ar_result_t result = AR_EOK;

   uint16_t word_size_in  = input_media_fmt_ptr->word_size;
   uint16_t word_size_out = output_media_fmt_ptr->word_size;
   uint16_t bit_width_out = output_media_fmt_ptr->bit_width;
   uint32_t samp          = 0;
   int32_t *buf_ptr_32    = NULL;

   // num_samples_in represents total samples for interleaved data, and samples per ch for deint data
   uint32_t num_samples_in = input_buf_ptr->actual_data_len / (word_size_in >> 3);

#ifdef MFC_OSRC_LIB_DEBUG
   mfc_osrc_lib_t *pc_ptr = (mfc_osrc_lib_t *)me_ptr;
   CNV_MSG(pc_ptr->miid,
           DBG_HIGH_PRIO,
           "Fixed to float: Word_size_in %d num_samples_in %d word_size_out %d, interleaving "
           "%d",
           word_size_in,
           num_samples_in,
           word_size_out,
           input_media_fmt_ptr->interleaving);
#endif

   const uint32_t iq31   = (1 << PCM_Q_FACTOR_31);
   const uint64_t iq31_1 = (1ll << PCM_Q_FACTOR_31);

   if (BIT_WIDTH_32 == bit_width_out)
   {
      float32_t       num32      = 0;
      float32_t *     dst_ptr_32 = NULL;
      const float32_t fq31       = (float32_t)(iq31);

      if (MFC_INTERLEAVED == input_media_fmt_ptr->interleaving)
      {
         buf_ptr_32 = (int32_t *)(input_buf_ptr->data_ptr);    /* Input buffer  */
         dst_ptr_32 = (float32_t *)(output_buf_ptr->data_ptr); /* Output buffer */

         for (samp = 0; samp < num_samples_in; samp++)
         {
            // Convert fixed point input sample of data to floating point
            num32 = ((float32_t)*buf_ptr_32++) / fq31;
            // Writing to output buffer
            *dst_ptr_32++ = num32;
         }
         output_buf_ptr->actual_data_len = num_samples_in * (word_size_out >> 3);
      }
      else
      {
         for (uint32_t i = 0; i < input_media_fmt_ptr->num_channels; i++)
         {
            buf_ptr_32 = (int32_t *)(input_buf_ptr[i].data_ptr);    /* Input buffer  */
            dst_ptr_32 = (float32_t *)(output_buf_ptr[i].data_ptr); /* Output buffer */

            for (samp = 0; samp < num_samples_in; samp++)
            {
               // Convert fixed point input sample of data to floating point
               num32 = ((float32_t)*buf_ptr_32++) / fq31;
               // Writing to output buffer
               *dst_ptr_32++ = num32;
            }
         }

         // optimization: write/read only first ch lens, and assume same lens for rest of the chs in the PC library.
         output_buf_ptr[0].actual_data_len = num_samples_in * (word_size_out >> 3);
      }
   }
   else if (BIT_WIDTH_64 == bit_width_out)
   {
      float64_t       num64      = 0;
      float64_t *     dst_ptr_64 = NULL;
      const float64_t dq31       = (float64_t)(iq31_1);

      if (MFC_INTERLEAVED == input_media_fmt_ptr->interleaving)
      {
         buf_ptr_32 = (int32_t *)(input_buf_ptr->data_ptr);    /* Input buffer  */
         dst_ptr_64 = (float64_t *)(output_buf_ptr->data_ptr); /* Output buffer */

         for (samp = 0; samp < num_samples_in; samp++)
         {
            // Convert fixed point input sample of data to floating point
            num64 = ((float64_t)*buf_ptr_32++) / dq31;
            // Writing to output buffer
            *dst_ptr_64++ = num64;
         }
         output_buf_ptr->actual_data_len = num_samples_in * (word_size_out >> 3);
      }
      else
      {
         for (uint32_t i = 0; i < input_media_fmt_ptr->num_channels; i++)
         {
            buf_ptr_32 = (int32_t *)(input_buf_ptr[i].data_ptr);    /* Input buffer  */
            dst_ptr_64 = (float64_t *)(output_buf_ptr[i].data_ptr); /* Output buffer */

            for (samp = 0; samp < num_samples_in; samp++)
            {
               // Convert fixed point input sample of data to floating point
               num64 = ((float64_t)*buf_ptr_32++) / dq31;
               // Writing to output buffer
               *dst_ptr_64++ = num64;
            }
         }

         // optimization: write/read only first ch lens, and assume same lens for rest of the chs in the PC library.
         output_buf_ptr[0].actual_data_len = num_samples_in * (word_size_out >> 3);
      }
   }
   else
   {
      result = AR_EUNSUPPORTED;
      return result;
   }

   return result;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
Function to check if floating point data format is supported by platform
______________________________________________________________________________________________________________________*/

bool_t mfc_osrc_is_floating_point_data_format_supported()
{
   return TRUE;
}
