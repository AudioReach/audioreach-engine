/**
 * \file capi_smart_sync.c
 * \brief
 *       Implementation of smart sync capi API functions.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_smart_sync_i.h"
#include "module_cmn_api.h"

/* =========================================================================
 * Static function declarations
 * =========================================================================*/

static capi_err_t capi_smart_sync_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

static capi_err_t capi_smart_sync_end(capi_t *_pif);

static capi_err_t capi_smart_sync_set_param(capi_t *                _pif,
                                            uint32_t                param_id,
                                            const capi_port_info_t *port_info_ptr,
                                            capi_buf_t *            params_ptr);

static capi_err_t capi_smart_sync_get_param(capi_t *                _pif,
                                            uint32_t                param_id,
                                            const capi_port_info_t *port_info_ptr,
                                            capi_buf_t *            params_ptr);

static capi_err_t capi_smart_sync_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static capi_err_t capi_smart_sync_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static capi_err_t capi_smart_sync_process_set_properties(capi_smart_sync_t *me_ptr, capi_proplist_t *proplist_ptr);

static capi_err_t capi_smart_sync_process_get_properties(capi_smart_sync_t *me_ptr, capi_proplist_t *proplist_ptr);

static capi_vtbl_t vtbl = { capi_smart_sync_process,        capi_smart_sync_end,
                            capi_smart_sync_set_param,      capi_smart_sync_get_param,
                            capi_smart_sync_set_properties, capi_smart_sync_get_properties };

/* =========================================================================
 * Function definitions
 * =========================================================================*/

/**
 * capi smart sync function to get the static properties
 */
capi_err_t capi_smart_sync_get_static_properties(capi_proplist_t *init_set_properties,
                                                 capi_proplist_t *static_properties)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL != static_properties)
   {
      capi_result = capi_smart_sync_process_get_properties((capi_smart_sync_t *)NULL, static_properties);
      if (CAPI_FAILED(capi_result))
      {
         AR_MSG(DBG_ERROR_PRIO, "capi smart sync: get static properties failed!");
         return capi_result;
      }
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Get static properties received bad pointer, 0x%p", static_properties);
   }

   return capi_result;
}

/**
 * Initialize the CAPIv2 Smart Sync Module. This function can allocate memory.
 */
capi_err_t capi_smart_sync_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == _pif || NULL == init_set_properties)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Init received bad pointer, 0x%p, 0x%p", _pif, init_set_properties);

      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_smart_sync_t *me_ptr = (capi_smart_sync_t *)_pif;

   memset(me_ptr, 0, sizeof(capi_smart_sync_t));

   me_ptr->vtbl.vtbl_ptr = &vtbl;

   if (CAPI_FAILED(capi_result))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Initialization Failed");
      return capi_result;
   }

   me_ptr->disable_ts_disc_handling = TRUE;

   // Start out with ports closed. State is interpreted as synced until second port is opened.
   me_ptr->state = SMART_SYNC_STATE_BEFORE_FIRST_PROC_TICK;

   if (NULL != init_set_properties)
   {
      capi_result = capi_smart_sync_process_set_properties(me_ptr, init_set_properties);
      if (CAPI_FAILED(capi_result))
      {
         AR_MSG(DBG_ERROR_PRIO, "capi smart sync:  Initialization Set Property Failed");
         return capi_result;
      }
   }

   capi_result |= capi_smart_sync_init_cmn_port(me_ptr, &(me_ptr->primary_in_port_info.cmn));
   capi_result |= capi_cmn_init_media_fmt_v2(&(me_ptr->primary_in_port_info.media_fmt));

   capi_result |= capi_smart_sync_init_cmn_port(me_ptr, &(me_ptr->secondary_in_port_info.cmn));
   capi_result |= capi_cmn_init_media_fmt_v2(&(me_ptr->secondary_in_port_info.media_fmt));

   capi_result |= capi_smart_sync_init_cmn_port(me_ptr, &(me_ptr->primary_out_port_info.cmn));
   capi_result |= capi_smart_sync_init_cmn_port(me_ptr, &(me_ptr->secondary_out_port_info.cmn));

   capi_result |= capi_smart_sync_reset_module_state(me_ptr, TRUE);

   capi_result |= capi_smart_sync_raise_event(me_ptr);

   AR_MSG(DBG_HIGH_PRIO, "capi smart sync: Initialization completed");

   return capi_result;
}

/**
 * Assume all incoming timestamps are continuous - cache the first timestamp and extrapolate the rest by
 * the amount of data processed.
 */
static capi_err_t capi_smart_sync_check_set_first_out_timestamp(capi_smart_sync_t *        me_ptr,
                                                                capi_stream_data_t *       input[],
                                                                capi_smart_sync_in_port_t *in_port_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (!me_ptr || !in_port_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: bad args: me_ptr 0x%lx, in port ptr 0x%lx ", me_ptr, in_port_ptr);
      return CAPI_EFAILED;
   }

   if (SMART_SYNC_PORT_INDEX_INVALID == in_port_ptr->cmn.index) // port is not opened.
   {
      return CAPI_EOK;
   }

   if (in_port_ptr->cur_out_buf_timestamp_us || (!input[in_port_ptr->cmn.index]->flags.is_timestamp_valid))
   {
      return capi_result;
   }

   /* For primary input port, update time stamp for the first time. */
   if (capi_smart_sync_input_has_data(input, in_port_ptr->cmn.index))
   {
      /* Update output timestamp, which is used to update the timestamp of actual output buffer timestamp */
      in_port_ptr->cur_out_buf_timestamp_us = input[in_port_ptr->cmn.index]->timestamp;
      in_port_ptr->is_ts_valid              = TRUE;

#ifdef SMART_SYNC_DEBUG_HIGH
      AR_MSG(DBG_MED_PRIO,
             "capi smart sync: input port first buffer, in index %ld, timestamp msw: %lu, lsw: %lu ",
             in_port_ptr->cmn.index,
             (uint32_t)(in_port_ptr->cur_out_buf_timestamp_us >> 32),
             (uint32_t)(in_port_ptr->cur_out_buf_timestamp_us));
#endif
   }
   return capi_result;
}

/**
 * Called at the top of each process call to maintain most recent versions of each shared variable
 * in cached locations.
 */
static capi_err_t capi_smart_sync_check_update_vt_info(capi_smart_sync_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == me_ptr->vfr_timestamp_us_ptr)
   {
#ifdef SMART_SYNC_DEBUG_HIGH
      AR_MSG(DBG_HIGH_PRIO,
             "capi smart sync: process was called while unsubscribed. going ahead with cached shared ptr values.");
#endif

      return capi_result;
   }

   //sync to vfr for first packet.
   if (me_ptr->out_generated_this_vfr_cycle == 0)
   {
     me_ptr->vfr_timestamp_at_cur_proc_tick = *(me_ptr->vfr_timestamp_us_ptr);
   }

   if (NULL != me_ptr->first_vfr_occurred_ptr)
   {
      me_ptr->cached_first_vfr_occurred = *(me_ptr->first_vfr_occurred_ptr);
   }
   return capi_result;
}

/**
 * Smart sync Sync module data process function to process an input buffer
 * and generates an output buffer.
 */
static capi_err_t capi_smart_sync_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t         capi_result = CAPI_EOK;
   capi_smart_sync_t *me_ptr      = (capi_smart_sync_t *)_pif;
   POSAL_ASSERT(me_ptr);

   bool_t PRIMARY_PATH   = TRUE;
   bool_t SECONDARY_PATH = FALSE;

   if (NULL == _pif)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: received bad capi pointer");
      return (CAPI_EFAILED);
   }

   capi_result |= capi_smart_sync_check_update_vt_info(me_ptr);

   me_ptr->primary_in_port_info.is_input_unconsumed   = FALSE;
   me_ptr->secondary_in_port_info.is_input_unconsumed = FALSE;

   bool_t is_eof_propagated = FALSE;

   uint32_t pri_data_len = 0;
   uint32_t sec_data_len = 0;

   bool_t is_primary_valid = (me_ptr->primary_in_port_info.cmn.index != SMART_SYNC_PORT_INDEX_INVALID) ? TRUE : FALSE;
   bool_t is_secondary_valid =
      (me_ptr->secondary_in_port_info.cmn.index != SMART_SYNC_PORT_INDEX_INVALID) ? TRUE : FALSE;

   if (is_primary_valid && input[me_ptr->primary_in_port_info.cmn.index] &&
       input[me_ptr->primary_in_port_info.cmn.index]->buf_ptr)
   {
      pri_data_len = input[me_ptr->primary_in_port_info.cmn.index]->buf_ptr[0].actual_data_len;
   }

   if (is_secondary_valid && input[me_ptr->secondary_in_port_info.cmn.index] &&
       input[me_ptr->secondary_in_port_info.cmn.index]->buf_ptr)
   {
      sec_data_len = input[me_ptr->secondary_in_port_info.cmn.index]->buf_ptr[0].actual_data_len;
   }

#ifdef SMART_SYNC_DEBUG_LOW
   AR_MSG(DBG_HIGH_PRIO,
          "capi smart sync: processing..... pri input len %ld, sec input len %ld",
          pri_data_len,
          sec_data_len);
#endif

   capi_result |= capi_smart_sync_check_set_first_out_timestamp(me_ptr, input, &(me_ptr->primary_in_port_info));
   capi_result |= capi_smart_sync_check_set_first_out_timestamp(me_ptr, input, &(me_ptr->secondary_in_port_info));

   bool_t skip_processing = FALSE;
   bool_t eof_found       = FALSE;
   capi_result |= capi_smart_sync_check_handle_ts_disc(me_ptr, input, output, &skip_processing, &eof_found);

   // Don't pad zeros in the same process call that eof occurred.
   if (!eof_found)
   {
      capi_result |= capi_smart_sync_check_pad_ts_disc_zeros(me_ptr, input, output);
   }

   if (!skip_processing)
   {
      switch (me_ptr->state)
      {
         case SMART_SYNC_STATE_BEFORE_FIRST_PROC_TICK:
         {
            // Don't do any syncing if data flow start for primary port came after proc tick.
            if ((me_ptr->is_proc_tick_notif_rcvd) &&
                (0 == me_ptr->primary_in_port_info.circ_buf.actual_data_len_per_ch))
            {
               AR_MSG(DBG_HIGH_PRIO,
                      "capi smart sync: received proc tic before primary data flow started, clearing proc tick.");

               me_ptr->is_proc_tick_notif_rcvd = FALSE;
            }

            // Buffer (and overrun if necessary) data on primary and secondary paths until the proc tick notification
            // has occurred. Note that first time proc_tick_notif_recvd is true, we might still need to buffer data
            // (that data was buffered by spl_cntr before proc tick).
            if (CAPI_PORT_STATE_STARTED == me_ptr->primary_in_port_info.cmn.state)
            {
#ifdef SMART_SYNC_DEBUG_HIGH
               if (input && input[me_ptr->primary_in_port_info.cmn.index] &&
                   input[me_ptr->primary_in_port_info.cmn.index]->buf_ptr)
               {
                  if (0 != input[me_ptr->primary_in_port_info.cmn.index]->buf_ptr[0].actual_data_len)
                  {
                     AR_MSG(DBG_HIGH_PRIO,
                            "capi smart sync: buffering data for primary port before first proc tick, new data amt: %ld",
                            input[me_ptr->primary_in_port_info.cmn.index]->buf_ptr[0].actual_data_len);
                  }
               }

#endif
               capi_result |= capi_smart_sync_buffer_new_data(me_ptr, input, PRIMARY_PATH, &is_eof_propagated);
            }

            if (CAPI_PORT_STATE_STARTED == me_ptr->secondary_in_port_info.cmn.state)
            {
#ifdef SMART_SYNC_DEBUG_HIGH
               if (input && input[me_ptr->secondary_in_port_info.cmn.index] &&
                   input[me_ptr->secondary_in_port_info.cmn.index]->buf_ptr)
               {

                  if (0 != input[me_ptr->secondary_in_port_info.cmn.index]->buf_ptr[0].actual_data_len)
                  {
                     AR_MSG(DBG_HIGH_PRIO,
                            "capi smart sync: buffering data for secondary port before first proc tick, new data amt: %ld",
                            input[me_ptr->secondary_in_port_info.cmn.index]->buf_ptr[0].actual_data_len);
                  }
               }
#endif
               capi_result |= capi_smart_sync_buffer_new_data(me_ptr, input, SECONDARY_PATH, &is_eof_propagated);
            }

            if ((me_ptr->is_proc_tick_notif_rcvd))
            {
               // The proc tick is our signal to send out whatever data we have, padding zeros up to the proc tick amount.
               capi_result |=
                  capi_smart_sync_pad_initial_zeroes(me_ptr,
                                                     PRIMARY_PATH,
                                                     me_ptr->primary_in_port_info.circ_buf.max_data_len_per_ch);
               capi_result |= capi_smart_sync_output_buffered_data(me_ptr, output, PRIMARY_PATH);

               if (CAPI_PORT_STATE_STARTED == me_ptr->secondary_in_port_info.cmn.state)
               {
                  // If no data came on the secondary port, then only send primary.
                  if (!(capi_smart_sync_is_int_buf_empty(me_ptr, SECONDARY_PATH)))
                  {
                     capi_result |=
                        capi_smart_sync_pad_initial_zeroes(me_ptr,
                                                           SECONDARY_PATH,
                                                           me_ptr->secondary_in_port_info.circ_buf.max_data_len_per_ch);
                     capi_result |= capi_smart_sync_output_buffered_data(me_ptr, output, SECONDARY_PATH);
                  }
                  else
                  {
                     AR_MSG(DBG_HIGH_PRIO,
                            "capi smart sync: No data came on secondary before proc tick, not sending "
                            "anythingon secondary port.");
                  }
               }

               // Underrun on secondary port if we didn't sync secondary yet.
               if (!me_ptr->secondary_in_port_info.zeros_were_padded)
               {
                  capi_result |= capi_smart_sync_underrun_secondary_port(me_ptr, output);
               }

               if (capi_smart_sync_should_move_to_steady_state(me_ptr))
               {
                  AR_MSG(DBG_HIGH_PRIO, "capi smart sync: in steady state now");
                  me_ptr->state = SMART_SYNC_STATE_STEADY_STATE;
                  capi_result |= capi_smart_sync_raise_event_toggle_threshold_n_sync_state(me_ptr, TRUE);
               }
               else
               {
                  AR_MSG(DBG_HIGH_PRIO, "capi smart sync: moving to syncing state");
                  me_ptr->state = SMART_SYNC_STATE_SYNCING;
                  capi_result |= capi_smart_sync_raise_event_toggle_threshold_n_sync_state(me_ptr, FALSE);
               }
            }
            break;
         }
         case SMART_SYNC_STATE_SYNCING:
         {
            bool_t is_first_frame = capi_smart_sync_is_first_frame_of_vfr_cycle(me_ptr);
            // Threshold for first frame is proc samples, otherwise it is the container frame length.
            uint32_t cur_thresh_bytes_per_ch = is_first_frame
                                                  ? me_ptr->primary_in_port_info.circ_buf.max_data_len_per_ch
                                                  : me_ptr->threshold_bytes_per_ch;
            bool_t primary_meets_thresh =
               (me_ptr->primary_in_port_info.circ_buf.actual_data_len_per_ch >= cur_thresh_bytes_per_ch);
            bool_t secondary_meets_thresh =
               (me_ptr->secondary_in_port_info.circ_buf.actual_data_len_per_ch >= cur_thresh_bytes_per_ch);

            // If threshold isn't met yet, buffer data. Otherwise leave input unconsumed.
            if (primary_meets_thresh)
            {
               me_ptr->primary_in_port_info.is_input_unconsumed = TRUE;
            }
            else
            {
#ifdef SMART_SYNC_DEBUG_HIGH
               if (0 != pri_data_len)
               {
                  AR_MSG(DBG_HIGH_PRIO,
                         "capi smart sync: buffering data for primary port in first vfr cycle, new data amt: %ld",
                         pri_data_len);
               }
#endif
               // If zeros are padded then sync is already done for primary port,
               // should not drop data and add discontinuity.
               if (me_ptr->primary_in_port_info.zeros_were_padded)
               {
                  uint32_t in_index = me_ptr->primary_in_port_info.cmn.index;
                  if (is_primary_valid && input && input[in_index] && input[in_index]->buf_ptr)
                  {
                     uint32_t primary_data_needed_bytes_per_ch =
                        cur_thresh_bytes_per_ch - me_ptr->primary_in_port_info.circ_buf.actual_data_len_per_ch;

                     pri_data_len = MIN(primary_data_needed_bytes_per_ch, pri_data_len);

                     AR_MSG(DBG_HIGH_PRIO,
                            "capi smart sync: buffering data for primary port, "
                            "reduced data amt: %lu",
                            pri_data_len);

                     for (uint32_t ch = 0; ch < input[in_index]->bufs_num; ch++)
                     {
                        input[in_index]->buf_ptr[ch].actual_data_len = pri_data_len;
                     }
                  }
               }

               capi_result |= capi_smart_sync_buffer_new_data(me_ptr, input, PRIMARY_PATH, &is_eof_propagated);
            }

            if (CAPI_PORT_STATE_STARTED == me_ptr->secondary_in_port_info.cmn.state)
            {
               if (secondary_meets_thresh)
               {
                  me_ptr->secondary_in_port_info.is_input_unconsumed = TRUE;
               }
               else
               {
                  if (0 != sec_data_len)
                  {
#ifdef SMART_SYNC_DEBUG_HIGH
                     AR_MSG(DBG_HIGH_PRIO,
                            "capi smart sync: buffering data for secondary port in first vfr cycle, new data amt: %ld",
                            sec_data_len);
#endif
                     // Pad zeros such that secondary circular buffer is filled equal to the primary circular buffer.
                     // this aligns to time of arrival.
                     if (capi_smart_sync_is_int_buf_empty(me_ptr, SECONDARY_PATH))
                     {
                        // it is a first frame of secondary.
                        is_first_frame  = TRUE;

                        uint32_t sec_zeros_bpc = me_ptr->primary_in_port_info.circ_buf.actual_data_len_per_ch;

                        // Secondary zeros + secondary data = primary held buffer length (primary data already buffered
                        // at this time).
                        sec_zeros_bpc = (sec_data_len > sec_zeros_bpc) ? 0 : (sec_zeros_bpc - sec_data_len);
                        capi_result |= capi_smart_sync_pad_initial_zeroes(me_ptr, SECONDARY_PATH, sec_zeros_bpc);
                     }
                  }

                  // If it is not a first frame of VFR then need to avoid overflow in circular buffer.
                  // Syncing  happened in the first frame so now can't add discontinuity
                  if (!is_first_frame)
                  {
                     uint32_t in_index = me_ptr->secondary_in_port_info.cmn.index;
                     if (is_secondary_valid && input && input[in_index] && input[in_index]->buf_ptr)
                     {
                        uint32_t secondary_data_needed_bytes_per_ch =
                           cur_thresh_bytes_per_ch - me_ptr->secondary_in_port_info.circ_buf.actual_data_len_per_ch;

                        sec_data_len = MIN(secondary_data_needed_bytes_per_ch, sec_data_len);

                        AR_MSG(DBG_HIGH_PRIO,
                               "capi smart sync: buffering data for secondary port, "
                               "reduced data amt: %lu",
                               sec_data_len);

                        for (uint32_t ch = 0; ch < input[in_index]->bufs_num; ch++)
                        {
                           input[in_index]->buf_ptr[ch].actual_data_len = sec_data_len;
                        }
                     }
                  }

                  capi_result |= capi_smart_sync_buffer_new_data(me_ptr, input, SECONDARY_PATH, &is_eof_propagated);
               }
            }

            if (!me_ptr->can_process)
            {
               AR_MSG(DBG_HIGH_PRIO, "capi smart sync: can't generate output this process.");
               break;
            }
            // Check again if threshold is met, since we have more data now.  Output only if both ports have threshold
            // amount of data.
            primary_meets_thresh =
               (me_ptr->primary_in_port_info.circ_buf.actual_data_len_per_ch >= cur_thresh_bytes_per_ch);
            secondary_meets_thresh =
               (me_ptr->secondary_in_port_info.circ_buf.actual_data_len_per_ch >= cur_thresh_bytes_per_ch);

            // If the secondary port started while in intial buffering state, we need to wait for threshold amount of data
            // on the secondary port. In this case zeros_were_padded is TRUE.
            // Otherwise (zeros_were_padded FALSE cases):
            // 1. Secondary port is stopped, we won't have any data. Just send primary.
            // or
            // 2. Secondary port started after first output was sent. Secondary port gets zeros padded up to primary and
            //    then we wait for enoguh data on both ports.
            //
            // pri_meets_thresh && ((sec_zeros_were_padded == FALSE) || (sec_zeros_were_padded == TRUE &&
            // secondary_meets_thresh))
            // Simplifies to
            // pri_meets_thresh && ((sec_zeros_were_padded == FALSE) || (secondary_meets_thresh))
            //
            // We must also wait for the proc tick before processing. If we finish processing before proc tick is received, then
            // after reaching steady state we may handle the proc tick late. This could happen because SC can process all frames
            // of VFR cycle from data trigger context and does not have logic to clear proc tick while smart sync is syncing.
            if (me_ptr->is_proc_tick_notif_rcvd && (primary_meets_thresh) && (!me_ptr->secondary_in_port_info.zeros_were_padded || secondary_meets_thresh))
            {
               capi_result |= capi_smart_sync_output_buffered_data(me_ptr, output, PRIMARY_PATH);

               // If secondary meets thresh, then send. otherwise underrun.
               if (secondary_meets_thresh)
               {
                  capi_result |= capi_smart_sync_output_buffered_data(me_ptr, output, SECONDARY_PATH);
               }
               else
               {
                  capi_result |= capi_smart_sync_underrun_secondary_port(me_ptr, output);
               }
            }

            /* After first VFR cycle, enable threshold and process in pass through mode*/
            if (capi_smart_sync_should_move_to_steady_state(me_ptr))
            {
               AR_MSG(DBG_HIGH_PRIO, "capi smart sync: in steady state now");
               me_ptr->state = SMART_SYNC_STATE_STEADY_STATE;
               capi_result |= capi_smart_sync_raise_event_toggle_threshold_n_sync_state(me_ptr, TRUE);
            }
            break;
         }

         case SMART_SYNC_STATE_STEADY_STATE:
         {
#ifdef SMART_SYNC_DEBUG_LOW
            AR_MSG(DBG_HIGH_PRIO,
                   "capi smart sync: steady state - passing through data for primary path, len %ld",
                   pri_data_len);
#endif

            capi_result |= capi_smart_sync_pass_through_data(me_ptr, input, output, PRIMARY_PATH, &is_eof_propagated);

#ifdef SMART_SYNC_DEBUG_LOW
            AR_MSG(DBG_HIGH_PRIO,
                   "capi smart sync: steady state - passing through data for secondary pat, len %ld",
                   sec_data_len);
#endif

            capi_result |= capi_smart_sync_pass_through_data(me_ptr, input, output, SECONDARY_PATH, &is_eof_propagated);

            break;
         }

         default:
         {
            AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Invalid state!!!");
            capi_result = CAPI_EFAILED;
            break;
         }
      }
   }

   if (is_primary_valid)
   {
      uint32_t in_index = me_ptr->primary_in_port_info.cmn.index;
      if (input && input[in_index])
      {
         input[in_index]->flags.end_of_frame = FALSE;
         if (me_ptr->primary_in_port_info.is_input_unconsumed)
         {
            uint32_t num_channels = me_ptr->primary_in_port_info.media_fmt.format.num_channels;
            for (uint32_t ch = 0; ch < num_channels; ch++)
            {
               input[in_index]->buf_ptr[ch].actual_data_len = 0;
            }
         }
      }
   }

   if (is_secondary_valid)
   {
      uint32_t in_index = me_ptr->secondary_in_port_info.cmn.index;
      if (input && input[in_index])
      {
         input[in_index]->flags.end_of_frame = FALSE;
         if (NULL != input[me_ptr->secondary_in_port_info.cmn.index]->buf_ptr)
         {
            if (me_ptr->secondary_in_port_info.is_input_unconsumed)
            {
               uint32_t num_channels = me_ptr->secondary_in_port_info.media_fmt.format.num_channels;
               for (uint32_t ch = 0; ch < num_channels; ch++)
               {
                  input[in_index]->buf_ptr[ch].actual_data_len = 0;
               }
            }
         }
      }
   }

   if (me_ptr->out_to_drop_from_next_vfr_cycle)
   {
      uint32_t pri_out_index = me_ptr->primary_out_port_info.cmn.index;
      uint32_t sec_out_index = me_ptr->secondary_out_port_info.cmn.index;
      me_ptr->out_to_drop_from_next_vfr_cycle -=
         MIN(me_ptr->out_to_drop_from_next_vfr_cycle, output[pri_out_index]->buf_ptr[0].actual_data_len);
      for (uint32_t ch = 0; ch < output[pri_out_index]->bufs_num; ch++)
      {
         output[pri_out_index]->buf_ptr[ch].actual_data_len = 0;
         /**
          * TODO: Since metadata is not propagated from input to output during syncing state
          * therefore there is nothing to drop.
          */
      }

      if (SMART_SYNC_PORT_INDEX_INVALID != sec_out_index)
      {
         if (output[sec_out_index] && (NULL != output[sec_out_index]->buf_ptr))
         {
            for (uint32_t ch = 0; ch < output[sec_out_index]->bufs_num; ch++)
            {
               output[sec_out_index]->buf_ptr[ch].actual_data_len = 0;
            }
         }
      }

      AR_MSG(DBG_HIGH_PRIO, "capi smart sync: output buf dropped for resync handling.");
   }

   // Check to update the trigger policy based on whether the vfr cycle is complete or not. If vfr cycle is complete,
   // wait on voice timer, otherwise, be buffer triggered.
   container_trigger_policy_t trigger_policy =
      capi_smart_sync_vfr_cycle_is_complete(me_ptr) ? VOICE_TIMER_TRIGGER : OUTPUT_BUFFER_TRIGGER;

   // We have to continue in disable threshold mode until secondary is synced.
   if (!me_ptr->secondary_in_port_info.zeros_were_padded)
   {
      trigger_policy = OUTPUT_BUFFER_TRIGGER;
   }
   capi_smart_sync_raise_event_change_container_trigger_policy(me_ptr, trigger_policy);

   // Reset output generated at the end of the vfr cycle.
   if (capi_smart_sync_vfr_cycle_is_complete(me_ptr))
   {
      me_ptr->out_generated_this_vfr_cycle = 0;

      // While syncing, clear the proc tic notif at the end of each cycle. Don't output any
      // data until it is set again.
      if (SMART_SYNC_STATE_STEADY_STATE != me_ptr->state)
      {
         me_ptr->is_proc_tick_notif_rcvd = FALSE;
      }
   }

   if (is_eof_propagated)
   {
      AR_MSG(DBG_HIGH_PRIO, "capi smart sync: EOF propagated from an output port, resyncing!.");
      capi_smart_sync_resync_module_state(me_ptr);
   }

   return capi_result;
}

/**
 * Smart Sync end function, returns the library to the uninitialized
 * state and frees all the memory that was allocated. This function also
 * frees the virtual function table.
 */
static capi_err_t capi_smart_sync_end(capi_t *_pif)
{

   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: End received bad pointer, 0x%p", _pif);
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_smart_sync_t *me_ptr = (capi_smart_sync_t *)_pif;

   capi_smart_sync_deallocate_internal_circ_buf(me_ptr, &(me_ptr->primary_in_port_info));

   capi_smart_sync_deallocate_internal_circ_buf(me_ptr, &(me_ptr->secondary_in_port_info));

   capi_smart_sync_unsubscribe_to_voice_timer(me_ptr);

   me_ptr->vtbl.vtbl_ptr = NULL;

   AR_MSG(DBG_HIGH_PRIO, "capi smart sync: End done");

   return capi_result;
}

static capi_err_t capi_smart_sync_set_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_smart_sync_t *me_ptr = (capi_smart_sync_t *)_pif;

   return capi_smart_sync_process_set_properties(me_ptr, props_ptr);
}

/**
 * Function to get the properties of Smart Sync module
 */
static capi_err_t capi_smart_sync_get_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_smart_sync_t *me_ptr = (capi_smart_sync_t *)_pif;

   return capi_smart_sync_process_get_properties(me_ptr, props_ptr);
}

/**
 * Sets either a parameter value or a parameter structure containing
 * multiple parameters. In the event of a failure, the appropriate error
 * code is returned.
 */
static capi_err_t capi_smart_sync_set_param(capi_t *                _pif,
                                            uint32_t                param_id,
                                            const capi_port_info_t *port_info_ptr,
                                            capi_buf_t *            params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: set param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
   }
   capi_smart_sync_t *me_ptr = (capi_smart_sync_t *)(_pif);

   switch (param_id)
   {
      case PARAM_ID_SMART_SYNC_VOICE_PROC_INFO:
      {
         if (params_ptr->actual_data_len != sizeof(smart_sync_voice_proc_info_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi smart sync: Param id 0x%lx Bad param size %lu",
                   (uint32_t)param_id,
                   params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }

         smart_sync_voice_proc_info_t *voice_proc_info_param_ptr = (smart_sync_voice_proc_info_t *)params_ptr->data_ptr;

         AR_MSG(DBG_HIGH_PRIO,
                "capi smart sync: Received Set param of voice proc info,  "
                "voice proc start offset = %ld us, "
                "voice proc start samples = %ld us, "
                "vfr cycle duration = %ld ms, "
                "path delay = %lu us",
                voice_proc_info_param_ptr->voice_proc_start_offset_us,
                voice_proc_info_param_ptr->voice_proc_start_samples_us,
                voice_proc_info_param_ptr->vfr_cycle_duration_ms,
                voice_proc_info_param_ptr->path_delay_us);

         /* Unsubscribe if already subscribed */
         if ((me_ptr->vfr_timestamp_us_ptr) || (FALSE == voice_proc_info_param_ptr->is_subscribe))
         {
            capi_smart_sync_unsubscribe_to_voice_timer(me_ptr);
         }

         memscpy(&(me_ptr->voice_proc_info),
                 sizeof(smart_sync_voice_proc_info_t),
                 voice_proc_info_param_ptr,
                 params_ptr->actual_data_len);

         if (FALSE == voice_proc_info_param_ptr->is_subscribe)
         {
            break;
         }

         /* Register with voice timer if not registered already */
         if (capi_smart_sync_can_subscribe_to_vt(me_ptr))
         {
            capi_result = capi_smart_sync_subscribe_to_voice_timer(me_ptr);
         }

         if (capi_smart_sync_media_fmt_is_valid(me_ptr, TRUE))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi smart sync: num channels: %lu",
                   me_ptr->primary_in_port_info.media_fmt.format.num_channels);
            capi_result = capi_smart_sync_allocate_internal_circ_buf(me_ptr, &(me_ptr->primary_in_port_info));
            me_ptr->out_required_per_vfr_cycle =
               capi_cmn_us_to_bytes_per_ch(me_ptr->voice_proc_info.vfr_cycle_duration_ms * 1000,
                                           me_ptr->primary_in_port_info.media_fmt.format.sampling_rate,
                                           me_ptr->primary_in_port_info.media_fmt.format.bits_per_sample);
         }

         /* TODO(CG): check if secondary port has started  */
         if (capi_smart_sync_media_fmt_is_valid(me_ptr, FALSE))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi smart sync: num channels: %lu",
                   me_ptr->secondary_in_port_info.media_fmt.format.num_channels);
            capi_result = capi_smart_sync_allocate_internal_circ_buf(me_ptr, &(me_ptr->secondary_in_port_info));
         }

         if(SMART_SYNC_STATE_BEFORE_FIRST_PROC_TICK != me_ptr->state)
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "capi smart sync: going back to sync since voice timing was sent during state 0x%lx",
                   me_ptr->state);

            // Why resync instead of reset? If voice proc params come halfway through vfr cycle, we still
            // need to maintain the correct amount of data sent downstream per vfr cycle. Resync logic will
            // drop some data so that old partial voice frame will get combined with new partial voice frame.
            capi_smart_sync_resync_module_state(me_ptr);
         }

         break;
      }
      case FWK_EXTN_PARAM_ID_CONTAINER_FRAME_DURATION:
      {
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_container_frame_duration_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi smart sync: Param id 0x%lx Bad param size %lu",
                   (uint32_t)param_id,
                   params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }

         fwk_extn_param_id_container_frame_duration_t *frame_len_param_ptr =
            (fwk_extn_param_id_container_frame_duration_t *)params_ptr->data_ptr;

         AR_MSG(DBG_HIGH_PRIO,
                "capi smart sync: Received Set param of fwk input threshold us = %ld",
                frame_len_param_ptr->duration_us);

         // Set threshold according to new amount.
         if (me_ptr->threshold_us != frame_len_param_ptr->duration_us)
         {
            me_ptr->threshold_us = frame_len_param_ptr->duration_us;
            if (capi_smart_sync_media_fmt_is_valid(me_ptr, TRUE))
            {
               me_ptr->threshold_bytes_per_ch =
                  capi_cmn_us_to_bytes_per_ch(me_ptr->threshold_us,
                                              me_ptr->primary_in_port_info.media_fmt.format.sampling_rate,
                                              me_ptr->primary_in_port_info.media_fmt.format.bits_per_sample);

               AR_MSG(DBG_HIGH_PRIO,
                      "capi smart sync: threshold_bytes_per_ch is now %ld",
                      me_ptr->threshold_bytes_per_ch);
            }
         }
         break;
      }

      case FWK_EXTN_VOICE_DELIVERY_PARAM_ID_FIRST_PROC_TICK_NOTIF:
      {
         AR_MSG(DBG_HIGH_PRIO, "Received proc tick notification");
         me_ptr->is_proc_tick_notif_rcvd = TRUE;
         break;
      }

      case FWK_EXTN_VOICE_DELIVERY_PARAM_ID_TOPO_PROCESS_NOTIF:
      {
         AR_MSG(DBG_HIGH_PRIO, "Received topo proc notification");
         me_ptr->can_process = TRUE;

         /* following logic is to ensure that sync at first proc tick is not delayed due to scheduling jitter.*/
         if (me_ptr->is_proc_tick_notif_rcvd && capi_smart_sync_is_first_frame_of_vfr_cycle(me_ptr) &&
             (me_ptr->vfr_timestamp_us_ptr))
         {
            uint64_t curr_time = posal_timer_get_time();
            uint64_t exp_proc_tick_time =
               *(me_ptr->vfr_timestamp_us_ptr) + me_ptr->voice_proc_info.voice_proc_start_offset_us;

            int64_t jitter = curr_time - exp_proc_tick_time;

            AR_MSG(DBG_MED_PRIO,
                   "vptx scheduling for first proc tick. Expected time lsb %lu, current time lsb "
                   "%lu, jitter %ld",
                   (uint32_t)exp_proc_tick_time,
                   (uint32_t)curr_time,
                   (int32_t)jitter);

            if (jitter >= SMART_SYNC_FIRST_PROC_TICK_SCHEDULING_JITTER_THRESHOLD_US) // if scheduling is delayed by more than 1.5ms then discard proc tick
            {
               //Resetting to avoid any internal buffering (in module and in topo).
               //Internal buffering can cause packet alignment issues
               AR_MSG(DBG_ERROR_PRIO, "vptx scheduling is delayed, ignorning proc tick.");
               capi_smart_sync_resync_module_state(me_ptr);
            }
         }

         break;
      }

      case FWK_EXTN_VOICE_DELIVERY_PARAM_ID_RESYNC_NOTIF:
      {
         AR_MSG(DBG_HIGH_PRIO, "Received VFR resync notification");
         if ((NULL != me_ptr->resync_status_ptr) && *(me_ptr->resync_status_ptr))
         {
            /* If resync occurred, update the module state to buffering and reset the necessary variables.
             * And disable the threshold for proc samples buffering */
            capi_smart_sync_resync_module_state(me_ptr);
            *(me_ptr->resync_status_ptr) = FALSE;
         }
         break;
      }

      case FWK_EXTN_VOICE_DELIVERY_PARAM_ID_DATA_DROP_DURING_SYNC:
      {
         if(SMART_SYNC_STATE_BEFORE_FIRST_PROC_TICK != me_ptr->state)
         {
            AR_MSG(DBG_HIGH_PRIO, "Received data drop during sync, doing resync behavior.");

            // Data in buffer is valid still, so don't drop any buffered data.
            capi_smart_sync_resync_module_state(me_ptr);
         }
         else
         {
            AR_MSG(DBG_HIGH_PRIO, "Received data drop during sync, before first proc tick - ignoring.");
         }

         break;
      }

      case INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION:
      {
         capi_result |= capi_smart_sync_set_properties_port_op(me_ptr, params_ptr);
         break;
      }
      case INTF_EXTN_PARAM_ID_METADATA_HANDLER:
      {
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_metadata_handler_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi smart sync: Param id 0x%lx Bad param size %lu",
                   (uint32_t)param_id,
                   params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
         intf_extn_param_id_metadata_handler_t *payload_ptr =
            (intf_extn_param_id_metadata_handler_t *)params_ptr->data_ptr;
         me_ptr->metadata_handler = *payload_ptr;
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "capi smart sync Set, unsupported param ID 0x%x", (int)param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
      }
   }
   return capi_result;
}

/**
 * Gets either a parameter value or a parameter structure containing
 * multiple parameters. In the event of a failure, the appropriate error
 * code is returned.
 */
static capi_err_t capi_smart_sync_get_param(capi_t *                _pif,
                                            uint32_t                param_id,
                                            const capi_port_info_t *port_info_ptr,
                                            capi_buf_t *            params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == _pif || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Get param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_EBADPARAM;
   }

   switch (param_id)
   {
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "capi smart sync get, unsupported param ID 0x%x", (int)param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
      }
   }

   return capi_result;
}

static capi_err_t capi_smart_sync_process_set_properties(capi_smart_sync_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == me_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync : Set common property received null ptr");
      return CAPI_EBADPARAM;
   }

   capi_result = capi_cmn_set_basic_properties(proplist_ptr, &me_ptr->heap_info, &me_ptr->cb_info, NULL);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Set basic properties failed with result %lu", capi_result);
      return capi_result;
   }

   capi_prop_t *prop_array = proplist_ptr->prop_ptr;
   uint32_t     i          = 0;

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &(prop_array[i].payload);

      switch (prop_array[i].id)
      {
         case CAPI_EVENT_CALLBACK_INFO:
         case CAPI_HEAP_ID:
         case CAPI_ALGORITHMIC_RESET:
         case CAPI_CUSTOM_INIT_DATA:
         {
            break;
         }
         case CAPI_PORT_NUM_INFO:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_port_num_info_t))
            {
               capi_port_num_info_t *data_ptr = (capi_port_num_info_t *)payload_ptr->data_ptr;

               if ((data_ptr->num_input_ports > SMART_SYNC_MAX_IN_PORTS) ||
                   (data_ptr->num_output_ports > SMART_SYNC_MAX_OUT_PORTS))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi smart sync: Set property id 0x%lx number of input and output ports cannot be more "
                         "than 1",
                         (uint32_t)prop_array[i].id);
                  CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
                  break;
               }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi smart sync: Set, Param id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break;
            }
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            if (sizeof(capi_media_fmt_v2_t) <= payload_ptr->actual_data_len)
            {
               // For output media format event.
               bool_t IS_INPUT_PORT = FALSE;

               capi_media_fmt_v2_t *      data_ptr    = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);
               capi_smart_sync_in_port_t *in_port_ptr = NULL;

               if (!capi_smart_sync_is_supported_media_type(data_ptr))
               {
                  CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
                  break;
               }

               if (!prop_array[i].port_info.is_valid)
               {
                  AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Media format port info is invalid");
                  CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
                  break;
               }

               // Get pointer to media format for this port (get primary/secondary from id/idx mapping).
               bool_t is_primary = FALSE;

               if (me_ptr->primary_in_port_info.cmn.index == prop_array[i].port_info.port_index)
               {
                  AR_MSG(DBG_HIGH_PRIO, "capi smart sync: Received Input media format on primary input");
                  is_primary  = TRUE;
                  in_port_ptr = &me_ptr->primary_in_port_info;
               }
               else if (me_ptr->secondary_in_port_info.cmn.index == prop_array[i].port_info.port_index)
               {
                  AR_MSG(DBG_HIGH_PRIO, "capi smart sync: Received Input media format on secondary input");
                  is_primary  = FALSE;
                  in_port_ptr = &me_ptr->secondary_in_port_info;
               }
               else
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi smart sync: Media format port info for non-primary and non-secondary port idx %ld",
                         prop_array[i].port_info.port_index);
                  CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
                  return capi_result;
               }

               // If new media format is same as old, then there's no necessary handling.
               if (capi_cmn_media_fmt_equal(data_ptr, &(in_port_ptr->media_fmt)))
               {
                  break;
               }

               // Copy and save the input media fmt.
               in_port_ptr->media_fmt = *data_ptr; // Copy.

               // If proc samples has been configured, recalculate bytes value since that depends on media format,
               // and reallocate buffers as size has changed.
               //
               // We only allocate/calculate circ buf size when the primary port received input media format, since
               // everywhere
               // we are using primary port media format as reference media format.
               if (me_ptr->voice_proc_info.voice_proc_start_samples_us)
               {
                  capi_result = capi_smart_sync_allocate_internal_circ_buf(me_ptr, in_port_ptr);
                  me_ptr->out_required_per_vfr_cycle =
                     capi_cmn_us_to_bytes_per_ch(me_ptr->voice_proc_info.vfr_cycle_duration_ms * 1000,
                                                 me_ptr->primary_in_port_info.media_fmt.format.sampling_rate,
                                                 me_ptr->primary_in_port_info.media_fmt.format.bits_per_sample);
               }

               /* If threshold is received and threshold bytes not calculated, calculate the threshold bytes now.
                * This occurs when threshold is received before input media format */
               if (me_ptr->threshold_us)
               {
                  me_ptr->threshold_bytes_per_ch =
                     capi_cmn_us_to_bytes_per_ch(me_ptr->threshold_us,
                                                 me_ptr->primary_in_port_info.media_fmt.format.sampling_rate,
                                                 me_ptr->primary_in_port_info.media_fmt.format.bits_per_sample);

                  AR_MSG(DBG_HIGH_PRIO,
                         "capi smart sync: threshold_bytes_per_ch is now %ld",
                         me_ptr->threshold_bytes_per_ch);
               }

               // Raise event for output media format.
               AR_MSG(DBG_HIGH_PRIO, "capi smart sync: Setting media format on port is_primary %ld", is_primary);

               capi_result |= capi_smart_sync_raise_event(me_ptr);

               uint32_t raise_index =
                  is_primary ? me_ptr->primary_out_port_info.cmn.index : me_ptr->secondary_out_port_info.cmn.index;
               capi_result |= capi_cmn_output_media_fmt_event_v2(&me_ptr->cb_info,
                                                                 &(in_port_ptr->media_fmt),
                                                                 IS_INPUT_PORT,
                                                                 raise_index);

               { // smart sync is raising threshold in case if EC module is not present in the topology.
                  uint32_t port_threshold_bytes = capi_cmn_us_to_bytes(10000, // 10ms
                                                                       in_port_ptr->media_fmt.format.sampling_rate,
                                                                       in_port_ptr->media_fmt.format.bits_per_sample,
                                                                       in_port_ptr->media_fmt.format.num_channels);

                  capi_cmn_update_port_data_threshold_event(&me_ptr->cb_info,
                                                            port_threshold_bytes,
                                                            TRUE, // input port
                                                            in_port_ptr->cmn.index);
                  capi_cmn_update_port_data_threshold_event(&me_ptr->cb_info,
                                                            port_threshold_bytes,
                                                            FALSE, // output port
                                                            raise_index);
               }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi smart sync: Set property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break;
            }
            break;
         }
         case CAPI_CUSTOM_PROPERTY:
         {
            capi_custom_property_t *cust_prop_ptr    = (capi_custom_property_t *)(payload_ptr->data_ptr);
            void *                  cust_payload_ptr = (void *)(cust_prop_ptr + 1);

            switch (cust_prop_ptr->secondary_prop_id)
            {
               case FWK_EXTN_PROPERTY_ID_VOICE_PROC_START_TRIGGER:
               {
                  if (payload_ptr->actual_data_len <
                      sizeof(capi_custom_property_t) + sizeof(capi_prop_voice_proc_start_trigger_t))
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "capi smart sync: Insufficient payload size for voice proc start trigger %d",
                            payload_ptr->actual_data_len);
                     return CAPI_EBADPARAM;
                  }

                  // Get end point info
                  capi_prop_voice_proc_start_trigger_t *trig_ptr =
                     (capi_prop_voice_proc_start_trigger_t *)cust_payload_ptr;
                  me_ptr->voice_proc_start_signal_ptr = trig_ptr->proc_start_signal_ptr;
                  me_ptr->voice_resync_signal_ptr     = trig_ptr->resync_signal_ptr;

                  AR_MSG(DBG_HIGH_PRIO, "capi smart sync: Received custom property for voice proc start trigger");

                  /* Register with voice timer if not registered already */
                  if (capi_smart_sync_can_subscribe_to_vt(me_ptr))
                  {
                     capi_result = capi_smart_sync_subscribe_to_voice_timer(me_ptr);
                  }

                  /* Disable threshold for proc sample buffering */
                  capi_result |= capi_smart_sync_raise_event_toggle_threshold_n_sync_state(me_ptr, FALSE);

                  break;
               }
               default:
               {
                  AR_MSG(DBG_HIGH_PRIO,
                         "capi smart sync: Unknown Custom Property[%d]",
                         cust_prop_ptr->secondary_prop_id);
                  capi_result |= CAPI_EUNSUPPORTED;
                  break;
               }
            }
            break;
         }
         case CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT_V2:
         {
            /* Validate the payload_ptr */
            if (payload_ptr->actual_data_len < sizeof(capi_register_event_to_dsp_client_v2_t))
            {
               AR_MSG_ISLAND(DBG_ERROR_PRIO, "Invalid payload size %d", payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
               break;
            }

            capi_register_event_to_dsp_client_v2_t *reg_event_ptr =
               (capi_register_event_to_dsp_client_v2_t *)(payload_ptr->data_ptr);

            AR_MSG_ISLAND(DBG_HIGH_PRIO,
                          "Received CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT_V2 is_reg%lu event ID 0x%lx addr 0x%lx "
                          "token 0x%lx",
                          reg_event_ptr->is_register,
                          reg_event_ptr->event_id,
                          reg_event_ptr->dest_address,
                          reg_event_ptr->token);

            switch (reg_event_ptr->event_id)
            {
               case FWK_EXTN_VOICE_DELIVERY_EVENT_ID_UPDATE_SYNC_STATE:
               {

                  if (reg_event_ptr->is_register)
                  {
                     // store the client details
                     if (0 == me_ptr->evt_dest_address)
                     {
                        // empty slot => store
                        me_ptr->evt_dest_address = reg_event_ptr->dest_address;
                        me_ptr->evt_dest_token   = reg_event_ptr->token;
                     }
                     else
                     {
                        AR_MSG_ISLAND(DBG_ERROR_PRIO,
                                      "Failed! Duplicate Registration for smart sync Event from the client ",
                                       reg_event_ptr->dest_address);
                        return CAPI_EFAILED;
                     }
                  }
                  else // deregister
                  {
                     // search for the client in the slots
                     if (reg_event_ptr->dest_address == me_ptr->evt_dest_address)
                     {
                        me_ptr->evt_dest_address   = 0;
                        me_ptr->evt_dest_token     = 0;
                        break; // out of the loop
                     }
                     else
                     {
                        AR_MSG_ISLAND(DBG_ERROR_PRIO,
                               "Couldn't find client with address 0x%llx in the reg list to deregister. Failing",
                               reg_event_ptr->dest_address);
                        return CAPI_EFAILED;
                     }
                  }
                  break;
               }
               default:
               {
                  AR_MSG_ISLAND(DBG_ERROR_PRIO, "Unsupported event ID[%d]", reg_event_ptr->event_id);
                  capi_result |= CAPI_EUNSUPPORTED;
                  break;
               }
            } // reg event id switch
            break;
         } // CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT_V2

         case CAPI_MODULE_INSTANCE_ID:

            break;
         case CAPI_LOGGING_INFO:

            break;
         default:
         {
#ifdef SMART_SYNC_DEBUG_HIGH
            AR_MSG(DBG_HIGH_PRIO, "capi smart sync: Set property id %#x. Not supported.", prop_array[i].id);
#endif
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }
      }
      if (CAPI_FAILED(capi_result))
      {
         AR_MSG(DBG_HIGH_PRIO,
                "capi smart sync: Set property for %#x failed with opcode %lu",
                prop_array[i].id,
                capi_result);
      }
   }

   return capi_result;
}

static capi_err_t capi_smart_sync_process_get_properties(capi_smart_sync_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   uint32_t fwk_extn_ids[SMART_SYNC_NUM_FRAMEWORK_EXTENSIONS] = { FWK_EXTN_SYNC,
                                                                  FWK_EXTN_VOICE_DELIVERY,
                                                                  FWK_EXTN_CONTAINER_FRAME_DURATION };

   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req    = align_to_8_byte(sizeof(capi_smart_sync_t));
   mod_prop.stack_size         = SMART_SYNC_STACK_SIZE;
   mod_prop.num_fwk_extns      = SMART_SYNC_NUM_FRAMEWORK_EXTENSIONS;
   mod_prop.fwk_extn_ids_arr   = fwk_extn_ids;
   mod_prop.is_inplace         = FALSE;
   mod_prop.req_data_buffering = TRUE;
   mod_prop.max_metadata_size  = 0;

   capi_result = capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Get common basic properties failed with result %lu", capi_result);
      return capi_result;
   }

   capi_prop_t *prop_array = proplist_ptr->prop_ptr;

   for (uint32_t i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_array[i].payload;
      switch (prop_array[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_STACK_SIZE:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         {
            // handled in capi common utils.
            break;
         }

         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi smart sync : null ptr while querying output mf");
               return CAPI_EBADPARAM;
            }

            // For mulitport modules, output media format querry must have a valid port info.
            if (!(prop_array[i].port_info.is_valid))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi smart sync : getting output mf on multiport module without specifying output port!");
               return CAPI_EBADPARAM;
            }

            if (prop_array[i].port_info.is_input_port)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi smart sync : can't get output mf of input port!");
               return CAPI_EBADPARAM;
            }

            // Check a correct port index was sent. This is just for validation purposes.
            uint32_t querried_port_index = prop_array[i].port_info.port_index;
            bool_t   is_primary          = FALSE;
            if (me_ptr->primary_out_port_info.cmn.index == querried_port_index)
            {
               is_primary = TRUE;
            }
            else if (me_ptr->secondary_out_port_info.cmn.index == querried_port_index)
            {
               is_primary = FALSE;
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO, "capi smart sync get: index not assigned to an output port!");
               return CAPI_EBADPARAM;
            }

            /* Validate the MF payload */
            capi_smart_sync_in_port_t *in_port_ptr =
               is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;
            uint32_t req_size = capi_cmn_media_fmt_v2_required_size(in_port_ptr->media_fmt.format.num_channels);
            if (payload_ptr->max_data_len < req_size)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi smart sync : Not enough space for get output media format v2, size %d",
                      payload_ptr->max_data_len);

               capi_result |= CAPI_ENEEDMORE;
               break;
            }

            // Copy proper media format to payload.
            capi_media_fmt_v2_t *media_fmt_ptr = &(in_port_ptr->media_fmt);
            memscpy(payload_ptr->data_ptr, payload_ptr->max_data_len, media_fmt_ptr, req_size);
            payload_ptr->actual_data_len = req_size;
            break;
         }
         case CAPI_PORT_DATA_THRESHOLD:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi smart sync: null ptr while querying threshold");
               return CAPI_EBADPARAM;
            }
            if (prop_array[i].payload.max_data_len >= sizeof(capi_port_data_threshold_t))
            {
               capi_port_data_threshold_t *data_ptr = (capi_port_data_threshold_t *)payload_ptr->data_ptr;
               if (!prop_array[i].port_info.is_valid)
               {
                  AR_MSG(DBG_ERROR_PRIO, "capi smart sync get: port threshold port id not valid");
                  capi_result |= CAPI_EBADPARAM;
                  break;
               }

               capi_media_fmt_v2_t *mf_ptr = NULL;
               if (prop_array[i].port_info.is_input_port)
               {
                  if (me_ptr->primary_in_port_info.cmn.index == prop_array[i].port_info.port_index)
                  {
                     mf_ptr = &me_ptr->primary_in_port_info.media_fmt;
                  }
                  else if (me_ptr->secondary_in_port_info.cmn.index == prop_array[i].port_info.port_index)
                  {
                     mf_ptr = &me_ptr->secondary_in_port_info.media_fmt;
                  }
                  else
                  {
                     AR_MSG(DBG_ERROR_PRIO, "capi smart sync get: input port threshold port index is not valid");
                     capi_result |= CAPI_EBADPARAM;
                     break;
                  }
               }
               else
               {
                  if (me_ptr->primary_out_port_info.cmn.index == prop_array[i].port_info.port_index)
                  {
                     mf_ptr = &me_ptr->primary_in_port_info.media_fmt;
                  }
                  else if (me_ptr->secondary_out_port_info.cmn.index == prop_array[i].port_info.port_index)
                  {
                     mf_ptr = &me_ptr->secondary_in_port_info.media_fmt;
                  }
                  else
                  {
                     AR_MSG(DBG_ERROR_PRIO, "capi smart sync get: output port threshold port index is not valid");
                     capi_result |= CAPI_EBADPARAM;
                     break;
                  }
               }

               uint32_t port_threshold_bytes = 1;
               if (mf_ptr->format.sampling_rate != CAPI_DATA_FORMAT_INVALID_VAL) // if media format is valid
               {
                  port_threshold_bytes = capi_cmn_us_to_bytes(10000, // 10ms
                                                              mf_ptr->format.sampling_rate,
                                                              mf_ptr->format.bits_per_sample,
                                                              mf_ptr->format.num_channels);
               }
               data_ptr->threshold_in_bytes = port_threshold_bytes;
               payload_ptr->actual_data_len = sizeof(capi_port_data_threshold_t);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi smart sync : Not enough space for get port threshold, size %d",
                      payload_ptr->max_data_len);

               capi_result |= CAPI_ENEEDMORE;
               break;
            }
            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         {
            capi_interface_extns_list_t *intf_ext_list = (capi_interface_extns_list_t *)payload_ptr->data_ptr;
            capi_result |=
               ((payload_ptr->max_data_len < sizeof(capi_interface_extns_list_t)) ||
                (payload_ptr->max_data_len < (sizeof(capi_interface_extns_list_t) +
                                              (intf_ext_list->num_extensions * sizeof(capi_interface_extn_desc_t)))))
                  ? CAPI_ENEEDMORE
                  : capi_result;

            if (CAPI_FAILED(capi_result))
            {
               payload_ptr->actual_data_len = 0;
               AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Insufficient get property size.");
               break;
            }

            capi_interface_extn_desc_t *curr_intf_extn_desc_ptr =
               (capi_interface_extn_desc_t *)(payload_ptr->data_ptr + sizeof(capi_interface_extns_list_t));

            for (uint32_t j = 0; j < intf_ext_list->num_extensions; j++)
            {
               switch (curr_intf_extn_desc_ptr->id)
               {
                  case INTF_EXTN_DATA_PORT_OPERATION:
                  {
                     curr_intf_extn_desc_ptr->is_supported = TRUE;
                     break;
                  }
                  case INTF_EXTN_METADATA:
                  {
                     curr_intf_extn_desc_ptr->is_supported = TRUE;
                     break;
                  }
                  default:
                  {
                     curr_intf_extn_desc_ptr->is_supported = FALSE;
                     break;
                  }
               }
               curr_intf_extn_desc_ptr++;
            }

            break;
         } // CAPI_INTERFACE_EXTENSIONS
         default:
         {
#ifdef SMART_SYNC_DEBUG_HIGH
            AR_MSG(DBG_HIGH_PRIO, "capi smart sync: Get property for ID %#x. Not supported.", prop_array[i].id);
#endif
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }
      }
      if (CAPI_FAILED(capi_result) && (CAPI_EUNSUPPORTED != capi_result))
      {
         AR_MSG(DBG_HIGH_PRIO,
                "capi smart sync: Get property for %#x failed with opcode %lu",
                prop_array[i].id,
                capi_result);
      }
   }
   return capi_result;
}
