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

#include "mfc_osrc_converter.h"

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */

#if ((defined __hexagon__) || (defined __qdsp6__))
#define USE_Q6_SPECIFIC_CODE
#endif

// Use to override AR_MSG with AR_MSG_ISLAND. Always include this after ar_msg.h
// #ifdef AR_MSG_IN_ISLAND
// #include "ar_msg_island_override.h"
// #endif

/*______________________________________________________________________________________________________________________
   DESCRIPTION:

______________________________________________________________________________________________________________________*/
ar_result_t mfc_osrc_intlv_16_out(capi_buf_t *input_buf_ptr,
                            capi_buf_t *output_buf_ptr,
                            uint16_t    word_size_in,
                            uint16_t    q_factor_in)
{

   int32_t  samp;
   int32_t *buf_ptr_32;
   int16_t *buf_ptr_16;
   
   #ifdef USE_Q6_SPECIFIC_CODE
   int64_t *buf_ptr_64;
   #endif

   uint32_t total_num_samples = input_buf_ptr->actual_data_len / (word_size_in >> 3);
   const uint32_t shift = (q_factor_in - 15); // (Qn - Q15)

   if (32 == word_size_in)
   {
#ifdef USE_Q6_SPECIFIC_CODE
      buf_ptr_64 = (int64_t *)(input_buf_ptr->data_ptr);  /* Input buffer  */
      buf_ptr_32 = (int32_t *)(output_buf_ptr->data_ptr); /* Output buffer */

      /* If input buf addr is 8 byte aligned and out buf addr is 4 byte aligned
         then only perform the vector operation
      */
      if ((0 == ((uint32_t)buf_ptr_64 & 0x7)) && (0 == ((uint32_t)buf_ptr_32 & 0x3)))
      {
         for (samp = total_num_samples; samp >= 2; samp -= 2)
         {
            int64_t temp64 = *buf_ptr_64++;
            /* Convert from Q31 to Q15*/

            /* Right shift each of the 32 bit words into 16 bits and
               store the two lower halfwords into destination
            */
            *buf_ptr_32++ = Q6_R_vasrw_PR(temp64, shift);
         }

         /* If the number of samples are odd, following loop will handle. */
         total_num_samples = samp;
      }
      /* if either of buf addr is not byte aligned as required for vectorization
       * or for the remaining samples in case it's odd*/
      else
      {
         int32_t temp32;

         int16_t *buf_ptr_src_16  = (int16_t *)buf_ptr_32;
         int32_t *buf_ptr_src2_32 = (int32_t *)buf_ptr_64;

         for (samp = 0; samp < total_num_samples; samp++)
         {
            temp32 = *buf_ptr_src2_32++;
            /* Q28 to Q15 */
            /* First right shift followed by rounding operation */
            *buf_ptr_src_16++ = Q6_R_asr_RR(temp32, shift);
         }
      }

#else
      /*----------- Non Q6 Version ---------------*/
      buf_ptr_16 = (int16_t *)output_buf_ptr->data_ptr;
      buf_ptr_32 = (int32_t *)input_buf_ptr->data_ptr;

      /* Qn to Q15 */
      // shift is 31-15 = 16 if q_factor_in is 31.
      for (samp = 0; samp < total_num_samples; samp++)
      {
         int32_t temp32 = *buf_ptr_32++;
         *buf_ptr_16++  = (int16_t)(temp32 >> shift);
      }
#endif /* USE_Q6_SPECIFIC_CODE */
   }
   else if (24 == word_size_in)
   {
      buf_ptr_16       = (int16_t *)output_buf_ptr->data_ptr;
      uint8_t *src_ptr = (uint8_t *)input_buf_ptr->data_ptr;
      /* Qn to Q15 */
      for (samp = 0; samp < total_num_samples; samp++)
      {
         int32_t  num32;
         uint32_t tem32;

         num32         = 0;
         tem32         = *src_ptr++;
         num32         = num32 | (tem32);
         tem32         = *src_ptr++;
         num32         = num32 | (tem32 << 8);
         tem32         = *src_ptr++;
         num32         = num32 | (tem32 << 16);
         *buf_ptr_16++ = (int16_t)(num32 >> shift);
      }
   }
   else
   {
      return AR_EUNSUPPORTED;
   }
   output_buf_ptr->actual_data_len = input_buf_ptr->actual_data_len * 2 / (word_size_in >> 3);
   return AR_EOK;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:

______________________________________________________________________________________________________________________*/
ar_result_t mfc_osrc_intlv_24_out(capi_buf_t *input_buf_ptr,
                            capi_buf_t *output_buf_ptr,
                            uint16_t    word_size_in,
                            uint16_t    q_factor_in)
{
   int32_t  samp;
   int32_t *buf_ptr_32;
   int8_t * buf_ptr_8;
   uint32_t total_num_samples = input_buf_ptr->actual_data_len / (word_size_in >> 3);
   uint32_t shift;

   if (16 == word_size_in)
   {
      /*----------- Non Q6 Version ---------------*/

      buf_ptr_8    = (int8_t *)output_buf_ptr->data_ptr;
      uint8_t *src = (uint8_t *)input_buf_ptr->data_ptr;

      /* Q16 -> Q23  conversion */
      for (samp = 0; samp < total_num_samples; samp++)
      {
         *buf_ptr_8++ = 0x00;
         *buf_ptr_8++ = *src++;
         *buf_ptr_8++ = *src++;
      }
   }
   else if (32 == word_size_in)
   {

      shift = (q_factor_in - 23); // (Qn - Q23)

      uint8_t *dst  = (uint8_t *)output_buf_ptr->data_ptr;
      buf_ptr_32    = (int32_t *)input_buf_ptr->data_ptr;
      int32_t num32 = 0;
      uint8_t temp8 = 0;

      /* Q27/31 -> Q23  conversion */
      for (samp = 0; samp < total_num_samples; samp++)
      {
         num32  = *buf_ptr_32++ >> shift;
         temp8  = (uint8_t)num32;
         *dst++ = temp8;

         num32  = num32 >> 8;
         temp8  = (uint8_t)num32;
         *dst++ = temp8;

         num32  = num32 >> 8;
         temp8  = (uint8_t)num32;
         *dst++ = temp8;
      }
   }
   else
   {
      return AR_EUNSUPPORTED;
   }

   output_buf_ptr->actual_data_len = input_buf_ptr->actual_data_len * 3 / (word_size_in >> 3);
   return AR_EOK;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:

______________________________________________________________________________________________________________________*/
ar_result_t mfc_osrc_intlv_32_out(capi_buf_t *input_buf_ptr,
                            capi_buf_t *output_buf_ptr,
                            uint16_t    word_size_in,
                            uint16_t    q_factor_in,
                            uint16_t    q_factor_out)
{

   int32_t  samp;
   int32_t *buf_ptr_32;
   int16_t *buf_ptr_16;
   uint32_t total_num_samples = input_buf_ptr->actual_data_len / (word_size_in >> 3);
#ifdef USE_Q6_SPECIFIC_CODE
   int64_t *buf_ptr_64;
#endif
   uint32_t shift;
   uint32_t min_value = 0;
   uint32_t max_value = 0;
   if (PCM_Q_FACTOR_23 == q_factor_in)
   {
       min_value = MIN_Q23;
       max_value = MAX_Q23;
   }
   else if (PCM_Q_FACTOR_27 == q_factor_in)
   {
        min_value = MIN_Q27;
        max_value = MAX_Q27;
   }
   else // when q_factor_in is other than 23 and 27
   {
       min_value = (-(1 << q_factor_in));
        max_value = ((1 << q_factor_in) - 1);
   }
   if (16 == word_size_in)
   {
      // no in-place
      if (input_buf_ptr == output_buf_ptr)
      {
         return AR_EBADPARAM;
      }

      shift = (q_factor_out - q_factor_in);

#ifdef USE_Q6_SPECIFIC_CODE

      buf_ptr_32 = (int32_t *)(input_buf_ptr->data_ptr);  /* Input buffer  */
      buf_ptr_64 = (int64_t *)(output_buf_ptr->data_ptr); /* Output buffer */

      /* Convert from Q15 to Q31/27, conversion output in scratch buffer
       */

      /* If Output buf addr is 8 byte aligned and input buf addr is 4 byte aligned
         then only perform the vector operation
      */
      if ((0 == ((uint32_t)buf_ptr_64 & 0x7)) && (0 == ((uint32_t)buf_ptr_32 & 0x3)))
      {
         for (samp = total_num_samples; samp >= 2; samp -= 2)
         {
            /* Sign extend two 16-bit words in to two 32-bit words */
            *buf_ptr_64 = Q6_P_vsxthw_R(*buf_ptr_32++);

            /* Shift left the result to convert it to Q31/27
             */
            *buf_ptr_64 = Q6_P_vaslw_PR(*buf_ptr_64, shift);
            buf_ptr_64++;
         }

         /* If the number of samples per channel is odd */
         total_num_samples = samp;
      }
      /* if either of buf addr is not byte aligned as required for vectorization
       * or for odd samples */
      else
      {
         buf_ptr_16 = (int16_t *)buf_ptr_32;
         buf_ptr_32 = (int32_t *)buf_ptr_64;

         /* Q15 -> Q31/27 conversion */
         for (samp = 0; samp < total_num_samples; samp++)
         {
            (*buf_ptr_32++) = (int32_t)((*buf_ptr_16++) << shift);
         }
      }
#else
      /*----------- Non Q6 Version ---------------*/

      buf_ptr_16 = (int16_t *)input_buf_ptr->data_ptr;
      buf_ptr_32 = (int32_t *)output_buf_ptr->data_ptr;

      /* Q15 -> Q31/27 conversion */
      for (samp = 0; samp < total_num_samples; samp++)
      {
         int32_t temp    = *buf_ptr_16++;
         (*buf_ptr_32++) = (int32_t)((temp) << shift);
      }

#endif /* USE_Q6_SPECIFIC_CODE */
   }
   else if (24 == word_size_in)
   {
      /*----------- Non Q6 Version ---------------*/
      shift        = (31 - q_factor_out);
      buf_ptr_32   = (int32_t *)output_buf_ptr->data_ptr;
      uint8_t *src = (uint8_t *)input_buf_ptr->data_ptr;
      // POSAL_ASSERT(shift <= 8);

      /* Q23 -> Q27  conversion */
      for (samp = 0; samp < total_num_samples; samp++)
      {
         int32_t  num32;
         uint32_t tem32;
         num32           = 0;
         tem32           = *src++;
         num32           = num32 | (tem32 << 8);
         tem32           = *src++;
         num32           = num32 | (tem32 << 16);
         tem32           = *src++;
         num32           = num32 | (tem32 << 24);
         (*buf_ptr_32++) = (int32_t)((num32) >> (shift));
      }
   }
   else if (32 == word_size_in)
   {
#ifdef USE_Q6_SPECIFIC_CODE
      int64_t *in_buf_ptr_64 = (int64_t *)(input_buf_ptr->data_ptr);  /* Input buffer  */
      buf_ptr_64             = (int64_t *)(output_buf_ptr->data_ptr); /* Output buffer */

      /* Convert from Q15 to Q27, conversion output in scratch buffer
       */

      /* If Output buf addr is 8 byte aligned and input buf addr is 8 byte aligned
         then only perform the vector operation
      */
      if ((q_factor_out < q_factor_in) && (0 == ((uint32_t)buf_ptr_64 & 0x7)) && (0 == ((uint32_t)in_buf_ptr_64 & 0x7)))
      {
         shift = q_factor_in - q_factor_out;
         for (samp = total_num_samples; samp >= 2; samp -= 2)
         {
            /* Shift left the result to convert it to Q27
             */
            *buf_ptr_64 = Q6_P_vasrw_PR(*in_buf_ptr_64++, shift);
            buf_ptr_64++;
         }

         /* If the number of samples per channel is odd */
         total_num_samples = samp;
      }
      /* if either of buf addr is not byte aligned as required for vectorization
       * or for odd samples */
      else
      {
         int32_t *in_buf_ptr_32 = (int32_t *)input_buf_ptr->data_ptr;
         buf_ptr_32             = (int32_t *)output_buf_ptr->data_ptr;
         if (q_factor_out >= q_factor_in)
         {
            shift = q_factor_out - q_factor_in;
         }
         else
         {
            shift = q_factor_in - q_factor_out;
         }


         if (q_factor_out >= q_factor_in)
         {
            for (samp = 0; samp < total_num_samples; samp++)
            {
               int32_t temp32 = (int32_t)(*in_buf_ptr_32++);
               temp32 = (temp32 < (int32_t)min_value) ? (int32_t)min_value : temp32;
               temp32 = (temp32 > (int32_t)max_value) ? (int32_t)max_value : temp32;
               (*buf_ptr_32++) = temp32 << shift;
            }
         }
         else
         {
            for (samp = 0; samp < total_num_samples; samp++)
            {
               (*buf_ptr_32++) = (int32_t)((*in_buf_ptr_32++) >> shift);
            }
         }
      }
#else // USE_Q6_SPECIFIC_CODE
      /*----------- Non Q6 Version ---------------*/
      int32_t *in_buf_ptr_32 = (int32_t *)input_buf_ptr->data_ptr;
      buf_ptr_32             = (int32_t *)output_buf_ptr->data_ptr;
      if (q_factor_out >= q_factor_in)
      {
         shift = q_factor_out - q_factor_in;
      }
      else
      {
         shift = q_factor_in - q_factor_out;
      }

      if (q_factor_out >= q_factor_in)
      {
         for (samp = 0; samp < total_num_samples; samp++)
         {
             int32_t temp32 = (int32_t)(*in_buf_ptr_32++);
             temp32 = (temp32 < (int32_t)min_value) ? (int32_t)min_value : temp32;
             temp32 = (temp32 > (int32_t)max_value) ? (int32_t)max_value : temp32;
             (*buf_ptr_32++) = temp32 << shift;
         }
      }
      else
      {
         for (samp = 0; samp < total_num_samples; samp++)
         {
            (*buf_ptr_32++) = (int32_t)((*in_buf_ptr_32++) >> shift);
         }
      }
#endif
   }
   else
   {
      return AR_EUNSUPPORTED;
   }

   output_buf_ptr->actual_data_len = input_buf_ptr->actual_data_len * 4 / (word_size_in >> 3);
   return AR_EOK;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:

______________________________________________________________________________________________________________________*/
static ar_result_t pc_deintlv_16_out_util_(capi_buf_t *input_buf_ptr,
                                           capi_buf_t *output_buf_ptr,
                                           uint16_t    num_channels,
                                           uint16_t    word_size_in,
                                           uint16_t    q_factor_in,
                                           bool_t      updates_only_first_ch_len)
{
   ar_result_t result = AR_EOK;
   int32_t  samp, i;
   int32_t *buf_ptr_32;
   int16_t *buf_ptr_16;
   uint32_t shift_factor = 0;
#ifdef USE_Q6_SPECIFIC_CODE
   int64_t *buf_ptr_64;
#endif
   uint32_t num_samp_per_ch = input_buf_ptr->actual_data_len / (word_size_in >> 3);

   if (32 == word_size_in)
   {
      int64_t dw_MAX    = 0;
      int64_t dw_MIN    = 0;
      int32_t max_value = 0;
      int32_t min_value = 0;

      if (PCM_Q_FACTOR_23 == q_factor_in)
      {
         dw_MAX       = ((int64_t)MAX_Q23 << 32) | (int64_t)MAX_Q23;
         dw_MIN       = ((int64_t)MIN_Q23 << 32) | (int64_t)MIN_Q23;
         shift_factor = PCM_Q_FACTOR_23 - PCM_Q_FACTOR_15; // q23 - q15
         max_value    = (int32_t)MAX_Q23;
         min_value    = (int32_t)MIN_Q23;
      }
      else if (PCM_Q_FACTOR_27 == q_factor_in)
      {
         dw_MAX       = ((int64_t)MAX_Q27 << 32) | (int64_t)MAX_Q27;
         dw_MIN       = ((int64_t)MIN_Q27 << 32) | (int64_t)MIN_Q27;
         shift_factor = BYTE_UPDOWN_CONV_SHIFT_FACT; // q27 - q15
         max_value    = (int32_t)MAX_Q27;
         min_value    = (int32_t)MIN_Q27;
      }
      // q31 to q15
      else if (PCM_Q_FACTOR_31 == q_factor_in)
      {
         dw_MAX       = ((int64_t)MAX_Q31 << 32) | (int64_t)MAX_Q31;
         dw_MIN       = ((int64_t)MIN_Q31 << 32) | (int64_t)MIN_Q31;
         shift_factor = PCM_Q_FACTOR_31 - PCM_Q_FACTOR_15; // q31 - q15
         max_value    = (int32_t)MAX_Q31;
         min_value    = (int32_t)MIN_Q31;
      }

#ifdef USE_Q6_SPECIFIC_CODE

      for (i = 0; i < num_channels; i++)
      {
         buf_ptr_64 = (int64_t *)(input_buf_ptr[i].data_ptr);  /* Input buffer  */
         buf_ptr_32 = (int32_t *)(output_buf_ptr[i].data_ptr); /* Output buffer */

         /* Convert from (Q27 or Q31) to Q15 */

         /* If input buf addr is 8 byte aligned and out buf addr is 4 byte aligned
            then only perform the vector operation */
         if ((0 == ((uint32_t)buf_ptr_64 & 0x7)) && (0 == ((uint32_t)buf_ptr_32 & 0x3)))
         {
            for (samp = num_samp_per_ch; samp >= 2; samp -= 2)
            {
               int64_t temp64;

               /* Saturate to 27 or 31 bits based on input q format */
               temp64 = Q6_P_vminw_PP(*buf_ptr_64++, dw_MAX);
               temp64 = Q6_P_vmaxw_PP(temp64, dw_MIN);

               /* Right shift each of the 32 bit words into 16 bits and
               store the two lower halfwords into destination */
               *buf_ptr_32++ = Q6_R_vasrw_PR(temp64, shift_factor);
            }

            /* If the number of samples are odd */
            if (samp)
            {
               int32_t temp32;

               int16_t *buf_ptr_src_16  = (int16_t *)buf_ptr_32;
               int32_t *buf_ptr_src2_32 = (int32_t *)buf_ptr_64;

               /* Saturate to 27 or 31 bits based on the input q format */
               temp32 = Q6_R_max_RR(*buf_ptr_src2_32, (int32_t)min_value);
               temp32 = Q6_R_min_RR(temp32, (int32_t)max_value);

               /* Q27->Q15 or Q31->Q15 with rounding */
               *buf_ptr_src_16 = Q6_R_asr_RR(temp32, shift_factor);
            }
         }
         else /* if either of buf addr is not byte aligned as required for vectorization */
         {
            int32_t temp32;

            int16_t *buf_ptr_src_16  = (int16_t *)buf_ptr_32;
            int32_t *buf_ptr_src2_32 = (int32_t *)buf_ptr_64;

            if (PCM_Q_FACTOR_27 == q_factor_in)
            {
               for (samp = 0; samp < num_samp_per_ch; samp++)
               {
                  /* Saturate to 27 bits */
                  temp32 = Q6_R_max_RR(*buf_ptr_src2_32++, (int32_t)min_value);
                  temp32 = Q6_R_min_RR(temp32, (int32_t)max_value);

                  /* Q27 to Q15 */
                  *buf_ptr_src_16++ = Q6_R_asr_RR(temp32, shift_factor);
               }
            }
            else if (PCM_Q_FACTOR_31 == q_factor_in)
            {
               for (samp = 0; samp < num_samp_per_ch; samp++)
               {
                  /* Saturate to 31 bits */
                  temp32 = Q6_R_max_RR(*buf_ptr_src2_32++, (int32_t)min_value);
                  temp32 = Q6_R_min_RR(temp32, (int32_t)max_value);

                  /* Q31 to Q15 */
                  *buf_ptr_src_16++ = Q6_R_asr_RR(temp32, shift_factor);
               }
            }
         }
      }
#else
      /*----------- Non Q6 Version ---------------*/
      for (i = 0; i < num_channels; i++)
      {
         buf_ptr_32 = (int32_t *)(input_buf_ptr[i].data_ptr);  /* Input buffer  */
         buf_ptr_16 = (int16_t *)(output_buf_ptr[i].data_ptr); /* Output buffer */

         /* Convert from Q27 (or Q31) to Q15 , inplace conversion */
         for (samp = 0; samp < num_samp_per_ch; samp++)
         {
            int32_t temp32 = *buf_ptr_32++;

            /* Saturate to 27 or 31 bits */
            temp32 = (temp32 < (int32_t)min_value) ? (int32_t)min_value : temp32;
            temp32 = (temp32 > (int32_t)max_value) ? (int32_t)max_value : temp32;

            /*shift to Q15 */
            *buf_ptr_16++ = (int16_t)(temp32 >> shift_factor);
         }
      }
#endif /* __q6_specific_code__ */
   }
   else if (24 == word_size_in)
   {
      shift_factor = 23 - 15;

      for (i = 0; i < num_channels; i++)
      {
         uint8_t *src_ptr = (uint8_t *)(input_buf_ptr[i].data_ptr);  /* Input buffer  */
         buf_ptr_16       = (int16_t *)(output_buf_ptr[i].data_ptr); /* Output buffer */

         /* Convert from Q27 (or Q31) to Q15 , inplace conversion */
         for (samp = 0; samp < num_samp_per_ch; samp++)
         {
            int32_t  num32;
            uint32_t tem32;

            num32 = 0;
            tem32 = *src_ptr++;
            num32 = num32 | (tem32);
            tem32 = *src_ptr++;
            num32 = num32 | (tem32 << 8);
            tem32 = *src_ptr++;
            num32 = num32 | (tem32 << 16);
            /*Right shift to Q15*/
            *buf_ptr_16++ = (int16_t)(num32 >> shift_factor);
         }
      }
   }
   else
   {
      // optimization: write/read only first ch lens, and assume same lens for rest of the chs
      output_buf_ptr[0].actual_data_len = 0;
      result = AR_EUNSUPPORTED;
      goto bailout_;
   }

   // optimization: write/read only first ch lens, and assume same lens for rest of the chs
   output_buf_ptr[0].actual_data_len = num_samp_per_ch * 2;

bailout_:
   // update rest of the channels as well
   if (FALSE == updates_only_first_ch_len)
   {
      for (uint32_t i = 1; i < num_channels; i++)
      {
         output_buf_ptr[i].actual_data_len = output_buf_ptr[0].actual_data_len;
      }
   }

   return result;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:

______________________________________________________________________________________________________________________*/
static ar_result_t pc_deintlv_24_out_util_(capi_buf_t *input_buf_ptr,
                                           capi_buf_t *output_buf_ptr,
                                           uint16_t    num_channels,
                                           uint16_t    word_size_in,
                                           uint16_t    q_factor_in,
                                           bool_t      updates_only_first_ch_len)
{
   ar_result_t result = AR_EOK;
   int32_t  samp, i;
   int32_t *buf_ptr_32;
   int8_t * buf_ptr_8;
   uint32_t shift;
   uint32_t num_samp_per_ch = input_buf_ptr->actual_data_len / (word_size_in >> 3);

   if (16 == word_size_in)
   {
      /*----------- Non Q6 Version ---------------*/

      /* Q16 -> Q23  conversion */
      uint8_t *src;
      for (i = 0; i < num_channels; i++)
      {
         buf_ptr_8 = (output_buf_ptr[i].data_ptr);           /* Input buffer */
         src       = (uint8_t *)(input_buf_ptr[i].data_ptr); /* Output buffer  */

         for (samp = 0; samp < num_samp_per_ch; samp++)
         {
            *buf_ptr_8++ = 0x00;
            *buf_ptr_8++ = *src++;
            *buf_ptr_8++ = *src++;
         }
      }
   }
   else if (32 == word_size_in)
   {

      shift = (q_factor_in - 23); // (Qn - Q23)

      int32_t  num32 = 0;
      uint8_t  temp8 = 0;
      uint8_t *dst;
      /* Q27/31 -> Q23  conversion */
      for (i = 0; i < num_channels; i++)
      {
         buf_ptr_32 = (int32_t *)(input_buf_ptr[i].data_ptr);  /* Input buffer */
         dst        = (uint8_t *)(output_buf_ptr[i].data_ptr); /* Output buffer  */

         for (samp = 0; samp < num_samp_per_ch; samp++)
         {
            num32  = *buf_ptr_32++ >> shift;
            temp8  = (uint8_t)num32;
            *dst++ = temp8;

            num32  = num32 >> 8;
            temp8  = (uint8_t)num32;
            *dst++ = temp8;

            num32  = num32 >> 8;
            temp8  = (uint8_t)num32;
            *dst++ = temp8;
         }
      }
   }
   else
   {
      // optimization: write/read only first ch lens, and assume same lens for rest of the chs
      output_buf_ptr[0].actual_data_len = 0;
      result                            = AR_EUNSUPPORTED;
      goto bailout_;
   }
   // optimization: write/read only first ch lens, and assume same lens for rest of the chs
   output_buf_ptr[0].actual_data_len = num_samp_per_ch * 3;

bailout_:
   // update rest of the channels as well
   if (FALSE == updates_only_first_ch_len)
   {
      for (uint32_t i = 1; i < num_channels; i++)
      {
         output_buf_ptr[i].actual_data_len = output_buf_ptr[0].actual_data_len;
      }
   }

   return result;
}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:

______________________________________________________________________________________________________________________*/
static ar_result_t pc_deintlv_32_out_util_(capi_buf_t *input_buf_ptr,
                                           capi_buf_t *output_buf_ptr,
                                           uint16_t    num_channels,
                                           uint16_t    word_size_in,
                                           uint16_t    q_factor_in,
                                           uint16_t    q_factor_out,
                                           bool_t      updates_only_first_ch_len)
{
   ar_result_t result = AR_EOK;
   int32_t  samp, i;
   int32_t *buf_ptr_32;
   uint8_t *buf_ptr_8;
   uint32_t shift_factor    = 0;
   uint32_t num_samp_per_ch = input_buf_ptr->actual_data_len / (word_size_in >> 3);
   uint32_t min_value = 0;
   uint32_t max_value = 0;
   if (PCM_Q_FACTOR_23 == q_factor_in)
   {
       min_value = MIN_Q23;
       max_value = MAX_Q23;
   }
   else if (PCM_Q_FACTOR_27 == q_factor_in)
   {
        min_value = MIN_Q27;
        max_value = MAX_Q27;
   }
   else // when q_factor_in is other than 23 and 27
   {
        min_value = (-(1 << q_factor_in));
        max_value = ((1 << q_factor_in) - 1);
   }

   if (16 == word_size_in)
   {
      shift_factor = q_factor_out - q_factor_in;
#ifdef USE_Q6_SPECIFIC_CODE
      for (i = 0; i < num_channels; i++)
      {
         int32_t *buf_ptr_32 = (int32_t *)(input_buf_ptr[i].data_ptr);  /* Input buffer  */
         int64_t *buf_ptr_64 = (int64_t *)(output_buf_ptr[i].data_ptr); /* Output buffer */


         /* Convert from Q15 to Q28, conversion output in scratch buffer */

         /* If Output buf addr is 8 byte aligned and input buf addr is 4 byte aligned
           then only perform the vector operation */
         if ((0 == ((uint32_t)buf_ptr_64 & 0x7)) && (0 == ((uint32_t)buf_ptr_32 & 0x3)))
         {
                  for (samp = num_samp_per_ch; samp >= 2; samp -= 2)
            {

               /* Sign extend two 16-bit words in to two 32-bit words */
               *buf_ptr_64 = Q6_P_vsxthw_R(*buf_ptr_32++);


               /* Shift left the result to convert it to Q27 or Q31 */
               *buf_ptr_64 = Q6_P_vaslw_PR(*buf_ptr_64, shift_factor);
               buf_ptr_64++;
                     }

            /* If the number of samples per channel is odd */
            if (samp)
            {
                        int16_t *buf_ptr_src_16  = (int16_t *)buf_ptr_32;
               int32_t *buf_ptr_src2_32 = (int32_t *)buf_ptr_64;
               *buf_ptr_src2_32         = (int32_t)(*buf_ptr_src_16 << shift_factor);
            }
         }
         else /* if either of buf addr is not byte aligned as required for vectorization */
         {
                  int16_t *buf_ptr_src_16  = (int16_t *)buf_ptr_32;
            int32_t *buf_ptr_src2_32 = (int32_t *)buf_ptr_64;

            /* Q15 -> Q27(Q31) conversion */
            for (samp = 0; samp < num_samp_per_ch; samp++)
            {
                        (*buf_ptr_src2_32++) = (int32_t)((*buf_ptr_src_16++) << shift_factor);
            }
         }
         }
#else
      /*----------- Non Q6 Version ---------------*/
      for (i = 0; i < num_channels; i++)
      {
         int16_t *buf_ptr_16 = (int16_t *)(input_buf_ptr[i].data_ptr);  /* Input buffer  */
         buf_ptr_32          = (int32_t *)(output_buf_ptr[i].data_ptr); /* Output buffer */

         /* Convert from Q15 to Q27(or Q31), conversion output in scratch buffer */
         for (samp = 0; samp < num_samp_per_ch; samp++)
         {
            *buf_ptr_32++ = ((int32_t)(*buf_ptr_16++)) << shift_factor;
         }
      }
#endif /* __qdsp6__ */
   }
   else if (24 == word_size_in)
   {
      shift_factor = (31 - q_factor_out);

#ifdef USE_Q6_SPECIFIC_CODE
      for (i = 0; i < num_channels; i++)
      {
         buf_ptr_8  = (uint8_t *)(input_buf_ptr[i].data_ptr);  /* Input buffer  */
         buf_ptr_32 = (int32_t *)(output_buf_ptr[i].data_ptr); /* Output buffer */

         int32_t num32 = 0;
         int32_t tem32 = 0, tem32_1 = 0, tem32_2 = 0;

         /* Q23 -> Q27  conversion */
         for (samp = 0; samp < num_samp_per_ch; samp++)
         {
            num32   = 0;
            tem32   = buf_ptr_8[0]; // load the 1st byte of a sample
            tem32_1 = buf_ptr_8[1]; // load the 2nd byte of a sample
            tem32_2 = buf_ptr_8[2]; // load the 3rd byte of a sample

            tem32 = Q6_R_aslor_RI(tem32,
                                  tem32_1,
                                  8); // left shift 2nd byte by 8 and or with first byte -- later left shift total by 8
            num32 = Q6_R_aslor_RI(num32, tem32_2, 24); // left shift 3rd byte by 24
            buf_ptr_8 += 3;                            // increment the input buf ptr

            num32 =
               Q6_R_aslor_RI(num32, tem32, 8); // effective shifts will be (1st - 8, 2nd - 16, 3rd - 24) for each byte
            (*buf_ptr_32++) = (int32_t)((num32) >> (shift_factor));
         }
      }
#else
      for (i = 0; i < num_channels; i++)
      {
         buf_ptr_8  = (uint8_t *)(input_buf_ptr[i].data_ptr);  /* Input buffer  */
         buf_ptr_32 = (int32_t *)(output_buf_ptr[i].data_ptr); /* Output buffer */

         /* Q23 -> Q27  conversion */
         for (samp = 0; samp < num_samp_per_ch; samp++)
         {
            int32_t  num32;
            uint32_t tem32;
            num32 = 0;

            tem32 = *buf_ptr_8++;
            num32 = num32 | (tem32 << 8);
            tem32 = *buf_ptr_8++;
            num32 = num32 | (tem32 << 16);
            tem32 = *buf_ptr_8++;
            num32 = num32 | (tem32 << 24);

            (*buf_ptr_32++) = (int32_t)((num32) >> (shift_factor));
         }
      }
#endif
   }
   else
   {
      shift_factor = (q_factor_out - q_factor_in); // (Q31/27 - Qn)

      if (q_factor_out >= q_factor_in)
      {
         shift_factor = q_factor_out - q_factor_in;
      }
      else
      {
         shift_factor = q_factor_in - q_factor_out;
      }
      for (i = 0; i < num_channels; i++)
      {
         int32_t *in_buf_ptr_32 = (int32_t *)(input_buf_ptr[i].data_ptr);
         buf_ptr_32             = (int32_t *)(output_buf_ptr[i].data_ptr);

         if (q_factor_out >= q_factor_in)
         {
            for (samp = 0; samp < num_samp_per_ch; samp++)
            {
               int32_t temp32 = (int32_t)(*in_buf_ptr_32++);
               temp32 = (temp32 < (int32_t)min_value) ? (int32_t)min_value : temp32;
               temp32 = (temp32 > (int32_t)max_value) ? (int32_t)max_value : temp32;
               (*buf_ptr_32++) = temp32 << shift_factor;
            }
         }
         else
         {
            for (samp = 0; samp < num_samp_per_ch; samp++)
            {
               (*buf_ptr_32++) = (int32_t)((*in_buf_ptr_32++) >> shift_factor);
            }
         }
      }
   }


   // optimization: write/read only first ch lens, and assume same lens for rest of the chs
   output_buf_ptr[0].actual_data_len = num_samp_per_ch * 4;

   // update rest of the channels as well
   if (FALSE == updates_only_first_ch_len)
   {
      for (uint32_t i = 1; i < num_channels; i++)
      {
         output_buf_ptr[i].actual_data_len = output_buf_ptr[0].actual_data_len;
      }
   }


   return result;
}

ar_result_t mfc_osrc_deintlv_16_out(capi_buf_t *input_buf_ptr,
                              capi_buf_t *output_buf_ptr,
                              uint16_t    num_channels,
                              uint16_t    word_size_in,
                              uint16_t    q_factor_in)
{
   return pc_deintlv_16_out_util_(input_buf_ptr, output_buf_ptr, num_channels, word_size_in, q_factor_in, FALSE);
}

ar_result_t mfc_osrc_deintlv_24_out(capi_buf_t *input_buf_ptr,
                              capi_buf_t *output_buf_ptr,
                              uint16_t    num_channels,
                              uint16_t    word_size_in,
                              uint16_t    q_factor_in)
{
   return pc_deintlv_24_out_util_(input_buf_ptr, output_buf_ptr, num_channels, word_size_in, q_factor_in, FALSE);
}

ar_result_t mfc_osrc_deintlv_32_out(capi_buf_t *input_buf_ptr,
                              capi_buf_t *output_buf_ptr,
                              uint16_t    num_channels,
                              uint16_t    word_size_in,
                              uint16_t    q_factor_in,
                              uint16_t    q_factor_out)
{
   return pc_deintlv_32_out_util_(input_buf_ptr,
                                  output_buf_ptr,
                                  num_channels,
                                  word_size_in,
                                  q_factor_in,
                                  q_factor_out,
                                  FALSE);
}

ar_result_t mfc_osrc_deintlv_unpacked_v2_16_out(capi_buf_t *input_buf_ptr,
                                          capi_buf_t *output_buf_ptr,
                                          uint16_t    num_channels,
                                          uint16_t    word_size_in,
                                          uint16_t    q_factor_in)
{
   return pc_deintlv_16_out_util_(input_buf_ptr, output_buf_ptr, num_channels, word_size_in, q_factor_in, TRUE);
}

ar_result_t mfc_osrc_deintlv_unpacked_v2_24_out(capi_buf_t *input_buf_ptr,
                                          capi_buf_t *output_buf_ptr,
                                          uint16_t    num_channels,
                                          uint16_t    word_size_in,
                                          uint16_t    q_factor_in)
{
   return pc_deintlv_24_out_util_(input_buf_ptr, output_buf_ptr, num_channels, word_size_in, q_factor_in, TRUE);
}

ar_result_t mfc_osrc_deintlv_unpacked_v2_32_out(capi_buf_t *input_buf_ptr,
                                          capi_buf_t *output_buf_ptr,
                                          uint16_t    num_channels,
                                          uint16_t    word_size_in,
                                          uint16_t    q_factor_in,
                                          uint16_t    q_factor_out)
{
   return pc_deintlv_32_out_util_(input_buf_ptr,
                                  output_buf_ptr,
                                  num_channels,
                                  word_size_in,
                                  q_factor_in,
                                  q_factor_out,
                                  TRUE);
}
