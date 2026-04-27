/* ========================================================================
  @file capi_alsa_device.c
  @brief This file contains CAPI implementation of ALSA device Module

  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
  SPDX-License-Identifier: BSD-3-Clause-Clear
==============================================================================*/

/*=====================================================================
  Includes
 ======================================================================*/
#include "capi_alsa_device_i.h"
#include "spf_interleaver.h"

static capi_err_t capi_alsa_device_common_init(capi_t *_pif, capi_proplist_t *init_set_properties, uint32_t dir);

static capi_err_t capi_alsa_device_process_set_properties(capi_alsa_device_t *me_ptr, capi_proplist_t *proplist_ptr);

static capi_err_t capi_alsa_device_process_get_properties(capi_alsa_device_t *me_ptr, capi_proplist_t *proplist_ptr, uint32_t dir);

static capi_err_t capi_alsa_device_get_mf(capi_alsa_device_t *me_ptr, capi_media_fmt_v2_t *media_fmt_ptr);

static capi_err_t capi_alsa_device_raise_thresh_delay_events(capi_alsa_device_t *me_ptr);

static capi_err_t capi_alsa_device_raise_media_fmt_event(capi_alsa_device_t *me_ptr);

capi_err_t capi_alsa_device_process_source(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

capi_err_t capi_alsa_device_process_sink(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

/* Thread function that performs blocking reads into read_buffer and signals framework */
static void capi_alsa_device_dma_wait_thread(void *arg)
{
   capi_alsa_device_t *me_ptr = (capi_alsa_device_t *)arg;

   AR_MSG(DBG_HIGH_PRIO, "CAPI_ALSA_DEVICE: DMA wait thread started for instance 0x%lx", me_ptr->iid);

   while (!me_ptr->exit_thread)
   {
      /*
       * TODO: Ideally this thread should use alsa_device_driver_wait() to only poll
       * for hardware buffer availability and signal the framework, while the actual
       * alsa_device_driver_read() is done in process_source. However, the
       * alsa_device_driver_wait() based approach was found to be unreliable in
       * practice. Until this is investigated and resolved, a blocking
       * alsa_device_driver_read() is used here as a safe and functional workaround —
       * it blocks until the data is available.
       */
      ar_result_t result = alsa_device_driver_read(&me_ptr->alsa_device_driver,
                                                    me_ptr->read_buffer,
                                                    me_ptr->read_buffer_size);
      if (result != AR_EOK)
      {
         AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Read failed in DMA thread, result=%d", result);
         continue;
      }

      AR_MSG(DBG_HIGH_PRIO, "CAPI_ALSA_DEVICE: DMA thread read %d bytes", me_ptr->read_buffer_size);

      /* Mark data as ready */
      me_ptr->data_ready = TRUE;

      /* Signal framework using STM signal */
      if (me_ptr->signal_ptr && me_ptr->enable_stm)
      {
         AR_MSG(DBG_HIGH_PRIO, "CAPI_ALSA_DEVICE: signaling the container");
         posal_signal_send(me_ptr->signal_ptr);
      }
   }

   me_ptr->is_thread_running = FALSE;
   AR_MSG(DBG_HIGH_PRIO, "CAPI_ALSA_DEVICE: DMA wait thread exiting for instance 0x%lx", me_ptr->iid);
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_source_init
  DESCRIPTION: Initialize the CAPIv2 alsa_device source module and library.
  This function can allocate memory.
  -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_source_init(
   capi_t *_pif,
   capi_proplist_t *init_set_properties)
{
   return capi_alsa_device_common_init(_pif, init_set_properties, ALSA_DEVICE_SOURCE);
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_source_get_static_properties
  DESCRIPTION: Function to get the static properties of alsa device source module
  -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_source_get_static_properties(
   capi_proplist_t *init_set_properties,
   capi_proplist_t *static_properties)
{
   return capi_alsa_device_process_get_properties((capi_alsa_device_t *)NULL, static_properties, ALSA_DEVICE_SOURCE);
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_sink_init
  DESCRIPTION: Initialize the CAPIv2 alsa_device sink module and library.
  This function can allocate memory.
  -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_sink_init(
   capi_t *_pif,
   capi_proplist_t *init_set_properties)
{
   return capi_alsa_device_common_init(_pif, init_set_properties, ALSA_DEVICE_SINK);
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_sink_get_static_properties
  DESCRIPTION: Function to get the static properties of alsa device sink module
  -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_sink_get_static_properties(
   capi_proplist_t *init_set_properties,
   capi_proplist_t *static_properties)
{
   return capi_alsa_device_process_get_properties((capi_alsa_device_t *)NULL, static_properties, ALSA_DEVICE_SINK);
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_common_init
  DESCRIPTION: Initialize the CAPIv2 alsa_device module and library.
  This function can allocate memory.
  -----------------------------------------------------------------------*/
static capi_err_t capi_alsa_device_common_init(capi_t *_pif, capi_proplist_t *init_set_properties, uint32_t dir)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == _pif || NULL == init_set_properties)
   {
      AR_MSG(DBG_ERROR_PRIO,
            "CAPI_ALSA_DEVICE: Init received bad pointer, 0x%lx, 0x%lx",
            (uint32_t)_pif,
            (uint32_t)init_set_properties);
      return CAPI_EBADPARAM;
   }

   capi_alsa_device_t *me_ptr = (capi_alsa_device_t *)_pif;
   memset((void *)me_ptr, 0, sizeof(capi_alsa_device_t));

   // Allocate vtbl
   me_ptr->vtbl.vtbl_ptr = capi_alsa_device_get_vtbl();

   // Cache direction
   me_ptr->direction = dir;

   // Initialize thread management fields
   me_ptr->dma_wait_thread = NULL;
   me_ptr->is_thread_running = FALSE;
   me_ptr->exit_thread = FALSE;

   // Initialize read buffer fields (for SOURCE direction)
   me_ptr->read_buffer = NULL;
   me_ptr->read_buffer_size = 0;
   me_ptr->data_ready = FALSE;

   //Init alsa_device_driver
   alsa_device_driver_init(&me_ptr->alsa_device_driver);

   capi_result = capi_alsa_device_process_set_properties(me_ptr, init_set_properties);
   capi_result ^= (capi_result & CAPI_EUNSUPPORTED); // ignore unsupported
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: init set properties failed");
      return capi_result;
   }

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_process_set_properties
  DESCRIPTION: Function to set the properties for the alsa_device module
 * -----------------------------------------------------------------------*/
static capi_err_t capi_alsa_device_process_set_properties(capi_alsa_device_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == me_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Set property received null property.");
      return CAPI_EBADPARAM;
   }

   capi_result = capi_cmn_set_basic_properties(proplist_ptr, &me_ptr->heap_mem, &me_ptr->cb_info, FALSE);

   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Set basic properties failed with result %lu", capi_result);
      return capi_result;
   }

   uint32_t i = 0;
   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;
      switch (prop_ptr[i].id)
      {
         case CAPI_HEAP_ID:
         case CAPI_EVENT_CALLBACK_INFO:
         case CAPI_ALGORITHMIC_RESET:
         {
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            // If the set prop happens for module output port
            if (!prop_ptr[i].port_info.is_input_port)
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Incorrect port info, output port[%d]", prop_ptr[i].id);
               return CAPI_EBADPARAM;
            }

            // If the INTF direction is not SINK
            if (ALSA_DEVICE_SINK != me_ptr->direction)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CAPI_ALSA_DEVICE: Input media setting on non sink module: dir: %lu",
                      me_ptr->direction);
               return CAPI_EBADPARAM;
            }

            // If hw ep mf is not yet received
            if (FALSE == me_ptr->ep_mf_received)
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Hw ep mf cfg not received yet");
               return CAPI_EFAILED;
            }

            me_ptr->is_capi_in_media_fmt_set = FALSE;

            // Validate the MF payload
            if (payload_ptr->max_data_len < sizeof(capi_media_fmt_v2_t))
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Not valid media format size %d", payload_ptr->actual_data_len);
               return CAPI_EBADPARAM;
            }

            capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);

            // Validate media format against alsa device config
            uint32_t q       = media_fmt_ptr->format.q_factor;
            uint32_t bit_width = ((PCM_Q_FACTOR_15 == q) ? 16 : ((PCM_Q_FACTOR_27 == q) ? 24 : 32));
            if ((media_fmt_ptr->format.sampling_rate != me_ptr->sample_rate) ||
                (bit_width != me_ptr->bit_width) ||
                (media_fmt_ptr->format.num_channels != me_ptr->num_channels) ||
                (q == PCM_Q_FACTOR_23))
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Media format validation failed");
               AR_MSG(DBG_ERROR_PRIO,
                    "media format sampling rate: %d ALSA device config sampling rate: %d",
                    media_fmt_ptr->format.sampling_rate,
                    me_ptr->sample_rate);
               AR_MSG(DBG_ERROR_PRIO,
                    "media format bit width: %d, bits_per_sample %d, q factor %d. ALSA device config bit width: %d",
                    bit_width,
                    media_fmt_ptr->format.bits_per_sample,
                    q,
                    me_ptr->bit_width);
               AR_MSG(DBG_ERROR_PRIO,
                    "media format num_channels: %d ALSA device config num_channels: %d",
                    media_fmt_ptr->format.num_channels,
                    me_ptr->num_channels);
               return CAPI_EBADPARAM;
            }

            if (CAPI_DEINTERLEAVED_PACKED == media_fmt_ptr->format.data_interleaving)
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Packed data is not supported.");
               return CAPI_EBADPARAM;
            }

            // save info
            me_ptr->gen_cntr_alsa_device_media_fmt.format.num_channels     = media_fmt_ptr->format.num_channels;
            me_ptr->gen_cntr_alsa_device_media_fmt.format.data_interleaving = media_fmt_ptr->format.data_interleaving;
            me_ptr->gen_cntr_alsa_device_media_fmt.format.sampling_rate    = media_fmt_ptr->format.sampling_rate;
            me_ptr->gen_cntr_alsa_device_media_fmt.format.bits_per_sample =
                (BIT_WIDTH_16 == me_ptr->bit_width)
                ? BITS_PER_SAMPLE_16
                : (BIT_WIDTH_32 == me_ptr->bit_width) ? BITS_PER_SAMPLE_32
                                            : BITS_PER_SAMPLE_24;
            me_ptr->gen_cntr_alsa_device_media_fmt.header.format_header.data_format =
                media_fmt_ptr->header.format_header.data_format;

            me_ptr->is_capi_in_media_fmt_set = TRUE;

            break;
        }
        case CAPI_PORT_NUM_INFO:
        {
            if (payload_ptr->actual_data_len >= sizeof(capi_port_num_info_t))
            {
                capi_port_num_info_t *data_ptr = (capi_port_num_info_t *)payload_ptr->data_ptr;
                if (!(data_ptr->num_input_ports == 1 && data_ptr->num_output_ports == 0) &&
                !(data_ptr->num_input_ports == 0 && data_ptr->num_output_ports == 1))
                {
                  AR_MSG(DBG_ERROR_PRIO,
                        "CAPI_ALSA_DEVICE: Invalid number of input = %d, number of output ports = %d.",
                        data_ptr->num_input_ports,
                        data_ptr->num_output_ports);
                  CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
                }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Bad param size %lu", payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
        }
        case CAPI_CUSTOM_PROPERTY:
        {
            capi_custom_property_t *cust_prop_ptr    = (capi_custom_property_t *)payload_ptr->data_ptr;
            void *                  cust_payload_ptr = (void *)(cust_prop_ptr + 1);
            switch (cust_prop_ptr->secondary_prop_id)
            {
               case FWK_EXTN_PROPERTY_ID_STM_TRIGGER:
               {
                  if (payload_ptr->actual_data_len < sizeof(capi_custom_property_t) + sizeof(capi_prop_stm_trigger_t))
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "Property id 0x%lx Insufficient payload size %d",
                            (uint32_t)cust_prop_ptr->secondary_prop_id,
                            payload_ptr->actual_data_len);
                     return CAPI_EBADPARAM;
                  }
                  capi_prop_stm_trigger_t *trig_ptr = (capi_prop_stm_trigger_t *)cust_payload_ptr;
                  me_ptr->signal_ptr                = trig_ptr->signal_ptr;
                  me_ptr->enable_stm                = TRUE;
                  AR_MSG(DBG_HIGH_PRIO,
                         "FWK_EXTN_PROPERTY_ID_STM_TRIGGER enable_stm %d", me_ptr->enable_stm);
                  break;
               }
               case FWK_EXTN_PROPERTY_ID_STM_CTRL:
               {
                  if (payload_ptr->actual_data_len < sizeof(capi_prop_stm_ctrl_t))
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "Property id 0x%lx Bad param size %lu",
                            (uint32_t)cust_prop_ptr->secondary_prop_id,
                            payload_ptr->actual_data_len);
                     capi_result |= CAPI_ENEEDMORE;
                     break;
                  }
                  capi_prop_stm_ctrl_t *timer_en = (capi_prop_stm_ctrl_t *)cust_payload_ptr;
                  me_ptr->enable_stm             = timer_en->enable;
                  AR_MSG(DBG_HIGH_PRIO,
                         "FWK_EXTN_PROPERTY_ID_STM_CTRL enable_stm %d", me_ptr->enable_stm);

                  if (ALSA_DEVICE_SOURCE == me_ptr->direction &&
                     me_ptr->enable_stm && me_ptr->state != ALSA_DEVICE_INTERFACE_START)
                  {
                     capi_result = alsa_device_driver_open(&me_ptr->alsa_device_driver, me_ptr->direction);
                     if (capi_result != AR_EOK)
                     {
                        AR_MSG(DBG_ERROR_PRIO, "alsa_device_driver_open failed with error code %d", capi_result);
                        return CAPI_EFAILED;
                     }

                     capi_result = alsa_device_driver_prepare(&me_ptr->alsa_device_driver);
                     if (capi_result != AR_EOK)
                     {
                        AR_MSG(DBG_ERROR_PRIO,
                              "CAPI_ALSA_DEVICE: alsa_device_driver_prepare failed with error code %d",
                              capi_result);
                        return CAPI_EFAILED;
                     }

                     capi_result = alsa_device_driver_start(&me_ptr->alsa_device_driver);
                     if (capi_result != AR_EOK)
                     {
                        AR_MSG(DBG_ERROR_PRIO,
                              "CAPI_ALSA_DEVICE: alsa_device_driver_start failed with error code %d",
                              capi_result);
                        return CAPI_EFAILED;
                     }

                     if (NULL == me_ptr->read_buffer)
                     {
                        struct pcm_config *config = &me_ptr->alsa_device_driver.config;
                        me_ptr->read_buffer_size = config->period_size * config->channels * (me_ptr->bit_width / 8);

                        me_ptr->read_buffer = (int8_t *)posal_memory_malloc(
                              me_ptr->read_buffer_size,
                              (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);

                        if (NULL == me_ptr->read_buffer)
                        {
                           AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Failed to allocate read_buffer, size=%d",
                                    me_ptr->read_buffer_size);
                           return CAPI_ENOMEMORY;
                        }

                        AR_MSG(DBG_HIGH_PRIO, "CAPI_ALSA_DEVICE: Allocated read_buffer, size=%d bytes",
                                 me_ptr->read_buffer_size);
                     }

                     if (!me_ptr->is_thread_running)
                     {
                        me_ptr->exit_thread = FALSE;

                        ar_result_t thread_result = posal_thread_launch(&me_ptr->dma_wait_thread,
                                                                        "ALSA_DMA_WAIT",
                                                                        ALSA_DEVICE_STACK_SIZE,
                                                                        0,
                                                                        capi_alsa_device_dma_wait_thread,
                                                                        (void *)me_ptr,
                                                                        (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);

                        if (AR_EOK != thread_result)
                        {
                           AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Failed to create DMA wait thread");
                           return CAPI_EFAILED;
                        }

                        me_ptr->is_thread_running = TRUE;
                        AR_MSG(DBG_HIGH_PRIO, "CAPI_ALSA_DEVICE: DMA wait thread created successfully");
                     }

                     me_ptr->state = ALSA_DEVICE_INTERFACE_START;
                  }
                  break;
               }
               default:
               {
                  AR_MSG(DBG_HIGH_PRIO, "Unknown Custom Property[%d]", cust_prop_ptr->secondary_prop_id);
                  capi_result |= CAPI_EUNSUPPORTED;
                  break;
               }
            }
            break;
        }
        case CAPI_MODULE_INSTANCE_ID:
        {
            if (payload_ptr->actual_data_len >= sizeof(capi_module_instance_id_t))
            {
               capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
               me_ptr->iid = data_ptr->module_instance_id;
               AR_MSG(DBG_LOW_PRIO,
                     "CAPI_ALSA_DEVICE: This module-id 0x%08lX, instance-id 0x%08lX",
                     data_ptr->module_id,
                     me_ptr->iid);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                     "CAPI_ALSA_DEVICE: Set, Param id 0x%lx Bad param size %lu",
                     (uint32_t)prop_ptr[i].id,
                     payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
        }
        default:
        {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Skipping set prop, unsupported param[%d]", prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
            continue;
        }
      } /* Outer switch - Generic CAPI Properties */
   } /* Loop all properties */
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_process_get_properties
  DESCRIPTION: Function to get the properties for the alsa_device module
 * -----------------------------------------------------------------------*/
static capi_err_t capi_alsa_device_process_get_properties(capi_alsa_device_t *me_ptr, capi_proplist_t *proplist_ptr, uint32_t dir)
{
   capi_err_t capi_result = CAPI_EOK;
   uint32_t i = 0;

   capi_basic_prop_t mod_prop;
   uint32_t          num_intf_extns;
   uint32_t          fwk_extn_ids[1] = { 0 };
   mod_prop.init_memory_req = sizeof(capi_alsa_device_t);
   mod_prop.stack_size = ALSA_DEVICE_STACK_SIZE;

   if (ALSA_DEVICE_SOURCE == dir)
   {
      num_intf_extns  = ALSA_DEVICE_NUM_FRAMEWORK_EXTENSIONS_SOURCE;
      fwk_extn_ids[0] = FWK_EXTN_STM;
   }
   else
   {
      num_intf_extns  = ALSA_DEVICE_NUM_FRAMEWORK_EXTENSIONS_SINK;
   }

   mod_prop.num_fwk_extns      = num_intf_extns;
   mod_prop.fwk_extn_ids_arr   = fwk_extn_ids;
   mod_prop.is_inplace = 0;       // NA
   mod_prop.req_data_buffering = 0; // NA
   mod_prop.max_metadata_size = 0;  // NA

   capi_result = capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Get common basic properties failed with result %lu", capi_result);
      return capi_result;
   }

   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;
   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;
      switch (prop_ptr[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_STACK_SIZE:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         {
            break;
         }
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE : null ptr while querying output mf");
               return CAPI_EBADPARAM;
            }

            if (!(FALSE == prop_ptr[i].port_info.is_input_port))
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Incorrect port info, input port [%d]", prop_ptr[i].id);
               return CAPI_EBADPARAM;
            }

            // If the INTF direction is not source
            if (ALSA_DEVICE_SOURCE != me_ptr->direction)
            {
               AR_MSG(DBG_ERROR_PRIO,
                     "CAPI_ALSA_DEVICE: Media format query on non source alsa_device: dir: %lu",
                     me_ptr->direction);
               return CAPI_EBADPARAM;
            }

            // Validate the MF payload size
            if (payload_ptr->max_data_len < sizeof(capi_media_fmt_v2_t))
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Not valid media format size %d", payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
               break;
            }

            capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);

            // Memset the media format payload
            memset(media_fmt_ptr, 0, sizeof(capi_media_fmt_v2_t));

            // Retrieve mf
            capi_alsa_device_get_mf(me_ptr, media_fmt_ptr);

            payload_ptr->actual_data_len = sizeof(capi_media_fmt_v2_t);
            break;
         }
         case CAPI_PORT_DATA_THRESHOLD:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE : null ptr while querying threshold");
               return CAPI_EBADPARAM;
            }

            if (TRUE != me_ptr->ep_mf_received)
            {
               //AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Media format not received yet");
               return CAPI_EFAILED;
            }

            // based on int samples per period calculation (frame size in bytes)
            uint32_t threshold_in_bytes = me_ptr->int_samples_per_period *
                                 me_ptr->bytes_per_channel *
                                 me_ptr->num_channels;

            capi_result = capi_cmn_handle_get_port_threshold(&prop_ptr[i], threshold_in_bytes);
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
                        "CAPI_ALSA_DEVICE: CAPI_INTERFACE_EXTENSIONS invalid param size %lu",
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
                        case INTF_EXTN_DATA_PORT_OPERATION:
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
                           "CAPI_ALSA_DEVICE: CAPI_INTERFACE_EXTENSIONS intf_ext = 0x%lx, is_supported = %d",
                           curr_intf_extn_desc_ptr->id,
                           (int)curr_intf_extn_desc_ptr->is_supported);
                     curr_intf_extn_desc_ptr++;
                  }
               }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CAPI_ALSA_DEVICE: CAPI_INTERFACE_EXTENSIONS Bad param size %lu",
                      payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }

            break;
         }
         default:
         {
            AR_MSG(DBG_HIGH_PRIO, "CAPI_ALSA_DEVICE: Skipped Get Property for 0x%x. Not supported.", prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
            continue;
         }
      }
   }

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_get_mf
  DESCRIPTION: Function to get alsa device media fmt
 * -----------------------------------------------------------------------*/
static capi_err_t capi_alsa_device_get_mf(capi_alsa_device_t *me_ptr, capi_media_fmt_v2_t *media_fmt_ptr)
{
   media_fmt_ptr->header.format_header.data_format = me_ptr->gen_cntr_alsa_device_media_fmt.header.format_header.data_format;

   media_fmt_ptr->format.bitstream_format = MEDIA_FMT_ID_PCM;
   media_fmt_ptr->format.sampling_rate = me_ptr->sample_rate;
   media_fmt_ptr->format.num_channels = me_ptr->num_channels;
   if (CAPI_COMPR_OVER_PCM_PACKETIZED == me_ptr->gen_cntr_alsa_device_media_fmt.header.format_header.data_format)
   {
      media_fmt_ptr->format.data_interleaving = CAPI_INTERLEAVED;
   }
   else
   {
      media_fmt_ptr->format.data_interleaving = CAPI_DEINTERLEAVED_UNPACKED;
   }

   media_fmt_ptr->format.bits_per_sample = (BIT_WIDTH_16 == me_ptr->bit_width)
                                    ? BITS_PER_SAMPLE_16
                                    : BITS_PER_SAMPLE_32;

   if (BIT_WIDTH_24 == me_ptr->bit_width)
   {
      media_fmt_ptr->format.q_factor = PCM_Q_FACTOR_27;
   }
   else
   {
      // For bit_width of 16 its Q15 and for bitwidth of 32 its Q31
      media_fmt_ptr->format.q_factor = me_ptr->bit_width - 1;
   }

   /* Set default channel map */

   for (uint32_t ch = 0; ch < media_fmt_ptr->format.num_channels; ch++)
   {
      media_fmt_ptr->format.channel_type[ch] = ch + 1;
   }

   return CAPI_EOK;
}

static capi_vtbl_t vtbl = {capi_alsa_device_process, capi_alsa_device_end,
                     capi_alsa_device_set_param, capi_alsa_device_get_param,
                     capi_alsa_device_set_properties, capi_alsa_device_get_properties};

capi_vtbl_t *capi_alsa_device_get_vtbl()
{
   return &vtbl;
}

/*---------------------------------------------------------------------
  Function name: capi_alsa_device_process
  DESCRIPTION: Processes an input buffer and generates an output buffer.
  -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_process(capi_t *_pif,
                           capi_stream_data_t *input[],
                           capi_stream_data_t *output[])
{
   capi_err_t result = CAPI_EOK;
   capi_alsa_device_t *me_ptr = (capi_alsa_device_t *)_pif;

   if (ALSA_DEVICE_SINK == me_ptr->direction)
   {
      result |= capi_alsa_device_process_sink(_pif, input, output);
   }
   else // ALSA_DEVICE_SOURCE
   {
      result |= capi_alsa_device_process_source(_pif, input, output);
   }
   return result;
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_end
  DESCRIPTION: Returns the library to the uninitialized state and frees the
  memory that was allocated by init(). This function also frees the virtual
  function table.
  -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_end(capi_t *_pif)
{
   capi_err_t capi_result = CAPI_EOK;
   ar_result_t ar_result = AR_EOK;

   if (NULL == _pif)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: capi_alsa_device_end received bad pointer, 0x%p", _pif);
      return CAPI_EBADPARAM;
   }

   capi_alsa_device_t *me_ptr = (capi_alsa_device_t *)_pif;

   if (me_ptr->is_thread_running)
   {
      me_ptr->exit_thread = TRUE;
   }

   ar_result = alsa_device_driver_stop(&me_ptr->alsa_device_driver);
   if (ar_result != AR_EOK)
   {
      AR_MSG(DBG_ERROR_PRIO,
            "CAPI_ALSA_DEVICE: alsa_device_driver_stop failed with error code %d",
            ar_result);
      capi_result = CAPI_EFAILED;
   }

   ar_result = alsa_device_driver_close(&me_ptr->alsa_device_driver);
   if (ar_result != AR_EOK)
   {
      AR_MSG(DBG_ERROR_PRIO,
            "CAPI_ALSA_DEVICE: alsa_device_driver_close failed with error code %d",
            ar_result);
      capi_result = CAPI_EFAILED;
   }

   if (me_ptr->read_buffer)
   {
      posal_memory_free(me_ptr->read_buffer);
      me_ptr->read_buffer = NULL;
      me_ptr->read_buffer_size = 0;
      AR_MSG(DBG_HIGH_PRIO, "CAPI_ALSA_DEVICE: read_buffer freed");
   }

   me_ptr->state = ALSA_DEVICE_INTERFACE_STOP;
   me_ptr->vtbl.vtbl_ptr = NULL;
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_get_param
  DESCRIPTION: Gets either a parameter value or a parameter structure
  containing multiple parameters. In the event of a failure, the appropriate
  error code is returned.
 * -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_get_param(capi_t *_pif,
                             uint32_t param_id,
                             const capi_port_info_t *port_info_ptr,
                             capi_buf_t *params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if ((NULL == _pif) || (NULL == params_ptr) || (NULL == params_ptr->data_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Get param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_EBADPARAM;
   }

   switch (param_id)
   {
      case FWK_EXTN_PARAM_ID_LATEST_TRIGGER_TIMESTAMP_PTR:
      {
         // TBD
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "unsupported get param ID 0x%x", (int)param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
      }
   }
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_set_param
  DESCRIPTION: Sets either a parameter value or a parameter structure containing
  multiple parameters. In the event of a failure, the appropriate error code is
  returned.
  -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_set_param(capi_t *_pif,
                             uint32_t param_id,
                             const capi_port_info_t *port_info_ptr,
                             capi_buf_t *params_ptr)
{
   if ((NULL == _pif) || (NULL == params_ptr) || (NULL == params_ptr->data_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Set param received bad pointer");
      return CAPI_EBADPARAM;
   }

   capi_err_t capi_result = CAPI_EOK;
   ar_result_t ar_result = AR_EOK;
   capi_alsa_device_t *me_ptr = (capi_alsa_device_t *)_pif;
   uint32_t param_size = params_ptr->actual_data_len;

   switch (param_id)
   {
      case PARAM_ID_ALSA_DEVICE_INTF_CFG:
      {
         if (param_size < sizeof(param_id_alsa_device_intf_cfg_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
               "CAPI_ALSA_DEVICE: SetParam 0x%lx, invalid param size %lx ",
               param_id,
               params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
            break;
         }
         param_id_alsa_device_intf_cfg_t *alsa_device_cfg_ptr = (param_id_alsa_device_intf_cfg_t *) params_ptr->data_ptr;

         ar_result = alsa_device_driver_set_intf_cfg(alsa_device_cfg_ptr, &me_ptr->alsa_device_driver);

         if (AR_EOK != ar_result)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Driver set intf cfg failed with 0x%lx", ar_result);
            return CAPI_EFAILED;
         }

         break;
      }
      case PARAM_ID_HW_EP_MF_CFG:
      {
         if (param_size < sizeof(param_id_hw_ep_mf_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
               "CAPI_ALSA_DEVICE: SetParam 0x%lx, invalid param size %lx ",
               param_id,
               params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
            break;
         }

         param_id_hw_ep_mf_t *alsa_device_cfg_ptr = (param_id_hw_ep_mf_t *)params_ptr->data_ptr;

         ar_result = capi_alsa_device_set_hw_ep_mf_cfg(alsa_device_cfg_ptr, _pif);
         if (AR_EOK != ar_result)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Driver set ep mf failed with 0x%lx", ar_result);
            return CAPI_EFAILED;
         }

         // Output media format, threshold and algo event
         capi_result |= capi_alsa_device_raise_media_fmt_event(me_ptr);
         capi_result |= capi_alsa_device_raise_thresh_delay_events(me_ptr);
         if (CAPI_EOK != capi_result)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE:Failed to raise output media fmt and algo delay event", ar_result);
         }

         // KPPS event
         uint32_t kpps = (3 * (me_ptr->sample_rate) *
                        (me_ptr->num_channels)) /
                        640;
         capi_result = capi_cmn_update_kpps_event(&me_ptr->cb_info, kpps);

         // Bandwidth event
         uint32_t code_bandwidth = 0;
         uint32_t data_bandwidth = me_ptr->int_samples_per_period *
                           me_ptr->bit_width *
                           me_ptr->num_channels * 2;

         capi_result = capi_cmn_update_bandwidth_event(&me_ptr->cb_info, code_bandwidth, data_bandwidth);
         if (CAPI_EOK != capi_result)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Failed to send bandwidth update event with %lu", capi_result);
         }

         /* Scratch buffer is used only for sink direction, so allocating only for sink */
         if (ALSA_DEVICE_SINK == me_ptr->direction)
         {
            uint32_t buf_size = me_ptr->int_samples_per_period *
               me_ptr->num_channels *
               me_ptr->bytes_per_channel;

            // Reallocate the out_data_buffer if the size changed.
            if (NULL != me_ptr->out_data_buffer && me_ptr->out_data_buffer_size != buf_size)
            {
               posal_memory_free(me_ptr->out_data_buffer);
               me_ptr->out_data_buffer = NULL;
            }

            // Allocate the out_data_buffer on first time or if size changed.
            if (NULL == me_ptr->out_data_buffer)
            {
               me_ptr->out_data_buffer =
                  (int8_t *)posal_memory_malloc(buf_size, (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
               if (NULL == me_ptr->out_data_buffer)
               {
                  AR_MSG(DBG_ERROR_PRIO,
                        "CAPI_ALSA_DEVICE: Cannot allocate memory for scratch buffer, size = %lu",
                        buf_size);
                  capi_result = CAPI_ENOMEMORY;
               }
            }

            AR_MSG(DBG_HIGH_PRIO, "CAPI_ALSA_DEVICE: Allocated scratch buffer of size = %lu", buf_size);
            me_ptr->out_data_buffer_size = buf_size;
         }

         break;
      }
      case PARAM_ID_HW_EP_FRAME_SIZE_FACTOR:
      {
         if (param_size < sizeof(param_id_frame_size_factor_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                  "CAPI_ALSA_DEVICE: SetParam 0x%lx, invalid param size %lx ",
                  param_id,
                  params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
            break;
         }

         param_id_frame_size_factor_t *alsa_device_cfg_ptr = (param_id_frame_size_factor_t *)params_ptr->data_ptr;

         ar_result = capi_alsa_device_set_frame_size_cfg(alsa_device_cfg_ptr, _pif);
         if (AR_EOK != ar_result)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Driver set param failed with 0x%lx", ar_result);
            return CAPI_EFAILED;
         }

         if (me_ptr->ep_mf_received)
         {
            capi_result = capi_alsa_device_raise_thresh_delay_events(me_ptr);
            if (CAPI_EOK != capi_result)
            {
               AR_MSG(DBG_ERROR_PRIO,
                  "CAPI_ALSA_DEVICE: Failed to raise output media fmt and algo delay event",
                  ar_result);
            }
         }
         /* Scratch buffer is used only for sink direction, so allocating only for sink */
         if (ALSA_DEVICE_SINK == me_ptr->direction)
         {
            uint32_t buf_size = me_ptr->int_samples_per_period *
            me_ptr->num_channels *
            me_ptr->bytes_per_channel;

            // Reallocate the out_data_buffer if the size changed.
            if (NULL != me_ptr->out_data_buffer && me_ptr->out_data_buffer_size != buf_size)
            {
               posal_memory_free(me_ptr->out_data_buffer);
               me_ptr->out_data_buffer = NULL;
            }

            // Allocate the out_data_buffer on first time or if size changed.
            if (NULL == me_ptr->out_data_buffer)
            {
               me_ptr->out_data_buffer =
                  (int8_t *)posal_memory_malloc(buf_size, (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
               if (NULL == me_ptr->out_data_buffer)
               {
                  AR_MSG(DBG_ERROR_PRIO,
                        "CAPI_ALSA_DEVICE: Cannot allocate memory for scratch buffer, size = %lu",
                        buf_size);
                  capi_result = CAPI_ENOMEMORY;
               }
            }

            AR_MSG(DBG_HIGH_PRIO, "CAPI_ALSA_DEVICE: Allocated scratch buffer of size = %lu", buf_size);

            me_ptr->out_data_buffer_size = buf_size;
         }
         break;
      }
      case PARAM_ID_HW_DELAY:
      {
         if (NULL == params_ptr->data_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Set param id 0x%lX, received null buffer", param_id);
            capi_result |= CAPI_EBADPARAM;
            break;
         }

         if (param_size < sizeof(param_id_hw_delay_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
               "CAPI_ALSA_DEVICE: SetParam 0x%lx, invalid param size %lu ",
               param_id,
               params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
            break;
         }

         param_id_hw_delay_t *hw_delay_cfg_ptr = (param_id_hw_delay_t *)params_ptr->data_ptr;

         me_ptr->hw_delay_us = hw_delay_cfg_ptr->hw_delay_us;

         // In case HW delay is rcvd. after M.F raise algo delay again with HW delay included.
         if (me_ptr->ep_mf_received)
         {
            capi_result = capi_alsa_device_raise_thresh_delay_events(me_ptr);
            if (CAPI_EOK != capi_result)
            {
               AR_MSG(DBG_ERROR_PRIO,
                  "CAPI_ALSA_DEVICE :Failed to raise output media fmt and algo delay event",
                  capi_result);
            }
         }

         AR_MSG(DBG_HIGH_PRIO,
               "CAPI_ALSA_DEVICE: Received PARAM_ID_HW_DELAY with delay %d us",
               hw_delay_cfg_ptr->hw_delay_us);

         break;
      }
      default:
      {
         break;
      }
   }
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_get_properties
  DESCRIPTION: Function to get the properties for the alsa_device module
 * -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_get_properties(capi_t *_pif,
                                 capi_proplist_t *proplist_ptr)
{
   capi_alsa_device_t * me_ptr = (capi_alsa_device_t *) _pif;
   return capi_alsa_device_process_get_properties(me_ptr, proplist_ptr, me_ptr->direction);
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_set_properties
  DESCRIPTION: Function to set the properties for the alsa_device module
 * -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_set_properties(capi_t *_pif,
                                 capi_proplist_t *proplist_ptr)
{
   return capi_alsa_device_process_set_properties((capi_alsa_device_t *)_pif, proplist_ptr);
}

/*---------------------------------------------------------------------
  Function name: capi_alsa_device_process_sink
  DESCRIPTION: Processes an input buffer and generates an output buffer.
  -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_process_sink(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t capi_result = CAPI_EOK;
   ar_result_t ar_result = AR_EOK;
   capi_alsa_device_t *me_ptr = (capi_alsa_device_t *)_pif;
   uint16_t port = 0;
   uint32_t total_bytes = 0;
   uint32_t total_bytes_copied = 0, total_samples_copied = 0;
   uint16_t num_channels = 0;
   uint16_t bytes_per_channel = 0;
   uint16_t bytes_per_sample = 0;
   uint32_t word_size = 0;
   uint16_t num_samples_per_intr = 0;
   uint32_t expected_ip_len_per_ch = 0;
   bool_t need_to_underrun = FALSE;
   int i = 0;

   num_samples_per_intr = me_ptr->int_samples_per_period;
   num_channels = me_ptr->num_channels;
   bytes_per_channel = me_ptr->bytes_per_channel;
   bytes_per_sample = me_ptr->bit_width / 8;
   word_size = bytes_per_sample << 3;
   total_bytes = bytes_per_channel * num_samples_per_intr * num_channels;
   expected_ip_len_per_ch = bytes_per_channel * num_samples_per_intr;

   capi_buf_t scratch_buf = {.data_ptr = "NULL", .actual_data_len = 0, .max_data_len = 0};
   scratch_buf.max_data_len = me_ptr->out_data_buffer_size;
   scratch_buf.actual_data_len = 0;
   scratch_buf.data_ptr = me_ptr->out_data_buffer;

   bool_t need_to_reduce_underrun_print = TRUE;
   bool_t is_input_available = TRUE;

   if (me_ptr->state != ALSA_DEVICE_INTERFACE_START)
   {
      ar_result = alsa_device_driver_open(&me_ptr->alsa_device_driver, me_ptr->direction);
      if (ar_result != AR_EOK)
      {
         AR_MSG(DBG_ERROR_PRIO, "alsa_device_driver_open failed with error code %d", ar_result);
         return CAPI_EFAILED;
      }
      AR_MSG(DBG_HIGH_PRIO,
             "CAPI_ALSA_DEVICE: alsa_device_driver_open success rc: %d",
             ar_result);

      ar_result = alsa_device_driver_prepare(&me_ptr->alsa_device_driver);
      if (ar_result != AR_EOK)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CAPI_ALSA_DEVICE: alsa_device_driver_prepare failed with error code %d",
                ar_result);
         return CAPI_EFAILED;
      }
      AR_MSG(DBG_HIGH_PRIO,
             "CAPI_ALSA_DEVICE: alsa_device_driver_prepare success rc: %d",
             ar_result);

      me_ptr->state =ALSA_DEVICE_INTERFACE_START;
   }

   // Size checks
   is_input_available = capi_alsa_device_check_data_sufficiency(*input,
                                                  &scratch_buf,
                                                  total_bytes,
                                                  false, // no packetized data
                                                  me_ptr->is_capi_in_media_fmt_set,
                                                  expected_ip_len_per_ch,
                                                  num_channels,
                                                  &need_to_underrun);

   //data flow state
   need_to_reduce_underrun_print = capi_alsa_device_update_dataflow_state(input[port], &me_ptr->df_state, is_input_available);

   if (!need_to_underrun)
   {
      if (CAPI_INTERLEAVED == me_ptr->gen_cntr_alsa_device_media_fmt.format.data_interleaving)
      {
         //For interleaved data, all data will be in one data buffer, so simply do a memcopy
         memscpy(scratch_buf.data_ptr,
                 scratch_buf.max_data_len,
                 input[port]->buf_ptr[i].data_ptr,
                 input[port]->buf_ptr[i].actual_data_len);
         scratch_buf.actual_data_len = input[port]->buf_ptr[i].actual_data_len;
      }
      else
      {
         if ((AR_EOK != spf_deintlv_to_intlv(input[port]->buf_ptr, &scratch_buf, num_channels, word_size)))
         {
            need_to_underrun = TRUE;
            is_input_available = FALSE;
            AR_MSG_ISLAND(DBG_ERROR_PRIO, "CAPI: Failed to interleave data");
         }
      }
   }
   if (need_to_underrun)
   {
      bool_t is_eos_set = input[port] ? input[port]->flags.marker_eos : FALSE;
      capi_cmn_check_print_underrun_multiple_threshold(&(me_ptr->underrun_info),
                                                       me_ptr->iid,
                                                       need_to_reduce_underrun_print,
                                                       is_eos_set,
                                                       me_ptr->is_capi_in_media_fmt_set);

      //If need_to_underrun is true but there is input available, copy the available input and fill the rest of the scratch_buf with zeroes
      if (is_input_available)
      {
         if (CAPI_INTERLEAVED == me_ptr->gen_cntr_alsa_device_media_fmt.format.data_interleaving)
         {
            memscpy(scratch_buf.data_ptr,
                    scratch_buf.max_data_len,
                    input[port]->buf_ptr[i].data_ptr,
                    input[port]->buf_ptr[i].actual_data_len);
            scratch_buf.actual_data_len = input[port]->buf_ptr[i].actual_data_len;

            memset(scratch_buf.data_ptr + scratch_buf.actual_data_len,
                   0,
                   scratch_buf.max_data_len - scratch_buf.actual_data_len);
            scratch_buf.actual_data_len = scratch_buf.max_data_len;
         }
         else
         {
            spf_deintlv_to_intlv(input[port]->buf_ptr, &scratch_buf, num_channels, word_size);
            memset(scratch_buf.data_ptr + scratch_buf.actual_data_len,
                   0,
                   scratch_buf.max_data_len - scratch_buf.actual_data_len);
         }
      }
      //If no input available, fill the whole buffer with zeroes
      else
      {
         AR_MSG(DBG_ERROR_PRIO, "Writing zeros to output. Output buffer too small.");
         memset(scratch_buf.data_ptr, 0, scratch_buf.max_data_len);
         scratch_buf.actual_data_len = scratch_buf.max_data_len;
      }
   }

   total_bytes_copied = min(scratch_buf.actual_data_len, scratch_buf.max_data_len);

   ar_result = alsa_device_driver_write(&me_ptr->alsa_device_driver, me_ptr->out_data_buffer, total_bytes_copied);
   if (ar_result != AR_EOK)
   {
      AR_MSG(DBG_ERROR_PRIO, "alsa_device_driver_write failed with error code %d", ar_result);
      return CAPI_EFAILED;
   }

   AR_MSG_ISLAND(DBG_HIGH_PRIO, "CAPI: alsa_device_driver_write successful, total bytes copied: %d", total_bytes_copied);

   return capi_result;
}

/*---------------------------------------------------------------------
  Function name: capi_alsa_device_update_dataflow_state
  DESCRIPTION: .
 -----------------------------------------------------------------------*/
bool_t capi_alsa_device_update_dataflow_state(capi_stream_data_t *input, alsa_device_data_flow_state *data_flow_state, bool is_data_valid)
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
  Function name: capi_alsa_device_check_data_sufficiency
  DESCRIPTION: .
 -----------------------------------------------------------------------*/
bool_t capi_alsa_device_check_data_sufficiency(capi_stream_data_t *input,
                                    capi_buf_t *      scratch_buf,
                                    uint32_t         total_bytes,
                                    bool_t           packetized,
                                    bool_t           is_capi_in_media_fmt_set,
                                    uint32_t         expected_data_len,
                                    uint16_t         num_channels,
                                    bool_t *         need_to_underrun)
{
   bool_t is_data_valid = TRUE;
   int i = 0;

   if (!input || !input->buf_ptr || !input->buf_ptr[0].data_ptr)
   {
      is_data_valid = FALSE;
      *need_to_underrun = TRUE;
   }
   else if (!is_capi_in_media_fmt_set) // First check if rcvd. media format is correct or not. Mark input as invalid,
                              // input buffer size and scratch buffer size might be different.
   {
      *need_to_underrun = TRUE;
      is_data_valid = FALSE;
   }
   else if (input->flags.erasure)
   {
      *need_to_underrun = TRUE;
   }
   else if (scratch_buf->max_data_len < total_bytes)
   {
      is_data_valid = FALSE;
      *need_to_underrun = TRUE;
      AR_MSG(DBG_ERROR_PRIO, "total_bytes %u, max_data_len %u\n", __func__, __LINE__, total_bytes,scratch_buf->max_data_len);
   }
   else if (is_capi_in_media_fmt_set)
   {
      // if cop or packetized format its already interleaved,
      // if it is mono needs to follow other case since it needs double the size
      if (packetized)
      {
         // this will drop any partial data
         if (input->buf_ptr[0].actual_data_len < total_bytes)
         {
            *need_to_underrun = TRUE;
            AR_MSG(DBG_ERROR_PRIO,
               "CAPI_ALSA_DEVICE: dropping data. actual data len %d, needed len %d",
               input->buf_ptr[0].actual_data_len,
               total_bytes);
         }
      }
      else
      {
         // Check if the actual data length for each channel is less than the bytes per channel
         for (i = 0; i < num_channels; i++)
         {
            if (input->buf_ptr[i].actual_data_len < expected_data_len)
            {
               AR_MSG(DBG_ERROR_PRIO,
                     "CAPI_ALSA_DEVICE: dropping data. ch %d, actual data len %d, needed "
                     "len %d",
                     i,
                     input->buf_ptr[i].actual_data_len,
                     expected_data_len);

               *need_to_underrun = TRUE;
            }
         }
      }
   }

   return is_data_valid;
}

/*---------------------------------------------------------------------
  Function name: capi_alsa_device_process_source
  DESCRIPTION: Processes capture (source) - copies data from read_buffer
               populated by DMA thread to output buffers
  -----------------------------------------------------------------------*/
capi_err_t capi_alsa_device_process_source(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t capi_result = CAPI_EOK;
   capi_alsa_device_t *me_ptr = (capi_alsa_device_t *)_pif;
   uint16_t port = 0;
   uint32_t total_bytes = 0;
   uint16_t num_channels = 0;
   uint16_t bytes_per_sample = 0;
   uint32_t word_size = 0;

   if (me_ptr->state != ALSA_DEVICE_INTERFACE_START)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Source not started");
      return CAPI_EFAILED;
   }

   if (!output || !output[port] || !output[port]->buf_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Invalid output buffer");
      return CAPI_EBADPARAM;
   }

   num_channels = me_ptr->num_channels;
   bytes_per_sample = me_ptr->bit_width / 8;
   word_size = bytes_per_sample << 3;
   total_bytes = me_ptr->read_buffer_size;

   if (!me_ptr->data_ready)
   {
      // Data not ready - this is an underrun condition
      AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Underrun - no data available in read_buffer");

      // Fill output with zeros
      if (CAPI_DEINTERLEAVED_UNPACKED == me_ptr->gen_cntr_alsa_device_media_fmt.format.data_interleaving)
      {
         uint32_t bytes_per_ch = total_bytes / num_channels;
         for (uint32_t ch = 0; ch < num_channels; ch++)
         {
            memset(output[port]->buf_ptr[ch].data_ptr, 0, bytes_per_ch);
            output[port]->buf_ptr[ch].actual_data_len = bytes_per_ch;
         }
      }
      else // CAPI_INTERLEAVED
      {
         memset(output[port]->buf_ptr[0].data_ptr, 0, total_bytes);
         output[port]->buf_ptr[0].actual_data_len = total_bytes;
      }
      return CAPI_EOK;
   }

   // Data is ready - copy from read_buffer
   if (CAPI_DEINTERLEAVED_UNPACKED == me_ptr->gen_cntr_alsa_device_media_fmt.format.data_interleaving)
   {
      capi_buf_t intlv_buf;
      intlv_buf.data_ptr = me_ptr->read_buffer;
      intlv_buf.actual_data_len = total_bytes;
      intlv_buf.max_data_len = me_ptr->read_buffer_size;

      // Deinterleave to output buffers
      if (AR_EOK != spf_intlv_to_deintlv(&intlv_buf,
                                         output[port]->buf_ptr,
                                         num_channels,
                                         word_size))
      {
         AR_MSG(DBG_ERROR_PRIO, "CAPI_ALSA_DEVICE: Failed to deinterleave data");
         return CAPI_EFAILED;
      }

      // Update actual data length for each channel
      uint32_t bytes_per_ch = total_bytes / num_channels;
      for (uint32_t ch = 0; ch < num_channels; ch++)
      {
         output[port]->buf_ptr[ch].actual_data_len = bytes_per_ch;
      }
   }
   else // CAPI_INTERLEAVED
   {
      // For interleaved output, copy directly to first buffer
      if (output[port]->buf_ptr[0].max_data_len < total_bytes)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CAPI_ALSA_DEVICE: Output buffer too small. Required: %d, Available: %d",
                total_bytes,
                output[port]->buf_ptr[0].max_data_len);
         return CAPI_ENOMEMORY;
      }

      memscpy(output[port]->buf_ptr[0].data_ptr,
              output[port]->buf_ptr[0].max_data_len,
              me_ptr->read_buffer,
              total_bytes);
      output[port]->buf_ptr[0].actual_data_len = total_bytes;
   }

   me_ptr->data_ready = FALSE;

   AR_MSG_ISLAND(DBG_HIGH_PRIO, "CAPI_ALSA_DEVICE: Process source successful, bytes: %d", total_bytes);

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_raise_media_fmt_event
  DESCRIPTION: Function to raise media fmt events for the alsa_device module
 * -----------------------------------------------------------------------*/
static capi_err_t capi_alsa_device_raise_media_fmt_event(capi_alsa_device_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   bool_t is_input_port = FALSE;
   if (ALSA_DEVICE_SOURCE == me_ptr->direction)
   {
      capi_media_fmt_v2_t out_media_fmt;

      // Memset the media format payload
      memset((void *)&out_media_fmt, 0, sizeof(capi_media_fmt_v2_t));

      // Retrieve mf
      capi_alsa_device_get_mf(me_ptr, &out_media_fmt);

      capi_result = capi_cmn_output_media_fmt_event_v2(&me_ptr->cb_info, &out_media_fmt, is_input_port, 0);
   }
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_raise_thresh_delay_events
  DESCRIPTION: Function to raise algo delay and threshold events for the alsa_device module
 * -----------------------------------------------------------------------*/
static capi_err_t capi_alsa_device_raise_thresh_delay_events(capi_alsa_device_t *me_ptr)
{
   capi_err_t capi_result   = CAPI_EOK;
   bool_t    is_input_port = FALSE;

   // Algo delay event, converts frame size to microseconds and add HW delay reported.
   uint32_t delay_in_us = (uint32_t)(((me_ptr->int_samples_per_period * NUM_US_PER_SEC) /
                           (me_ptr->sample_rate)) + me_ptr->hw_delay_us);

   capi_result = capi_cmn_update_algo_delay_event(&me_ptr->cb_info, delay_in_us);

   // Port threshold event
   uint32_t threshold = me_ptr->int_samples_per_period *
                  me_ptr->bytes_per_channel *
                  me_ptr->num_channels;

   if (ALSA_DEVICE_SINK == me_ptr->direction)
   {
      is_input_port = TRUE;
   }
   capi_result = capi_cmn_update_port_data_threshold_event(&me_ptr->cb_info, threshold, is_input_port, 0);

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_set_hw_ep_mf_cfg
  DESCRIPTION: Function to set alsa device ep mf configuration
 * -----------------------------------------------------------------------*/
ar_result_t capi_alsa_device_set_hw_ep_mf_cfg(param_id_hw_ep_mf_t *alsa_device_cfg_ptr, capi_t *_pif)
{
   capi_alsa_device_t *me_ptr = (capi_alsa_device_t *)_pif;

   if ((NULL == alsa_device_cfg_ptr) || (NULL == me_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA Device: Pointer to alsa device handle/cfg_ptr pointer is null");
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO,
         "ALSA Device: Received hw media format config: sample rate = %d, bit width = %d, num channels = %d, data "
         "format "
         "= %d",
         alsa_device_cfg_ptr->sample_rate,
         alsa_device_cfg_ptr->bit_width,
         alsa_device_cfg_ptr->num_channels,
         alsa_device_cfg_ptr->data_format);

   if (ALSA_DEVICE_INTERFACE_START == me_ptr->state)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA Device: interface already running, config not allowed");
      return AR_EALREADY;
   }

   me_ptr->ep_mf_received = FALSE;

   switch (alsa_device_cfg_ptr->bit_width)
   {
      case BITS_PER_SAMPLE_16:
      case BITS_PER_SAMPLE_24:
      case BITS_PER_SAMPLE_32:
      {
         me_ptr->bit_width = alsa_device_cfg_ptr->bit_width;
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "ALSA Device un-supported bit width: %x", alsa_device_cfg_ptr->bit_width);
         return AR_EBADPARAM;
      }
   }

   me_ptr->bytes_per_channel = (me_ptr->bit_width > BITS_PER_SAMPLE_16) ? 4 : 2;

   /* Determining the qformat shift factor based on bit width. We use this shift factor for 24bit only*/
   if (BITS_PER_SAMPLE_24 == me_ptr->bit_width)
   {
      me_ptr->q_format_shift_factor = QFORMAT_SHIFT_FACTOR;
   }
   else
   {
      me_ptr->q_format_shift_factor = 0;
   }

   switch (alsa_device_cfg_ptr->data_format)
   {
      case DATA_FORMAT_COMPR_OVER_PCM_PACKETIZED:
      case DATA_FORMAT_FIXED_POINT:
      {
         me_ptr->data_format = alsa_device_cfg_ptr->data_format;
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "ALSA Device un-supported data format: 0x%x", alsa_device_cfg_ptr->data_format);
         return AR_EBADPARAM;
      }
   }

   switch (alsa_device_cfg_ptr->sample_rate)
   {
      case SAMPLE_RATE_8K:
      case SAMPLE_RATE_11_025K:
      case SAMPLE_RATE_12K:
      case SAMPLE_RATE_16K:
      case SAMPLE_RATE_22_05K:
      case SAMPLE_RATE_24K:
      case SAMPLE_RATE_32K:
      case SAMPLE_RATE_44_1K:
      case SAMPLE_RATE_48K:
      case SAMPLE_RATE_88_2K:
      case SAMPLE_RATE_96K:
      case SAMPLE_RATE_176_4K:
      case SAMPLE_RATE_192K:
      case SAMPLE_RATE_352_8K:
      case SAMPLE_RATE_384K:
      {
         me_ptr->sample_rate = alsa_device_cfg_ptr->sample_rate;
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "ALSA Device un-supported sampling rate: %lu", alsa_device_cfg_ptr->sample_rate);
         return AR_EBADPARAM;
      }
   }

   me_ptr->num_channels = alsa_device_cfg_ptr->num_channels;

   /* Initialize data_format for SOURCE direction (for SINK it's set via CAPI_INPUT_MEDIA_FORMAT_V2) */
   if (ALSA_DEVICE_SOURCE == me_ptr->direction)
   {
      if (DATA_FORMAT_COMPR_OVER_PCM_PACKETIZED == me_ptr->data_format)
      {
         me_ptr->gen_cntr_alsa_device_media_fmt.header.format_header.data_format = CAPI_COMPR_OVER_PCM_PACKETIZED;
      }
      else
      {
         me_ptr->gen_cntr_alsa_device_media_fmt.header.format_header.data_format = CAPI_FIXED_POINT;
      }

      AR_MSG(DBG_HIGH_PRIO, "ALSA Device: Initialized data_format for SOURCE direction: %d",
             me_ptr->gen_cntr_alsa_device_media_fmt.header.format_header.data_format);
   }

   if (me_ptr->frame_size_cfg_received)
   {
      // setting operating frame_size
      if (FRAME_SIZE_MAX_MS >= me_ptr->frame_size_ms && FRAME_SIZE_MIN_MS <= me_ptr->frame_size_ms)
      {
         me_ptr->int_samples_per_period =
            (me_ptr->sample_rate / NUM_MS_PER_SEC) * me_ptr->frame_size_ms;
      }
      else
      {
         AR_MSG(DBG_ERROR_PRIO, "ALSA Device: Un-supported frame size factor %d", me_ptr->frame_size_ms);
         return AR_EBADPARAM;
      }
   }
   else
   {
      // Set default frame size to 1ms
      me_ptr->int_samples_per_period = me_ptr->sample_rate / NUM_MS_PER_SEC;
   }
   alsa_device_driver_set_cfg(&me_ptr->alsa_device_driver, alsa_device_cfg_ptr);
   // Set flag to true
   me_ptr->ep_mf_received = TRUE;

   return AR_EOK;
}

/*------------------------------------------------------------------------
  Function name: capi_alsa_device_set_frame_size_cfg
  DESCRIPTION: Function to set alsa device frame size configuration
 * -----------------------------------------------------------------------*/
ar_result_t capi_alsa_device_set_frame_size_cfg(param_id_frame_size_factor_t *alsa_device_cfg_ptr,
                                     capi_t *_pif)
{
   ar_result_t result = AR_EOK;
   capi_alsa_device_t *me_ptr = (capi_alsa_device_t *)_pif;

   if ((NULL == me_ptr) || (NULL == alsa_device_cfg_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE: Pointer to alsa device handle/cfg_ptr pointer is null");
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO,
         "ALSA_DEVICE: Received frame size config: frame size = %d ms",
          alsa_device_cfg_ptr->frame_size_factor);

   if (ALSA_DEVICE_INTERFACE_START == me_ptr->state)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE: interface already running, config not allowed");
      return AR_EALREADY;
   }

   me_ptr->frame_size_cfg_received = FALSE;

   if (me_ptr->ep_mf_received)
   {
      // setting operating frame_size
      if (FRAME_SIZE_MAX_MS >= alsa_device_cfg_ptr->frame_size_factor &&
         FRAME_SIZE_MIN_MS <= alsa_device_cfg_ptr->frame_size_factor)
      {
         me_ptr->int_samples_per_period =
            (me_ptr->sample_rate / NUM_MS_PER_SEC) * alsa_device_cfg_ptr->frame_size_factor;
      }
      else
      {
         AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE: Un-supported frame size: %d ms",
               alsa_device_cfg_ptr->frame_size_factor);
         return AR_EBADPARAM;
      }
   }

   me_ptr->frame_size_ms = alsa_device_cfg_ptr->frame_size_factor;

   result = alsa_device_driver_set_frame_size_cfg(alsa_device_cfg_ptr, (&me_ptr->alsa_device_driver));
   // Set flag to true
   me_ptr->frame_size_cfg_received = TRUE;

   return result;
}
