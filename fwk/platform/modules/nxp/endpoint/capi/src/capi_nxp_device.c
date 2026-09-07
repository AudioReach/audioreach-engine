/* ========================================================================
  @file capi_nxp_device.c
  @brief This file contains CAPI implementation of nxp device Module

  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
  SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/

/*=====================================================================
  Includes
 ======================================================================*/
#include "capi_nxp_device_i.h"

/*------------------------------------------------------------------------
  Function name: capi_nxp_device_sink_init
  DESCRIPTION: Initialize the CAPIv2 nxp_device sink module and library.
  This function can allocate memory.
  -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_sink_init(
   capi_t *_pif,
   capi_proplist_t *init_set_properties)
{
   //AR_MSG(DBG_ERROR_PRIO,
   //         "CAPI_NXP_DEVICE: Init enter");
   return capi_nxp_device_common_init(_pif, init_set_properties, 1);
}

capi_err_t capi_nxp_device_sink_get_static_properties(
   capi_proplist_t *init_set_properties,
   capi_proplist_t *static_properties)
{
   return capi_nxp_device_process_get_properties((capi_nxp_device_t *)NULL, static_properties);
}

/*---------------------------------------------------------------------
  Function name: capi_nxp_device_process
  DESCRIPTION: Processes an input buffer and generates an output buffer.
  -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_process(capi_t *_pif,
                           capi_stream_data_t *input[],
                           capi_stream_data_t *output[])
{
   capi_err_t result = CAPI_EOK;
   capi_nxp_device_t *me_ptr = (capi_nxp_device_t *)_pif;

   if (me_ptr->direction == 0)
   {
      result |= capi_nxp_device_process_sink(_pif, input, output);
   }
   return result;
}

/*------------------------------------------------------------------------
  Function name: capi_nxp_device_get_mf
  DESCRIPTION: Function to get nxp device media fmt
 * -----------------------------------------------------------------------*/
static capi_err_t capi_nxp_device_get_mf(capi_nxp_device_t *me_ptr, capi_media_fmt_v2_t *media_fmt_ptr)
{

   return CAPI_EOK;
}

static capi_vtbl_t vtbl = {capi_nxp_device_process, capi_nxp_device_end,
                     capi_nxp_device_set_param, capi_nxp_device_get_param,
                     capi_nxp_device_set_properties, capi_nxp_device_get_properties};

capi_vtbl_t *capi_nxp_device_get_vtbl()
{
   return &vtbl;
}

/*------------------------------------------------------------------------
  Function name: capi_nxp_device_get_param
  DESCRIPTION: Gets either a parameter value or a parameter structure
  containing multiple parameters. In the event of a failure, the appropriate
  error code is returned.
 * -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_get_param(capi_t *_pif,
                             uint32_t param_id,
                             const capi_port_info_t *port_info_ptr,
                             capi_buf_t *params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if ((NULL == _pif) || (NULL == params_ptr) || (NULL == params_ptr->data_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Get param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_EBADPARAM;
   }

   capi_nxp_device_t *me_ptr      = (capi_nxp_device_t *)_pif;
   switch (param_id)
   {
      case FWK_EXTN_PARAM_ID_LATEST_TRIGGER_TIMESTAMP_PTR:
      {
         //capi_result                                       = capi_cmn_populate_trigger_ts_payload(params_ptr,
         //                                                   &me_ptr->drift_info.drift_calc_status.curr_intr_ts,
         //                                                   capi_hw_intf_cmn_update_ts,
         //                                                   (void *)(&me_ptr->hw_intf_ts_prop_cfg));
         params_ptr->data_ptr = NULL;
         break;
      }
      case PARAM_ID_HW_EP_MF_CFG:
      {
         param_id_hw_ep_mf_t *i2s_cfg_ptr = (param_id_hw_ep_mf_t *)params_ptr->data_ptr;
         i2s_cfg_ptr->bit_width    = 16;
         i2s_cfg_ptr->num_channels = 1;
         i2s_cfg_ptr->sample_rate  = 48000;
         i2s_cfg_ptr->data_format  = DATA_FORMAT_FIXED_POINT;

         break;
      }
      /*case PARAM_ID_I2S_INTF_CFG:
      {
         param_id_i2s_intf_cfg_t *i2s_cfg_ptr = (param_id_i2s_intf_cfg_t *)params_ptr->data_ptr;
         i2s_cfg_ptr->intf_idx    = 0;
         i2s_cfg_ptr->sd_line_idx = I2S_SD0;
         i2s_cfg_ptr->ws_src      = 0;

         break;
      }*/
      default: {
         AR_MSG(DBG_ERROR_PRIO, "unsupported get param ID 0x%x", (int)param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
      }
   }

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_nxp_device_set_param
  DESCRIPTION: Sets either a parameter value or a parameter structure containing
  multiple parameters. In the event of a failure, the appropriate error code is
  returned.
  -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_set_param(capi_t *_pif,
                             uint32_t param_id,
                             const capi_port_info_t *port_info_ptr,
                             capi_buf_t *params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if ((NULL == _pif) || (NULL == params_ptr) || (NULL == params_ptr->data_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_nxp_DEVICE: Get param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_EBADPARAM;
   }

   switch (param_id) {
      case PARAM_ID_HW_EP_TIMESTAMP_PROPAGATION:
      {
         /*if (I2S_SOURCE == me_ptr->i2s_driver.i2s_intf_state.direction)
         {
            if (param_size < sizeof(param_id_hw_ep_timestamp_propagation_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                     "CAPI_I2S: SetParam 0x%lx, invalid param size %lx ",
                     param_id,
                     params_ptr->actual_data_len);
               capi_result = CAPI_ENEEDMORE;
               break;
            }
            param_id_hw_ep_timestamp_propagation_t *i2s_ts_prop_cfg_ptr =
               (param_id_hw_ep_timestamp_propagation_t *)params_ptr->data_ptr;

            ar_result =
               capi_hw_intf_cmn_ts_prop_cfg(&me_ptr->hw_intf_ts_prop_cfg, i2s_ts_prop_cfg_ptr->is_timestamp_disabled);

            if (AR_EOK != ar_result)
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S: Timestamp propagation failed with 0x%lx", ar_result);
               return CAPI_EFAILED;
            }
            AR_MSG(DBG_LOW_PRIO,
                  "CAPI_I2S: Timestamp propagation set param received with value [%d] (0:Enable, 1:Disable)",
                  i2s_ts_prop_cfg_ptr->is_timestamp_disabled);
         }
         else
         {
            capi_result |= CAPI_EUNSUPPORTED;
         }*/
         break;
      }
      case PARAM_ID_HW_EP_SRC_OUT_DATA_INTERLEAVING:
      {
         /*if (I2S_SOURCE == me_ptr->i2s_driver.i2s_intf_state.direction)
         {
            if (param_size < sizeof(param_id_hw_ep_data_interleaving_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CAPI_I2S: SetParam 0x%lx, invalid param size %lx ",
                      param_id,
                      params_ptr->actual_data_len);
               capi_result = CAPI_ENEEDMORE;
               break;
            }

            param_id_hw_ep_data_interleaving_t *i2s_src_data_int_ptr =
               (param_id_hw_ep_data_interleaving_t *)params_ptr->data_ptr;

            if ((PCM_DEINTERLEAVED_UNPACKED != i2s_src_data_int_ptr->data_interleaving) &&
                (PCM_INTERLEAVED != i2s_src_data_int_ptr->data_interleaving))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CAPI_I2S: Interleaving value [%lu] not supported. ",
                      i2s_src_data_int_ptr->data_interleaving);
               capi_result = CAPI_EBADPARAM;
               break;
            }

            pcm_to_capi_interleaved_with_native_param(&me_ptr->i2s_media_fmt.format.data_interleaving,
                                                      i2s_src_data_int_ptr->data_interleaving,
                                                      CAPI_INVALID_INTERLEAVING);

            if (me_ptr->i2s_driver.i2s_intf_state.ep_mf_received)
            {
               /*Raise media format events
               capi_result |= capi_i2s_raise_media_fmt_event(me_ptr);
            }
            AR_MSG(DBG_LOW_PRIO,
                   "CAPI_I2S: Data Interleaving set param received, with int [%d]",
                   me_ptr->i2s_media_fmt.format.data_interleaving);
         }
         else
         {
            capi_result |= CAPI_EUNSUPPORTED;
         }*/
         break;
      }
      case PARAM_ID_HW_EP_MF_CFG:
      {
         /*if (param_size < sizeof(param_id_hw_ep_mf_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "CAPI_I2S: SetParam 0x%lx, invalid param size %lx ",
                   param_id,
                   params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
            break;
         }

         param_id_hw_ep_mf_t *i2s_cfg_ptr = (param_id_hw_ep_mf_t *)params_ptr->data_ptr;

         ar_result = i2s_driver_set_hw_ep_mf_cfg(i2s_cfg_ptr, &(me_ptr->i2s_driver));
         if (AR_EOK != ar_result)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S: Driver set ep mf failed with 0x%lx", ar_result);
            if(AR_EALREADY == ar_result)
            {
               return CAPI_EALREADY;
            }
            return CAPI_EFAILED;
         }

         if (I2S_SOURCE == me_ptr->i2s_driver.i2s_intf_state.direction)
         {
            capi_cmn_data_fmt_map(&me_ptr->i2s_driver.i2s_intf_state.data_format, &me_ptr->i2s_media_fmt);
            AR_MSG(DBG_HIGH_PRIO, "CAPI_I2S: Data format %d", me_ptr->i2s_media_fmt.header.format_header.data_format);
         }

         /*Raise media format events
         capi_result |= capi_i2s_raise_media_fmt_event(me_ptr);

         // threshold and algo event
         capi_result |= capi_i2s_raise_thresh_delay_events(me_ptr);
         if (CAPI_EOK != capi_result)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S:Failed to raise output media fmt and algo delay event", ar_result);
         }

         // KPPS event
         uint32_t kpps =
            (3 * (me_ptr->i2s_driver.i2s_intf_state.sample_rate) * (me_ptr->i2s_driver.i2s_intf_state.num_channels)) /
            640;
         capi_result = capi_cmn_update_kpps_event(&me_ptr->cb_info, kpps);

         // Bandwidth event
         uint32_t code_bandwidth = 0;
         uint32_t data_bandwidth = me_ptr->i2s_driver.i2s_intf_state.int_samples_per_period *
                                   me_ptr->i2s_driver.i2s_intf_state.bit_width *
                                   me_ptr->i2s_driver.i2s_intf_state.num_channels * 2;

         capi_result = capi_cmn_update_bandwidth_event(&me_ptr->cb_info, code_bandwidth, data_bandwidth);
         if (CAPI_EOK != capi_result)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S: Failed to send bandwidth update event with %lu", capi_result);
         }*/
         break;
      }
      case PARAM_ID_HW_EP_FRAME_SIZE_FACTOR:
      {
         /*if (param_size < sizeof(param_id_frame_size_factor_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "CAPI_I2S: SetParam 0x%lx, invalid param size %lu ",
                   param_id,
                   params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
            break;
         }
         param_id_frame_size_factor_t *i2s_cfg_ptr = (param_id_frame_size_factor_t *)params_ptr->data_ptr;

         ar_result = i2s_driver_set_frame_size_cfg(i2s_cfg_ptr, &(me_ptr->i2s_driver));
         if (AR_EOK != ar_result)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S: Driver set param failed with 0x%lx", ar_result);
            if(AR_EALREADY == ar_result)
            {
               return CAPI_EALREADY;
            }
            return CAPI_EFAILED;
         }

         if (me_ptr->i2s_driver.i2s_intf_state.ep_mf_received)
         {
            capi_result = capi_i2s_raise_thresh_delay_events(me_ptr);
            if (CAPI_EOK != capi_result)
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S:Failed to raise output media fmt and algo delay event", ar_result);
            }
         }*/
         break;
      }
      case PARAM_ID_HW_EP_POWER_MODE_CFG:
      {
         // cfg should be received before START (STM trigger)
         /*if (param_size < sizeof(param_id_hw_ep_power_mode_cfg_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "CAPI_I2S: SetParam POWER_MODE_CFG, invalid param size %lu ",
                   params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
            break;
         }
         param_id_hw_ep_power_mode_cfg_t *power_cfg_ptr = (param_id_hw_ep_power_mode_cfg_t *)params_ptr->data_ptr;

         if (HW_EP_POWER_MODE_MAX < power_cfg_ptr->power_mode)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S: SetParam POWER_MODE_CFG, invalid value %lu ", power_cfg_ptr->power_mode);
            capi_result = CAPI_EFAILED;
            break;
         }

         me_ptr->pm_info.power_mode = power_cfg_ptr->power_mode;
         AR_MSG(DBG_HIGH_PRIO, "Received POWER_MODE_CFG: mode %lu", power_cfg_ptr->power_mode);*/
         break;
      }
      case PARAM_ID_HW_EP_SRC_CHANNEL_MAP:
      {
         /*if (param_size < sizeof(param_id_hw_ep_src_channel_map_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "CAPI_I2S: SetParam HW_EP_SRC_CHANNEL_MAP, invalid param size %lu ",
                   params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
            break;
         }

         if (I2S_SOURCE != me_ptr->i2s_driver.i2s_intf_state.direction)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S: SetParam HW_EP_SRC_CHANNEL_MAP only supported for source modules");
            capi_result = CAPI_EBADPARAM;
            break;
         }

         param_id_hw_ep_src_channel_map_t *channel_cfg_ptr = (param_id_hw_ep_src_channel_map_t *)params_ptr->data_ptr;

         /* Cache channel map to driver
         capi_result |= i2s_driver_set_src_channel_map_cfg(channel_cfg_ptr, &me_ptr->i2s_driver);

         /* Send output mf if mf is received, if not it will be sent later
         if (me_ptr->i2s_driver.i2s_intf_state.ep_mf_received)
         {
            /*Raise media format events
            capi_result |= capi_i2s_raise_media_fmt_event(me_ptr);
         }

         AR_MSG(DBG_HIGH_PRIO, "CAPI_I2S: Received Src Channel Map Cfg");*/
         break;
      }
      case INTF_EXTN_PARAM_ID_IMCL_PORT_OPERATION:
      {
         /*if (NULL == params_ptr->data_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S: Set param id 0x%lX, received null buffer", param_id);

            capi_result |= CAPI_EBADPARAM;

            break;
         }

         /** Cache previous drift link state
         bool_t was_ctrl_link_connected = me_ptr->ctrl_port_cfg.is_drift_link_connected ? TRUE : FALSE;

     /** Set the control port operation
         if (CAPI_EOK != (capi_result = capi_hw_intf_cmn_imcl_port_operation(&me_ptr->ctrl_port_cfg,
                                                                             &me_ptr->cb_info,
                                                                             port_info_ptr,
                                                                             params_ptr,
                                                                             me_ptr->heap_mem.heap_id)))
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S :Failed to set control port operation, result %d", capi_result);
         }
         else
      {
         // Check if STM module is enabled
        if (TRUE == me_ptr->ctrl_port_cfg.is_stm_enabled)
        {
               // Check is there any change in drift link connection state
               if (was_ctrl_link_connected != me_ptr->ctrl_port_cfg.is_drift_link_connected)
               {
                  /** Reset drift tracking struct
                  tdu_reset_drift_info(&me_ptr->drift_info); // under mutex lock

                  /* Chooses intended update period
                  tdu_cfg_drift_cal_params(&me_ptr->drift_info,
                                           (me_ptr->ctrl_port_cfg.is_drift_link_connected)
                                              ? TDU_DEFAULT_DRIFT_CALC_PERIOD_US
                                              : TDU_DRIFT_CALC_PERIOD_US_LINK_NOT_CONNECTED,
                                           me_ptr->i2s_driver.i2s_intf_state.frame_size_us);
               }
        }
     }*/
         break;
      }
      case PARAM_ID_HW_DELAY:
      {
         /*if (NULL == params_ptr->data_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S: Set param id 0x%lX, received null buffer", param_id);

            capi_result |= CAPI_EBADPARAM;

            break;
         }

         if (param_size < sizeof(param_id_hw_delay_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "CAPI_I2S: SetParam 0x%lx, invalid param size %lu ",
                   param_id,
                   params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
            break;
         }

         param_id_hw_delay_t *hw_delay_cfg_ptr = (param_id_hw_delay_t *)params_ptr->data_ptr;

         me_ptr->i2s_driver.i2s_intf_state.hw_delay_us = hw_delay_cfg_ptr->hw_delay_us;

         // In case HW delay is rcvd. after M.F raise algo delay again with HW delay included.
         if (me_ptr->i2s_driver.i2s_intf_state.ep_mf_received)
         {
            capi_result = capi_i2s_raise_thresh_delay_events(me_ptr);
            if (CAPI_EOK != capi_result)
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_I2S :Failed to raise output media fmt and algo delay event", capi_result);
            }
         }

         AR_MSG(DBG_HIGH_PRIO,
                "CAPI_I2S: Received PARAM_ID_HW_DELAY with delay %d us",
                hw_delay_cfg_ptr->hw_delay_us);*/
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Set, unsupported param ID 0x%x", (int)param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
      }

   } /** End of switch (param ID) */

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_nxp_device_end
  DESCRIPTION: Returns the library to the uninitialized state and frees the
  memory that was allocated by init(). This function also frees the virtual
  function table.
  -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_end(capi_t *_pif)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == _pif)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: capi_nxp_device_end received bad pointer, 0x%p", _pif);
      return CAPI_EBADPARAM;
   }

   capi_nxp_device_t *me_ptr = (capi_nxp_device_t *)_pif;

   capi_nxp_device_stop(me_ptr);

   capi_nxp_device_reset(me_ptr);

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_nxp_device_get_properties
  DESCRIPTION: Function to get the properties for the nxp_device module
 * -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_get_properties(capi_t *_pif,
                                 capi_proplist_t *proplist_ptr)
{
   return capi_nxp_device_process_get_properties((capi_nxp_device_t *)_pif, proplist_ptr);
}

/*------------------------------------------------------------------------
  Function name: capi_nxp_device_set_properties
  DESCRIPTION: Function to set the properties for the nxp_device module
 * -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_set_properties(capi_t *_pif,
                                 capi_proplist_t *proplist_ptr)
{
   return capi_nxp_device_process_set_properties((capi_nxp_device_t *)_pif, proplist_ptr);
}


/*---------------------------------------------------------------------
  Function name: capi_nxp_device_update_dataflow_state
  DESCRIPTION: .
 -----------------------------------------------------------------------*/
bool_t capi_nxp_device_update_dataflow_state(capi_stream_data_t *input, nxp_device_data_flow_state *data_flow_state, bool is_data_valid)
{
   bool_t is_not_steady_state = TRUE;
   if (input && (TRUE == input->flags.marker_eos))
   {
      *data_flow_state = DF_STOPPING;
   }
   else if (input && input->buf_ptr[0].data_ptr && is_data_valid)
   {
      *data_flow_state   = DF_STARTED;
      is_not_steady_state = FALSE;
   }
   else if (*data_flow_state == DF_STOPPING)
   {
      *data_flow_state = DF_STOPPED;
   }
   return is_not_steady_state;
}

/*---------------------------------------------------------------------
  Function name: capi_nxp_device_check_data_sufficiency
  DESCRIPTION: .
 -----------------------------------------------------------------------*/
bool_t capi_nxp_device_check_data_sufficiency(capi_stream_data_t *input,
                                    capi_buf_t *      scratch_buf,
                                    uint32_t         total_bytes,
                                    bool_t           packetized,
                                    bool_t           is_capi_in_media_fmt_set,
                                    uint32_t         expected_data_len,
                                    uint16_t         num_channels,
                                    bool_t *         need_to_underrun)
{
   bool_t is_data_valid = TRUE;

   return is_data_valid;
}
