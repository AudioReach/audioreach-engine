/**
 * \file capi_splitter_utils.c
 * \brief
 *     Source file to implement utility functions called by the CAPI Interface for Simple Splitter (SPLITTER) Module.
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_splitter_utils.h"
#include "ar_msg.h"
/*==========================================================================
  MACROS
========================================================================== */
#define SPLITTER_KPPS_MONO_UNDER_48K 40
#define SPLITTER_KPPS_MONO_48K 90
/*==========================================================================
  Function Definitions
========================================================================== */
capi_err_t capi_splitter_update_and_raise_kpps_bw_event(capi_splitter_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   uint32_t   kpps        = 0;
   uint32_t   bw          = 0;

   if (32000 >= me_ptr->operating_mf.format.sampling_rate)
   {
      kpps = SPLITTER_KPPS_MONO_UNDER_48K;
   }
   else
   {
      kpps = SPLITTER_KPPS_MONO_48K;
   }
   kpps *= me_ptr->operating_mf.format.num_channels;
   bw    = me_ptr->operating_mf.format.sampling_rate *
                    me_ptr->operating_mf.format.num_channels *
                     (me_ptr->operating_mf.format.bits_per_sample >> 3);
					       // derived the equation from mux_demux module
   
   me_ptr->events_config.splitter_bw = bw;
   capi_result = capi_cmn_update_bandwidth_event(&me_ptr->cb_info, 0, me_ptr->events_config.splitter_bw);
  
   if (kpps != me_ptr->events_config.splitter_kpps)
   {
      me_ptr->events_config.splitter_kpps = kpps;
      capi_result |= capi_cmn_update_kpps_event(&me_ptr->cb_info, me_ptr->events_config.splitter_kpps);
   }

   return capi_result;
}

void capi_splitter_update_port_md_flag(capi_splitter_t *me_ptr, uint32_t port_index)
{
   uint32_t *wl_arr = NULL, num_metadata = 0;

   if (!me_ptr->out_port_md_prop_cfg_ptr)
   {
      return;
   }

   if (TRUE == capi_splitter_check_if_port_cache_cfg_rcvd_get_wl_info(me_ptr,
                                                                      me_ptr->out_port_state_arr[port_index].port_id,
                                                                      &wl_arr,
                                                                      &num_metadata))
   {
      bool_t is_eos_disable = capi_splitter_is_md_blcoked_wl((uint32_t *)wl_arr, num_metadata, MODULE_CMN_MD_ID_EOS);

      AR_MSG(DBG_HIGH_PRIO,
             "Eos Disable %d on port 0x%lx",
             is_eos_disable,
             me_ptr->out_port_state_arr[port_index].port_id);

      me_ptr->out_port_state_arr[port_index].flags.is_eos_disable    = is_eos_disable;
      me_ptr->out_port_state_arr[port_index].flags.is_all_md_blocked = (num_metadata == 0) ? TRUE : FALSE;
   }
}

capi_err_t capi_splitter_check_if_any_port_is_open_and_update_eos_flag(capi_splitter_t *me_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (!me_ptr->out_port_md_prop_cfg_ptr)
   {
      return result;
   }
   for (uint32_t i = 0; i < me_ptr->num_out_ports; i++)
   {
      if (DATA_PORT_STATE_CLOSED != me_ptr->out_port_state_arr[i].state)
      {
         capi_splitter_update_port_md_flag(me_ptr, i);
      }
   }
   return result;
}

void capi_splitter_update_port_ts_flag(capi_splitter_t *me_ptr, uint32_t out_port_index)
{
   if (!me_ptr->out_port_ts_prop_cfg_ptr || out_port_index >= me_ptr->num_out_ports)
   {
      return;
   }

   per_port_ts_cfg_t *per_port_ts_cfg_arr = (per_port_ts_cfg_t *)(me_ptr->out_port_ts_prop_cfg_ptr + 1);

   for (uint32_t i = 0; i < me_ptr->out_port_ts_prop_cfg_ptr->num_ports; i++)
   {
      if (me_ptr->out_port_state_arr[out_port_index].port_id == per_port_ts_cfg_arr[i].port_id)
      {
         me_ptr->out_port_state_arr[out_port_index].flags.ts_cfg = per_port_ts_cfg_arr[i].ts_configuration;

         AR_MSG(DBG_HIGH_PRIO,
                "output port id %lu, timestamp propagation configuration %lu [0: input, 1: block: 2: hw-ts]",
		me_ptr->out_port_state_arr[out_port_index].port_id,
                me_ptr->out_port_state_arr[out_port_index].flags.ts_cfg);
         return;
      }
   }
}
capi_err_t capi_splitter_update_all_opened_port_ts_config_flag(capi_splitter_t *me_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (!me_ptr->out_port_ts_prop_cfg_ptr)
   {
      return result;
   }
   for (uint32_t i = 0; i < me_ptr->num_out_ports; i++)
   {
      if (DATA_PORT_STATE_CLOSED != me_ptr->out_port_state_arr[i].state)
      {
         capi_splitter_update_port_ts_flag(me_ptr, i);
      }
   }
   return result;
}

static void capi_splitter_update_tgp(capi_splitter_t *me_ptr)
{
   if (NULL == me_ptr->tgp.tg_policy_cb.change_data_trigger_policy_cb_fn)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi splitter:  callback is not set. Unable to raise trigger policy!");
      return;
   }

   capi_err_t capi_result  = CAPI_EOK;
   bool_t     is_any_rt_ds = FALSE, is_any_nrt_ds = FALSE;

   fwk_extn_port_trigger_affinity_t   inp_affinity[SPLITTER_MAX_INPUT_PORTS];
   fwk_extn_port_nontrigger_policy_t  inp_nontgp[SPLITTER_MAX_INPUT_PORTS];
   fwk_extn_port_trigger_affinity_t * out_affinity      = me_ptr->tgp.out_port_triggerable_affinity_arr;
   fwk_extn_port_nontrigger_policy_t *out_nontgp        = me_ptr->tgp.out_port_nontriggerable_policy_arr;
   fwk_extn_port_trigger_group_t      triggerable_group = {.in_port_grp_affinity_ptr = inp_affinity,
                                                      .out_port_grp_affinity_ptr     = out_affinity };
   fwk_extn_port_nontrigger_group_t nontriggerable_group = {.in_port_grp_policy_ptr  = inp_nontgp,
                                                            .out_port_grp_policy_ptr = out_nontgp };

   // default initialization
   for (uint32_t i = 0; i < SPLITTER_MAX_INPUT_PORTS; i++)
   {
      inp_affinity[i] = FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;
      inp_nontgp[i]   = FWK_EXTN_PORT_NON_TRIGGER_INVALID;
   }

   // default initialization
   for (uint32_t i = 0; i < me_ptr->num_out_ports; i++)
   {
      if (DATA_PORT_STATE_STARTED == me_ptr->out_port_state_arr[i].state)
      {
         out_affinity[i] = FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;
         out_nontgp[i]   = FWK_EXTN_PORT_NON_TRIGGER_INVALID;
         if (me_ptr->out_port_state_arr[i].flags.is_ds_rt)
         {
            is_any_rt_ds = TRUE;
         }
         else
         {
            is_any_nrt_ds = TRUE;
         }
      }
      else
      {
         out_affinity[i] = FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE;
         out_nontgp[i]   = FWK_EXTN_PORT_NON_TRIGGER_INVALID;

         AR_MSG(DBG_HIGH_PRIO,
                  "capi splitter: 0x%lx: out port id[%lu] %lu is not started",
                  me_ptr->miid,
                  i,
                  me_ptr->out_port_state_arr[i].port_id);
      }
   }
    
   #ifdef SPLITTER_DBG_LOW
      if(me_ptr->input_port_state == DATA_PORT_STATE_STARTED)
      {
         AR_MSG(DBG_HIGH_PRIO,"capi splitter: Input port is in start state");
      }
      else
      {
         AR_MSG(DBG_HIGH_PRIO,"capi splitter: Input port is not in start state");
      }
   #endif
   
   // Set to non-default TGP if,
   //   a)Input port is in start state then only tgp should be raised again based output ports state
   //   b)If there are mix of RT and NRT output ports and any of the output port is in start state
   // Set to default TGP if,
   //   a) If input port is in stopped/closed state
   //   b) if all downstreams are rt or all are nrt then set to default TGP
   //   c) if all the outputs are stopped/closed, then all ports are neither RT or NRT.
   bool_t set_to_default_tgp = TRUE;
   if ((me_ptr->input_port_state == DATA_PORT_STATE_STARTED) && (is_any_rt_ds && is_any_nrt_ds))
   {
	   for (uint32_t i = 0; i < me_ptr->num_out_ports; i++)
       {
           if (DATA_PORT_STATE_STARTED == me_ptr->out_port_state_arr[i].state)
           {
               set_to_default_tgp = FALSE;
			   break;			 
	       }
       }
   }

   AR_MSG(DBG_HIGH_PRIO,
          "capi splitter: 0x%lx: trigger policy updated. set_to_default_tgp=%lu",
          me_ptr->miid,
          set_to_default_tgp);

   // update port level trigger policy only if TGP is not default
   if (!set_to_default_tgp)
   {
      for (uint32_t i = 0; i < me_ptr->num_out_ports; i++)
      {
         if (DATA_PORT_STATE_STARTED == me_ptr->out_port_state_arr[i].state)
         {
            if (me_ptr->out_port_state_arr[i].flags.is_ds_rt)
            {
               out_affinity[i] = FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;

               AR_MSG(DBG_HIGH_PRIO,
                      "capi splitter: 0x%lx: trigger policy update, out port id[%lu] %lu is mandatory.",
                      me_ptr->miid,
                      i,
                      me_ptr->out_port_state_arr[i].port_id);
            }
            else
            {
               out_affinity[i] = FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE;
               out_nontgp[i]   = FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL;

               AR_MSG(DBG_HIGH_PRIO,
                      "capi splitter: 0x%lx: trigger policy update, out port id[%lu] %lu is optional.",
                      me_ptr->miid,
                      i,
                      me_ptr->out_port_state_arr[i].port_id);
            }
         }
      }

      capi_result = me_ptr->tgp.tg_policy_cb.change_data_trigger_policy_cb_fn(me_ptr->tgp.tg_policy_cb.context_ptr,
                                                                              &nontriggerable_group,
                                                                              FWK_EXTN_PORT_TRIGGER_POLICY_MANDATORY,
                                                                              1,
                                                                              &triggerable_group);

   }
   else // raise NULL ptrs to reset to default tgp
   {
      capi_result = me_ptr->tgp.tg_policy_cb.change_data_trigger_policy_cb_fn(me_ptr->tgp.tg_policy_cb.context_ptr,
                                                                        NULL,
                                                                        FWK_EXTN_PORT_TRIGGER_POLICY_MANDATORY,
                                                                        0,
                                                                        NULL);
   }

   if (CAPI_SUCCEEDED(capi_result))
   {
      AR_MSG(DBG_HIGH_PRIO,
             "capi splitter: 0x%lx: trigger policy updated. set_to_default_tgp=%lu",
             me_ptr->miid,
             set_to_default_tgp);
   }
}

capi_err_t capi_splitter_update_is_rt_property(capi_splitter_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   /*
    * -upstream RT/FTRT state is same for all output ports.
    * -It is RT if input port's upstream is RT.
    * -It is FTRT if input ports' upstream is FTRT
    */

   bool_t is_out_us_rt_state = me_ptr->flags.is_us_rt;

   for (uint32_t i = 0; i < me_ptr->num_out_ports; i++)
   {
      capi_buf_t                               payload;
      intf_extn_param_id_is_rt_port_property_t event;
      event.is_input = FALSE;
      event.is_rt    = is_out_us_rt_state;

      payload.data_ptr        = (int8_t *)&event;
      payload.actual_data_len = payload.max_data_len = sizeof(event);

      if (DATA_PORT_STATE_CLOSED != me_ptr->out_port_state_arr[i].state)
      {
         if (is_out_us_rt_state != me_ptr->out_port_state_arr[i].flags.is_us_rt)
         {
            me_ptr->out_port_state_arr[i].flags.is_us_rt = is_out_us_rt_state;
            event.port_index                             = i;
            capi_result |=
               capi_cmn_raise_data_to_dsp_svc_event(&me_ptr->cb_info, INTF_EXTN_EVENT_ID_IS_RT_PORT_PROPERTY, &payload);
         }
      }
   }

   /*
    * -If any output port's downstream is RT then input port's downstream is RT.
    * -If all output port's downstream is FTRT then input port's downstream is FTRT.
    */

   bool_t in_ds_rt_state = FALSE;
   for (uint32_t i = 0; i < me_ptr->num_out_ports; i++)
   {
      if (DATA_PORT_STATE_CLOSED != me_ptr->out_port_state_arr[i].state)
      {
         if (me_ptr->out_port_state_arr[i].flags.is_ds_rt)
         {
            in_ds_rt_state = TRUE;
            break;
         }
      }
   }

   //Always raise the event irrespective of a change(or not). This is because ports may have closed and opened and may be out of sync from module's state.
   if (in_ds_rt_state != me_ptr->flags.is_ds_rt)
   {
      capi_buf_t                               payload;
      intf_extn_param_id_is_rt_port_property_t event;
      event.is_input = TRUE;
      event.is_rt    = in_ds_rt_state;

      payload.data_ptr        = (int8_t *)&event;
      payload.actual_data_len = payload.max_data_len = sizeof(event);
      me_ptr->flags.is_ds_rt                         = in_ds_rt_state;
      event.port_index                               = 0;
      capi_result |=
         capi_cmn_raise_data_to_dsp_svc_event(&me_ptr->cb_info, INTF_EXTN_EVENT_ID_IS_RT_PORT_PROPERTY, &payload);
   }

   if (CAPI_FAILED(capi_result))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Port property event failed.");
   }

   // Check and update tgp
   capi_splitter_update_tgp(me_ptr);

   return CAPI_EOK;
}

/**
 * Handles data port property propagation and raises event.
 */
capi_err_t capi_splitter_set_data_port_property(capi_splitter_t *me_ptr, capi_buf_t *payload_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == payload_ptr->data_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Set port property, received null buffer");
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   // Validate length.
   if (sizeof(intf_extn_param_id_is_rt_port_property_t) > payload_ptr->actual_data_len)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Set port property, Bad param size %lu", payload_ptr->actual_data_len);
      CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
      return capi_result;
   }

   intf_extn_param_id_is_rt_port_property_t *data_ptr =
      (intf_extn_param_id_is_rt_port_property_t *)(payload_ptr->data_ptr);

   AR_MSG(DBG_HIGH_PRIO,
          "capi_splitter: 0x%lx: data port property set param "
          "is_input %lu, port index %lu, is_rt %lu",
          me_ptr->miid,
          data_ptr->is_input,
          data_ptr->port_index,
          data_ptr->is_rt);

   if (data_ptr->is_input)
   {
      me_ptr->flags.is_us_rt = data_ptr->is_rt;
   }
   else
   {
      if (data_ptr->port_index < me_ptr->num_out_ports)
      {
         me_ptr->out_port_state_arr[data_ptr->port_index].flags.is_ds_rt = data_ptr->is_rt;
      }
   }

   capi_result |= capi_splitter_update_is_rt_property(me_ptr);

   return capi_result;
}
