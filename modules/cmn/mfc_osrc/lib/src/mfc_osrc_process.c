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

______________________________________________________________________________________________________________________*/
ar_result_t mfc_osrc_endianness_process(void           *me_ptr,
                                  capi_buf_t     *input_buf_ptr,
                                  capi_buf_t     *output_buf_ptr,
                                  mfc_media_fmt_t *input_media_fmt_ptr,
                                  mfc_media_fmt_t *output_media_fmt_ptr)
{
   ar_result_t result = AR_EOK;
   // No need to check for the same endianness, since it is done in the init phase
   if (MFC_INTERLEAVED != input_media_fmt_ptr->interleaving)
   {


      uint32_t num_bytes_to_process = input_buf_ptr[0].actual_data_len;
      uint32_t num_bytes_processed  = 0;
      for (uint16_t ch = 0; ch < input_media_fmt_ptr->num_channels; ch++)
      {
         // Buffer length remains the same since we use the same buffer
         mfc_osrc_change_endianness(input_buf_ptr[ch].data_ptr,
                              output_buf_ptr[ch].data_ptr,
                              num_bytes_to_process,
                              &num_bytes_processed,
                              input_media_fmt_ptr->word_size);
      }
      output_buf_ptr[0].actual_data_len = num_bytes_processed;
   }
   else
   {
      // Buffer length remains the same since we use the same buffer
      mfc_osrc_change_endianness(input_buf_ptr->data_ptr,
                           output_buf_ptr->data_ptr,
                           input_buf_ptr->actual_data_len,
                           &output_buf_ptr->actual_data_len,
                           input_media_fmt_ptr->word_size);
   }

   return result;
}


/*______________________________________________________________________________________________________________________
   DESCRIPTION:

______________________________________________________________________________________________________________________*/

