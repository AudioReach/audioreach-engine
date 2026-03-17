/*==============================================================================
 @file capi_rat.c
 This file contains capi functions for Rate Adapted Timer Endpoint module

 ================================================================================
 Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 SPDX-License-Identifier: BSD-3-Clause-Clear
 ==============================================================================*/
// clang-format off
// clang-format on
/*=====================================================================
 Includes
 ======================================================================*/

#include "capi_siso_rat.h"
#include "capi_mimo_rat.h"
#include "capi_rat_i.h"

static void capi_rat_process_metadata(capi_rat_t *        me_ptr,
                                      capi_stream_data_t *input[],
                                      capi_stream_data_t *output[],
                                      uint32_t            input_port_index,
                                      uint32_t            output_port_index,
                                      uint32_t            bytes_processed,
                                      uint32_t            initial_input_len,
                                      bool_t              has_output);

static void capi_rat_track_inserted_silence_status(capi_rat_t *           me_ptr,
                                                   capi_stream_data_v2_t *out_stream_ptr,
                                                   capi_rat_out_port_t *  out_port_info_ptr,
                                                   capi_rat_in_port_t *   in_port_info_ptr,
                                                   uint32_t               bytes_processed,
                                                   bool_t                 is_input_ready);

static void capi_rat_check_print_underrun_per_port(capi_cmn_underrun_info_t *underrun_info_ptr,
                                                   uint32_t                  iid,
                                                   uint32_t                  port_id,
                                                   uint32_t                  bytes,
                                                   bool_t                    need_to_reduce_underrun_print);

static capi_err_t capi_rat_calc_drift_set_timer(capi_rat_t *me_ptr);

static bool_t capi_rat_check_data_flow_state(capi_stream_data_t * input,
                                             bool_t               is_capi_in_media_fmt_set,
                                             rat_data_flow_state *data_flow_state_ptr);

static capi_vtbl_t vtbl = { capi_rat_process,        capi_rat_end,           capi_rat_set_param, capi_rat_get_param,
                            capi_rat_set_properties, capi_rat_get_properties };

capi_vtbl_t *capi_rat_get_vtbl()
{
   return &vtbl;
}

/*---------------------------------------------------------------------
 Function name: capi_rat_process
 DESCRIPTION: Processes an input buffer and generates an output buffer.
 -----------------------------------------------------------------------*/
capi_err_t capi_rat_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t  result     = CAPI_EOK;
   capi_rat_t *me_ptr     = (capi_rat_t *)_pif;
   bool_t      has_output = FALSE, is_input_ready = FALSE;

   if (FALSE == me_ptr->configured_media_fmt_received)
   {
      RAT_MSG_ISLAND(me_ptr->iid, DBG_ERROR_PRIO, "CAPI_RAT: Operating media format not set, cannot process");
      return CAPI_EFAILED;
   }

   /* Calculate drift and set the timer accordingly*/
   result = capi_rat_calc_drift_set_timer(me_ptr);

   // Output closed cases, loop over input and drop data
   capi_rat_process_md_with_no_output(me_ptr, input);

   // Looping over all outputs, return here if there is no output or no out port ptr struct created
   if (!((output) && (me_ptr->out_port_info_ptr)))
   {
      return result;
   }

   // Initialize the output actual data len to zero
   for (uint32_t i = 0; i < me_ptr->num_out_ports; i++)
   {
      if (NULL == output[i])
      {
         continue;
      }
      output[i]->buf_ptr[0].actual_data_len = 0;
   }

   for (uint32_t out_port_index = 0; out_port_index < me_ptr->num_out_ports; out_port_index++)
   {
      uint32_t bytes_to_copy = 0, bytes_copied = 0;

      // Check if this output port idx is actually invalid, if yes, it is already handled in
      // capi_rat_process_md_with_no_output()
      if (RAT_PORT_INDEX_INVALID == me_ptr->out_port_info_ptr[out_port_index].cmn.self_index)
      {
#ifdef RAT_DEBUG
         RAT_MSG_ISLAND(me_ptr->iid,
                        DBG_HIGH_PRIO,
                        "CAPI_RAT: out_port_index 0x%lx is not opened, dropping data at input",
                        out_port_index);
#endif
         continue;
      }

      // Derive corresponding input port index from out struct
      capi_rat_out_port_t *rat_out_port_ptr = &me_ptr->out_port_info_ptr[out_port_index];
      uint32_t             in_port_index    = rat_out_port_ptr->cmn.conn_index;

      // Computed each process call. Input is ready if:
      // 1. Input port idx is valid
      // 2. Input port is opened, started and media format is received on this port
      // 3. Data is present on this port
      is_input_ready = ((RAT_PORT_INDEX_INVALID != in_port_index) && (me_ptr->in_port_info_ptr) &&
                        (DATA_PORT_STATE_STARTED == me_ptr->in_port_info_ptr[in_port_index].cmn.port_state) &&
                        (me_ptr->in_port_info_ptr[in_port_index].inp_mf_received) && (input[in_port_index]) &&
                        (input[in_port_index]->buf_ptr) && (input[in_port_index]->buf_ptr[0].data_ptr) &&
                        (input[in_port_index]->buf_ptr[0].actual_data_len));

      // Due to fwk optimization, invoke process function only when first channel data pointer is non NULL.
      // This flag is set if
      // 1. Output port is opened
      // 2. Buffer is present on this output
      has_output = ((output[out_port_index]) && (output[out_port_index]->buf_ptr) &&
                    (output[out_port_index]->buf_ptr[0].data_ptr) &&
                    (DATA_PORT_STATE_STARTED == me_ptr->out_port_info_ptr[out_port_index].cmn.port_state));

#ifdef RAT_DEBUG
      bool_t inp_mf_received = FALSE;
      if (me_ptr->in_port_info_ptr)
      {
         inp_mf_received = me_ptr->in_port_info_ptr[in_port_index].inp_mf_received;
      }
      RAT_MSG_ISLAND(me_ptr->iid,
                     DBG_HIGH_PRIO,
                     "CAPI_RAT: Enter process: in_port_index 0x%lx, out_port_index 0x%lx, inp ready %d, has output "
                     "%d, inp mf received: %d, config media fmt received: %d",
                     in_port_index,
                     out_port_index,
                     is_input_ready,
                     has_output,
                     inp_mf_received,
                     me_ptr->configured_media_fmt_received);
#endif

      bool_t need_to_underrun              = FALSE;
      bool_t need_to_reduce_underrun_print = TRUE;
      // By default assume its not in steady state, if it is input port info should be valid
      if ((me_ptr->in_port_info_ptr) && (RAT_PORT_INDEX_INVALID != in_port_index))
      {
         capi_rat_check_data_flow_state(input[in_port_index],
                                        me_ptr->in_port_info_ptr[in_port_index].inp_mf_received,
                                        &me_ptr->in_port_info_ptr[in_port_index].df_state);
      }

      if (has_output)
      {
         // Case 1: Has output and input mf is received i.e. input is ready, then we can copy and send the data
         if (is_input_ready)
         {
            uint32_t in_actual_data_len = input[in_port_index]->buf_ptr[0].actual_data_len;
            uint32_t out_max_data_len   = output[out_port_index]->buf_ptr[0].max_data_len;
            for (uint32_t i = 0; i < output[out_port_index]->bufs_num; i++)
            {
               if (!(output[out_port_index]->buf_ptr[i].data_ptr) || !(input[in_port_index]->buf_ptr[i].data_ptr))
               {
                  continue;
               }

               bytes_copied = memscpy(output[out_port_index]->buf_ptr[i].data_ptr,
                                      out_max_data_len,
                                      input[in_port_index]->buf_ptr[i].data_ptr,
                                      in_actual_data_len);

               // Inp data len before process
               bytes_to_copy = in_actual_data_len;

               if (bytes_copied < out_max_data_len)
               {
                  memset(output[out_port_index]->buf_ptr[i].data_ptr + bytes_copied,
                         0,
                         out_max_data_len - bytes_copied);
                  need_to_underrun = TRUE;
               }

               // since unpacked v2 update lengths only for first ch
               if (0 == i)
               {
                  input[in_port_index]->buf_ptr[0].actual_data_len   = bytes_copied;
                  output[out_port_index]->buf_ptr[0].actual_data_len = out_max_data_len;
               }
            }
         }
         // Case 2: Output is present, it is the static port (would have received configured mf checked above)
         // but input is not ready because actual data len is 0/did not receive inp mf -> we push zeros with config
         // mf
         else if (capi_rat_is_output_static_port(&me_ptr->out_port_info_ptr[out_port_index].cmn))
         {
            uint32_t out_max_data_len = output[out_port_index]->buf_ptr[0].max_data_len;
            for (uint32_t i = 0; i < output[out_port_index]->bufs_num; i++)
            {
               memset(output[out_port_index]->buf_ptr[i].data_ptr, 0, out_max_data_len);
            }
            output[out_port_index]->buf_ptr[0].actual_data_len = out_max_data_len;

            need_to_underrun = TRUE;
         }
         // Case 3: Output is present, it is not the static port,received inp mf,
         // input is not ready because actual data len is 0, we push zeros with inp mf
         else if ((me_ptr->in_port_info_ptr) && (RAT_PORT_INDEX_INVALID != in_port_index) &&
                  (me_ptr->in_port_info_ptr[in_port_index].inp_mf_received))
         {
            uint32_t out_max_data_len = output[out_port_index]->buf_ptr[0].max_data_len;
            for (uint32_t i = 0; i < output[out_port_index]->bufs_num; i++)
            {
               memset(output[out_port_index]->buf_ptr[i].data_ptr, 0, out_max_data_len);
            }
            output[out_port_index]->buf_ptr[0].actual_data_len = out_max_data_len;

            need_to_underrun = TRUE;
         }
         // Case 4: Do nothing. Output is present and it is not the static port. Further, can be any one of these
         // scenarios:
         // 1. Dynamic inp port is not opened (inp port idx = INVALID)
         // 2. Dyn inp port did not receive inp mf -> In this case it will drop input
         // 3. Dyn inp port did not receive any data i.e. actual data len 0
         else
         {
            // input data is consumed, print only once, once inp mf is received it will go into underrun check anyway
            if (me_ptr->counter <= 2)
            {
               RAT_MSG_ISLAND(me_ptr->iid,
                              DBG_LOW_PRIO,
                              "CAPI_RAT: Dropping data(if present) at input - either inp port not opened, no inp "
                              "buffer or no input media format received at inp port index 0x%lx inp port id 0x%lx",
                              in_port_index,
                              rat_out_port_ptr->cmn.conn_port_id);
            }
         }

         // bytes_copied will be non zero only in Case 1
         if (need_to_underrun)
         {
            capi_rat_check_print_underrun_per_port(&me_ptr->out_port_info_ptr[out_port_index].underrun_info,
                                                   me_ptr->iid,
                                                   me_ptr->out_port_info_ptr[out_port_index].cmn.self_port_id,
                                                   (output[out_port_index]->buf_ptr[0].max_data_len - bytes_copied),
                                                   need_to_reduce_underrun_print);
         }

      }    // End of has_output = true
      else // Case 5: Output buffer is not present or Output is opened but not started, drop the input
      {
         RAT_MSG_ISLAND(me_ptr->iid,
                        DBG_LOW_PRIO,
                        "CAPI_RAT: Dropping data at RAT module since theres no started output, out port idx 0x%lx, "
                        "has_output %d",
                        out_port_index,
                        has_output);
      }

      // drop dfg and mark flushing eos to non-flushing if output is present, otherwise just drop metadata
      capi_rat_process_metadata(me_ptr,
                                input,
                                output,
                                in_port_index,
                                out_port_index,
                                bytes_copied,
                                bytes_to_copy,
                                has_output);

      // If output is present, insert begin and end silence md, Case 1,2,3
      if ((CAPI_MIMO_RAT == me_ptr->type) && has_output && (me_ptr->in_port_info_ptr) &&
          ((me_ptr->in_port_info_ptr[in_port_index].inp_mf_received) ||
           capi_rat_is_output_static_port(&me_ptr->out_port_info_ptr[out_port_index].cmn)))
      {
         capi_stream_data_v2_t *out_stream_ptr = (capi_stream_data_v2_t *)output[out_port_index];

         // Insert begin or end silence md depending on current state of RAT module
         capi_rat_track_inserted_silence_status(me_ptr,
                                                out_stream_ptr,
                                                &me_ptr->out_port_info_ptr[out_port_index],
                                                &me_ptr->in_port_info_ptr[out_port_index],
                                                bytes_copied,
                                                is_input_ready);
      }
   } // End of for loop going over all output ports
   return result;
}

static capi_err_t capi_rat_calc_drift_set_timer(capi_rat_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   int32_t prev_acc_qt_hwep_drift_us = 0, curr_acc_qt_hwep_drift_us = 0, inst_qt_hwep_drift_us = 0;
   int32_t prev_acc_hwep_bt_drift_us = 0, curr_acc_hwep_bt_drift_us = 0, inst_hwep_bt_drift_us = 0;
   int32_t prev_acc_qt_remote_drift_us = 0, curr_acc_qt_remote_drift_us = 0, inst_qt_remote_drift_us = 0;
   int32_t qt_adj_us = 0, inst_combined_drift_us = 0;

   /* Depending on the ports which are opened we check what drift needs to be calculated
    * If the ports are not opened the drift value will anyway be 0 */

   if (CTRL_PORT_PEER_CONNECTED == me_ptr->inp_ctrl_port_arr[HWEP_BT_PORT_IDX].state)
   {
      rat_inp_ctrl_port_info_t *hwep_bt_drift_ctrl_port_ptr = &me_ptr->inp_ctrl_port_arr[HWEP_BT_PORT_IDX];

      /* Calculate instantaneous drift between hwep and bt */
      prev_acc_hwep_bt_drift_us = hwep_bt_drift_ctrl_port_ptr->acc_drift.acc_drift_us;

      /* Get the drift*/
      capi_rat_get_inp_drift(hwep_bt_drift_ctrl_port_ptr);

      curr_acc_hwep_bt_drift_us = hwep_bt_drift_ctrl_port_ptr->acc_drift.acc_drift_us;
      if (me_ptr->counter > 1)
      {
         inst_hwep_bt_drift_us = curr_acc_hwep_bt_drift_us - prev_acc_hwep_bt_drift_us;
      }

#ifdef RAT_DEBUG
      if (0 != inst_hwep_bt_drift_us)
      {
         RAT_MSG_ISLAND(me_ptr->iid,
                        DBG_HIGH_PRIO,
                        "CAPI_RAT: hwep_bt drift port op with curr_acc_hwep_bt_drift_us %d, inst drift = %d",
                        curr_acc_hwep_bt_drift_us,
                        inst_hwep_bt_drift_us);
      }
#endif
   }

   if (CTRL_PORT_PEER_CONNECTED == me_ptr->inp_ctrl_port_arr[QT_HWEP_PORT_IDX].state)
   {
      rat_inp_ctrl_port_info_t *qt_hwep_drift_ctrl_port_ptr = &me_ptr->inp_ctrl_port_arr[QT_HWEP_PORT_IDX];

      /* Calculate instantaneous drift between QT and hwep */
      prev_acc_qt_hwep_drift_us = qt_hwep_drift_ctrl_port_ptr->acc_drift.acc_drift_us;

      /* Get the drift*/
      capi_rat_get_inp_drift(qt_hwep_drift_ctrl_port_ptr);

      curr_acc_qt_hwep_drift_us = qt_hwep_drift_ctrl_port_ptr->acc_drift.acc_drift_us;
      if (me_ptr->counter > 1)
      {
         inst_qt_hwep_drift_us = curr_acc_qt_hwep_drift_us - prev_acc_qt_hwep_drift_us;
      }

#ifdef RAT_DEBUG
      if (0 != inst_qt_hwep_drift_us)
      {
         RAT_MSG_ISLAND(me_ptr->iid,
                        DBG_HIGH_PRIO,
                        "CAPI_RAT: qt hwep drift port op with curr_acc_qt_hwep_drift_us %d, inst drift = %d",
                        curr_acc_qt_hwep_drift_us,
                        inst_qt_hwep_drift_us);
      }
#endif
   }

   if (CTRL_PORT_PEER_CONNECTED == me_ptr->inp_ctrl_port_arr[QT_REMOTE_PORT_IDX].state)
   {
      rat_inp_ctrl_port_info_t *qt_remote_drift_ctrl_port_ptr = &me_ptr->inp_ctrl_port_arr[QT_REMOTE_PORT_IDX];

      /* Calculate instantaneous drift between qt and remote */
      prev_acc_qt_remote_drift_us = qt_remote_drift_ctrl_port_ptr->acc_drift.acc_drift_us;

      /* Get the drift*/
      capi_rat_get_inp_drift(qt_remote_drift_ctrl_port_ptr);

      curr_acc_qt_remote_drift_us = qt_remote_drift_ctrl_port_ptr->acc_drift.acc_drift_us;
      if (me_ptr->counter > 1)
      {
         inst_qt_remote_drift_us = curr_acc_qt_remote_drift_us - prev_acc_qt_remote_drift_us;
      }

#ifdef RAT_DEBUG
      if (0 != inst_qt_remote_drift_us)
      {
         RAT_MSG_ISLAND(me_ptr->iid,
                        DBG_HIGH_PRIO,
                        "CAPI_RAT: qt_remote drift port op with curr_acc_qt_remote_drift_us %d, inst drift = %d",
                        curr_acc_qt_remote_drift_us,
                        inst_qt_remote_drift_us);
      }
#endif
   }

   /* Calculate instantaneous accumulated drift
    * This could be either one of the below scenarios
    * 1. Between qt and remote
    * 2. between qt and bt by using two drifts qt_hwep and hwep_bt
    * 3. Between qt and hwep */
   inst_combined_drift_us = inst_qt_hwep_drift_us + inst_hwep_bt_drift_us + inst_qt_remote_drift_us;

   /* Accumulate all inst drift here, will be updated with correction later to reflect pending amount */
   me_ptr->rat_pending_drift_us += inst_combined_drift_us;

   /* No corrections applied if acc_drift_us falls inside -50us to +50us*/
   /*
    * +ve => QT is faster than bt (or hwep or remote).
    *        Means bt (hwep, remote) is slower than QT.
    *        So, QT period should be increased to
    *        match bt rate.
    * */
   if (me_ptr->rat_pending_drift_us > 0)
   {
      if (me_ptr->rat_pending_drift_us >= MAX_CORR_US)
      {
         // should be +ve
         qt_adj_us = (int32_t)(MAX_CORR_US);
      }
   }
   /*
    * -ve => QT is slower than bt (hwep or remote).
    *        Means bt (hwep or remote) is faster than QT.
    *        So, QT period should be decreased to
    *        match bt rate.
    * */
   else if (me_ptr->rat_pending_drift_us < 0)
   {
      if (me_ptr->rat_pending_drift_us <= -MAX_CORR_US)
      {
         // should be -ve
         qt_adj_us = -(int32_t)(MAX_CORR_US);
      }
   }

   /* Update the pending drift with what has been corrected */
   me_ptr->rat_pending_drift_us -= qt_adj_us;
   /* Reported drift should be what has been corrected */
   me_ptr->rat_out_drift_info.rat_acc_drift.acc_drift_us += qt_adj_us;
#ifdef FLOATING_POINT_DEFINED
   double calc_ts_us          = (double)(me_ptr->integ_sr_us * me_ptr->counter);
   double time_us_based_on_sr = calc_ts_us / ((double)me_ptr->configured_media_fmt.format.sampling_rate);
#else
   uint64_t calc_ts_us          = (me_ptr->integ_sr_us * me_ptr->counter);
   uint64_t time_us_based_on_sr = calc_ts_us / me_ptr->configured_media_fmt.format.sampling_rate;
#endif
   /* Adjust next interrupt duration = abs start time + bytes consumed + total drift corrected so far
    * Store the value of next interrupt time, which is used for time stamp purpose
    * Initially timestamp is 0 and we add each process cycle depending on drift,
    * thatswhy use an absolute timer */
   me_ptr->rat_out_drift_info.rat_acc_drift.time_stamp_us =
      me_ptr->absolute_start_time_us + time_us_based_on_sr + me_ptr->rat_out_drift_info.rat_acc_drift.acc_drift_us;

   /* Get current time */
   uint64_t curr_time_us = (uint64_t)posal_timer_get_time();

#ifdef RAT_DEBUG
   RAT_MSG_ISLAND(me_ptr->iid,
                  DBG_HIGH_PRIO,
                  "CAPI_RAT: counter %d: new timestamp %ld = abs_time + time_us_based_on_sr %lu + total corrected "
                  "drift(output "
                  "drift) %ld \n curr_time_us %ld inst combined drift %d, inst adj drift %d, pending drift %ld",
                  me_ptr->counter,
                  me_ptr->rat_out_drift_info.rat_acc_drift.time_stamp_us,
                  time_us_based_on_sr,
                  me_ptr->rat_out_drift_info.rat_acc_drift.acc_drift_us,
                  curr_time_us,
                  inst_combined_drift_us,
                  qt_adj_us,
                  me_ptr->rat_pending_drift_us);
#endif

   /**< Next interrupt time to be programmed is less than current time,
    *  which indicates signal miss. Set is_signal_miss flag and by checking this flag,
    *  dynamic thread work loop will be able to detect timer interrupt driven thread's
    *  signal miss  */
   if (me_ptr->rat_out_drift_info.rat_acc_drift.time_stamp_us < curr_time_us)
   {
      RAT_MSG_ISLAND(me_ptr->iid,
                     DBG_ERROR_PRIO,
                     "CAPI_RAT: module signal miss, timestamp %d, curr time %d",
                     me_ptr->rat_out_drift_info.rat_acc_drift.time_stamp_us,
                     curr_time_us);

#ifdef SIM
      *((volatile uint32_t *)0) = 0; // induce a crash

#else // handling for on-tgt

      // Counter to know how many frames needed to be added
      uint64_t curr_counter = me_ptr->counter;

      while (me_ptr->rat_out_drift_info.rat_acc_drift.time_stamp_us < curr_time_us)
      {
         // Increment counter which is used for calculating absolute-time based frame duration, incrementing by 1 is
         // equivalent to adding one frame dur ms
         me_ptr->counter++;

#ifdef FLOATING_POINT_DEFINED
         double calc_ts_us          = (double)(me_ptr->integ_sr_us * me_ptr->counter);
         double time_us_based_on_sr = calc_ts_us / ((double)me_ptr->configured_media_fmt.format.sampling_rate);
#else
         calc_ts_us          = (me_ptr->integ_sr_us * me_ptr->counter);
         time_us_based_on_sr = calc_ts_us / me_ptr->configured_media_fmt.format.sampling_rate;
#endif
         // New output timestamp
         me_ptr->rat_out_drift_info.rat_acc_drift.time_stamp_us = me_ptr->absolute_start_time_us + time_us_based_on_sr +
                                                                  me_ptr->rat_out_drift_info.rat_acc_drift.acc_drift_us;
      }

      RAT_MSG_ISLAND(me_ptr->iid,
                     DBG_HIGH_PRIO,
                     "CAPI_RAT: Counter: %ld: Added frame duration %d, %d times, to ensure next timer tick is in the "
                     "future, "
                     "timestamp %d, curr time "
                     "%d",
                     me_ptr->counter,
                     time_us_based_on_sr,
                     (me_ptr->counter - curr_counter),
                     me_ptr->rat_out_drift_info.rat_acc_drift.time_stamp_us,
                     curr_time_us);
#endif // SIM
   }

   // Increment timer for next process
   me_ptr->counter++;

   /** Set up timer for specified absolute duration */

   int32_t rc =
      posal_timer_oneshot_start_absolute(me_ptr->timer, me_ptr->rat_out_drift_info.rat_acc_drift.time_stamp_us);
   if (rc)
   {
      RAT_MSG_ISLAND(me_ptr->iid, DBG_ERROR_PRIO, "CAPI_RAT: one shot timer start failed result: %lu", rc);
      return CAPI_EFAILED;
   }

   return capi_result;
}

static bool_t capi_rat_check_data_flow_state(capi_stream_data_t * input,
                                             bool_t               is_capi_in_media_fmt_set,
                                             rat_data_flow_state *data_flow_state_ptr)
{
   bool_t is_data_valid       = TRUE;
   bool_t is_not_steady_state = TRUE;

   if (!input || !input->buf_ptr || !input->buf_ptr[0].data_ptr || (!is_capi_in_media_fmt_set))
   {
      is_data_valid = FALSE;
   }

   if (input && (TRUE == input->flags.marker_eos))
   {
      *data_flow_state_ptr = RAT_DF_STOPPING;
   }
   else if (is_data_valid)
   {
      *data_flow_state_ptr = RAT_DF_STARTED;
      is_not_steady_state  = FALSE;
   }
   else if (*data_flow_state_ptr == RAT_DF_STOPPING)
   {
      *data_flow_state_ptr = RAT_DF_STOPPED;
   }

   return is_not_steady_state;
}

static void capi_rat_check_print_underrun_per_port(capi_cmn_underrun_info_t *underrun_info_ptr,
                                                   uint32_t                  iid,
                                                   uint32_t                  port_id,
                                                   uint32_t                  bytes,
                                                   bool_t                    need_to_reduce_underrun_print)
{
   underrun_info_ptr->underrun_counter++;
   uint64_t curr_time = posal_timer_get_time();
   uint64_t diff      = curr_time - underrun_info_ptr->prev_time;
   uint64_t threshold = CAPI_CMN_UNDERRUN_TIME_THRESH_US;

   if (!need_to_reduce_underrun_print)
   {
      threshold = CAPI_CMN_STEADY_STATE_UNDERRUN_TIME_THRESH_US; // Every 10ms
   }

   if ((diff >= threshold) || (0 == underrun_info_ptr->prev_time))
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO,
                    "CAPI_RAT 0x%lx: Underrun detected inserting %d zeros on port_id 0x%lx, Count:%ld, time since prev "
                    "print: %ld us, steady state: %d",
                    iid,
                    bytes,
                    port_id,
                    underrun_info_ptr->underrun_counter,
                    diff,
                    !need_to_reduce_underrun_print);
      underrun_info_ptr->prev_time        = curr_time;
      underrun_info_ptr->underrun_counter = 0;
   }

   return;
}

/* In this function we insert start or end of silence md*/
static void capi_rat_track_inserted_silence_status(capi_rat_t *           me_ptr,
                                                   capi_stream_data_v2_t *out_stream_ptr,
                                                   capi_rat_out_port_t *  out_port_info_ptr,
                                                   capi_rat_in_port_t *   in_port_info_ptr,
                                                   uint32_t               bytes_processed,
                                                   bool_t                 is_input_ready)
{
   // By default set to true so that static port can send begin silence md at start
   bool_t   curr_state_is_inserted_silence = TRUE;
   uint32_t md_offset                      = 0;

   // This flag will be set either if input closed and we need to send eos on connected output,
   // or if we received flushing eos at the input
   if (out_port_info_ptr->begin_silence_insertion_md)
   {
      curr_state_is_inserted_silence                = TRUE;
      out_port_info_ptr->begin_silence_insertion_md = FALSE;

      uint32_t bytes_per_sample = 0;
      if (capi_rat_is_output_static_port(&out_port_info_ptr->cmn))
      {
         bytes_per_sample = me_ptr->configured_media_fmt.format.bits_per_sample >> 3;
      }
      else
      {
         bytes_per_sample = in_port_info_ptr->media_fmt.format.bits_per_sample >> 3;
      }

      // For begin silence md due to eos, silence will start after data produced on current call
      md_offset = bytes_processed / (bytes_per_sample);
   }
   // if there is valid input change to false
   else if (is_input_ready)
   {
      curr_state_is_inserted_silence = FALSE;
   }

   // If there is a change in state we need to insert a metadata
   if (curr_state_is_inserted_silence != out_port_info_ptr->is_state_inserted_silence)
   {
      // Insert md
      if (!me_ptr->metadata_handler.metadata_create)
      {
         RAT_MSG_ISLAND(me_ptr->iid, DBG_ERROR_PRIO, "CAPI_RAT: No metadata_handler available");
         return;
      }

      module_cmn_md_t *md_ptr    = NULL;
      ar_result_t      ar_result = me_ptr->metadata_handler.metadata_create(me_ptr->metadata_handler.context_ptr,
                                                                       &out_stream_ptr->metadata_list_ptr,
                                                                       0 /*payload_size*/,
                                                                       me_ptr->heap_mem,
                                                                       FALSE /*is_outband*/,
                                                                       &md_ptr);

      if (AR_DID_FAIL(ar_result))
      {
         RAT_MSG_ISLAND(me_ptr->iid,
                        DBG_ERROR_PRIO,
                        "CAPI_RAT: Failed to create metadata entry for silence md with error 0x%x",
                        ar_result);
         return;
      }

      md_ptr->metadata_flag.buf_sample_association = MODULE_CMN_MD_BUFFER_ASSOCIATED;
      md_ptr->offset                               = md_offset;

      // Update the metadata parameters depending on state change
      if (curr_state_is_inserted_silence)
      {
         md_ptr->metadata_id = MD_ID_BEGIN_INSERTED_SILENCE_DATA;
      }
      else
      {
         md_ptr->metadata_id = MD_ID_END_INSERTED_SILENCE_DATA;
      }

      // Update the output state
      out_port_info_ptr->is_state_inserted_silence = curr_state_is_inserted_silence;

      RAT_MSG_ISLAND(me_ptr->iid,
                     DBG_HIGH_PRIO,
                     "CAPI_RAT: Generated MD ID 0x%lx, out len %d offset %d on out port index %d",
                     md_ptr->metadata_id,
                     out_stream_ptr->buf_ptr->actual_data_len,
                     md_ptr->offset,
                     out_port_info_ptr->cmn.self_index);
   }
}

static void capi_rat_process_metadata(capi_rat_t *        me_ptr,
                                      capi_stream_data_t *input[],
                                      capi_stream_data_t *output[],
                                      uint32_t            input_port_index,
                                      uint32_t            output_port_index,
                                      uint32_t            bytes_processed,
                                      uint32_t            initial_input_len,
                                      bool_t              has_output)
{
   if (input && input[input_port_index])
   {
      capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input[input_port_index];

      if ((!in_stream_ptr->metadata_list_ptr) && (in_stream_ptr->flags.end_of_frame || in_stream_ptr->flags.marker_eos))
      {
         in_stream_ptr->flags.marker_eos   = FALSE;
         in_stream_ptr->flags.end_of_frame = FALSE;
         RAT_MSG_ISLAND(me_ptr->iid,
                        DBG_ERROR_PRIO,
                        "CAPI_RAT: Md list ptr is NULL, so clearing EOF and EOS on input port.");
         return;
      }

      // This covers Case 1, 2, 3
      if (has_output && ((me_ptr->in_port_info_ptr[input_port_index].inp_mf_received) ||
                         capi_rat_is_output_static_port(&me_ptr->out_port_info_ptr[output_port_index].cmn)))
      {
         capi_stream_data_v2_t *out_stream_ptr = (capi_stream_data_v2_t *)output[output_port_index];

         capi_media_fmt_v2_t *mf_ptr = NULL;
         if (capi_rat_is_output_static_port(&me_ptr->out_port_info_ptr[output_port_index].cmn))
         {
            mf_ptr = &me_ptr->configured_media_fmt;
         }
         else
         {
            mf_ptr = &me_ptr->in_port_info_ptr[input_port_index].media_fmt;
         }

         // Flushing eos is converted to non flushing
         // DFG is dropped at the input
         if (in_stream_ptr->flags.end_of_frame)
         {
            // go through each MD and mark as non-flushing
            module_cmn_md_list_t *node_ptr = in_stream_ptr->metadata_list_ptr;
            module_cmn_md_list_t *next_ptr = NULL;
            while (node_ptr)
            {
               next_ptr = node_ptr->next_ptr;

               module_cmn_md_t *    md_ptr            = (module_cmn_md_t *)node_ptr->obj_ptr;
               module_cmn_md_eos_t *eos_md_ptr        = NULL;
               bool_t               flush_eos_present = FALSE;

               if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
               {
                  bool_t out_of_band = md_ptr->metadata_flag.is_out_of_band;
                  if (out_of_band)
                  {
                     eos_md_ptr = (module_cmn_md_eos_t *)md_ptr->metadata_ptr;
                  }
                  else
                  {
                     eos_md_ptr = (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);
                  }

                  flush_eos_present = eos_md_ptr->flags.is_flushing_eos;
                  if (flush_eos_present)
                  {
                     RAT_MSG_ISLAND(me_ptr->iid,
                                    DBG_HIGH_PRIO,
                                    "CAPI_RAT: EOS metadata found 0x%p, flush_eos_present %u. Marking as "
                                    "non-flushing",
                                    eos_md_ptr,
                                    flush_eos_present);

                     if (me_ptr->type == CAPI_MIMO_RAT)
                     {
                        // Send out silence md: We will get flushing eos on input if a)If upstream stops/closes b)If
                        // input
                        // port of RAT stops/closes
                        RAT_MSG_ISLAND(me_ptr->iid,
                                       DBG_HIGH_PRIO,
                                       "CAPI_RAT: Eos received, setting flag to send BEGIN silence md");
                        me_ptr->out_port_info_ptr[output_port_index].begin_silence_insertion_md = TRUE;
                     }
                     eos_md_ptr->flags.is_flushing_eos = MODULE_CMN_MD_EOS_NON_FLUSHING;
                  }

                  // moving eos at the end of buffer
                  md_ptr->offset = bytes_processed / (mf_ptr->format.bits_per_sample >> 3);
               }
               else if (MODULE_CMN_MD_ID_DFG == md_ptr->metadata_id)
               {

                  me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                            node_ptr,
                                                            TRUE /* is dropped*/,
                                                            &in_stream_ptr->metadata_list_ptr);
               }
               node_ptr = next_ptr;
            }
         }

         in_stream_ptr->flags.marker_eos   = FALSE;
         in_stream_ptr->flags.end_of_frame = FALSE;

         intf_extn_md_propagation_t input_md_info, output_md_info;
         memset(&input_md_info, 0, sizeof(input_md_info));
         input_md_info.df                          = mf_ptr->header.format_header.data_format;
         input_md_info.len_per_ch_in_bytes         = bytes_processed;
         input_md_info.initial_len_per_ch_in_bytes = initial_input_len;
         input_md_info.bits_per_sample             = mf_ptr->format.bits_per_sample;
         input_md_info.sample_rate                 = mf_ptr->format.sampling_rate;

         memscpy(&output_md_info, sizeof(output_md_info), &input_md_info, sizeof(input_md_info));
         output_md_info.initial_len_per_ch_in_bytes = 0;

         me_ptr->metadata_handler.metadata_propagate(me_ptr->metadata_handler.context_ptr,
                                                     in_stream_ptr,
                                                     out_stream_ptr,
                                                     NULL,
                                                     0,
                                                     &input_md_info,
                                                     &output_md_info);
      }
      else // Case 4 and 5: no output or input mf not received
      {
         // destroy any metadata that might be there in input
         if (in_stream_ptr->metadata_list_ptr)
         {
            module_cmn_md_list_t *node_ptr = in_stream_ptr->metadata_list_ptr;
            module_cmn_md_list_t *next_ptr = NULL;

            while (node_ptr)
            {
               next_ptr = node_ptr->next_ptr;
               me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                         node_ptr,
                                                         TRUE /* is dropped*/,
                                                         &in_stream_ptr->metadata_list_ptr);
               node_ptr = next_ptr;
            }
         }

         in_stream_ptr->flags.marker_eos   = FALSE; // marking input EOS/EOF flags as FALSE as destroying all meta data
         in_stream_ptr->flags.end_of_frame = FALSE;
      } // End of case where no output or no input mf is received
   }    // End of input is not null

   return;
}

void capi_rat_process_md_with_no_output(capi_rat_t *me_ptr, capi_stream_data_t *input[])
{
   if ((input) && (me_ptr->in_port_info_ptr))
   {
      for (uint32_t in_port_index = 0; in_port_index < me_ptr->num_in_ports; in_port_index++)
      {
         // This check indicates input port is valid but corresponding output port is not opened/connected
         if ((RAT_PORT_INDEX_INVALID != me_ptr->in_port_info_ptr[in_port_index].cmn.self_index) &&
             RAT_PORT_INDEX_INVALID == me_ptr->in_port_info_ptr[in_port_index].cmn.conn_index)
         {
            RAT_MSG_ISLAND(me_ptr->iid,
                           DBG_LOW_PRIO,
                           "CAPI_RAT: Dropping data & md at input port id %lx, since corresponding output port is not "
                           "opened/connected",
                           me_ptr->in_port_info_ptr[in_port_index].cmn.self_port_id);

            capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input[in_port_index];

            in_stream_ptr->flags.marker_eos = FALSE; // marking input EOS/EOF flags as FALSE as destroying all meta data
            in_stream_ptr->flags.end_of_frame = FALSE;

            // destroy any metadata that might be there in input
            if (in_stream_ptr->metadata_list_ptr)
            {
               module_cmn_md_list_t *node_ptr = in_stream_ptr->metadata_list_ptr;
               module_cmn_md_list_t *next_ptr = NULL;

               while (node_ptr)
               {
                  next_ptr = node_ptr->next_ptr;
                  me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                            node_ptr,
                                                            TRUE /* is dropped*/,
                                                            &in_stream_ptr->metadata_list_ptr);
                  node_ptr = next_ptr;
               }
            }
         }
      }
   }
   return;
}

/*============================ Incoming IMCL port handling functions ======================== */

capi_err_t capi_rat_get_inp_drift(rat_inp_ctrl_port_info_t *port_info_ptr)
{
   imcl_tdi_hdl_t *timer_drift_hdl_ptr = port_info_ptr->timer_drift_info_hdl_ptr;
   if ((NULL != timer_drift_hdl_ptr) && (NULL != timer_drift_hdl_ptr->get_drift_fn_ptr))
   {
      timer_drift_hdl_ptr->get_drift_fn_ptr(timer_drift_hdl_ptr, &port_info_ptr->acc_drift);
   }

   return CAPI_EOK;
}

// Checks if the output port passed is the static output port and if it already received mf configuration
bool_t capi_rat_is_output_static_port(capi_rat_cmn_port_t *port_cmn_ptr)
{
   if (PORT_ID_RATE_ADAPTED_TIMER_OUTPUT == port_cmn_ptr->self_port_id)
   {
      return TRUE;
   }
   return FALSE;
}