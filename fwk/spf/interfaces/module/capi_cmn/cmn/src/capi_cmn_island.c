/**
 * \file capi_cmn_island.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_cmn.h"

/*=====================================================================
  Functions
 ======================================================================*/

capi_err_t capi_cmn_raise_island_vote_event(capi_event_callback_info_t *cb_info_ptr, bool_t island_vote)
{
   if (NULL == cb_info_ptr->event_cb)
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "capi_cmn : Event callback is not set, Unable to raise island vote event!");
      return CAPI_EBADPARAM;
   }

   capi_err_t               result = CAPI_EOK;
   capi_event_island_vote_t event;
   event.island_vote = island_vote;
   capi_event_info_t event_info;
   event_info.port_info.is_valid      = FALSE;
   event_info.payload.actual_data_len = event_info.payload.max_data_len = sizeof(capi_event_island_vote_t);
   event_info.payload.data_ptr                                          = (int8_t *)(&event);
   result = cb_info_ptr->event_cb(cb_info_ptr->event_context, CAPI_EVENT_ISLAND_VOTE, &event_info);
   if (CAPI_FAILED(result))
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "capi_cmn : Failed to send Island vote event %lu update event", island_vote);
   }
   return result;
}

capi_err_t capi_cmn_update_kpps_event(capi_event_callback_info_t *cb_info_ptr, uint32_t kpps)
{
   if (NULL == cb_info_ptr->event_cb)
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "capi_cmn : Event callback is not set, Unable to raise kpps event!");
      return CAPI_EBADPARAM;
   }

   capi_err_t        result = CAPI_EOK;
   capi_event_KPPS_t event;
   event.KPPS = kpps;
   capi_event_info_t event_info;
   event_info.port_info.is_valid      = FALSE;
   event_info.payload.actual_data_len = event_info.payload.max_data_len = sizeof(capi_event_KPPS_t);
   event_info.payload.data_ptr                                          = (int8_t *)(&event);
   result = cb_info_ptr->event_cb(cb_info_ptr->event_context, CAPI_EVENT_KPPS, &event_info);
   if (CAPI_FAILED(result))
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "capi_cmn : Failed to send KPPS update event with %lu", result);
   }
   return result;
}

void capi_cmn_check_print_underrun(capi_cmn_underrun_info_t *underrun_info_ptr, uint32_t iid)
{
   underrun_info_ptr->underrun_counter++;
   uint64_t curr_time = posal_timer_get_time();
   uint64_t diff      = curr_time - underrun_info_ptr->prev_time;
   if ((diff >= CAPI_CMN_STEADY_STATE_UNDERRUN_TIME_THRESH_US) || (0 == underrun_info_ptr->prev_time))
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO,
                    "MODULE:%08lX, Underrun detected, Count:%ld, time since prev print: %ld us",
                    iid,
                    underrun_info_ptr->underrun_counter,
                    diff);
      underrun_info_ptr->prev_time        = curr_time;
      underrun_info_ptr->underrun_counter = 0;
   }

   return;
}

void capi_cmn_check_print_underrun_multiple_threshold(capi_cmn_underrun_info_t *underrun_info_ptr,
                                                      uint32_t                  iid,
                                                      bool                      need_to_reduce_underrun_print,
                                                      bool_t                    marker_eos,
                                                      bool_t                    is_capi_in_media_fmt_set)
{
   underrun_info_ptr->underrun_counter++;
   uint64_t curr_time = posal_timer_get_time();
   uint64_t diff      = curr_time - underrun_info_ptr->prev_time;
   uint64_t threshold = CAPI_CMN_UNDERRUN_TIME_THRESH_US;
   if (!need_to_reduce_underrun_print)
   {
      threshold = CAPI_CMN_STEADY_STATE_UNDERRUN_TIME_THRESH_US;
   }

   if ((diff >= threshold) || (0 == underrun_info_ptr->prev_time))
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO,
                    "MODULE:%08lX, Underrun detected, Count:%ld, time since prev print: %ld us, marker_eos: %d , "
                    "media_format_set: %d",
                    iid,
                    underrun_info_ptr->underrun_counter,
                    diff,
                    marker_eos,
                    is_capi_in_media_fmt_set);
      underrun_info_ptr->prev_time        = curr_time;
      underrun_info_ptr->underrun_counter = 0;
   }

   return;
}

void capi_cmn_dec_update_buffer_end_md(capi_stream_data_v2_t *in_stream_ptr,
                                       capi_stream_data_v2_t *out_stream_ptr,
                                       capi_err_t *           agg_process_result,
                                       bool_t *               error_recovery_done)
{
   // if result is not EOK or ENEEDMORE, or there is error_recovery_done, if we see end md we fill it and reset values
   // first level check will prevent unnecessary looping for end md
   if (((CAPI_EOK != *agg_process_result) && (CAPI_ENEEDMORE != *agg_process_result)) || (TRUE == *error_recovery_done))
   {
      module_cmn_md_list_t *node_ptr = in_stream_ptr->metadata_list_ptr;

      while (node_ptr)
      {
         module_cmn_md_t *md_ptr = node_ptr->obj_ptr;
         if (MODULE_CMN_MD_ID_BUFFER_END == md_ptr->metadata_id)
         {
            module_cmn_md_buffer_end_t *md_buf_end_ptr =
               (module_cmn_md_buffer_end_t *)((uint8_t *)md_ptr + sizeof(metadata_header_t));

            // error result
            capi_set_bits(&md_buf_end_ptr->flags,
                          MD_END_RESULT_FAILED,
                          MD_END_PAYLOAD_FLAGS_BIT_MASK_ERROR_RESULT,
                          MD_END_PAYLOAD_FLAGS_SHIFT_ERROR_RESULT);

            // result will be failed but if recovery is done we need to indicate to the clients
            if (TRUE == *error_recovery_done)
            {
               capi_set_bits(&md_buf_end_ptr->flags,
                             MD_END_RESULT_ERROR_RECOVERY_DONE,
                             MD_END_PAYLOAD_FLAGS_BIT_MASK_ERROR_RECOVERY_DONE,
                             MD_END_PAYLOAD_FLAGS_SHIFT_ERROR_RECOVERY_DONE);
            }

            // reset the flags
            *error_recovery_done = FALSE;
            *agg_process_result  = CAPI_EOK;

            AR_MSG_ISLAND(DBG_HIGH_PRIO,
                        "MD_DBG: Fill error in end md flags: node_ptr 0x%lx, md_ptr 0x%lx, md_id 0x%lx, buf idx msw:0x%lx "
                        "lsw:0x%lx, flag set %lx",
                        node_ptr,
                        md_ptr,
                        md_ptr->metadata_id,
                        md_buf_end_ptr->buffer_index_msw,
                        md_buf_end_ptr->buffer_index_lsw,
                        md_buf_end_ptr->flags);
            break;
         }
         node_ptr = node_ptr->next_ptr;
      }
   }
   return;
}
