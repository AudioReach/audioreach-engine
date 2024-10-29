/**
 *   \file capi_jitter_buf_island.c
 *   \brief
 *        This file contains CAPI implementation of Jitter Buf module.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_jitter_buf_i.h"

// Use to override AR_MSG with AR_MSG_ISLAND. Always include this after ar_msg.h
#ifdef AR_MSG_IN_ISLAND
#include "ar_msg_island_override.h"
#endif

static capi_vtbl_t vtbl = { capi_jitter_buf_process,        capi_jitter_buf_end,
                            capi_jitter_buf_set_param,      capi_jitter_buf_get_param,
                            capi_jitter_buf_set_properties, capi_jitter_buf_get_properties };

/*==============================================================================
   Local Function forward declaration
==============================================================================*/

/*==============================================================================
   Public Function Implementation
==============================================================================*/

capi_vtbl_t *capi_jitter_buf_get_vtbl()
{
   return &vtbl;
}

/*------------------------------------------------------------------------
  Function name: capi_jitter_buf_process
  Processes an input buffer and generates an output buffer.
 * -----------------------------------------------------------------------*/
capi_err_t capi_jitter_buf_process(capi_t *capi_ptr, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t result = CAPI_EOK;

   if (NULL == capi_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "jitter_buf: received bad property pointer");
      return CAPI_EFAILED;
   }

   if ((NULL == input) || (NULL == output))
   {
      return result;
   }

   capi_jitter_buf_t *me_ptr = (capi_jitter_buf_t *)capi_ptr;

   if(!me_ptr->is_input_mf_received || !me_ptr->jitter_allowance_in_ms || me_ptr->is_disabled_by_failure)
   {
      AR_MSG(DBG_ERROR_PRIO, "jitter_buf: not buf size %d / mf %d, is_module_disabled_by_fatal_failure %d",me_ptr->jitter_allowance_in_ms, me_ptr->is_input_mf_received, me_ptr->is_disabled_by_failure);
      if(me_ptr->is_input_mf_received && input[0]->buf_ptr)
      {
         for (int i = 0; i < input[0]->bufs_num; i++)
         {
            // marking as input not consumed
            input[0]->buf_ptr[i].actual_data_len = 0;
         }
      }
      return CAPI_EOK;
   }

   if (0 == me_ptr->frame_duration_in_us)
   {
      if (0 == input[0]->buf_ptr->actual_data_len)
      {
         /* The first time when decoder sends data starts the process for jitter buffer.
          * Circ buf is set up and zeros are filled based on this. So for the first time
          * decoder produces data the downstream buffer will be available since we do not
          * fill it with anything till jitter buffer process starts. This also makes sure that
          * we capture the exact actual data len from the decoder. */

         AR_MSG(DBG_LOW_PRIO, "jitter_buf: frame duration not set yet.");
         return CAPI_EOK;
      }

      /* TODO: Pending - check calculation */
      me_ptr->frame_duration_in_bytes = input[0]->buf_ptr->actual_data_len;

       if (CAPI_EOK != (result = ar_result_to_capi_err(jitter_buf_calibrate_driver(me_ptr))))
       {
          AR_MSG(DBG_ERROR_PRIO, "jitter_buf: Failed calibrating the driver with error = %lx", result);

          for (int i = 0; i < input[0]->bufs_num; i++)
          {
             // marking as input not consumed
             input[0]->buf_ptr[i].actual_data_len = 0;
          }

          return result;
       }
   }

   bool_t is_output_written = FALSE;

   /* mode0: input buffer at output trigger
    * Module is driven based on the reader availability. So when output is available we
    * first read it and then only write input if available. This module does not tolerate dropping
    * input data at any cost.
    *
    * mode1: input buffer at input trigger
    * Module is driven based on either reader or writer availability. */

   // if input and output are independently triggered then always prefills zeros when buffer is empty.
   // This is to ensure that zeros are pushed in the beginning and not in between of valid input data.
   if (JBM_BUFFER_INPUT_AT_INPUT_TRIGGER == me_ptr->input_buffer_mode)
   {
      result |= jitter_buf_check_fill_zeros(me_ptr);
      if (result == AR_EFAILED)
      {
         return CAPI_EFAILED;
      }
   }

   if (output)
   {
      // output buffers sanity check.
      if (NULL == output[0])
      {
         AR_MSG(DBG_HIGH_PRIO, "Output buffers not available ");
      }
      else
      {

         if (!me_ptr->first_frame_written)
         {
            /* If first call we add delay amount of zeros here and then read it. If we reach here then
             * we have the first frame available since we would have to calibrate before reaching here  */
            result |= jitter_buf_check_fill_zeros(me_ptr);

            /* We read the filled zeros here */
            result |= jitter_buf_stream_read(me_ptr, output[0]);
            if (result == AR_EFAILED)
            {
               /* If we do not read into output we do not write into it either
                * This works as a queue to prevent overruns in case buffer is
                * almost full */
               return CAPI_EFAILED;
            }
         }
         else
         {
            bool_t is_buf_empty = FALSE;
            spf_circ_buf_driver_is_buffer_empty(me_ptr->driver_hdl.reader_handle, &is_buf_empty);

            if (!is_buf_empty)
            {
               /* At this point buffer is not empty - it may have pcm data / was filled with zeros so we
                * have data to read into output buffer.*/
               result |= jitter_buf_stream_read(me_ptr, output[0]);
               if (result == AR_EFAILED)
               {
                  /* If we do not read into output we do not write into it either
                   * This works as a queue to prevent overruns in case buffer is
                   * almost full */
                  return CAPI_EFAILED;
               }
               is_output_written = TRUE;
            }
            else // this will never happen if input is buffered at input triggered because we already prefilled zeros
                 // before reading.
            {
               /* At this point first frame was already written and first delay zeros already filled. If
                * we find empty buffer here and also nothing at input then we dont have data and fill delay
                * ms amount of zeros into the buffer since we have drained it. So we check if there is input
                * we can send to output - if not we have drained and send zeros.*/

               AR_MSG(DBG_HIGH_PRIO, "Buf empty at read so attempting to write before read ");
            }
         }
      }
   }

   if (input)
   {
      /* Check if the ports stream buffers are valid */
      if (NULL == input[0])
      {
         AR_MSG(DBG_HIGH_PRIO, "Input buffers not available ");
      }
      else
      {
         /* Write data into the jitter buffer one frame (assuming
          * one frame was successfully read into the output) */
         result = jitter_buf_stream_write(me_ptr, input[0]);
      }
   }

   if (!is_output_written)
   {
      /* If we have reached this point then jitter buffer was empty when trying to read.
       * We check if in the write cycle it was filled to send data out. If not the jitter
       * buffer is drained and we add zeros to the buffer and read it into the output. */
      result |= jitter_buf_check_fill_zeros(me_ptr);

      result |= jitter_buf_stream_read(me_ptr, output[0]);
      if (result == AR_EFAILED)
      {
         /* If we do not read into output we do not write into it either
          * This works as a queue to prevent overruns in case buffer is
          * almost full */
         return CAPI_EFAILED;
      }
   }

   return result;
}
