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

______________________________________________________________________________________________________________________*/

/** changes endianess as it moves data into destination buffer, and returns the actual data length of the destination.*/
ar_result_t mfc_osrc_change_endianness(int8_t   *in_ptr,
                                 int8_t   *out_ptr,
                                 uint32_t  src_actual_len,
                                 uint32_t *dest_actual_len_ptr,
                                 uint32_t  word_size)
{
   uint32_t total_num_samples = src_actual_len / (word_size >> 3);
   uint32_t i;

   if (16 == word_size)
   {
      int16_t *src_ptr_16 = (int16_t *)in_ptr;
      int16_t *dst_ptr_16 = (int16_t *)out_ptr;
      uint16_t val;
      for (i = 0; i < total_num_samples; i++)
      {
         val           = *src_ptr_16++;
         *dst_ptr_16++ = (int16_t)(((val & 0xFF00) >> 8) | ((val & 0x00FF) << 8));
      }
   }
   else if (24 == word_size)
   {
      int8_t *src_ptr = in_ptr;
      int8_t *dst_ptr = out_ptr;
      uint8_t val1, val2, val3;
      for (i = 0; i < total_num_samples; i++)
      {
         val1       = *src_ptr++;
         val2       = *src_ptr++;
         val3       = *src_ptr++;
         *dst_ptr++ = val3;
         *dst_ptr++ = val2;
         *dst_ptr++ = val1;
      }
   }
   else if (32 == word_size)
   {
      int32_t *dst_ptr = (int32_t *)out_ptr;
      int32_t *src_ptr = (int32_t *)in_ptr;
      uint32_t val;
      for (i = 0; i < total_num_samples; i++)
      {
         val = *src_ptr++;
#if ((defined __hexagon__) || (defined __qdsp6__))
         *dst_ptr++ = Q6_R_swiz_R(val);
#else
         *dst_ptr++ = (int32_t)((val & 0x000000FF) << 24 | (val & 0x0000FF00) << 8 | (val & 0x00FF0000) >> 8 |
                                (val & 0xFF000000) >> 24);
#endif
      }
   }
   *dest_actual_len_ptr = src_actual_len;
   return AR_EOK;
}
