/**
 * \file capi_data_marker_md_utils.c
 * \brief
 *  
 *   Source file to implement utility functions called by the CAPI Interface for Data Marker module specifically to deal
 *  with metadata.
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "capi_data_marker_i.h"

/*==========================================================================
  Function Definitions
========================================================================== */
static uint32_t capi_data_marker_convert_offset_to_us(uint32_t offset_spc, capi_media_fmt_v2_t *media_fmt_v2_ptr)
{
   uint32_t offset_us = 0;
   if (CAPI_CMN_IS_PCM_FORMAT(media_fmt_v2_ptr->header.format_header.data_format))
   {
      // offset is in samples/ch (spc)
      offset_us = (offset_spc * 1000000) / media_fmt_v2_ptr->format.sampling_rate;
   }
   return offset_us;
}

static ar_result_t capi_data_marker_raise_delay_event_to_clients(capi_data_marker_t *me_ptr,
                                                                 uint32_t            delay,
                                                                 uint32_t            token,
																 uint32_t            frame_counter)
{
   // go through the list of client info; match event id to delay, and raise
   capi_err_t                         result = CAPI_EOK;
   capi_event_info_t                  event_info;
   capi_event_data_to_dsp_client_v2_t event;
   uint32_t                           payload_size = sizeof(event_id_delay_marker_info_t);
   event_id_delay_marker_info_t       event_payload;

   event_payload.delay         = delay;
   event_payload.token         = token;
   event_payload.frame_counter = frame_counter;

   spf_list_node_t *curr_list_node_ptr = me_ptr->client_info_list_ptr;

   while (curr_list_node_ptr)
   {
      event_reg_client_info_t *node_obj_ptr = (event_reg_client_info_t *)curr_list_node_ptr->obj_ptr;
      if (EVENT_ID_DELAY_MARKER_INFO == node_obj_ptr->event_id)
      {
         event.event_id                     = EVENT_ID_DELAY_MARKER_INFO;
         event.payload.actual_data_len      = payload_size;
         event.payload.max_data_len         = payload_size;
         event.payload.data_ptr             = (int8_t *)(&event_payload);
         event_info.port_info.is_valid      = FALSE;
         event_info.payload.actual_data_len = sizeof(capi_event_data_to_dsp_client_v2_t);
         event_info.payload.data_ptr        = (int8_t *)&event;
         event_info.payload.max_data_len    = sizeof(capi_event_data_to_dsp_client_v2_t);
         event.dest_address                 = node_obj_ptr->address;
         event.token                        = node_obj_ptr->token;

         AR_MSG(DBG_HIGH_PRIO, "capi_data_marker: 0x%lX: raise_delay_event_to_clients: raised event for detection", me_ptr->miid);

         result |= me_ptr->event_cb_info.event_cb(me_ptr->event_cb_info.event_context,
                                                  CAPI_EVENT_DATA_TO_DSP_CLIENT_V2,
                                                  &event_info);
         if (CAPI_EOK != result)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_data_marker: 0x%lX: raise_delay_event_to_clients: Failed to raise event for detection", me_ptr->miid);
         }
      }
      curr_list_node_ptr = curr_list_node_ptr->next_ptr;
   }
   return result;
}

ar_result_t capi_data_marker_intercept_delay_marker_and_check_raise_events(capi_data_marker_t *  me_ptr,
                                                                           module_cmn_md_list_t *md_list_ptr,
                                                                           bool_t                is_delay_event_reg)
{
   ar_result_t           result           = AR_EOK;
   module_cmn_md_list_t *node_ptr         = md_list_ptr;
   module_cmn_md_list_t *next_ptr         = NULL;
   uint64_t              intercepted_time = posal_timer_get_time();
   uint32_t              offset_us;
   while (node_ptr)
   {
      next_ptr = node_ptr->next_ptr;

      module_cmn_md_t *                 md_ptr       = (module_cmn_md_t *)node_ptr->obj_ptr;
      data_marker_md_id_delay_marker_t *delay_md_ptr = NULL;

      if (DATA_MARKER_MD_ID_DELAY_MARKER == md_ptr->metadata_id)
      {
         // payload is sent inband
         delay_md_ptr = (data_marker_md_id_delay_marker_t *)&(md_ptr->metadata_buf);

         offset_us      = capi_data_marker_convert_offset_to_us(md_ptr->offset, &me_ptr->operating_mf);
         uint32_t delay = intercepted_time - delay_md_ptr->insertion_time + offset_us;

         AR_MSG(DBG_HIGH_PRIO,
                "capi_data_marker: 0x%lX: PATH_DELAY_MEASUREMENT: Delay Marker MD found 0x%p with token 0x%lx, "
                "MD frame counter %lu, intercepted time %lu (0x%lx%lx) us, offset_us = 0x%lx, delay = %lu",
                me_ptr->miid,
                delay_md_ptr,
                delay_md_ptr->token,
                delay_md_ptr->frame_counter,
                (uint32_t)intercepted_time,
                (uint32_t)(intercepted_time >> 32),
                (uint32_t)intercepted_time,
                offset_us,
                delay);

         if (is_delay_event_reg)
         {
            result |= capi_data_marker_raise_delay_event_to_clients(me_ptr, delay, delay_md_ptr->token, delay_md_ptr->frame_counter);
         }
      }
#if 0
      else
      {
         AR_MSG(DBG_LOW_PRIO, "capi_data_marker: metadata 0x%lx ", md_ptr->metadata_id);
      }
#endif
      node_ptr = next_ptr;
   }
   return result;
}



ar_result_t capi_data_marker_insert_marker(capi_data_marker_t *me_ptr, module_cmn_md_list_t **md_list_pptr)
{
   ar_result_t result = AR_EOK;

   spf_list_node_t *curr_list_node_ptr = me_ptr->insert_md_cfg_list_ptr;
   spf_list_node_t *node_to_be_deleted_ptr = NULL;

   // this function is only called if the module is an insertor.
   if (!curr_list_node_ptr)
   {
      return AR_EOK;
   }

   uint64_t insertion_time = posal_timer_get_time();
   while (curr_list_node_ptr)
   {
      cfg_md_info_t *node_obj_ptr = (cfg_md_info_t *)curr_list_node_ptr->obj_ptr;

      if ((0 != node_obj_ptr->frame_dur_ms) &&
          (0 != ((me_ptr->frame_counter * me_ptr->cntr_frame_dur_us) % (node_obj_ptr->frame_dur_ms * 1000))))
      {
    	 curr_list_node_ptr = curr_list_node_ptr->next_ptr;
         continue;
      }

      // go through the cfg list; create md with the mdid, fill token
      module_cmn_md_t *new_md_ptr = NULL;

      result = me_ptr->metadata_handler.metadata_create(me_ptr->metadata_handler.context_ptr,
                                                        md_list_pptr,
                                                        sizeof(data_marker_md_id_delay_marker_t),
                                                        me_ptr->heap_mem,
                                                        FALSE /*is_out_band*/,
                                                        &new_md_ptr);

      new_md_ptr->metadata_id = node_obj_ptr->md_id;
      new_md_ptr->offset      = 0;

      if (DATA_MARKER_MD_ID_DELAY_MARKER == new_md_ptr->metadata_id)
      {
         // fill the md payload
         data_marker_md_id_delay_marker_t *created_md_payload_ptr =
            (data_marker_md_id_delay_marker_t *)(&(new_md_ptr->metadata_ptr));

         created_md_payload_ptr->token          = node_obj_ptr->token;
         created_md_payload_ptr->insertion_time = insertion_time;
         created_md_payload_ptr->frame_counter  = me_ptr->frame_counter;

         AR_MSG(DBG_HIGH_PRIO,
                "capi_data_marker: 0x%lX: PATH_DELAY_MEASUREMENT: Inserted delay marker 0x%p with token 0x%lx at time %lu (0x%lx%lx) us. Frame counter %lu",
                me_ptr->miid,
                created_md_payload_ptr,
                node_obj_ptr->token,
                (uint32_t)insertion_time,
                (uint32_t)(insertion_time >> 32),
                (uint32_t)insertion_time,
                me_ptr->frame_counter);
      }
      else
      {
         // no other md id supported today
      }

      //Save current node
	  node_to_be_deleted_ptr = curr_list_node_ptr;

      //Find the next node
      curr_list_node_ptr = curr_list_node_ptr->next_ptr;

      //Delete current node
      if (0 == node_obj_ptr->frame_dur_ms)
      {
         // do not remove for frame counter logic. remove after one time insertion otherwise.
         // after inserting remove the item (one time insert only)
         spf_list_delete_node_and_free_obj(&node_to_be_deleted_ptr, &me_ptr->insert_md_cfg_list_ptr, TRUE);
      }
   }

   return result;
}
