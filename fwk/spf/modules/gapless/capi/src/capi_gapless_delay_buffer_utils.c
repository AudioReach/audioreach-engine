/**
 * \file capi_gapless_delay_buffer_utils.c
 * \brief
 *      Implementation of utility functions for handling the delay buffer.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_gapless_i.h"

bool_t capi_gapless_does_delay_buffer_exist(capi_gapless_t *me_ptr, capi_gapless_in_port_t *in_port_ptr)
{
   if (in_port_ptr && in_port_ptr->stream_drv_ptr)
   {
      return TRUE;
   }
   return FALSE;
}

bool_t capi_gapless_is_delay_buffer_empty(capi_gapless_t *me_ptr, capi_gapless_in_port_t *in_port_ptr)
{
   if (capi_gapless_does_delay_buffer_exist(me_ptr, in_port_ptr))
   {
      bool_t is_empty = FALSE;
      spf_circ_buf_driver_is_buffer_empty(in_port_ptr->reader_handle, &is_empty);
      return is_empty;
   }
   else
   {
      // Delay buffer doesn't exist, so it's empty.
      return TRUE;
   }

   return FALSE;
}

bool_t capi_gapless_is_delay_buffer_full(capi_gapless_t *me_ptr, capi_gapless_in_port_t *in_port_ptr)
{
   bool_t is_full = FALSE;

   if (capi_gapless_does_delay_buffer_exist(me_ptr, in_port_ptr))
   {
      spf_circ_buf_driver_is_buffer_full(in_port_ptr->reader_handle, &is_full);
   }

   return is_full;
}

capi_err_t gapless_read_delay_buffer(capi_gapless_t *        me_ptr,
                                     capi_gapless_in_port_t *in_port_ptr,
                                     capi_stream_data_v2_t * out_sdata_ptr,
                                     module_cmn_md_t **      eos_md_pptr)
{
   capi_err_t            capi_result = CAPI_EOK;
   ar_result_t           ar_result   = AR_EOK;
   capi_stream_data_v2_t temp_out_sdata;
   bool_t                has_partial_output_data_md     = FALSE;
   uint32_t              prev_actual_data_len_per_ch = 0;

   if (!capi_gapless_does_delay_buffer_exist(me_ptr, in_port_ptr))
   {
      AR_MSG(DBG_MED_PRIO,
             "Trying to read from input port idx %lx's delay buffer but it doesn't exist.",
             in_port_ptr->cmn.index);
      return CAPI_EFAILED;
   }

   if ((!out_sdata_ptr) || (!out_sdata_ptr->buf_ptr) || (!out_sdata_ptr->buf_ptr[0].data_ptr))
   {
#ifdef CAPI_GAPLESS_DEBUG
      AR_MSG(DBG_MED_PRIO,
             "Output stream data was not provided: sdata_ptr 0x%lx, buf_ptr 0x%lx, data_ptr 0x%lx. Returning early.",
             out_sdata_ptr,
             (out_sdata_ptr ? out_sdata_ptr->buf_ptr : NULL),
             ((out_sdata_ptr && out_sdata_ptr->buf_ptr) ? out_sdata_ptr->buf_ptr[0].data_ptr : NULL));
#endif
      return CAPI_EOK;
   }

   if (out_sdata_ptr->buf_ptr[0].actual_data_len == out_sdata_ptr->buf_ptr[0].max_data_len)
   {
#ifdef CAPI_GAPLESS_DEBUG
      AR_MSG(DBG_MED_PRIO, "Output sdata was full. No more room to write.");
#endif
      return CAPI_EOK;
   }

   has_partial_output_data_md = ((0 != out_sdata_ptr->buf_ptr[0].actual_data_len) || (NULL != out_sdata_ptr->metadata_list_ptr));

   if (has_partial_output_data_md)
   {
      capi_result |= gapless_setup_output_sdata(me_ptr, &temp_out_sdata, out_sdata_ptr, &prev_actual_data_len_per_ch);
   }

   // Read one container frame from the circular buffer.
   ar_result = spf_circ_buf_read_one_frame(in_port_ptr->reader_handle, (capi_stream_data_t *)out_sdata_ptr);
   if (AR_EFAILED == ar_result)
   {
      AR_MSG(DBG_HIGH_PRIO, "delay_buf: Failed reading data from the input port 0x%lx buffer.", in_port_ptr->cmn.index);
      return CAPI_EFAILED;
   }

   // Search for newly generated eos. This should be done before adjusting since we shouldn't count EOS which was
   // already output from a previous write_output.
   if (!out_sdata_ptr->metadata_list_ptr)
   {
      *eos_md_pptr = NULL;
   }
   else
   {
      *eos_md_pptr = gapless_find_eos(out_sdata_ptr);
      if (*eos_md_pptr)
      {
         AR_MSG(DBG_HIGH_PRIO, "EOS found on output port! md_ptr 0x%lx", *eos_md_pptr);
      }
   }

   if (has_partial_output_data_md)
   {
      capi_result |= gapless_adjust_output_sdata(me_ptr, &temp_out_sdata, out_sdata_ptr, prev_actual_data_len_per_ch);
   }

#ifdef CAPI_GAPLESS_DEBUG
   uint32_t unread_bytes = 0;
   ar_result |= spf_circ_buf_get_unread_bytes(in_port_ptr->reader_handle, &unread_bytes);
   if (AR_EFAILED == ar_result)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "Failed getting unread bytes from input index 0x%lx's delay buffer.",
             in_port_ptr->cmn.index);
      return CAPI_EFAILED;
   }
   AR_MSG(DBG_LOW_PRIO,
          "Read %ld bytes per channel of data into output from the delay buffer at input idx %ld id 0x%lx. Unread "
          "bytes after read: %ld",
          ((out_sdata_ptr && out_sdata_ptr->buf_ptr) ? out_sdata_ptr->buf_ptr[0].actual_data_len : 0),
          in_port_ptr->cmn.index,
          in_port_ptr->cmn.port_id,
          unread_bytes);
#endif

   return capi_result;
}

capi_err_t gapless_raise_allow_duty_cycling_event(capi_gapless_t *me_ptr, bool_t allow_duty_cycling)
{
   capi_err_t result = CAPI_EOK;

   intf_extn_event_id_allow_duty_cycling_v2_t event_payload;
   event_payload.allow_duty_cycling = allow_duty_cycling;

   /* Create event */
   capi_event_data_to_dsp_service_t to_send;
   to_send.param_id                = INTF_EXTN_EVENT_ID_ALLOW_DUTY_CYCLING_V2;
   to_send.payload.actual_data_len = sizeof(intf_extn_event_id_allow_duty_cycling_v2_t);
   to_send.payload.max_data_len    = sizeof(intf_extn_event_id_allow_duty_cycling_v2_t);
   to_send.payload.data_ptr        = (int8_t *)&event_payload;

   /* Create event info */
   capi_event_info_t event_info;
   event_info.port_info.is_input_port = FALSE;
   event_info.port_info.is_valid      = FALSE;
   event_info.payload.actual_data_len = sizeof(to_send);
   event_info.payload.max_data_len    = sizeof(to_send);
   event_info.payload.data_ptr        = (int8_t *)&to_send;

   result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_DATA_TO_DSP_SERVICE, &event_info);

   if (CAPI_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_gapless: Failed to raise bufer fullness event");
   }
   else
   {
      AR_MSG(DBG_HIGH_PRIO, "capi_gapless: Raised Buffer fullness event");
   }
   return result;
}

capi_err_t gapless_write_delay_buffer(capi_gapless_t *        me_ptr,
                                      capi_gapless_in_port_t *in_port_ptr,
                                      capi_stream_data_v2_t * in_sdata_ptr)
{
   bool_t ALLOW_OVERFLOW_FALSE = FALSE;

   if (!capi_gapless_does_delay_buffer_exist(me_ptr, in_port_ptr))
   {
      AR_MSG(DBG_MED_PRIO,
             "Trying to write to input port idx %lx's delay buffer but it doesn't exist.",
             in_port_ptr->cmn.index);
      return CAPI_EFAILED;
   }

   // Write one container frame to the circular buffer.
   ar_result_t result = spf_circ_buf_write_one_frame(in_port_ptr->writer_handle,
                                                     (capi_stream_data_t *)in_sdata_ptr,
                                                     ALLOW_OVERFLOW_FALSE);

   if (AR_EFAILED == result)
   {
      AR_MSG(DBG_HIGH_PRIO, "delay_buf: Failed writing data into input ports 0x%lx buffer.", in_port_ptr->cmn.index);
      return CAPI_EFAILED;
   }
   if (me_ptr->is_gapless_cntr_duty_cycling)
   {
      if (capi_gapless_is_delay_buffer_full(me_ptr, in_port_ptr))
      {
         AR_MSG(DBG_HIGH_PRIO, "gapless ready for entering island due to buffer fullness");
         result = gapless_raise_allow_duty_cycling_event(me_ptr, TRUE);
         if (AR_EFAILED == result)
         {
            AR_MSG(DBG_HIGH_PRIO, "delay_buf: Failed to raise island entry event");
            return CAPI_EFAILED;
         }
      }
   }

#ifdef CAPI_GAPLESS_DEBUG
   uint32_t    bytes_buffered = (in_sdata_ptr && in_sdata_ptr->buf_ptr) ? in_sdata_ptr->buf_ptr[0].actual_data_len : 0;
   uint32_t    unread_bytes   = 0;
   ar_result_t loc_result     = AR_EOK;
   loc_result |= spf_circ_buf_get_unread_bytes(in_port_ptr->reader_handle, &unread_bytes);
   if (AR_EFAILED == loc_result)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "Failed getting unread bytes from input index 0x%lx's delay buffer.",
             in_port_ptr->cmn.index);
      return CAPI_EFAILED;
   }

   if (bytes_buffered)
   {
      AR_MSG(DBG_LOW_PRIO,
             "Wrote %ld bytes per channel of data into the delay buffer at input port idx %ld id 0x%lx. Unread bytes "
             "after write: %ld",
             bytes_buffered,
             in_port_ptr->cmn.index,
             in_port_ptr->cmn.port_id,
             unread_bytes);
   }
#endif

   return CAPI_EOK;
}

capi_err_t capi_gapless_create_delay_buffer(capi_gapless_t *        me_ptr,
                                            capi_gapless_in_port_t *in_port_ptr,
                                            capi_media_fmt_v2_t *   media_fmt_ptr,
                                            uint32_t                size_ms)
{
   ar_result_t result = AR_EOK;

   if (in_port_ptr->stream_drv_ptr)
   {
      return AR_EOK;
   }

   // All the input arguments are same for all channel buffers except buffer ID.
   spf_circ_buf_alloc_inp_args_t inp_args;
   memset(&inp_args, 0, sizeof(spf_circ_buf_alloc_inp_args_t));
   inp_args.preferred_chunk_size  = GAPLESS_PREFERRED_CHUNK_SIZE;
   inp_args.heap_id               = (POSAL_HEAP_ID)me_ptr->heap_info.heap_id;
   inp_args.metadata_handler      = &me_ptr->metadata_handler;
   inp_args.buf_id                = in_port_ptr->cmn.index;
   inp_args.cb_info.event_cb      = gapless_circular_buffer_event_cb;
   inp_args.cb_info.event_context = (void *)me_ptr;

   // Creates circular buffers and returns handle
   if (AR_DID_FAIL(result |= spf_circ_buf_init(&in_port_ptr->stream_drv_ptr, &inp_args)))
   {
      AR_MSG(DBG_ERROR_PRIO, "delay_buf: create failed");
      return AR_EFAILED;
   }

   // register the writer handle with the driver.
   if (AR_DID_FAIL(result |= spf_circ_buf_register_writer_client(in_port_ptr->stream_drv_ptr,
                                                                 size_ms * 1000, // us
                                                                 &in_port_ptr->writer_handle)))
   {
      AR_MSG(DBG_ERROR_PRIO, "delay_buf: writer reg failed");
      return AR_EFAILED;
   }

   // register the reader with the driver handle.
   if (AR_DID_FAIL(result |= spf_circ_buf_register_reader_client(in_port_ptr->stream_drv_ptr,
                                                                 0, // not requesting any resize
                                                                 &in_port_ptr->reader_handle)))
   {
      AR_MSG(DBG_ERROR_PRIO, "delay_buf: reader reg failed");
      return AR_EFAILED;
   }

   AR_MSG(DBG_MED_PRIO, "delay_buf: Created stream buffer for port_index = 0x%lx", in_port_ptr->cmn.port_id);
   return result;
}

capi_err_t capi_gapless_destroy_delay_buffer(capi_gapless_t *me_ptr, capi_gapless_in_port_t *in_port_ptr)
{

   // Destroys circular buffers and reader/writer registers
   capi_err_t res = spf_circ_buf_deinit(&in_port_ptr->stream_drv_ptr);
   if (CAPI_EOK != res)
   {
      return CAPI_EFAILED;
   }

   in_port_ptr->writer_handle = NULL;
   in_port_ptr->reader_handle = NULL;

   return CAPI_EOK;
}

capi_err_t capi_gapless_get_delay_buffer_media_fmt(capi_gapless_t *        me_ptr,
                                                   capi_gapless_in_port_t *in_port_ptr,
                                                   capi_media_fmt_v2_t *   ret_mf_ptr)
{
   if ((!in_port_ptr->writer_handle) || (!ret_mf_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "delay_buf: null arg to get_delay_buffer_media_fmt");
      return CAPI_EFAILED;
   }

   // get the current media format of the reader client.
   return spf_circ_buf_get_media_format(in_port_ptr->reader_handle, ret_mf_ptr);
   ;
}

capi_err_t capi_gapless_set_delay_buffer_media_fmt(capi_gapless_t *        me_ptr,
                                                   capi_gapless_in_port_t *in_port_ptr,
                                                   capi_media_fmt_v2_t *   media_fmt_ptr,
                                                   uint32_t                cntr_frame_size_us)
{
   // set writers media format on the circular buffer
   return spf_circ_buf_set_media_format(in_port_ptr->writer_handle, media_fmt_ptr, cntr_frame_size_us);
}

/* Callback funtion used by circular buffer to raise output media format events whenever there is a change in
 * media format for a reader client*/
capi_err_t gapless_circular_buffer_event_cb(void *          context_ptr,
                                            spf_circ_buf_t *circ_buf_ptr,
                                            uint32_t        event_id,
                                            void *          event_info_ptr)
{
   capi_err_t      result = CAPI_EOK;
   capi_gapless_t *me_ptr = (capi_gapless_t *)context_ptr;

   if (NULL == circ_buf_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "NULL circ buf ptr in circ buf callback");
      return CAPI_EFAILED;
   }

   if (event_id == SPF_CIRC_BUF_EVENT_ID_OUTPUT_MEDIA_FORMAT)
   {
      // Find input port associated with the event.
      capi_gapless_in_port_t *in_port_ptr = NULL;
      for (uint32_t i = 0; i < me_ptr->num_in_ports; i++)
      {
         capi_gapless_in_port_t *cur_in_port_ptr = &(me_ptr->in_ports[i]);
         if (cur_in_port_ptr->stream_drv_ptr == circ_buf_ptr)
         {
            in_port_ptr = cur_in_port_ptr;
            break;
         }
      }

      if (NULL == in_port_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "circ buf ptr 0x%lx which raised output media format event does not correspond to any gapless input "
                "port.",
                circ_buf_ptr);
         return CAPI_EFAILED;
      }

      // Handle event immediately if this is the active stream or if there is not an operating media formatyet .
      // Otherwise, when switchings streams, raise event if opmf changed.
      if (capi_gapless_should_set_operating_media_format(me_ptr, in_port_ptr))
      {
         capi_media_fmt_v2_t *evt_mf_info_ptr = (capi_media_fmt_v2_t *)event_info_ptr;
         result |= capi_gapless_set_operating_media_format(me_ptr, evt_mf_info_ptr);
      }
      else
      {
         AR_MSG(DBG_HIGH_PRIO,
                "delay_buf: not raising output media format coming from inactive input port idx %ld",
                in_port_ptr->cmn.index);
      }
   }

   return result;
}
