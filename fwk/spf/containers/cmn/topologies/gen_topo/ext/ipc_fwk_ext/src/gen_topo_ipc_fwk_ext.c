/**
 * \file gen_topo_sync_fwk_ext.c
 *
 * \brief
 *     Implementation of sync fwk extn in gen topo
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause */

/* =======================================================================
Includes
========================================================================== */
#include "gen_topo.h"

ar_result_t gen_topo_get_ipc_port_callback_info(gen_topo_t                            *topo_ptr,
                                                gen_topo_module_t                     *module_ptr,
                                                fwk_extn_prop_ipc_msg_callback_info_t *cb_info,
                                                bool_t                                 is_ext_input,
                                                uint32_t                               port_index)
{
   capi_err_t err_code = CAPI_EOK;

   /* Property structure for async signal trigger */
   typedef struct
   {
      capi_custom_property_t                cust_prop;
      fwk_extn_prop_ipc_msg_callback_info_t ipc_msg_cb_info;
   } callback_info_t;

   callback_info_t callback_info;
   memset(&callback_info, 0, sizeof(callback_info_t));

   /* Populate the async signal trigger */
   callback_info.cust_prop.secondary_prop_id = FWK_EXTN_PROPERTY_ID_IPC_MSG_CALLBACK_INFO;

   capi_prop_t get_prop[] = { { CAPI_CUSTOM_PROPERTY,
                                { (int8_t *)(&callback_info), 0 /*actual_len*/, sizeof(callback_info) /*max_len*/ },
                                { TRUE, is_ext_input, port_index } } };

   capi_proplist_t get_proplist = { SIZE_OF_ARRAY(get_prop), get_prop };

   err_code = module_ptr->capi_ptr->vtbl_ptr->get_properties(module_ptr->capi_ptr, &get_proplist);

   if (sizeof(callback_info) > get_prop[0].payload.actual_data_len)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX: unexpected payload actual_data_len %lu",
               module_ptr->gu.module_instance_id,
               get_prop[0].payload.actual_data_len);
      return AR_EFAILED;
   }

   if (CAPI_FAILED(err_code))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Module 0x%lX: Warning: Get IPC callback info property returned failed",
               module_ptr->gu.module_instance_id);
      return capi_err_to_ar_result(err_code);
   }

   *cb_info = callback_info.ipc_msg_cb_info;

   return AR_EOK;
}

bool_t gen_topo_is_ipc_module_trigger_satisfied(gen_topo_t        *topo_ptr,
                                                gen_topo_module_t *module_ptr,
                                                bool_t             inp_has_no_trigger,
                                                bool_t             out_has_no_trigger)
{
   bool_t ext_trigger_not_satisfied = FALSE;

   // Check IPC TX modules trigger
   // for IPC Tx and Rx module process to be called both input and output trigger needs to be satisfied
   if ((1 == module_ptr->gu.num_input_ports) && (1 == module_ptr->gu.num_ipc_output_ports))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;

      gen_topo_output_port_t *ipc_out_port_ptr =
         (gen_topo_output_port_t *)module_ptr->gu.ipc_output_port_list_ptr->op_port_ptr;

      // if input already is checked for trigger and is not satisfied then no need to check further
      if (inp_has_no_trigger)
      {
         return FALSE;
      }

      if ((TOPO_PORT_STATE_STARTED != in_port_ptr->common.state) ||
          (TOPO_PORT_STATE_STARTED != ipc_out_port_ptr->common.state))
      {
         return FALSE;
      }

      // check if input has non-zero amount of data or metadata.
      bool_t inp_trigger_present =
         (in_port_ptr->common.bufs_ptr[0].data_ptr && in_port_ptr->common.bufs_ptr[0].actual_data_len) ||
         in_port_ptr->common.sdata.flags.end_of_frame || in_port_ptr->common.sdata.metadata_list_ptr;

      fwk_extn_ipc_port_trigger_t ipc_out_trigger =
         gen_topo_get_ipc_port_trigger_from_module(topo_ptr,
                                                   module_ptr,
                                                   FALSE,
                                                   ipc_out_port_ptr->gu.cmn.id,
                                                   &ipc_out_port_ptr->common);

      bool_t is_trigger_satisfied =
         (inp_trigger_present && ((GEN_TOPO_SIGNAL_TRIGGER == topo_ptr->proc_context.curr_trigger) ||
                                  (FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED == ipc_out_trigger)));

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "IPC TX Module 0x%lX: inp_trigger_present: %lu ipc_out_trigger: %lu is_module_trigger_satisfied %lu",
               module_ptr->gu.module_instance_id,
               inp_trigger_present,
               ipc_out_trigger,
               is_trigger_satisfied);
#endif
      return is_trigger_satisfied;
   }
   // Check IPC RX modules trigger
   else if ((1 == module_ptr->gu.num_ipc_input_ports) && (1 == module_ptr->gu.num_output_ports))
   {
      gen_topo_input_port_t *ipc_in_port_ptr =
         (gen_topo_input_port_t *)module_ptr->gu.ipc_input_port_list_ptr->ip_port_ptr;

      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr;

      // if output already is checked for trigger and is not satisfied then no need to check further
      if (out_has_no_trigger)
      {
         return FALSE;
      }

      if ((TOPO_PORT_STATE_STARTED != ipc_in_port_ptr->common.state) ||
          (TOPO_PORT_STATE_STARTED != out_port_ptr->common.state))
      {
         return FALSE;
      }

      fwk_extn_ipc_port_trigger_t ipc_in_trigger = gen_topo_get_ipc_port_trigger_from_module(topo_ptr,
                                                                                             module_ptr,
                                                                                             TRUE,
                                                                                             ipc_in_port_ptr->gu.cmn.id,
                                                                                             &ipc_in_port_ptr->common);

      bool_t out_trigger_present =
         (GEN_TOPO_MODULE_OUTPUT_BUF_ACCESS == out_port_ptr->common.flags.supports_buffer_reuse_extn) ||
         gen_topo_output_has_empty_buffer(out_port_ptr);

      bool_t in_trigger_present =
         ((GEN_TOPO_SIGNAL_TRIGGER == topo_ptr->proc_context.curr_trigger) ||
          (FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED | FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED_OPTIONALLY) & ipc_in_trigger);

      bool_t is_trigger_satisfied = (out_trigger_present && in_trigger_present);

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               "IPC RX Module 0x%lX: ipc_in_trigger: %lu out_trigger_present: %lu is_module_trigger_satisfied %lu",
               module_ptr->gu.module_instance_id,
               ipc_in_trigger,
               out_trigger_present,
               is_trigger_satisfied);
#endif

      return is_trigger_satisfied;
   }
   else
   {

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " IPC Module 0x%lX: Trigger not satisfied, Unsupported Num inputs (%lu, %lu) Num outputs (%lu, %lu) "
               "not supported",
               module_ptr->gu.module_instance_id,
               module_ptr->gu.num_input_ports,
			   module_ptr->gu.num_ipc_input_ports,
               module_ptr->gu.num_output_ports,
			   module_ptr->gu.num_ipc_output_ports);
#endif
      return FALSE;
   }
   return FALSE;
}

void gen_topo_pure_st_check_ipc_input_port_triggers(gen_topo_t        *topo_ptr,
                                                    gen_topo_module_t *module_ptr,
                                                    bool_t            *atleast_one_input_has_data)
{
   for (gu_input_port_list_t *ipc_in_port_list_ptr = module_ptr->gu.ipc_input_port_list_ptr;
        (NULL != ipc_in_port_list_ptr);
        LIST_ADVANCE(ipc_in_port_list_ptr))
   {
      gen_topo_input_port_t *ipc_in_port_ptr = (gen_topo_input_port_t *)ipc_in_port_list_ptr->ip_port_ptr;

      // skip stopped ipc ports for signal triggers.
      if (TOPO_PORT_STATE_STARTED != ipc_in_port_ptr->common.state)
      {
         continue;
      }

      fwk_extn_ipc_port_trigger_t ipc_in_trigger = gen_topo_get_ipc_port_trigger_from_module(topo_ptr,
                                                                                             module_ptr,
                                                                                             TRUE,
                                                                                             ipc_in_port_ptr->gu.cmn.id,
                                                                                             &ipc_in_port_ptr->common);

      *atleast_one_input_has_data =
         ((FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED | FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED_OPTIONALLY) & ipc_in_trigger);
   }
   return;
}

void gen_topo_pure_st_check_ipc_output_port_triggers(gen_topo_t        *topo_ptr,
                                                     gen_topo_module_t *module_ptr,
                                                     bool_t            *atleast_one_op_started,
                                                     bool_t            *all_started_out_ports_have_trigger)
{
   for (gu_output_port_list_t *ipc_out_port_list_ptr = module_ptr->gu.ipc_output_port_list_ptr;
        (NULL != ipc_out_port_list_ptr);
        LIST_ADVANCE(ipc_out_port_list_ptr))
   {
      gen_topo_output_port_t *ipc_out_port_ptr = (gen_topo_output_port_t *)ipc_out_port_list_ptr->op_port_ptr;

      // skip stopped ipc ports for signal triggers.
      if (TOPO_PORT_STATE_STARTED != ipc_out_port_ptr->common.state)
      {
         continue;
      }

      *atleast_one_op_started = TRUE;

      fwk_extn_ipc_port_trigger_t ipc_out_trigger =
         gen_topo_get_ipc_port_trigger_from_module(topo_ptr,
                                                   module_ptr,
                                                   FALSE,
                                                   ipc_out_port_ptr->gu.cmn.id,
                                                   &ipc_out_port_ptr->common);

      if (FWK_EXTN_IPC_PORT_BUFFER_NEEDED == ipc_out_trigger)
      {
         *all_started_out_ports_have_trigger = FALSE;
      }
   }
   return;
}

ar_result_t gen_topo_init_set_get_ipc_data_port_properties(gen_topo_module_t     *module_ptr,
                                                           gen_topo_t            *topo_ptr,
                                                           gen_topo_graph_init_t *graph_init_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   // New ports may be added. need to set it to module.
   for (gu_input_port_list_t *ipc_in_port_list_ptr = module_ptr->gu.ipc_input_port_list_ptr;
        (NULL != ipc_in_port_list_ptr);
        LIST_ADVANCE(ipc_in_port_list_ptr))
   {
      gen_topo_input_port_t *ipc_in_port_ptr = (gen_topo_input_port_t *)ipc_in_port_list_ptr->ip_port_ptr;

      if (GU_STATUS_NEW == ipc_in_port_ptr->gu.cmn.gu_status)
      {
         if (module_ptr->capi_ptr)
         {
            if (GU_STATUS_UPDATED == module_ptr->gu.gu_status)
            {
               // if module is new then gen_topo_intf_extn_data_ports_hdl_at_init takes care of this.
               TRY(result,
                   gen_topo_capi_set_data_port_op(module_ptr,
                                                  INTF_EXTN_DATA_PORT_OPEN,
                                                  &ipc_in_port_ptr->common.last_issued_opcode,
                                                  TRUE, /*is_input*/
                                                  ipc_in_port_ptr->gu.cmn.index,
                                                  ipc_in_port_ptr->gu.cmn.id));
            }
         }
      }

      // port is never updated
      ipc_in_port_ptr->gu.cmn.gu_status = GU_STATUS_DEFAULT;
   }

   for (gu_output_port_list_t *ipc_out_port_list_ptr = module_ptr->gu.ipc_output_port_list_ptr; (NULL != ipc_out_port_list_ptr);
        LIST_ADVANCE(ipc_out_port_list_ptr))
   {
      gen_topo_output_port_t *ipc_out_port_ptr    = (gen_topo_output_port_t *)ipc_out_port_list_ptr->op_port_ptr;

      if (GU_STATUS_NEW == ipc_out_port_ptr->gu.cmn.gu_status)
      {
         if (module_ptr->capi_ptr && (GU_STATUS_UPDATED == module_ptr->gu.gu_status))
         {
            // if module is new, then gen_topo_create_module takes care of this.
            TRY(result,
                gen_topo_capi_set_data_port_op(module_ptr,
                                               INTF_EXTN_DATA_PORT_OPEN,
                                               &ipc_out_port_ptr->common.last_issued_opcode,
                                               FALSE, /*is_input*/
                                               ipc_out_port_ptr->gu.cmn.index,
                                               ipc_out_port_ptr->gu.cmn.id));
         }
      }
      // port is never updated
      ipc_out_port_ptr->gu.cmn.gu_status = GU_STATUS_DEFAULT;
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
   }

   return result;
}