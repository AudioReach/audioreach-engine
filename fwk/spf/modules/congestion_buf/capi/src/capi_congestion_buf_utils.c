/**
 *   \file capi_congestion_buf_utils.c
 *   \brief
 *        This file contains CAPI implementation of RT Proxy module.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_congestion_buf_i.h"

/*==============================================================================
   Public Function Implementation
==============================================================================*/

/* raise capi output media format after receiving valid input media format */
capi_err_t congestion_buf_raise_output_media_format_event(capi_congestion_buf_t *me_ptr)
{
   capi_err_t        result = CAPI_EOK;
   capi_event_info_t event_info;

   if (!me_ptr->is_deint)
   {
      capi_cmn_raw_media_fmt_t mf;
      mf.format.bitstream_format          = me_ptr->bitstream_format;
      mf.header.format_header.data_format = CAPI_RAW_COMPRESSED;

      event_info.port_info.is_valid      = 1;
      event_info.port_info.is_input_port = 0;
      event_info.port_info.port_index    = 0;

      event_info.payload.actual_data_len = sizeof(capi_cmn_raw_media_fmt_t);
      event_info.payload.data_ptr        = (int8_t *)&mf;
      event_info.payload.max_data_len    = sizeof(capi_cmn_raw_media_fmt_t);

      result = me_ptr->event_cb_info.event_cb(me_ptr->event_cb_info.event_context,
                                              CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED_V2,
                                              &event_info);
   }
   else
   {
      event_info.port_info.is_valid      = 1;
      event_info.port_info.is_input_port = 0;
      event_info.port_info.port_index    = 0;

      event_info.payload.actual_data_len = sizeof(capi_set_get_media_format_t) +
                                           sizeof(capi_deinterleaved_raw_compressed_data_format_t) +
                                           sizeof(capi_channel_mask_t) * me_ptr->mf.deint_raw.bufs_num;
      event_info.payload.data_ptr     = (int8_t *)&me_ptr->mf;
      event_info.payload.max_data_len = sizeof(capi_deint_mf_combined_t);

      result = me_ptr->event_cb_info.event_cb(me_ptr->event_cb_info.event_context,
                                              CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED_V2,
                                              &event_info);
   }

   if (CAPI_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Failed to raise event for output media format");
   }

   return result;
}

/* Congestion buffer is in a signal triggered module requesting
 * data trigger policy at init time */
capi_err_t capi_congestion_buf_event_dt_in_st_cntr(capi_congestion_buf_t *me_ptr)
{
   capi_err_t                                  result = CAPI_EOK;
   capi_buf_t                                  payload;
   fwk_extn_event_id_data_trigger_in_st_cntr_t event;

   event.is_enable             = TRUE;
   event.needs_input_triggers  = TRUE;
   event.needs_output_triggers = TRUE;

   payload.data_ptr        = (int8_t *)&event;
   payload.actual_data_len = payload.max_data_len = sizeof(event);

   result =
      capi_cmn_raise_data_to_dsp_svc_event(&me_ptr->event_cb_info, FWK_EXTN_EVENT_ID_DATA_TRIGGER_IN_ST_CNTR, &payload);

   if (CAPI_FAILED(result))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to raise event to enable data_trigger.");
      return result;
   }
   else
   {
      AR_MSG(DBG_HIGH_PRIO, "raised event to enable data_trigger.");
   }

   return result;
}

/* Congestion Buffer is triggered whenever there is input to read
 * or whenever there is output requirement to write. */
void congestion_buf_change_trigger_policy(capi_congestion_buf_t *me_ptr)
{

   fwk_extn_port_nontrigger_group_t nontriggerable_ports = { 0 };
   fwk_extn_port_trigger_affinity_t input_group1         = { FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT };
   fwk_extn_port_trigger_affinity_t output_group1        = { FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT };

   fwk_extn_port_trigger_group_t triggerable_groups[1];
   triggerable_groups[0].in_port_grp_affinity_ptr  = &input_group1;
   triggerable_groups[0].out_port_grp_affinity_ptr = &output_group1;

   me_ptr->policy_chg_cb.change_data_trigger_policy_cb_fn(me_ptr->policy_chg_cb.context_ptr,
                                                          &nontriggerable_ports,
                                                          FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL,
                                                          1,
                                                          triggerable_groups);
   return;
}

/* Congestion buffer is triggered at signal trigger also. If there was an output data trigger
 * right before signal trigger then the data in output buffer should not be reset. So output
 * must be ignored on signal trigger. But the data provided by depack on signal trigger should
 * not be dropped  - so input is consumed on signal trigger. */
void congestion_buf_change_signal_trigger_policy(capi_congestion_buf_t *me_ptr)
{

   fwk_extn_port_nontrigger_policy_t input_group2  = { FWK_EXTN_PORT_NON_TRIGGER_INVALID };
   fwk_extn_port_nontrigger_policy_t output_group2 = { FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL };

   fwk_extn_port_nontrigger_group_t nontriggerable_group[1];
   nontriggerable_group[0].in_port_grp_policy_ptr  = &input_group2;
   nontriggerable_group[0].out_port_grp_policy_ptr = &output_group2;

   fwk_extn_port_trigger_affinity_t input_group1  = { FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT };
   fwk_extn_port_trigger_affinity_t output_group1 = { FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE };

   fwk_extn_port_trigger_group_t triggerable_groups[1];
   triggerable_groups[0].in_port_grp_affinity_ptr  = &input_group1;
   triggerable_groups[0].out_port_grp_affinity_ptr = &output_group1;

   // By default set the mode to RT, when the write arrives then make it FTRT.
   me_ptr->policy_chg_cb.change_signal_trigger_policy_cb_fn(me_ptr->policy_chg_cb.context_ptr,
                                                            nontriggerable_group,
                                                            FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL,
                                                            1,
                                                            triggerable_groups);

   return;
}

/* Configures the buffer when parameters are available and creates it */
capi_err_t capi_congestion_buf_init_create_buf(capi_congestion_buf_t *me_ptr, bool_t is_debug)
{
   capi_err_t result = CAPI_EOK;

   if (me_ptr->driver_hdl.stream_buf)
   {
      /* If the circular buffer is already created then destroy and recreate
       * only if this is due to debug param id. Otherwise return. */
      if (is_debug)
      {
         if (AR_EOK != (result = congestion_buf_driver_deinit(me_ptr)))
         {
            AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Cannot destroy driver. ");
            return CAPI_EFAILED;
         }
      }
      else
      {
         return result;
      }
   }

   /* If debug value is present that takes precedence */
   if (me_ptr->debug_ms)
   {
      me_ptr->cfg_ptr.congestion_buffer_duration_ms = me_ptr->debug_ms;
   }

   /* Based on the configuration set the congestion max size */
   me_ptr->congestion_size_bytes_max =
      (me_ptr->cfg_ptr.bit_rate >> 3) * me_ptr->cfg_ptr.congestion_buffer_duration_ms / 1000;

   /* If bitrate is unknown or zero then use max lpi storage */
   if (0 == me_ptr->congestion_size_bytes_max)
   {
      me_ptr->congestion_size_bytes_max = CONGESTION_BUF_RAW_BUF_MAX_MEMORY;
   }

   /* If MTU is provided use that - otherwise max mtu */
   me_ptr->raw_data_len = (me_ptr->cfg_ptr.mtu_size > 0) ? me_ptr->cfg_ptr.mtu_size : CONGESTION_BUFFER_MAX_MTU_SIZE;

   /* Initialize Circular Buffer */
   if (!me_ptr->driver_hdl.stream_buf)
   {
      ar_result_t result = AR_EOK;
      if (AR_EOK != (result = congestion_buf_driver_init(me_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Cannot intialize the driver. ");
         return CAPI_EFAILED;
      }
   }

   /* Create circular buffer */
   result = spf_circ_buf_raw_resize(me_ptr->driver_hdl.writer_handle, me_ptr->raw_data_len);

   return result;
}
