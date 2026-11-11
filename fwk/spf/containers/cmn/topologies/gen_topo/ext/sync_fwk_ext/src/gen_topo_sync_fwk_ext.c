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
#include "gen_topo_capi.h"

static void gen_topo_fwk_extn_sync_propagate_threshold_state_upstream(gen_topo_t *       me_ptr,
                                                                      gen_topo_module_t *module_ptr,
                                                                      bool_t             is_threshold_disabled);
/* =======================================================================
Function definitions
========================================================================== */
// function to find sync module and set will-start in the downstream of a module.
static ar_result_t gen_topo_fwk_extn_check_set_will_start(gen_topo_t *me_ptr, gen_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   // if module is a SYNC module and threshold is enabled then send the will-start setparam
   if (module_ptr->flags.need_sync_extn && !module_ptr->flags.is_threshold_disabled)
   {
      TOPO_MSG(me_ptr->gu.log_id, DBG_HIGH_PRIO, "Sending will start to fwk extn module.");

      result = gen_topo_capi_set_param(me_ptr->gu.log_id,
                                       module_ptr->capi_ptr,
                                       FWK_EXTN_SYNC_PARAM_ID_PORT_WILL_START,
                                       (int8_t *)NULL,
                                       0);
      if ((result != AR_EOK) && (result != AR_EUNSUPPORTED))
      {
         TOPO_MSG(me_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: setting will start to fwk extn modules failed err code %d",
                  module_ptr->gu.module_instance_id,
                  result);
         return result;
      }
      else
      {
         TOPO_MSG(me_ptr->gu.log_id,
                  DBG_HIGH_PRIO,
                  "Module 0x%lX: setting will start to fwk extn modules success",
                  module_ptr->gu.module_instance_id);
      }
   }

   // check all output ports and if there is a port which in in at-gap state then continue in that path.
   // need to go recursively to make sure that trigger policy is updated as soon as external input starts data-flow
   for (gu_output_port_list_t *list_ptr = module_ptr->gu.output_port_list_ptr; list_ptr != NULL; LIST_ADVANCE(list_ptr))
   {
      gen_topo_input_port_t *conn_in_port_ptr = (gen_topo_input_port_t *)list_ptr->op_port_ptr->conn_in_port_ptr;
      if (conn_in_port_ptr && conn_in_port_ptr->nblc_end_ptr &&
          (TOPO_DATA_FLOW_STATE_AT_GAP == conn_in_port_ptr->nblc_end_ptr->common.data_flow_state))
      {
         result |= gen_topo_fwk_extn_check_set_will_start(me_ptr,
                                                          (gen_topo_module_t *)
                                                             conn_in_port_ptr->nblc_end_ptr->gu.cmn.module_ptr);
      }
   }

   return result;
}

/*
 * Send a set param to the sync module to indicate data flow will start.
 */
ar_result_t gen_topo_fwk_extn_sync_will_start(gen_topo_t *me_ptr, gen_topo_module_t *module_ptr)
{
   if (!me_ptr->flags.is_sync_module_present || !module_ptr)
   {
      return AR_EOK;
   }

   return gen_topo_fwk_extn_check_set_will_start(me_ptr, module_ptr);
}


// set the threshold state if it is external input port or propagate if it is internal.
static void gen_topo_fwk_extn_sync_set_propagate_threshold_state_from_ip_port(gen_topo_t *           me_ptr,
                                                                              gen_topo_input_port_t *in_port_ptr,
                                                                              bool_t is_threshold_disabled)
{

   // update the threshold state to this port.
   in_port_ptr->flags.is_threshold_disabled_prop = is_threshold_disabled;

   TOPO_MSG(me_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "Input port miid 0x%x port-id 0x%x, threshold_disabled %d",
            in_port_ptr->gu.cmn.module_ptr->module_instance_id,
            in_port_ptr->gu.cmn.id,
            is_threshold_disabled);

   if (in_port_ptr->gu.conn_out_port_ptr)
   {
      // propagate the threshold state upstream
      gen_topo_fwk_extn_sync_propagate_threshold_state_upstream(me_ptr,
                                                                (gen_topo_module_t *)
                                                                   in_port_ptr->gu.conn_out_port_ptr->cmn.module_ptr,
                                                                is_threshold_disabled);
   }
}

// recursive function to send update to all dm modules present in the upstream of a sync module
static void gen_topo_fwk_extn_sync_propagate_threshold_state_upstream(gen_topo_t *       me_ptr,
                                                                      gen_topo_module_t *module_ptr,
                                                                      bool_t             is_threshold_disabled)
{
   // if this is sync module then stop back propagation. (this is to handle when there are back to back sync module
   // smart-sync -> priority-sync)
   if (module_ptr->flags.need_sync_extn)
   {
      return;
   }

   gen_topo_send_dm_consume_partial_input(me_ptr, module_ptr, is_threshold_disabled);

   // propagate backwards till the IPC ports, IPC ports also need to be updated
   // to ensure to make the ports optional if threshold is disabled.
   gu_input_port_list_t *in_port_lists[] = { module_ptr->gu.input_port_list_ptr,
                                             module_ptr->gu.ipc_input_port_list_ptr };
   for (uint32_t i = 0; i < SIZE_OF_ARRAY(in_port_lists); i++)
   {
      for (gu_input_port_list_t *list_ptr = in_port_lists[i]; list_ptr != NULL; LIST_ADVANCE(list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)list_ptr->ip_port_ptr;

         gen_topo_fwk_extn_sync_set_propagate_threshold_state_from_ip_port(me_ptr, in_port_ptr, is_threshold_disabled);
      }
   }
}

// start from each sync module and update the dm module in its upstream to consume partial input (if threshold is
// disabled from that sync)
// also inform external input port about threshold disabled state
void gen_topo_fwk_extn_sync_propagate_threshold_state(gen_topo_t *me_ptr)
{
   if (!me_ptr->flags.is_sync_module_present)
   {
      return;
   }

   for (gu_sg_list_t *sg_list_ptr = me_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         if (module_ptr->flags.need_sync_extn)
         {
            bool_t is_threshold_disabled = module_ptr->flags.is_threshold_disabled;

            // note that sync wont have any IPC input, just handling it here for completeness
            // if IPC ports are supported in future.
            gu_input_port_list_t *in_port_lists[] = { module_ptr->gu.input_port_list_ptr,
                                                      module_ptr->gu.ipc_input_port_list_ptr };
            for (uint32_t i = 0; i < SIZE_OF_ARRAY(in_port_lists); i++)
            {
               for (gu_input_port_list_t *list_ptr = in_port_lists[i]; list_ptr != NULL; LIST_ADVANCE(list_ptr))
               {
                  gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)list_ptr->ip_port_ptr;

                  gen_topo_fwk_extn_sync_set_propagate_threshold_state_from_ip_port(me_ptr,
                                                                                    in_port_ptr,
                                                                                    is_threshold_disabled);
               }
            }
         }
      }
   }
}

bool_t gen_topo_fwk_extn_does_sync_module_exist_downstream_util_(gen_topo_t *me_ptr, gen_topo_input_port_t *in_port_ptr)
{
   gen_topo_module_t *    module_ptr  = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;

   if (module_ptr->flags.need_sync_extn)
   {
      return TRUE;
   }

   // iterate through outputs check if sync module is present in the output path.
   bool_t is_sync_module_present = FALSE;
   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; NULL != out_port_list_ptr;
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      if (out_port_ptr->gu.conn_in_port_ptr)
      {
         is_sync_module_present =
            gen_topo_fwk_extn_does_sync_module_exist_downstream_util_(me_ptr,
                                                                      (gen_topo_input_port_t *)
                                                                         out_port_ptr->gu.conn_in_port_ptr);
      }

      // return as soon as sync module is found in the downstream path.
      if (is_sync_module_present)
      {
         return TRUE;
      }
   }

   return is_sync_module_present;
}
