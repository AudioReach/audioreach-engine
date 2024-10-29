/**
 *   \file jitter_buf_driver.c
 *   \brief
 *        This file contains implementation of Jitter buffer driver
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "jitter_buf_driver.h"
#include "spf_circular_buffer.h"
#include "ar_msg.h"
#include "spf_list_utils.h"
#include "capi_jitter_buf_i.h"

#ifdef DEBUG_CIRC_BUF_UTILS
#define DEBUG_JITTER_BUF_DRIVER
#endif

#define CIRCULAR_BUFFER_PREFERRED_CHUNK_SIZE 2048

/*==============================================================================
   Local Function Implementation
==============================================================================*/

/* Get the size of the jitter buffer in microseconds */
ar_result_t jitter_buf_driver_get_required_circbuf_size_in_us(capi_jitter_buf_t *me_ptr, uint32_t *size_us_ptr)
{
   uint32_t buf_size_in_ms = 0;

   buf_size_in_ms = me_ptr->jitter_allowance_in_ms;

   *size_us_ptr = (buf_size_in_ms * 1000);

#ifdef DEBUG_JITTER_BUF_DRIVER
   AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: Required circular_buf_size_in_us = %lu", *size_us_ptr);
#endif

   return AR_EOK;
}

/* Event callback that raises media format when changed - this is sent as a
 * part of init params to the spf circular buffer */
capi_err_t jitter_buf_circular_buffer_event_cb(void *          context_ptr,
                                               spf_circ_buf_t *circ_buf_ptr,
                                               uint32_t        event_id,
                                               void *          event_info_ptr)
{
   capi_jitter_buf_t *me_ptr = (capi_jitter_buf_t *)context_ptr;

   if (event_id == SPF_CIRC_BUF_EVENT_ID_OUTPUT_MEDIA_FORMAT)
   {
      AR_MSG(DBG_HIGH_PRIO, "JITTER_BUF_DRIVER: circular buf raised output media format");
      capi_jitter_buf_raise_output_mf_event(me_ptr, (capi_media_fmt_v2_t *)event_info_ptr);
   }

   return CAPI_EOK;
}

/* Initialize the sizes, callback events, reader and writer clients*/
ar_result_t jitter_buf_driver_intialize_circular_buffers(capi_jitter_buf_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   if (!me_ptr)
   {
      return AR_EFAILED;
   }

   /* All the input arguments are same for all channel buffers except buffer ID. */
   spf_circ_buf_alloc_inp_args_t inp_args;
   memset(&inp_args, 0, sizeof(spf_circ_buf_alloc_inp_args_t));
   inp_args.preferred_chunk_size  = CIRCULAR_BUFFER_PREFERRED_CHUNK_SIZE;
   inp_args.heap_id               = me_ptr->heap_id;
   inp_args.metadata_handler      = &me_ptr->metadata_handler;
   inp_args.cb_info.event_cb      = jitter_buf_circular_buffer_event_cb;
   inp_args.cb_info.event_context = me_ptr;

   /* Intiailizes circular buffers and returns handle */
   if (AR_DID_FAIL(result = spf_circ_buf_init(&me_ptr->driver_hdl.stream_buf, &inp_args)))
   {
#ifdef DEBUG_JITTER_BUF_DRIVER
      AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: buffer create failed");
#endif
      return AR_EFAILED;
   }

   /* Register the writer handle with the driver. */
   if (AR_DID_FAIL(result = spf_circ_buf_register_writer_client(me_ptr->driver_hdl.stream_buf,
                                                                me_ptr->driver_hdl.circ_buf_size_in_us, /* buf size*/
                                                                &me_ptr->driver_hdl.writer_handle)))
   {
#ifdef DEBUG_JITTER_BUF_DRIVER
      AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: writer registration failed.");
#endif
      return AR_EFAILED;
   }

   /* Register the reader with the driver handle. */
   if (AR_DID_FAIL(result = spf_circ_buf_register_reader_client(me_ptr->driver_hdl.stream_buf,
                                                                0, // not requesting any resize
                                                                &me_ptr->driver_hdl.reader_handle)))
   {
#ifdef DEBUG_JITTER_BUF_DRIVER
      AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: Reader registration failed.");
#endif
      return AR_EFAILED;
   }

   return result;
}

/*==============================================================================
   Public Function Implementation
==============================================================================*/
ar_result_t jitter_buf_driver_init(capi_jitter_buf_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   if (NULL == me_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: Init failed. bad input params ");
      return AR_EBADPARAM;
   }

   /* if calibration or mf is not set. return not ready. */
   if (!me_ptr->jitter_allowance_in_ms)
   {
      AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: Not ready to initalize. ");
      return AR_ENOTREADY;
   }

   AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: Initializing the driver");

   /* Get circular buffer size. */
   uint32_t new_circbuf_size_in_us = 0;
   jitter_buf_driver_get_required_circbuf_size_in_us(me_ptr, &new_circbuf_size_in_us);

   me_ptr->driver_hdl.circ_buf_size_in_us = new_circbuf_size_in_us;

   /* Create circular buffers for each channel, initialize read and write handles. */
   if (AR_EOK != (result = jitter_buf_driver_intialize_circular_buffers(me_ptr)))
   {
      return result;
   }

   /* Compute upped and lower drift thresholds. And water mark levels in bytes based on mf. */
   if (AR_EOK != (result = jitter_buf_calibrate_driver(me_ptr)))
   {
      return result;
   }

   return result;
}

/* Calibrate the parameters related to media format and also the circular
 * buffer in case any of them have changed */
ar_result_t jitter_buf_calibrate_driver(capi_jitter_buf_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   if (!me_ptr->is_input_mf_received)
   {
      return result;
   }

   if (me_ptr->jitter_allowance_in_ms)
   {
      me_ptr->jiter_bytes = capi_cmn_us_to_bytes_per_ch(me_ptr->jitter_allowance_in_ms * 1000,
                                                        me_ptr->operating_mf.format.sampling_rate,
                                                        me_ptr->operating_mf.format.bits_per_sample);
   }

   if (me_ptr->frame_duration_in_bytes)
   {
      me_ptr->frame_duration_in_us = capi_cmn_bytes_to_us(me_ptr->frame_duration_in_bytes,
                                                          me_ptr->operating_mf.format.sampling_rate,
                                                          me_ptr->operating_mf.format.bits_per_sample,
                                                          1,
                                                          NULL);
   }

   spf_circ_buf_result_t circ_buf_result = spf_circ_buf_set_media_format(me_ptr->driver_hdl.writer_handle,
                                                                        &me_ptr->operating_mf,
                                                                        me_ptr->frame_duration_in_us);
   if (SPF_CIRCBUF_SUCCESS != circ_buf_result)
   {
       result = AR_EFAILED;
       me_ptr->is_disabled_by_failure = TRUE;
   }
   return result;
}

/* Deinit and destroy circular buffer if created */
ar_result_t jitter_buf_driver_deinit(capi_jitter_buf_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   if (NULL == me_ptr || NULL == me_ptr->driver_hdl.stream_buf)
   {
#ifdef DEBUG_JITTER_BUF_DRIVER
      AR_MSG(DBG_ERROR_PRIO, "jitter_buf_driver_deinit: Failed. bad input params.");
#endif
      return AR_EBADPARAM;
   }

   /* Destory the driver. Destroys circular buffers and reader/writer registers */
   if (AR_DID_FAIL(result = spf_circ_buf_deinit(&me_ptr->driver_hdl.stream_buf)))
   {
      return AR_EFAILED;
   }

   return result;
}
