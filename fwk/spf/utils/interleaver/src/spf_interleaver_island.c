/*========================================================================

 file spf_interleaver.cpp
This file contains functions for interleaving/deinterleaving

Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
======================================================================*/

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */

#include "spf_interleaver.h"

/* -----------------------------------------------------------------------
 ** Temp util function
 ** ----------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/
#if ((defined __hexagon__) || (defined __qdsp6__))
extern void interleaver_16(int16_t *inBuf_L, int16_t *inBuf_R, int16_t *outBuf, uint32_t numSamplesToInterleave);

extern void deinterleaver_16(int16_t *inBuf, int16_t *outBuf_L, int16_t *outBuf_R, uint32_t numSamplesToDeInterleave);

extern void align_interleaver_16(int16_t *inBuf_L, int16_t *inBuf_R, int16_t *outBuf, uint32_t numSamplesToInterleave);

extern void align_deinterleaver_16(int16_t *inBuf,
                                   int16_t *outBuf_L,
                                   int16_t *outBuf_R,
                                   uint32_t numSamplesToDeInterleave);
#endif
#ifdef __cplusplus
}
#endif /*__cplusplus*/

/*______________________________________________________________________________________________________________________
   DESCRIPTION:

______________________________________________________________________________________________________________________*/
ar_result_t spf_deintlv_to_intlv_v2(capi_buf_t *input_buf_ptr,
                                    capi_buf_t *output_buf_ptr,
                                    uint32_t    num_channels,
                                    uint32_t    bytes_per_samp,
                                    uint32_t    num_samp_per_ch)
{
   int i, j, k;

   if (1 == num_channels)
   {
      memscpy(output_buf_ptr[0].data_ptr,
              num_samp_per_ch * bytes_per_samp,
              input_buf_ptr->data_ptr,
              num_samp_per_ch * bytes_per_samp);
   }
   else
   {
      if (2 == bytes_per_samp)
      {
         int16_t *src_ptr = (int16_t *)input_buf_ptr[0].data_ptr;
         int16_t *dst_ptr = (int16_t *)output_buf_ptr->data_ptr;
#if ((defined __hexagon__) || (defined __qdsp6__))
         if ((2 == num_channels) && (num_samp_per_ch >= 2))
         {
            int16_t *src_left_ptr  = (int16_t *)input_buf_ptr[0].data_ptr;
            int16_t *src_right_ptr = (int16_t *)input_buf_ptr[1].data_ptr;
            if ((0 == ((uint32_t)src_left_ptr & 0x3)) && (0 == ((uint32_t)src_right_ptr & 0x3)) &&
                (0 == ((uint32_t)dst_ptr & 0x7)))
            {
               align_interleaver_16(src_left_ptr, src_right_ptr, dst_ptr, num_samp_per_ch);
            }
            else
            {
               interleaver_16(src_left_ptr, src_right_ptr, dst_ptr, num_samp_per_ch);
            }
         }
         else
#endif
         {
            for (j = 0; j < num_channels; j++)
            {
               src_ptr = (int16_t *)input_buf_ptr[j].data_ptr;
               k       = j;
               for (i = 0; i < num_samp_per_ch; i++)
               {
                  dst_ptr[k] = src_ptr[i];
                  k += num_channels;
               }
            }
         }
      }
      else if (3 == bytes_per_samp)
      {
         int8_t *src_ptr = (int8_t *)input_buf_ptr[0].data_ptr;
         int8_t *dst_ptr = (int8_t *)output_buf_ptr->data_ptr;
         int8_t  byte_1 = 0, byte_2 = 0, byte_3 = 0; // to store the three bytes of each sample
         int32_t temp = 0;

         for (j = 0; j < num_channels; j++)
         {
            src_ptr = (int8_t *)input_buf_ptr[j].data_ptr;
            temp    = 0;
            for (i = 0; i < num_samp_per_ch; i++)
            {
               k = bytes_per_samp * (i * num_channels + j);
               byte_1 = src_ptr[temp + 0]; // load first byte in the sample
               byte_2 = src_ptr[temp + 1]; // load second byte in the sample
               byte_3 = src_ptr[temp + 2]; // load third byte in the sample

               dst_ptr[k + 0] = byte_1;
               dst_ptr[k + 1] = byte_2;
               dst_ptr[k + 2] = byte_3;
               temp += bytes_per_samp;
            }
         }
      }
      else if (4 == bytes_per_samp)
      {
         int32_t *src_ptr = (int32_t *)input_buf_ptr[0].data_ptr;
         int32_t *dst_ptr = (int32_t *)output_buf_ptr->data_ptr;

         for (j = 0; j < num_channels; j++)
         {
            src_ptr = (int32_t *)input_buf_ptr[j].data_ptr;
            k       = j;
            for (i = 0; i < num_samp_per_ch; i++)
            {
               dst_ptr[k] = src_ptr[i];
               k += num_channels;
            }
         }
      }
      else if (8 == bytes_per_samp)
      {
         int64_t *src_ptr = (int64_t *)input_buf_ptr[0].data_ptr;
         int64_t *dst_ptr = (int64_t *)output_buf_ptr->data_ptr;

         for (j = 0; j < num_channels; j++)
         {
            src_ptr = (int64_t *)input_buf_ptr[j].data_ptr;
            k       = j;
            for (i = 0; i < num_samp_per_ch; i++)
            {
               dst_ptr[k] = src_ptr[i];
               k += num_channels;
            }
         }
      }
      else
      {
         output_buf_ptr->actual_data_len = 0;
#ifndef __XTENSA__ // TODO, This is just a temp workaronud to get the compilation for slate. For some reason slate build
                   // is giving error for only this AR_MSG in core msg api
         AR_MSG(DBG_ERROR_PRIO,
                "spf_deintlv_to_intlv_v2: Invalid src bytes_per_samp while deint to int conversion, %d",
                bytes_per_samp);
#endif
         return AR_EUNSUPPORTED;
      }
   }
   // We have consumed all the data from the input, so the actual_data_len in input side will remain same
   // since capi expects that variable to hold the amount of data consumed during the process
   output_buf_ptr->actual_data_len = num_samp_per_ch * num_channels * (bytes_per_samp);
   return AR_EOK;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:

______________________________________________________________________________________________________________________*/

ar_result_t spf_intlv_to_deintlv_util_(capi_buf_t *input_buf_ptr,
                                       capi_buf_t *output_buf_ptr,
                                       uint32_t    num_src_channels,
                                       uint32_t    num_dst_channels,
                                       uint32_t    bytes_per_samp,
                                       uint32_t    num_samp_per_ch,
                                       bool_t      updates_only_first_ch_length)
{
   int32_t i = 0;
   int32_t j = 0;
   int32_t k = 0;

   uint32_t num_bufs_lens_to_update = updates_only_first_ch_length ? 1 : num_dst_channels;

   if(num_dst_channels > num_src_channels)
   {
      return AR_EFAILED;
   }

   if (1 == num_src_channels)
   {
      memscpy(output_buf_ptr[0].data_ptr,
              num_samp_per_ch * bytes_per_samp,
              input_buf_ptr->data_ptr,
              num_samp_per_ch * bytes_per_samp);
   }
   else
   {
      if (2 == bytes_per_samp)
      {
         int16_t *src_ptr = (int16_t *)input_buf_ptr->data_ptr;
         int16_t *dst_ptr = (int16_t *)output_buf_ptr[0].data_ptr;
#if ((defined __hexagon__) || (defined __qdsp6__))
         if ((2 == num_src_channels) && (2 == num_dst_channels) && (num_samp_per_ch >= 2))
         {
            int16_t *dst_left_ptr  = (int16_t *)output_buf_ptr[0].data_ptr;
            int16_t *dst_right_ptr = (int16_t *)output_buf_ptr[1].data_ptr;

            // outbuf_L outbuf_R must be 32 bit aligned inbuf must be 64 bit aligned
            if ((0 == ((uint32_t)dst_left_ptr & 0x3)) && (0 == ((uint32_t)dst_right_ptr & 0x3)) &&
                (0 == ((uint32_t)src_ptr & 0x7)))
            {
               align_deinterleaver_16(src_ptr, dst_left_ptr, dst_right_ptr, num_samp_per_ch);
            }
            else
            {
               deinterleaver_16(src_ptr, dst_left_ptr, dst_right_ptr, num_samp_per_ch);
            }
         }
         else
#endif
         {
            for (j = 0; j < num_dst_channels; j++)
            {
               dst_ptr = (int16_t *)output_buf_ptr[j].data_ptr;
               k       = j;
               for (i = 0; i < num_samp_per_ch; i++)
               {
                  dst_ptr[i] = src_ptr[k];
                  k += num_src_channels;
               }
            }
         }
      }
      else if (3 == bytes_per_samp)
      {
         int8_t *src_ptr = (int8_t *)input_buf_ptr->data_ptr;
         int8_t *dst_ptr = (int8_t *)output_buf_ptr[0].data_ptr;
         int8_t  byte_1 = 0, byte_2 = 0, byte_3 = 0; // to store the three bytes of each sample
         int32_t temp = 0;

         for (j = 0; j < num_dst_channels; j++)
         {
            dst_ptr = (int8_t *)output_buf_ptr[j].data_ptr;
            k       = bytes_per_samp * (j);
            temp    = 0;
            for (i = 0; i < num_samp_per_ch; i++)
            {
               temp   = i * bytes_per_samp;
               byte_1 = src_ptr[k + 0]; // load first byte in the sample
               byte_2 = src_ptr[k + 1]; // load second byte in the sample
               byte_3 = src_ptr[k + 2]; // load third byte in the sample

               dst_ptr[temp + 0] = byte_1;
               dst_ptr[temp + 1] = byte_2;
               dst_ptr[temp + 2] = byte_3;
               k += bytes_per_samp * num_src_channels;
            }
         }
      }
      else if (4 == bytes_per_samp)
      {
         int32_t *dst_ptr = (int32_t *)output_buf_ptr[0].data_ptr;
         int32_t *src_ptr = (int32_t *)input_buf_ptr->data_ptr;

         for (j = 0; j < num_dst_channels; j++)
         {
            dst_ptr = (int32_t *)output_buf_ptr[j].data_ptr;
            k       = j;
            for (i = 0; i < num_samp_per_ch; i++)
            {
               dst_ptr[i] = src_ptr[k];
               k += num_src_channels;
            }
         }
      }
      else if (8 == bytes_per_samp)
      {
         int64_t *dst_ptr = (int64_t *)output_buf_ptr[0].data_ptr;
         int64_t *src_ptr = (int64_t *)input_buf_ptr->data_ptr;

         for (j = 0; j <  num_dst_channels; j++)
         {
            dst_ptr = (int64_t *)output_buf_ptr[j].data_ptr;
            k       = j;
            for (i = 0; i < num_samp_per_ch; i++)
            {
               dst_ptr[i] = src_ptr[k];
               k +=  num_src_channels;
            }
         }
      }
      else
      {
         for (i = 0; i < num_bufs_lens_to_update; i++)
         {
            output_buf_ptr[i].actual_data_len = 0;
         }
#ifndef __XTENSA__ // TODO, This is just a temp workaronud to get the compilation for slate. For some reason slate build
                   // is giving error for only this AR_MSG in core msg api
         AR_MSG(DBG_ERROR_PRIO,
                "PCM_MF_CNV_LIB:Invalid dst sample bytes_per_samp while deint to int conversion, %d",
                bytes_per_samp);
#endif
         return AR_EUNSUPPORTED;
      }
   }

   for (i = 0; i < num_bufs_lens_to_update; i++)
   {
      output_buf_ptr[i].actual_data_len = num_samp_per_ch * (bytes_per_samp);
   }
   return AR_EOK;
}

ar_result_t spf_intlv_to_deintlv_v3(capi_buf_t *input_buf_ptr,
                                    capi_buf_t *output_buf_ptr,
                                    uint32_t    num_src_channels,
                                    uint32_t    num_dst_channels,
                                    uint32_t    bytes_per_samp,
                                    uint32_t    num_samp_per_ch)
{
   return spf_intlv_to_deintlv_util_(input_buf_ptr,
                                     output_buf_ptr,
                                     num_src_channels,
                                     num_dst_channels,
                                     bytes_per_samp,
                                     num_samp_per_ch,
                                     FALSE /*updates only first channel lengths*/);
}

ar_result_t spf_intlv_to_deintlv_unpacked_v2(capi_buf_t *input_buf_ptr,
                                             capi_buf_t *output_buf_ptr,
                                             uint32_t    num_channels,
                                             uint32_t    bytes_per_samp,
                                             uint32_t    num_samp_per_ch)
{
   return spf_intlv_to_deintlv_util_(input_buf_ptr,
                                     output_buf_ptr,
                                     num_channels,
                                     num_channels,
                                     bytes_per_samp,
                                     num_samp_per_ch,
                                     TRUE /*updates only first channel lengths*/);
}