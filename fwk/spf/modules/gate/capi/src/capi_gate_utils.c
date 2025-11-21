/**
 * \file capi_gate_utils.c
 * \brief
 *     Source file to implement utility functions called by the CAPI Interface for gate module
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_gate_i.h"

/*==========================================================================
  Function Definitions
========================================================================== */
static inline uint64_t capi_gate_rounded_up_time(uint64_t time_us)
{
    return 1000*((time_us + 999)/1000);
}
/*------------------------------------------------------------------------
 Function name: capi_gate_until_deadline_process
  1. If the control link is opened and connected
*       a. If proc delay is received and deadline time is received from depack
*           First frame should be sent to encoder only if
*           deadline is reached.
*
*           If deadline is in future and not within frame duration, then data
*           can be dropped
*
*           If deadline is in past, then need to add frame interval
*           time to deadline and check whether the deadline is reached
*           or not.
*
*           If deadline is reached (within 1ms) then data is sent to
*           the encoder for processing.
*
*           Once deadline is serviced then, module is bypassed.
*
*       b.If they are not received wait and drop data till then
* 2. If open not set and not connected its a error scenario*
 * -----------------------------------------------------------------------*/

capi_err_t capi_gate_until_deadline_process(capi_gate_t *       me_ptr,
                                            capi_stream_data_t *input[],
                                            capi_stream_data_t *output[])
{
   capi_err_t result                    = CAPI_EOK;
   int32_t   time_to_reach_deadline_us = 0;

   if (CTRL_PORT_PEER_CONNECTED == me_ptr->in_ctrl_port_info.state)
   {
      if ((me_ptr->proc_dur_received) && (me_ptr->deadline_time_intent_received))
      {
         uint64_t deadline_us = 0, calc_deadline_time_us = 0, cur_time_us = 0;

         // 1. slimbus/swr delay = 3ms
         // 2. RT-Jitter delay = 1ms (delay between when encoder generated the frame and next signal trigger)
         uint64_t const_delay_us = 3000 + 1000;

         // encoder sometime will take rounded up ms to collect data and process.
         // if frame interval in 7.3 ms then it will take 8ms or 7ms to buffer.
         uint32_t rounded_up_frame_interval_us = capi_gate_rounded_up_time(me_ptr->frame_interval_us);
         
         // EP runs at 1ms frame size, if packetized encoded frame size length in time is not multiple of 1ms then it should be rounded up.
         // if encoded frame size length in time after COP pack is 5.3ms then it will take 6 ep intrrupts to transmit this frame completely.
         uint32_t rounded_up_ep_transmit_delay_us = capi_gate_rounded_up_time(me_ptr->ep_transmit_delay_us);

         cur_time_us = (uint64_t)posal_timer_get_time();
         deadline_us = me_ptr->deadline_time_us;

         POSAL_ASSERT(input[0]);

         uint32_t len_available_all_ch = input[0]->buf_ptr[0].actual_data_len * input[0]->bufs_num;
         uint32_t bytes_us_available   = capi_cmn_bytes_to_us(len_available_all_ch,
                                                            me_ptr->operating_mf.format.sampling_rate,
                                                            me_ptr->operating_mf.format.bits_per_sample,
                                                            me_ptr->operating_mf.format.num_channels,
                                                            (uint64_t *)NULL);

         // Calc deadline time is the estimated time at which our encoded packet will reach bt controller for OTA
         // This includes= data collection time(1ms data already available) + round up time(enc jitter)
         // + enc proc time + ep transmit time + const delay
         calc_deadline_time_us = cur_time_us + (rounded_up_frame_interval_us - bytes_us_available) +
                                 me_ptr->proc_dur_us + rounded_up_ep_transmit_delay_us + const_delay_us;

         AR_MSG(DBG_HIGH_PRIO,
                "capi_gate: Process: BT deadline time msw:%lu lsw:%lu, "
                "calc_deadline_time_us msw: %lu lsw:%lu, "
                "current time MSW:%ld LSW:%lu, "
                "proc delay %lu, "
                "transmit_delay over ep %lu",
                (uint32_t)(me_ptr->deadline_time_us >> 32),
                (uint32_t)(me_ptr->deadline_time_us),
                (uint32_t)(calc_deadline_time_us >> 32),
                (uint32_t)(calc_deadline_time_us),
                (uint32_t)(cur_time_us >> 32),
                (uint32_t)(cur_time_us),
                me_ptr->proc_dur_us,
                rounded_up_ep_transmit_delay_us);

         AR_MSG(DBG_HIGH_PRIO,
                "capi_gate: Process: bytes_available %d round up frame us %d  const delay %d",
                bytes_us_available,
                rounded_up_frame_interval_us,
                const_delay_us);

         /* deadline time in the past*/
         /* moving the deadline time to the next frame only if the calc_deadline_time_us is more than 100us ahead of deadline_us
            calc_deadline_time_us is based on the current time which also includes scheduling jitter. */
         while (calc_deadline_time_us > (deadline_us + GATE_SCHEDULING_JITTER_US))
         {
            deadline_us += me_ptr->frame_interval_us;
            AR_MSG(DBG_HIGH_PRIO,
                   "capi_gate: Deadline time in the past, adding frame interval to match new deadline time = "
                   "MSW: %lu LSW: %lu ",
                   (uint32_t)(deadline_us >> 32),
                   (uint32_t)(deadline_us));
         }

         me_ptr->deadline_time_us  = deadline_us;
         time_to_reach_deadline_us = me_ptr->deadline_time_us - calc_deadline_time_us;

         // Open the gate within 1ms, this works only when encoder sets req data buff to TRUE
         if (time_to_reach_deadline_us > GATE_NUM_US_PER_MS)
         {
            result = CAPI_ENEEDMORE;

            /* Deadline is not near, so skip sending data to encoder */
            AR_MSG(DBG_HIGH_PRIO, "capi_gate: Deadline time not reached yet");
         }
         else // lesser than or equal case open the gate
         {
            me_ptr->gate_opened = TRUE;
            AR_MSG(DBG_HIGH_PRIO, "capi_gate: Opening the gate");

            // Gate module can disable itself after opening and pushing data out
            capi_cmn_update_process_check_event(&me_ptr->event_cb_info, FALSE);

            POSAL_ASSERT(output[0]);

            for (uint32_t i = 0; i < input[0]->bufs_num; i++)
            {
               AR_MSG(DBG_HIGH_PRIO,
                      "capi_gate: Process: out max data len: %d, input actual len %d, New deadline time msw:%lu "
                      "lsw:%lu, "
                      "time_to_reach_deadline_us %ld",
                      output[0]->buf_ptr[i].max_data_len,
                      input[0]->buf_ptr[i].actual_data_len,
                      (uint32_t)(me_ptr->deadline_time_us >> 32),
                      (uint32_t)(me_ptr->deadline_time_us),
                      time_to_reach_deadline_us);

               // If not inplace do memscpy
               if (input[0]->buf_ptr[i].data_ptr != output[0]->buf_ptr[i].data_ptr)
               {
                  // memscpy will make sure it copies min of both length
                  memscpy(output[0]->buf_ptr[i].data_ptr,
                          output[0]->buf_ptr[i].max_data_len,
                          input[0]->buf_ptr[i].data_ptr,
                          input[0]->buf_ptr[i].actual_data_len);
               }

               uint32_t length = input[0]->buf_ptr[i].actual_data_len > output[0]->buf_ptr[i].max_data_len
                                    ? output[0]->buf_ptr[i].max_data_len
                                    : input[0]->buf_ptr[i].actual_data_len;

               output[0]->buf_ptr[i].actual_data_len = length;
               // No need to update input length, in this process call input should always be fully consumed
               // i.e. copied to the output, or latest input is copied and rest is dropped

               AR_MSG(DBG_HIGH_PRIO,
                      "capi_gate: End of process: out buf actual data length: %d input length %d ",
                      output[0]->buf_ptr[i].actual_data_len,
                      input[0]->buf_ptr[i].actual_data_len);
            }
         }
      }
      else
      {
         AR_MSG(DBG_MED_PRIO,
                "capi_gate: Warning!: Cannot process till container delay %d and deadline time %d have "
                "been "
                "received, dropping data",
                me_ptr->proc_dur_received,
                me_ptr->deadline_time_intent_received);
      }
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi_gate: Error: Cannot receive deadline time from COP depacketizer because ctrl port is "
             "not in connected state (state: %d) ",
             me_ptr->in_ctrl_port_info.state);
      return CAPI_EFAILED;
   }

   return result;
}

capi_err_t capi_gate_calc_acc_delay(capi_gate_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   return capi_result;
}

capi_err_t capi_gate_imcl_port_operation(capi_gate_t *           me_ptr,
                                         const capi_port_info_t *port_info_ptr,
                                         capi_buf_t *            params_ptr)
{
   capi_err_t                  capi_result   = CAPI_EOK;
   intf_extn_imcl_port_open_t *port_open_ptr = NULL;

   if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_imcl_port_operation_t))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi_gate: Invalid payload size[%lu] for ctrl port operation",
             params_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   intf_extn_param_id_imcl_port_operation_t *port_op_ptr =
      (intf_extn_param_id_imcl_port_operation_t *)(params_ptr->data_ptr);

   if (!port_op_ptr->op_payload.data_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_gate: NULL payload for ctrl port open/close opcode [0x%lX]", port_op_ptr->opcode);
      return CAPI_EBADPARAM;
   }

   switch (port_op_ptr->opcode)
   {
      case INTF_EXTN_IMCL_PORT_OPEN:
      {
         port_open_ptr = (intf_extn_imcl_port_open_t *)port_op_ptr->op_payload.data_ptr;

         /** Get the number of control ports being opened */
         uint32_t num_ports = port_open_ptr->num_ports;

         if (num_ports > GATE_MAX_CONTROL_PORTS)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_gate: IMCL CONNECT: gate module only supports one control port. Trying to "
                   "connect %lu",
                   num_ports);
            return CAPI_EBADPARAM;
         }

         /** Size Validation*/
         uint32_t valid_size =
            sizeof(intf_extn_imcl_port_open_t) + (num_ports * sizeof(intf_extn_imcl_id_intent_map_t));

         if (port_op_ptr->op_payload.actual_data_len < valid_size)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_gate: Invalid payload size[%lu] for ctrl port OPEN",
                   params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         /** Iterate over the list of all the control ports being opened */
         for (uint32_t iter = 0; iter < num_ports; iter++)
         {
            /** Validate the number of intents per control port   */
            if (port_open_ptr->intent_map[iter].num_intents > GATE_MAX_INTENTS_PER_CTRL_PORT)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_gate: IMCL OPEN: Expected num intent[%lu], Configured num intent[%lu]",
                      GATE_MAX_INTENTS_PER_CTRL_PORT,
                      port_open_ptr->intent_map[iter].num_intents);
               return CAPI_EBADPARAM;
            }

            /** Size Validation*/
            valid_size += (port_open_ptr->intent_map[iter].num_intents * sizeof(uint32_t));

            /** Validate the actual payload length */
            if (port_op_ptr->op_payload.actual_data_len < valid_size)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_gate: Invalid payload size[%lu] for ctrl port OPEN",
                      params_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            uint32_t ctrl_port_id = port_open_ptr->intent_map[iter].port_id;

            if (DEADLINE_TIME_INFO_IN != ctrl_port_id)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_gate: IMCL OPEN: Error, unsupported control port id %d  ", ctrl_port_id);
               return CAPI_EBADPARAM;
            }

            if (CTRL_PORT_OPEN == me_ptr->in_ctrl_port_info.state)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_gate: OPEN: Error, Ctrl port[0x%lX] already opened", ctrl_port_id);
               return CAPI_EFAILED;
            }

            /** Intent ID check */
            for (uint32_t idx = 0; idx < port_open_ptr->intent_map[iter].num_intents; idx++)
            {
               if (INTENT_ID_BT_DEADLINE_TIME != port_open_ptr->intent_map[iter].intent_arr[idx])
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_gate: Invalid intent id %x",
                         port_open_ptr->intent_map[iter].intent_arr[idx]);
                  capi_result |= CAPI_EBADPARAM;
                  continue;
               }
            }

            /* Save the values */
            me_ptr->in_ctrl_port_info.port_id = ctrl_port_id;
            me_ptr->in_ctrl_port_info.state   = CTRL_PORT_OPEN;

            AR_MSG(DBG_HIGH_PRIO, "capi_gate: IMCL OPEN: Ctrl port[0x%lX] open", ctrl_port_id);

         } /** End of for loop (num ports) */
         break;
      } /** End of case INTF_EXTN_IMCL_PORT_OPEN */
      case INTF_EXTN_IMCL_PORT_PEER_CONNECTED:
      {
         if (port_op_ptr->op_payload.data_ptr)
         {
            intf_extn_imcl_port_start_t *port_start_ptr =
               (intf_extn_imcl_port_start_t *)port_op_ptr->op_payload.data_ptr;
            /*Size Validation*/
            uint32_t num_ports = port_start_ptr->num_ports;

            if (num_ports > GATE_MAX_CONTROL_PORTS)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_gate: IMCL CONNECT: gate module only supports one control port. Trying to "
                      "connect %lu",
                      num_ports);
               return CAPI_EBADPARAM;
            }

            uint32_t valid_size = sizeof(intf_extn_imcl_port_start_t) + (num_ports * sizeof(uint32_t));
            if (port_op_ptr->op_payload.actual_data_len < valid_size)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_gate: IMCL CONNECT: Invalid payload size for ctrl port peer connect %d",
                      params_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            // for each port id in the list that follows
            for (uint32_t iter = 0; iter < num_ports; iter++)
            {
               if (DEADLINE_TIME_INFO_IN != port_start_ptr->port_id_arr[iter])
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_gate: IMCL CONNECT: Ctrl port id %lx mapping not found.",
                         port_start_ptr->port_id_arr[iter]);
                  return CAPI_EBADPARAM;
               }
               me_ptr->in_ctrl_port_info.state = CTRL_PORT_PEER_CONNECTED;
            }
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO, "capi_gate: IMCL CONNECT: Ctrl port peer connected expects a payload. Failing.");
            return CAPI_EBADPARAM;
         }
         break;
      }
      case INTF_EXTN_IMCL_PORT_PEER_DISCONNECTED:
      {
         if (port_op_ptr->op_payload.data_ptr)
         {
            intf_extn_imcl_port_stop_t *port_start_ptr = (intf_extn_imcl_port_stop_t *)port_op_ptr->op_payload.data_ptr;
            /*Size Validation*/
            uint32_t num_ports = port_start_ptr->num_ports;

            if (num_ports > GATE_MAX_CONTROL_PORTS)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_gate: IMCL DISCONNECT: gate module only supports %d control port. Trying to "
                      "disconnect %lu",
                      GATE_MAX_CONTROL_PORTS,
                      num_ports);
               return CAPI_EBADPARAM;
            }

            uint32_t valid_size = sizeof(intf_extn_imcl_port_stop_t) + (num_ports * sizeof(uint32_t));
            if (port_op_ptr->op_payload.actual_data_len < valid_size)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_gate: IMCL DISCONNECT: Invalid payload size for ctrl port peer disconnect %d",
                      params_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            // for each port id in the list that follows
            for (uint32_t iter = 0; iter < num_ports; iter++)
            {
               if (DEADLINE_TIME_INFO_IN != port_start_ptr->port_id_arr[iter])
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_gate: IMCL DISCONNECT: Ctrl port %lu mapping not found.",
                         port_start_ptr->port_id_arr[iter]);
                  return CAPI_EBADPARAM;
               }
               me_ptr->in_ctrl_port_info.state = CTRL_PORT_PEER_DISCONNECTED;
            }
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO, "capi_gate: IMCL DISCONNECT: expects a payload.Failing.");
            return CAPI_EBADPARAM;
         }
         break;
      }
      case INTF_EXTN_IMCL_PORT_CLOSE:
      {
         intf_extn_imcl_port_close_t *port_close_ptr = (intf_extn_imcl_port_close_t *)port_op_ptr->op_payload.data_ptr;

         /** Size Validation*/
         uint32_t num_ports = port_close_ptr->num_ports;
         if (!num_ports)
         {
            AR_MSG(DBG_HIGH_PRIO, "capi_gate: IMCL CLOSE: Warning! Num ctrl ports to be closed is zero");
            return CAPI_EOK;
         }

         if (num_ports > GATE_MAX_CONTROL_PORTS)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_gate: IMCL CLOSE: gate module only supports one control port. Trying to "
                   "close %lu",
                   num_ports);
            return CAPI_EBADPARAM;
         }

         uint32_t valid_size = sizeof(intf_extn_imcl_port_close_t) + (num_ports * sizeof(uint32_t));

         if (port_op_ptr->op_payload.actual_data_len < valid_size)
         {
            AR_MSG(DBG_ERROR_PRIO, "capi_gate: IMCL CLOSE: Invalid payload size %d", params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         /** Iterate over the list of port ID's  */
         for (uint32_t iter = 0; iter < num_ports; iter++)
         {
            /** Get the control port ID to be closed */
            uint32_t ctrl_port_id = port_close_ptr->port_id_arr[iter];

            if (DEADLINE_TIME_INFO_IN != ctrl_port_id)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_gate: IMCL CLOSE: Error, unsupported control port id %lx ", ctrl_port_id);
               return CAPI_EBADPARAM;
            }

            me_ptr->in_ctrl_port_info.state = CTRL_PORT_CLOSE;
         } /** End of for() */
         break;
      } /** End of case INTF_EXTN_IMCL_PORT_CLOSE */
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "capi_gate: Set, unsupported IMC port operation opcode ID[0x%lX]", port_op_ptr->opcode);
         capi_result = CAPI_EUNSUPPORTED;
         break;
      }
   } /** End of switch (port_op)) */
   return capi_result;
}
