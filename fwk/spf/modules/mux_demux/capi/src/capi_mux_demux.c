/**
 * \file capi_mux_demux.c
 *
 * \brief
 *        CAPI for mux demux module.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_mux_demux.h"
#include "capi_mux_demux_utils.h"

/*------------------------------------------------------------------------
 * Static declarations
 * -----------------------------------------------------------------------*/

extern capi_vtbl_t mux_demux_vtbl;

/*----------------------------------------------------------------------------
 * Function Definitions
 * -------------------------------------------------------------------------*/
capi_err_t capi_mux_demux_get_static_properties(capi_proplist_t *init_props_ptr, capi_proplist_t *out_props_ptr)
{
   return capi_mux_demux_get_properties(NULL, out_props_ptr);
}

capi_err_t capi_mux_demux_init(capi_t *_pif, capi_proplist_t *init_props_ptr)
{
   capi_err_t        result = CAPI_EOK;
   capi_mux_demux_t *me_ptr = (capi_mux_demux_t *)_pif;

   result |= (NULL == _pif) ? CAPI_EFAILED : result;
   CHECK_ERR_AND_RETURN(result, "Init received null pointer.");

   memset(me_ptr, 0, sizeof(capi_mux_demux_t));
   me_ptr->heap_id = POSAL_HEAP_DEFAULT;

   if (init_props_ptr)
   {
      result = capi_mux_demux_set_properties(_pif, init_props_ptr);

      // Ignoring non-fatal error code.
      result ^= (result & CAPI_EUNSUPPORTED);
      CHECK_ERR_AND_RETURN(result, "Init set properties failed.");
   }

   me_ptr->vtbl = &mux_demux_vtbl;

#ifdef MUX_DEMUX_INTERLEAVED_DATA_WORKAROUND
   me_ptr->data_interleaving   = CAPI_DEINTERLEAVED_UNPACKED_V2;
#endif

   result |= capi_cmn_raise_deinterleaved_unpacked_v2_supported_event(&me_ptr->cb_info);

   AR_MSG(DBG_LOW_PRIO, "Init done, status 0x%x.", result);

   return result;
}

// check if overlap connections are present or not.
static bool_t is_overlap_connections(const param_id_mux_demux_config_t *config_ptr)
{
   const mux_demux_connection_config_t *config_connection_arr = (const mux_demux_connection_config_t *)(config_ptr + 1);
   for (uint32_t i = 0; i < config_ptr->num_of_connections; i++)
   {
      for (uint32_t j = i + 1; j < config_ptr->num_of_connections; j++)
      {
         if (config_connection_arr[i].output_port_id == config_connection_arr[j].output_port_id &&
             config_connection_arr[i].output_channel_index == config_connection_arr[j].output_channel_index)
         {
            return TRUE;
         }
      }
   }

   return FALSE;
}

capi_err_t capi_mux_demux_set_param(capi_t *                _pif,
                                    uint32_t                param_id,
                                    const capi_port_info_t *port_info_ptr,
                                    capi_buf_t *            params_ptr)
{
   capi_err_t        result = CAPI_EOK;
   capi_mux_demux_t *me_ptr = (capi_mux_demux_t *)_pif;

   result |= (NULL == _pif || NULL == params_ptr || NULL == params_ptr->data_ptr) ? CAPI_EFAILED : result;
   CHECK_ERR_AND_RETURN(result, "setparam received bad pointer.");

   switch (param_id)
   {
      case INTF_EXTN_PARAM_ID_METADATA_HANDLER:
      {
         result |=
            (params_ptr->actual_data_len < sizeof(intf_extn_param_id_metadata_handler_t)) ? CAPI_ENEEDMORE : result;
         CHECK_ERR_AND_RETURN(result, "Insufficient size for metadata handler setparam.");

         intf_extn_param_id_metadata_handler_t *payload_ptr =
            (intf_extn_param_id_metadata_handler_t *)params_ptr->data_ptr;
         me_ptr->metadata_handler = *payload_ptr;
         break;
      }
      case INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION:
      {
         const intf_extn_data_port_operation_t *port_config_ptr =
            (intf_extn_data_port_operation_t *)(params_ptr->data_ptr);

         if ((params_ptr->actual_data_len < sizeof(intf_extn_data_port_operation_t)) ||
             (params_ptr->actual_data_len < sizeof(intf_extn_data_port_operation_t) +
                                               (port_config_ptr->num_ports * sizeof(intf_extn_data_port_id_idx_map_t))))
         {
            AR_MSG(DBG_ERROR_PRIO, "Insufficient size for port operation param.");
            return CAPI_ENEEDMORE;
         }
         result = capi_mux_demux_port_operation(me_ptr, port_config_ptr);
         break;
      }
      case PARAM_ID_MUX_DEMUX_CONFIG:
      {
         const param_id_mux_demux_config_t *config_ptr = (param_id_mux_demux_config_t *)(params_ptr->data_ptr);
         uint32_t                           required_payload_size = sizeof(param_id_mux_demux_config_t);

         if ((params_ptr->actual_data_len < required_payload_size) ||
             (params_ptr->actual_data_len <
              required_payload_size + (config_ptr->num_of_connections * sizeof(mux_demux_connection_config_t))))
         {
            AR_MSG(DBG_ERROR_PRIO, "Insufficient size for mux demux config param.");
            return CAPI_ENEEDMORE;
         }

         // This check should be removed to support mixing use case
         if (is_overlap_connections(config_ptr))
         {
            AR_MSG(DBG_ERROR_PRIO, "overlap config received. mixing use case is not yet supported.");
            return CAPI_EBADPARAM;
         }

         required_payload_size += (config_ptr->num_of_connections * sizeof(mux_demux_connection_config_t));
         if (me_ptr->cached_config_ptr)
         {
            posal_memory_free(me_ptr->cached_config_ptr);
            me_ptr->cached_config_ptr = NULL;
         }

         me_ptr->cached_config_ptr =
            (param_id_mux_demux_config_t *)posal_memory_malloc(required_payload_size, (POSAL_HEAP_ID)me_ptr->heap_id);

         if (NULL == me_ptr->cached_config_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "failed in malloc");
            return CAPI_ENOMEMORY;
         }

         memscpy(me_ptr->cached_config_ptr, required_payload_size, config_ptr, params_ptr->actual_data_len);

         result = capi_mux_demux_update_connection(me_ptr);

         AR_MSG(DBG_HIGH_PRIO, "connection config setparam done.");

         break;
      }
      case PARAM_ID_MUX_DEMUX_OUT_FORMAT:
      {
         const param_id_mux_demux_out_format_t *config_ptr = (param_id_mux_demux_out_format_t *)(params_ptr->data_ptr);
         uint32_t                               required_payload_size = sizeof(param_id_mux_demux_out_format_t);
         mux_demux_out_format_t *               out_format_ptr        = NULL;

         if ((params_ptr->actual_data_len < required_payload_size) ||
             (params_ptr->actual_data_len <
              required_payload_size + (config_ptr->num_config * sizeof(mux_demux_out_format_t))))
         {
            AR_MSG(DBG_ERROR_PRIO, "Insufficient size for mux demux out format param.");
            return CAPI_ENEEDMORE;
         }

         for (uint32_t i = 0; i < config_ptr->num_config; i++)
         {
            uint32_t out_port_arr_index = me_ptr->num_of_output_ports;

            out_format_ptr = (mux_demux_out_format_t *)(params_ptr->data_ptr + required_payload_size);

            required_payload_size += sizeof(mux_demux_out_format_t) + (out_format_ptr->num_channels * sizeof(uint16_t));
            if (params_ptr->actual_data_len < required_payload_size)
            {
               AR_MSG(DBG_ERROR_PRIO, "Insufficient size for mux demux out format param.");
               return CAPI_ENEEDMORE;
            }
            required_payload_size = CAPI_ALIGN_4_BYTE(required_payload_size); // in case if num_channels are odd

            out_port_arr_index = capi_mux_demux_get_output_arr_index_from_port_id(me_ptr, out_format_ptr->output_port_id);
            if (out_port_arr_index >= me_ptr->num_of_output_ports)
            {
               AR_MSG(DBG_ERROR_PRIO, "Output port config array was full, not able to find the free index");
               continue;
            }

            result |=
                ((CAPI_MAX_CHANNELS_V2 < out_format_ptr->num_channels) ||
                 !((out_format_ptr->bits_per_sample == BIT_WIDTH_16 &&
                    out_format_ptr->q_factor == PCM_Q_FACTOR_15) ||
                   (out_format_ptr->bits_per_sample == BIT_WIDTH_32 &&
                    (out_format_ptr->q_factor == PCM_Q_FACTOR_27 ||
                     out_format_ptr->q_factor == PCM_Q_FACTOR_31))))
                    ? CAPI_EBADPARAM
                    : CAPI_EOK;

            if (CAPI_FAILED(result))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "Invalid media format received for output port array index 0x%x port id 0x%x. "
                      "bps=%u, q_factor=%u, num_channels=%u",
                      out_port_arr_index,
                      out_format_ptr->output_port_id,
                      out_format_ptr->bits_per_sample,
                      out_format_ptr->q_factor,
                      out_format_ptr->num_channels);
               continue;
            }

            me_ptr->output_port_info_ptr[out_port_arr_index].fmt.bits_per_sample  = out_format_ptr->bits_per_sample;
            me_ptr->output_port_info_ptr[out_port_arr_index].fmt.q_factor         = out_format_ptr->q_factor;
            me_ptr->output_port_info_ptr[out_port_arr_index].fmt.num_max_channels = out_format_ptr->num_channels;

            memscpy(&me_ptr->output_port_info_ptr[out_port_arr_index].fmt.channel_type[0],
                    sizeof(me_ptr->output_port_info_ptr[out_port_arr_index].fmt.channel_type),
                    (out_format_ptr + 1),
                    out_format_ptr->num_channels * sizeof(uint16_t));

            capi_mux_demux_raise_out_port_media_format_event(me_ptr, out_port_arr_index);
         }
         break;
      }
      case PARAM_ID_MUX_DEMUX_TS_PROPAGATION:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_mux_demux_ts_propagation_t))
         {
            AR_MSG(DBG_ERROR_PRIO, "Insufficient size for mux demux TS Prop param.");
            return CAPI_ENEEDMORE;
         }
         param_id_mux_demux_ts_propagation_t *config_ptr =
            (param_id_mux_demux_ts_propagation_t *)(params_ptr->data_ptr);
         me_ptr->enable_ts_propagation = config_ptr->enable_ts_propagation;
         AR_MSG(DBG_HIGH_PRIO, "Set param for TS Prop - %u", me_ptr->enable_ts_propagation);
         break;
      }

#ifdef SIM
      case FWK_EXTN_PARAM_ID_TRIGGER_POLICY_CB_FN:
      {
         if (NULL == params_ptr->data_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "Param id 0x%lx received null buffer", (uint32_t)param_id);
            result |= CAPI_EBADPARAM;
            break;
         }

         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_trigger_policy_cb_fn_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Param id 0x%lx Bad param size %lu",
                   (uint32_t)param_id,
                   params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }

         fwk_extn_param_id_trigger_policy_cb_fn_t *payload_ptr =
            (fwk_extn_param_id_trigger_policy_cb_fn_t *)params_ptr->data_ptr;
         me_ptr->tgp.tg_policy_cb = *payload_ptr;

         AR_MSG(DBG_ERROR_PRIO, "FWK_EXTN_PARAM_ID_TRIGGER_POLICY_CB_FN passed");

         break;
      }
      case PARAM_ID_MUX_DEMUX_TP_CFG:
      {

         fwk_extn_port_trigger_affinity_t  inp_affinity[me_ptr->num_of_input_ports];
         fwk_extn_port_nontrigger_policy_t inp_nontgp[me_ptr->num_of_input_ports];

         fwk_extn_port_trigger_affinity_t  out_affinity[me_ptr->num_of_output_ports];
         fwk_extn_port_nontrigger_policy_t out_nontgp[me_ptr->num_of_output_ports];

         for (uint8_t i = 0; i < me_ptr->num_of_input_ports; i++)
         {
            if (0 == i) // PARAM_ID_MUX_DEMUX_TP_CFG is only used for sim and for now raising TP for index 0 I/P port. This we did for Auto use case profiling.
            {
               inp_affinity[i] = FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;
               inp_nontgp[i]   = FWK_EXTN_PORT_NON_TRIGGER_INVALID;
            }
            else
            {
               inp_affinity[i] = FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE;
               inp_nontgp[i]   = FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL;
            }
         }

         for (uint8_t j = 0; j < me_ptr->num_of_output_ports; j++)
         {
            out_affinity[j] = FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE;
            out_nontgp[j]   = FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL;
         }

         fwk_extn_port_trigger_group_t triggerable_group = { .in_port_grp_affinity_ptr = inp_affinity, .out_port_grp_affinity_ptr = out_affinity };

         fwk_extn_port_nontrigger_group_t nontriggerable_group = { .in_port_grp_policy_ptr = inp_nontgp, .out_port_grp_policy_ptr = out_nontgp };

         result = me_ptr->tgp.tg_policy_cb.change_data_trigger_policy_cb_fn(me_ptr->tgp.tg_policy_cb.context_ptr,
                                                                            &nontriggerable_group,
                                                                            FWK_EXTN_PORT_TRIGGER_POLICY_MANDATORY,
                                                                            1,
                                                                            &triggerable_group);
         if(CAPI_EOK != result)
         {
            AR_MSG(DBG_ERROR_PRIO, "PARAM_ID_MUX_DEMUX_TP_CFG Failed");
         }
         else
         {
            AR_MSG(DBG_HIGH_PRIO, "PARAM_ID_MUX_DEMUX_TP_CFG passed");
         }

         break;
      }
#endif
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "Invalid setparam received 0x%lx", param_id);
         return CAPI_EUNSUPPORTED;
      }
   }

   return result;
}

capi_err_t capi_mux_demux_get_param(capi_t *                _pif,
                                    uint32_t                param_id,
                                    const capi_port_info_t *port_info_ptr,
                                    capi_buf_t *            params_ptr)
{
   capi_err_t        result = CAPI_EOK;
   capi_mux_demux_t *me_ptr = (capi_mux_demux_t *)_pif;

   result |= (NULL == _pif || NULL == params_ptr || NULL == params_ptr->data_ptr) ? CAPI_EFAILED : result;
   CHECK_ERR_AND_RETURN(result, "getparam received bad pointer.");

   params_ptr->actual_data_len = 0;
   AR_MSG(DBG_HIGH_PRIO, "getparam received 0x%lx", param_id);
   switch (param_id)
   {
      case PARAM_ID_MUX_DEMUX_CONFIG:
      {
         uint32_t payload_size = 0;
         if (NULL == me_ptr->cached_config_ptr)
         {
            break;
         }
         payload_size = sizeof(param_id_mux_demux_config_t) +
                        (me_ptr->cached_config_ptr->num_of_connections * sizeof(mux_demux_connection_config_t));
         if (params_ptr->max_data_len >= payload_size)
         {
            params_ptr->actual_data_len =
               memscpy(params_ptr->data_ptr, params_ptr->max_data_len, me_ptr->cached_config_ptr, payload_size);
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO, "Insufficient size for mux demux config getparam.");
            return CAPI_ENEEDMORE;
         }
         break;
      }
      case PARAM_ID_MUX_DEMUX_OUT_FORMAT:
      {
         uint32_t                         payload_size = sizeof(param_id_mux_demux_out_format_t);
         param_id_mux_demux_out_format_t *config_ptr   = (param_id_mux_demux_out_format_t *)params_ptr->data_ptr;

         if (params_ptr->max_data_len < payload_size)
         {
            AR_MSG(DBG_ERROR_PRIO, "Insufficient size for mux demux out format getparam.");
            return CAPI_ENEEDMORE;
         }

         config_ptr->num_config = 0;

         for (uint32_t i = 0; i < me_ptr->num_of_output_ports; i++)
         {
            mux_demux_out_format_t *out_fmt_ptr       = NULL;
            uint32_t                channel_type_size = 0;
            if (0 == me_ptr->output_port_info_ptr[i].fmt.num_max_channels)
            {
               continue; // config not received for this output port
            }
            channel_type_size = me_ptr->output_port_info_ptr[i].fmt.num_max_channels * sizeof(uint16_t);


            out_fmt_ptr = (mux_demux_out_format_t *)(params_ptr->data_ptr + payload_size);

            payload_size += sizeof(mux_demux_out_format_t) + channel_type_size;
            if (params_ptr->max_data_len < payload_size)
            {
               AR_MSG(DBG_ERROR_PRIO, "Insufficient size for mux demux out format getparam.");
               return CAPI_ENEEDMORE;
            }
            out_fmt_ptr->output_port_id  = me_ptr->output_port_info_ptr[i].port_id;
            out_fmt_ptr->bits_per_sample = me_ptr->output_port_info_ptr[i].fmt.bits_per_sample;
            out_fmt_ptr->q_factor        = me_ptr->output_port_info_ptr[i].fmt.q_factor;
            out_fmt_ptr->num_channels    = me_ptr->output_port_info_ptr[i].fmt.num_max_channels;
            memscpy(out_fmt_ptr + 1,
                    channel_type_size,
                    &me_ptr->output_port_info_ptr[i].fmt.channel_type[0],
                    channel_type_size);

            config_ptr->num_config++;
            // each output format will start from an aligned address.
            if ((payload_size & 0x3) && payload_size + sizeof(int16_t) <= params_ptr->max_data_len)
            {
               *((int16_t *)(params_ptr->data_ptr + payload_size)) = 0;
               payload_size                                        = CAPI_ALIGN_4_BYTE(payload_size);
            }
            params_ptr->actual_data_len = payload_size;
         }

         break;
      }
      case PARAM_ID_MUX_DEMUX_TS_PROPAGATION:
      {
         if (params_ptr->max_data_len < sizeof(param_id_mux_demux_ts_propagation_t))
         {
            AR_MSG(DBG_ERROR_PRIO, "Insufficient size for mux demux TS Prop param.");
            return CAPI_ENEEDMORE;
         }
         param_id_mux_demux_ts_propagation_t *config_ptr =
            (param_id_mux_demux_ts_propagation_t *)(params_ptr->data_ptr);
         config_ptr->enable_ts_propagation = me_ptr->enable_ts_propagation;
         params_ptr->actual_data_len = sizeof(param_id_mux_demux_ts_propagation_t);
         break;
      }
      default:
         AR_MSG(DBG_ERROR_PRIO, "Invalid getparam received 0x%lx", param_id);
         result = CAPI_EUNSUPPORTED;
         break;
   }

   return result;
}

capi_err_t capi_mux_demux_set_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_err_t result  = CAPI_EOK;
   capi_err_t result2 = CAPI_EOK;

   result |= (!_pif || !props_ptr) ? CAPI_EFAILED : result;
   CHECK_ERR_AND_RETURN(result, "Error! set properties received null pointer.");

   capi_mux_demux_t *me_ptr = (capi_mux_demux_t *)_pif;

   result |= capi_cmn_set_basic_properties(props_ptr, (capi_heap_id_t *)&me_ptr->heap_id, &me_ptr->cb_info, FALSE);

   for (uint32_t i = 0; i < props_ptr->props_num; i++)
   {
      capi_prop_t *current_prop_ptr = &(props_ptr->prop_ptr[i]);
      capi_buf_t * payload_ptr      = &(current_prop_ptr->payload);
      result2                       = CAPI_EOK;
      switch (current_prop_ptr->id)
      {
         case CAPI_EVENT_CALLBACK_INFO:
         case CAPI_HEAP_ID:
         {
            continue;
         }
         case CAPI_ALGORITHMIC_RESET:
         {
            payload_ptr->actual_data_len = 0;
            break;
         }
         case CAPI_MODULE_INSTANCE_ID:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_module_instance_id_t))
            {
               capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
               me_ptr->miid                         = data_ptr->module_instance_id;
            }
            break;
         }
         case CAPI_PORT_NUM_INFO:
         {
            const capi_port_num_info_t *port_info_ptr = (capi_port_num_info_t *)payload_ptr->data_ptr;
            if (payload_ptr->actual_data_len >= sizeof(capi_port_num_info_t))
            {
               if (port_info_ptr->num_input_ports != me_ptr->num_of_input_ports ||
                   port_info_ptr->num_output_ports != me_ptr->num_of_output_ports)
               {
                  capi_mux_demux_cleanup_port_config(me_ptr);
                  me_ptr->num_of_input_ports  = port_info_ptr->num_input_ports;
                  me_ptr->num_of_output_ports = port_info_ptr->num_output_ports;

                  result2 = capi_mux_demux_alloc_port_config(me_ptr);
                  CHECK_ERR_AND_RETURN(result2, "malloc failed!");
               }

               AR_MSG(DBG_LOW_PRIO,
                      "Number of input ports %lu, output ports %lu",
                      port_info_ptr->num_input_ports,
                      port_info_ptr->num_output_ports);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO, "wrong size port info set property.");
               CAPI_SET_ERROR(result2, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            uint32_t port_index = current_prop_ptr->port_info.port_index;
            if (FALSE == current_prop_ptr->port_info.is_valid || port_index >= me_ptr->num_of_input_ports)
            {
               AR_MSG(DBG_ERROR_PRIO, "Invalid port info for input media format.");
               CAPI_SET_ERROR(result2, CAPI_EUNSUPPORTED);
               break;
            }

            if (payload_ptr->actual_data_len < CAPI_MF_V2_MIN_SIZE)
            {
               AR_MSG(DBG_ERROR_PRIO, "wrong size for input media format property.");
               CAPI_SET_ERROR(result2, CAPI_ENEEDMORE);
               break;
            }

            const capi_media_fmt_v2_t *fmt_ptr = (capi_media_fmt_v2_t *)payload_ptr->data_ptr;

            result2 |=
               ((CAPI_FIXED_POINT != fmt_ptr->header.format_header.data_format) ||
                (CAPI_MAX_CHANNELS_V2 < fmt_ptr->format.num_channels) ||
                (BIT_WIDTH_16 != fmt_ptr->format.bits_per_sample && BIT_WIDTH_32 != fmt_ptr->format.bits_per_sample) ||
                (PCM_Q_FACTOR_15 != fmt_ptr->format.q_factor && PCM_Q_FACTOR_27 != fmt_ptr->format.q_factor &&
                 PCM_Q_FACTOR_31 != fmt_ptr->format.q_factor))
                  ? CAPI_EBADPARAM
                  : CAPI_EOK;

#ifdef MUX_DEMUX_INTERLEAVED_DATA_WORKAROUND
            if ((CAPI_DEINTERLEAVED_UNPACKED != fmt_ptr->format.data_interleaving &&
                 CAPI_DEINTERLEAVED_UNPACKED_V2 != fmt_ptr->format.data_interleaving) &&
                (CAPI_INTERLEAVED != fmt_ptr->format.data_interleaving) && (fmt_ptr->format.num_channels != 1))
            {
               result2 |= CAPI_EBADPARAM;
            }
#else
            if ((CAPI_DEINTERLEAVED_UNPACKED != fmt_ptr->format.data_interleaving &&
                 CAPI_DEINTERLEAVED_UNPACKED_V2 != fmt_ptr->format.data_interleaving) &&
                (fmt_ptr->format.num_channels != 1))
            {
               result2 |= CAPI_EBADPARAM;
            }
#endif

            if (CAPI_FAILED(result2))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "Invalid input media format received for input port index 0x%x port id 0x%x.",
                      port_index,
                      me_ptr->input_port_info_ptr[port_index].port_id);
               memset(&me_ptr->input_port_info_ptr[port_index].fmt,
                      0,
                      sizeof(me_ptr->input_port_info_ptr[port_index].fmt));
            }
            else
            {
               me_ptr->input_port_info_ptr[port_index].fmt.sample_rate     = fmt_ptr->format.sampling_rate;
               me_ptr->input_port_info_ptr[port_index].fmt.bits_per_sample = fmt_ptr->format.bits_per_sample;
               me_ptr->input_port_info_ptr[port_index].fmt.q_factor        = fmt_ptr->format.q_factor;
               me_ptr->input_port_info_ptr[port_index].fmt.is_valid        = FALSE;

#ifdef MUX_DEMUX_INTERLEAVED_DATA_WORKAROUND
               if(CAPI_INTERLEAVED == fmt_ptr->format.data_interleaving)
               {
                  me_ptr->data_interleaving   = CAPI_INTERLEAVED;
               }
#endif

#ifdef MUX_DEMUX_TX_DEBUG_INFO
               AR_MSG(DBG_LOW_PRIO,
                      "Input media format received for input port index 0x%x port id 0x%x."
                      "sample rate %lu, num_channels %lu, q factor %lu. Max channels: %lu",
                      port_index,
                      me_ptr->input_port_info_ptr[port_index].port_id,
                      fmt_ptr->format.sampling_rate,
                      fmt_ptr->format.num_channels,
                      fmt_ptr->format.q_factor,
                      CAPI_MAX_CHANNELS_V2);
#endif
            }
            capi_mux_demux_update_operating_fmt(me_ptr);

            break;
         }

         default:
         {
            payload_ptr->actual_data_len = 0;
            AR_MSG(DBG_HIGH_PRIO, "Invalid setproperty 0x%x.", (int)current_prop_ptr->id);
            CAPI_SET_ERROR(result2, CAPI_EUNSUPPORTED);
            break;
         }
      }

      if (CAPI_FAILED(result2))
      {
         payload_ptr->actual_data_len = 0;
         CAPI_SET_ERROR(result, result2);
      }
   }
   return result;
}

capi_err_t capi_mux_demux_get_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_err_t result  = CAPI_EOK;
   capi_err_t result2 = CAPI_EOK;

   result |= (!props_ptr) ? CAPI_EFAILED : result;
   CHECK_ERR_AND_RETURN(result, "Error! get properties received null pointer.");

   capi_prop_t *     prop_ptr = props_ptr->prop_ptr;
   capi_mux_demux_t *me_ptr   = (capi_mux_demux_t *)_pif;
   uint32_t          i;
#ifdef SIM
   uint32_t         fwk_extn_ids[] = { FWK_EXTN_TRIGGER_POLICY };
#endif

   capi_basic_prop_t mod_prop_ptr = { .init_memory_req    = sizeof(capi_mux_demux_t),
                                      .stack_size         = MUX_DEMUX_STACK_SIZE,
#ifdef SIM
                                      .num_fwk_extns      = sizeof(fwk_extn_ids) / sizeof(fwk_extn_ids[0]),
                                      .fwk_extn_ids_arr   = fwk_extn_ids,
#else
                                      .num_fwk_extns      = 0,
                                      .fwk_extn_ids_arr   = NULL,
#endif
                                      .is_inplace         = FALSE,
                                      .req_data_buffering = FALSE,
                                      .max_metadata_size  = 0 };

   result |= capi_cmn_get_basic_properties(props_ptr, &mod_prop_ptr);

   // iterating over the properties
   for (i = 0; i < props_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;
      result2                 = CAPI_EOK;
      switch (prop_ptr[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_STACK_SIZE:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_MAX_METADATA_SIZE:
         {
            // handled in capi common utils.
            break;
         }
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         {
            if (NULL == me_ptr || NULL == me_ptr->output_port_info_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "Get property id 0x%lx, module is not allocated", prop_ptr[i].id);
               CAPI_SET_ERROR(result, CAPI_EBADPARAM);
               break;
            }

            result2 |=
               (payload_ptr->max_data_len < sizeof(capi_output_media_format_size_t)) ? CAPI_ENEEDMORE : CAPI_EOK;
            if (CAPI_FAILED(result2))
            {
               payload_ptr->actual_data_len = 0;
               AR_MSG(DBG_ERROR_PRIO, "Insufficient get property size.");
               CAPI_SET_ERROR(result, result2);
               break;
            }

            if (prop_ptr[i].port_info.is_valid && !prop_ptr[i].port_info.is_input_port &&
                prop_ptr[i].port_info.port_index < me_ptr->num_of_output_ports)
            {
               uint32_t                         out_arr_index = capi_mux_demux_get_output_arr_index_from_port_index(me_ptr, prop_ptr[i].port_info.port_index);
               if(out_arr_index >= me_ptr->num_of_output_ports)
               {
               payload_ptr->actual_data_len = 0;
               AR_MSG(DBG_ERROR_PRIO, "Invalid port info.");
               CAPI_SET_ERROR(result, CAPI_EBADPARAM);
               break;
               }
               capi_output_media_format_size_t *data_ptr   = (capi_output_media_format_size_t *)payload_ptr->data_ptr;
               uint32_t                         channel_type_size = 0;

               if (me_ptr->output_port_info_ptr[out_arr_index].fmt.num_max_channels)
               {
                  channel_type_size =
                     me_ptr->output_port_info_ptr[out_arr_index].fmt.num_max_channels * sizeof(capi_channel_type_t);
               }
               else
               {
                  channel_type_size =
                     me_ptr->output_port_info_ptr[out_arr_index].fmt.num_channels * sizeof(capi_channel_type_t);
               }

               data_ptr->size_in_bytes = sizeof(capi_standard_data_format_v2_t) + channel_type_size;

               payload_ptr->actual_data_len = sizeof(capi_output_media_format_size_t);
            }
            else
            {
               payload_ptr->actual_data_len = 0;
               AR_MSG(DBG_ERROR_PRIO, "Invalid port info.");
               CAPI_SET_ERROR(result, CAPI_EBADPARAM);
               break;
            }

            break;
         }
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr || NULL == me_ptr->output_port_info_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "Get property id 0x%lx, module is not allocated", prop_ptr[i].id);
               CAPI_SET_ERROR(result, CAPI_EBADPARAM);
               break;
            }
            uint32_t out_arr_index = me_ptr->num_of_output_ports;
            if(!prop_ptr[i].port_info.is_input_port)
            {
               out_arr_index = capi_mux_demux_get_output_arr_index_from_port_index(me_ptr, prop_ptr[i].port_info.port_index);
            }
            if (prop_ptr[i].port_info.is_valid && out_arr_index < me_ptr->num_of_output_ports &&
                prop_ptr[i].port_info.port_index < me_ptr->num_of_output_ports)
            {

               capi_media_fmt_v2_t *data_ptr   = (capi_media_fmt_v2_t *)payload_ptr->data_ptr;
               uint32_t             num_channels;

               if (me_ptr->output_port_info_ptr[out_arr_index].fmt.num_max_channels)
               { // if client has set the channels then use it.
                  num_channels = me_ptr->output_port_info_ptr[out_arr_index].fmt.num_max_channels;
               }
               else
               { // number of channels based on the connection index
                  num_channels = me_ptr->output_port_info_ptr[out_arr_index].fmt.num_channels;
               }

               uint32_t required_channel_map_size = num_channels * sizeof(capi_channel_type_t);

               uint32_t required_size = sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t) +
                                        required_channel_map_size;

               result2 |= (payload_ptr->max_data_len < required_size) ? CAPI_ENEEDMORE : CAPI_EOK;
               if (CAPI_FAILED(result2))
               {
                  payload_ptr->actual_data_len = 0;
                  AR_MSG(DBG_ERROR_PRIO, "Insufficient get property size.");
                  CAPI_SET_ERROR(result, result2);
                  break;
               }

               payload_ptr->actual_data_len =
                  memscpy(data_ptr, payload_ptr->max_data_len, &MUX_DEMUX_MEDIA_FMT_V2, required_size);

               data_ptr->format.sampling_rate   = me_ptr->operating_sample_rate;
               data_ptr->format.bits_per_sample = me_ptr->output_port_info_ptr[out_arr_index].fmt.bits_per_sample;
               data_ptr->format.q_factor        = me_ptr->output_port_info_ptr[out_arr_index].fmt.q_factor;
               data_ptr->format.num_channels    = num_channels;
               memscpy(data_ptr->channel_type,
                       required_channel_map_size,
                       me_ptr->output_port_info_ptr[out_arr_index].fmt.channel_type,
                       required_channel_map_size);
            }
            else
            {
               payload_ptr->actual_data_len = 0;
               AR_MSG(DBG_ERROR_PRIO, "Invalid port info.");
               CAPI_SET_ERROR(result, CAPI_EBADPARAM);
               break;
            }

            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         {
            capi_interface_extns_list_t *intf_ext_list = (capi_interface_extns_list_t *)payload_ptr->data_ptr;
            result2 |=
               ((payload_ptr->max_data_len < sizeof(capi_interface_extns_list_t)) ||
                (payload_ptr->max_data_len < (sizeof(capi_interface_extns_list_t) +
                                              (intf_ext_list->num_extensions * sizeof(capi_interface_extn_desc_t)))))
                  ? CAPI_ENEEDMORE
                  : result;

            if (CAPI_FAILED(result2))
            {
               payload_ptr->actual_data_len = 0;
               AR_MSG(DBG_ERROR_PRIO, "Insufficient get property size.");
               CAPI_SET_ERROR(result, result2);
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
         }
         default:
         {
            payload_ptr->actual_data_len = 0;
            AR_MSG(DBG_ERROR_PRIO, "Unsupported getproperty 0x%x.", prop_ptr[i].id);
            result |= CAPI_EUNSUPPORTED;
            break;
         }
      } // switch
   }
   return result;
}

capi_err_t capi_mux_demux_end(capi_t *_pif)
{
   capi_err_t        result = CAPI_EOK;
   capi_mux_demux_t *me_ptr = (capi_mux_demux_t *)_pif;

   result |= (NULL == _pif) ? CAPI_EFAILED : result;
   CHECK_ERR_AND_RETURN(result, "end received null pointer.");

   capi_mux_demux_cleanup_port_config(me_ptr);

   if (me_ptr->cached_config_ptr)
   {
      posal_memory_free(me_ptr->cached_config_ptr);
      me_ptr->cached_config_ptr = NULL;
   }

   me_ptr->vtbl = NULL;

   AR_MSG(DBG_HIGH_PRIO, "end.");

   return CAPI_EOK;
}
