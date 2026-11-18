/**
 * \file capi_data_marker.c
 * \brief
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "capi_data_marker.h"
#include "capi_data_marker_i.h"

/*==============================================================================
   Local Defines
==============================================================================*/

static capi_err_t capi_data_marker_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

static capi_err_t capi_data_marker_end(capi_t *_pif);

static capi_err_t capi_data_marker_set_param(capi_t *                _pif,
                                             uint32_t                param_id,
                                             const capi_port_info_t *port_info_ptr,
                                             capi_buf_t *            params_ptr);

static capi_err_t capi_data_marker_get_param(capi_t *                _pif,
                                             uint32_t                param_id,
                                             const capi_port_info_t *port_info_ptr,
                                             capi_buf_t *            params_ptr);

static capi_err_t capi_data_marker_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static capi_err_t capi_data_marker_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static const capi_vtbl_t vtbl = { capi_data_marker_process,        capi_data_marker_end,
                                  capi_data_marker_set_param,      capi_data_marker_get_param,
                                  capi_data_marker_set_properties, capi_data_marker_get_properties };

/* clang-format on */

/*==========================================================================
  Function Definitions
========================================================================== */

static capi_err_t data_marker_handle_metadata(capi_data_marker_t *me_ptr,
                                              uint32_t            input_before,
                                              capi_stream_data_t *input[],
                                              capi_stream_data_t *output[])
{
   capi_err_t result             = CAPI_EOK;
   bool_t     is_delay_event_reg = FALSE;

   /*
    * Rules - in order:
    1) By default:
         * Iterate, find the md, calculate delay,
         * raise event if subscribed to.
    2) Propagate MD
    3) If we received a set cfg, then we need to insert md:
    */
   if (CAPI_STREAM_V2 != input[0]->flags.stream_data_version)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: stream version must be 1");
      return CAPI_EFAILED;
   }

   capi_stream_data_v2_t *in_stream_ptr  = (capi_stream_data_v2_t *)input[0];
   capi_stream_data_v2_t *out_stream_ptr = (capi_stream_data_v2_t *)output[0];

   bool   input_had_eos           = FALSE;
   bool_t is_input_fully_consumed = (input_before == input[0]->buf_ptr[0].actual_data_len);

   if (in_stream_ptr->metadata_list_ptr)
   {
      // intercept md
      // see if we need to intercept delay marker
      is_delay_event_reg = capi_get_bits(me_ptr->intercept_flag, DATA_MARKER_DELAY_MASK, DATA_MARKER_DELAY_SHIFT);

      result = capi_data_marker_intercept_delay_marker_and_check_raise_events(me_ptr,
                                                                              in_stream_ptr->metadata_list_ptr,
                                                                              is_delay_event_reg);
      if (AR_EOK != result)
      {
         AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: Failed to raise event with the delay");
      }

      int32_t prev_out_marker_eos = out_stream_ptr->flags.marker_eos;
      bool_t  new_out_marker_eos  = FALSE;

      // propagate from input to output
      module_cmn_md_list_t **internal_list_pptr = &me_ptr->md_list_ptr;

      intf_extn_md_propagation_t input_md_info;
      memset(&input_md_info, 0, sizeof(input_md_info));
      input_md_info.df                          = me_ptr->operating_mf.header.format_header.data_format;
      input_md_info.initial_len_per_ch_in_bytes = input_before;
      input_md_info.len_per_ch_in_bytes         = in_stream_ptr->buf_ptr[0].actual_data_len;
      input_md_info.bits_per_sample             = me_ptr->operating_mf.format.bits_per_sample;
      input_md_info.sample_rate                 = me_ptr->operating_mf.format.sampling_rate;

      intf_extn_md_propagation_t output_md_info;
      memscpy(&output_md_info, sizeof(output_md_info), &input_md_info, sizeof(input_md_info));
      output_md_info.initial_len_per_ch_in_bytes = 0;
      out_stream_ptr->flags.marker_eos           = FALSE;
      output_md_info.len_per_ch_in_bytes         = out_stream_ptr->buf_ptr[0].actual_data_len;

      me_ptr->metadata_handler.metadata_propagate(me_ptr->metadata_handler.context_ptr,
                                                  in_stream_ptr,
                                                  out_stream_ptr,
                                                  internal_list_pptr,
                                                  0, /*algo_delay_us*/
                                                  &input_md_info,
                                                  &output_md_info);

      new_out_marker_eos = out_stream_ptr->flags.marker_eos;
      out_stream_ptr->flags.marker_eos |= prev_out_marker_eos;

      if (new_out_marker_eos)
      {
         AR_MSG(DBG_HIGH_PRIO, "flushing eos was propagated - clearing from input");
      }
   }

   // eof propagation
   // EOF propagation during EOS: propagate only once input EOS goes to output.
   if (input_had_eos)
   {
      if (out_stream_ptr->flags.marker_eos && !in_stream_ptr->flags.marker_eos)
      {
         if (in_stream_ptr->flags.end_of_frame)
         {
            in_stream_ptr->flags.end_of_frame  = FALSE;
            out_stream_ptr->flags.end_of_frame = TRUE;
         }
      }
   }
   else
   {
      if (is_input_fully_consumed)
      {
         if (in_stream_ptr->flags.end_of_frame)
         {
            in_stream_ptr->flags.end_of_frame  = FALSE;
            out_stream_ptr->flags.end_of_frame = TRUE;
         }
      }
   }

   if (me_ptr->is_md_inserter)
   {
      result |= capi_data_marker_insert_marker(me_ptr, &out_stream_ptr->metadata_list_ptr);
      if (AR_EOK != result)
      {
         AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: Failed to insert configured data markers");
      }
   }

   return result;
}

/*------------------------------------------------------------------------
   Function name: capi_data_marker_get_static_properties
   DESCRIPTION: Function to get the static properties of data marker module
-----------------------------------------------------------------------*/
capi_err_t capi_data_marker_get_static_properties(capi_proplist_t *init_set_prop_ptr, capi_proplist_t *static_prop_ptr)
{
   capi_err_t capi_result = CAPI_EOK;


   if (NULL != static_prop_ptr)
   {
      capi_result |= capi_data_marker_get_properties((capi_t *)NULL, static_prop_ptr);
   }
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_data_marker_init
  DESCRIPTION: Initialize the Data Marker module.
  -----------------------------------------------------------------------*/
capi_err_t capi_data_marker_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   if ((NULL == _pif) || (NULL == init_set_properties))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: Init received NULL pointer");
      return CAPI_EBADPARAM;
   }

   capi_data_marker_t *me_ptr = (capi_data_marker_t *)_pif;
   memset(me_ptr, 0, sizeof(capi_data_marker_t));
   me_ptr->vtbl_ptr = &vtbl; // assigning the vtbl with all function pointers

   capi_data_marker_set_properties((capi_t *)me_ptr, init_set_properties);

   // data marker is not supported in island.
   capi_cmn_raise_island_vote_event(&me_ptr->event_cb_info, CAPI_CMN_ISLAND_VOTE_EXIT);

   return CAPI_EOK;
}

/*------------------------------------------------------------------------
  Function name: capi_data_marker_set_properties
  DESCRIPTION: This function is used set properties of the CAPI.
  -----------------------------------------------------------------------*/
static capi_err_t capi_data_marker_set_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{

   if (!_pif || !props_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: set_prop(): Error! Received bad pointer in set_property");
      return CAPI_EFAILED;
   }

   capi_err_t          capi_result = CAPI_EOK;
   capi_data_marker_t *me_ptr      = (capi_data_marker_t *)_pif;
   //uint32_t            buffer_size;
   capi_prop_t *       current_prop_ptr;

   capi_result =
      capi_cmn_set_basic_properties(props_ptr, &me_ptr->heap_mem, &me_ptr->event_cb_info, TRUE /*port info*/);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: Set basic properties failed with result %lu", capi_result);
   }

   // This module does not expect media type changes at run
   // Only certain properties are expected to be set
   // for remainder, just print a message and continue
   uint32_t i;
   for (i = 0; i < props_ptr->props_num; i++)
   {
      current_prop_ptr        = &(props_ptr->prop_ptr[i]);
      capi_buf_t *payload_ptr = &(current_prop_ptr->payload);
      //buffer_size             = payload_ptr->actual_data_len;
      switch (current_prop_ptr->id)
      {
         case CAPI_EVENT_CALLBACK_INFO:
         case CAPI_INTERFACE_EXTENSIONS:
         case CAPI_HEAP_ID:
         case CAPI_PORT_DATA_THRESHOLD:
         case CAPI_PORT_NUM_INFO:
         {
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == payload_ptr->data_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_data_marker: Set property id 0x%lx, received null buffer",
                      current_prop_ptr->id);
               return CAPI_EBADPARAM;
            }

            if (payload_ptr->actual_data_len <
                (sizeof(capi_data_format_header_t) + sizeof(capi_raw_compressed_data_format_t)))
            {
               return CAPI_EBADPARAM;
            }

            switch (((capi_data_format_header_t *)(payload_ptr->data_ptr))->data_format)
            {
               case CAPI_RAW_COMPRESSED:
               {
                  capi_raw_compressed_data_format_t *media_fmt_ptr =
                     (capi_raw_compressed_data_format_t *)(payload_ptr->data_ptr + sizeof(capi_data_format_header_t));
                  me_ptr->operating_mf.header.format_header.data_format = CAPI_RAW_COMPRESSED;
                  me_ptr->operating_mf.format.bitstream_format = media_fmt_ptr->bitstream_format;

                  AR_MSG(DBG_LOW_PRIO, "capi_data_marker: 0x%lX: raw media fmt, fmt_id 0x%lx", me_ptr->miid, me_ptr->operating_mf.format.bitstream_format);

                  capi_event_info_t event_info;
                  event_info.port_info.port_index    = 0;
                  event_info.port_info.is_valid      = TRUE;
                  event_info.port_info.is_input_port = FALSE;

                  event_info.payload.actual_data_len = payload_ptr->actual_data_len;
                  event_info.payload.max_data_len    = payload_ptr->max_data_len;
                  event_info.payload.data_ptr        = payload_ptr->data_ptr;
                  capi_result = me_ptr->event_cb_info.event_cb(me_ptr->event_cb_info.event_context, CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED_V2, &event_info);

                  if (CAPI_FAILED(capi_result))
                  {
                     AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: Failed to send output media format updated event V2 with %d", capi_result);
                  }

                  break;
               }
               case CAPI_DEINTERLEAVED_RAW_COMPRESSED:
               {
                  capi_result |= CAPI_EUNSUPPORTED;
                  AR_MSG(DBG_LOW_PRIO, "capi_data_marker: 0x%lX: deinterleaved raw media fmt not supported yet", me_ptr->miid);
#if 0
                  capi_deinterleaved_raw_compressed_data_format_t
                  if (payload_ptr->actual_data_len <
                      (sizeof(capi_data_format_header_t) + sizeof(capi_deinterleaved_raw_compressed_data_format_t)))
                  {
                     return CAPI_EBADPARAM;
                  }

                  capi_set_get_media_format_t *main_ptr = (capi_set_get_media_format_t *)(payload_ptr->data_ptr);
                  capi_deinterleaved_raw_compressed_data_format_t *data_ptr =
                     (capi_deinterleaved_raw_compressed_data_format_t *)(main_ptr + 1);

                  me_ptr->operating_mf.header.format_header.data_format =
                     ((capi_data_format_header_t *)(payload_ptr->data_ptr))->data_format;

                  if (data_ptr->bufs_num < 1)
                  {
                     AR_MSG(DBG_ERROR_PRIO, "CAPI Data Logging: received bad prop_id 0x%x", data_ptr->bufs_num);
                     return CAPI_EBADPARAM;
                  }

                  me_ptr->operating_mf.format.bitstream_format  = data_ptr->bitstream_format;
                  me_ptr->operating_mf.format.num_channels      = data_ptr->bufs_num;
                  me_ptr->operating_mf.format.bits_per_sample   = 0;
                  me_ptr->operating_mf.format.q_factor          = 0;
                  me_ptr->operating_mf.format.sampling_rate     = 0;
                  me_ptr->operating_mf.format.data_interleaving = CAPI_DEINTERLEAVED_UNPACKED;
                  memset(me_ptr->operating_mf.format.channel_type,
                         0,
                         sizeof(uint16_t) * me_ptr->operating_mf.format.num_channels);

                  AR_MSG(DBG_LOW_PRIO,
                         "capi_data_marker 0x%lX: deinterleaved raw media fmt, num_bufs 0x%lx",
                         me_ptr->miid,
                         me_ptr->operating_mf.format.num_channels);
#endif
                  break;
               }
               default:
               {
                  /* Validate the MF payload */
                  if (payload_ptr->actual_data_len <
                      sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t))
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "capi_data_marker: Invalid media format size %d",
                            payload_ptr->actual_data_len);
                     capi_result |= CAPI_ENEEDMORE;
                     break;
                  }

                  capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);
                  if ((CAPI_MAX_CHANNELS_V2 < media_fmt_ptr->format.num_channels))
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "capi_data_marker: Unsupported Data format %lu or num_channels %lu. Max channels: %lu",
                            media_fmt_ptr->header.format_header.data_format,
                            media_fmt_ptr->format.num_channels,
							CAPI_MAX_CHANNELS_V2);
                     capi_result |= CAPI_EBADPARAM;
                     break;
                  }
                  uint32_t size_to_copy = sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t) +
                                          (media_fmt_ptr->format.num_channels * sizeof(capi_channel_type_t));

                  // accept as the operating Media format
                  memscpy(&me_ptr->operating_mf, size_to_copy, media_fmt_ptr, payload_ptr->actual_data_len);

                  capi_result |=
                     capi_cmn_output_media_fmt_event_v2(&me_ptr->event_cb_info, &me_ptr->operating_mf, FALSE, i);

#ifdef DATA_MARKER_DBG
                  AR_MSG(DBG_HIGH_PRIO,
                         "capi_data_marker: 0x%lX: Input Media format set prop: bits per sample: %lu bytes, num in/out "
                         "channels %lu",
                         me_ptr->miid,
                         me_ptr->operating_mf.format.bits_per_sample,
                         me_ptr->operating_mf.format.num_channels);
#endif // DATA_MARKER_DBG

                  break;
               }
            }

            me_ptr->is_in_media_fmt_set = TRUE;

            capi_result |= capi_cmn_update_kpps_event(&me_ptr->event_cb_info, CAPI_DATA_MARKER_KPPS);

            break;
         } // CAPI_INPUT_MEDIA_FORMAT_V2

         case CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT_V2:
         {
            /* Validate the payload */
            if (payload_ptr->actual_data_len < sizeof(capi_register_event_to_dsp_client_v2_t))
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: Invalid payload size %d", payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
               break;
            }
            AR_MSG(DBG_HIGH_PRIO, "capi_data_marker: Received CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT_V2");

            capi_register_event_to_dsp_client_v2_t *reg_event_ptr =
               (capi_register_event_to_dsp_client_v2_t *)(payload_ptr->data_ptr);

            switch (reg_event_ptr->event_id)
            {
               case EVENT_ID_DELAY_MARKER_INFO:
               {
                  if (reg_event_ptr->is_register)
                  {
                     event_reg_client_info_t *client_obj_ptr =
                        (event_reg_client_info_t *)posal_memory_malloc(sizeof(event_reg_client_info_t),
                                                                       (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);

                     if (NULL == client_obj_ptr)
                     {
                        AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: Malloc Failed while allocating client list node");
                        capi_result |= CAPI_ENOMEMORY;
                        break;
                     }

                     client_obj_ptr->address  = reg_event_ptr->dest_address;
                     client_obj_ptr->token    = reg_event_ptr->token;
                     client_obj_ptr->event_id = reg_event_ptr->event_id;

                     spf_list_insert_tail(&me_ptr->client_info_list_ptr,
                                          client_obj_ptr,
                                          (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id,
                                          TRUE /* use_pool*/);

                     bool_t is_delay_event_reg =
                        capi_get_bits(me_ptr->intercept_flag, DATA_MARKER_DELAY_MASK, DATA_MARKER_DELAY_SHIFT);

                     if (!is_delay_event_reg)
                     {

                        capi_set_bits(&me_ptr->intercept_flag,
                                      DATA_MARKER_DELAY_EVENT,
                                      DATA_MARKER_DELAY_MASK,
                                      DATA_MARKER_DELAY_SHIFT);
                     }

                     AR_MSG(DBG_HIGH_PRIO,
                            "Registered Client with Address 0x%lx for EVENT_ID_DELAY_MARKER_INFO",
                            reg_event_ptr->dest_address);
                  }
                  else // deregister
                  {
                     if (NULL == me_ptr->client_info_list_ptr)
                     {
                        AR_MSG(DBG_ERROR_PRIO,
                               "No client has registered with this module. Dereg has no meaning. Failing.");
                        capi_result |= CAPI_EFAILED;
                        break;
                     }

                     /** Get the pointer to per-sg-per-cont module list */
                     spf_list_node_t *curr_list_node_ptr = me_ptr->client_info_list_ptr;

                     bool_t node_found = FALSE;
                     while (curr_list_node_ptr)
                     {
                        event_reg_client_info_t *node_obj_ptr =
                           (event_reg_client_info_t *)me_ptr->client_info_list_ptr->obj_ptr;

                        if (reg_event_ptr->dest_address == node_obj_ptr->address)
                        {
                           spf_list_delete_node_and_free_obj(&curr_list_node_ptr,
                                                             &me_ptr->client_info_list_ptr,
                                                             TRUE /*pool_used*/);
                           node_found = TRUE;
                           break;
                        }
                        curr_list_node_ptr = curr_list_node_ptr->next_ptr;
                     }
                     if (!node_found)
                     {
                        AR_MSG(DBG_ERROR_PRIO,
                               "Couldn't find client with address 0x%llx in the reg list to deregister. Failing",
                               reg_event_ptr->dest_address);
                        capi_result |= CAPI_EFAILED;
                        break;
                     }
                     else
                     {
                        AR_MSG(DBG_HIGH_PRIO,
                               "DERegistered Client with Address 0x%lx for EVENT_ID_DELAY_MARKER_INFO",
                               reg_event_ptr->dest_address);
                     }
                  }
                  break;
               }

               default:
               {
                  AR_MSG(DBG_ERROR_PRIO, "Unsupported event ID[%d]", reg_event_ptr->event_id);
                  capi_result |= CAPI_EUNSUPPORTED;
                  break;
               }
            } // reg event id switch
            break;
         } // CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT_V2
         case CAPI_ALGORITHMIC_RESET:
         {

            break;
         }
         case CAPI_MODULE_INSTANCE_ID:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_module_instance_id_t))
            {
               capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
               me_ptr->miid                         = data_ptr->module_instance_id;
            }
            else
            {
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         default:
         {
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      }
   }

   return capi_result;
}
/*------------------------------------------------------------------------
  Function name: capi_data_marker_get_properties
  DESCRIPTION: Function to get the properties from the Data-Marker module
  -----------------------------------------------------------------------*/
static capi_err_t capi_data_marker_get_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_err_t          capi_result = CAPI_EOK;
   capi_data_marker_t *me_ptr      = (capi_data_marker_t *)_pif;
   uint32_t            i;

   if (NULL == props_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: Get property received null property array");
      return CAPI_EBADPARAM;
   }
   capi_prop_t *prop_ptr = props_ptr->prop_ptr;

   uint32_t          fwk_extn_ids_arr[1] = { FWK_EXTN_CONTAINER_FRAME_DURATION };

   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req    = sizeof(capi_data_marker_t);
   mod_prop.stack_size         = CAPI_DATA_MARKER_STACK_SIZE;
   mod_prop.num_fwk_extns      = 1;
   mod_prop.fwk_extn_ids_arr   = fwk_extn_ids_arr;
   mod_prop.is_inplace         = TRUE;
   mod_prop.req_data_buffering = FALSE;
   mod_prop.max_metadata_size  = 0;

   capi_result = capi_cmn_get_basic_properties(props_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: Get common basic properties failed with result %lu", capi_result);
   }

   // iterating over the properties
   for (i = 0; i < props_ptr->props_num; i++)
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
         case CAPI_PORT_DATA_THRESHOLD: // ignore this (1).
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         {
            break;
         }
         // end static props
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: pif is NULL for get OUTPUT MF");
               capi_result |= CAPI_EBADPARAM;
               break;
            }

            if (!me_ptr->is_in_media_fmt_set)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: media format is not set!");
               capi_result |= CAPI_EBADPARAM;
               break;
            }

            switch (((capi_data_format_header_t *)(payload_ptr->data_ptr))->data_format)
            {
               case CAPI_RAW_COMPRESSED:
               {
                  uint32_t ret_size = sizeof(capi_cmn_raw_media_fmt_t);

                  /* Validate the MF payload */
                  if (payload_ptr->actual_data_len < ret_size)
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "capi_data_marker: Invalid media format size %d",
                            payload_ptr->actual_data_len);
                     capi_result |= CAPI_ENEEDMORE;
                     break;
                  }
                  capi_cmn_raw_media_fmt_t *media_fmt_ptr = (capi_cmn_raw_media_fmt_t *)(payload_ptr->data_ptr);
                  media_fmt_ptr->header.format_header.data_format =
                     me_ptr->operating_mf.header.format_header.data_format;
                  media_fmt_ptr->format.bitstream_format = me_ptr->operating_mf.format.bitstream_format;
                  payload_ptr->actual_data_len           = sizeof(capi_cmn_raw_media_fmt_t);
                  break;
               }
#if 0
               case CAPI_DEINTERLEAVED_RAW_COMPRESSED:
               {
                  uint32_t ret_size = sizeof(capi_cmn_raw_media_fmt_t);

                  /* Validate the MF payload */
                  if (payload_ptr->actual_data_len < ret_size)
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "capi_data_marker: Invalid media format size %d",
                            payload_ptr->actual_data_len);
                     capi_result |= CAPI_ENEEDMORE;
                     break;
                  }
                  capi_cmn_raw_media_fmt_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);
                  media_fmt_ptr->header.format_header.data_format =
                     me_ptr->operating_mf.header.format_header.data_format;
                  media_fmt_ptr->format.bitstream_format = me_ptr->operating_mf.format.bitstream_format;
                  payload_ptr->actual_data_len           = sizeof(capi_cmn_raw_media_fmt_t);
                  break;
               }
#endif
               default:
               {
                  uint32_t ret_size = sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t) +
                                      (me_ptr->operating_mf.format.num_channels * sizeof(capi_channel_type_t));

                  /* Validate the MF payload */
                  if (payload_ptr->actual_data_len <
                      sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t))
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "capi_data_marker: Invalid media format size %d",
                            payload_ptr->actual_data_len);
                     capi_result |= CAPI_ENEEDMORE;
                     break;
                  }
                  capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);
                  memscpy(media_fmt_ptr, ret_size, &me_ptr->operating_mf, ret_size);
                  payload_ptr->actual_data_len = sizeof(capi_media_fmt_v2_t);

                  break;
               }
            }

            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         {
            if (payload_ptr->max_data_len >= sizeof(capi_interface_extns_list_t))
            {
               capi_interface_extns_list_t *intf_ext_list = (capi_interface_extns_list_t *)payload_ptr->data_ptr;
               if (payload_ptr->max_data_len < (sizeof(capi_interface_extns_list_t) +
                                                (intf_ext_list->num_extensions * sizeof(capi_interface_extn_desc_t))))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_data_marker: CAPI_INTERFACE_EXTENSIONS invalid param size %lu",
                         payload_ptr->max_data_len);
                  CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               }
               else
               {
                  capi_interface_extn_desc_t *curr_intf_extn_desc_ptr =
                     (capi_interface_extn_desc_t *)(payload_ptr->data_ptr + sizeof(capi_interface_extns_list_t));

                  for (uint32_t i = 0; i < intf_ext_list->num_extensions; i++)
                  {
                     switch (curr_intf_extn_desc_ptr->id)
                     {
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
                     AR_MSG(DBG_HIGH_PRIO,
                            "capi_data_marker: CAPI_INTERFACE_EXTENSIONS intf_ext = 0x%lx, is_supported = "
                            "%d",
                            curr_intf_extn_desc_ptr->id,
                            (int)curr_intf_extn_desc_ptr->is_supported);

                     curr_intf_extn_desc_ptr++;
                  }
               }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_data_marker: CAPI_INTERFACE_EXTENSIONS Bad param size %lu",
                      payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         default:
         {
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      } // switch
   }    // for
   return capi_result;
}

static capi_err_t capi_data_marker_set_param(capi_t *                _pif,
                                             uint32_t                param_id,
                                             const capi_port_info_t *port_info_ptr,
                                             capi_buf_t *            params_ptr)
{
   capi_err_t          capi_result = CAPI_EOK;
   capi_data_marker_t *me_ptr      = (capi_data_marker_t *)(_pif);
   if (NULL == _pif || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: set param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_EBADPARAM;
   }
   switch (param_id)
   {
      case PARAM_ID_DATA_MARKER_INSERT_MD:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_data_marker_insert_md_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_data_marker: 0x%lX Payload size is incorrect for the PARAM_ID_DATA_MARKER_INSERT_MD %lu",
				   me_ptr->miid,
                   params_ptr->actual_data_len);
            return AR_EBADPARAM;
         }

         param_id_data_marker_insert_md_t *payload_ptr = (param_id_data_marker_insert_md_t *)params_ptr->data_ptr;

         if (DATA_MARKER_MD_ID_DELAY_MARKER != payload_ptr->metadata_id)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_data_marker: 0x%lX: Only Delay marker MD is supported in cfg. Provided unsupported ID = "
                   "0x%lx",
                   me_ptr->miid,
                   payload_ptr->metadata_id);
            return AR_EUNSUPPORTED;
         }

         cfg_md_info_t *node_obj_ptr =
            (cfg_md_info_t *)posal_memory_malloc(sizeof(cfg_md_info_t), (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
         if (NULL == node_obj_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: 0x%lX: Malloc Failed", me_ptr->miid);
            return AR_ENOMEMORY;
         }
         node_obj_ptr->md_id        = payload_ptr->metadata_id;
         node_obj_ptr->token        = payload_ptr->token;
         node_obj_ptr->frame_dur_ms = payload_ptr->frame_duration_ms;

         // cache in a list
         spf_list_insert_tail(&me_ptr->insert_md_cfg_list_ptr,
                              node_obj_ptr,
                              (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id,
                              TRUE /* use_pool*/);

         me_ptr->is_md_inserter = TRUE;

         AR_MSG(DBG_MED_PRIO,
                "capi_data_marker: 0x%lX: Received DATA_MARKER_INSERT CFG: metadata_id = 0x%lx, token = 0x%lx, dur = %d",
                me_ptr->miid,
                payload_ptr->metadata_id,
                payload_ptr->token,
				payload_ptr->frame_duration_ms);

         break;
      }
      case INTF_EXTN_PARAM_ID_METADATA_HANDLER:
      {
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_metadata_handler_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_data_marker: Param id 0x%lx Bad param size %lu",
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
      case FWK_EXTN_PARAM_ID_CONTAINER_FRAME_DURATION:
      {
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_container_frame_duration_t))
         {
            capi_result |= CAPI_ENEEDMORE;
            break;
         }

         fwk_extn_param_id_container_frame_duration_t *fm_dur =
            (fwk_extn_param_id_container_frame_duration_t *)params_ptr->data_ptr;
         me_ptr->cntr_frame_dur_us = fm_dur->duration_us;

         break;
      }

      default:
      {
         AR_MSG(DBG_HIGH_PRIO, "capi_data_marker: Unsupported Param id ::0x%x \n", param_id);
         capi_result = CAPI_EUNSUPPORTED;
      }
      break;
   }

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_data_marker_get_param
  Gets either a parameter value or a parameter structure containing multiple
  parameters. In the event of a failure, the appropriate error code is
  returned.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_data_marker_get_param(capi_t *                _pif,
                                             uint32_t                param_id,
                                             const capi_port_info_t *port_info_ptr,
                                             capi_buf_t *            params_ptr)
{
   return CAPI_EUNSUPPORTED;
}

/*------------------------------------------------------------------------
  Function name: capi_data_marker_process
  Processes an input buffer and generates an output buffer.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_data_marker_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   POSAL_ASSERT(_pif);
   POSAL_ASSERT(input[0]);
   POSAL_ASSERT(output[0]);

   capi_err_t          result = CAPI_EOK;
   capi_data_marker_t *me_ptr = (capi_data_marker_t *)_pif;

   if (!me_ptr->is_in_media_fmt_set)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: 0x%lX process called: Input Media format not set yet", me_ptr->miid);
      return CAPI_EFAILED;
   }
   uint32_t input_before = input[0]->buf_ptr[0].actual_data_len;

   for (uint32_t i = 0; i < input[0]->bufs_num; i++)
   {
      /* in place module */
      if (input[0]->buf_ptr[i].data_ptr != output[0]->buf_ptr[i].data_ptr)
      {
#ifdef DATA_MARKER_DBG
         AR_MSG(DBG_HIGH_PRIO, "capi_data_marker: input and output data buffers data ptrs are different");
#endif
         memscpy(output[0]->buf_ptr[i].data_ptr,
                 output[0]->buf_ptr[i].max_data_len,
                 input[0]->buf_ptr[i].data_ptr,
                 input[0]->buf_ptr[i].actual_data_len);
      }

      uint32_t length = input[0]->buf_ptr[i].actual_data_len > output[0]->buf_ptr[i].max_data_len
                           ? output[0]->buf_ptr[i].max_data_len
                           : input[0]->buf_ptr[i].actual_data_len;
      output[0]->buf_ptr[i].actual_data_len = length;
      input[0]->buf_ptr[i].actual_data_len  = length;

#ifdef DATA_MARKER_DBG
      AR_MSG(DBG_HIGH_PRIO, "capi_data_marker: out buf actual data length: %d ", output[0]->buf_ptr[i].actual_data_len);
#endif
   }

   result = data_marker_handle_metadata(me_ptr, input_before, input, output);

   me_ptr->frame_counter++;

   return (result);
}

/*------------------------------------------------------------------------
  Function name: capi_data_marker_end
  Returns the library to the uninitialized state and frees the
  memory that was allocated by module. This function also frees the virtual
  function table.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_data_marker_end(capi_t *capi_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == capi_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_data_marker: End received bad pointer, %p", capi_ptr);
      return CAPI_EBADPARAM;
   }
   capi_data_marker_t *me_ptr = (capi_data_marker_t *)((capi_ptr));

   spf_list_delete_list_and_free_objs(&me_ptr->client_info_list_ptr, TRUE);
   spf_list_delete_list_and_free_objs(&me_ptr->insert_md_cfg_list_ptr, TRUE);
   me_ptr->vtbl_ptr = NULL;

   return result;
}
