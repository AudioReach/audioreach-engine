/**
 *   \file capi_jitter_buf_utils.c
 *   \brief
 *        This file contains CAPI implementation of Jitter Buf module.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_jitter_buf_i.h"

/*==============================================================================
   Public Function Implementation
==============================================================================*/

/* TODO: Pending - Currently dropping all metadata, need to implement metadata in circ buf. */
capi_err_t capi_jitter_buf_propagate_metadata(capi_jitter_buf_t *me_ptr, capi_stream_data_t *input)
{
   capi_err_t             capi_result   = CAPI_EOK;
   capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input;

   /* need to check if the stream version is v2 */
   if (CAPI_STREAM_V2 == in_stream_ptr->flags.stream_data_version) // stream version v2
   {
      // return if metadata list is NULL.
      if (in_stream_ptr->metadata_list_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "Warning: Dropping metadata on input port");
         capi_result |= capi_jitter_buf_destroy_md_list(me_ptr, &in_stream_ptr->metadata_list_ptr);
      }

      // EOF will be dropped from the module.
      if (in_stream_ptr->flags.end_of_frame)
      {
         AR_MSG(DBG_MED_PRIO, "Warning: EOF is dropped on input port");
         in_stream_ptr->flags.end_of_frame = FALSE;
      }
   }

   return capi_result;
}

/* Calls metadata_destroy on each node in the passed in metadata list.  */
capi_err_t capi_jitter_buf_destroy_md_list(capi_jitter_buf_t *me_ptr, module_cmn_md_list_t **md_list_pptr)
{
   module_cmn_md_list_t *next_ptr = NULL;
   for (module_cmn_md_list_t *node_ptr = *md_list_pptr; node_ptr;)
   {
      bool_t IS_DROPPED_TRUE = TRUE;
      next_ptr               = node_ptr->next_ptr;
      if (me_ptr->metadata_handler.metadata_destroy)
      {
         me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                   node_ptr,
                                                   IS_DROPPED_TRUE,
                                                   md_list_pptr);
      }
      else
      {
         AR_MSG(DBG_ERROR_PRIO, "jitter_buf: Error: metadata handler not provided, can't drop metadata.");
         return CAPI_EFAILED;
      }
      node_ptr = next_ptr;
   }

   return CAPI_EOK;
}

/* Set size of circular buffer based on the configuration received */
capi_err_t capi_jitter_buf_set_size(capi_jitter_buf_t *me_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (me_ptr->driver_hdl.stream_buf)
   {
      return result;
   }

   /* If debug value is present that takes precedence */
   if (me_ptr->debug_size_ms)
   {
      me_ptr->jitter_allowance_in_ms = me_ptr->debug_size_ms;
   }

   /* If jitter size is zero disable module and return*/
   if (me_ptr->jitter_allowance_in_ms == 0)
   {
      /* If config received is zero then module is disabled */
      result                             = capi_cmn_update_process_check_event(&me_ptr->event_cb_info, FALSE);
      me_ptr->event_config.process_state = FALSE;
      return result;
   }

   /* If calibration and media format are set then initialize the driver.
    * we need calibration to create buffers. */
   result = ar_result_to_capi_err(jitter_buf_driver_init(me_ptr));
   if (CAPI_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "jitter_buf: Cannot intialize the driver. ");
      return result;
   }

   return CAPI_EOK;
}

/* Check validate and store media format */
capi_err_t capi_jitter_buf_vaildate_and_cache_input_mf(capi_jitter_buf_t *me_ptr, capi_buf_t *buf_ptr)
{
   capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)buf_ptr->data_ptr;

   // compute the actual size of the mf.
   uint32_t actual_mf_size = media_fmt_ptr->format.num_channels * sizeof(uint16_t);
   actual_mf_size += sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t);

   // validate the MF payload
   if (buf_ptr->actual_data_len < actual_mf_size)
   {
      AR_MSG(DBG_ERROR_PRIO, "jitter_buf: Invalid media format size %d", buf_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   /*  TODO: Pending - Validate media format fields. Only deinterleaved ?*/

   memscpy(&me_ptr->operating_mf, sizeof(capi_media_fmt_v2_t), media_fmt_ptr, actual_mf_size);

   /* Set media format received to TRUE. */
   me_ptr->is_input_mf_received = TRUE;

   return CAPI_EOK;
}

/* Raise capi output media format after receiving input media format */
capi_err_t capi_jitter_buf_raise_output_mf_event(capi_jitter_buf_t *me_ptr, capi_media_fmt_v2_t *mf_info_ptr)
{
   capi_err_t        result = CAPI_EOK;
   capi_event_info_t event_info;

   event_info.port_info.is_valid      = TRUE;
   event_info.port_info.is_input_port = FALSE;
   event_info.port_info.port_index    = 0;

   event_info.payload.actual_data_len = sizeof(capi_media_fmt_v2_t);
   event_info.payload.data_ptr        = (int8_t *)mf_info_ptr;
   event_info.payload.max_data_len    = sizeof(capi_media_fmt_v2_t);

   result = me_ptr->event_cb_info.event_cb(me_ptr->event_cb_info.event_context,
                                           CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED_V2,
                                           &event_info);
   if (CAPI_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "jitter_buf: Failed to raise event for output media format");
   }

   return result;
}

/* Raise kpps and bw depending on input media format */
/* TODO: Pending - Check values */
capi_err_t capi_jitter_buf_raise_kpps_bw_event(capi_jitter_buf_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (!me_ptr->is_input_mf_received)
   {
      return capi_result;
   }

   uint32_t kpps, data_bw;

   if (me_ptr->operating_mf.format.num_channels <= 8)
   {
      kpps    = JITTER_BUF_KPPS;
      data_bw = JITTER_BUF_BW;
   }
   else if (me_ptr->operating_mf.format.num_channels <= 16)
   {
      kpps    = JITTER_BUF_KPPS_16;
      data_bw = JITTER_BUF_BW_16;
   }
   else
   {
      kpps    = JITTER_BUF_KPPS_32;
      data_bw = JITTER_BUF_BW_32;
   }

   if (kpps != me_ptr->event_config.kpps)
   {
      capi_result |= capi_cmn_update_kpps_event(&me_ptr->event_cb_info, kpps);
      if (!capi_result)
      {
         me_ptr->event_config.kpps = kpps;
      }
   }

   if (data_bw != me_ptr->event_config.data_bw)
   {
      capi_result |= capi_cmn_update_bandwidth_event(&me_ptr->event_cb_info, 0, data_bw);
      if (!capi_result)
      {
         me_ptr->event_config.data_bw = data_bw;
      }
   }

   return capi_result;
}

