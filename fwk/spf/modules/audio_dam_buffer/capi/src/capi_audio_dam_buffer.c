/**
 *   \file capi_audio_dam_buffer.c
 *   \brief
 *        This file contains CAPI implementation of Audio Dam buffer module
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_audio_dam_buffer_i.h"
#include "posal_power_mgr.h"

/*==============================================================================
   Local Function forward declaration
==============================================================================*/

// Check if media format and port config is received and then allocate stream writer for the particular index.
static capi_err_t capi_check_and_init_input_port(capi_audio_dam_t *me_ptr, uint32_t arr_index);

// raise capi events on receiving the input and output port configurations.
static capi_err_t capi_raise_mpps_and_bw_events(capi_audio_dam_t *me_ptr);

static capi_err_t capi_destroy_input_port(capi_audio_dam_t *me_ptr, uint32_t arr_index);

static capi_err_t capi_create_port_structures(capi_audio_dam_t *me_ptr);

static void capi_prepare_fixed_point_output_media_fmt_ptr(capi_audio_dam_t    *me_ptr,
                                                          capi_media_fmt_v2_t *out_fmt_ptr,
                                                          uint32_t             arr_index,
                                                          uint32_t             num_channels,
                                                          uint32_t            *ch_ids);

static capi_err_t capi_audio_dam_data_port_op_handler(capi_audio_dam_t *me_ptr, capi_buf_t *params_ptr);

static capi_err_t capi_audio_dam_buffer_compare_with_cached_mf(capi_audio_dam_t *me_ptr,
                                                               void             *mf_payload_ptr,
                                                               uint32_t         *num_chs_ptr);

static capi_err_t capi_audio_dam_init_ports_after_updating_input_mf_info(capi_audio_dam_t *me_ptr, uint32_t arr_index);

static capi_err_t capi_audio_dam_reinit_all_associated_ports(capi_audio_dam_t           *me_ptr,
                                                             uint32_t                    inp_arr_index,
                                                             audio_dam_input_port_cfg_t *cfg_ptr);

static capi_err_t capi_check_and_reinit_input_port(capi_audio_dam_t           *me_ptr,
                                                   uint32_t                    ip_arr_index,
                                                   audio_dam_input_port_cfg_t *cfg_ptr);

/*==============================================================================
   Public Function Implementation
==============================================================================*/

/*
  This function is used to query the static properties to create the CAPI.

  param[in] init_set_prop_ptr: Pointer to the initializing property list
  param[in, out] static_prop_ptr: Pointer to the static property list

  return: CAPI_EOK(0) on success else failure error code
 */
capi_err_t capi_audio_dam_buffer_get_static_properties(capi_proplist_t *init_set_prop_ptr,
                                                       capi_proplist_t *static_prop_ptr)
{
   return capi_audio_dam_buffer_get_properties(NULL, static_prop_ptr);
}

/*
 * DAM needs data trigger in STM container in order to produce output FTRT.
 */
static capi_err_t capi_audio_dam_raise_event_data_trigger_in_st_cntr(capi_audio_dam_t *me_ptr)
{
   capi_err_t                                  result = CAPI_EOK;
   capi_buf_t                                  payload;
   fwk_extn_event_id_data_trigger_in_st_cntr_t event;

   event.is_enable             = TRUE;
   event.needs_input_triggers  = FALSE;
   event.needs_output_triggers = TRUE;

   payload.data_ptr        = (int8_t *)&event;
   payload.actual_data_len = payload.max_data_len = sizeof(event);

   result =
      capi_cmn_raise_data_to_dsp_svc_event(&me_ptr->event_cb_info, FWK_EXTN_EVENT_ID_DATA_TRIGGER_IN_ST_CNTR, &payload);

   if (CAPI_FAILED(result))
   {
      DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_audio_dam: Failed to raise event to enable data_trigger.");
      return result;
   }
   else
   {
      DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "capi_audio_dam: raised event to enable data_trigger.");
   }

   return result;
}

/*
  This function is used init the CAPI lib.

  param[in] capi_ptr: Pointer to the CAPI lib.
  param[in] init_set_prop_ptr: Pointer to the property list that needs to be
            initialized

  return: CAPI_EOK(0) on success else failure error code
 */
capi_err_t capi_audio_dam_buffer_init(capi_t *capi_ptr, capi_proplist_t *init_set_prop_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == capi_ptr || NULL == init_set_prop_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "NULL capi_ptr[%p], init_set_prop_ptr[%p]", capi_ptr, init_set_prop_ptr);
      return CAPI_EFAILED;
   }

   capi_audio_dam_t *me_ptr = (capi_audio_dam_t *)((capi_ptr));
   me_ptr->vtbl             = capi_audio_dam_buffer_get_vtable();
   me_ptr->kpps_vote        = 0;

   // Set the init properties.
   result = capi_audio_dam_buffer_set_properties((capi_t *)me_ptr, init_set_prop_ptr);

   // Intialize audio dam driver library, its done after init props since we need heap ID for the lib init.
   audio_dam_init_args_t args;
   memset(&args, 0, sizeof(audio_dam_init_args_t));
   args.heap_id              = me_ptr->heap_id;
   args.iid                  = me_ptr->miid;
   args.preferred_chunk_size = DEFAULT_CIRC_BUF_CHUNK_SIZE;
   result |= audio_dam_driver_init(&args, &me_ptr->driver_handle);

   // To allow ftrt data drain in STM container
   result = capi_audio_dam_raise_event_data_trigger_in_st_cntr(me_ptr);
   return result;
}

/*==============================================================================
   Local Function Implementation
==============================================================================*/

capi_err_t capi_audio_dam_buffer_set_properties(capi_t *capi_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if ((NULL == capi_ptr) || (NULL == proplist_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_audio_dam: Set property failed. received bad pointer in set_property");
      return CAPI_EFAILED;
   }

   capi_audio_dam_t *me_ptr = (capi_audio_dam_t *)capi_ptr;

   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;
   if (NULL == prop_ptr)
   {
      DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_audio_dam: Set property failed. received bad pointer in prop_ptr");
      return CAPI_EFAILED;
   }

   uint32_t i;

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;

      switch (prop_ptr[i].id)
      {
         case CAPI_MODULE_INSTANCE_ID:
         {
            if (payload_ptr->actual_data_len < sizeof(capi_module_instance_id_t))
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Set property id 0x%lx, Bad param size %lu",
                       prop_ptr[i].id,
                       payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break;
            }

            capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
            if (data_ptr == NULL)
            {
               DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "callback pointer is NULL");
               CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
            }
            else
            {
               /* Module instance ID, used in debug messages.*/
               me_ptr->miid = data_ptr->module_instance_id;
            }
            break;
         }
         case CAPI_PORT_NUM_INFO:
         {
            if (payload_ptr->actual_data_len < sizeof(capi_port_num_info_t))
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: CAPI V2 FAILED Set Property id 0x%x Bad param size %u",
                       (uint32_t)prop_ptr[i].id,
                       payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }
            capi_port_num_info_t *data_ptr = (capi_port_num_info_t *)payload_ptr->data_ptr;

            // Port related structures
            if (NULL != me_ptr->in_port_info_arr || NULL != me_ptr->out_port_info_arr)
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: Set property failed. Port number info is already set.");
               return CAPI_EFAILED;
            }

            // Assign max input ports.
            me_ptr->max_input_ports  = data_ptr->num_input_ports;
            me_ptr->max_output_ports = data_ptr->num_output_ports;

            // Create port related structures based on the port info.
            capi_result |= capi_create_port_structures(me_ptr);
            capi_result |= capi_create_trigger_policy_mem(me_ptr);

            break;
         }
         case CAPI_HEAP_ID:
         {
            if (payload_ptr->actual_data_len < sizeof(capi_heap_id_t))
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam:  Set Property failed. id= 0x%x Bad param size %u",
                       (uint32_t)prop_ptr[i].id,
                       payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            capi_heap_id_t *data_ptr = (capi_heap_id_t *)payload_ptr->data_ptr;
            me_ptr->heap_id          = (POSAL_HEAP_ID)data_ptr->heap_id;

            break;
         }
         case CAPI_EVENT_CALLBACK_INFO:
         {
            if (payload_ptr->actual_data_len < sizeof(capi_event_callback_info_t))
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: Set Property Failed. id= 0x%x Bad param size %u",
                       (uint32_t)prop_ptr[i].id,
                       payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            capi_event_callback_info_t *data_ptr = (capi_event_callback_info_t *)payload_ptr->data_ptr;
            me_ptr->event_cb_info.event_cb       = data_ptr->event_cb;
            me_ptr->event_cb_info.event_context  = data_ptr->event_context;

            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == payload_ptr->data_ptr)
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: Set property id 0x%lx, received null buffer",
                       prop_ptr[i].id);
               return CAPI_EFAILED;
            }

            /* Validate the MF payload */
            if (payload_ptr->actual_data_len < sizeof(capi_set_get_media_format_t))
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: Invalid media format size %d",
                       payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            if (!prop_ptr[i].port_info.is_valid)
            {
               DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_audio_dam: Media format port info is invalid");
               return CAPI_ENEEDMORE;
            }

            capi_set_get_media_format_t *fmt_ptr = (capi_set_get_media_format_t *)(payload_ptr->data_ptr);

            // Check if the number of channels match with the input port set config
            if ((fmt_ptr->format_header.data_format != CAPI_FIXED_POINT) &&
                (fmt_ptr->format_header.data_format != CAPI_RAW_COMPRESSED))
            {
               DAM_MSG(me_ptr->miid,
                      DBG_ERROR_PRIO,
                      "capi_audio_dam: Unsupported mf data format= %lu,",
                      fmt_ptr->format_header.data_format);
               return CAPI_EBADPARAM;
            }
            // End MF Validation.

            uint32_t num_channels = 0;
            if (FALSE == me_ptr->is_input_media_fmt_set)
            {
               // cache data fmt first
               me_ptr->operating_mf.fmt              = fmt_ptr->format_header.data_format;

               if (CAPI_RAW_COMPRESSED == me_ptr->operating_mf.fmt)
               {
                  capi_cmn_raw_media_fmt_t *raw_fmt_ptr = (capi_cmn_raw_media_fmt_t *)(payload_ptr->data_ptr);

                  me_ptr->operating_mf.bitstream_format = raw_fmt_ptr->format.bitstream_format;

                  if (MEDIA_FMT_ID_G722 != raw_fmt_ptr->format.bitstream_format)
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "capi_audio_dam: Unsupported bit stream=0x%x, only MEDIA_FMT_ID_G722 is supported. ",
                            raw_fmt_ptr->format.bitstream_format);
                     return CAPI_EBADPARAM;
                  }

                  num_channels = 1; // for raw compressed input mf is assumed to be 1ch

                  // me_ptr->is_input_media_fmt_set = TRUE;
                  // this flag is set to true when G722 frame length info if also recevied.
                  // is received in the process context, until then circ buffer is not created and input mf handling
                  // is not considered to be complete.
               }
               else // FIXED point, PCM data fmt
               {
                  capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);
                  if (media_fmt_ptr->format.data_interleaving != CAPI_DEINTERLEAVED_UNPACKED)
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "capi_audio_dam: Unsupported data interleaving type= 0x%x, only deinterleaved unpacked is "
                            "supported. ",
                            media_fmt_ptr->format.data_interleaving);
                     return CAPI_EBADPARAM;
                  }

                  // Check if the number of channels match with the input port set config
                  if (media_fmt_ptr->format.num_channels > MAX_CHANNELS_PER_STREAM)
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "capi_audio_dam: Unsupported mf num_channels= %d,",
                            media_fmt_ptr->format.num_channels);
                     return CAPI_EBADPARAM;
                  }

                  me_ptr->operating_mf.bits_per_sample  = media_fmt_ptr->format.bits_per_sample;
                  me_ptr->operating_mf.bitstream_format = media_fmt_ptr->format.bitstream_format;
                  me_ptr->operating_mf.data_is_signed   = media_fmt_ptr->format.data_is_signed;
                  me_ptr->operating_mf.q_factor         = media_fmt_ptr->format.q_factor;
                  me_ptr->operating_mf.sampling_rate    = media_fmt_ptr->format.sampling_rate;

                  me_ptr->operating_mf.bytes_per_sample = (me_ptr->operating_mf.bits_per_sample == 16) ? 2 : 4;

                  num_channels = media_fmt_ptr->format.num_channels;

                  audio_dam_set_pcm_mf(&me_ptr->driver_handle,
                                       me_ptr->operating_mf.sampling_rate,
                                       me_ptr->operating_mf.bytes_per_sample);

                  me_ptr->is_input_media_fmt_set = TRUE;
               }
            }
            else // mf is already set
            {
               // compare input with prev cached mf
               if (CAPI_EOK !=
                   (capi_result = capi_audio_dam_buffer_compare_with_cached_mf(me_ptr, payload_ptr->data_ptr, &num_channels)))
               {
                  me_ptr->is_input_media_fmt_set = FALSE;
                  return capi_result;
               }
            }

            DAM_MSG(me_ptr->miid, DBG_MED_PRIO, "capi_audio_dam: Received valid media format.");

            // Cache port related mf info
            uint32_t input_port_index = prop_ptr[i].port_info.port_index;
            uint32_t arr_index        = get_arr_index_from_port_index(me_ptr, input_port_index, TRUE);
            // index of the input port

            if (IS_INVALID_PORT_INDEX(arr_index))
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: Input port array index is unknown, port_index=%u",
                       input_port_index);
               capi_result |= CAPI_EFAILED;
               break;
            }

            // for raw input MF is set to TRUE only after receiving MD
            me_ptr->in_port_info_arr[arr_index].is_mf_set    = me_ptr->is_input_media_fmt_set;
            me_ptr->in_port_info_arr[arr_index].num_channels = num_channels; // for raw its going to be 1 per input port

            capi_audio_dam_init_ports_after_updating_input_mf_info(me_ptr, arr_index);

            break;
         }
         case CAPI_ALGORITHMIC_RESET:
         {
            DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_audio_dam: Received Algo Reset");
            // if a gate is marked pending for close, and we get self stop
            // before the subsequent process call, we need to close the gates here.
            for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_output_ports; arr_idx++)
            {
               if (me_ptr->out_port_info_arr[arr_idx].is_pending_gate_close)
               {
                  capi_check_and_close_the_gate(me_ptr, arr_idx, FALSE);
                  me_ptr->out_port_info_arr[arr_idx].is_pending_gate_close = FALSE;
               }
            }

            // when gate is closed, vote for low KPPS
            capi_audio_dam_buffer_update_kpps_vote(me_ptr);
            break;
         }
         default:
         {
            DAM_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "capi_audio_dam: Set property failed. received un-supported prop_id 0x%x",
                    (uint32_t)prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
         }
      } /* Outer switch - Generic CAPI Properties */
   }    /* Loop all properties */

   return capi_result;
}

/** reinits input and all associated output ports. As part of input reinit, channels objects are recreated.
 *  Outputs need to reinitialized in this context because channels objects mapped to output need to be updated. */
static capi_err_t capi_audio_dam_reinit_all_associated_ports(capi_audio_dam_t           *me_ptr,
                                                             uint32_t                    input_arr_index,
                                                             audio_dam_input_port_cfg_t *input_cfg_ptr)
{
   capi_err_t result = CAPI_EOK;

   // Reinit input ports with new configuration
   result = capi_check_and_reinit_input_port(me_ptr, input_arr_index, input_cfg_ptr);

   // Check reinit, and raise output media format on all the output ports.
   // Since output media format cannot be raised until atleast on one of the input port valid
   // media format.
   for (uint32_t outport_index = 0; outport_index < me_ptr->max_output_ports; outport_index++)
   {
      result |= capi_check_and_reinit_output_port(me_ptr, outport_index, NULL);
   }

   result |= capi_raise_mpps_and_bw_events(me_ptr);

   return result;
}

static capi_err_t capi_audio_dam_init_ports_after_updating_input_mf_info(capi_audio_dam_t *me_ptr, uint32_t arr_index)
{
   capi_err_t result = CAPI_EOK;
   // Check and create stream writer for the input port if needed.
   result = capi_check_and_init_input_port(me_ptr, arr_index);

   // Check and raise output media format on all the output ports.
   // Since output media format cannot be raised until atleast on one of the input port valid
   // media format.
   for (uint32_t outport_index = 0; outport_index < me_ptr->max_output_ports; outport_index++)
   {
      result |= capi_check_and_init_output_port(me_ptr, outport_index);
   }

   result |= capi_raise_mpps_and_bw_events(me_ptr);

   return result;
}

capi_err_t capi_audio_dam_buffer_get_properties(capi_t *capi_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t        capi_result = CAPI_EOK;
   capi_audio_dam_t *me_ptr      = (capi_audio_dam_t *)capi_ptr;
   uint32_t          i;

   if (NULL == proplist_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_audio_dam: Get properties received null arguments");
      return CAPI_EBADPARAM;
   }

   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;

   uint32_t fwk_extn_ids[CAPI_AUDIO_DAM_NUM_FRAMEWORK_EXTENSIONS] = { 0 };
   fwk_extn_ids[0]                                                = FWK_EXTN_MULTI_PORT_BUFFERING;
   fwk_extn_ids[1]                                                = FWK_EXTN_TRIGGER_POLICY;

   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req    = sizeof(capi_audio_dam_t);
   mod_prop.stack_size         = AUDIO_DAM_STACK_SIZE_REQUIREMENT;
   mod_prop.num_fwk_extns      = CAPI_AUDIO_DAM_NUM_FRAMEWORK_EXTENSIONS;
   mod_prop.fwk_extn_ids_arr   = fwk_extn_ids;
   mod_prop.is_inplace         = FALSE; // not capable of in-place processing of data
   mod_prop.req_data_buffering = FALSE; // Doesnt require data buffering.
   mod_prop.max_metadata_size  = 0;     // NA

   capi_result = capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_audio_dam: Get properties failed with result %lu", capi_result);
      return capi_result;
   }

   // iterating over the properties
   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;

      switch (prop_ptr[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_STACK_SIZE:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         {
            // handled in capi common utils.
            break;
         }
         case CAPI_MIN_PORT_NUM_INFO:
         {
            if (payload_ptr->max_data_len >= sizeof(capi_min_port_num_info_t))
            {
               capi_min_port_num_info_t *data_ptr = (capi_min_port_num_info_t *)payload_ptr->data_ptr;
               data_ptr->num_min_input_ports      = 1; // always needs input
               data_ptr->num_min_output_ports     = 0; // can act as sink
               payload_ptr->actual_data_len       = sizeof(capi_min_port_num_info_t);
            }
            else
            {
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_audio_dam: Get properties received null arguments");
               return CAPI_EFAILED;
            }

            // index of the output port
            uint32_t arr_index = get_arr_index_from_port_index(me_ptr, prop_ptr[i].port_info.port_index, FALSE);
            if (IS_INVALID_PORT_INDEX(arr_index))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_audio_dam: Output media format is unknown, port_index=%u",
                      prop_ptr[i].port_info.port_index);
               break;
            }

            // Output media format can be know only if the output port configuration is received
            // and stream reader is setup.
            if (!is_dam_output_port_initialized(me_ptr, arr_index) )
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_audio_dam: Output media format is unknown, arr_index=%u", arr_index);
               capi_result |= CAPI_EFAILED;
               break;
            }

            uint32_t num_channels = me_ptr->out_port_info_arr[arr_index].actual_output_num_chs;
            uint32_t ret_size     = 0;
            if (CAPI_FIXED_POINT == me_ptr->operating_mf.fmt)
            {
               capi_media_fmt_v2_t *out_mf_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);

               /* Validate the MF payload */
               ret_size += sizeof(capi_media_fmt_v1_t) + (num_channels * sizeof(capi_channel_type_t));
               if (payload_ptr->max_data_len < ret_size)
               {
                  AR_MSG(DBG_ERROR_PRIO, "capi_audio_dam: Invalid media format size %d", payload_ptr->actual_data_len);

                  capi_result |= CAPI_ENEEDMORE;
                  break;
               }

               capi_prepare_fixed_point_output_media_fmt_ptr(me_ptr,
                                                             out_mf_ptr,
                                                             arr_index,
                                                             num_channels,
                                                             &me_ptr->out_port_info_arr[arr_index]
                                                                 .actual_output_ch_ids[0]);
            }
            else if (CAPI_RAW_COMPRESSED == me_ptr->operating_mf.fmt)
            {
               capi_cmn_raw_media_fmt_t *out_mf_ptr = (capi_cmn_raw_media_fmt_t *)(payload_ptr->data_ptr);

               ret_size += sizeof(capi_cmn_raw_media_fmt_t);
               if (payload_ptr->max_data_len < ret_size)
               {
                  AR_MSG(DBG_ERROR_PRIO, "capi_audio_dam: Invalid media format size %d", payload_ptr->actual_data_len);

                  capi_result |= CAPI_ENEEDMORE;
                  break;
               }

               // Prepare the output media format pointer.
               out_mf_ptr->header.format_header.data_format = CAPI_RAW_COMPRESSED;
               out_mf_ptr->format.bitstream_format          = me_ptr->operating_mf.bitstream_format;

               ret_size += sizeof(capi_cmn_raw_media_fmt_t);
            }

            // Update the actual data length.
            payload_ptr->actual_data_len = ret_size;

            break;
         } // CAPI_OUTPUT_MEDIA_FORMAT_V2
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
               AR_MSG(DBG_ERROR_PRIO, "Insufficient get property size.");
               break;
            }

            capi_interface_extn_desc_t *curr_intf_extn_desc_ptr =
               (capi_interface_extn_desc_t *)(payload_ptr->data_ptr + sizeof(capi_interface_extns_list_t));

            for (uint32_t j = 0; j < intf_ext_list->num_extensions; j++)
            {
               switch (curr_intf_extn_desc_ptr->id)
               {
                  case INTF_EXTN_IMCL:
                     curr_intf_extn_desc_ptr->is_supported = TRUE;
                     break;
                  case INTF_EXTN_DATA_PORT_OPERATION:
                     curr_intf_extn_desc_ptr->is_supported = TRUE;
                     break;
                  case INTF_EXTN_METADATA:
                     curr_intf_extn_desc_ptr->is_supported = TRUE;
                     break;
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
            AR_MSG(DBG_ERROR_PRIO, "capi_audio_dam: Unknown Prop[%d]", prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      } // switch
   }
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_audio_dam_buffer_set_param
  Sets either a parameter value or a parameter structure containing multiple
  parameters. In the event of a failure, the appropriate error code is
  returned.
 * -----------------------------------------------------------------------*/
capi_err_t capi_audio_dam_buffer_set_param_non_island(capi_t                 *capi_ptr,
                                                      uint32_t                param_id,
                                                      const capi_port_info_t *port_info_ptr,
                                                      capi_buf_t             *params_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == capi_ptr || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi_audio_dam: Set param failed. received bad property pointer for param_id property, 0x%x",
             param_id);
      return CAPI_EFAILED;
   }

   capi_audio_dam_t *me_ptr = (capi_audio_dam_t *)((capi_ptr));

   switch (param_id)
   {
      case INTF_EXTN_PARAM_ID_METADATA_HANDLER:
      {
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_metadata_handler_t))
         {
            DAM_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         intf_extn_param_id_metadata_handler_t *payload_ptr =
            (intf_extn_param_id_metadata_handler_t *)params_ptr->data_ptr;
         me_ptr->metadata_handler = *payload_ptr;
         break;
      }
      case INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION:
      {
         result |= capi_audio_dam_data_port_op_handler(me_ptr, params_ptr);
         break;
      }
      case PARAM_ID_AUDIO_DAM_INPUT_PORTS_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_audio_dam_input_ports_cfg_t))
         {
            DAM_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "capi_audio_dam: Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }

         param_id_audio_dam_input_ports_cfg_t *ip_cfg_ptr =
            (param_id_audio_dam_input_ports_cfg_t *)params_ptr->data_ptr;

         audio_dam_input_port_cfg_t *cur_param_cfg_ptr = (audio_dam_input_port_cfg_t *)(ip_cfg_ptr + 1);

         for (int i = 0; i < ip_cfg_ptr->num_input_ports; i++)
         {
            // Get the port index from the input port ID.
            uint32_t arr_index = get_arr_index_from_input_port_id(me_ptr, cur_param_cfg_ptr->input_port_id);
            if (IS_INVALID_PORT_INDEX(arr_index))
            {
               result = CAPI_EBADPARAM;
               break;
            }

            // Get the current input ports configuration size.
            uint32_t inp_cfg_size =
               sizeof(audio_dam_input_port_cfg_t) + (sizeof(uint32_t) * cur_param_cfg_ptr->num_channels);

            if (params_ptr->actual_data_len < inp_cfg_size)
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: Param id 0x%lx Bad param size %lu, required size= %lu",
                       (uint32_t)param_id,
                       params_ptr->actual_data_len,
                       inp_cfg_size);
               result = CAPI_EBADPARAM;
               break;
            }

            DAM_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Input port id 0x%lx received ch cfg with num_channels=%lu",
                    cur_param_cfg_ptr->input_port_id,
                    cur_param_cfg_ptr->num_channels);

            if ((0 == cur_param_cfg_ptr->num_channels) || (MAX_CHANNELS_PER_STREAM < cur_param_cfg_ptr->num_channels))
            {
               result = CAPI_EBADPARAM;
               break;
            }

            // Validate the input port config parameters.
            if (me_ptr->in_port_info_arr[arr_index].is_mf_set &&
                (cur_param_cfg_ptr->num_channels != me_ptr->in_port_info_arr[arr_index].num_channels))
            {
               DAM_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "Warning! Input port_id=0x%x configured num_channels= %d, doesnt match media fmt "
                       "num_channels= %d",
                       cur_param_cfg_ptr->input_port_id,
                       cur_param_cfg_ptr->num_channels,
                       me_ptr->in_port_info_arr[arr_index].num_channels);
            }

            // return error if the port is already started.
            if (me_ptr->in_port_info_arr[arr_index].is_started && me_ptr->in_port_info_arr[arr_index].strm_writer_ptr)
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: Param id 0x%lx failed, cannot be set when the port is started.",
                       (uint32_t)param_id);

               // This input port is already initialized, advance to the next input ports configuration.
               cur_param_cfg_ptr = (audio_dam_input_port_cfg_t *)(((int8_t *)cur_param_cfg_ptr) + inp_cfg_size);
               continue;
            }

            audio_dam_input_port_cfg_t *inp_cfg_params =
               (audio_dam_input_port_cfg_t *)posal_memory_malloc(inp_cfg_size, me_ptr->heap_id);
            if (NULL == inp_cfg_params)
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: Param id 0x%lx, memory couldn't be allocated for the internal "
                       "structs.");
               result = CAPI_ENOMEMORY;
               break;
            }

            // Copy the configuration from to the internal port structure.
            memscpy((void *)inp_cfg_params, inp_cfg_size, cur_param_cfg_ptr, inp_cfg_size);

            // reinit input and output ports
            capi_audio_dam_reinit_all_associated_ports(me_ptr, arr_index, inp_cfg_params);

            // Advance to the next input ports configuration.
            cur_param_cfg_ptr = (audio_dam_input_port_cfg_t *)(((int8_t *)cur_param_cfg_ptr) + inp_cfg_size);
         }

         break;
      }
      case PARAM_ID_AUDIO_DAM_OUTPUT_PORTS_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_audio_dam_output_ports_cfg_t))
         {
            DAM_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "capi_audio_dam: Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }

         param_id_audio_dam_output_ports_cfg_t *op_cfg_ptr =
            (param_id_audio_dam_output_ports_cfg_t *)params_ptr->data_ptr;

         audio_dam_output_port_cfg_t *cur_param_cfg_ptr = (audio_dam_output_port_cfg_t *)(op_cfg_ptr + 1);

         for (uint32_t i = 0; i < op_cfg_ptr->num_output_ports; i++)
         {
            // Get the port index from the output port ID.
            uint32_t arr_index = get_arr_index_from_output_port_id(me_ptr, cur_param_cfg_ptr->output_port_id);
            if (IS_INVALID_PORT_INDEX(arr_index))
            {
               result |= CAPI_EBADPARAM;
               break;
            }

            // Allocate and register as stream writer with the given port.
            uint32_t out_cfg_size =
               sizeof(audio_dam_output_port_cfg_t) + (sizeof(channel_map_t) * cur_param_cfg_ptr->num_channels);

            if (params_ptr->actual_data_len < out_cfg_size)
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: Param id 0x%lx Bad param size %lu, required size= %lu",
                       (uint32_t)param_id,
                       params_ptr->actual_data_len,
                       out_cfg_size);
               result |= CAPI_EBADPARAM;
               break;
            }


            DAM_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Output port id 0x%lx recieved ch cfg with num_channels=%lu",
                    cur_param_cfg_ptr->output_port_id,
                    cur_param_cfg_ptr->num_channels);

            if ((0 == cur_param_cfg_ptr->num_channels) || (MAX_CHANNELS_PER_STREAM < cur_param_cfg_ptr->num_channels))
            {
               result = CAPI_EBADPARAM;
               break;
            }

            audio_dam_output_port_cfg_t *out_cfg_params =
               (audio_dam_output_port_cfg_t *)posal_memory_malloc(out_cfg_size, me_ptr->heap_id);
            if (NULL == out_cfg_params)
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: Param id 0x%lx, memory couldn't be allocated for the internal "
                       "struts.");
               result |= CAPI_ENOMEMORY;
               break;
            }

            // Copy the configuration from to the internal pointer.
            memscpy((void *)out_cfg_params, out_cfg_size, cur_param_cfg_ptr, out_cfg_size);

            // Reinitialize the output port with new configuration.
            capi_check_and_reinit_output_port(me_ptr, arr_index, out_cfg_params);

            // Advance to the next output ports configuration.
            cur_param_cfg_ptr = (audio_dam_output_port_cfg_t *)(((int8_t *)cur_param_cfg_ptr) + out_cfg_size);
         }
         break;
      }
      case PARAM_ID_AUDIO_DAM_DOWNSTREAM_SETUP_DURATION:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_audio_dam_downstream_setup_duration_t))
         {
            DAM_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "capi_audio_dam: Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }

         param_id_audio_dam_downstream_setup_duration_t *param_cfg_ptr =
            (param_id_audio_dam_downstream_setup_duration_t *)params_ptr->data_ptr;

         audio_dam_downstream_setup_duration_t *port_cfgs =
            (audio_dam_downstream_setup_duration_t *)(param_cfg_ptr + 1);

         for (uint32_t i = 0; i < param_cfg_ptr->num_output_ports; i++)
         {
            // Get the port index from the output port ID.
            uint32_t arr_index = get_arr_index_from_output_port_id(me_ptr, port_cfgs[i].output_port_id);
            if (IS_INVALID_PORT_INDEX(arr_index))
            {
               result |= CAPI_EBADPARAM;
               break;
            }

            if (is_dam_output_port_initialized(me_ptr, arr_index) &&
                (port_cfgs[i].dwnstrm_setup_duration_ms !=
                 me_ptr->out_port_info_arr[arr_index].downstream_setup_duration_in_ms))
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_audio_dam: Port is being with new downstream delay %lu.",
                       port_cfgs[i].dwnstrm_setup_duration_ms);
            }

            // If port has not started just update the downstream setup duration.
            // Currently expectation is that downstream setup delay is obtained at module init.
            me_ptr->out_port_info_arr[arr_index].downstream_setup_duration_in_ms =
               port_cfgs[i].dwnstrm_setup_duration_ms;
         }

         break;
      }
      case PARAM_ID_AUDIO_DAM_CTRL_TO_DATA_PORT_MAP:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_audio_ctrl_to_data_port_map_t))
         {
            DAM_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "capi_audio_dam: Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }

         param_id_audio_ctrl_to_data_port_map_t *cfg_ptr =
            (param_id_audio_ctrl_to_data_port_map_t *)params_ptr->data_ptr;

         audio_dam_port_binding_t *ctrl_to_outport_map = (audio_dam_port_binding_t *)(cfg_ptr + 1);

         for (uint32_t i = 0; i < cfg_ptr->num_output_ports; i++)
         {
            // Get the output ports arr_index.
            uint32_t op_arr_index = get_arr_index_from_output_port_id(me_ptr, ctrl_to_outport_map[i].output_port_id);
            if (IS_INVALID_PORT_INDEX(op_arr_index))
            {
               result |= CAPI_EBADPARAM;
               break;
            }

            if (me_ptr->out_port_info_arr[op_arr_index].ctrl_port_id &&
                (me_ptr->out_port_info_arr[op_arr_index].ctrl_port_id != ctrl_to_outport_map[i].control_port_id))
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Ctrl port_id 0x%lx  to output port id 0x%lx mapping is already set.",
                       me_ptr->out_port_info_arr[op_arr_index].ctrl_port_id,
                       ctrl_to_outport_map[i].output_port_id);
               result |= CAPI_EBADPARAM;
               break;
            }

            me_ptr->out_port_info_arr[op_arr_index].ctrl_port_id = ctrl_to_outport_map[i].control_port_id;

            DAM_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Mapped ctrl port_id 0x%lx to output port id 0x%lx.",
                    me_ptr->out_port_info_arr[op_arr_index].ctrl_port_id,
                    ctrl_to_outport_map[i].output_port_id);

            // Check and Init output port now if its waiting for this ctrl port mapping
            capi_check_and_init_output_port(me_ptr, op_arr_index);
         }
         break;
      }

      case INTF_EXTN_PARAM_ID_IMCL_PORT_OPERATION:
      {
         if (NULL == params_ptr->data_ptr)
         {
            DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_dam: Set param id 0x%lx, received null buffer", param_id);
            result |= CAPI_EBADPARAM;
            break;
         }
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_imcl_port_operation_t))
         {
            DAM_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "capi_dam: Invalid payload size for ctrl port operation %d",
                    params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }
         intf_extn_param_id_imcl_port_operation_t *port_op_ptr =
            (intf_extn_param_id_imcl_port_operation_t *)(params_ptr->data_ptr);
         switch (port_op_ptr->opcode)
         {
            case INTF_EXTN_IMCL_PORT_OPEN:
            {
               if (port_op_ptr->op_payload.data_ptr)
               {
                  intf_extn_imcl_port_open_t *port_open_ptr =
                     (intf_extn_imcl_port_open_t *)port_op_ptr->op_payload.data_ptr;
                  /*Size Validation*/
                  uint32_t num_ports = port_open_ptr->num_ports;

                  // Number of control ports can be more than the output ports
                  if (num_ports > me_ptr->max_output_ports)
                  {
                     DAM_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "capi_dam: DAM only supports a max of %lu control ports. Trying to open %lu",
                             me_ptr->max_output_ports,
                             num_ports);
                     return CAPI_EBADPARAM;
                  }

                  uint32_t valid_size =
                     sizeof(intf_extn_imcl_port_open_t) + (num_ports * sizeof(intf_extn_imcl_id_intent_map_t));
                  if (port_op_ptr->op_payload.actual_data_len < valid_size)
                  {
                     DAM_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "capi_dam: Invalid payload size for ctrl port OPEN %d",
                             params_ptr->actual_data_len);
                     return CAPI_ENEEDMORE;
                  }

                  for (uint32_t iter = 0; iter < num_ports; iter++)
                  {
                     /*Size Validation*/
                     valid_size += (port_open_ptr->intent_map[iter].num_intents * sizeof(uint32_t));

                     // Dam always expects just one intent per ctrl port
                     if ((port_open_ptr->intent_map[iter].num_intents != AUDIO_DAM_MAX_INTENTS_PER_CTRL_PORT) ||
                         (port_op_ptr->op_payload.actual_data_len < valid_size))
                     {
                        DAM_MSG(me_ptr->miid,
                                DBG_ERROR_PRIO,
                                "capi_dam: Note: DAM always expects just one intent per ctrl port;"
                                "Invalid payload size for ctrl port OPEN %d",
                                params_ptr->actual_data_len);
                        return CAPI_ENEEDMORE;
                     }

                     uint32_t ctrl_port_id = port_open_ptr->intent_map[iter].port_id;

                     // Get output port index corresponding to the control port ID.
                     uint32_t cp_arr_idx = capi_dam_get_ctrl_port_arr_idx_from_ctrl_port_id(me_ptr, ctrl_port_id);
                     if (IS_INVALID_PORT_INDEX(cp_arr_idx))
                     {
                        DAM_MSG(me_ptr->miid,
                                DBG_ERROR_PRIO,
                                "capi_dam: Ctrl port 0x%lx mapping not found.",
                                ctrl_port_id);
                        return CAPI_EBADPARAM;
                     }

                     // Cache the intents in the cp_arr_idx
                     me_ptr->imcl_port_info_arr[cp_arr_idx].port_id     = ctrl_port_id;
                     me_ptr->imcl_port_info_arr[cp_arr_idx].state       = CTRL_PORT_OPEN;
                     me_ptr->imcl_port_info_arr[cp_arr_idx].num_intents = port_open_ptr->intent_map[iter].num_intents;

                     for (uint32_t i = 0; i < port_open_ptr->intent_map[iter].num_intents; i++)
                     {
                        me_ptr->imcl_port_info_arr[cp_arr_idx].intent_list_arr[i] =
                           port_open_ptr->intent_map[iter].intent_arr[i];
                     }

                     DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "capi_dam: ctrl port_id 0x%lx received open. ", ctrl_port_id);

                     // Check and Init output port now if its waiting for this ctrl port to open
                     uint32_t   mapped_op_arr_idxs[CAPI_AUDIO_DAM_MAX_OUTPUT_PORTS];
                     uint32_t   num_mapped_output_ports = 0;
                     capi_err_t err                     = get_output_arr_index_from_ctrl_port_id(me_ptr,
                                                                             ctrl_port_id,
                                                                             &num_mapped_output_ports,
                                                                             mapped_op_arr_idxs);
                     if (CAPI_FAILED(err))
                     {
                        DAM_MSG(me_ptr->miid,
                                DBG_HIGH_PRIO,
                                "capi_dam: Ctrl port 0x%lx to output port mapping not found.",
                                ctrl_port_id);
                     }

                     // iterate through mapped output ports and init the output
                     for (uint32_t idx = 0; idx < num_mapped_output_ports; idx++)
                     {
                        uint32_t op_arr_idx = mapped_op_arr_idxs[idx];
                        capi_check_and_init_output_port(me_ptr, op_arr_idx);
                     }
                  }
               }
               else
               {
                  DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_dam: Ctrl port open expects a payload. Failing.");
                  return CAPI_EFAILED;
               }
               break;
            }
            case INTF_EXTN_IMCL_PORT_CLOSE:
            {
               if (port_op_ptr->op_payload.data_ptr)
               {
                  intf_extn_imcl_port_close_t *port_close_ptr =
                     (intf_extn_imcl_port_close_t *)port_op_ptr->op_payload.data_ptr;
                  /*Size Validation*/
                  uint32_t num_ports = port_close_ptr->num_ports;

                  if (num_ports > me_ptr->max_output_ports)
                  {
                     DAM_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "capi_dam: Dam module only supports a max of %lu control ports. Trying to close %lu",
                             me_ptr->max_output_ports,
                             num_ports);
                     return CAPI_EBADPARAM;
                  }

                  uint32_t valid_size = sizeof(intf_extn_imcl_port_close_t) + (num_ports * sizeof(uint32_t));
                  if (port_op_ptr->op_payload.actual_data_len < valid_size)
                  {
                     DAM_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "capi_dam: Invalid payload size for ctrl port CLOSE %d",
                             params_ptr->actual_data_len);
                     return CAPI_ENEEDMORE;
                  }

                  // for each port id in the list that follows...
                  for (uint32_t iter = 0; iter < num_ports; iter++)
                  {
                     // Get the arr index for the control port ID.
                     uint32_t cp_arr_idx =
                        capi_dam_get_ctrl_port_arr_idx_from_ctrl_port_id(me_ptr, port_close_ptr->port_id_arr[iter]);
                     if (IS_INVALID_PORT_INDEX(cp_arr_idx))
                     {
                        DAM_MSG(me_ptr->miid,
                                DBG_ERROR_PRIO,
                                "capi_dam: Ctrl port 0x%lx mapping not found.",
                                port_close_ptr->port_id_arr[iter]);
                        continue;
                        CAPI_EBADPARAM;
                     }

                     me_ptr->imcl_port_info_arr[cp_arr_idx].state       = CTRL_PORT_CLOSE;
                     me_ptr->imcl_port_info_arr[cp_arr_idx].port_id     = 0;
                     me_ptr->imcl_port_info_arr[cp_arr_idx].num_intents = 0;

                     // Check and close the output port gates.
                     uint32_t   mapped_op_arr_idxs[CAPI_AUDIO_DAM_MAX_OUTPUT_PORTS];
                     uint32_t   num_mapped_output_ports = 0;
                     capi_err_t err                     = get_output_arr_index_from_ctrl_port_id(me_ptr,
                                                                             port_close_ptr->port_id_arr[iter],
                                                                             &num_mapped_output_ports,
                                                                             mapped_op_arr_idxs);
                     if (CAPI_FAILED(err))
                     {
                        DAM_MSG(me_ptr->miid,
                                DBG_HIGH_PRIO,
                                "capi_dam: Ctrl port 0x%lx to output port mapping not found.",
                                port_close_ptr->port_id_arr[iter]);
                     }

                     /** free the virtual writer configuration received on this control port since the client is getting
                      * disconneted/closed.*/
                     if (me_ptr->imcl_port_info_arr[cp_arr_idx].virt_wr_cfg_ptr)
                     {
                        posal_memory_free(me_ptr->imcl_port_info_arr[cp_arr_idx].virt_wr_cfg_ptr);
                        me_ptr->imcl_port_info_arr[cp_arr_idx].virt_wr_cfg_ptr = NULL;
                     }

                     // iterate through mapped output ports and close the gates
                     for (uint32_t idx = 0; idx < num_mapped_output_ports; idx++)
                     {
                        uint32_t op_arr_idx = mapped_op_arr_idxs[idx];
                        if (FALSE == me_ptr->out_port_info_arr[op_arr_idx].is_open) // output port is closed
                        {
                           DAM_MSG(me_ptr->miid,
                                   DBG_HIGH_PRIO,
                                   "capi_dam: Output port idx %ld linked to Ctrl port 0x%lx is closed already",
                                   op_arr_idx,
                                   port_close_ptr->port_id_arr[idx]);
                        }
                        else
                        {
                           // If the output port is not closed yet then exit close the gate.
                           capi_check_and_close_the_gate(me_ptr, op_arr_idx, FALSE);

                           // Resize the output buffers since the IMC connection is closing.
                           if (is_dam_output_port_initialized(me_ptr, op_arr_idx))
                           {
                              // reset the previous requested resize to 0 since the control port is closing
                              // client needs to send resize request again when a new ctrl port is open.
                              me_ptr->out_port_info_arr[op_arr_idx].requested_resize_in_us = 0;
                              me_ptr->out_port_info_arr[op_arr_idx].is_peer_heap_id_valid = FALSE; // reset peer heap ID
                              capi_audio_dam_resize_buffers(me_ptr, op_arr_idx);

                              // Fallback to regular mode, since virtual writer client is being stopped/closed
                              if (audio_dam_driver_is_virtual_writer_mode(
                                     me_ptr->out_port_info_arr[op_arr_idx].strm_reader_ptr))
                              {
                                 capi_check_and_reinit_output_port(
                                    me_ptr, op_arr_idx, NULL /** no change in output ch map configuration */);
                              }
                           }
                        }
                     }
                  }
               }
               else
               {
                  DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_dam: Ctrl port close expects a payload. Failing.");
                  return CAPI_EFAILED;
               }
               break;
            }
            case INTF_EXTN_IMCL_PORT_PEER_CONNECTED:
            {
               if (port_op_ptr->op_payload.data_ptr)
               {
                  intf_extn_imcl_port_start_t *port_start_ptr =
                     (intf_extn_imcl_port_start_t *)port_op_ptr->op_payload.data_ptr;
                  /*Size Validation*/
                  uint32_t num_ports = port_start_ptr->num_ports;

                  if (num_ports > me_ptr->max_output_ports)
                  {
                     DAM_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "capi_dam: Dam module only supports a max of %lu control ports. Trying to start %lu",
                             me_ptr->max_output_ports,
                             num_ports);
                     return CAPI_EBADPARAM;
                  }

                  uint32_t valid_size = sizeof(intf_extn_imcl_port_start_t) + (num_ports * sizeof(uint32_t));
                  if (port_op_ptr->op_payload.actual_data_len < valid_size)
                  {
                     DAM_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "capi_dam: Invalid payload size for ctrl port Start %d",
                             params_ptr->actual_data_len);
                     return CAPI_ENEEDMORE;
                  }

                  // for each port id in the list that follows...
                  for (uint32_t iter = 0; iter < num_ports; iter++)
                  {
                     // Get the arr index for the control port ID.
                     uint32_t cp_arr_idx =
                        capi_dam_get_ctrl_port_arr_idx_from_ctrl_port_id(me_ptr, port_start_ptr->port_id_arr[iter]);
                     if (IS_INVALID_PORT_INDEX(cp_arr_idx))
                     {
                        DAM_MSG(me_ptr->miid,
                                DBG_ERROR_PRIO,
                                "capi_dam: Ctrl port 0x%lx mapping not found.",
                                port_start_ptr->port_id_arr[iter]);
                        continue;
                        result = CAPI_EBADPARAM;
                     }

                     me_ptr->imcl_port_info_arr[cp_arr_idx].state = CTRL_PORT_PEER_CONNECTED;

                     DAM_MSG(me_ptr->miid,
                             DBG_HIGH_PRIO,
                             "capi_dam: ctrl port_id 0x%lx received start. ",
                             port_start_ptr->port_id_arr[iter]);
                  }
               }
               else
               {
                  DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_dam: Ctrl port start expects a payload. Failing.");
                  return CAPI_EFAILED;
               }
               break;
            }
            case INTF_EXTN_IMCL_PORT_PEER_DISCONNECTED:
            {
               if (port_op_ptr->op_payload.data_ptr)
               {
                  intf_extn_imcl_port_stop_t *port_disconn_ptr =
                     (intf_extn_imcl_port_stop_t *)port_op_ptr->op_payload.data_ptr;
                  /*Size Validation*/
                  uint32_t num_ports = port_disconn_ptr->num_ports;

                  if (num_ports > me_ptr->max_output_ports)
                  {
                     DAM_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "capi_dam: Dam module only supports a max of %lu control ports. Trying to STOP %lu",
                             me_ptr->max_output_ports,
                             num_ports);
                     return CAPI_EBADPARAM;
                  }

                  uint32_t valid_size = sizeof(intf_extn_imcl_port_stop_t) + (num_ports * sizeof(uint32_t));
                  if (port_op_ptr->op_payload.actual_data_len < valid_size)
                  {
                     DAM_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "capi_dam: Invalid payload size for ctrl port STOP %d",
                             params_ptr->actual_data_len);
                     return CAPI_ENEEDMORE;
                  }

                  // for each port id in the list that follows...
                  for (uint32_t iter = 0; iter < num_ports; iter++)
                  {
                     // Get the arr index for the control port ID.
                     uint32_t cp_arr_idx =
                        capi_dam_get_ctrl_port_arr_idx_from_ctrl_port_id(me_ptr, port_disconn_ptr->port_id_arr[iter]);
                     if (IS_INVALID_PORT_INDEX(cp_arr_idx))
                     {
                        DAM_MSG(me_ptr->miid,
                                DBG_ERROR_PRIO,
                                "capi_dam: Ctrl port 0x%lx mapping not found.",
                                port_disconn_ptr->port_id_arr[iter]);
                        continue;
                        CAPI_EBADPARAM;
                     }

                     DAM_MSG(me_ptr->miid,
                             DBG_HIGH_PRIO,
                             "capi_dam: ctrl port_id 0x%lx received disconnect.",
                             port_disconn_ptr->port_id_arr[iter]);

                     me_ptr->imcl_port_info_arr[cp_arr_idx].state = CTRL_PORT_PEER_DISCONNECTED;

                     // Check and close the output port gate.
                     uint32_t   mapped_op_arr_idxs[CAPI_AUDIO_DAM_MAX_OUTPUT_PORTS];
                     uint32_t   num_mapped_output_ports = 0;
                     capi_err_t err                     = get_output_arr_index_from_ctrl_port_id(me_ptr,
                                                                             port_disconn_ptr->port_id_arr[iter],
                                                                             &num_mapped_output_ports,
                                                                             mapped_op_arr_idxs);
                     if (CAPI_FAILED(err))
                     {
                        DAM_MSG(me_ptr->miid,
                                DBG_HIGH_PRIO,
                                "capi_dam: Ctrl port 0x%lx to output port mapping not found.",
                                port_disconn_ptr->port_id_arr[iter]);
                     }

                     /** free the virtual writer configuration received on this control port since the client is getting
                      * disconneted/closed.*/
                     if (me_ptr->imcl_port_info_arr[cp_arr_idx].virt_wr_cfg_ptr)
                     {
                        posal_memory_free(me_ptr->imcl_port_info_arr[cp_arr_idx].virt_wr_cfg_ptr);
                        me_ptr->imcl_port_info_arr[cp_arr_idx].virt_wr_cfg_ptr = NULL;
                     }

                     // iterate through mapped output ports and close the gate
                     for (uint32_t idx = 0; idx < num_mapped_output_ports; idx++)
                     {
                        uint32_t op_arr_idx = mapped_op_arr_idxs[idx];
                        if (FALSE == me_ptr->out_port_info_arr[op_arr_idx].is_gate_opened) // output port is closed
                        {
                           DAM_MSG(me_ptr->miid,
                                   DBG_HIGH_PRIO,
                                   "capi_dam: Output port %lu gate is already closed for ctrl port id 0x%lx",
                                   op_arr_idx,
                                   mapped_op_arr_idxs[idx]);
                        }
                        else
                        {
                           // If the output port gate is not closed, close it
                           capi_check_and_close_the_gate(me_ptr, op_arr_idx, FALSE);
                        }

                        // Fallback to regular mode, since virtual writer client is being stopped/closed
                        if (is_dam_output_port_initialized(me_ptr, op_arr_idx) &&
                             audio_dam_driver_is_virtual_writer_mode(
                               me_ptr->out_port_info_arr[op_arr_idx].strm_reader_ptr))
                        {
                           capi_check_and_reinit_output_port(me_ptr,
                                                             op_arr_idx,
                                                             NULL /** no change in output ch map configuration */);
                        }
                     }
                  }
               }
               else
               {
                  DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_dam: Ctrl port STOP expects a payload. Failing.");
                  return CAPI_EFAILED;
               }
               break;
            }
            default:
            {
               DAM_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "capi_dam: Received unsupported ctrl port opcode %lu",
                       port_op_ptr->opcode);
               return CAPI_EUNSUPPORTED;
            }
         }

         break;
      }
      case FWK_EXTN_PARAM_ID_TRIGGER_POLICY_CB_FN:
      {
         if (NULL == params_ptr->data_ptr)
         {
            DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_dam: Set param id 0x%lx, received null buffer", param_id);
            result |= CAPI_EBADPARAM;
            break;
         }

         // Level 1 check
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_trigger_policy_cb_fn_t))
         {
            DAM_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "CAPI V2 DAM: Invalid payload size for trigger policy %d",
                    params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         fwk_extn_param_id_trigger_policy_cb_fn_t *payload_ptr =
            (fwk_extn_param_id_trigger_policy_cb_fn_t *)params_ptr->data_ptr;
         me_ptr->policy_chg_cb = *payload_ptr;

         break;
      }
      default:
      {
         DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_audio_dam: Unsupported Param id ::0x%x \n", param_id);
         result = CAPI_EUNSUPPORTED;
      }
   }

   capi_audio_dam_buffer_update_kpps_vote(me_ptr);
   DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "capi_audio_dam: Set param 0x%x done 0x%x", param_id, result);
   return result;
}

/*------------------------------------------------------------------------
  Function name: capi_audio_dam_buffer_get_param
  Gets either a parameter value or a parameter structure containing multiple
  parameters. In the event of a failure, the appropriate error code is
  returned.
 * -----------------------------------------------------------------------*/
capi_err_t capi_audio_dam_buffer_get_param(capi_t *                capi_ptr,
                                           uint32_t                param_id,
                                           const capi_port_info_t *port_info_ptr,
                                           capi_buf_t *            params_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == capi_ptr || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CAPI V2 BRDG BUFFERING FAILED received bad property pointer for param_id property, 0x%x",
             param_id);
      return CAPI_EFAILED;
   }

   capi_audio_dam_t *me_ptr = (capi_audio_dam_t *)(capi_ptr);

   switch (param_id)
   {
      default:
      {
         DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "MODULE_BUFFERING::Unsupported Param id ::0x%x \n", param_id);
         result = CAPI_EUNSUPPORTED;
      }
      break;
   }

   DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "CAPI Buffering Get param done for param id 0x%x", param_id);
   return result;
}

/*------------------------------------------------------------------------
  Function name: capi_audio_dam_buffer_end
  Returns the library to the uninitialized state and frees the memory that
  was allocated by Init(). This function also frees the virtual function
  table.
 * -----------------------------------------------------------------------*/
capi_err_t capi_audio_dam_buffer_end(capi_t *capi_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == capi_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Audio Dam End received bad pointer, %p", capi_ptr);
      return CAPI_EFAILED;
   }
   capi_audio_dam_t *me_ptr = (capi_audio_dam_t *)((capi_ptr));

   if (me_ptr->imcl_port_info_arr)
   {
      // TODO: Destroy each of the control ports.
      // // Destroy all the output ports and free the port structure memory.
      // for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_output_ports; arr_idx++)
      // {
      //    capi_destroy_control_port(me_ptr, arr_idx);
      // }
   }

   if (me_ptr->out_port_info_arr)
   {
      // Destroy all the output ports and free the port structure memory.
      for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_output_ports; arr_idx++)
      {
         capi_destroy_output_port(me_ptr, arr_idx);
      }
   }

   if (me_ptr->in_port_info_arr)
   {
      // Destroy all the input ports and free the port structure memory.
      for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_input_ports; arr_idx++)
      {
         capi_destroy_input_port(me_ptr, arr_idx);
      }
      posal_memory_free(me_ptr->in_port_info_arr);
   }

   if (me_ptr->signal_tp.trigger_groups_ptr)
   {
      // donot free memory here, data tp and signal tp structs are allocated together
      // and data tp has the block ptr
      me_ptr->signal_tp.trigger_groups_ptr    = NULL;
      me_ptr->signal_tp.non_trigger_group_ptr = NULL; // same mem used
   }

   if (me_ptr->data_tp.trigger_groups_ptr)
   {
      posal_memory_free(me_ptr->data_tp.trigger_groups_ptr);
      me_ptr->data_tp.trigger_groups_ptr    = NULL;
      me_ptr->data_tp.non_trigger_group_ptr = NULL; // same mem used
   }

   // De initialize the dam driver.
   audio_dam_driver_deinit(&me_ptr->driver_handle);

   me_ptr->vtbl = NULL;
   return result;
}

// Check if media format and port config is received and then allocate stream writer for the particular index.
static capi_err_t capi_check_and_init_input_port(capi_audio_dam_t *me_ptr, uint32_t arr_index)
{
   if ((NULL == me_ptr->in_port_info_arr[arr_index].strm_writer_ptr) &&
       (TRUE == me_ptr->in_port_info_arr[arr_index].is_mf_set) &&
       (TRUE == me_ptr->in_port_info_arr[arr_index].is_open) && (NULL != me_ptr->in_port_info_arr[arr_index].cfg_ptr))
   {
      // Check num channels in port config vs media format
      if (me_ptr->in_port_info_arr[arr_index].cfg_ptr &&
          (me_ptr->in_port_info_arr[arr_index].cfg_ptr->num_channels !=
          me_ptr->in_port_info_arr[arr_index].num_channels))
      {
         AR_MSG( DBG_HIGH_PRIO,
                "Warning! Input port_id=0x%x configured num_channels= %d, doesnt match media fmt "
                "num_channels= %d",
                me_ptr->in_port_info_arr[arr_index].port_id,
                me_ptr->in_port_info_arr[arr_index].cfg_ptr->num_channels,
                me_ptr->in_port_info_arr[arr_index].num_channels);
      }

      // Create unique channel id for each channel using input port index and channel map.
      uint32_t  channel_ids[MAX_CHANNELS_PER_STREAM];
      uint32_t *channel_ids_from_inport = (uint32_t *)(me_ptr->in_port_info_arr[arr_index].cfg_ptr + 1);

      // Creating channel buffers solely based on input port configuration.
      // In case if there is a mismatch is number of chs between config vs MF, then during process
      // MIN(configured input chs, input mf chs) will be buffered and rest of the input chs will be dropped.
      for (uint32_t i = 0; i < me_ptr->in_port_info_arr[arr_index].cfg_ptr->num_channels; i++)
      {
         channel_ids[i] = channel_ids_from_inport[i];
      }

      ar_result_t result = audio_dam_stream_writer_create(&me_ptr->driver_handle,
                                                          0, // writers base buffer size
                                                          me_ptr->in_port_info_arr[arr_index].cfg_ptr->num_channels,
                                                          channel_ids,
                                                          &me_ptr->in_port_info_arr[arr_index].strm_writer_ptr);
      if (AR_EOK != result)
      {
         DAM_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "capi_audio_dam: Input port's can't be initiated. port_id=0x%x, result %d",
                 me_ptr->in_port_info_arr[arr_index].port_id,
                 result);
         return CAPI_EFAILED;
      }
      else
      {
         DAM_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "capi_audio_dam: Input port_id=0x%lx stream writer initialized successfully.",
                 me_ptr->in_port_info_arr[arr_index].port_id);
      }
   }
   else if (me_ptr->in_port_info_arr[arr_index].strm_writer_ptr)
   {
      DAM_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "capi_audio_dam: Input port already initialized port_id=0x%x",
              me_ptr->in_port_info_arr[arr_index].port_id);
   }
   else
   {
      DAM_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "capi_audio_dam: Input port not ready to initialize port_id=0x%x is_mf_set:%lu is_open:%lu cfg_set:%lu ",
              me_ptr->in_port_info_arr[arr_index].port_id,
              me_ptr->in_port_info_arr[arr_index].is_mf_set,
              me_ptr->in_port_info_arr[arr_index].is_open,
              (NULL != me_ptr->in_port_info_arr[arr_index].cfg_ptr));
   }

   return CAPI_EOK;
}

capi_err_t capi_check_and_reinit_input_port(capi_audio_dam_t *          me_ptr,
                                            uint32_t                    ip_arr_index,
                                            audio_dam_input_port_cfg_t *cfg_ptr)
{
   DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "capi_audio_dam: Input port is being reinit");

   // Destroy the stream writer for the input port
   if (me_ptr->in_port_info_arr[ip_arr_index].strm_writer_ptr)
   {
      audio_dam_stream_writer_destroy(&me_ptr->driver_handle, &me_ptr->in_port_info_arr[ip_arr_index].strm_writer_ptr);
      me_ptr->in_port_info_arr[ip_arr_index].strm_writer_ptr = NULL;
   }

   // if a new config is being set, reconfigure
   if (cfg_ptr)
   {
      // Free the old cfg pointer for the output port
      if (me_ptr->in_port_info_arr[ip_arr_index].cfg_ptr)
      {
         posal_memory_free(me_ptr->in_port_info_arr[ip_arr_index].cfg_ptr);
         me_ptr->in_port_info_arr[ip_arr_index].cfg_ptr = NULL;
      }

      // Update the new config pointer.
      me_ptr->in_port_info_arr[ip_arr_index].cfg_ptr = cfg_ptr;
   }

   // Check the state and initialize the output port buffers.
   return capi_check_and_init_input_port(me_ptr, ip_arr_index);
}

// Check if the port config is received and then allocate stream reader for the particular index.
capi_err_t capi_check_and_reinit_output_port(capi_audio_dam_t            *me_ptr,
                                             uint32_t                     op_arr_index,
                                             audio_dam_output_port_cfg_t *new_output_cfg_ptr)
{

   DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "capi_audio_dam: Output port is being reinit");

   // Check and close output port gate.
   capi_check_and_close_the_gate(me_ptr, op_arr_index, TRUE);

   // Destroy the existing stream reader handle.
   if (is_dam_output_port_initialized(me_ptr, op_arr_index))
   {
      audio_dam_stream_reader_destroy(&me_ptr->driver_handle, &me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr);
   }

   // Free the old cfg pointer for the output port
   if (new_output_cfg_ptr)
   {
      // Free the old cfg pointer for the output port
      if (me_ptr->out_port_info_arr[op_arr_index].cfg_ptr)
      {
         posal_memory_free(me_ptr->out_port_info_arr[op_arr_index].cfg_ptr);
         me_ptr->out_port_info_arr[op_arr_index].cfg_ptr = NULL;
      }

      // Update the new config pointer.
      me_ptr->out_port_info_arr[op_arr_index].cfg_ptr = new_output_cfg_ptr;
   }

   // Check the state and initialize the output port buffers.
   return capi_check_and_init_output_port(me_ptr, op_arr_index);
}

// Check if the port config is received and then allocate stream reader for the particular index.
capi_err_t capi_check_and_init_output_port(capi_audio_dam_t *me_ptr, uint32_t op_arr_idx)
{
   // if the ctrl port is not opened/mapped yet, then there is no client that can open/close the output gate
   // hence we can skip initializing that port until ctrl port is opened
   if (0 == me_ptr->out_port_info_arr[op_arr_idx].ctrl_port_id)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "Warning: Output port (id, index) (0x%lx, 0x%lx) not ready to initalize."
             "Not yet mapped mapped to valid ctrl_port_id:0x%lx ",
             me_ptr->out_port_info_arr[op_arr_idx].port_id,
             me_ptr->out_port_info_arr[op_arr_idx].port_index,
             me_ptr->out_port_info_arr[op_arr_idx].ctrl_port_id);
      return CAPI_EOK;
   }

   uint32_t ctrl_arr_index =
      capi_dam_get_ctrl_port_arr_idx_from_ctrl_port_id(me_ptr, me_ptr->out_port_info_arr[op_arr_idx].ctrl_port_id);
   if (IS_INVALID_PORT_INDEX(ctrl_arr_index))
   {
      return CAPI_EBADPARAM;
   }

   if (me_ptr->imcl_port_info_arr[ctrl_arr_index].state == CTRL_PORT_CLOSE)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "Warning: Output port (id, index) (0x%lx, 0x%lx) not ready to initalize. mapped ctrl_port_id:0x%lx is "
             "closed",
             me_ptr->out_port_info_arr[op_arr_idx].port_id,
             me_ptr->out_port_info_arr[op_arr_idx].port_index,
             me_ptr->out_port_info_arr[op_arr_idx].ctrl_port_id);
      return CAPI_EOK;
   }

   if (NULL == me_ptr->out_port_info_arr[op_arr_idx].cfg_ptr)
   {
      DAM_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "capi_audio_dam: Output port not ready to initialize. port_id=0x%x cfg_set:%lu",
              me_ptr->out_port_info_arr[op_arr_idx].port_id,
              (NULL != me_ptr->out_port_info_arr[op_arr_idx].cfg_ptr));
      return CAPI_EOK;
   }

   if (is_dam_output_port_initialized(me_ptr, op_arr_idx))
   {
      DAM_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "capi_audio_dam: Output port is already initialized. port_id=0x%x",
              me_ptr->out_port_info_arr[op_arr_idx].port_id);
      return CAPI_EOK;
   }

   audio_dam_output_port_cfg_t *cfg_ptr = me_ptr->out_port_info_arr[op_arr_idx].cfg_ptr;

   // Convert downstream setup delay from us to bytes.
   uint32_t downstream_setup_duration_in_ms = me_ptr->out_port_info_arr[op_arr_idx].downstream_setup_duration_in_ms;

   uint32_t downstream_setup_duration_in_us = (1000 * downstream_setup_duration_in_ms);

   uint32_t out_ch_id_arr[MAX_CHANNELS_PER_STREAM];
   me_ptr->out_port_info_arr[op_arr_idx].actual_output_num_chs = cfg_ptr->num_channels;

   if (cfg_ptr->num_channels > MAX_CHANNELS_PER_STREAM)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi_audio_dam: Output port 0x%x can't be initiated. num_channels=%lu not supported. ",
             me_ptr->out_port_info_arr[op_arr_idx].port_id,
             cfg_ptr->num_channels);
      return CAPI_EUNSUPPORTED;
   }

   // only 1 ch is supported for G722
   if ((me_ptr->operating_mf.bitstream_format == MEDIA_FMT_ID_G722) && (cfg_ptr->num_channels > 1))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi_audio_dam: Output port 0x%x can't be initiated. output cfg invalid. num_chs=%lu not supported "
             "for G722.",
             me_ptr->out_port_info_arr[op_arr_idx].port_id,
             cfg_ptr->num_channels);
      return AR_EFAILED;
   }

   channel_map_t *output_ch_map = (channel_map_t *)(cfg_ptr + 1);
   for (uint32_t i = 0; i < cfg_ptr->num_channels; i++)
   {
      // Set LSB 2 bytes to input channel type.
      out_ch_id_arr[i]                                              = output_ch_map[i].input_ch_id;
      me_ptr->out_port_info_arr[op_arr_idx].actual_output_ch_ids[i] = out_ch_id_arr[i];
   }

   // check if any peer heap ID is received
   POSAL_HEAP_ID peer_heap_id = capi_audio_dam_get_peer_heap_id(me_ptr, op_arr_idx);

   ar_result_t result = audio_dam_stream_reader_create(&me_ptr->driver_handle,
                                                       peer_heap_id,
                                                       downstream_setup_duration_in_us,
                                                       cfg_ptr->num_channels,
                                                       (uint32_t *)out_ch_id_arr,
                                                       me_ptr->imcl_port_info_arr[ctrl_arr_index].virt_wr_cfg_ptr,
                                                       &me_ptr->out_port_info_arr[op_arr_idx].strm_reader_ptr);
   if (AR_EOK != result)
   {
      if (AR_ENOTREADY == result)
      {
         DAM_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "capi_audio_dam: Output port not ready to initialize. port_id=0x%x, result %d",
                 me_ptr->out_port_info_arr[op_arr_idx].port_id,
                 result);
         return CAPI_EOK;
      }

      // else a failure
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "capi_audio_dam: Failed to init output port_id=0x%x, result %d",
              me_ptr->out_port_info_arr[op_arr_idx].port_id,
              result);
      return CAPI_EFAILED;
   }

   DAM_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "capi_audio_dam: Output port initiated successfully. port_id=0x%x, result %d",
           me_ptr->out_port_info_arr[op_arr_idx].port_id,
           result);

   // If the resize request is received prior to output port init, need to resize now.
   capi_audio_dam_resize_buffers(me_ptr, op_arr_idx);

   // Raise output media format events.
   capi_check_and_raise_output_media_format_event(me_ptr, op_arr_idx, cfg_ptr->num_channels, &out_ch_id_arr[0]);

   return CAPI_EOK;
}

// Check if the port config is received and then allocate stream writer for the particular index.
static capi_err_t capi_destroy_input_port(capi_audio_dam_t *me_ptr, uint32_t arr_index)
{
   // Destroy the stream writer for the input port
   if (me_ptr->in_port_info_arr[arr_index].strm_writer_ptr)
   {
      audio_dam_stream_writer_destroy(&me_ptr->driver_handle, &me_ptr->in_port_info_arr[arr_index].strm_writer_ptr);
   }

   // Free the cfg pointer for the input port
   if (me_ptr->in_port_info_arr[arr_index].cfg_ptr)
   {
      posal_memory_free(me_ptr->in_port_info_arr[arr_index].cfg_ptr);
   }

   // memset the port structure
   memset(&me_ptr->in_port_info_arr[arr_index], 0, sizeof(_aud_dam_input_port_info));

   return CAPI_EOK;
}

capi_err_t capi_destroy_output_port(capi_audio_dam_t *me_ptr, uint32_t op_arr_index)
{
   // Check and close output port gate.
   capi_check_and_close_the_gate(me_ptr, op_arr_index, TRUE);

   if (is_dam_output_port_initialized(me_ptr, op_arr_index))
   {
      audio_dam_stream_reader_destroy(&me_ptr->driver_handle, &me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr);
   }

   // Free the cfg pointer for the output port
   if (me_ptr->out_port_info_arr[op_arr_index].cfg_ptr)
   {
      posal_memory_free(me_ptr->out_port_info_arr[op_arr_index].cfg_ptr);
   }

   // memset the port structure
   memset(&me_ptr->out_port_info_arr[op_arr_index], 0, sizeof(_aud_dam_output_port_info));

   return CAPI_EOK;
}

static void capi_prepare_fixed_point_output_media_fmt_ptr(capi_audio_dam_t *   me_ptr,
                                                          capi_media_fmt_v2_t *out_fmt_ptr,
                                                          uint32_t             arr_index,
                                                          uint32_t             num_channels,
                                                          uint32_t *           ch_ids)
{
   out_fmt_ptr->header.format_header.data_format = CAPI_FIXED_POINT;

   out_fmt_ptr->format.bits_per_sample   = me_ptr->operating_mf.bits_per_sample;
   out_fmt_ptr->format.bitstream_format  = me_ptr->operating_mf.bitstream_format;
   out_fmt_ptr->format.data_is_signed    = me_ptr->operating_mf.data_is_signed;
   out_fmt_ptr->format.q_factor          = me_ptr->operating_mf.q_factor;
   out_fmt_ptr->format.sampling_rate     = me_ptr->operating_mf.sampling_rate;
   out_fmt_ptr->format.data_interleaving = CAPI_DEINTERLEAVED_UNPACKED;
   out_fmt_ptr->format.minor_version     = 0;
   out_fmt_ptr->format.num_channels      = num_channels;

   // Copy the output channel type from the port configuration.
   for (uint32_t idx = 0; idx < num_channels; idx++)
   {
      uint32_t ch_id = ch_ids[idx];

      // Get the output channel type for the given input ch_id from the ch_map config.
      bool_t found = FALSE;

      channel_map_t *output_ch_map = (channel_map_t *)(me_ptr->out_port_info_arr[arr_index].cfg_ptr + 1);
      for (uint32_t iter = 0; iter < me_ptr->out_port_info_arr[arr_index].cfg_ptr->num_channels && !found; iter++)
      {
         if (ch_id == output_ch_map[iter].input_ch_id)
         {
            out_fmt_ptr->channel_type[idx] = output_ch_map[iter].output_ch_id;
            found                          = TRUE;
         }
      }
   }
}

// raise capi events on receiving the input and output port configurations.
capi_err_t capi_check_and_raise_output_media_format_event(capi_audio_dam_t *me_ptr,
                                                          uint32_t          arr_index,
                                                          uint32_t          num_channels,
                                                          uint32_t *        out_ch_id_arr)
{
   capi_err_t result = CAPI_EOK;

   if (!me_ptr->is_input_media_fmt_set || !me_ptr->out_port_info_arr[arr_index].is_open ||
       !me_ptr->out_port_info_arr[arr_index].cfg_ptr)
   {
      return result;
   }

   if(CAPI_FIXED_POINT == me_ptr->operating_mf.fmt)
   {
      capi_media_fmt_v2_t out_fmt;
      memset(&out_fmt, 0, sizeof(capi_media_fmt_v2_t));

      // Prepare the output media format pointer.
      capi_prepare_fixed_point_output_media_fmt_ptr(me_ptr, &out_fmt, arr_index, num_channels, out_ch_id_arr);

      result |= capi_cmn_output_media_fmt_event_v2(&me_ptr->event_cb_info,
                                                   &out_fmt,
                                                   FALSE,
                                                   me_ptr->out_port_info_arr[arr_index].port_index);
   }
   else if (CAPI_RAW_COMPRESSED == me_ptr->operating_mf.fmt)
   {
      capi_cmn_raw_media_fmt_t out_fmt;
      memset(&out_fmt, 0, sizeof(capi_cmn_raw_media_fmt_t));

      // Prepare the output media format pointer.
      out_fmt.header.format_header.data_format = CAPI_RAW_COMPRESSED;
      out_fmt.format.bitstream_format = me_ptr->operating_mf.bitstream_format;

      result |= capi_cmn_raw_output_media_fmt_event(&me_ptr->event_cb_info,
                                                      &out_fmt,
                                                      FALSE,
                                                      me_ptr->out_port_info_arr[arr_index].port_index);

   }

   return result;
}

static capi_err_t capi_create_trigger_policy_mem_init_util_(capi_audio_dam_t *me_ptr, audio_dam_tp_info_t *tp_ptr)
{
   // worst case each port can be its own group
   uint32_t num_groups = 1; // below code works only for 1 group

   uint8_t *ptr = (uint8_t *)tp_ptr->trigger_groups_ptr + (num_groups * sizeof(fwk_extn_port_trigger_group_t));

   tp_ptr->trigger_groups_ptr[0].in_port_grp_affinity_ptr = (fwk_extn_port_trigger_affinity_t *)ptr;
   ptr += sizeof(fwk_extn_port_trigger_affinity_t) * me_ptr->max_input_ports;

   tp_ptr->trigger_groups_ptr[0].out_port_grp_affinity_ptr = (fwk_extn_port_trigger_affinity_t *)ptr;
   ptr += sizeof(fwk_extn_port_trigger_affinity_t) * me_ptr->max_output_ports;

   for (uint32_t i = 0; i < me_ptr->max_input_ports; i++)
   {
      if (me_ptr->in_port_info_arr[i].is_started)
      {
         tp_ptr->trigger_groups_ptr[0].in_port_grp_affinity_ptr[me_ptr->in_port_info_arr[i].port_index] =
            FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;
      }
   }

   for (uint32_t i = 0; i < me_ptr->max_output_ports; i++)
   {
      if (me_ptr->out_port_info_arr[i].is_started)
      {
         tp_ptr->trigger_groups_ptr[0].out_port_grp_affinity_ptr[me_ptr->out_port_info_arr[i].port_index] =
            FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE;
      }
   }

   ptr                           = (uint8_t *)ALIGN_8_BYTES((uint64_t)ptr);
   tp_ptr->non_trigger_group_ptr = (fwk_extn_port_nontrigger_group_t *)ptr;
   ptr += sizeof(fwk_extn_port_nontrigger_group_t);

   tp_ptr->non_trigger_group_ptr->in_port_grp_policy_ptr  = NULL; // inputs always trigger
   tp_ptr->non_trigger_group_ptr->out_port_grp_policy_ptr = (fwk_extn_port_nontrigger_policy_t *)ptr;
   for (uint32_t i = 0; i < me_ptr->max_output_ports; i++)
   {
      if (me_ptr->out_port_info_arr[i].is_started)
      {
         tp_ptr->non_trigger_group_ptr->out_port_grp_policy_ptr[me_ptr->out_port_info_arr[i].port_index] =
            FWK_EXTN_PORT_NON_TRIGGER_INVALID;
      }
   }

   return CAPI_EOK;
}

capi_err_t capi_create_trigger_policy_mem(capi_audio_dam_t *me_ptr)
{
   // worst case each port can be its own group
   uint32_t num_groups = 1; // below code works only for 1 group

   // size for all input and output trigger groups and output non-trigger group.
   uint32_t size = ALIGN_8_BYTES(num_groups * sizeof(fwk_extn_port_trigger_group_t) +
                                 num_groups * (me_ptr->max_input_ports + me_ptr->max_output_ports) *
                                    sizeof(fwk_extn_port_trigger_affinity_t)) +
                   sizeof(fwk_extn_port_nontrigger_group_t) +
                   (me_ptr->max_output_ports) * sizeof(fwk_extn_port_nontrigger_policy_t);
   uint32_t aligned_size = ALIGN_8_BYTES(size);

   // for two structs, one for signal and one for data trigger policy change event
   uint32_t total_size = aligned_size * 2;

   int8_t *blob_ptr = (int8_t *)posal_memory_malloc(total_size, me_ptr->heap_id);
   if (NULL == blob_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_audio_dam: failed to allocate memory for trigger policy");
      return CAPI_EFAILED;
   }
   memset(blob_ptr, 0, total_size);

   me_ptr->data_tp.trigger_groups_ptr   = (fwk_extn_port_trigger_group_t *)blob_ptr;
   me_ptr->signal_tp.trigger_groups_ptr = (fwk_extn_port_trigger_group_t *)(blob_ptr + aligned_size);

   capi_create_trigger_policy_mem_init_util_(me_ptr, &me_ptr->data_tp);
   capi_create_trigger_policy_mem_init_util_(me_ptr, &me_ptr->signal_tp);

   return CAPI_EOK;
}

static capi_err_t capi_create_port_structures(capi_audio_dam_t *me_ptr)
{

   if (NULL != me_ptr->in_port_info_arr)
   {
      DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_audio_dam: Port structures are already allocated.");
      return CAPI_EFAILED;
   }

   // Allocate memory for the input port structures.
   uint32_t input_alloc_size_aligned    = ALIGN_8_BYTES(me_ptr->max_input_ports * sizeof(_aud_dam_input_port_info));
   uint32_t ouptut_alloc_size_aligned   = ALIGN_8_BYTES(me_ptr->max_output_ports * sizeof(_aud_dam_output_port_info));
   uint32_t ctrlport_alloc_size_aligned = ALIGN_8_BYTES(me_ptr->max_output_ports * sizeof(imcl_port_info_t));

   uint32_t total_size = input_alloc_size_aligned + ouptut_alloc_size_aligned + ctrlport_alloc_size_aligned;

   DAM_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "capi_audio_dam: Allocating memory for port strucutres ip %lu op %lu cp %lu size %lu ",
           input_alloc_size_aligned,
           ouptut_alloc_size_aligned,
           ctrlport_alloc_size_aligned,
           total_size);

   int8_t *blob_ptr = (int8_t *)posal_memory_malloc(total_size, me_ptr->heap_id);
   if (NULL == blob_ptr)
   {
      DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_audio_dam: Couldn't allocate memory for port strucutres ");
      return CAPI_ENOMEMORY;
   }
   memset(blob_ptr, 0, total_size);

   me_ptr->in_port_info_arr   = (_aud_dam_input_port_info *)blob_ptr;
   me_ptr->out_port_info_arr  = (_aud_dam_output_port_info *)(blob_ptr + input_alloc_size_aligned);
   me_ptr->imcl_port_info_arr = (imcl_port_info_t *)(blob_ptr + input_alloc_size_aligned + ouptut_alloc_size_aligned);

   return CAPI_EOK;
}

uint32_t get_arr_index_from_port_index(capi_audio_dam_t *me_ptr, uint32_t port_index, bool_t is_input)
{

   if (!is_input)
   {
      for (uint32_t arr_index = 0; arr_index < me_ptr->max_output_ports; arr_index++)
      {
         if ((me_ptr->out_port_info_arr[arr_index].is_open) &&
             port_index == me_ptr->out_port_info_arr[arr_index].port_index)
         {
            return arr_index;
         }
      }
   }
   else
   {
      for (uint32_t arr_index = 0; arr_index < me_ptr->max_input_ports; arr_index++)
      {
         if ((me_ptr->in_port_info_arr[arr_index].is_open) &&
             port_index == me_ptr->in_port_info_arr[arr_index].port_index)
         {
            return arr_index;
         }
      }
   }

   DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_audio_dam: Port ID = 0x%lx to index mapping not found.", port_index);
   return UMAX_32;
}

uint32_t get_arr_index_from_input_port_id(capi_audio_dam_t *me_ptr, uint32_t port_id)
{
   uint32_t available_arr_index = 0;

   // Check if its not a valid input port ID.
   if (port_id % 2)
   {
      goto bailout;
   }

   available_arr_index = me_ptr->max_input_ports;
   for (uint32_t arr_index = 0; arr_index < me_ptr->max_input_ports; arr_index++)
   {
      if (port_id == me_ptr->in_port_info_arr[arr_index].port_id)
      {
         return arr_index;
      }
      else if (0 == me_ptr->in_port_info_arr[arr_index].port_id) // unused port structure
      {
         available_arr_index = arr_index;
      }
   }

   if (available_arr_index != me_ptr->max_input_ports)
   {
      DAM_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "capi_audio_dam: Mapping port id=0x%x  to arr idx=0x%x ",
              port_id,
              available_arr_index);
      me_ptr->in_port_info_arr[available_arr_index].port_id = port_id;
      return available_arr_index;
   }

bailout:
   DAM_MSG(me_ptr->miid,
           DBG_ERROR_PRIO,
           "capi_audio_dam: Input Port ID = 0x%lx to idx map not found or port id is invalid.",
           port_id);
   return UMAX_32;
}

uint32_t get_arr_index_from_output_port_id(capi_audio_dam_t *me_ptr, uint32_t port_id)
{
   uint32_t available_arr_index = 0;

   // Check if its not a valid output port ID.
   if (0 == (port_id % 2))
   {
      goto bailout;
   }

   available_arr_index = me_ptr->max_output_ports;
   for (uint32_t arr_index = 0; arr_index < me_ptr->max_output_ports; arr_index++)
   {
      if (port_id == me_ptr->out_port_info_arr[arr_index].port_id)
      {
         return arr_index;
      }
      else if (0 == me_ptr->out_port_info_arr[arr_index].port_id) // unused port structure
      {
         available_arr_index = arr_index;
      }
   }

   if (available_arr_index != me_ptr->max_output_ports)
   {
      DAM_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "capi_audio_dam: Mapping port id=0x%x  to arr idx=0x%x ",
              port_id,
              available_arr_index);
      me_ptr->out_port_info_arr[available_arr_index].port_id = port_id;
      return available_arr_index;
   }

bailout:
   DAM_MSG(me_ptr->miid,
           DBG_ERROR_PRIO,
           "capi_audio_dam: output Port ID = 0x%lx to idx map not found or port id is invalid .",
           port_id);
   return UMAX_32;
}

//// raise capi events on receiving the input and output port configurations.
static capi_err_t capi_raise_mpps_and_bw_events(capi_audio_dam_t *me_ptr)
{
   capi_audio_dam_buffer_update_kpps_vote(me_ptr);
   return CAPI_EOK;
}

capi_err_t capi_dam_duty_cycling_buf_send_message_to_dcm(capi_audio_dam_t *me_ptr, uint32_t spf_msg_cmd_dcm_req_code)
{

   capi_err_t result = CAPI_EOK;
   ar_result_t cmd_result = AR_EOK;
   me_ptr->payload.dcm_island_change_request = spf_msg_cmd_dcm_req_code;
   me_ptr->payload.signal_ptr                = NULL;

   cmd_result |= posal_power_mgr_send_command(me_ptr->payload.dcm_island_change_request,
                                             &me_ptr->payload,
                                             sizeof(dcm_island_control_payload_t));

   if (AR_EOK != cmd_result)
   {
      DAM_MSG(me_ptr->miid,
             DBG_ERROR_PRIO,
             "FAILED to create pm_server msg payload");
      return CAPI_EFAILED;
   }

   DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "sent msg to pm_server cmd [%lu], result: 0x%lx", spf_msg_cmd_dcm_req_code, cmd_result);
   return result;
}

static capi_err_t capi_audio_dam_data_port_op_handler(capi_audio_dam_t *me_ptr, capi_buf_t *params_ptr)
{
   capi_err_t result                = CAPI_EOK;
   bool_t     change_trigger_policy = FALSE;

   if (NULL == params_ptr->data_ptr)
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "capi_audio_dam: Set param id 0x%lx, received null buffer",
              INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION);
      return CAPI_EBADPARAM;
   }
   if (params_ptr->actual_data_len < sizeof(intf_extn_data_port_operation_t))
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "capi_audio_dam: Invalid payload size for port operation %d",
              params_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   intf_extn_data_port_operation_t *data_ptr = (intf_extn_data_port_operation_t *)(params_ptr->data_ptr);
   if (params_ptr->actual_data_len <
       sizeof(intf_extn_data_port_operation_t) + (data_ptr->num_ports * sizeof(intf_extn_data_port_id_idx_map_t)))
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "capi_audio_dam: Invalid payload size for port operation %d",
              params_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   if (data_ptr->is_input_port && (data_ptr->num_ports > me_ptr->max_input_ports))
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "capi_audio_dam: Invalid input ports. num_ports =%d, max_input_ports = %d",
              data_ptr->num_ports,
              me_ptr->max_input_ports);
      return CAPI_EBADPARAM;
   }

   if (!data_ptr->is_input_port && (data_ptr->num_ports > me_ptr->max_output_ports))
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "capi_audio_dam: Invalid output ports. num_ports =%d, max_output_ports = %d",
              data_ptr->num_ports,
              me_ptr->max_output_ports);
      return CAPI_EBADPARAM;
   }

   for (uint32_t iter = 0; iter < data_ptr->num_ports; iter++)
   {

      uint32_t port_id    = data_ptr->id_idx[iter].port_id;
      uint32_t port_index = data_ptr->id_idx[iter].port_index;
      uint32_t arr_index  = UMAX_32;

      DAM_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "Port operation 0x%x performed on port_index= %lu, port_id= 0x%lx is_input_port= %d ",
              data_ptr->opcode,
              port_index,
              data_ptr->id_idx[iter].port_id,
              data_ptr->is_input_port);

      if ((data_ptr->is_input_port && (port_index >= me_ptr->max_input_ports)) ||
          (!data_ptr->is_input_port && (port_index >= me_ptr->max_output_ports)))
      {
         DAM_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "capi_sal: Bad parameter in id-idx map on port 0x%lx, port_index = %lu, max in ports = %d, max "
                 "out ports = %d ",
                 port_id,
                 port_index,
                 me_ptr->max_input_ports,
                 me_ptr->max_output_ports);
         return CAPI_EBADPARAM;
      }
      if (data_ptr->is_input_port)
      {
         // Get port structures array index from port ID.
         arr_index = get_arr_index_from_input_port_id(me_ptr, port_id);
         if (IS_INVALID_PORT_INDEX(arr_index))
         {
            result |= CAPI_EBADPARAM;
            break;
         }
      }
      else
      {
         // Get port structures array index from port ID.
         arr_index = get_arr_index_from_output_port_id(me_ptr, port_id);
         if (IS_INVALID_PORT_INDEX(arr_index))
         {
            result |= CAPI_EBADPARAM;
            break;
         }
      }

      switch (data_ptr->opcode)
      {
         case INTF_EXTN_DATA_PORT_OPEN:
         {
            if (data_ptr->is_input_port)
            {
               // Update the port state in the port info structure.
               me_ptr->in_port_info_arr[arr_index].is_open = TRUE;

               // Cache the input port ID value.
               me_ptr->in_port_info_arr[arr_index].port_index = port_index;
               me_ptr->in_port_info_arr[arr_index].port_id    = port_id;

               capi_check_and_init_input_port(me_ptr, arr_index);
            }
            else
            {
               // Update the port state in the port info structure.
               me_ptr->out_port_info_arr[arr_index].is_open = TRUE;

               // Cache the output port ID value.
               me_ptr->out_port_info_arr[arr_index].port_index = port_index;
               me_ptr->out_port_info_arr[arr_index].port_id    = port_id;

               capi_check_and_init_output_port(me_ptr, arr_index);
            }

            break;
         }
         case INTF_EXTN_DATA_PORT_CLOSE:
         {
            if (data_ptr->is_input_port)
            {
               // Free the stream writer
               if (me_ptr->in_port_info_arr[arr_index].strm_writer_ptr)
               {
                  audio_dam_stream_writer_destroy(&me_ptr->driver_handle,
                                                  &me_ptr->in_port_info_arr[arr_index].strm_writer_ptr);
               }

               me_ptr->in_port_info_arr[arr_index].is_open = FALSE;
            }
            else
            {
               me_ptr->out_port_info_arr[arr_index].is_open = FALSE;
               me_ptr->out_port_info_arr[arr_index].gate_ctrl_op                = AUDIO_DAM_BATCH_INVALID;
               me_ptr->out_port_info_arr[arr_index].is_dcm_duty_cycling_enabled = FALSE;
            }

            break;
         }
         case INTF_EXTN_DATA_PORT_START:
         {
            if (data_ptr->is_input_port)
            {
               me_ptr->in_port_info_arr[arr_index].is_started = TRUE;

               /** for inputs trigger policy is always optionally present*/
               me_ptr->data_tp.trigger_groups_ptr[0].in_port_grp_affinity_ptr[port_index] =
                  FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;

               me_ptr->signal_tp.trigger_groups_ptr[0].in_port_grp_affinity_ptr[port_index] =
                  FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;

            }
            else
            {
               me_ptr->out_port_info_arr[arr_index].is_started = TRUE;
               if(AUDIO_DAM_BATCH_STREAM_WITH_ISLAND_DUTY_CYCLING == me_ptr->out_port_info_arr[arr_index].gate_ctrl_op)
               {
                  me_ptr->out_port_info_arr[arr_index].is_dcm_duty_cycling_enabled = TRUE;
               }

               /** for outputs trigger policy is optional present if gate is opened, other wise it is non-trigger
                * blocked*/
               if (me_ptr->out_port_info_arr[arr_index].is_gate_opened)
               {
                  me_ptr->data_tp.trigger_groups_ptr[0].out_port_grp_affinity_ptr[port_index] =
                     FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;

                  me_ptr->signal_tp.trigger_groups_ptr[0].out_port_grp_affinity_ptr[port_index] =
                     FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;

                  me_ptr->data_tp.non_trigger_group_ptr->out_port_grp_policy_ptr[port_index] =
                     FWK_EXTN_PORT_NON_TRIGGER_INVALID;

                  me_ptr->signal_tp.non_trigger_group_ptr->out_port_grp_policy_ptr[port_index] =
                     FWK_EXTN_PORT_NON_TRIGGER_INVALID;
               }
               else
               {
                  me_ptr->data_tp.trigger_groups_ptr[0].out_port_grp_affinity_ptr[port_index] =
                     FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE; // once gate is opened it will be made PRESENT

                  me_ptr->signal_tp.trigger_groups_ptr[0].out_port_grp_affinity_ptr[port_index] =
                     FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE; // once gate is opened it will be made PRESENT

                  me_ptr->data_tp.non_trigger_group_ptr->out_port_grp_policy_ptr[port_index] =
                     FWK_EXTN_PORT_NON_TRIGGER_BLOCKED; // one gate is opened it will be moved to trigger group

                  me_ptr->signal_tp.non_trigger_group_ptr[0].out_port_grp_policy_ptr[port_index] =
                     FWK_EXTN_PORT_NON_TRIGGER_BLOCKED; // once gate is opened it will be made PRESENT
               }
            }
            change_trigger_policy = TRUE;
            break;
         }
         case INTF_EXTN_DATA_PORT_STOP:
         {
            if (data_ptr->is_input_port)
            {
               me_ptr->in_port_info_arr[arr_index].is_started = FALSE;

               me_ptr->data_tp.trigger_groups_ptr[0].in_port_grp_affinity_ptr[port_index] =
                  FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE;

               me_ptr->signal_tp.trigger_groups_ptr[0].in_port_grp_affinity_ptr[port_index] =
                  FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE;

               change_trigger_policy = TRUE;

               /* handling the device switch scenario in ASR usecase, if the mode is batching and the gate is open, outp[ut port start
                  cache the bytes before EOS and flush an EOS after to mark the data at gap to downstream.
               */
               for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_output_ports; arr_idx++)
               {
                  if ((!me_ptr->out_port_info_arr[arr_idx].is_gate_opened) ||
                     (!is_dam_output_port_initialized(me_ptr, arr_idx)) ||
                     (!me_ptr->out_port_info_arr[arr_idx].is_started) ||
                     (!me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr->is_batch_streaming))
                  {
                     continue;
                  }

                  audio_dam_force_set_pending_bytes(me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr);

                  // check if the DCM is eneabled and pending bytes is non-zero (capture transition from 0 -> non-zero number)
                  if (FALSE == me_ptr->out_port_info_arr[arr_idx].is_dcm_duty_cycling_enabled)
                  {
                     posal_island_trigger_island_exit();
                     capi_dam_duty_cycling_buf_send_message_to_dcm(me_ptr, (uint32_t)SPF_MSG_CMD_DCM_REQ_FOR_ISLAND_EXIT);
                  }

                  uint32_t pending_bytes = 0;
                  audio_dam_get_stream_reader_pending_bytes(me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr,
                                                         &pending_bytes);

                  me_ptr->out_port_info_arr[arr_idx].bytes_before_eos = pending_bytes;
                  if (me_ptr->out_port_info_arr[arr_idx].bytes_before_eos)
                  {
                     me_ptr->out_port_info_arr[arr_idx].pending_eos = TRUE;
                  }
                  DAM_MSG(me_ptr->miid,
                        DBG_HIGH_PRIO,
                        "CAPI_DAM: device data port closed, pending eos %d bytes before eos %d",
                        me_ptr->out_port_info_arr[arr_idx].pending_eos,
                        me_ptr->out_port_info_arr[arr_idx].bytes_before_eos);
               }
            }
            else
            {
               me_ptr->out_port_info_arr[arr_index].is_started = FALSE;
               me_ptr->out_port_info_arr[arr_index].gate_ctrl_op                = AUDIO_DAM_BATCH_INVALID;
               me_ptr->out_port_info_arr[arr_index].is_dcm_duty_cycling_enabled = FALSE;
            }

            break;
         }
         default:
         {
            DAM_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "capi_audio_dam: Port operation - Unsupported opcode: %lu",
                    data_ptr->opcode);
            result = CAPI_EUNSUPPORTED;
            break;
         }
      }
   }

   if (change_trigger_policy)
   {
      capi_audio_dam_change_trigger_policy(me_ptr);
   }
   return result;
}

// reoder best channels at gate open
void capi_audio_dam_reorder_chs_at_gate_open(capi_audio_dam_t *me_ptr,
                                             uint32_t          op_arr_index,
                                             param_id_audio_dam_data_flow_ctrl_t *cfg_ptr)
{
   // Check if best channel info is sent. Ignore best channel info for Acoustic Activity Detection usecase.
   if (capi_audio_dam_is_mf_valid_and_fixed_point(me_ptr) && cfg_ptr->num_best_channels &&
       (cfg_ptr->num_best_channels <= MAX_CHANNELS_PER_STREAM))
   {
      // Sort the channel map in the order of best channels IDs and raise output media format with the
      // new order.
      uint32_t *best_ch_arr_ptr = (uint32_t*)(cfg_ptr + 1);
      if (AR_EOK == audio_dam_stream_reader_ch_order_sort(me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr,
                                                          cfg_ptr->num_best_channels,
                                                          best_ch_arr_ptr))
      {
         // Raise output media format based on best channel info.
         capi_check_and_raise_output_media_format_event(me_ptr,
                                                        op_arr_index,
                                                        cfg_ptr->num_best_channels,
                                                        best_ch_arr_ptr);

         me_ptr->out_port_info_arr[op_arr_index].actual_output_num_chs = cfg_ptr->num_best_channels;

         for (uint32_t i = 0; i < cfg_ptr->num_best_channels; i++)
         {
            me_ptr->out_port_info_arr[op_arr_index].actual_output_ch_ids[i] = cfg_ptr->best_ch_ids[i];
            DAM_MSG_ISLAND(me_ptr->miid,
                           DBG_HIGH_PRIO,
                           "CAPI V2 DAM: IMC Got Best channel ID 0x%lx ",
                           cfg_ptr->best_ch_ids[i]);
         }
      }
   }

   return;
}

// During gate open, the channels might have been reordered based on best channel info from detection engines.
// When control port is closing the gate but Dam module is not destroyed. we just need to revert the channel order
// as per the output port cfg calib.
//
// For Acoustic Activity Detection usecase, we do not support best channel feature, so we dont have to revert the channel order.
void capi_audio_dam_reorder_chs_at_gate_close(capi_audio_dam_t *me_ptr, uint32_t op_arr_index, bool_t is_destroy)
{
   if (capi_audio_dam_is_mf_valid_and_fixed_point(me_ptr) && (!is_destroy) && (me_ptr->out_port_info_arr[op_arr_index].cfg_ptr))
   {
      // Reset the channel output order when we get the gate closes.
      uint32_t ch_ids[MAX_CHANNELS_PER_STREAM];
      uint32_t num_channels = me_ptr->out_port_info_arr[op_arr_index].cfg_ptr->num_channels;

      channel_map_t *output_ch_map = (channel_map_t *)(me_ptr->out_port_info_arr[op_arr_index].cfg_ptr + 1);

      for (uint32_t idx = 0; idx < num_channels; idx++)
      {
         ch_ids[idx]                                                       = output_ch_map[idx].input_ch_id;
         me_ptr->out_port_info_arr[op_arr_index].actual_output_ch_ids[idx] = ch_ids[idx];
      }

      me_ptr->out_port_info_arr[op_arr_index].actual_output_num_chs = num_channels;

      // Reset the output channel sort order to default.
      // Sort the channel map in the order of best channels IDs and raise output media format with the new order.
      ar_result_t lib_res = AR_EOK;
      if (AR_EOK ==
            (lib_res = audio_dam_stream_reader_ch_order_sort(me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr,
                                                            me_ptr->out_port_info_arr[op_arr_index].cfg_ptr->num_channels,
                                                            &ch_ids[0])))
      {
         // Raise output media format based on best channel info.
         capi_check_and_raise_output_media_format_event(me_ptr, op_arr_index, num_channels, &ch_ids[0]);
      }
   }
   return;
}

static capi_err_t capi_audio_dam_buffer_compare_with_cached_mf(capi_audio_dam_t *me_ptr, void *mf_payload_ptr, uint32_t *num_chs_ptr)
{
   capi_set_get_media_format_t *fmt_ptr = (capi_set_get_media_format_t *)(mf_payload_ptr);
   // Compare the media format to the operating media format.
   if (me_ptr->operating_mf.fmt != fmt_ptr->format_header.data_format)
   {
      DAM_MSG(me_ptr->miid,
             DBG_ERROR_PRIO,
             "capi_audio_dam: Data fmt not matching. prev=%d, new=0x%d",
             me_ptr->operating_mf.fmt,
             fmt_ptr->format_header.data_format);
      return CAPI_EBADPARAM;
   }

   if (CAPI_RAW_COMPRESSED == me_ptr->operating_mf.fmt)
   {
      capi_cmn_raw_media_fmt_t *media_fmt_ptr = (capi_cmn_raw_media_fmt_t *)(mf_payload_ptr);
      if (me_ptr->operating_mf.bitstream_format != media_fmt_ptr->format.bitstream_format)
      {
         DAM_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "capi_audio_dam: Media fmt not supported.bf=0x%x",
                 media_fmt_ptr->format.bitstream_format);
         return CAPI_EBADPARAM;
      }
      *num_chs_ptr = 1;
   }
   else // fixed point
   {
      capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(mf_payload_ptr);
      // Compare the media format to the operating media format.
      if (me_ptr->operating_mf.bits_per_sample != media_fmt_ptr->format.bits_per_sample ||
          me_ptr->operating_mf.bitstream_format != media_fmt_ptr->format.bitstream_format ||
          me_ptr->operating_mf.data_is_signed != media_fmt_ptr->format.data_is_signed ||
          me_ptr->operating_mf.q_factor != media_fmt_ptr->format.q_factor ||
          me_ptr->operating_mf.sampling_rate != media_fmt_ptr->format.sampling_rate)
      {
         DAM_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "capi_audio_dam: Media fmt not supported. bps=%d, bf=0x%x, signed=%d, qf=%d, SR=%d fmt=%u",
                 media_fmt_ptr->header.format_header.data_format,
                 media_fmt_ptr->format.bits_per_sample,
                 media_fmt_ptr->format.bitstream_format,
                 media_fmt_ptr->format.data_is_signed,
                 media_fmt_ptr->format.q_factor,
                 media_fmt_ptr->format.sampling_rate);
         return CAPI_EBADPARAM;
      }
      *num_chs_ptr = media_fmt_ptr->format.num_channels;
   }

   return CAPI_EOK;
}

capi_err_t capi_audio_dam_handle_pcm_frame_info_metadata(capi_audio_dam_t *             me_ptr,
                                                         capi_stream_data_t *           input,
                                                         uint32_t                       ip_port_index,
                                                         md_encoder_pcm_frame_length_t *info_ptr)
{
   if (MEDIA_FMT_ID_G722 != info_ptr->bitstream_format)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_audio_dam: bitstream fmt 0x%lx not supported", info_ptr->bitstream_format);
      return AR_EFAILED;
   }

   if (MEDIA_FMT_ID_G722 != me_ptr->operating_mf.bitstream_format)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi_audio_dam: g722 MD unexpected, Input media format bitstream fmt is 0x%lx",
             me_ptr->operating_mf.bitstream_format);
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO,
          "capi_audio_dam: G722 metadata framelen us:%lu, framelen bytes:%lu, bytes per sample:%lu, sample "
          "rate:%lu ",
          info_ptr->output_pcm_frame_duration_in_us,
          info_ptr->max_output_frame_len_in_bytes);

   audio_dam_set_raw_compressed_mf(&me_ptr->driver_handle,
                                   info_ptr->output_pcm_frame_duration_in_us,
                                   info_ptr->max_output_frame_len_in_bytes);

   me_ptr->is_input_media_fmt_set                    = TRUE;
   me_ptr->in_port_info_arr[ip_port_index].is_mf_set = me_ptr->is_input_media_fmt_set;

   capi_audio_dam_init_ports_after_updating_input_mf_info(me_ptr, ip_port_index);

   return AR_EOK;
}

capi_err_t capi_audio_dam_handle_pending_eos(capi_audio_dam_t *me_ptr,
                                             capi_stream_data_t *output[],
                                             uint32_t arr_idx,
                                             uint32_t port_index)
{
   capi_err_t result = CAPI_EOK;
   me_ptr->out_port_info_arr[arr_idx].bytes_before_eos =
      (me_ptr->out_port_info_arr[arr_idx].bytes_before_eos < output[port_index]->buf_ptr->actual_data_len)
         ? 0
         : (me_ptr->out_port_info_arr[arr_idx].bytes_before_eos - output[port_index]->buf_ptr->actual_data_len);

   if (0 == me_ptr->out_port_info_arr[arr_idx].bytes_before_eos)
   {
      uint32_t temp_unread_bytes = 0;
      audio_dam_get_stream_reader_unread_bytes(me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr,
                                             &temp_unread_bytes);
      // send flushing/non flushing EOS downstream,
      eos_type_t eos_type = (temp_unread_bytes == 0) ? FLUSHING_EOS : NON_FLUSHING_EOS;
      result = capi_dam_insert_flushing_eos_at_out_port(me_ptr, output[port_index], FALSE, eos_type);
      me_ptr->out_port_info_arr[arr_idx].pending_eos = FALSE;
      //todo:  print all variables
      DAM_MSG(me_ptr->miid,
                     DBG_MED_PRIO,
                     "handle pending eos: eos_type [%d] unread_bytes [%lu]",
                     eos_type, temp_unread_bytes);
   }
   return result;
}

#if 0
capi_err_t capi_audio_dam_driver_reinit(capi_audio_dam_t *me_ptr)
{
   capi_err_t result = CAPI_EOK;
   INIT_EXCEPTION_HANDLING

   DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Reinit Dam driver with virt_mode enable:%lu", virt_wr_cfg_ptr);

   // destory stream reader and writer handles
   if (me_ptr->out_port_info_arr)
   {
      // Destroy all the output ports and free the port structure memory.
      for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_output_ports; arr_idx++)
      {
         // Check and close output port gate.
         TRY(result, capi_check_and_close_the_gate(me_ptr, arr_idx, TRUE));

         if (is_dam_output_port_initialized(me_ptr, arr_idx))
         {
            TRY(result,
                audio_dam_stream_reader_destroy(&me_ptr->driver_handle,
                                                &me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr));
         }
      }
   }

   if (me_ptr->in_port_info_arr)
   {
      // Destroy all the input ports and free the port structure memory.
      for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_input_ports; arr_idx++)
      {
         // Destroy the stream writer for the input port
         if (me_ptr->in_port_info_arr[arr_idx].strm_writer_ptr)
         {
            TRY(result,
                audio_dam_stream_writer_destroy(&me_ptr->driver_handle,
                                                &me_ptr->in_port_info_arr[arr_idx].strm_writer_ptr));
         }
      }
   }

   // De initialize the dam driver.
   TRY(result, audio_dam_driver_deinit(&me_ptr->driver_handle));

   // Re-Intialize audio dam driver library
   audio_dam_init_args_t args;
   args.heap_id              = me_ptr->heap_id;
   args.iid                  = me_ptr->miid;
   args.preferred_chunk_size = DEFAULT_CIRC_BUF_CHUNK_SIZE;
   TRY(result, audio_dam_driver_init(&args, &me_ptr->driver_handle));

   // Check and create stream writer for the input port if needed.
   for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_input_ports; arr_idx++)
   {
      TRY(result, capi_check_and_init_input_port(me_ptr, arr_idx));
   }

   // Check and raise output media format on all the output ports.
   // Since output media format cannot be raised until atleast on one of the input port valid
   // media format.
   for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_output_ports; arr_idx++)
   {
      TRY(result, capi_check_and_init_output_port(me_ptr, arr_idx));
   }

   DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Dam reinit successful");

   CATCH(result, DAM_MSG_PREFIX, me_ptr->miid)
   {
      DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Dam reinit failed");
      audio_dam_driver_deinit(&me_ptr->driver_handle);
   }

   return result;
}

// Check if the port config is received and then allocate stream reader for the particular index.
static capi_err_t capi_destroy_control_port(capi_audio_dam_t *me_ptr, uint32_t arr_index)
{
   if (me_ptr->imcl_port_info_arr[arr_index])
   {
      audio_dam_stream_reader_destroy(&me_ptr->driver_handle, &me_ptr->out_port_info_arr[arr_index].strm_reader_ptr);
   }

   // Free the cfg pointer for the output port
   if (me_ptr->out_port_info_arr[arr_index].cfg_ptr)
   {
      posal_memory_free(me_ptr->out_port_info_arr[arr_index].cfg_ptr);
   }

   // memset the port structure
   memset(&me_ptr->out_port_info_arr[arr_index], 0, sizeof(_aud_dam_output_port_info));

   return CAPI_EOK;
}
#endif
