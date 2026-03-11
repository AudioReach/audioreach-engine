/**
 *   \file capi_congestion_buf.c
 *   \brief
 *        This file contains CAPI implementation of Congestion Buffer module.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_congestion_buf_i.h"

// Use to override AR_MSG with AR_MSG_ISLAND. Always include this after ar_msg.h
#ifdef AR_MSG_IN_ISLAND
#include "ar_msg_island_override.h"
#endif

static capi_vtbl_t vtbl = { capi_congestion_buf_process,        capi_congestion_buf_end,
                                  capi_congestion_buf_set_param,      capi_congestion_buf_get_param,
                                  capi_congestion_buf_set_properties, capi_congestion_buf_get_properties };

capi_vtbl_t *capi_congestion_buf_get_vtbl()
{
   return &vtbl;
}

/*==============================================================================
   Local Function forward declaration
==============================================================================*/

/*==============================================================================
   Public Function Implementation
==============================================================================*/

/*------------------------------------------------------------------------
  Function name: capi_congestion_buf_process
  Processes an input buffer and generates an output buffer.
 * -----------------------------------------------------------------------*/
capi_err_t capi_congestion_buf_process(capi_t *            capi_ptr,
                                              capi_stream_data_t *input[],
                                              capi_stream_data_t *output[])
{
   capi_err_t result = CAPI_EOK;

   if (NULL == capi_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "congestion_buf: received bad property pointer");
      return CAPI_EFAILED;
   }

   if ((NULL == input) || (NULL == output))
   {
      return result;
   }

   capi_congestion_buf_t *me_ptr = (capi_congestion_buf_t *)capi_ptr;

   if (input)
   {
      /* Check if the ports stream buffers are valid */
      if (NULL == input[0])
      {
         AR_MSG(DBG_HIGH_PRIO, "Input buffers not available ");
      }
      else
      {
         /* Parse the metadata to check if num frames are sent */
         result = capi_congestion_buf_parse_md_num_frames(me_ptr, input[0]);
         if (result)
         {
            AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Error parsing metadata for num frames %d", result);
         }

         /* Write the data into congestion buffer */
         result = congestion_buf_stream_write(me_ptr, input[0]);
         if (result)
         {
            AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Error writing data into buffer %d", result);
         }
      }
   }

   if (output)
   {
      /* Check if the ports stream buffers are valid */
      if (NULL == output[0])
      {
         AR_MSG(DBG_HIGH_PRIO, "congestion_buf: Output buffers not available ");
      }
      else
      {
         result = congestion_buf_stream_read(me_ptr, output[0]);

         if (result == AR_ENEEDMORE)
         {
            AR_MSG(DBG_HIGH_PRIO, "congestion_buf: Underrun.");
            result = CAPI_EOK;
         }
         else if (result == AR_EFAILED)
         {
            result = CAPI_EFAILED;
         }

         /* If data is received fast / slow with different gaps in
          * timestamps due to ts disc the data will not be copied. */
         output[0]->flags.is_timestamp_valid = FALSE;
         output[0]->flags.ts_continue        = FALSE;
      }
   }

   return result;
}
