/**
 * \file gen_topo_capi.c
 * \brief
 *     This file contains basic topo uitility functions for capi v2 handling.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"
#include "gen_topo_capi.h"

/* =======================================================================
Public Function Definitions
========================================================================== */

// when 1 is returned, it means capi can work with any size.
#define DEFAULT_PORT_THRESHOLD 1

#define CONVERT_GEN_TOPO_INVALID_TO_CAPI_INVALID(x) ((0 == x) ? CAPI_DATA_FORMAT_INVALID_VAL : x)

static void init_media_fmt(capi_standard_data_format_t *std_media_fmt)
{
   std_media_fmt->bits_per_sample   = CAPI_DATA_FORMAT_INVALID_VAL;
   std_media_fmt->bitstream_format  = CAPI_DATA_FORMAT_INVALID_VAL;
   std_media_fmt->data_interleaving = CAPI_INVALID_INTERLEAVING;
   std_media_fmt->data_is_signed    = CAPI_DATA_FORMAT_INVALID_VAL;
   std_media_fmt->num_channels      = CAPI_DATA_FORMAT_INVALID_VAL;
   std_media_fmt->q_factor          = CAPI_DATA_FORMAT_INVALID_VAL;
   std_media_fmt->sampling_rate     = CAPI_DATA_FORMAT_INVALID_VAL;

   for (uint32_t j = 0; (j < CAPI_MAX_CHANNELS); j++)
   {
      std_media_fmt->channel_type[j] = (uint16_t)CAPI_DATA_FORMAT_INVALID_VAL;
   }
}

static void init_media_fmt_v2(capi_standard_data_format_v2_t *std_media_fmt)
{
   std_media_fmt->minor_version     = CAPI_DATA_FORMAT_INVALID_VAL;
   std_media_fmt->bits_per_sample   = CAPI_DATA_FORMAT_INVALID_VAL;
   std_media_fmt->bitstream_format  = CAPI_DATA_FORMAT_INVALID_VAL;
   std_media_fmt->data_interleaving = CAPI_INVALID_INTERLEAVING;
   std_media_fmt->data_is_signed    = CAPI_DATA_FORMAT_INVALID_VAL;
   std_media_fmt->num_channels      = CAPI_DATA_FORMAT_INVALID_VAL;
   std_media_fmt->q_factor          = CAPI_DATA_FORMAT_INVALID_VAL;
   std_media_fmt->sampling_rate     = CAPI_DATA_FORMAT_INVALID_VAL;
}

static ar_result_t gen_topo_capi_get_stack_size(void *           amdb_handle,
                                                uint32_t         log_id,
                                                uint32_t *       stack_size,
                                                capi_proplist_t *init_proplist_ptr)
{
   capi_err_t err_code = CAPI_EOK;
   INIT_EXCEPTION_HANDLING
   *stack_size = 0;

   capi_proplist_t   props_list;
   capi_prop_t       prop[1]; // query only one property at once.
   capi_stack_size_t stack_size_struct = { 0 };

   uint32_t i = 0;

   i                               = 0;
   prop[i].id                      = CAPI_STACK_SIZE;
   prop[i].payload.actual_data_len = 0;
   prop[i].payload.max_data_len    = sizeof(capi_stack_size_t);
   prop[i].payload.data_ptr        = (int8_t *)&stack_size_struct;
   prop[i].port_info.is_valid      = FALSE;
   i++;

   props_list.prop_ptr  = prop;
   props_list.props_num = i;

   TRY(err_code, amdb_capi_get_static_properties_f((void *)amdb_handle, init_proplist_ptr, &props_list));

   *stack_size = stack_size_struct.size_in_bytes;

   CATCH(err_code, TOPO_MSG_PREFIX, log_id)
   {
      return capi_err_to_ar_result(err_code);
   }
   return AR_EOK;
}

#define NUM_INIT_PARAMS 6
static ar_result_t gen_topo_prepare_init_proplist_util_(capi_proplist_t *           init_proplist_ptr,
                                                        capi_prop_t                 init_props[],
                                                        capi_event_callback_info_t *cb_obj_ptr,
                                                        capi_heap_id_t *            heap_id_ptr,
                                                        capi_port_num_info_t *      port_info_ptr,
                                                        capi_module_instance_id_t * miid_ptr,
                                                        capi_logging_info_t *       logging_info_ptr)
{
   ar_result_t result = AR_EOK;

   uint32_t i = 0;

   init_props[i].id                      = CAPI_EVENT_CALLBACK_INFO;
   init_props[i].payload.actual_data_len = sizeof(capi_event_callback_info_t);
   init_props[i].payload.max_data_len    = sizeof(capi_event_callback_info_t);
   init_props[i].payload.data_ptr        = (int8_t *)(cb_obj_ptr);
   init_props[i].port_info.is_valid      = FALSE;
   i++;

   init_props[i].id                      = CAPI_HEAP_ID;
   init_props[i].payload.actual_data_len = sizeof(capi_heap_id_t);
   init_props[i].payload.max_data_len    = sizeof(capi_heap_id_t);
   init_props[i].payload.data_ptr        = (int8_t *)(heap_id_ptr);
   init_props[i].port_info.is_valid      = FALSE;
   i++;

   init_props[i].id                      = CAPI_PORT_NUM_INFO;
   init_props[i].payload.actual_data_len = sizeof(capi_port_num_info_t);
   init_props[i].payload.max_data_len    = sizeof(capi_port_num_info_t);
   init_props[i].payload.data_ptr        = (int8_t *)(port_info_ptr);
   init_props[i].port_info.is_valid      = FALSE;
   i++;

   init_props[i].id                      = CAPI_MODULE_INSTANCE_ID;
   init_props[i].payload.actual_data_len = sizeof(capi_module_instance_id_t);
   init_props[i].payload.max_data_len    = sizeof(capi_module_instance_id_t);
   init_props[i].payload.data_ptr        = (int8_t *)(miid_ptr);
   init_props[i].port_info.is_valid      = FALSE;
   i++;

   init_props[i].id                      = CAPI_LOGGING_INFO;
   init_props[i].payload.actual_data_len = sizeof(capi_logging_info_t);
   init_props[i].payload.max_data_len    = sizeof(capi_logging_info_t);
   init_props[i].payload.data_ptr        = (int8_t *)(logging_info_ptr);
   init_props[i].port_info.is_valid      = FALSE;
   i++;

   if (i >= NUM_INIT_PARAMS)
   {
      return AR_EFAILED;
   }

   init_proplist_ptr->props_num = i;
   init_proplist_ptr->prop_ptr  = init_props;

   return result;
}

/* Returns Max stack size required for all the opened modules */
ar_result_t gen_topo_get_aggregated_capi_stack_size(gen_topo_t *topo_ptr, uint32_t *max_stack_size)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   uint32_t log_id = topo_ptr->gu.log_id;

   uint8_t num_gu       = 1;
   gu_t *  gu_ptr_arr[] = { &topo_ptr->gu, NULL };
   gu_t *  open_gu_ptr  = get_gu_ptr_for_current_command_context(gu_ptr_arr[0]);
   if (open_gu_ptr != gu_ptr_arr[0])
   {
      gu_ptr_arr[1] = open_gu_ptr;
      num_gu++;
   }

   for (int i = 0; i < num_gu; i++)
   {
      gu_t *gu_ptr = gu_ptr_arr[i];
      for (gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
      {
         gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;
         for (gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr; (NULL != module_list_ptr);
              LIST_ADVANCE(module_list_ptr))
         {
            gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

            // Skip querying stack size from Stub and framework modules.
            if ((AMDB_INTERFACE_TYPE_STUB == module_ptr->gu.itype) ||
                (AMDB_MODULE_TYPE_FRAMEWORK == module_ptr->gu.module_type))
            {
               continue;
            }

            /* port info */
            capi_proplist_t init_proplist;
            capi_prop_t     init_props[NUM_INIT_PARAMS];

            capi_event_callback_info_t cb_obj = { .event_cb = topo_ptr->capi_cb, .event_context = (void *)module_ptr };

            capi_port_num_info_t num_max_ports_info = { .num_input_ports  = module_ptr->gu.max_input_ports,
                                                        .num_output_ports = module_ptr->gu.max_output_ports };

            /**
             * container debug msg logging uses upper 16 bits (lower 16 are zeros).
             * modules need its instance num (within container) as well as EoS/flush bits
             */
            uint32_t      temp_log_id = log_id;
            POSAL_HEAP_ID mem_heap_id = module_ptr->gu.module_heap_id;
            gen_topo_get_mod_heap_id_and_log_id(&temp_log_id, &mem_heap_id, module_ptr->serial_num, mem_heap_id);
            capi_heap_id_t            heap_id = { (uint32_t)mem_heap_id };
            capi_module_instance_id_t miid    = { .module_id          = module_ptr->gu.module_id,
                                               .module_instance_id = module_ptr->gu.module_instance_id };
            capi_logging_info_t logging_info  = { .log_id = temp_log_id, .log_id_mask = LOG_ID_LOG_DISCONTINUITY_MASK };

            TRY(result,
                gen_topo_prepare_init_proplist_util_(&init_proplist,
                                                     init_props,
                                                     &cb_obj,
                                                     &heap_id,
                                                     &num_max_ports_info,
                                                     &miid,
                                                     &logging_info));

            // get stack size and find max.
            uint32_t stack_size = 0;
            TRY(result,
                gen_topo_capi_get_stack_size((void *)module_ptr->gu.amdb_handle,
                                             topo_ptr->gu.log_id,
                                             &stack_size,
                                             &init_proplist));

            *max_stack_size = MAX(*max_stack_size, stack_size);
         }
      }
   }

   CATCH(result, TOPO_MSG_PREFIX, log_id)
   {
      TOPO_MSG(log_id, DBG_HIGH_PRIO, "Failed to get aggregated stack size result 0x%lx", result);
   }

   return result;
}

static ar_result_t gen_topo_capi_is_inplace_n_requires_data_buf(void *           amdb_handle,
                                                                uint32_t         module_instance_id,
                                                                uint32_t         log_id,
                                                                capi_proplist_t *init_proplist_ptr,
                                                                bool_t *         is_inplace,
                                                                bool_t *         requires_data_buf)
{
   capi_err_t err_code = CAPI_EOK;

   *is_inplace        = FALSE;
   *requires_data_buf = FALSE;

   capi_proplist_t                props_list;
   capi_prop_t                    prop[2];
   capi_is_inplace_t              is_in_place;
   capi_requires_data_buffering_t rdf;
   uint32_t                       i = 0;
   memset(&is_in_place, 0, sizeof(capi_is_inplace_t));
   memset(&rdf, 0, sizeof(capi_requires_data_buffering_t));

   prop[i].id                      = CAPI_IS_INPLACE;
   prop[i].payload.actual_data_len = 0;
   prop[i].payload.max_data_len    = sizeof(capi_is_inplace_t);
   prop[i].payload.data_ptr        = (int8_t *)&is_in_place;
   prop[i].port_info.is_valid      = FALSE;
   i++;

   prop[i].id                      = CAPI_REQUIRES_DATA_BUFFERING;
   prop[i].payload.actual_data_len = 0;
   prop[i].payload.max_data_len    = sizeof(capi_requires_data_buffering_t);
   prop[i].payload.data_ptr        = (int8_t *)&rdf;
   prop[i].port_info.is_valid      = FALSE;
   i++;

   props_list.prop_ptr  = prop;
   props_list.props_num = i;

   err_code = amdb_capi_get_static_properties_f((void *)amdb_handle, init_proplist_ptr, &props_list);
   if (CAPI_FAILED(err_code))
   {
      TOPO_MSG(log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX: Get properties error. Assuming capi is not inplace, and does not require data buffering.",
               module_instance_id);
      is_in_place.is_inplace      = FALSE;
      rdf.requires_data_buffering = FALSE;
      err_code                    = CAPI_EOK; // in-place & rdf query are not fatal
   }
   else
   {
      TOPO_MSG(log_id,
               DBG_LOW_PRIO,
               "Module 0x%lX: inplace = %u, requires-data-buffering = %u",
               module_instance_id,
               is_in_place.is_inplace,
               rdf.requires_data_buffering);
   }

   *is_inplace        = is_in_place.is_inplace;
   *requires_data_buf = rdf.requires_data_buffering;

   return err_code;
}

ar_result_t gen_topo_capi_get_required_fmwk_extensions(void *           topo_ctx_ptr,
                                                       void *           module_ctx_ptr,
                                                       void *           amdb_handle,
                                                       capi_proplist_t *init_proplist_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   capi_err_t                     err_code                = CAPI_EOK;
   capi_framework_extension_id_t *needed_fmwk_xtn_ids_arr = NULL;
   gen_topo_module_t *            module_ptr              = (gen_topo_module_t *)module_ctx_ptr;
   gen_topo_t *                   topo_ptr                = (gen_topo_t *)topo_ctx_ptr;

   uint32_t module_instance_id = module_ptr->gu.module_instance_id;
   uint32_t log_id             = module_ptr->topo_ptr->gu.log_id;

   capi_proplist_t props_list;
   capi_prop_t     prop[1]; // query only one property at once.

   capi_num_needed_framework_extensions_t num_fmwk_extns = { 0 };

   uint32_t i;

   i                               = 0;
   prop[i].id                      = CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS;
   prop[i].payload.actual_data_len = 0;
   prop[i].payload.max_data_len    = sizeof(num_fmwk_extns);
   prop[i].payload.data_ptr        = (int8_t *)&num_fmwk_extns;
   prop[i].port_info.is_valid      = FALSE;
   i++;

   props_list.prop_ptr  = prop;
   props_list.props_num = i;

   err_code = amdb_capi_get_static_properties_f((void *)amdb_handle, init_proplist_ptr, &props_list);
   // ignore any error since params are options.

   if ((num_fmwk_extns.num_extensions != 0) && (CAPI_SUCCEEDED(err_code)))
   {
      needed_fmwk_xtn_ids_arr = (capi_framework_extension_id_t *)
         posal_memory_malloc(sizeof(capi_framework_extension_id_t) * num_fmwk_extns.num_extensions, topo_ptr->heap_id);

      VERIFY(result, (NULL != needed_fmwk_xtn_ids_arr));

      i                               = 0;
      prop[i].id                      = CAPI_NEEDED_FRAMEWORK_EXTENSIONS;
      prop[i].payload.actual_data_len = 0;
      prop[i].payload.max_data_len    = sizeof(capi_framework_extension_id_t) * num_fmwk_extns.num_extensions;
      prop[i].payload.data_ptr        = (int8_t *)needed_fmwk_xtn_ids_arr;
      prop[i].port_info.is_valid      = FALSE;
      i++;

      props_list.prop_ptr  = prop;
      props_list.props_num = i;

      err_code = amdb_capi_get_static_properties_f((void *)amdb_handle, init_proplist_ptr, &props_list);

      bool_t any_extn_not_supported = FALSE;
      for (i = 0; i < num_fmwk_extns.num_extensions; i++)
      {
         bool_t extn_supported = TRUE;
         switch (needed_fmwk_xtn_ids_arr[i].id)
         {
            case FWK_EXTN_GLOBAL_SHMEM_MSG:
            {
               module_ptr->flags.need_global_shmem_extn = TRUE;
               break;
            }
            case FWK_EXTN_SYNC:
            {
               module_ptr->flags.need_sync_extn = TRUE;

               // SYNCING is achieved by enabling trigger policy.
               // Trigger policy is not updated by the module, it is directly updated by the sync-fwk-extn
               module_ptr->flags.need_trigger_policy_extn = TRUE;
               break;
            }
            case FWK_EXTN_ECNS:
            {
               // no special handling in GC
               break;
            }
            case FWK_EXTN_STM:
            {
               module_ptr->flags.need_stm_extn = TRUE;
               break;
            }
            case FWK_EXTN_MULTI_PORT_BUFFERING:
            {
               module_ptr->flags.need_mp_buf_extn = TRUE;
               break;
            }
            case FWK_EXTN_PCM:
            {
               module_ptr->flags.need_pcm_extn = TRUE;
               break;
            }
            case FWK_EXTN_TRIGGER_POLICY:
            {
               module_ptr->flags.need_trigger_policy_extn = TRUE;
               break;
            }
            case FWK_EXTN_DM:
            {
               module_ptr->flags.need_dm_extn = TRUE;
               break;
            }
            case FWK_EXTN_CONTAINER_PROC_DURATION:
            {
               module_ptr->flags.need_proc_dur_extn = TRUE;
               break;
            }
            case FWK_EXTN_CONTAINER_FRAME_DURATION:
            {
               module_ptr->flags.need_cntr_frame_dur_extn = TRUE;
               break;
            }
            case FWK_EXTN_THRESHOLD_CONFIGURATION:
            {
               module_ptr->flags.need_thresh_cfg_extn = TRUE;
               break;
            }
            case FWK_EXTN_BT_CODEC:
            {
               // no need to store.
               break;
            }
            case FWK_EXTN_SOFT_TIMER:
            {
               module_ptr->flags.need_soft_timer_extn = TRUE;
               break;
            }
            case FWK_EXTN_ISLAND:
            {
               module_ptr->flags.need_island_extn = TRUE;
               break;
            }
            case FWK_EXTN_ASYNC_SIGNAL_TRIGGER:
            {
               module_ptr->flags.need_async_st_extn = TRUE;
               break;
            }
            default:
            {
               extn_supported = FALSE;
               break;
            }
         }

         if (!extn_supported)
         {
            TOPO_MSG(log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: fmwk extensions not supported are 0x%lx. Module may not work correctly",
                     module_instance_id,
                     needed_fmwk_xtn_ids_arr[i].id);
            any_extn_not_supported = TRUE;
         }
         else
         {
            TOPO_MSG(log_id,
                     DBG_HIGH_PRIO,
                     "Module 0x%lX: fmwk extensions needed & supported 0x%lX",
                     module_instance_id,
                     needed_fmwk_xtn_ids_arr[i].id);
         }
      }

      if (any_extn_not_supported)
      {
         TOPO_MSG(log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: Failing since the fmwk doesnot support some extensions",
                  module_instance_id);
         THROW(result, AR_EFAILED);
      }
   }
   else
   {
      err_code = CAPI_EOK;
   }

   CATCH(result, TOPO_MSG_PREFIX, log_id)
   {
      result = capi_err_to_ar_result(err_code);
   }

   if (needed_fmwk_xtn_ids_arr)
   {
      posal_memory_free(needed_fmwk_xtn_ids_arr);
   }
   return result;
}

static ar_result_t gen_topo_capi_query_intf_extn_support(void *             amdb_handle,
                                                         gen_topo_module_t *module_ptr,
                                                         capi_proplist_t *  init_proplist_ptr)
{
   ar_result_t result = AR_EOK;

   // clang-format off
   #define INTF_EXTNS_ARRAY {                                                                      \
                              { INTF_EXTN_IMCL,                      FALSE, { NULL, 0, 0 } },      \
                              { INTF_EXTN_DATA_PORT_OPERATION,       FALSE, { NULL, 0, 0 } },      \
                              { INTF_EXTN_METADATA,                  FALSE, { NULL, 0, 0 } },      \
                              { INTF_EXTN_PATH_DELAY,                FALSE, { NULL, 0, 0 } },      \
                              { INTF_EXTN_PROP_PORT_DS_STATE,        FALSE, { NULL, 0, 0 } },      \
                              { INTF_EXTN_PROP_IS_RT_PORT_PROPERTY,  FALSE, { NULL, 0, 0 } },      \
                              { INTF_EXTN_DUTY_CYCLING_ISLAND_MODE,  FALSE, { NULL, 0, 0 } },      \
                              { INTF_EXTN_PERIOD,                    FALSE, { NULL, 0, 0 } },      \
                              { INTF_EXTN_STM_TS,                    FALSE, { NULL, 0, 0 } },      \
                            }

   #define LEN_OF_INTF_EXTNS_ARRAY SIZE_OF_ARRAY((capi_interface_extn_desc_t[]) INTF_EXTNS_ARRAY)

   typedef struct gen_topo_module_intf_extn_list_t
   {
      capi_interface_extns_list_t intf_extn_num;
      capi_interface_extn_desc_t  intf_extn_desc[LEN_OF_INTF_EXTNS_ARRAY];
   } gen_topo_module_intf_extn_list_t;

   gen_topo_module_intf_extn_list_t intf_ext_list = {
         .intf_extn_num  = {LEN_OF_INTF_EXTNS_ARRAY},
         .intf_extn_desc = INTF_EXTNS_ARRAY };
   // clang-format on

   capi_err_t      err_code           = CAPI_EOK;
   uint32_t        module_instance_id = module_ptr->gu.module_instance_id;
   uint32_t        log_id             = module_ptr->topo_ptr->gu.log_id;
   capi_proplist_t props_list;
   capi_prop_t     prop[1]; // query only one property

   prop[0].id                      = CAPI_INTERFACE_EXTENSIONS;
   prop[0].payload.actual_data_len = sizeof(gen_topo_module_intf_extn_list_t);
   prop[0].payload.max_data_len    = sizeof(gen_topo_module_intf_extn_list_t);

   prop[0].payload.data_ptr        = (int8_t *)(&intf_ext_list);
   prop[0].port_info.is_valid      = FALSE;
   prop[0].port_info.is_input_port = FALSE;
   prop[0].port_info.port_index    = 0;

   props_list.prop_ptr  = prop;
   props_list.props_num = 1;

   err_code = amdb_capi_get_static_properties_f((void *)amdb_handle, init_proplist_ptr, &props_list);

   if (CAPI_SUCCEEDED(err_code))
   {
      /*  Querying for interface extension*/
      for (uint32_t j = 0; j < intf_ext_list.intf_extn_num.num_extensions; j++)
      {
         if (intf_ext_list.intf_extn_desc[j].is_supported)
         {
            TOPO_MSG(log_id,
                     DBG_HIGH_PRIO,
                     "Module 0x%lX: intf extension supported 0x%lX",
                     module_instance_id,
                     intf_ext_list.intf_extn_desc[j].id);

            // switch case used to make it easier to add future interface extensions.
            switch (intf_ext_list.intf_extn_desc[j].id)
            {
               case INTF_EXTN_IMCL:
               {
                  // need not be stored
                  // module_ptr->flags.supports_imcl = TRUE;
                  break;
               }
               case INTF_EXTN_METADATA:
               {
                  module_ptr->flags.supports_metadata = TRUE;
                  break;
               }
               case INTF_EXTN_DATA_PORT_OPERATION:
               {
                  module_ptr->flags.supports_data_port_ops = TRUE;
                  break;
               }
               case INTF_EXTN_PROP_PORT_DS_STATE:
               {
                  module_ptr->flags.supports_prop_port_ds_state = TRUE;
                  break;
               }
               case INTF_EXTN_PROP_IS_RT_PORT_PROPERTY:
               {
                  module_ptr->flags.supports_prop_is_rt_port_prop = TRUE;
                  break;
               }
               case INTF_EXTN_PATH_DELAY:
               {
                  // no need of flag for this as it's req/resp based.
                  break;
               }
               case INTF_EXTN_DUTY_CYCLING_ISLAND_MODE:
               {
                  module_ptr->flags.supports_module_allow_duty_cycling = TRUE;
                  break;
               }
               case INTF_EXTN_PERIOD:
               {
                  module_ptr->flags.supports_period = TRUE;
                  break;
               }
               case INTF_EXTN_STM_TS:
               {
                  module_ptr->flags.supports_stm_ts = TRUE;
                  break;
               }
               default:
               {
                  // Something can't be supported and not be handled. Shouldn't get here.
                  TOPO_MSG(log_id,
                           DBG_ERROR_PRIO,
                           "Warning: FWK Querying interface Extension 0x%lX.  Module 0x%lx says it supports it. FWK "
                           "may not be handling it.",
                           intf_ext_list.intf_extn_desc[j].id,
                           module_instance_id);
                  break;
               }
            }
         }
      }
   }

   return result;
}

/**
 * is_input_module - module which is connected to input port of the container
 */
ar_result_t gen_topo_capi_create_from_amdb(gen_topo_module_t *    module_ptr,
                                           gen_topo_t *           topo_ptr,
                                           void *                 amdb_handle,
                                           POSAL_HEAP_ID          mem_heap_id,
                                           gen_topo_graph_init_t *graph_init_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t module_instance_id = module_ptr->gu.module_instance_id;
   capi_t * capi_ptr           = NULL;
   uint32_t log_id             = topo_ptr->gu.log_id;
   uint32_t stack_size         = 0;
   bool_t   inplace = FALSE, requires_data_buf = FALSE;

   capi_event_callback_info_t cb_obj = { .event_cb = graph_init_ptr->capi_cb, .event_context = (void *)module_ptr };

   if (module_ptr->capi_ptr || (NULL == amdb_handle))
   {
      // if CAPI already exists return; maybe new port was added in this graph-open.
      // if AMDB handle is null, then module may be fmwk module
      return AR_EOK;
   }

   /* port info */
   capi_err_t                     err_code = CAPI_EOK;
   capi_proplist_t                init_proplist;
   capi_prop_t                    init_props[NUM_INIT_PARAMS];
   capi_prop_t                    static_prop[1];
   capi_proplist_t                static_properties;
   capi_port_num_info_t           num_max_ports_info = { .num_input_ports  = module_ptr->gu.max_input_ports,
                                               .num_output_ports = module_ptr->gu.max_output_ports };
   capi_init_memory_requirement_t init_mem           = { 0 };

   /**
    * container debug msg logging uses upper 16 bits (lower 16 are zeros).
    * modules need its instance num (within container) as well as EoS/flush bits
    */
   uint32_t temp_log_id = log_id;
   gen_topo_get_mod_heap_id_and_log_id(&temp_log_id, &mem_heap_id, module_ptr->serial_num, mem_heap_id);
   module_ptr->gu.module_heap_id = mem_heap_id;
   TOPO_MSG(log_id,
            DBG_LOW_PRIO,
            "Module 0x%lX: mem_heap_id: 0x%lX",
            module_ptr->gu.module_instance_id,
            module_ptr->gu.module_heap_id);
   capi_heap_id_t heap_id = { (uint32_t)mem_heap_id };

   capi_module_instance_id_t miid         = { .module_id          = module_ptr->gu.module_id,
                                      .module_instance_id = module_ptr->gu.module_instance_id };
   capi_logging_info_t       logging_info = { .log_id = temp_log_id, .log_id_mask = LOG_ID_LOG_DISCONTINUITY_MASK };

   TRY(result,
       gen_topo_prepare_init_proplist_util_(&init_proplist,
                                            init_props,
                                            &cb_obj,
                                            &heap_id,
                                            &num_max_ports_info,
                                            &miid,
                                            &logging_info));

   // static props: query one by one. for each static prop, pass same init_proplist
   //               once all static props are queried, call init.

   uint32_t i                             = 0;
   static_prop[i].id                      = CAPI_INIT_MEMORY_REQUIREMENT;
   static_prop[i].payload.actual_data_len = 0;
   static_prop[i].payload.max_data_len    = sizeof(capi_init_memory_requirement_t);
   static_prop[i].payload.data_ptr        = (int8_t *)&init_mem;
   static_prop[i].port_info.is_valid      = FALSE;
   i++;

   static_properties.props_num = i;
   static_properties.prop_ptr  = static_prop;

   TRY(err_code, amdb_capi_get_static_properties_f((void *)amdb_handle, &init_proplist, &static_properties));
   // as per CAPI V2, init_proplist_ptr passed during init must also be passed during get_static_prop

   TOPO_MSG(log_id,
            DBG_LOW_PRIO,
            "Module 0x%lX: Get Static Properties done with init_mem_req=%lu, num init props %lu, mem_heap_id 0x%lX",
            module_instance_id,
            init_mem.size_in_bytes,
            init_proplist.props_num,
            mem_heap_id);

   MALLOC_MEMSET(capi_ptr, capi_t, init_mem.size_in_bytes, mem_heap_id, result);

   // get stack size and find max.
   TRY(result, gen_topo_capi_get_stack_size(amdb_handle, topo_ptr->gu.log_id, &stack_size, &init_proplist));

   graph_init_ptr->max_stack_size = MAX(graph_init_ptr->max_stack_size, stack_size);

   gen_topo_capi_is_inplace_n_requires_data_buf(amdb_handle,
                                                module_ptr->gu.module_instance_id,
                                                topo_ptr->gu.log_id,
                                                &init_proplist,
                                                &inplace,
                                                &requires_data_buf);

   // MIMO modules can't be inplace.
   VERIFY(result, !(inplace && ((module_ptr->gu.num_input_ports > 1) || (module_ptr->gu.num_output_ports > 1))));
   module_ptr->flags.inplace           = inplace;
   module_ptr->flags.requires_data_buf = requires_data_buf;

   // get required framework extensions
   TRY(result,
       topo_ptr->gen_topo_vtable_ptr->capi_get_required_fmwk_extensions((void *)topo_ptr,
                                                                        (void *)module_ptr,
                                                                        amdb_handle,
                                                                        &init_proplist));

   // Check if the module supports any of the available interface extensions
   gen_topo_capi_query_intf_extn_support(amdb_handle, module_ptr, &init_proplist);

   TRY(err_code, amdb_capi_init_f((void *)amdb_handle, capi_ptr, &init_proplist));

   CATCH(err_code, TOPO_MSG_PREFIX, log_id)
   {
      result = capi_err_to_ar_result(err_code);
   }

   CATCH(result, TOPO_MSG_PREFIX, log_id)
   {
      MFREE_NULLIFY(capi_ptr);
   }

   module_ptr->capi_ptr = capi_ptr;

   return result;
}

ar_result_t gen_topo_capi_get_port_thresh(uint32_t  module_instance_id,
                                          uint32_t  log_id,
                                          capi_t *  capi_ptr,
                                          bool_t    is_input,
                                          uint16_t  port_index,
                                          uint32_t *thresh)
{
   capi_err_t err_code = CAPI_EOK;
   INIT_EXCEPTION_HANDLING

   *thresh = 1;

   capi_proplist_t props_list;
   capi_prop_t     prop[1]; // query only one property at once.

   capi_port_data_threshold_t port_thresh;

   memset(&port_thresh, 0, sizeof(capi_port_data_threshold_t));

   // defaults:
   port_thresh.threshold_in_bytes = DEFAULT_PORT_THRESHOLD;

   uint32_t i = 0;

   VERIFY(err_code, NULL != capi_ptr);

   /**
    * requires data buffering = TRUE:
    *    threshold = 1 => resampler, rate matching, MFC.
    *    threshold > 1 => decoder, depack, pack, conv, (and encoder)
    * requires data buffering = FALSE:
    *    threshold = 1 => sample based. most PP modules.
    *    threshold > 1 => fixed frame size modules such as EC. Also possibly encoder (and dec if encoded frame sizes are
    * known upfront).
    */
   i                               = 0;
   prop[i].id                      = CAPI_PORT_DATA_THRESHOLD;
   prop[i].payload.actual_data_len = 0;
   prop[i].payload.max_data_len    = sizeof(capi_port_data_threshold_t);
   prop[i].port_info.is_valid      = TRUE;
   prop[i].port_info.is_input_port = is_input;
   prop[i].port_info.port_index    = port_index;
   prop[i].payload.data_ptr        = (int8_t *)&port_thresh;
   i++;

   props_list.prop_ptr  = prop;
   props_list.props_num = i;

   err_code = capi_ptr->vtbl_ptr->get_properties(capi_ptr, &props_list);
   if (CAPI_FAILED(err_code))
   {
      if (CAPI_EUNSUPPORTED != err_code)
      {
         TOPO_MSG(log_id,
                  DBG_HIGH_PRIO,
                  "Module 0x%lX: Warning: Get port-thresh property returned fail. Assuming no threshold needed",
                  module_instance_id);
      }
      // most PP modules don't require threshold (thresh = 1)
      port_thresh.threshold_in_bytes = DEFAULT_PORT_THRESHOLD;
      err_code                       = CAPI_EOK;
   }

   TOPO_MSG(log_id,
            DBG_HIGH_PRIO,
            "Module 0x%lX: port-threshold for is_input %u, port_index %u, is %lu bytes",
            module_instance_id,
            is_input,
            port_index,
            port_thresh.threshold_in_bytes);

   CATCH(err_code, TOPO_MSG_PREFIX, log_id){}

      *thresh = port_thresh.threshold_in_bytes;

   return AR_EOK;
}

static ar_result_t gen_topo_set_media_fmt_v1(gen_topo_t *       topo_ptr,
                                             gen_topo_module_t *module_ptr,
                                             topo_media_fmt_t * media_fmt_ptr,
                                             capi_property_id_t prop_id,
                                             uint16_t           port_index)
{
   INIT_EXCEPTION_HANDLING

   capi_err_t      err_code = CAPI_EOK;
   uint32_t        log_id   = topo_ptr->gu.log_id;
   capi_t *        capi_ptr = module_ptr->capi_ptr;
   uint32_t        size     = 0;
   capi_proplist_t props_list;
   capi_prop_t     props[1];

   gen_topo_capi_media_fmt_t  capi_media_fmt;
   gen_topo_capi_media_fmt_t *capi_media_fmt_ptr = &capi_media_fmt;

   VERIFY(err_code, NULL != capi_ptr);

   capi_media_fmt_ptr->main.format_header.data_format =
      gen_topo_convert_spf_data_format_to_capi_data_format(media_fmt_ptr->data_format);

   if (SPF_IS_PACKETIZED_OR_PCM(media_fmt_ptr->data_format))
   {
      size = sizeof(capi_media_fmt);
      init_media_fmt(&capi_media_fmt_ptr->std);

      capi_media_fmt_ptr->std.bits_per_sample = media_fmt_ptr->pcm.bits_per_sample;

      capi_media_fmt_ptr->std.bitstream_format = media_fmt_ptr->fmt_id;

      capi_media_fmt_ptr->std.data_interleaving =
         gen_topo_convert_gen_topo_interleaving_to_capi_interleaving(media_fmt_ptr->pcm.interleaving);

      capi_media_fmt_ptr->std.data_is_signed = TRUE;

      capi_media_fmt_ptr->std.num_channels = media_fmt_ptr->pcm.num_channels;
      for (uint32_t i = 0; (i < media_fmt_ptr->pcm.num_channels) && (i < CAPI_MAX_CHANNELS); i++)
      {
         capi_media_fmt_ptr->std.channel_type[i] = media_fmt_ptr->pcm.chan_map[i];
      }

      capi_media_fmt_ptr->std.q_factor = media_fmt_ptr->pcm.q_factor;

      capi_media_fmt_ptr->std.sampling_rate = media_fmt_ptr->pcm.sample_rate;
   }
   else if (SPF_DEINTERLEAVED_RAW_COMPRESSED == media_fmt_ptr->data_format)
   {
      memset(&capi_media_fmt_ptr->deint_raw, 0, sizeof(capi_media_fmt_ptr->deint_raw));
      capi_media_fmt_ptr->deint_raw.deint_raw.bitstream_format = media_fmt_ptr->fmt_id;
      capi_media_fmt_ptr->deint_raw.deint_raw.bufs_num         = media_fmt_ptr->deint_raw.bufs_num;
      capi_media_fmt_ptr->deint_raw.deint_raw.minor_version    = 1;
      for (uint32_t i = 0; i < media_fmt_ptr->deint_raw.bufs_num; i++)
      {
         capi_media_fmt_ptr->deint_raw.ch_mask[i].channel_mask_lsw =
            media_fmt_ptr->deint_raw.ch_mask[i].channel_mask_lsw;
         capi_media_fmt_ptr->deint_raw.ch_mask[i].channel_mask_msw =
            media_fmt_ptr->deint_raw.ch_mask[i].channel_mask_msw;
      }
      size = sizeof(capi_set_get_media_format_t) + sizeof(capi_deinterleaved_raw_compressed_data_format_t) +
             media_fmt_ptr->deint_raw.bufs_num * sizeof(capi_channel_mask_t);
   }
   else
   {
      if (media_fmt_ptr->raw.buf_ptr)
      {
         capi_media_fmt_ptr = (gen_topo_capi_media_fmt_t *)media_fmt_ptr->raw.buf_ptr;
         size               = media_fmt_ptr->raw.buf_size;
      }
      else
      {
         capi_media_fmt_ptr->raw_fmt.bitstream_format = media_fmt_ptr->fmt_id;
         size = sizeof(capi_set_get_media_format_t) + sizeof(capi_raw_compressed_data_format_t);
      }
   }

   props[0].id                      = prop_id;
   props[0].payload.actual_data_len = size;
   props[0].payload.max_data_len    = props[0].payload.actual_data_len;
   props[0].payload.data_ptr        = (int8_t *)(capi_media_fmt_ptr);
   props[0].port_info.is_valid      = TRUE;

   if (prop_id == CAPI_INPUT_MEDIA_FORMAT)
   {
      props[0].port_info.is_input_port = TRUE;
   }
   else // Out MF
   {
      props[0].port_info.is_input_port = FALSE;
   }

   props[0].port_info.port_index = port_index;

   props_list.props_num = 1;
   props_list.prop_ptr  = props;

   err_code = capi_ptr->vtbl_ptr->set_properties(capi_ptr, &props_list);
   if (CAPI_FAILED(err_code))
   {
      TOPO_MSG(log_id, DBG_ERROR_PRIO, "setting media fmt 0x%lx failed", prop_id);
   }

   CATCH(err_code, TOPO_MSG_PREFIX, log_id)
   {
   }
   return capi_err_to_ar_result(err_code);
}

static ar_result_t gen_topo_set_media_fmt_v2(gen_topo_t *       topo_ptr,
                                             gen_topo_module_t *module_ptr,
                                             topo_media_fmt_t * media_fmt_ptr,
                                             capi_property_id_t prop_id,
                                             uint16_t           port_index)
{
   INIT_EXCEPTION_HANDLING

   capi_err_t      err_code = CAPI_EOK;
   uint32_t        log_id   = topo_ptr->gu.log_id;
   capi_t *        capi_ptr = module_ptr->capi_ptr;
   uint32_t        size     = 0;
   capi_proplist_t props_list;
   capi_prop_t     props[1];

   gen_topo_capi_media_fmt_v2_t  capi_media_fmt;
   gen_topo_capi_media_fmt_v2_t *capi_media_fmt_ptr = &capi_media_fmt;

   VERIFY(err_code, NULL != capi_ptr);

   capi_media_fmt_ptr->main.format_header.data_format =
      gen_topo_convert_spf_data_format_to_capi_data_format(media_fmt_ptr->data_format);

   if (SPF_IS_PACKETIZED_OR_PCM(media_fmt_ptr->data_format))
   {
      size = sizeof(capi_media_fmt);
      init_media_fmt_v2(&capi_media_fmt_ptr->std.fmt);

      capi_media_fmt_ptr->std.fmt.bits_per_sample = media_fmt_ptr->pcm.bits_per_sample;

      capi_media_fmt_ptr->std.fmt.bitstream_format = media_fmt_ptr->fmt_id;

      capi_media_fmt_ptr->std.fmt.data_interleaving =
         gen_topo_convert_gen_topo_interleaving_to_capi_interleaving(media_fmt_ptr->pcm.interleaving);

      capi_media_fmt_ptr->std.fmt.data_is_signed = TRUE;

      capi_media_fmt_ptr->std.fmt.num_channels = media_fmt_ptr->pcm.num_channels;
      for (uint32_t i = 0; (i < media_fmt_ptr->pcm.num_channels) && (i < CAPI_MAX_CHANNELS_V2); i++)
      {
         capi_media_fmt_ptr->std.channel_type[i] = media_fmt_ptr->pcm.chan_map[i];
      }

      capi_media_fmt_ptr->std.fmt.q_factor = media_fmt_ptr->pcm.q_factor;

      capi_media_fmt_ptr->std.fmt.sampling_rate = media_fmt_ptr->pcm.sample_rate;
   }
   else if (SPF_DEINTERLEAVED_RAW_COMPRESSED == media_fmt_ptr->data_format)
   {
      memset(&capi_media_fmt_ptr->deint_raw, 0, sizeof(capi_media_fmt_ptr->deint_raw));
      capi_media_fmt_ptr->deint_raw.deint_raw.bitstream_format = media_fmt_ptr->fmt_id;
      capi_media_fmt_ptr->deint_raw.deint_raw.bufs_num         = media_fmt_ptr->deint_raw.bufs_num;
      capi_media_fmt_ptr->deint_raw.deint_raw.minor_version    = 1;
      for (uint32_t i = 0; i < media_fmt_ptr->deint_raw.bufs_num; i++)
      {
         capi_media_fmt_ptr->deint_raw.ch_mask[i].channel_mask_lsw =
            media_fmt_ptr->deint_raw.ch_mask[i].channel_mask_lsw;
         capi_media_fmt_ptr->deint_raw.ch_mask[i].channel_mask_msw =
            media_fmt_ptr->deint_raw.ch_mask[i].channel_mask_msw;
      }
      size = sizeof(capi_set_get_media_format_t) + sizeof(capi_deinterleaved_raw_compressed_data_format_t) +
             media_fmt_ptr->deint_raw.bufs_num * sizeof(capi_channel_mask_t);
   }
   else
   {
      if (media_fmt_ptr->raw.buf_ptr)
      {
         capi_media_fmt_ptr = (gen_topo_capi_media_fmt_v2_t *)media_fmt_ptr->raw.buf_ptr;
         size               = media_fmt_ptr->raw.buf_size;
      }
      else
      {
         capi_media_fmt_ptr->raw_fmt.bitstream_format = media_fmt_ptr->fmt_id;
         size = sizeof(capi_set_get_media_format_t) + sizeof(capi_raw_compressed_data_format_t);
      }
   }

   props[0].id                      = prop_id;
   props[0].payload.actual_data_len = size;
   props[0].payload.max_data_len    = props[0].payload.actual_data_len;
   props[0].payload.data_ptr        = (int8_t *)(capi_media_fmt_ptr);
   props[0].port_info.is_valid      = TRUE;

   if (prop_id == CAPI_INPUT_MEDIA_FORMAT_V2)
   {
      props[0].port_info.is_input_port = TRUE;
   }
   else // Out MF V2
   {
      props[0].port_info.is_input_port = FALSE;
   }

   props[0].port_info.port_index = port_index;

   props_list.props_num = 1;
   props_list.prop_ptr  = props;

   err_code = capi_ptr->vtbl_ptr->set_properties(capi_ptr, &props_list);
   if (CAPI_FAILED(err_code))
   {
      TOPO_MSG(log_id, DBG_ERROR_PRIO, "Warning: setting media fmt 0x%lx failed", prop_id);
   }

   CATCH(err_code, TOPO_MSG_PREFIX, log_id)
   {
   }
   return capi_err_to_ar_result(err_code);
}

/** value must be CAPI_DATA_FORMAT_INVALID_VAL when not available*/
ar_result_t gen_topo_capi_set_media_fmt(gen_topo_t *       topo_ptr,
                                        gen_topo_module_t *module_ptr,
                                        topo_media_fmt_t * media_fmt_ptr,
                                        bool_t             is_input_mf,
                                        uint16_t           port_index)
{
   if ((NULL == topo_ptr) || (NULL == module_ptr) || (NULL == media_fmt_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "gen_topo_capi_set_media_fmt: Received NULL pointer(s)");
      return AR_EBADPARAM;
   }
   capi_err_t result = AR_EOK;

   if (SPF_IS_PACKETIZED_OR_PCM(media_fmt_ptr->data_format))
   {
      if (module_ptr->flags.need_pcm_extn)
      {
         gen_topo_capi_set_fwk_extn_media_fmt(topo_ptr, module_ptr, is_input_mf, port_index, media_fmt_ptr);
      }
   }

   if (is_input_mf)
   {
      // first we try setting V2, if it fails, we try V1
      if (AR_EUNSUPPORTED ==
          (result =
              gen_topo_set_media_fmt_v2(topo_ptr, module_ptr, media_fmt_ptr, CAPI_INPUT_MEDIA_FORMAT_V2, port_index)))
      {
         TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "V2 set media format not supported by module, trying V1");
         if (AR_EOK !=
             (result =
                 gen_topo_set_media_fmt_v1(topo_ptr, module_ptr, media_fmt_ptr, CAPI_INPUT_MEDIA_FORMAT, port_index)))
         {
            TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "Set input media format failed, result = %lu", result);
            return AR_EFAILED;
         }
      }
   }
   else // output set prop
   {
      // first we try setting V2, if it fails, we try V1
      if (AR_EUNSUPPORTED ==
          (result =
              gen_topo_set_media_fmt_v2(topo_ptr, module_ptr, media_fmt_ptr, CAPI_OUTPUT_MEDIA_FORMAT_V2, port_index)))
      {
         TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "V2 set media format not supported by module, trying V1");
         if (AR_EOK !=
             (result =
                 gen_topo_set_media_fmt_v1(topo_ptr, module_ptr, media_fmt_ptr, CAPI_OUTPUT_MEDIA_FORMAT, port_index)))
         {
            TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "Set output media format failed, result = %lu", result);
            return AR_EFAILED;
         }
      }
   }
   return result;
}

static data_format_t gen_topo_get_out_data_format_by_module_type(uint16_t module_type)
{
   if (module_type > AMDB_MODULE_TYPE_DEPACKETIZER)
   {
      return CAPI_MAX_FORMAT_TYPE;
   }
   data_format_t df[] = {
      CAPI_MAX_FORMAT_TYPE, // 0
      CAPI_MAX_FORMAT_TYPE, // 1
      CAPI_FIXED_POINT,     // AMDB_MODULE_TYPE_GENERIC                   2
      CAPI_FIXED_POINT,     // AMDB_MODULE_TYPE_DECODER                   3
      CAPI_RAW_COMPRESSED,  // AMDB_MODULE_TYPE_ENCODER                   4
      CAPI_RAW_COMPRESSED,  // AMDB_MODULE_TYPE_CONVERTER                 5
      CAPI_MAX_FORMAT_TYPE, // AMDB_MODULE_TYPE_PACKETIZER                6
      CAPI_MAX_FORMAT_TYPE, // AMDB_MODULE_TYPE_DEPACKETIZER              7
      CAPI_FIXED_POINT,     // AMDB_MODULE_TYPE_DETECTOR 8
      CAPI_FIXED_POINT,     // AMDB_MODULE_TYPE_GENERATOR 9
      CAPI_FIXED_POINT,     // AMDB_MODULE_TYPE_PP 10
      CAPI_FIXED_POINT,     // AMDB_MODULE_TYPE_END_POINT 11

   };
   return df[module_type];
}

static void gen_topo_assign_default_out_medifa_fmt(gen_topo_module_t *           module_ptr,
                                                   gen_topo_capi_media_fmt_v2_t *media_fmt_ptr)
{
   switch (module_ptr->gu.module_type)
   {
      case AMDB_MODULE_TYPE_GENERIC:
      case AMDB_MODULE_TYPE_PP:
      case AMDB_MODULE_TYPE_GENERATOR:
      case AMDB_MODULE_TYPE_DETECTOR:
      case AMDB_MODULE_TYPE_END_POINT:
      {
         if (module_ptr->gu.flags.is_siso)
         {
            gen_topo_input_port_t *in_port_ptr =
               (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;
            topo_media_fmt_t *topo_media_fmt_ptr = in_port_ptr->common.media_fmt_ptr;

            media_fmt_ptr->main.format_header.data_format =
               gen_topo_convert_spf_data_format_to_capi_data_format(topo_media_fmt_ptr->data_format);
            media_fmt_ptr->std.fmt.sampling_rate    = topo_media_fmt_ptr->pcm.sample_rate;
            media_fmt_ptr->std.fmt.bitstream_format = topo_media_fmt_ptr->fmt_id;
            media_fmt_ptr->std.fmt.bits_per_sample  = topo_media_fmt_ptr->pcm.bits_per_sample;
            media_fmt_ptr->std.fmt.q_factor         = topo_media_fmt_ptr->pcm.q_factor;
            media_fmt_ptr->std.fmt.num_channels     = topo_media_fmt_ptr->pcm.num_channels;
            media_fmt_ptr->std.fmt.data_interleaving =
               gen_topo_convert_gen_topo_interleaving_to_capi_interleaving(topo_media_fmt_ptr->pcm.interleaving);
            media_fmt_ptr->std.fmt.data_is_signed = TRUE;
            media_fmt_ptr->std.fmt.minor_version  = 0;
            memscpy(media_fmt_ptr->std.fmt.channel_type,
                    sizeof(capi_channel_type_t) * media_fmt_ptr->std.fmt.num_channels,
                    topo_media_fmt_ptr->pcm.chan_map,
                    sizeof(topo_media_fmt_ptr->pcm.chan_map));
            TOPO_MSG(module_ptr->topo_ptr->gu.log_id, DBG_LOW_PRIO, "Assuming output media format same as input");
         }
         break;
      }
      default:
      {
         media_fmt_ptr->main.format_header.data_format =
            gen_topo_get_out_data_format_by_module_type(module_ptr->gu.module_type);
         TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "Assuming default data format %d",
                  media_fmt_ptr->main.format_header.data_format);
      }
   }
}

ar_result_t gen_topo_capi_get_out_media_fmt(gen_topo_t *            topo_ptr,
                                            gen_topo_module_t *     module_ptr,
                                            gen_topo_output_port_t *out_port_ptr)
{
   if ((NULL == topo_ptr) || (NULL == module_ptr) || (NULL == out_port_ptr) || (module_ptr->capi_ptr == NULL))
   {
      return AR_EBADPARAM;
   }

   ar_result_t     result        = AR_EOK;
   capi_err_t      err_code      = CAPI_EOK;
   bool_t          is_std_fmt_v2 = TRUE;
   capi_proplist_t props_list;
   capi_prop_t     prop[1];
   uint32_t        total_size_v2 = 0;
   uint32_t        total_size_v1 = 0;
   /** Note: for raw fmt this acts as a buffer. */
   union
   {
      gen_topo_capi_media_fmt_v2_t v2;
      gen_topo_capi_media_fmt_t    v1;
   } capi_media_fmt;

   memset(&capi_media_fmt, 0, sizeof(capi_media_fmt));

   capi_event_info_t event_info;

   // query size, N
   capi_output_media_format_size_t out_media_fmt_size_struct = { 0 };

   // even if CAPI returns success while actually not implementing get prop, we will assume v2 for std media fmt
   out_media_fmt_size_struct.size_in_bytes = sizeof(gen_topo_capi_media_fmt_v2_t) - sizeof(capi_data_format_header_t);

   prop[0].id                      = CAPI_OUTPUT_MEDIA_FORMAT_SIZE;
   prop[0].payload.actual_data_len = 0;
   prop[0].payload.max_data_len    = sizeof(capi_output_media_format_size_t);
   prop[0].port_info.is_input_port = FALSE;
   prop[0].port_info.is_valid      = TRUE;
   prop[0].port_info.port_index    = out_port_ptr->gu.cmn.index;
   prop[0].payload.data_ptr        = (int8_t *)&out_media_fmt_size_struct;

   props_list.prop_ptr  = prop;
   props_list.props_num = 1;

   err_code = module_ptr->capi_ptr->vtbl_ptr->get_properties(module_ptr->capi_ptr, &props_list);

   if (CAPI_FAILED(err_code))
   {
      // assume pcm/packetized if CAPI_OUTPUT_MEDIA_FORMAT_SIZE is not implemented.
      total_size_v2 = sizeof(gen_topo_capi_media_fmt_v2_t);
      total_size_v1 = sizeof(gen_topo_capi_media_fmt_t);
   }
   else
   {
      total_size_v2 = out_media_fmt_size_struct.size_in_bytes + sizeof(capi_data_format_header_t);
      total_size_v1 = out_media_fmt_size_struct.size_in_bytes + sizeof(capi_data_format_header_t);
   }

   // if N is smaller than out_port_ptr->cmn.media_fmt, then give the pointer &out_port_ptr->cmn.media_fmt and N as the
   // size
   if (total_size_v2 <= sizeof(gen_topo_capi_media_fmt_v2_t))
   {
// query for v2
#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "Querying for CAPI_OUTPUT_MEDIA_FORMAT_V2 of module 0x%lX",
               module_ptr->gu.module_instance_id);
#endif

      prop[0].id                      = CAPI_OUTPUT_MEDIA_FORMAT_V2;
      prop[0].payload.actual_data_len = 0;
      prop[0].payload.max_data_len    = sizeof(gen_topo_capi_media_fmt_v2_t);
      prop[0].payload.data_ptr        = (int8_t *)&capi_media_fmt.v2;

      err_code = module_ptr->capi_ptr->vtbl_ptr->get_properties(module_ptr->capi_ptr, &props_list);
   }
   else
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lx, Port index %lu, Insufficient memory for output media format query"
               "size: %lu is greater than media fmt size v2: %lu",
               module_ptr->gu.module_instance_id,
               out_port_ptr->gu.cmn.index,
               total_size_v2,
               sizeof(gen_topo_capi_media_fmt_v2_t));
   }
   // if it fails, query for v1
   if (CAPI_FAILED(err_code))
   {
      if (total_size_v1 <= sizeof(gen_topo_capi_media_fmt_t))
      {
         is_std_fmt_v2 = FALSE;
#ifdef VERBOSE_DEBUGGING
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  "V2 Failed - get property(): Querying for CAPI_OUTPUT_MEDIA_FORMAT of module 0x%lX",
                  module_ptr->gu.module_instance_id);
#endif
         prop[0].id                      = CAPI_OUTPUT_MEDIA_FORMAT;
         prop[0].payload.actual_data_len = 0;
         prop[0].payload.max_data_len    = sizeof(gen_topo_capi_media_fmt_t);
         prop[0].payload.data_ptr        = (int8_t *)&capi_media_fmt.v1;

         err_code = module_ptr->capi_ptr->vtbl_ptr->get_properties(module_ptr->capi_ptr, &props_list);

         if (CAPI_FAILED(err_code))
         {
            gen_topo_assign_default_out_medifa_fmt(module_ptr, &capi_media_fmt.v2);
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "V1 Failed - get property(): CAPI_OUTPUT_MEDIA_FORMAT. Assumed defaults");
         }
      }
      else
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lx, Port index %lu, Insufficient memory for output media format query"
                  " size: %lu is greater than media fmt size v1: %lu",
                  module_ptr->gu.module_instance_id,
                  out_port_ptr->gu.cmn.index,
                  total_size_v1,
                  sizeof(gen_topo_capi_media_fmt_t));
         return AR_EFAILED;
      }
   }

   event_info.payload   = prop[0].payload;
   event_info.port_info = prop[0].port_info;

   result = capi_err_to_ar_result(
      gen_topo_handle_output_media_format_event(topo_ptr, module_ptr, &event_info, is_std_fmt_v2, FALSE));

   return result;
}

ar_result_t gen_topo_capi_algorithmic_reset(uint32_t log_id,
                                            capi_t * capi_ptr,
                                            bool_t   is_port_valid,
                                            bool_t   is_input,
                                            uint16_t port_index)
{
   capi_err_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   capi_proplist_t props_list;
   capi_prop_t     props[1];

   VERIFY(result, NULL != capi_ptr);

   props[0].id                      = CAPI_ALGORITHMIC_RESET;
   props[0].payload.actual_data_len = 0;
   props[0].payload.max_data_len    = props[0].payload.actual_data_len;
   props[0].payload.data_ptr        = NULL;
   props[0].port_info.is_valid      = is_port_valid;
   props[0].port_info.is_input_port = is_input;
   props[0].port_info.port_index    = port_index;

   props_list.props_num = 1;
   props_list.prop_ptr  = props;

   result = capi_ptr->vtbl_ptr->set_properties(capi_ptr, &props_list);
   if (CAPI_FAILED(result) && (result != CAPI_EUNSUPPORTED))
   {
      TOPO_MSG(log_id, DBG_ERROR_PRIO, "capi algorithmic reset failed");
      return result;
   }
   result = CAPI_EOK;

   CATCH(result, TOPO_MSG_PREFIX, log_id)
   {
   }

   return capi_err_to_ar_result(result);
}

ar_result_t gen_topo_capi_get_param(uint32_t  log_id,
                                    capi_t *  capi_ptr,
                                    uint32_t  param_id,
                                    int8_t *  payload,
                                    uint32_t *size_ptr)
{
   capi_err_t result = CAPI_EOK;
   INIT_EXCEPTION_HANDLING

   capi_buf_t buf;
   buf.actual_data_len = *size_ptr;
   buf.data_ptr        = (int8_t *)payload;
   buf.max_data_len    = *size_ptr;

   capi_port_info_t port_info;

   VERIFY(result, NULL != capi_ptr);

   port_info.is_valid = FALSE;

   // Assume the Set param value would be a 32-bit integer, as is specified in CAPI document.
   result = capi_ptr->vtbl_ptr->get_param(capi_ptr, param_id, &port_info, &buf);

   if (CAPI_FAILED(result))
   {
      TOPO_MSG(log_id, DBG_ERROR_PRIO, "get param for (param id 0x%lx) result %d", param_id, result);
   }
   else
   {
      // TOPO_MSG(log_id, DBG_LOW_PRIO, "get param for (param id 0x%lx) success", param_id);
   }

   if ((buf.actual_data_len <= buf.max_data_len) || (CAPI_ENEEDMORE == result))
   {
      *size_ptr = buf.actual_data_len;
   }
   else
   {
      *size_ptr = 0;
   }
   CATCH(result, TOPO_MSG_PREFIX, log_id)
   {
   }

   return capi_err_to_ar_result(result);
}

ar_result_t gen_topo_capi_set_param(uint32_t log_id,
                                    capi_t * capi_ptr,
                                    uint32_t param_id,
                                    int8_t * payload,
                                    uint32_t size)
{
   capi_err_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   capi_buf_t buf;
   buf.actual_data_len = size;
   buf.data_ptr        = payload;
   buf.max_data_len    = size;

   capi_port_info_t port_info;
   port_info.is_valid   = FALSE;
   port_info.port_index = 0;

   VERIFY(result, NULL != capi_ptr);

   if (capi_ptr == NULL)
   {
      return result = AR_EUNSUPPORTED;
   }
   else
   {
      // Assume the Set param value would be a 32-bit integer, as is specified in CAPI document.
      result = capi_ptr->vtbl_ptr->set_param(capi_ptr, param_id, &port_info, &buf);
   }
   if (CAPI_FAILED(result))
   {
      TOPO_MSG(log_id, DBG_ERROR_PRIO, "set param for (param id 0x%lx, size %lu) result %d", param_id, size, result);
   }
   else
   {
      // TOPO_MSG(log_id, DBG_LOW_PRIO, "set param for (param id 0x%lx, size %lu) success", param_id, size);
   }

   CATCH(result, TOPO_MSG_PREFIX, log_id)
   {
   }

   return capi_err_to_ar_result(result);
}

static ar_result_t gen_topo_capi_register_event_v1(uint32_t           log_id,
                                                   capi_t *           capi_ptr,
                                                   gen_topo_module_t *module_ptr,
                                                   topo_reg_event_t * reg_event_payload_ptr,
                                                   bool_t             is_register)
{
   capi_err_t result = AR_EOK;

   capi_proplist_t                     prop_list;
   capi_prop_t                         prop;
   capi_register_event_to_dsp_client_t capi_reg_event_payload;

   capi_reg_event_payload.event_id      = reg_event_payload_ptr->event_id;
   capi_reg_event_payload.is_registered = is_register;

   prop_list.props_num          = 1;
   prop_list.prop_ptr           = &prop;
   prop.id                      = CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT;
   prop.payload.data_ptr        = (int8_t *)(&capi_reg_event_payload);
   prop.payload.actual_data_len = sizeof(capi_register_event_to_dsp_client_t);
   prop.port_info.is_valid      = FALSE;

   result = capi_ptr->vtbl_ptr->set_properties(capi_ptr, &prop_list);

   if (CAPI_FAILED(result))
   {
      TOPO_MSG(log_id, DBG_ERROR_PRIO, "Set property for register events failed, result: %d", result);
      /* On set property failure, return for registration and fall thru for de-registration as event has to be deleted
       * from the event list */
      if (is_register)
      {
         return capi_err_to_ar_result(result);
      }
   }

   return capi_err_to_ar_result(result);
}

static ar_result_t gen_topo_capi_register_event_v2(uint32_t          log_id,
                                                   capi_t *          capi_ptr,
                                                   topo_reg_event_t *reg_event_payload_ptr,
                                                   bool_t            is_register)
{
   capi_err_t                             result = AR_EOK;
   capi_proplist_t                        prop_list;
   capi_prop_t                            prop;
   capi_register_event_to_dsp_client_v2_t capi_reg_event_payload;
   topo_evt_dest_addr_t                   dest_address;

   dest_address.address = 0;
   /* 64 bit destination address is populated as follows:
    * bits 0-31:src port,   bits 32-39:src domain id,   bits 40-47:dest domain id,   bits 48-56: heap id,
    *  bits 57-63:0 */
   dest_address.a.src_port       = reg_event_payload_ptr->src_port;
   dest_address.a.src_domain_id  = reg_event_payload_ptr->src_domain_id;
   dest_address.a.dest_domain_id = reg_event_payload_ptr->dest_domain_id;
   dest_address.a.gpr_heap_index = reg_event_payload_ptr->gpr_heap_index;

   capi_reg_event_payload.dest_address              = dest_address.address;
   capi_reg_event_payload.event_id                  = reg_event_payload_ptr->event_id;
   capi_reg_event_payload.token                     = reg_event_payload_ptr->token;
   capi_reg_event_payload.is_register               = is_register;
   capi_reg_event_payload.event_cfg.actual_data_len = reg_event_payload_ptr->event_cfg.actual_data_len;
   capi_reg_event_payload.event_cfg.data_ptr        = reg_event_payload_ptr->event_cfg.data_ptr;

   prop_list.props_num   = 1;
   prop_list.prop_ptr    = &prop;
   prop.id               = CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT_V2;
   prop.payload.data_ptr = (int8_t *)(&capi_reg_event_payload);
   prop.payload.actual_data_len =
      sizeof(capi_register_event_to_dsp_client_v2_t) + reg_event_payload_ptr->event_cfg.actual_data_len;
   prop.port_info.is_valid = FALSE;

   result = capi_ptr->vtbl_ptr->set_properties(capi_ptr, &prop_list);

   if (CAPI_FAILED(result))
   {
      TOPO_MSG(log_id, DBG_ERROR_PRIO, "Set property for register events v2 failed, result: %d", result);
   }

   return capi_err_to_ar_result(result);
}

ar_result_t gen_topo_set_event_reg_prop_to_capi_modules(uint32_t           log_id,
                                                        capi_t *           capi_ptr,
                                                        gen_topo_module_t *module_ptr,
                                                        topo_reg_event_t * reg_event_payload_ptr,
                                                        bool_t             is_register,
                                                        bool_t *           store_client_info_ptr)
{
   capi_err_t result = AR_EOK;

   *store_client_info_ptr = FALSE;

   // first we try setting V2, if it fails and event cfg payload size is zero, we try V1
   if (AR_EOK != (result = gen_topo_capi_register_event_v2(log_id, capi_ptr, reg_event_payload_ptr, is_register)))
   {
      if (0 == reg_event_payload_ptr->event_cfg.actual_data_len)
      {
         TOPO_MSG(log_id, DBG_HIGH_PRIO, "Trying V1 register event");
         if (AR_EOK !=
             (result =
                 gen_topo_capi_register_event_v1(log_id, capi_ptr, module_ptr, reg_event_payload_ptr, is_register)))
         {
            TOPO_MSG(log_id, DBG_ERROR_PRIO, "Register event v1 failed, result = %lu", result);
            return result;
         }
         else
         {
            *store_client_info_ptr = TRUE;
         }
      }
      else
      {
         TOPO_MSG(log_id, DBG_ERROR_PRIO, "Module failed V2 register event and not trying V1");
      }
   }

   return result;
}

void gen_topo_get_dl_info(uint32_t log_id, void *amdb_handle, bool_t *is_dl, uint32_t **start_addr, uint32_t *so_size)
{
   // Release the handle since we no longer need it.
   amdb_module_handle_info_t module_handle_info;
   module_handle_info.interface_type = AMDB_INTERFACE_TYPE_CAPI;
   module_handle_info.module_type    = 0; // Ignored
   module_handle_info.module_id      = 0; // Ignored
   module_handle_info.handle_ptr     = amdb_handle;
   module_handle_info.result         = AR_EOK;
   amdb_get_dl_info(&module_handle_info, is_dl, start_addr, so_size);
}

ar_result_t gen_topo_capi_set_persistence_prop(uint32_t            log_id,
                                               gen_topo_module_t * module_ptr,
                                               uint32_t            param_id,
                                               bool_t              is_deregister,
                                               spf_cfg_data_type_t cfg_type)
{
   capi_err_t err_code = CAPI_EOK;

   capi_proplist_t props_list;
   capi_prop_t     props[1];

   if (NULL == module_ptr->capi_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Received NULL Capi pointer, failing.");
      return AR_EFAILED;
   }
   capi_param_persistence_info_t pers_info;

   switch (cfg_type)
   {
      case SPF_CFG_DATA_PERSISTENT:
      {
         pers_info.mem_type = CAPI_PERSISTENT_MEM;
         break;
      }
      case SPF_CFG_DATA_SHARED_PERSISTENT:
      {
         pers_info.mem_type = CAPI_GLOBAL_PERSISTENT;
         break;
      }
      case SPF_CFG_DATA_TYPE_DEFAULT:
      {
         return AR_EOK; // no need to set this property
      }
      default:
      { // should never hit this case
         AR_MSG(DBG_ERROR_PRIO, "CFG Type for param is invalid");
         return AR_EBADPARAM;
      }
   }

   pers_info.is_register = !is_deregister;
   pers_info.param_id    = param_id;

   props[0].id                      = CAPI_PARAM_PERSISTENCE_INFO;
   props[0].payload.actual_data_len = sizeof(pers_info);
   props[0].payload.max_data_len    = props[0].payload.actual_data_len;
   props[0].payload.data_ptr        = (int8_t *)(&pers_info);
   props[0].port_info.is_valid      = FALSE;

   props_list.props_num = 1;
   props_list.prop_ptr  = props;

   err_code = module_ptr->capi_ptr->vtbl_ptr->set_properties(module_ptr->capi_ptr, &props_list);

   if ((err_code != CAPI_EOK) && (err_code != CAPI_EUNSUPPORTED))
   {
      TOPO_MSG(log_id, DBG_ERROR_PRIO, "setting PARAM_PERSISTENCE_INFO failed");
      return capi_err_to_ar_result(err_code);
   }
   return AR_EOK;
}

ar_result_t gen_topo_validate_client_pcm_float_media_format(const payload_media_fmt_pcm_t *pcm_fmt_ptr)
{
   return capi_err_to_ar_result(capi_cmn_validate_client_pcm_float_media_format(pcm_fmt_ptr));
}

ar_result_t gen_topo_validate_client_pcm_media_format(const payload_media_fmt_pcm_t *pcm_fmt_ptr)
{
   return capi_err_to_ar_result(capi_cmn_validate_client_pcm_media_format(pcm_fmt_ptr));
}

ar_result_t gen_topo_validate_client_pcm_output_cfg(const payload_pcm_output_format_cfg_t *pcm_cfg_ptr)
{
   return capi_err_to_ar_result(capi_cmn_validate_client_pcm_output_cfg(pcm_cfg_ptr));
}
