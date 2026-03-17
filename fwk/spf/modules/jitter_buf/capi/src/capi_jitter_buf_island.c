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

   /* If debug value is present that takes precedence */
   if (!me_ptr->jitter_allowance_in_ms && me_ptr->debug_size_ms)
   {
      me_ptr->jitter_allowance_in_ms = me_ptr->debug_size_ms;
   }

   if (!me_ptr->is_input_mf_received || !me_ptr->jitter_allowance_in_ms || me_ptr->is_disabled_by_failure)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "jitter_buf: buf size %d, mf received %d, is_module_disabled_by_fatal_failure %d",
             me_ptr->jitter_allowance_in_ms,
             me_ptr->is_input_mf_received,
             me_ptr->is_disabled_by_failure);

      if (me_ptr->is_input_mf_received && input[0]->buf_ptr)
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

      /*Circular buffer can work with only deinterleaved unpacked as of now */
      me_ptr->frame_duration_in_us = capi_cmn_bytes_to_us(input[0]->buf_ptr->actual_data_len,
                                                          me_ptr->operating_mf.format.sampling_rate,
                                                          me_ptr->operating_mf.format.bits_per_sample,
                                                          1,
                                                          NULL);

#ifdef DEBUG_JITTER_BUF_DRIVER
      AR_MSG(DBG_LOW_PRIO, "jitter_buf: frame duration set to %lu", me_ptr->frame_duration_in_us);
#endif
      posal_island_trigger_island_exit();
      if (CAPI_EOK != (result = capi_jitter_buf_set_size(me_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO, "jitter_buf: Failed init the driver with error = %lx", result);

         for (int i = 0; i < input[0]->bufs_num; i++)
         {
            // marking as input not consumed
            input[0]->buf_ptr[i].actual_data_len = 0;
         }

         return result;
      }
   }

   bool_t is_output_written = FALSE, update_drift_thresh = FALSE, is_input_trig = FALSE;

   /* mode0: input buffer at output trigger
    * Module is driven based on the reader availability. So when output is available we
    * first read it and then only write input if available. This module does not tolerate dropping
    * input data at any cost.
    *
    * mode1: input buffer at input trigger
    * Module is driven based on either reader or writer availability. */

   // if input and output are independently triggered then always prefills zeros when buffer is empty.
   // This is to ensure that zeros are pushed in the beginning and not in between of valid input data.
   if (JBM_BUFFER_INPUT_AT_INPUT_TRIGGER == me_ptr->configured_buffer_mode)
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

         /* This should be the first output trigger after settlement time is reached - we must be in steady state
          * from now onwards. */
         if ((output[0]->buf_ptr) && (output[0]->buf_ptr->data_ptr) && me_ptr->settlement_time_done &&
             (!me_ptr->steady_state_value_done))
         {
            update_drift_thresh = TRUE;
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

         is_input_trig = (NULL != input[0]->buf_ptr->data_ptr);
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

   // drift detection is disabled if module is configured at input buffering mode.
   if (JBM_BUFFER_INPUT_AT_INPUT_TRIGGER != me_ptr->configured_buffer_mode)
   {
      /* Apply drift correction if buffer size crosses upper/lower thresholds. Depending on
       * current status of fullness change trigger policy mode */
      if (update_drift_thresh)
      {
         jitter_buffer_set_steady_state_buffer_fullness(me_ptr);
      }

      uint8_t prev_buffer_mode = me_ptr->buffer_mode;

      jitter_buf_update_drift_based_on_buffer_fullness(me_ptr, is_input_trig);

      if (prev_buffer_mode != me_ptr->buffer_mode)
      {
         capi_jitter_buf_change_trigger_policy(me_ptr);
      }
   }

   return result;
}

/* Jitter buffer is by default driven based on output availability. Only if output is read
 * input is written if it is available at that time. If not, the input gets buffered
 * up in congestion buffering module.
 * If input buffering mode is enabled (ICMD) then input is written if it is available.*/
capi_err_t capi_jitter_buf_change_trigger_policy(capi_jitter_buf_t *me_ptr)
{
   capi_err_t result = AR_EOK;

   if (NULL == me_ptr->policy_chg_cb.change_data_trigger_policy_cb_fn)
   {
      return result;
   }

   fwk_extn_port_trigger_affinity_t input_group1  = { FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE };
   fwk_extn_port_trigger_affinity_t output_group1 = { FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT };

   fwk_extn_port_trigger_group_t triggerable_groups[1];
   triggerable_groups[0].in_port_grp_affinity_ptr  = &input_group1;
   triggerable_groups[0].out_port_grp_affinity_ptr = &output_group1;

   fwk_extn_port_nontrigger_policy_t input_group2  = { FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL };
   fwk_extn_port_nontrigger_policy_t output_group2 = { FWK_EXTN_PORT_NON_TRIGGER_INVALID };

   fwk_extn_port_nontrigger_group_t nontriggerable_group[1];
   nontriggerable_group[0].in_port_grp_policy_ptr  = &input_group2;
   nontriggerable_group[0].out_port_grp_policy_ptr = &output_group2;

   /* If input buffering is to be done at input trigger then need to mark optional triggerable.*/
   if (JBM_BUFFER_INPUT_AT_INPUT_TRIGGER == me_ptr->buffer_mode)
   {
      input_group1 = FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;
      input_group2 = FWK_EXTN_PORT_NON_TRIGGER_INVALID;
   }

   // By default set the mode to RT, when the write arrives then make it FTRT.
   result = me_ptr->policy_chg_cb.change_data_trigger_policy_cb_fn(me_ptr->policy_chg_cb.context_ptr,
                                                                   nontriggerable_group,
                                                                   FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL,
                                                                   1,
                                                                   triggerable_groups);

   AR_MSG(DBG_HIGH_PRIO, "jitter_buf: Raised TP with mode : %d", me_ptr->buffer_mode);
   return result;
}
