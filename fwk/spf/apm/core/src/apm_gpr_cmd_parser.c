/**
 * \file apm_gpr_cmd_parser.c
 *
 * \brief
 *     This file contains parser utilities for APR command parser
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/****************************************************************************
 * INCLUDE HEADER FILES                                                     *
 ****************************************************************************/

#include "apm_internal.h"
#include "apm_graph_db.h"
#include "apm_msg_utils.h"
#include "apm_graph_utils.h"
#include "apm_offload_utils.h"
#include "apm_proxy_def.h"
#include "apm_proxy_utils.h"
#include "apm_proxy_vcpm_utils.h"
#include "apm_cmd_utils.h"
#include "apm_data_path_utils.h"
#include "offload_path_delay_api.h"
#include "apm_cntr_peer_heap_utils.h"
#include "apm_runtime_link_hdlr_utils.h"
#include "apm_gpr_cmd_parser.h"
#include "apm_ext_cmn.h"

//#define APM_DEBUG_CMD_PARSING

/****************************************************************************
 * GLOBALS                                                                  *
 ****************************************************************************/

ar_result_t apm_get_prop_data_size(uint8_t * prop_payload_ptr,
                                   uint32_t  num_prop,
                                   uint32_t *prop_list_payload_size_ptr,
                                   uint32_t  cmd_payload_size)
{
   ar_result_t      result = AR_EOK;
   uint32_t         prop_id_payload_size;
   uint32_t         prop_list_data_size = 0;
   uint8_t *        curr_payload_ptr;
   apm_prop_data_t *prop_data_ptr;

   /** Init the return pointer */
   *prop_list_payload_size_ptr = 0;

   /** Validate if the prop payload pointer is at least 4 byte
    *  aligned. Since we only need to check the alignment,
    *  typecast of ptr to uint32 should be ok */
   if (!prop_payload_ptr || (!IS_ALIGN_4_BYTE((uintptr_t)prop_payload_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_get_prop_data_size(): Invalid prop_payload_ptr: 0x%lX, num_prop[%lu]",
             prop_payload_ptr,
             num_prop);

      return AR_EBADPARAM;
   }

   AR_MSG(DBG_HIGH_PRIO,
          "apm_get_prop_data_size(): prop_payload_ptr: 0x%lX, num_prop[%lu]",
          prop_payload_ptr,
          num_prop);

   curr_payload_ptr = prop_payload_ptr;

   /** Iterate over property list */
   while (num_prop)
   {
      prop_data_ptr = (apm_prop_data_t *)curr_payload_ptr;

      if (!IS_ALIGN_4_BYTE(prop_data_ptr->prop_size))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_get_prop_data_size(): Invalid prop_size[%lu], prop_data_ptr: 0x%lX",
                prop_data_ptr->prop_size,
                prop_payload_ptr);

         /** Clear any partial aggregation  */
         *prop_list_payload_size_ptr = 0;

         return AR_EBADPARAM;
      }

      if (0 == prop_data_ptr->prop_size || prop_data_ptr->prop_size > cmd_payload_size)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_get_prop_data_size(): Invalid prop_size[%lu] either prop size is 0 or greater than cmd "
                "payload size[%lu], prop_data_ptr: 0x%lX",
                prop_data_ptr->prop_size,
                cmd_payload_size,
                prop_payload_ptr);

         /** Clear any partial aggregation  */
         *prop_list_payload_size_ptr = 0;

         return AR_EBADPARAM;
      }

      prop_id_payload_size = (sizeof(apm_prop_data_t) + prop_data_ptr->prop_size);

      /** Accumulate the property data payload size */
      prop_list_data_size += prop_id_payload_size;

      /** Get the pointer to next property */
      curr_payload_ptr += prop_id_payload_size;

      /** Decrement num prop */
      num_prop--;
   }

   /** Populate the return value */
   *prop_list_payload_size_ptr = prop_list_data_size;

   return result;
}

ar_result_t apm_cfg_sub_graph_prop(apm_sub_graph_t *sub_graph_node_ptr, uint8_t *sg_prop_data_ptr, uint32_t num_props)
{
   ar_result_t           result = AR_EOK;
   uint8_t *             curr_prop_data_ptr;
   uint32_t              sub_graph_id;
   apm_sub_graph_prop_t *sg_prop_ptr;
   uint32_t              prop_payload_size = 0;
   apm_prop_data_t *     prop_data_ptr;

   /** Get the sub-graph ID */
   sub_graph_id = sub_graph_node_ptr->sub_graph_id;

   /** Get the pointer to sub-graph property structure */
   sg_prop_ptr = &sub_graph_node_ptr->prop;

   /** Get the pointer to start of property list struct */
   curr_prop_data_ptr = sg_prop_data_ptr;

   /** Iterate over the sub-graph property list */
   while (num_props)
   {
      prop_data_ptr = (apm_prop_data_t *)curr_prop_data_ptr;

      /** Store the sub-graph properties */
      switch (prop_data_ptr->prop_id)
      {
         case APM_SUB_GRAPH_PROP_ID_PERF_MODE:
         {
            if (prop_data_ptr->prop_size < sizeof(apm_sg_prop_id_perf_mode_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "SG_CFG: Less than required size : %lu provided for SG PROP ID: %lu, SG_ID[0x%lX]",
                      prop_data_ptr->prop_size,
                      prop_data_ptr->prop_id,
                      sub_graph_id);

               /** Abort processing and return */
               return AR_EBADPARAM;
            }
            apm_sg_prop_id_perf_mode_t *prop_cfg_ptr =
               (apm_sg_prop_id_perf_mode_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            /** Store the perf mode for this sub-graph */
            sg_prop_ptr->perf_mode = prop_cfg_ptr->perf_mode;
            break;
         }
         case APM_SUB_GRAPH_PROP_ID_DIRECTION:
         {
            if (prop_data_ptr->prop_size < sizeof(apm_sg_prop_id_direction_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "SG_CFG: Less than required size : %lu provided for SG PROP ID: %lu, SG_ID[0x%lX]",
                      prop_data_ptr->prop_size,
                      prop_data_ptr->prop_id,
                      sub_graph_id);

               /** Abort processing and return */
               return AR_EBADPARAM;
            }

            apm_sg_prop_id_direction_t *prop_cfg_ptr =
               (apm_sg_prop_id_direction_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            /** Store the sub-graph direction */
            sg_prop_ptr->direction = prop_cfg_ptr->direction;
            break;
         }
         case APM_SUB_GRAPH_PROP_ID_SCENARIO_ID:
         {
            if (prop_data_ptr->prop_size < sizeof(apm_sg_prop_id_scenario_id_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "SG_CFG: Less than required size : %lu provided for SG PROP ID: %lu, SG_ID[0x%lX]",
                      prop_data_ptr->prop_size,
                      prop_data_ptr->prop_id,
                      sub_graph_id);

               /** Abort processing and return */
               return AR_EBADPARAM;
            }

            apm_sg_prop_id_scenario_id_t *prop_cfg_ptr =
               (apm_sg_prop_id_scenario_id_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            /** Store the scenario ID */
            sg_prop_ptr->scenario_id = prop_cfg_ptr->scenario_id;
            break;
         }
         case APM_SUB_GRAPH_PROP_ID_VSID:
         {
            if (prop_data_ptr->prop_size < sizeof(apm_sg_prop_id_vsid_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "SG_CFG: Less than required size : %lu provided for SG PROP ID: %lu, SG_ID[0x%lX]",
                      prop_data_ptr->prop_size,
                      prop_data_ptr->prop_id,
                      sub_graph_id);

               /** Abort processing and return */
               return AR_EBADPARAM;
            }

            apm_sg_prop_id_vsid_t *prop_cfg_ptr =
               (apm_sg_prop_id_vsid_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            /** Store the scenario ID */
            sg_prop_ptr->vsid = prop_cfg_ptr->vsid;
            break;
         }
         case APM_SUBGRAPH_PROP_ID_CLOCK_SCALE_FACTOR:
         {
            apm_subgraph_prop_id_clock_scale_factor_t *prop_cfg_ptr =
               (apm_subgraph_prop_id_clock_scale_factor_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            /** Store the scenario ID */
            sg_prop_ptr->enable_duty_cycling                = prop_cfg_ptr->enable_duty_cycling;
            sg_prop_ptr->duty_cycling_clock_scale_factor_q4 = prop_cfg_ptr->duty_cycling_clock_scale_factor_q4;
            sg_prop_ptr->clock_scale_factor_q4              = prop_cfg_ptr->clock_scale_factor_q4;
            break;
         }
         case APM_SUBGRAPH_PROP_ID_BW_SCALE_FACTOR:
         {
            apm_subgraph_prop_id_bw_scale_factor_t *prop_cfg_ptr =
               (apm_subgraph_prop_id_bw_scale_factor_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));
            sg_prop_ptr->bus_scale_factor_q4              = prop_cfg_ptr->bus_scale_factor_q4;
            break;
         }

         default:
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "SG_CFG: Invalid SG PROP ID: %lu, SG_ID[0x%lX]",
                   prop_data_ptr->prop_id,
                   sub_graph_id);

            /** Abort processing and return */
            return AR_EBADPARAM;
         }

      } /** End of switch (curr_prop_data_ptr->prop_id) */

      /** Accumulate the prop payload size */
      prop_payload_size += (sizeof(apm_prop_data_t) + prop_data_ptr->prop_size);

      /** Increment the current property data pointer */
      curr_prop_data_ptr += (sizeof(apm_prop_data_t) + prop_data_ptr->prop_size);

      /** Decrement the number of property */
      num_props--;

   } /** End of while (num_prop) */

   /** Update the property payload size */
   sub_graph_node_ptr->prop.payload_size = prop_payload_size;

   return result;
}

static ar_result_t apm_parse_subgraph_list(apm_t *apm_info_ptr, uint8_t *mod_pid_payload_ptr, uint32_t payload_size)
{
   ar_result_t                   result                      = AR_EOK;
   apm_sub_graph_t *             sub_graph_node_ptr          = NULL;
   uint32_t                      num_prop                    = 0;
   uint32_t                      local_domain_id             = 0;
   uint32_t                      sub_graph_prop_payload_size = 0;
   uint32_t                      sub_graph_cntr;
   apm_sub_graph_cfg_t *         curr_sg_cfg_ptr;
   uint8_t *                     curr_payload_ptr;
   apm_param_id_sub_graph_cfg_t *pid_data_ptr;
   apm_cmd_ctrl_t *              apm_cmd_ctrl_ptr;
   uint32_t                      expected_payload_size;
   uint32_t                      curr_payload_size;

   /** Validate the payload pointer */
   if (!mod_pid_payload_ptr || !payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "SG_PARSE: PID payload pointer[0x%lX] and/or size[%lu] is NULL",
             mod_pid_payload_ptr,
             payload_size);

      return AR_EFAILED;
   }

   __gpr_cmd_get_host_domain_id(&local_domain_id);

   /* Get the pointer to current APM command control obj */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to sub-graph config payload start */
   pid_data_ptr = (apm_param_id_sub_graph_cfg_t *)mod_pid_payload_ptr;

   /** Validate the number of sub-graphs being configured */
   if (!pid_data_ptr->num_sub_graphs)
   {
      AR_MSG(DBG_ERROR_PRIO, "SG_PARSE: Num sub-graphs is 0");

      return AR_EBADPARAM;
   }

   /** Get the number of sub-graphs */
   sub_graph_cntr = pid_data_ptr->num_sub_graphs;

   /** Compute min payload size */
   expected_payload_size = sizeof(apm_param_id_sub_graph_cfg_t) + (sub_graph_cntr * sizeof(apm_sub_graph_cfg_t));

   /** Validate provided payload size */
   if (payload_size < expected_payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "SG_PARSE: Insufficient payload size[%lu bytes], min size[%lu bytes], num sg[%lu]",
             payload_size,
             expected_payload_size,
             sub_graph_cntr);

      return AR_EBADPARAM;
   }

   AR_MSG(DBG_MED_PRIO,
          "SG_PARSE: Processing NUM_SUB_GRAPHS[%lu], payload_size[%lu bytes]",
          pid_data_ptr->num_sub_graphs,
          payload_size);

   /** Get the pointer to start of sub-graph object list */
   curr_payload_ptr = mod_pid_payload_ptr + sizeof(apm_param_id_sub_graph_cfg_t);

   /** Init current payload size */
   curr_payload_size = payload_size - sizeof(apm_param_id_sub_graph_cfg_t);

   /** Iterate over the list of sub-graph config objects */
   for (uint32_t idx = pid_data_ptr->num_sub_graphs; idx > 0; idx--)
   {
      /** Get the pointer to current sub-graph object */
      curr_sg_cfg_ptr = (apm_sub_graph_cfg_t *)curr_payload_ptr;

      if ((AR_INVALID_INSTANCE_ID == curr_sg_cfg_ptr->sub_graph_id) ||
          (curr_sg_cfg_ptr->sub_graph_id < AR_DYNAMIC_INSTANCE_ID_RANGE_BEGIN))
      {

         AR_MSG(DBG_ERROR_PRIO, "SG_PARSE: Invalid SG_ID: [0x%lX]", curr_sg_cfg_ptr->sub_graph_id);

         return AR_EBADPARAM;
      }

      /** Check if the sub-graph ID already exists in the graph data base. */

      result = apm_db_get_sub_graph_node(&apm_info_ptr->graph_info,
                                         curr_sg_cfg_ptr->sub_graph_id,
                                         &sub_graph_node_ptr,
                                         APM_DB_OBJ_CREATE_REQD);

      if (sub_graph_node_ptr)
      {
         /** Trying to create a sub graph which is already existing in the system is considered
          *  as an invalid configuration from HLOS and the open parsing would return a
          *  failure in the Master PD context for same client and should return Success for a new client.
          */
         if (APM_PROC_DOMAIN_ID_ADSP == local_domain_id) // todo : check with Master PD
         {
            if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
                apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_parse_sg_list_fptr)
            {
               if (AR_EOK != (result = apm_info_ptr->ext_utils.multi_client_vtbl_ptr
                                          ->apm_multi_client_parse_sg_list_fptr(sub_graph_node_ptr,
                                                                                apm_cmd_ctrl_ptr,
                                                                                &curr_payload_ptr)))
               {
                  return result;
               }
               continue;
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO, "SG_PARSE: SG_ID: [0x%lX] already exists", sub_graph_node_ptr->sub_graph_id);
            return AR_EDUPLICATE;
         }
         }
         else
         {
            /** The above scenario is gracefully handled in the Satellite APM context, as multiple OLC
             *  can be created within a sub graph in Master PD. Each OLC will create a payload with the
             *  same sub graph configuration and Satellite APM could indeed get configuration to create the
             *  same sub graph multiple times. We assume the property configurations do not change across
             *  these multiple configurations and ignore the configuration payload if the sub graph is already
             *  created. Please note both the OLC instance in the master PD would not communicate with each-other.
             */

            /** Get the pointer to start of property list struct */
            curr_payload_ptr += sizeof(apm_sub_graph_cfg_t);

            /** Get the property payload size */
            if (AR_EOK != (result = apm_get_prop_data_size(curr_payload_ptr,
                                                           curr_sg_cfg_ptr->num_sub_graph_prop,
                                                           &sub_graph_prop_payload_size,
                                                           apm_info_ptr->curr_cmd_ctrl_ptr->cmd_payload_size)))
            {
               return result;
            }

            curr_payload_ptr += sub_graph_prop_payload_size;

            AR_MSG(DBG_HIGH_PRIO,
                   "SG_PARSE: SG_ID: [0x%lX] already exists. "
                   "This is fine in Satellite PD. continue parsing..",
                   curr_sg_cfg_ptr->sub_graph_id);

            continue;
         }
      }
      else if (AR_EFAILED == result)
      {
         AR_MSG(DBG_ERROR_PRIO, "SG_PARSE: Failed to get SG_ID: [0x%lX]", curr_sg_cfg_ptr->sub_graph_id);

         return AR_EFAILED;
      }

      /** If the sub-graph ID does not exist */

      /** ALlocate memory for sub-graph node for APM graph DB */
      if (NULL ==
          (sub_graph_node_ptr = (apm_sub_graph_t *)posal_memory_malloc(sizeof(apm_sub_graph_t), APM_INTERNAL_STATIC_HEAP_ID)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "SG_PARSE: Failed to allocat SG node mem, SG_ID: [0x%lX]",
                curr_sg_cfg_ptr->sub_graph_id);

         return AR_ENOMEMORY;
      }

      /** Clear the allocated struct */
      memset(sub_graph_node_ptr, 0, sizeof(apm_sub_graph_t));

      /** Store the sub-graph ID */
      sub_graph_node_ptr->sub_graph_id = curr_sg_cfg_ptr->sub_graph_id;

      /** Get the number of sub-graph properties configured */
      num_prop = curr_sg_cfg_ptr->num_sub_graph_prop;

      /** Get the pointer to start of property list struct */
      curr_payload_ptr += sizeof(apm_sub_graph_cfg_t);

      /** Store sub-graph properties. Also validates and flag any
       *  errors.
       *  Sub-graph properties for the same ID are not expected to
       *  change across multiple GRAPH OPEN Commands.
       */
      if (AR_EOK != (result = apm_cfg_sub_graph_prop(sub_graph_node_ptr, curr_payload_ptr, num_prop)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "SG_PARSE: Failed to parse SG prop list, SG_ID: [0x%lX]",
                sub_graph_node_ptr->sub_graph_id);

         goto _bail_out_sg_list_parse;
      }

      /** After all successful validations update the number of
       *  sub-graph properties configured */
      sub_graph_node_ptr->prop.num_properties = curr_sg_cfg_ptr->num_sub_graph_prop;

      if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
          apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_add_sg_client_context_fptr)
      {
         if (AR_EOK !=
             (result = apm_info_ptr->ext_utils.multi_client_vtbl_ptr
                          ->apm_multi_client_add_sg_client_context_fptr(sub_graph_node_ptr, apm_cmd_ctrl_ptr)))
         {
            goto _bail_out_sg_list_parse;
         }
      }

      /** After all the validations are successful, add the
         sub-graph to the APM graph data base. This call also
         increments the number of sub-graph counter */
      if (AR_EOK != (result = apm_db_add_node_to_list(&apm_info_ptr->graph_info.sub_graph_list_ptr,
                                                      sub_graph_node_ptr,
                                                      &apm_info_ptr->graph_info.num_sub_graphs)))
      {
         goto _bail_out_sg_list_parse;
      }

      /** Cache this sub-graph ID in the APM graph open command
       *  control */
      if (AR_EOK != (result = apm_db_add_node_to_list(&apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.sg_id_list_ptr,
                                                      sub_graph_node_ptr,
                                                      &apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.num_sub_graphs)))
      {
         /** Remove this sub-graph node from the APM graph DB */
         apm_db_remove_node_from_list(&apm_info_ptr->graph_info.sub_graph_list_ptr,
                                      sub_graph_node_ptr,
                                      &apm_info_ptr->graph_info.num_sub_graphs);

         goto _bail_out_sg_list_parse;
      }

      /** Initiate Proxy Command control if required.*/
      (void)apm_proxy_util_check_if_proxy_required(apm_info_ptr->curr_cmd_ctrl_ptr,
                                                   &apm_info_ptr->graph_info,
                                                   sub_graph_node_ptr);

      AR_MSG(DBG_MED_PRIO, "SG_PARSE: Parsed SG_ID: [0x%lX]", curr_sg_cfg_ptr->sub_graph_id);

      /** Store the sub-graph object pointer to be sent to containers */
      sub_graph_node_ptr->cfg_cmd_payload = curr_sg_cfg_ptr;

      /** Advance the pointer to point to next sub-graph config */
      curr_payload_ptr += sub_graph_node_ptr->prop.payload_size;

      /** Decrement the available payload size */
      curr_payload_size -= (sizeof(apm_sub_graph_cfg_t) + sub_graph_node_ptr->prop.payload_size);

      /** Remaining sub-graph */
      sub_graph_cntr--;

      /** Check the min size required for remaining sub-graphs */
      if (sub_graph_cntr && (curr_payload_size < (sub_graph_cntr * sizeof(apm_sub_graph_cfg_t))))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "SG_PARSE: Insuffient payload size rem[%lu], num rem sg[%lu]",
                curr_payload_size,
                sub_graph_cntr);

         return AR_EBADPARAM;
      }

   } /** End of for loop (sub_graph_cntr) */

   return result;

_bail_out_sg_list_parse:

   /** Free up the memory for allocated sub-graph node */
   if (sub_graph_node_ptr)
   {
      posal_memory_free(sub_graph_node_ptr);
   }

   return result;
}

ar_result_t apm_cfg_container_prop(apm_container_t *container_node_ptr, uint8_t *cont_prop_data_ptr, uint32_t num_props)
{
   ar_result_t           result = AR_EOK;
   uint8_t *             curr_prop_data_ptr;
   uint32_t              container_id;
   apm_container_prop_t *cont_prop_ptr;
   uint32_t              prop_payload_size = 0;
   apm_prop_data_t *     prop_data_ptr;
   bool_t                mandatory_prop_set = FALSE;

   /** Get the sub-graph ID */
   container_id = container_node_ptr->container_id;

   /** Get the pointer to sub-graph property structure */
   cont_prop_ptr = &container_node_ptr->prop;

   /** Get the pointer to start of property list struct */
   curr_prop_data_ptr = cont_prop_data_ptr;

   /*initialize to default*/
   cont_prop_ptr->heap_id = APM_CONT_HEAP_DEFAULT;

   /*initialize to self heap id*/
   cont_prop_ptr->peer_heap_id = cont_prop_ptr->heap_id;
   /** Iterate over the sub-graph property list */
   while (num_props)
   {
      prop_data_ptr = (apm_prop_data_t *)curr_prop_data_ptr;

      /** Store the sub-graph properties */
      switch (prop_data_ptr->prop_id)
      {
         case APM_CONTAINER_PROP_ID_CONTAINER_TYPE:
         {
            apm_cont_prop_id_type_t *prop_cfg_ptr =
               (apm_cont_prop_id_type_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            if (1 != prop_cfg_ptr->version)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CONTAINER_CFG: version must be one for 0x%lx, CNT_ID 0x%lx",
                      prop_data_ptr->prop_id,
                      container_id);

               return AR_EBADPARAM;
            }

            cont_prop_ptr->cntr_type = prop_cfg_ptr->type_id;

            mandatory_prop_set |= TRUE;
            break;
         }
         case APM_CONTAINER_PROP_ID_GRAPH_POS:
         {
            apm_cont_prop_id_graph_pos_t *prop_cfg_ptr =
               (apm_cont_prop_id_graph_pos_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            /** Store the graph position */
            cont_prop_ptr->graph_position = prop_cfg_ptr->graph_pos;
            break;
         }
         case APM_CONTAINER_PROP_ID_STACK_SIZE:
         {
            apm_cont_prop_id_stack_size_t *prop_cfg_ptr =
               (apm_cont_prop_id_stack_size_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            /** Store the stack size */
            cont_prop_ptr->stack_size = prop_cfg_ptr->stack_size;
            break;
         }
         case APM_CONTAINER_PROP_ID_PROC_DOMAIN:
         {
            apm_cont_prop_id_proc_domain_t *prop_cfg_ptr =
               (apm_cont_prop_id_proc_domain_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            /** Store the proc_domain */
            cont_prop_ptr->proc_domain = prop_cfg_ptr->proc_domain;
            break;
         }
         case APM_CONTAINER_PROP_ID_PARENT_CONTAINER_ID:
         {
            uint32_t host_domain_id = 0;
            __gpr_cmd_get_host_domain_id(&host_domain_id);
            apm_cont_prop_id_parent_container_t *prop_cfg_ptr =
               (apm_cont_prop_id_parent_container_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            AR_MSG(DBG_HIGH_PRIO,
                   "CONTAINER_CFG: Parent Container PROP ID: [0x%lX] "
                   "sent to container CNT_ID[0x%lX] in process domain %lu "
                   "with configuration Parent_CNT_ID[0x%lX]",
                   prop_data_ptr->prop_id,
                   container_id,
                   host_domain_id,
                   prop_cfg_ptr->parent_container_id);

            break;
         }
         case APM_CONTAINER_PROP_ID_HEAP_ID:
         {
            apm_cont_prop_id_heap_id_t *prop_cfg_ptr =
               (apm_cont_prop_id_heap_id_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            /** Store the heap_id */
            cont_prop_ptr->heap_id = prop_cfg_ptr->heap_id;

            break;
         }
         case APM_CONTAINER_PROP_ID_PEER_HEAP_ID:
         {
            apm_cont_prop_id_peer_heap_id_t *prop_cfg_ptr =
               (apm_cont_prop_id_peer_heap_id_t *)(curr_prop_data_ptr + sizeof(apm_prop_data_t));

            /** Store the peer heap_id */
            cont_prop_ptr->peer_heap_id = prop_cfg_ptr->heap_id;

            break;
         }
         case APM_CONTAINER_PROP_ID_THREAD_PRIORITY:
         case APM_CONTAINER_PROP_ID_THREAD_SCHED_POLICY:
         case APM_CONTAINER_PROP_ID_THREAD_CORE_AFFINITY:
         case APM_CONTAINER_PROP_ID_FRAME_SIZE:
         {
            break;
         }
         default:
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "CONTAINER_CFG: Invalid Container PROP ID: %lu, CONT_ID[0x%lX]",
                   prop_data_ptr->prop_id,
                   container_id);

            /** Abort processing and return */
            return AR_EBADPARAM;
         }

      } /** End of switch (curr_prop_data_ptr->prop_id) */

      /** Accumulate the prop payload size */
      prop_payload_size += (sizeof(apm_prop_data_t) + prop_data_ptr->prop_size);

      /** Increment the current property data pointer */
      curr_prop_data_ptr += (sizeof(apm_prop_data_t) + prop_data_ptr->prop_size);

      /** Decrement the number of property */
      num_props--;

   } /** End of while (num_prop) */

   if (!mandatory_prop_set) // if mandatory prop is not set, we fail Graph Open
   {
      AR_MSG(DBG_ERROR_PRIO, "CONTAINER_CFG: APM_CONTAINER_PROP_ID_CAPABILITY_LIST is mandatory - Not found. Failing");
      return AR_EFAILED;
   }

   /** Update the property payload size */
   container_node_ptr->prop_payload_size = prop_payload_size;

   return result;
}

ar_result_t apm_parse_container_list(apm_t *apm_info_ptr, uint8_t *mod_pid_payload_ptr, uint32_t payload_size)
{
   ar_result_t                   result             = AR_EOK;
   apm_container_t *             container_node_ptr = NULL;
   uint32_t                      container_cntr;
   apm_container_cfg_t *         curr_cont_cfg_ptr;
   uint8_t *                     curr_payload_ptr;
   uint32_t                      prop_data_size;
   uint8_t *                     prop_data_ptr;
   apm_param_id_container_cfg_t *pid_data_ptr;
   cntr_cmn_init_params_t        cont_init_params;
   spf_handle_t *                cont_handle_ptr = NULL;
   apm_cont_cmd_ctrl_t *         cont_cmd_ctrl_ptr;
   uint32_t                      expected_payload_size;
   uint32_t                      curr_payload_size;
   apm_ext_utils_t *             ext_utils_ptr;

   /** Validate the payload pointer */
   if (!mod_pid_payload_ptr || !payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CNTR_PARSE: PID payload pointer[0x%lX] and/or size[%lu] is NULL",
             mod_pid_payload_ptr,
             payload_size);

      return AR_EFAILED;
   }

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Get the pointer to container config payload start */
   pid_data_ptr = (apm_param_id_container_cfg_t *)mod_pid_payload_ptr;

   /** Validate the number of containers being configured */
   if (!pid_data_ptr->num_container)
   {
      AR_MSG(DBG_ERROR_PRIO, "CONT_PARSE: Num container is 0");

      return AR_EBADPARAM;
   }

   /** Get the number of containers */
   container_cntr = pid_data_ptr->num_container;

   /** Compute min payload size */
   expected_payload_size = sizeof(apm_param_id_container_cfg_t) + (container_cntr * sizeof(apm_container_cfg_t));

   /** Validate provided payload size */
   if (payload_size < expected_payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CONT_PARSE: Insufficient payload size[%lu bytes], min size[%lu bytes], num_cont[%lu]",
             payload_size,
             expected_payload_size,
             container_cntr);

      return AR_EBADPARAM;
   }

   AR_MSG(DBG_MED_PRIO,
          "CONT_PARSE: Processing NUM_CONTAINERS[%lu], payload_size[%lu bytes]",
          container_cntr,
          payload_size);

   /** Get the pointer to start of container object list */
   curr_payload_ptr = mod_pid_payload_ptr + sizeof(apm_param_id_container_cfg_t);

   /** Init current payload size */
   curr_payload_size = (payload_size - sizeof(apm_param_id_container_cfg_t));

   /** Iterate over the list of container cfg objects */
   while (container_cntr)
   {
      /** Get the pointer to current container cfg object */
      curr_cont_cfg_ptr = (apm_container_cfg_t *)curr_payload_ptr;

      if ((AR_INVALID_INSTANCE_ID == curr_cont_cfg_ptr->container_id) ||
          (curr_cont_cfg_ptr->container_id < AR_DYNAMIC_INSTANCE_ID_RANGE_BEGIN))
      {
         AR_MSG(DBG_ERROR_PRIO, "CONT_PARSE: Invalid CONT_ID[0x%lX]", curr_cont_cfg_ptr->container_id);
         return AR_EBADPARAM;
      }

      /** Check if the container ID already exists
       *  in the graph data base */
      if (AR_EOK != (result = apm_db_get_container_node(&apm_info_ptr->graph_info,
                                                        curr_cont_cfg_ptr->container_id,
                                                        &container_node_ptr,
                                                        APM_DB_OBJ_CREATE_REQD)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CONT_PARSE: Failed to get container node: CONT_ID[0x%lX]",
                curr_cont_cfg_ptr->container_id);

         return AR_EFAILED;
      }

      /** If the container node already exits */
      if (container_node_ptr)
      {
         AR_MSG(DBG_MED_PRIO, "CONT_PARSE: Container ID: [0x%lX] already exists", curr_cont_cfg_ptr->container_id);

         /** Advance the pointer to point to next container config */
         curr_payload_ptr += (sizeof(apm_container_cfg_t) + container_node_ptr->prop_payload_size);

         /** Decrement the container counter */
         container_cntr--;

         /** Nothing to do */
         continue;
      }

      /** Get container prop data payload size */
      prop_data_ptr = (curr_payload_ptr + sizeof(apm_container_cfg_t));

      /** Get the overall property data size for this container */
      if (AR_EOK != (result = apm_get_prop_data_size(prop_data_ptr,
                                                     curr_cont_cfg_ptr->num_prop,
                                                     &prop_data_size,
                                                     apm_info_ptr->curr_cmd_ctrl_ptr->cmd_payload_size)))
      {
         return result;
      }

      /* if the container is marked for offload ( i.e., process_domain != Master_DSP domain),
       * then there is no need to create the container.
       * We need to cache the payload corresponding to the satellite container and continue
       * These changes are only required on the master DSP.
       * On the Satellite DSP, ideally we need to create all the containers */
      // todo VB: need to check if APM can have some process_domain information.
      // It will help to avoid this code from being exercised on  Satellite DSP.
      // if (apm_info_ptr->process_domain_mode) // Master Mode is 1)
      {
         uint32_t sat_cnt_prop_payload_size = (sizeof(apm_container_cfg_t) + prop_data_size);
         bool_t   is_cont_offloaded         = FALSE;

         /*  if the function return value is success, it means the following
          *   - the container process domain is specified and is in master DSP (or)
          *   - the container process domain is not specified and assumed to be in master DSP (or)
          *   - the container process domain is specified and is in satellite DSP
          *   - sat_cnt_prop_payload_size will help to determine if the container is in satellite DSP */

         if (ext_utils_ptr->offload_vtbl_ptr &&
             ext_utils_ptr->offload_vtbl_ptr->apm_check_and_cache_satellite_container_config_fptr)
         {
            if (AR_EOK != (result = ext_utils_ptr->offload_vtbl_ptr
                                       ->apm_check_and_cache_satellite_container_config_fptr(apm_info_ptr,
                                                                                             curr_cont_cfg_ptr,
                                                                                             &is_cont_offloaded)))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CONT_PARSE: Failed to cache the satellite container payload: CONT_ID[0x%lX]",
                      curr_cont_cfg_ptr->container_id);

               return result;
            }
         }

         /* if the satellite container flag is set to true, indicates that the container need not be created in
          * master process domain
          */
         if (TRUE == is_cont_offloaded)
         {
            curr_payload_ptr += sat_cnt_prop_payload_size;
            container_cntr--;
            continue;
         }
      }

      /** If the container does not exist */

      /** Increment the Global container create count */
      apm_info_ptr->graph_info.container_count++;

      /** Clear the container init param structure */
      memset(&cont_init_params, 0, sizeof(cntr_cmn_init_params_t));

      /** Populate init param structure  */
      cont_init_params.container_cfg_ptr = curr_cont_cfg_ptr;
      cont_init_params.log_seq_id        = apm_info_ptr->graph_info.container_count;

      /** Call base container create API */
      if (AR_EOK != (result = cntr_cmn_create(&cont_init_params, &cont_handle_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CONT_PARSE: Failed to create container instance: CONT_ID[0x%lX]",
                curr_cont_cfg_ptr->container_id);

         return AR_EFAILED;
      }

      /** ALlocate memory for container node for APM Graph DB */
      if (NULL ==
          (container_node_ptr = (apm_container_t *)posal_memory_malloc(sizeof(apm_container_t), APM_INTERNAL_STATIC_HEAP_ID)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CONT_PARSE: Failed to allocate cont node mem CONT_ID[0x%lX]",
                curr_cont_cfg_ptr->container_id);

         return AR_ENOMEMORY;
      }

      /** Clear the allocated struct */
      memset(container_node_ptr, 0, sizeof(apm_container_t));

      /** Set the flag to indicate container is newly created */
      container_node_ptr->newly_created = TRUE;

      /** Store the container ID in the created node */
      container_node_ptr->container_id = curr_cont_cfg_ptr->container_id;

      /** Store the handle of container created */
      container_node_ptr->cont_hdl_ptr = cont_handle_ptr;

      /** Get container prop data payload size */
      prop_data_ptr = (curr_payload_ptr + sizeof(apm_container_cfg_t));

      /** Save the overall property payload size */
      container_node_ptr->prop_payload_size = prop_data_size;

      /** Store container properties. Also validates and flag any
       *  errors.
       *  container properties for the same ID are not expected to
       *  change across multiple GRAPH OPEN Commands.
       */
      if (AR_EOK != (result = apm_cfg_container_prop(container_node_ptr, prop_data_ptr, curr_cont_cfg_ptr->num_prop)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "SG_PARSE: Failed to parse cont prop list, CNTR_ID: [0x%lX]",
                container_node_ptr->container_id);

         return result;
      }

      /** After all successful validations update the number of
       *  sub-graph properties configured */
      container_node_ptr->prop.num_properties = curr_cont_cfg_ptr->num_prop;

      /** Advance the payload pointer to point to next container config object */
      curr_payload_ptr += (sizeof(apm_container_cfg_t) + prop_data_size);

      /** Get the pointer to current container command control
       *  pointer */
      apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_info_ptr->curr_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

      /** Store the container object pointer to be sent to containers */
      cont_cmd_ctrl_ptr->cached_cfg_params.graph_open_params.container_prop_ptr = curr_cont_cfg_ptr;

      /** Add this container node to the list of containers to
       *  which the message is pending to be sent  */
      apm_add_cont_to_pending_msg_send_list(apm_info_ptr->curr_cmd_ctrl_ptr, container_node_ptr, cont_cmd_ctrl_ptr);

      /** Add the container to the APM graph data base.  This call
       *  also increments the number of containers present in the
       *  data base  */
      if (AR_EOK != (result = apm_db_add_obj_to_list(apm_info_ptr->graph_info.container_list_ptr,
                                                     container_node_ptr,
                                                     container_node_ptr->container_id,
                                                     APM_OBJ_TYPE_CONTAINER,
                                                     &apm_info_ptr->graph_info.num_containers)))
      {
         return result;
      }

      apm_ext_utils_t *ext_utils_ptr = &apm_info_ptr->ext_utils;
      if ((NULL != ext_utils_ptr->db_query_vtbl_ptr) &&
          (NULL != ext_utils_ptr->db_query_vtbl_ptr->apm_db_query_add_cntr_to_list_fptr))
      {
         bool_t IS_OPEN_TRUE = TRUE;
         if (AR_EOK !=
             (result = ext_utils_ptr->db_query_vtbl_ptr->apm_db_query_add_cntr_to_list_fptr(apm_info_ptr,
                                                                                            (void *)container_node_ptr,
                                                                                            IS_OPEN_TRUE)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "CONT_PARSE: Failed to add cntr to db query CONT_ID[0x%lX]",
                   curr_cont_cfg_ptr->container_id);
            return result;
         }
      }

      /** Add the container to the list of standalone containers
       *  that need to be added to the graph */
      spf_list_insert_tail(&apm_info_ptr->graph_info.standalone_cont_list_ptr,
                           container_node_ptr,
                           APM_INTERNAL_STATIC_HEAP_ID,
                           TRUE /* use_pool*/);

      AR_MSG(DBG_MED_PRIO, "CONT_PARSE: Parsed CONT_ID[0x%lX]", curr_cont_cfg_ptr->container_id);

      /** Decrement the available payload size  */
      curr_payload_size -= (sizeof(apm_container_cfg_t) + prop_data_size);

      /** Decrement the container counter */
      container_cntr--;

      /** Validate remaining payload size */
      if (container_cntr && (curr_payload_size < (container_cntr * sizeof(apm_container_cfg_t))))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CONT_PARSE: Insuffient payload size rem[%lu], num rem cont[%lu]",
                curr_payload_size,
                container_cntr);

         return AR_EBADPARAM;
      }

   } /** End of while (container_cntr) */

   return result;
}

ar_result_t apm_parse_modules_list(apm_t *apm_info_ptr, uint8_t *mod_pid_payload_ptr, uint32_t payload_size)
{
   ar_result_t                  result          = AR_EOK;
   apm_module_t *               module_node_ptr = NULL;
   uint32_t                     num_modules;
   apm_module_cfg_t *           curr_mod_cfg_ptr;
   uint8_t *                    curr_payload_ptr;
   apm_container_t *            container_node_ptr = NULL;
   apm_sub_graph_t *            sub_graph_node_ptr = NULL;
   apm_param_id_modules_list_t *pid_data_ptr;
   uint8_t *                    curr_mod_list_payload_ptr;
   apm_modules_list_t *         module_list_ptr;
   uint32_t                     num_mod_list;
   apm_pspc_module_list_t *     mod_list_node_ptr = NULL;
   apm_cont_cached_cfg_t *      cont_cached_cfg_ptr;
   apm_cont_cmd_ctrl_t *        cont_cmd_ctrl_ptr;
   uint32_t                     expected_payload_size;
   uint32_t                     curr_payload_size;
   apm_cmd_ctrl_t *             apm_cmd_ctrl_ptr;
   bool_t                       mod_list_parse_req = TRUE;

   /** Validate the payload pointer */
   if (!mod_pid_payload_ptr || !payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "MOD_PARSE: PID payload pointer[0x%lX] and/or size[%lu] is NULL",
             mod_pid_payload_ptr,
             payload_size);

      return AR_EFAILED;
   }

   /* Get the pointer to current APM command control obj */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to sub-graph config payload start */
   pid_data_ptr = (apm_param_id_modules_list_t *)mod_pid_payload_ptr;

   /** Validate the number of module lists being configured */
   if (!pid_data_ptr->num_modules_list)
   {
      AR_MSG(DBG_ERROR_PRIO, "MOD_PARSE: Num module list is 0");

      return AR_EBADPARAM;
   }

   /** Get the number of modules list */
   num_mod_list = pid_data_ptr->num_modules_list;

   /** Compute expected min payload size */
   expected_payload_size = sizeof(apm_param_id_modules_list_t) + (num_mod_list * sizeof(apm_modules_list_t));

   /** Validate provided payload size */
   if (payload_size < expected_payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "MOD_PARSE: Insufficient payload size[%lu bytes], expected[%lu bytes], num_module_lists[%lu]",
             payload_size,
             expected_payload_size,
             num_mod_list);

      return AR_EBADPARAM;
   }

   AR_MSG(DBG_MED_PRIO, "MOD_PARSE: num_module_lists[%lu], payload_size[%lu bytes]", num_mod_list, payload_size);

   /** Get the pointer to start of the array of module-list
    *  objects */
   curr_mod_list_payload_ptr = mod_pid_payload_ptr + sizeof(apm_param_id_modules_list_t);

   /** Init current payload size */
   curr_payload_size = (payload_size - sizeof(apm_param_id_modules_list_t));

   /** Iterate over the array of module list */
   for (uint32_t idx = 0; idx < pid_data_ptr->num_modules_list; idx++)
   {
      module_list_ptr = (apm_modules_list_t *)curr_mod_list_payload_ptr;

      /** Check if the given sub-graph and container ID have been
       *  configured at least once */

      /**
       * If the module's SG id is present in the skip_sg_id_list_ptr then skip entire current module list
       * and increment to next module list, in multi-client context
       */
      if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
          apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_parse_mod_list_fptr)
      {
         apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_parse_mod_list_fptr(apm_cmd_ctrl_ptr,
                                                                                             module_list_ptr,
                                                                                             &curr_mod_list_payload_ptr,
                                                                                             &mod_list_parse_req);
         if (!mod_list_parse_req)
         {
            continue;
         }
      }

      if (AR_EOK != (result = apm_db_get_sub_graph_node(&apm_info_ptr->graph_info,
                                                        module_list_ptr->sub_graph_id,
                                                        &sub_graph_node_ptr,
                                                        APM_DB_OBJ_QUERY)))
      {
         AR_MSG(DBG_ERROR_PRIO, "MOD_PARSE: SG_ID[0x%lX] is not configured", module_list_ptr->sub_graph_id);

         return AR_EFAILED;
      }
	  if (!sub_graph_node_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "MOD_PARSE: SG_ID[0x%lX] is not configured, sg_node_ptr is NULL", module_list_ptr->sub_graph_id);

         return AR_EFAILED;
      }
      /* for a container in satellite PD, this function would return its parent container node in the master PD */
      if (AR_EOK != (result = apm_db_get_container_node(&apm_info_ptr->graph_info,
                                                        module_list_ptr->container_id,
                                                        &container_node_ptr,
                                                        APM_DB_OBJ_QUERY)))
      {
         return AR_EFAILED;
      }

      /** If either of the sub-graph or container with given ID is
       *  not present, bail out  */
      if (!container_node_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "MOD_PARSE: CONT_ID[0x%lX] is not configured", module_list_ptr->container_id);

         return AR_EFAILED;
      }

      /** Check if the list of modules per-sub-graph_container exists.
       *  The container ID has to be taken from the container  node,
       *  as module in satellite PD associated with a container in Satellite PD
       *  would still get added to parent(offload) container in the Master APM context.
       */
      if (AR_EOK != (result = apm_db_get_module_list_node(container_node_ptr->pspc_module_list_node_ptr,
                                                          sub_graph_node_ptr->sub_graph_id,
                                                          container_node_ptr->container_id,
                                                          &mod_list_node_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO, "MOD_PARSE: Failed to get module list per sg per cont node");

         return AR_EFAILED;
      }

      /** If the per sg per cont mod list node does not exist,
       *  create one now */
      if (!mod_list_node_ptr)
      {
         mod_list_node_ptr =
            (apm_pspc_module_list_t *)posal_memory_malloc(sizeof(apm_pspc_module_list_t), APM_INTERNAL_STATIC_HEAP_ID);

         if (!mod_list_node_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "Failed to alloce memory for per sg per cont module list");

            return AR_ENOMEMORY;
         }

         /** Clear the allocated structure */
         memset(mod_list_node_ptr, 0, sizeof(apm_pspc_module_list_t));

         /** Init the allocated node */
         mod_list_node_ptr->sub_graph_id = module_list_ptr->sub_graph_id;
         /* The container ID is populated from the container node, as the
          * offloaded module is added to the parent container ID in master APM context
          */
         mod_list_node_ptr->container_id = container_node_ptr->container_id;

         /** Add the module list node to host container's module list */
         if (AR_EOK != (result = apm_db_add_node_to_list(&container_node_ptr->pspc_module_list_node_ptr,
                                                         mod_list_node_ptr,
                                                         &container_node_ptr->num_pspc_module_lists)))
         {
            return result;
         }
      }

      /** Get the pointer to current container command control obj
       *  pointer */
      apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_info_ptr->curr_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

      /** Get the number of modules in the list */
      num_modules = module_list_ptr->num_modules;

      AR_MSG(DBG_MED_PRIO,
             "MOD_PARSE: SG_ID[0x%lX], CONT_ID[0x%lX], num_modules[%lu]",
             module_list_ptr->sub_graph_id,
             module_list_ptr->container_id,
             num_modules);

      /** Get the pointer to start of module object list */
      curr_payload_ptr = ((uint8_t *)module_list_ptr + sizeof(apm_modules_list_t));

      /** Iterate over the list of module cfg objects */
      for (uint32_t idx = num_modules; idx > 0; idx--)
      {
         /** Get the pointer to current module cfg object */
         curr_mod_cfg_ptr = (apm_module_cfg_t *)curr_payload_ptr;

         /** Check if the module ID already exists
          *  in the graph data base. If it exists, its an error
          *  condition and parsing need to be aborted */
         if (AR_EOK != (result = apm_db_get_module_node(&apm_info_ptr->graph_info,
                                                        curr_mod_cfg_ptr->instance_id,
                                                        &module_node_ptr,
                                                        APM_DB_OBJ_CREATE_REQD)))
         {
            return AR_EFAILED;
         }

         /** Check if the module already exists.  */
         if (module_node_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "MOD_PARSE: Module: [MID, IID]:[0x%lx, %lu], already exists",
                   curr_mod_cfg_ptr->module_id,
                   curr_mod_cfg_ptr->instance_id);

            return AR_EBADPARAM;
         }

         /** If the module does not exist in APM DB  */

         /** ALlocate memory for module node */
         if (NULL == (module_node_ptr = (apm_module_t *)posal_memory_malloc(sizeof(apm_module_t), APM_INTERNAL_STATIC_HEAP_ID)))
         {
            AR_MSG(DBG_ERROR_PRIO, "MOD_PARSE: Failed to allocate module node mem");
            return AR_ENOMEMORY;
         }

         /** Clear the allocated struct */
         memset(module_node_ptr, 0, sizeof(apm_module_t));

         /** Store the MID, IID */
         module_node_ptr->module_id   = curr_mod_cfg_ptr->module_id;
         module_node_ptr->instance_id = curr_mod_cfg_ptr->instance_id;

         /** Update this container's handle in the module node */
         module_node_ptr->host_cont_ptr = container_node_ptr;

         /** Update this sub-graphs's handle in the module node */
         module_node_ptr->host_sub_graph_ptr = sub_graph_node_ptr;

         /** Add this module node to per container per subgraph module
          *  list */
         if (AR_EOK != (result = apm_db_add_node_to_list(&mod_list_node_ptr->module_list_ptr,
                                                         module_node_ptr,
                                                         &mod_list_node_ptr->num_modules)))
         {
            return result;
         }

         /** Add this container node to the list of container pending
             send message  */
         apm_add_cont_to_pending_msg_send_list(apm_info_ptr->curr_cmd_ctrl_ptr, container_node_ptr, cont_cmd_ctrl_ptr);

         /** After all the validations are successful, add the module
             node to the APM graph data base. This call also increments
             the number of module counter */
         if (AR_EOK != (result = apm_db_add_obj_to_list(apm_info_ptr->graph_info.module_list_ptr,
                                                        module_node_ptr,
                                                        curr_mod_cfg_ptr->instance_id,
                                                        APM_OBJ_TYPE_MODULE,
                                                        &apm_info_ptr->graph_info.num_modules)))
         {
            return result;
         }

         /** Advance the current payload pointer to point to next module
          *  config object */
         curr_payload_ptr += sizeof(apm_module_cfg_t);

      } /** End of for (num_modules) */

      /** Get the pointer to container cached command configuration
       *  corresponding to current APM command */
      cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

      /** Add the module list cfg to running list of modules to be
       *  sent to container as part of GRAPH OPEN */
      if (AR_EOK != (result = apm_db_add_node_to_list(&cont_cached_cfg_ptr->graph_open_params.module_cfg_list_ptr,
                                                      module_list_ptr,
                                                      &cont_cached_cfg_ptr->graph_open_params.num_module_list_cfg)))
      {
         return result;
      }
      /** Add the host container to this sub-graph ID's container
       *  list */
      if (AR_EOK != (result = apm_db_search_and_add_node_to_list(&sub_graph_node_ptr->container_list_ptr,
                                                                 container_node_ptr,
                                                                 &sub_graph_node_ptr->num_containers)))
      {
         return result;
      }

      /** Add this sub-graph ID to the host container's sub_graph_id
       *  list */
      if (AR_EOK != (result = apm_db_search_and_add_node_to_list(&container_node_ptr->sub_graph_list_ptr,
                                                                 sub_graph_node_ptr,
                                                                 &container_node_ptr->num_sub_graphs)))
      {
         return result;
      }

      /** Check if this container is already part of a sorted
       *  graph, then add the new sub-graph ID to the list of
       *  sub-graphs part of the container graph */
      if (container_node_ptr->cont_graph_ptr)
      {
         if (AR_EOK !=
             (result = apm_db_search_and_add_node_to_list(&container_node_ptr->cont_graph_ptr->sub_graph_list_ptr,
                                                          sub_graph_node_ptr,
                                                          &container_node_ptr->cont_graph_ptr->num_sub_graphs)))
         {
            return result;
         }
      }

      /** Add the sub-graph node client configuration to running list
       *  of sub-graphs to be sent to container as part of GRAPH
       *  OPEN */
      if (AR_EOK !=
          (result = apm_db_search_and_add_node_to_list(&cont_cached_cfg_ptr->graph_open_params.sub_graph_cfg_list_ptr,
                                                       sub_graph_node_ptr,
                                                       &cont_cached_cfg_ptr->graph_open_params.num_sub_graphs)))
      {
         return result;
      }

#ifdef APM_DEBUG_CMD_PARSING
      AR_MSG(DBG_MED_PRIO,
             "MOD_PARSE: Parsed module_id[0x%lX], module_iid[0x%lX]",
             module_node_ptr->module_id,
             module_node_ptr->instance_id);
#endif

      /** Get the pointer to next module list  */
      curr_mod_list_payload_ptr +=
         (sizeof(apm_modules_list_t) + (module_list_ptr->num_modules * sizeof(apm_module_cfg_t)));

      /** Decrement number of module list */
      num_mod_list--;

      /** Decrement current payload size */
      curr_payload_size -= (sizeof(apm_modules_list_t) + (module_list_ptr->num_modules * sizeof(apm_module_cfg_t)));

      /** Validate remaining payload size */
      if (num_mod_list && (curr_payload_size < (num_mod_list * sizeof(apm_modules_list_t))))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "MOD_PARSE: Insuffient payload size rem[%lu], num rem mod_list[%lu]",
                curr_payload_size,
                num_mod_list);

         return AR_EBADPARAM;
      }

   } /** End of for( num modules_list ) */

   AR_MSG(DBG_HIGH_PRIO, "MOD_PROP_PARSE: Done parsing modules list, result: 0x%lX", result);

   return result;
}

ar_result_t apm_parse_module_prop_list(apm_t *apm_info_ptr, uint8_t *mod_pid_payload_ptr, uint32_t payload_size)
{
   ar_result_t                 result          = AR_EOK;
   apm_module_t *              module_node_ptr = NULL;
   uint32_t                    num_mod_prop_cfg;
   apm_module_prop_cfg_t *     curr_mod_prop_cfg_ptr;
   uint8_t *                   curr_payload_ptr;
   uint32_t                    prop_data_size;
   uint8_t *                   prop_data_ptr;
   apm_param_id_module_prop_t *pid_data_ptr;
   spf_list_node_t **          list_pptr;
   uint32_t *                  num_list_nodes_ptr;
   apm_cont_cmd_ctrl_t *       cont_cmd_ctrl_ptr;
   uint32_t                    expected_payload_size;
   uint32_t                    curr_payload_size;
   apm_cmd_ctrl_t *            apm_cmd_ctrl_ptr;
   bool_t                      mod_prop_list_parse_req= TRUE;

   /** Validate the payload pointer */
   if (!mod_pid_payload_ptr || !payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "MOD_PROP_PARSE: PID payload pointer[0x%lX] and/or size[%lu] is NULL",
             mod_pid_payload_ptr,
             payload_size);

      return AR_EFAILED;
   }

   /* Get the pointer to current APM command control obj */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to module config payload start */
   pid_data_ptr = (apm_param_id_module_prop_t *)mod_pid_payload_ptr;

   /** Validate the number of sub-graphs being configured */
   if (!pid_data_ptr->num_module_prop_cfg)
   {
      AR_MSG(DBG_ERROR_PRIO, "MOD_PROP_PARSE: Num module prop obj num is 0");

      return AR_EBADPARAM;
   }

   /** Get the number of containers */
   num_mod_prop_cfg = pid_data_ptr->num_module_prop_cfg;

   /** Compute min required payload size */
   expected_payload_size = sizeof(apm_param_id_module_prop_t) + (num_mod_prop_cfg * sizeof(apm_module_prop_cfg_t));

   /** Validate provided payload size */
   if (payload_size < expected_payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "MOD_PROP_PARSE: Insufficient payload size[%lu bytes], min size[%lu bytes], num_mod_prop_cfg[%lu]",
             payload_size,
             expected_payload_size,
             num_mod_prop_cfg);

      return AR_EBADPARAM;
   }

   AR_MSG(DBG_MED_PRIO,
          "MOD_PROP_PARSE: Num module prop obj[%lu], payload_size[%lu bytes]",
          num_mod_prop_cfg,
          payload_size);

   /** Get the pointer to start of container object list */
   curr_payload_ptr = mod_pid_payload_ptr + sizeof(apm_param_id_module_prop_t);

   /** Init current payload size */
   curr_payload_size = (payload_size - sizeof(apm_param_id_module_prop_t));

   /** Iterate over the list of container objects */
   while (num_mod_prop_cfg)
   {
      /** Get the pointer to current module property config object */
      curr_mod_prop_cfg_ptr = (apm_module_prop_cfg_t *)curr_payload_ptr;

      /** Check if the module ID already exists
       *  in the graph data base.  */
      if (AR_EOK != (result = apm_db_get_module_node(&apm_info_ptr->graph_info,
                                                     curr_mod_prop_cfg_ptr->instance_id,
                                                     &module_node_ptr,
                                                     APM_DB_OBJ_QUERY)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "MOD_PROP_PARSE: Failed to get module node: [M_IID]:[%lu]",
                curr_mod_prop_cfg_ptr->instance_id);

         return AR_EFAILED;
      }

      /** Check if the module exists.  */
      if (!module_node_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "MOD_PROP_PARSE: Module: [M_IID]:[%lu], does not exist",
                curr_mod_prop_cfg_ptr->instance_id);

         return AR_EBADPARAM;
      }

      /** Get container prop data payload size */
      prop_data_ptr = (curr_payload_ptr + sizeof(apm_module_prop_cfg_t));

      /** Get the overall property data size for this module prop configuration */
      if (AR_EOK != (result = apm_get_prop_data_size(prop_data_ptr,
                                                     curr_mod_prop_cfg_ptr->num_props,
                                                     &prop_data_size,
                                                     apm_info_ptr->curr_cmd_ctrl_ptr->cmd_payload_size)))
      {
         return result;
      }

      /** Advance the pointer to start of next property config object */
      curr_payload_ptr += (sizeof(apm_module_prop_cfg_t) + prop_data_size);

      /**
       * If module host SG id is in skip list (which means same module prop is coming again) then skip module prop list
       * in multi-client context
       */
      if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
          apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_parse_mod_prop_list_fptr)
      {
         apm_info_ptr->ext_utils.multi_client_vtbl_ptr
            ->apm_multi_client_parse_mod_prop_list_fptr(apm_cmd_ctrl_ptr, module_node_ptr, &mod_prop_list_parse_req);

         if (!mod_prop_list_parse_req)
         {
            /** Decrement the module config counter */
            num_mod_prop_cfg--;
            continue;
         }
      }

      /** Get the pointer to current container command control
       *  pointer */
      apm_get_cont_cmd_ctrl_obj(module_node_ptr->host_cont_ptr, apm_info_ptr->curr_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

      /** Pointer to this container's running list of module
       *  property configuration to be sent to containers as part
       *  of GRAPH OPEN command */
      list_pptr = &cont_cmd_ctrl_ptr->cached_cfg_params.graph_open_params.module_prop_list_ptr;

      /** Total number of module properties configured for the host
       *  container */
      num_list_nodes_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params.graph_open_params.num_module_props;

      /** Add the property configuration structure to the running
       *  list of module properties to be sent to containers */
      if (AR_EOK != (result = apm_db_add_node_to_list(list_pptr, curr_mod_prop_cfg_ptr, num_list_nodes_ptr)))
      {
         return result;
      }

      /** Decerement the module config counter */
      num_mod_prop_cfg--;

      /** Decrement the available payload size */
      curr_payload_size -= (sizeof(apm_module_prop_cfg_t) + prop_data_size);

      /** Validate remaining payload size */
      if (num_mod_prop_cfg && (curr_payload_size < (num_mod_prop_cfg * sizeof(apm_module_prop_cfg_t))))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "MOD_PROP_PARSE: Insuffient payload size rem[%lu], num rem mod prop[%lu]",
                curr_payload_size,
                num_mod_prop_cfg);

         return AR_EBADPARAM;
      }

   } /** End of while (mod_prop_cfg_ctr) */

   AR_MSG(DBG_HIGH_PRIO, "MOD_PROP_PARSE: Done parsing module prop list, result: 0x%lX", result);

   return result;
}

ar_result_t apm_parse_module_conn_list(apm_t *apm_info_ptr, uint8_t *mod_pid_payload_ptr, uint32_t payload_size)
{
   ar_result_t                 result                  = AR_EOK;
   apm_module_t *              module_node_ptr_list[2] = { NULL };
   uint32_t                    num_connections;
   apm_module_conn_cfg_t *     curr_conn_cfg_ptr;
   uint8_t *                   curr_payload_ptr;
   apm_param_id_module_conn_t *pid_data_ptr;
   spf_list_node_t **          cont_cached_port_conn_list_pptr;
   uint32_t *                  num_list_nodes_ptr;
   uint32_t                    num_cont_updated = 1;
   apm_module_t *              curr_module_node_ptr;
   apm_container_t *           container_node_ptr;
   apm_container_t *           src_cont_node_ptr;
   apm_container_t *           dst_cont_node_ptr;
   apm_cont_cmd_ctrl_t *       cont_cmd_ctrl_ptr;
   bool_t                      node_added = FALSE;
   uint32_t                    cmd_opcode;
   uint32_t                    data_link_port_id_list[2];
   apm_graph_mgmt_cmd_ctrl_t * gm_cmd_ctrl_ptr;
   bool_t                      DANGLING_LINK_ALLOWED = TRUE;
   uint32_t                    expected_payload_size;
   apm_ext_utils_t *           ext_utils_ptr;
   uint32_t                    peer_heap_id_list[2]       = { 0 };
   bool_t                      is_mixed_heap_data_link    = FALSE;
   bool_t                      DATA_LINK_TRUE             = TRUE;
   bool_t                      link_start_reqd            = FALSE;
   spf_module_port_type_t      port_type;
   bool_t                      conn_parse_req = TRUE;
   bool_t                      conn_skip_req  = FALSE;

   enum
   {
      SRC_MODULE  = 0,
      DSTN_MODULE = 1
   };

   /** Validate the payload pointer */
   if (!mod_pid_payload_ptr || !payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "MOD_DATA_LNK_PARSE: PID payload pointer[0x%lX] and/or size[%lu] is NULL",
             mod_pid_payload_ptr,
             payload_size);

      return AR_EFAILED;
   }

   /** Get the current command opcode being processed */
   cmd_opcode = apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode;

   /** Get the pointer to ext utils vtbl  */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Validate if this function is called only for Graph OPEN
    *  or CLOSE, else return failure */
   if ((APM_CMD_GRAPH_OPEN != cmd_opcode) && (APM_CMD_GRAPH_CLOSE != cmd_opcode))
   {
      AR_MSG(DBG_ERROR_PRIO, "MOD_DATA_LNK_PARSE: Unexpected command opcode, cmd_opcode[0x%lX]", cmd_opcode);

      return AR_EFAILED;
   }

   /** Get the pointer to module config payload start */
   pid_data_ptr = (apm_param_id_module_conn_t *)mod_pid_payload_ptr;

   /** Validate the number of connections being configured */
   if (!pid_data_ptr->num_connections)
   {
      AR_MSG(DBG_HIGH_PRIO, "MOD_DATA_LNK_PARSE: WARNING: Num connections is 0");

      return AR_EOK;
   }

   /** Get the number of connections */
   num_connections = pid_data_ptr->num_connections;

   /** Compute expected payload size */
   expected_payload_size = sizeof(apm_param_id_module_conn_t) + (num_connections * sizeof(apm_module_conn_cfg_t));

   /** Validate provided payload size */
   if (payload_size < expected_payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "MOD_DATA_LNK_PARSE: Insufficient payload size[%lu bytes], expected[%lu bytes], cmd_opcode[0x%lX]",
             payload_size,
             expected_payload_size,
             cmd_opcode);

      return AR_EBADPARAM;
   }

   AR_MSG(DBG_MED_PRIO,
          "MOD_DATA_LNK_PARSE: Processing cmd_opcode[0x%lX], num_data_links[%lu], payload_size[%lu bytes]",
          cmd_opcode,
          num_connections,
          payload_size);

   /** Get the pointer to start of module port connection list */
   curr_payload_ptr = (mod_pid_payload_ptr + sizeof(apm_param_id_module_conn_t));

   /** Iterate over the list of container objects */
   while (num_connections)
   {
      /** Init the number of updated containers assuming that source
       *  and destination modules belong to same containers. This
       *  count is changed if these belong to different containers. */
      num_cont_updated = 1;

      /** Get the pointer to current connection cfg object */
      curr_conn_cfg_ptr = (apm_module_conn_cfg_t *)curr_payload_ptr;

      /* Reset non-default heap peer cntr index to Invalid*/
      is_mixed_heap_data_link    = FALSE;

      /** Validate the module instance pair if they exist */
      if (AR_EOK != (result = apm_validate_module_instance_pair(&apm_info_ptr->graph_info,
                                                                curr_conn_cfg_ptr->src_mod_inst_id,
                                                                curr_conn_cfg_ptr->dst_mod_inst_id,
                                                                module_node_ptr_list,
                                                                DANGLING_LINK_ALLOWED)))
      {
         AR_MSG(DBG_ERROR_PRIO, "MOD_DATA_LNK_PARSE: Module IID validation failed");

         return result;
      }

      if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
          apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_parse_mod_conn_list_fptr)
      {
         /**Get the connection from db and update client context*/
         if (AR_EOK != (result = apm_info_ptr->ext_utils.multi_client_vtbl_ptr
                                    ->apm_multi_client_parse_mod_conn_list_fptr(apm_info_ptr,
                                                                                curr_conn_cfg_ptr,
                                                                                &curr_payload_ptr,
                                                                                &num_connections,
                                                                                &conn_parse_req)))
         {
            return result;
         }

         /*same connections is ref counted for new client. So continue
          * parsing new connection */
         if (!conn_parse_req)
         {
            continue;
         }
      }

      if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
          apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_cache_connections_fptr)
      {
         /**Get the cache the connection & add the 1st client*/
         if (AR_EOK != (result = apm_info_ptr->ext_utils.multi_client_vtbl_ptr
                                    ->apm_multi_client_cache_connections_fptr(apm_info_ptr,
                                                                              module_node_ptr_list,
                                                                              curr_conn_cfg_ptr,
                                                                              &conn_skip_req)))
         {
            return result;
         }

         if (conn_skip_req)
         {
            /** Advance the pointer to start of next connection object  */
            curr_payload_ptr += (sizeof(apm_module_conn_cfg_t));

            /** Decrement the num connection counter */
            num_connections--;
            continue;
         }
      }

      /** If either of the source or destination module instance
       *  has not been opened yet, skip parsing this link and move
       *  on to next */
      if (!module_node_ptr_list[SRC_MODULE] || !module_node_ptr_list[DSTN_MODULE])
      {
         goto _continue_data_link_payload_parse;
      }

      /** For GRAPH OPEN command, if the host sub-graphs for source
       *  and destination modules in the data link have already
       *  been started, then cache this control link for sending
       *  prepare and start command for port handles/link to the
       *  corresponding host containers */
      if (APM_CMD_GRAPH_OPEN == cmd_opcode)
      {
         if (ext_utils_ptr->runtime_link_hdlr_vtbl_ptr &&
             ext_utils_ptr->runtime_link_hdlr_vtbl_ptr->apm_check_and_cache_link_to_start_fptr)
         {
            if (AR_EOK != (result = ext_utils_ptr->runtime_link_hdlr_vtbl_ptr
                                       ->apm_check_and_cache_link_to_start_fptr(apm_info_ptr,
                                                                                module_node_ptr_list,
                                                                                curr_conn_cfg_ptr,
                                                                                LINK_TYPE_DATA,
                                                                                &link_start_reqd)))
            {
               AR_MSG(DBG_ERROR_PRIO, "MOD_DATA_LNK_PARSE: Failed to cache opened data link for start");

               return result;
            }
         }

         /** Cache this data link in both the source and destination module's context */
         if (ext_utils_ptr->data_path_vtbl_ptr &&
             ext_utils_ptr->data_path_vtbl_ptr->apm_graph_open_cmd_cache_data_link_fptr)
         {
            if (AR_EOK !=
                (result =
                    ext_utils_ptr->data_path_vtbl_ptr->apm_graph_open_cmd_cache_data_link_fptr(curr_conn_cfg_ptr,
                                                                                               module_node_ptr_list)))
            {
               AR_MSG(DBG_ERROR_PRIO, "MOD_DATA_LNK_PARSE: Failed to store the data link info");

               return result;
            }
         }

      } /** End of if (APM_CMD_GRAPH_OPEN == cmd_opcode) */

      /** If the current command is GRAPH CLOSE, then data links
       *  are expected to be closed only across different sub-graphs.
       *  If the links within the same sub-graphs are being closed,
       *  then print warning and continue parsing. */
      if (APM_CMD_GRAPH_CLOSE == cmd_opcode)
      {
         /** Get the pointer to graph management command control
          *  object */
         gm_cmd_ctrl_ptr = &apm_info_ptr->curr_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl;

         /** Check if the link is in valid state to be closed. This call
          *  also  adds the host sub-graphs for the modules part of the
          *  link in the  list of sub-graphs to be processed as part of
          *  the close command */
         if (AR_EOK !=
             (result = apm_validate_and_cache_link_for_state_mgmt(apm_info_ptr, gm_cmd_ctrl_ptr, module_node_ptr_list)))
         {
            /** If the link being closed is within same sub-graph, it needs
             *  to be skipped. For any other errors need to return
             *  failure */
            if (AR_ECONTINUE == result)
            {
               goto _continue_data_link_payload_parse;
            }
            else /** Any other errors */
            {
               return result;
            }

         } /** End of if (link validation) */

      } /** End of if (close cmd) */

      /** Populate a temporary list of data link port ID's. Index
       *  = 0 is for source module OP port ID, Index = 1 for dstn
       *  module IP port ID */
      data_link_port_id_list[SRC_MODULE]  = curr_conn_cfg_ptr->src_mod_op_port_id;
      data_link_port_id_list[DSTN_MODULE] = curr_conn_cfg_ptr->dst_mod_ip_port_id;

      /** If source and destination module belongs to different
       *  container then need to send connect message to both the
       *  containers. Increment the updated container count */
      if ((module_node_ptr_list[SRC_MODULE]->host_cont_ptr->container_id) !=
          (module_node_ptr_list[DSTN_MODULE]->host_cont_ptr->container_id))
      {
         /*check if the heap property is different. If so, add to list of mixed heap links*/
         if (ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr &&
             ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr->apm_check_and_handle_mixed_heap_id_cntr_links_fptr)
         {
            ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr
               ->apm_check_and_handle_mixed_heap_id_cntr_links_fptr(module_node_ptr_list,
                                                                    peer_heap_id_list,
                                                                    &is_mixed_heap_data_link,
                                                                    DATA_LINK_TRUE);
         }

         num_cont_updated = 2; /** Source and destination host containers */

         /** Current command is graph open, then update the peer
          *  container info for each the src and dstn containers */
         if (APM_CMD_GRAPH_OPEN == cmd_opcode)
         {
            src_cont_node_ptr = module_node_ptr_list[SRC_MODULE]->host_cont_ptr;
            dst_cont_node_ptr = module_node_ptr_list[DSTN_MODULE]->host_cont_ptr;

            /** Add the host container for destination module in the
             *  downstream container list of host container of source
             *  module. If there are multiple connections are across 2
             *  containers, then for the purpose of container graph
             *  sorting, they are treated as single connection. Function
             *  below skips adding the container node if it is already
             *  present in such case. */
            spf_list_search_and_add_obj(&src_cont_node_ptr->peer_list.downstream_cont_list_ptr,
                                        dst_cont_node_ptr,
                                        &node_added,
                                        APM_INTERNAL_STATIC_HEAP_ID,
                                        TRUE /*use_pool*/);

            if (node_added)
            {
               src_cont_node_ptr->peer_list.num_downstream_cont++;
            }

            /** Add the host container for source module in the upstream
             *  container list of host container of destination module */

            spf_list_search_and_add_obj(&dst_cont_node_ptr->peer_list.upstream_cont_list_ptr,
                                        src_cont_node_ptr,
                                        &node_added,
                                        APM_INTERNAL_STATIC_HEAP_ID,
                                        TRUE /*use_pool*/);

            if (node_added)
            {
               dst_cont_node_ptr->peer_list.num_upstream_cont++;
            }

            /** Update container graph with source and destination
             *  containers */
            apm_update_cont_graph(&apm_info_ptr->graph_info,
                                  src_cont_node_ptr,
                                  dst_cont_node_ptr,
                                  FALSE /** Re-sort graphs if updated*/);

         } /** if (APM_CMD_GRAPH_OPEN == cmd_opcode)*/

      } /** End of if(src_mod(host_cont) != dstn_mod(host_cont) */

      /** Identify the impacted containers and add them to pending
       *  list for sending message */
      for (uint32_t idx = 0; idx < num_cont_updated; idx++)
      {
         curr_module_node_ptr = module_node_ptr_list[idx];

         /** Get the host container handle */
         container_node_ptr = curr_module_node_ptr->host_cont_ptr;

         /** Get the pointer to current container command control
          *  pointer */
         apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_info_ptr->curr_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

         if (APM_CMD_GRAPH_OPEN == cmd_opcode)
         {
            /** Pointer to this container's running list of module
             *  connection configuration to be sent to containers as part
             *  of GRAPH OPEN command */
            cont_cached_port_conn_list_pptr =
               &cont_cmd_ctrl_ptr->cached_cfg_params.graph_open_params.mod_data_link_cfg_list_ptr;

            /** Total number of module connections configured for the host
             *  container */
            num_list_nodes_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params.graph_open_params.num_data_links;

            /** Add the config structure to the running list of module
             *  connections to be sent to containers */
            if (AR_EOK !=
                (result =
                    apm_db_add_node_to_list(cont_cached_port_conn_list_pptr, curr_conn_cfg_ptr, num_list_nodes_ptr)))
            {
               return result;
            }

            /*If current idx points to the container connected to the non-default heap ID peer, then cache this
             * link/heap information to be sent to the container as part of the OPEN message*/
            if (is_mixed_heap_data_link)
            {
               if (ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr &&
                   ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr->apm_cache_mxd_heap_id_cntr_link_in_db_fptr)
               {
                  if (AR_EOK !=
                      (result = ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr
                                   ->apm_cache_mxd_heap_id_cntr_link_in_db_fptr(cont_cmd_ctrl_ptr,
                                                                                (void *)curr_conn_cfg_ptr,
                                                                                peer_heap_id_list[idx],
                                                                                DATA_LINK_TRUE)))
                  {
                     return result;
                  }
               }
            }

            /** Add this container to pending message send list */
            apm_add_cont_to_pending_msg_send_list(apm_info_ptr->curr_cmd_ctrl_ptr,
                                                  container_node_ptr,
                                                  cont_cmd_ctrl_ptr);
         }
         else /** if (APM_CMD_GRAPH_CLOSE == cmd_opcode ) */
         {
            /** If the data link is present within the container, there
             *  will be no port handles. Need to send the link as is to the
             *  container for closing. The link information is not stored
             *  in the container cached config but rather as command
             *  params because as per the sub-graph state for the host
             *  module, they might not be processed in the first
             *  iteration of the close command. */
            if ((module_node_ptr_list[SRC_MODULE]->host_cont_ptr->container_id) ==
                (module_node_ptr_list[DSTN_MODULE]->host_cont_ptr->container_id))
            {
               /** Pointer to this container's running list of module
                *  connection configuration to be sent to containers as part
                *  of GRAPH CLOSE command */
               cont_cached_port_conn_list_pptr =
                  &cont_cmd_ctrl_ptr->cmd_params.link_close_params.mod_data_link_cfg_list_ptr;

               /** Total number of module connections configured for the host
                *  container */
               num_list_nodes_ptr = &cont_cmd_ctrl_ptr->cmd_params.link_close_params.num_data_links;

               /** Add the config structure to the running list of module
                *  connections to be sent to containers */
               if (AR_EOK !=
                   (result =
                       apm_db_add_node_to_list(cont_cached_port_conn_list_pptr, curr_conn_cfg_ptr, num_list_nodes_ptr)))
               {
                  return result;
               }
            }
            else /** Link is across 2 containers */
            {
               /** For data links across 2 containers, cache the port
                *  handles to be sent to the containers  */
               if (SRC_MODULE == idx)
               {
                  port_type = PORT_TYPE_DATA_OP;
               }
               else if (DSTN_MODULE == idx)
               {
                  port_type = PORT_TYPE_DATA_IP;
               }

               if (AR_EOK != (result = apm_search_and_cache_cont_port_hdl(container_node_ptr,
                                                                          cont_cmd_ctrl_ptr,
                                                                          module_node_ptr_list[idx]->instance_id,
                                                                          data_link_port_id_list[idx],
                                                                          port_type,
                                                                          cmd_opcode)))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "MOD_DATA_LNK_PARSE: Failed to cache port handle for "
                         "CONT_ID[0x%lX]"
                         "M_IID[0x%lX], port_id[0x%lx]",
                         container_node_ptr->container_id,
                         module_node_ptr_list[idx]->instance_id,
                         data_link_port_id_list[idx]);

                  return result;
               }

            } /** End of if-else (link across 2 containers) */

         } /** End of if-else (APM_CMD_GRAPH_CLOSE == cmd_opcode ) */

      } /** End of for loop */

   _continue_data_link_payload_parse:

      /** Decrement the num connection counter */
      num_connections--;

      /** Advance the pointer to start of next connection object  */
      curr_payload_ptr += (sizeof(apm_module_conn_cfg_t));

#ifdef APM_DEBUG_CMD_PARSING
      AR_MSG(DBG_MED_PRIO,
             "MOD_DATA_LNK_PARSE: Parsed data link src_module_iid[0x%lX], src_op_port_id[0x%lX], "
             "dst_module_iid[0x%lX], dst_ip_port_id[0x%lX]",
             curr_conn_cfg_ptr->src_mod_inst_id,
             curr_conn_cfg_ptr->src_mod_op_port_id,
             curr_conn_cfg_ptr->dst_mod_inst_id,
             curr_conn_cfg_ptr->dst_mod_ip_port_id);
#endif

   } /** End of while (num_connections) */

   AR_MSG(DBG_HIGH_PRIO,
          "MOD_DATA_LNK_PARSE: Done parsing data link list, cmd_opcode[0x%8lX], result: 0x%lX",
          cmd_opcode,
          result);

   return result;
}

ar_result_t apm_parse_module_ctrl_link_cfg_list(apm_t *  apm_info_ptr,
                                                uint8_t *mod_pid_payload_ptr,
                                                uint32_t payload_size)
{
   ar_result_t                          result                  = AR_EOK;
   apm_module_t *                       module_node_ptr_list[2] = { NULL };
   uint32_t                             num_ctrl_links;
   apm_module_ctrl_link_cfg_t *         curr_ctrl_link_cfg_ptr;
   uint8_t *                            curr_payload_ptr;
   apm_param_id_module_ctrl_link_cfg_t *pid_data_ptr;
   spf_list_node_t **                   list_pptr;
   uint32_t *                           num_list_nodes_ptr;
   uint32_t                             num_cont_updated = 1;
   apm_module_t *                       curr_module_node_ptr;
   apm_container_t *                    container_node_ptr;
   apm_cont_cmd_ctrl_t *                cont_cmd_ctrl_ptr;
   uint8_t *                            prop_data_ptr;
   uint32_t                             prop_data_size;
   uint32_t                             cmd_opcode;
   spf_module_port_conn_t *             port_conn_info_ptr;
   uint32_t                             ctrl_link_port_id_list[2] = { 0 };
   apm_graph_mgmt_cmd_ctrl_t *          gm_cmd_ctrl_ptr;
   uint32_t                             peer_domain_id[2] = { 0 };
   bool_t                               is_inter_proc_link, is_sat_inter_proc_imcl;
   bool_t                               DANGLING_LINK_ALLOWED = TRUE;
   uint32_t                             expected_payload_size;
   uint32_t                             curr_payload_size;
   apm_ext_utils_t *                    ext_utils_ptr;
   apm_imcl_peer_domain_info_t          remote_peer_info_obj = { 0 };
   uint32_t                             local_peer_idx = 0, local_peer_domain_id = 0;
   uint32_t                             peer_heap_id_list[2]       = { 0 };
   bool_t                               is_mixed_heap_ctrl_link    = FALSE;
   bool_t                               CTRL_LINK_TRUE             = FALSE;
   bool_t                               link_start_reqd            = FALSE;
   bool_t                               conn_parse_req             = TRUE;
   enum
   {
      PEER_1_MODULE = 0,
      PEER_2_MODULE = 1
   };

   /** Validate the payload pointer */
   if (!mod_pid_payload_ptr || !payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "MOD_CTRL_LNK_PARSE: PID payload pointer[0x%lX] and/or size[%lu] is NULL",
             mod_pid_payload_ptr,
             payload_size);

      return AR_EFAILED;
   }

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Get the pointer to current APM command control obj */
   apm_cmd_ctrl_t *apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to graph management command control
    *  object */
   gm_cmd_ctrl_ptr = &apm_info_ptr->curr_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl;

   /** Get the pointer to imcl peer config payload
    *  start and cache in the cmd ctrl */
   uint8_t *sat_prv_param_ptr = apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.sat_prv_cfg_ptr;

   /** Get the local proc domain ID   */
   __gpr_cmd_get_host_domain_id(&local_peer_domain_id);

   /** Get the opcode for current command being executed */
   cmd_opcode = apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode;

   /** Validate if this function is called only for Graph OPEN
    *  or CLOSE, else return failure */
   if ((APM_CMD_GRAPH_OPEN != cmd_opcode) && (APM_CMD_GRAPH_CLOSE != cmd_opcode))
   {
      AR_MSG(DBG_ERROR_PRIO, "MOD_CTRL_LNK_PARSE: Unexpected command opcode, cmd_opcode[0x%lX]", cmd_opcode);

      return AR_EFAILED;
   }

   /** Get the pointer to module control link config payload
    *  start */
   pid_data_ptr = (apm_param_id_module_ctrl_link_cfg_t *)mod_pid_payload_ptr;

   /** Validate the number of connections being configured */
   if (!pid_data_ptr->num_ctrl_link_cfg)
   {
      AR_MSG(DBG_HIGH_PRIO, "MOD_CTRL_LNK_PARSE: WARNING: Num control link is 0");

      return AR_EOK;
   }

   /** Get the number of control links */
   num_ctrl_links = pid_data_ptr->num_ctrl_link_cfg;

   /** Compute min payload size */
   expected_payload_size =
      sizeof(apm_param_id_module_ctrl_link_cfg_t) + (num_ctrl_links * sizeof(apm_module_ctrl_link_cfg_t));

   /** Validate provided payload size */
   if (payload_size < expected_payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "MOD_CTRL_LNK_PARSE: Insufficient payload size[%lu bytes], min_size[%lu bytes], cmd_opcode[0x%lX]",
             payload_size,
             expected_payload_size,
             cmd_opcode);

      return AR_EBADPARAM;
   }

   AR_MSG(DBG_MED_PRIO,
          "MOD_CTRL_LNK_PARSE: Processing cmd_opcode[0x%lX], num_ctrl_links[%lu], payload_size[%lu bytes]",
          cmd_opcode,
          num_ctrl_links,
          payload_size);

   /** Get the pointer to start of module control link config
    *  list  */
   curr_payload_ptr = (mod_pid_payload_ptr + sizeof(apm_param_id_module_ctrl_link_cfg_t));

   /** Init current payload size for control link config obj array
    *  length */
   curr_payload_size = (payload_size - sizeof(apm_param_id_module_ctrl_link_cfg_t));

   /** Iterate over the list of container objects */
   while (num_ctrl_links)
   {
      is_inter_proc_link     = FALSE;
      is_sat_inter_proc_imcl = FALSE;

      /** Init the number of updated containers assuming that source
       *  and destination modules belong to same containers. This
       *  count is changed if these belong to different containers. */
      num_cont_updated = 1;

      /* Reset mixed ctrl link flag */
      is_mixed_heap_ctrl_link = FALSE;

      /** Get the pointer to current control link cfg object */
      curr_ctrl_link_cfg_ptr = (apm_module_ctrl_link_cfg_t *)curr_payload_ptr;

      /** Validate the module instance pair if they exist */
      if (AR_EOK != (result = apm_validate_module_instance_pair(&apm_info_ptr->graph_info,
                                                                curr_ctrl_link_cfg_ptr->peer_1_mod_iid,
                                                                curr_ctrl_link_cfg_ptr->peer_2_mod_iid,
                                                                module_node_ptr_list,
                                                                DANGLING_LINK_ALLOWED)))
      {
         AR_MSG(DBG_ERROR_PRIO, "MOD_CTRL_LNK_PARSE: Module IID validation failed");

         return AR_EBADPARAM;
      }

      if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
          apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_parse_mod_ctrl_link_cfg_fptr)
      {
         /**Get the connection from db and update client context*/
         if (AR_EOK != (result = apm_info_ptr->ext_utils.multi_client_vtbl_ptr
                                    ->apm_multi_client_parse_mod_ctrl_link_cfg_fptr(apm_info_ptr,
                                                                                    curr_ctrl_link_cfg_ptr,
                                                                                    &curr_payload_ptr,
                                                                                    &num_ctrl_links,
                                                                                    &conn_parse_req)))
         {
            return result;
         }

         /*same connections is ref counted for new client. So continue
          * parsing new connection */
         if (!conn_parse_req)
         {
            continue;
         }
      }

      if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
          apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_cache_ctrl_links_fptr)
      {
         /**Get the cache the connection & add the 1st client*/
         if (AR_EOK != (result = apm_info_ptr->ext_utils.multi_client_vtbl_ptr
                                    ->apm_multi_client_cache_ctrl_links_fptr(apm_info_ptr,
                                                                             module_node_ptr_list,
                                                                             curr_ctrl_link_cfg_ptr)))
         {
            return result;
         }
      }

      /** if both peers are present as a part of open, it can be
       *  intra-container control link within satellite PD. No special
       *  handling */

      /** For non-offload scenario, in master PD if either of the
       *  peer1 or peer2 module instance has not been opened yet, skip
       *  parsing this link and move on to next. Clients may
       *  attempt to open/close danling links and it needs to be
       *  skipped gracefully. */

      /** For offload scenario, on satellite PD, if either of the
       *  peer1 or peer2 module intance are not found, then it
       *  could be either a danling link or one of the peer is in
       *  another satellite pd */

      if (!module_node_ptr_list[PEER_1_MODULE] || !module_node_ptr_list[PEER_2_MODULE])
      {
         /** For satellite PD, if control link is across
          *  master-satellite or satellite1 and satellite2, master APM
          *  sends the module domain info. This info will not be
          *  available in master pd context */

         if (NULL == sat_prv_param_ptr)
         {
            /** Master PD context and dangling link found, skip parsing
             *  this link */
            goto _continue_ctrl_link_payload_parse;
         }
         else /** Satellite PD context */
         {
            if (ext_utils_ptr->offload_vtbl_ptr &&
                ext_utils_ptr->offload_vtbl_ptr->apm_offload_get_ctrl_link_remote_peer_info_fptr)
            {
               if (AR_EOK != (result = ext_utils_ptr->offload_vtbl_ptr
                                          ->apm_offload_get_ctrl_link_remote_peer_info_fptr(curr_ctrl_link_cfg_ptr,
                                                                                            sat_prv_param_ptr,
                                                                                            module_node_ptr_list,
                                                                                            &remote_peer_info_obj,
                                                                                            &local_peer_idx)))
               {
                  /** If the error is non-critical, conitnue parsing the rest
                   *  of the payload, else return failre */
                  if (AR_ECONTINUE == result)
                  {
                     goto _continue_ctrl_link_payload_parse;
                  }
                  else
                  {
                     return result;
                  }
               }
            }

            /** Set flag to indicate this link is inter proc domain */
            is_inter_proc_link = TRUE;

            /** Set flag to indicate the inter proc link is being parsed
             *  on satellite */
            is_sat_inter_proc_imcl = TRUE;
         }

      } /** End of if (one of the peer module not found) */

      /** Conditional below gets executed if a complete link is
       *  found either in the master or satellite PD */
      if (!is_sat_inter_proc_imcl)
      {
         /** If the current command is GRAPH CLOSE, then control links
          *  are expected to be closed only across different sub-graphs.
          *  If the links within the same sub-graphs are being closed,
          *  then print warning and continue parsing */
         if (APM_CMD_GRAPH_CLOSE == cmd_opcode)
         {
            /** Validate if the link is in the right state to be closed */
            if (AR_EOK !=
                (result =
                    apm_validate_and_cache_link_for_state_mgmt(apm_info_ptr, gm_cmd_ctrl_ptr, module_node_ptr_list)))
            {
               /** If link being closed is within the same sub-graphs, it needs
                *  to be skipped. For any other errors, return the failure. */
               if (AR_ECONTINUE == result)
               {
                  goto _continue_ctrl_link_payload_parse;
               }
               else
               {
                  return result;
               }
            }

         } /** End of if (APM_CMD_GRAPH_CLOSE == cmd_opcode) */

         /** Populate a temporary list of control port ID's to be used
          *  in the for-loop below. Index = 0 corresponds to PEER_1
          *  and Index = 1 corresponds to PEER_2 */
         ctrl_link_port_id_list[PEER_1_MODULE] = curr_ctrl_link_cfg_ptr->peer_1_mod_ctrl_port_id;
         ctrl_link_port_id_list[PEER_2_MODULE] = curr_ctrl_link_cfg_ptr->peer_2_mod_ctrl_port_id;

         /** If peer1 and peer2 modules belong to different container
          *  then need to send connect message to both the containers.
          *  Increment the updated container count */
         if ((module_node_ptr_list[PEER_1_MODULE]->host_cont_ptr->container_id) !=
             (module_node_ptr_list[PEER_2_MODULE]->host_cont_ptr->container_id))
         {
            num_cont_updated = 2;

            /*check if the heap property is different. If so, add to list of mixed heap links*/
            if (ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr &&
                ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr->apm_check_and_handle_mixed_heap_id_cntr_links_fptr)
            {
               ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr
                  ->apm_check_and_handle_mixed_heap_id_cntr_links_fptr(module_node_ptr_list,
                                                                       peer_heap_id_list,
                                                                       &is_mixed_heap_ctrl_link,
                                                                       CTRL_LINK_TRUE);
            }
         }

         /** Populate temp list of domain id's of both the peer in
          *  current link */
         peer_domain_id[PEER_1_MODULE] = module_node_ptr_list[PEER_1_MODULE]->host_cont_ptr->prop.proc_domain;
         peer_domain_id[PEER_2_MODULE] = module_node_ptr_list[PEER_2_MODULE]->host_cont_ptr->prop.proc_domain;

         /** If the domain ID's does not match, then its an inter PD
          *  link. This is possible only on the master PD */
         if (peer_domain_id[PEER_1_MODULE] != peer_domain_id[PEER_2_MODULE])
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "MOD_CTRL_LNK_PARSE: APM Received inter-domain Ctrl link between proc domains %lu and %lu",
                   peer_domain_id[PEER_1_MODULE],
                   peer_domain_id[PEER_2_MODULE]);

            /** Set the flag to indicate inter pd link   */
            is_inter_proc_link = TRUE;
         }

         /** For GRAPH OPEN command, if the host sub-graphs for source
          *  and destination modules in the ctrl link have already
          *  been started, then cache this control link for sending
          *  prepare and start command to the corresponding host
          *  containers */
         if (APM_CMD_GRAPH_OPEN == cmd_opcode)
         {
            if (ext_utils_ptr->runtime_link_hdlr_vtbl_ptr &&
                ext_utils_ptr->runtime_link_hdlr_vtbl_ptr->apm_check_and_cache_link_to_start_fptr)
            {
               if (AR_EOK != (result = ext_utils_ptr->runtime_link_hdlr_vtbl_ptr
                                          ->apm_check_and_cache_link_to_start_fptr(apm_info_ptr,
                                                                                   module_node_ptr_list,
                                                                                   curr_ctrl_link_cfg_ptr,
                                                                                   LINK_TYPE_CTRL,
                                                                                   &link_start_reqd)))
               {
                  AR_MSG(DBG_ERROR_PRIO, "MOD_CTRL_LNK_PARSE: Failed to cache opened ctrl link for start");

                  return result;
               }
            }
         }

      } /** End of if (!is_sat_inter_proc_imcl) */

      /** Identify the impacted containers and add them to pending
       *  list for sending message */
      for (uint32_t idx = 0; idx < num_cont_updated; idx++)
      {
         /** Reset the node added flag */
         //is_node_added = FALSE;

         /** idx tracks the local module,container etc. */
         /** num_containers impacted can  be max 2 for a link. */

         /** Peer is the other (0 -> 1 ; 1 -> 0) */
         uint32_t imcl_peer_idx = !idx;

         /** On sat PD, for an inter PD link, number of containers
          *  update will be 1 only */
         if (is_sat_inter_proc_imcl)
         {
            /** If execution context is satellite pd, curr_module_node_ptr
             *  should point to the module that is local to "this" proc
             *  domain */
            curr_module_node_ptr = module_node_ptr_list[local_peer_idx];
         }
         else /** Complete control link on master/sat PD */
         {
            curr_module_node_ptr = module_node_ptr_list[idx];
         }

         /** Get the host container handle */
         container_node_ptr = curr_module_node_ptr->host_cont_ptr;

         /** Get the pointer to current container command control pointer */
         apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_info_ptr->curr_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

         /** Check the current command being processed */
         if (APM_CMD_GRAPH_OPEN == cmd_opcode)
         {
            /** Pointer to this container's running list of module control
             *  link  configuration to be sent to containers as part of
             *  GRAPH OPEN command */
            list_pptr = &cont_cmd_ctrl_ptr->cached_cfg_params.graph_open_params.mod_ctrl_link_cfg_list_ptr;

            /** Total number of module control links configured for the host container */
            num_list_nodes_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params.graph_open_params.num_ctrl_links;

            /** Add the control link config structure to the running list
             *  of module control links to be sent to containers */
            if (AR_EOK != (result = apm_db_add_node_to_list(list_pptr, curr_ctrl_link_cfg_ptr, num_list_nodes_ptr)))
            {
               return result;
            }

            /*If current idx points to the container connected to the non-default heap ID peer, then cache this
             * link/heap information to be sent to the container as part of the OPEN message*/
            if (is_mixed_heap_ctrl_link)
            {
               if (ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr &&
                   ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr->apm_cache_mxd_heap_id_cntr_link_in_db_fptr)
               {
                  if (AR_EOK !=
                      (result = ext_utils_ptr->cntr_peer_heap_utils_vtbl_ptr
                                   ->apm_cache_mxd_heap_id_cntr_link_in_db_fptr(cont_cmd_ctrl_ptr,
                                                                                (void *)curr_ctrl_link_cfg_ptr,
                                                                                peer_heap_id_list[idx],
                                                                                CTRL_LINK_TRUE)))
                  {
                     return result;
                  }
               }
            }

            /** If current control link is inter pd */
            if (is_inter_proc_link)
            {
               list_pptr = &cont_cmd_ctrl_ptr->cached_cfg_params.graph_open_params.imcl_peer_domain_info_list_ptr;

               /** Total number of unique (if peer A->B has 2 links, it's not unique) inter-proc ctrl peers
                * configured for the host container */
               num_list_nodes_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params.graph_open_params.num_offloaded_imcl_peers;

               apm_imcl_peer_domain_info_t remote_peer_info;

               if (is_sat_inter_proc_imcl)
               {
                  remote_peer_info.module_iid = remote_peer_info_obj.module_iid;
                  remote_peer_info.domain_id  = remote_peer_info_obj.domain_id;
               }
               else
               {
                  remote_peer_info.module_iid = module_node_ptr_list[imcl_peer_idx]->instance_id;
                  remote_peer_info.domain_id  = peer_domain_id[imcl_peer_idx];
               }

               if (ext_utils_ptr->offload_vtbl_ptr &&
                   ext_utils_ptr->offload_vtbl_ptr->apm_check_alloc_add_to_peer_domain_ctrl_list_fptr)
               {
                  /** Check if node already exists with same data, and add to list for each container */
                  if (AR_EOK != (result = ext_utils_ptr->offload_vtbl_ptr
                                             ->apm_check_alloc_add_to_peer_domain_ctrl_list_fptr(list_pptr,
                                                                                                 &remote_peer_info,
                                                                                                 num_list_nodes_ptr)))
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "Failed to Add the domain info node to db for Self PEER [M_IID]:[0x%lx], on proc domain "
                            "%lu "
                            "and REMOTE PEER "
                            "[M_IID]:[0x%lx] on proc domain %lu",
                            is_sat_inter_proc_imcl ? module_node_ptr_list[local_peer_idx]->instance_id
                                                   : module_node_ptr_list[idx]->instance_id,
                            is_sat_inter_proc_imcl ? local_peer_domain_id : peer_domain_id[idx],
                            remote_peer_info.module_iid,
                            remote_peer_info.domain_id);

                     return result;
                  }
               }
            }
            /** Add this container to pending message send list */
            apm_add_cont_to_pending_msg_send_list(apm_info_ptr->curr_cmd_ctrl_ptr,
                                                  container_node_ptr,
                                                  cont_cmd_ctrl_ptr);
         }
         else /** if (APM_CMD_GRAPH_CLOSE == cmd_opcode ) */
         {
            uint32_t arr_index = idx;

            /** If execution context is satellite PD, arr_idx should point
             *  to  the elements that are local to "this" proc domain */

            if (is_sat_inter_proc_imcl)
            {
               arr_index = local_peer_idx;
            }

            /** If the control link being closed is within the single
             *  container, then link info need to be sent as is to the
             *  container as there are no port handles. */
            if ((module_node_ptr_list[PEER_1_MODULE]->host_cont_ptr->container_id) ==
                (module_node_ptr_list[PEER_2_MODULE]->host_cont_ptr->container_id))
            {
               /** Pointer to this container's running list of module control
                *  link  configuration to be sent to containers as part of
                *  GRAPH CLOSE command */
               list_pptr = &cont_cmd_ctrl_ptr->cmd_params.link_close_params.mod_ctrl_link_cfg_list_ptr;

               /** Total number of module control links configured for the host container */
               num_list_nodes_ptr = &cont_cmd_ctrl_ptr->cmd_params.link_close_params.num_ctrl_links;

               /** Add the control link config structure to the running list
                *  of module control links to be sent to containers */
               if (AR_EOK != (result = apm_db_add_node_to_list(list_pptr, curr_ctrl_link_cfg_ptr, num_list_nodes_ptr)))
               {
                  return result;
               }
            }
            else /** Control is present across 2 containers, cache the port handles */
            {
               /** Get the container port handle corresponding to the port ID */
               if (AR_EOK !=
                   (result =
                       apm_db_get_cont_port_conn(container_node_ptr
                                                    ->cont_ports_per_sg_pair[APM_PORT_TYPE_ACYCLIC][PORT_TYPE_CTRL_IO]
                                                    .list_ptr,
                                                 module_node_ptr_list[arr_index]->instance_id,
                                                 ctrl_link_port_id_list[arr_index],
                                                 &port_conn_info_ptr,
                                                 APM_DB_OBJ_QUERY)))
               {
                  AR_MSG(DBG_HIGH_PRIO, "MOD_CTRL_LNK_PARSE :: WARNING :: Failed to get port handle, skipping");

                  result = AR_EOK;

                  goto _continue_ctrl_link_payload_parse;
               }

               /** Pointer to this container's running list of module control
                *  link port handles to be sent to containers as part of
                *  GRAPH CLOSE command */
               list_pptr =
                  &cont_cmd_ctrl_ptr->cmd_params.link_close_params.cont_ports[APM_PORT_TYPE_ACYCLIC][PORT_TYPE_CTRL_IO]
                      .list_ptr;

               /** Pointer to total number of module control handles to be
                *   sent to host container */
               num_list_nodes_ptr =
                  &cont_cmd_ctrl_ptr->cmd_params.link_close_params.cont_ports[APM_PORT_TYPE_ACYCLIC][PORT_TYPE_CTRL_IO]
                      .num_nodes;

               /** Check if the node is already in the cached list. If not
                *  added the node. Node presence check is required because
                *  as part of GRAPH CLOSE command, client may send both the
                *  SG ID close and link close, in which case we should cache
                *  the port connection node only once. */
               apm_db_search_and_add_node_to_list(list_pptr, port_conn_info_ptr, num_list_nodes_ptr);

            } /** End of if (same container link) - else */

         } /** End of if (open cmd) - else */

      } /** End of for loop */

      /** If this control links is not inter processor and not
       *  across 2 satellites, then update the container graphs.
       *  Since this is control link connections, the 2 disjoint
       *  and already sorted graphs are only merged and not
       *  resorted. */
      if ((APM_CMD_GRAPH_OPEN == cmd_opcode) && (!is_sat_inter_proc_imcl && !is_inter_proc_link) &&
          (module_node_ptr_list[PEER_1_MODULE]->host_cont_ptr->container_id !=
           module_node_ptr_list[PEER_2_MODULE]->host_cont_ptr->container_id))
      {
         apm_update_cont_graph(&apm_info_ptr->graph_info,
                               module_node_ptr_list[PEER_1_MODULE]->host_cont_ptr,
                               module_node_ptr_list[PEER_2_MODULE]->host_cont_ptr,
                               FALSE /** Resort container graph */);
      }

   _continue_ctrl_link_payload_parse:

      /** Get container prop data payload size */
      prop_data_ptr = (curr_payload_ptr + sizeof(apm_module_ctrl_link_cfg_t));

      /** Get the overall property data size for this module prop configuration */
      if (AR_EOK != (result = apm_get_prop_data_size(prop_data_ptr,
                                                     curr_ctrl_link_cfg_ptr->num_props,
                                                     &prop_data_size,
                                                     apm_info_ptr->curr_cmd_ctrl_ptr->cmd_payload_size)))
      {
         return result;
      }

      /** Advance the pointer to start of next control link config
       *  object */
      curr_payload_ptr += (sizeof(apm_module_ctrl_link_cfg_t) + prop_data_size);

      /** Reduce current payload size  */
      curr_payload_size -= (sizeof(apm_module_ctrl_link_cfg_t) + prop_data_size);

      /** Decrement the num control link counter */
      num_ctrl_links--;

      /** Validate the remaining min payload size */
      if (num_ctrl_links && (curr_payload_size < (num_ctrl_links * sizeof(apm_module_ctrl_link_cfg_t))))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "MOD_CTRL_LNK_PARSE: Rem payload size[%lu] is less than min req'd, rem num_ctrl_link[%lu],"
                "cmd_opcode[0x%lX]",
                curr_payload_size,
                num_ctrl_links,
                cmd_opcode);

         return AR_EBADPARAM;
      }

#ifdef APM_DEBUG_CMD_PARSING
      AR_MSG(DBG_MED_PRIO,
             "MOD_CTRL_LNK_PARSE: Parsed data link peer1_module_iid[0x%lX], peer1_ctrl_port_id[0x%lX], "
             "peer2_module_iid[0x%lX], peer2_ctrl_port_id[0x%lX], num_props[%lu]",
             curr_ctrl_link_cfg_ptr->peer_1_mod_iid,
             curr_ctrl_link_cfg_ptr->peer_1_mod_ctrl_port_id,
             curr_ctrl_link_cfg_ptr->peer_2_mod_iid,
             curr_ctrl_link_cfg_ptr->peer_2_mod_ctrl_port_id,
             curr_ctrl_link_cfg_ptr->num_props);
#endif

   } /** while (num_ctrl_links) */

   AR_MSG(DBG_HIGH_PRIO,
          "MOD_CTRL_LNK_PARSE: Done parsing control link list, cmd_opcode[0x%8lX], result: 0x%lX",
          cmd_opcode,
          result);

   return result;
}

static ar_result_t apm_parse_graph_open_params(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr)
{
   ar_result_t      result = AR_EOK;
   uint8_t *        mod_pid_payload_ptr;
   apm_ext_utils_t *ext_utils_ptr;

   if (!apm_info_ptr || !mod_data_ptr)
   {
      return AR_EFAILED;
   }

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   AR_MSG(DBG_HIGH_PRIO,
          "GRAPH_OPEN: Processing config: [M_IID, PID] = [0x%lx, 0x%lx], param size[%lu bytes]",
          mod_data_ptr->module_instance_id,
          mod_data_ptr->param_id,
          mod_data_ptr->param_size);

   /** Get the param ID payload pointer */
   mod_pid_payload_ptr = (uint8_t *)mod_data_ptr + sizeof(apm_module_param_data_t);

   /** Handle the param ID's */
   switch (mod_data_ptr->param_id)
   {
      case APM_PARAM_ID_SUB_GRAPH_CONFIG:
      {
         result = apm_parse_subgraph_list(apm_info_ptr, mod_pid_payload_ptr, mod_data_ptr->param_size);
         break;
      }
      case APM_PARAM_ID_CONTAINER_CONFIG:
      {
         result = apm_parse_container_list(apm_info_ptr, mod_pid_payload_ptr, mod_data_ptr->param_size);
         break;
      }
      case APM_PARAM_ID_MODULES_LIST:
      {
         result = apm_parse_modules_list(apm_info_ptr, mod_pid_payload_ptr, mod_data_ptr->param_size);
         break;
      }
      case APM_PARAM_ID_MODULE_PROP:
      {
         result = apm_parse_module_prop_list(apm_info_ptr, mod_pid_payload_ptr, mod_data_ptr->param_size);
         break;
      }
      case APM_PARAM_ID_MODULE_CONN:
      {
         result = apm_parse_module_conn_list(apm_info_ptr, mod_pid_payload_ptr, mod_data_ptr->param_size);
         break;
      }
      case APM_PARAM_ID_MODULE_CTRL_LINK_CFG:
      {
         result = apm_parse_module_ctrl_link_cfg_list(apm_info_ptr, mod_pid_payload_ptr, mod_data_ptr->param_size);
         break;
      }
      case APM_PARAM_ID_IMCL_PEER_DOMAIN_INFO:
      {
         /** Private param ID for satellite APM introduced by OLC.
          *  Should be placed be before APM_PARAM_ID_MODULE_CTRL_LINK_CFG */

         if (ext_utils_ptr->offload_vtbl_ptr &&
             ext_utils_ptr->offload_vtbl_ptr->apm_parse_imcl_peer_domain_info_list_fptr)
         {
            result =
               ext_utils_ptr->offload_vtbl_ptr->apm_parse_imcl_peer_domain_info_list_fptr(apm_info_ptr,
                                                                                          mod_pid_payload_ptr,
                                                                                          mod_data_ptr->param_size);
         }
         else
         {
            result = AR_EUNSUPPORTED;
         }

         break;
      }
      default:
      {
         /** Unknown Param ID */
         AR_MSG(DBG_ERROR_PRIO,
                "GRAPH_OPEN Unsupported config: [M_IID, PID] = [0x%lx, 0x%lx]",
                mod_data_ptr->module_instance_id,
                mod_data_ptr->param_id);

         result = AR_EUNSUPPORTED;
         break;
      }
   } /** End of switch (mod_data_ptr->param_id) */

   /** If any error occurs during the command parsing, clean up
    *  the cached graph configuration and destroy any launched
    *  container threads */

   if (AR_EOK != result)
   {
      /** Update the error code in  module param data header */
      mod_data_ptr->error_code = result;

      /** Set the command status for further clean up   */
      apm_info_ptr->curr_cmd_ctrl_ptr->graph_open_cmd_ctrl.cmd_status = APM_OPEN_CMD_STATE_CONT_CREATE_FAIL;

      /** Update the overall command status */
      apm_info_ptr->curr_cmd_ctrl_ptr->cmd_status = result;
   }

   return result;
}

ar_result_t apm_parse_graph_mgmt_sg_id_list(apm_t *             apm_info_ptr,
                                            apm_sub_graph_id_t *sg_id_list_ptr,
                                            uint32_t            num_sg,
                                            uint32_t            cmd_opcode)
{
   ar_result_t     result = AR_EOK;
   apm_cmd_ctrl_t *apm_cmd_ctrl_ptr;

   /** Get the pointer to current command control */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Validate input arguments */
   if (!num_sg || !sg_id_list_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "GRAPH_MGMT sub-graph list null and/or invalid num sg: %lu, cmd_opcode[0x%lX]",
             num_sg,
             cmd_opcode);

      return AR_EBADPARAM;
   }

   AR_MSG(DBG_HIGH_PRIO, SPF_LOG_PREFIX "GRAPH_MGMT: Processing cmd_opcode[0x%lX], NUM_SG[%lu]", cmd_opcode, num_sg);

   /** Validate sub-graph ID list being processed. This call
    *  populates the list of sub-graphs to be processed and also
    *  sets the default state for the overall sub-graph list */
   if (AR_EOK != (result = apm_gm_cmd_validate_sg_list(apm_info_ptr, num_sg, sg_id_list_ptr, cmd_opcode)))
   {
      AR_MSG(DBG_ERROR_PRIO, "GRAPH_MGMT: SG_ID list validation failed, cmd_opcode[0x%08lx]", cmd_opcode);

      return result;
   }

   /** Cache sub-graph list info  */
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_cmd_sg_id      = num_sg;
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cmd_sg_id_list_ptr = sg_id_list_ptr;

   /**Validates below check when num_reg_sub_graphs is 0 and it's multi-client.
    * num_reg_sub_graphs can be 0, for this cmd(this client) as previous cmd(client) might have already processed and
    * send it to sequencer and not needed again to add it to reg_sg_list_ptr.
    * So num_reg_sub_graphs can be 0 for multi-client and NON zero for default.
    * */

   if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
       (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_reg_sub_graphs == 0))
   {
      AR_MSG(DBG_MED_PRIO,
             "GRAPH_MGMT:[multi-client] returning EOK as num_reg_sub_graphs is %d ",
             apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_reg_sub_graphs);
      return AR_EOK;
   }

   /** If there are any sub-graphs present managed via proxy
    *  manager, separate them out from the regular sub-graph
    *  list. */
   if (AR_EOK != (result = apm_proxy_util_sort_graph_mgmt_sg_lists(apm_info_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "GRAPH_MGMT: Failed to update grph_mgmt lists, cmd_opcode[0x%08lx]", cmd_opcode);

      return result;
   }

   /** If both regular sub-graph and proxy sub-graph list to
    *  process is empty, then return error to the caller */
   if (!apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_reg_sub_graphs &&
       !apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_proxy_sub_graphs)
   {
      return AR_EFAILED;
   }

   return result;
}

static ar_result_t apm_parse_gm_cmd_sg_id_list(apm_t *                  apm_info_ptr,
                                               apm_module_param_data_t *mod_data_ptr,
                                               uint32_t                 cmd_opcode)
{
   ar_result_t                    result = AR_EOK;
   uint8_t *                      mod_pid_payload_ptr;
   apm_param_id_sub_graph_list_t *sg_list_ptr;
   apm_sub_graph_id_t *           sg_id_list_ptr;
   uint32_t                       req_param_size;

   /** Get the param ID payload pointer */
   mod_pid_payload_ptr = (uint8_t *)mod_data_ptr + sizeof(apm_module_param_data_t);

   sg_list_ptr    = (apm_param_id_sub_graph_list_t *)mod_pid_payload_ptr;
   sg_id_list_ptr = (apm_sub_graph_id_t *)(mod_pid_payload_ptr + sizeof(apm_param_id_sub_graph_list_t));

   /** Valide number of sub-graphs */
   if (!sg_list_ptr->num_sub_graphs)
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_parse_gm_cmd_sg_id_list(): Num sub_graph is 0 , cmd_opcode[0x%08lx]", cmd_opcode);

      return AR_EBADPARAM;
   }

   /** Calculate required param size */
   req_param_size = sizeof(apm_param_id_sub_graph_list_t) + (sg_list_ptr->num_sub_graphs * sizeof(apm_sub_graph_id_t));

   /** Validate param size */
   if (mod_data_ptr->param_size < req_param_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_parse_gm_cmd_sg_id_list(): In sufficient param size[%lu bytes] req[%lu bytes], "
             "cmd_opcode[0x%08lx]",
             mod_data_ptr->param_size,
             req_param_size,
             cmd_opcode);

      return AR_EBADPARAM;
   }

   result = apm_parse_graph_mgmt_sg_id_list(apm_info_ptr, sg_id_list_ptr, sg_list_ptr->num_sub_graphs, cmd_opcode);

   return result;
}

static ar_result_t apm_process_graph_mgmt_params(apm_t *                  apm_info_ptr,
                                                 apm_module_param_data_t *mod_data_ptr,
                                                 uint32_t                 cmd_opcode)
{
   ar_result_t result = AR_EOK;
   uint8_t *   mod_pid_payload_ptr;

   AR_MSG(DBG_MED_PRIO,
          "GRAPH MGMT CMD: [M_IID, PID] = [0x%lX, 0x%lX], param size[%lu bytes], cmd_opcode[0x%08lx]",
          mod_data_ptr->module_instance_id,
          mod_data_ptr->param_id,
          mod_data_ptr->param_size,
          cmd_opcode);

   /** Get the param ID payload pointer */
   mod_pid_payload_ptr = (uint8_t *)mod_data_ptr + sizeof(apm_module_param_data_t);

   switch (cmd_opcode)
   {
      case APM_CMD_GRAPH_PREPARE:
      case APM_CMD_GRAPH_START:
      case APM_CMD_GRAPH_STOP:
      case APM_CMD_GRAPH_FLUSH:
      case APM_CMD_GRAPH_SUSPEND:
      {
         switch (mod_data_ptr->param_id)
         {
            case APM_PARAM_ID_SUB_GRAPH_LIST:
            {
               result = apm_parse_gm_cmd_sg_id_list(apm_info_ptr, mod_data_ptr, cmd_opcode);

               break;
            }
            default:
            {
               AR_MSG(DBG_HIGH_PRIO,
                      "GRAPH_MGMT :: WARNING :: Un-supported param iD[0x%lX], cmd_opcode[0x%08lx]",
                      mod_data_ptr->param_id,
                      cmd_opcode);

               return AR_EOK;
            }

         } /** End of switch (mod_data_ptr->param_id)*/

         break;
      }
      case APM_CMD_GRAPH_CLOSE:
      {
         switch (mod_data_ptr->param_id)
         {
            case APM_PARAM_ID_SUB_GRAPH_LIST:
            {
               result = apm_parse_gm_cmd_sg_id_list(apm_info_ptr, mod_data_ptr, cmd_opcode);

               break;
            }
            case APM_PARAM_ID_MODULE_CONN:
            {
               result = apm_parse_module_conn_list(apm_info_ptr, mod_pid_payload_ptr, mod_data_ptr->param_size);
               break;
            }
            case APM_PARAM_ID_MODULE_CTRL_LINK_CFG:
            {
               result =
                  apm_parse_module_ctrl_link_cfg_list(apm_info_ptr, mod_pid_payload_ptr, mod_data_ptr->param_size);
               break;
            }
            default:
            {
               break;
            }

         } /** End of switch (mod_data_ptr->param_id) */

         break;
      }
      default:
      {
         /** Unknown command opcode */
         /** Should not hit */

         result = AR_EUNSUPPORTED;
         break;
      }

   } /** End of switch (cmd_opcode) */

   /** Update the error code in the PID header */
   mod_data_ptr->error_code = result;

   return result;
}

static ar_result_t apm_parse_fwk_cfg_params(apm_t *                  apm_info_ptr,
                                            apm_module_param_data_t *mod_data_ptr,
                                            uint32_t                 cmd_opcode)
{
   ar_result_t      result = AR_EOK;
   apm_ext_utils_t *ext_utils_ptr;

   /** Get the ext utils vtb ptr  */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   /** Validate input arguments   */
   if (!apm_info_ptr || (!mod_data_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_parse_fwk_cfg_params(): Param data and/or apm info is NULL, cmd_opcode[0x%lX]",
             cmd_opcode);

      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO,
          "apm_parse_fwk_cfg_params(): [M_IID, PID] = [0x%lX, 0x%lX], param_size[%lu bytes], cmd_opcode[0x%lX]",
          mod_data_ptr->module_instance_id,
          mod_data_ptr->param_id,
          mod_data_ptr->param_size,
          cmd_opcode);

   switch (cmd_opcode)
   {
      case APM_CMD_GRAPH_OPEN:
      {
         result = apm_parse_graph_open_params(apm_info_ptr, mod_data_ptr);

         break;
      }
      case APM_CMD_GRAPH_PREPARE:
      case APM_CMD_GRAPH_START:
      case APM_CMD_GRAPH_STOP:
      case APM_CMD_GRAPH_CLOSE:
      case APM_CMD_GRAPH_FLUSH:
      case APM_CMD_GRAPH_SUSPEND:
      {
         result = apm_process_graph_mgmt_params(apm_info_ptr, mod_data_ptr, cmd_opcode);

         break;
      }
      case APM_CMD_CLOSE_ALL:
      {
         break;
      }
      case APM_CMD_GET_CFG:
      {
         if (ext_utils_ptr->set_get_cfg_vtbl_ptr &&
             ext_utils_ptr->set_get_cfg_vtbl_ptr->apm_parse_fwk_get_cfg_params_fptr)
         {
            result = ext_utils_ptr->set_get_cfg_vtbl_ptr->apm_parse_fwk_get_cfg_params_fptr(apm_info_ptr, mod_data_ptr);
         }

         break;
      }
      case APM_CMD_SET_CFG:
      {
         if (ext_utils_ptr->set_get_cfg_vtbl_ptr &&
             ext_utils_ptr->set_get_cfg_vtbl_ptr->apm_parse_fwk_set_cfg_params_fptr)
         {
            result = ext_utils_ptr->set_get_cfg_vtbl_ptr->apm_parse_fwk_set_cfg_params_fptr(apm_info_ptr, mod_data_ptr);
         }

         break;
      }
      default:
      {
         /** Unknown command opcode */
         /** Should not hit */
         result = AR_EUNSUPPORTED;
         break;
      }

   } /** End of switch (cmd_opcode) */

   return result;
}

static ar_result_t apm_aggregate_module_cfg(apm_t *apm_info_ptr, apm_module_param_data_t *param_data_ptr)
{
   ar_result_t            result = AR_EOK;
   apm_module_t *         module_node_ptr;
   apm_container_t *      host_cont_node_ptr;
   apm_cont_cached_cfg_t *cont_cached_cfg_ptr;
   apm_cont_cmd_ctrl_t *  cont_cmd_ctrl_ptr;
   apm_sub_graph_t       *sub_graph_node_ptr = NULL;
   gpr_packet_t          *packet_curr_client;
   apm_ext_utils_t       *ext_utils_ptr = &apm_info_ptr->ext_utils;

   if (AR_EOK != (result = apm_db_get_module_node(&apm_info_ptr->graph_info,
                                                  param_data_ptr->module_instance_id,
                                                  &module_node_ptr,
                                                  APM_DB_OBJ_QUERY)))
   {
      return result;
   }

   /** Check if the module exits */
   if (NULL == module_node_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO, "Module Not Found: (M_IID) = (0x%lX)", param_data_ptr->module_instance_id);

      return AR_EBADPARAM;
   }

   packet_curr_client = (gpr_packet_t *)apm_info_ptr->curr_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /** For the case of default calibration during Graph Open, checks the calibration state ("is_calibrated" of
    * apm_subgraph_t) of subgraph. If "is_calibrated' is TRUE, the return from here itself; otherwise proceed further.
    */
   if (APM_CMD_DB_DEFAULT_SET_CFG == apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode)
   {
      /** Check the calibration state of subraph of the module and if TRUE ,skip the calibration of the module */

      if (AR_EOK != (result = apm_db_get_sub_graph_node(&apm_info_ptr->graph_info,
                                                        module_node_ptr->host_sub_graph_ptr->sub_graph_id,
                                                        &sub_graph_node_ptr,
                                                        APM_DB_OBJ_QUERY)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "[multi-client]:Failed to get Subgraph [0x%lX]",
                module_node_ptr->host_sub_graph_ptr->sub_graph_id);

         return result;
      }

      /** Check if the subgraph exist */
      if (NULL == sub_graph_node_ptr)
      {
         AR_MSG(DBG_LOW_PRIO,
                "[multi-client]:Subgraph Not Found:(0x%lX)",
                module_node_ptr->host_sub_graph_ptr->sub_graph_id);

         return AR_EBADPARAM;
      }

      /** If "is_calibrated' is TRUE, the return from here itself */

      if (TRUE == sub_graph_node_ptr->is_calibrated)
      {
         AR_MSG(DBG_HIGH_PRIO,
                "[multi-client]:Client[0x%lX]- Skipping as Module id = (0x%lX) of subgraph (0x%lX) is already "
                "calibrated",
                packet_curr_client->src_port,
                param_data_ptr->module_instance_id,
                module_node_ptr->host_sub_graph_ptr->sub_graph_id);

         return result;
      }
   }

   AR_MSG(DBG_LOW_PRIO,
          "Aggregating module cfg: (M_IID, PID) = (0x%lX, 0x%lX)",
          module_node_ptr->instance_id,
          param_data_ptr->param_id);

   /** Get the host container node for this module */
   host_cont_node_ptr = module_node_ptr->host_cont_ptr;

   /** Get the pointer to host container's command control
    *  pointer */
   apm_get_cont_cmd_ctrl_obj(host_cont_node_ptr, apm_info_ptr->curr_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

   /** Get the pointer to container cached configuration params
    *  corresponding to current APM command */
   cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

   if (APM_CMD_GRAPH_OPEN == apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode)
   {
      /** Add this param ID to the cached configuration list of this module */
      if (AR_EOK != (result = apm_db_add_node_to_list(&cont_cached_cfg_ptr->graph_open_params.param_data_list_ptr,
                                                      param_data_ptr,
                                                      &cont_cached_cfg_ptr->graph_open_params.num_mod_param_cfg)))
      {
         return result;
      }
   }
   else /** SET/GET CFG */
   {
      /** Add this param ID to the cached configuration list of this module */
      if (AR_EOK != (result = apm_db_add_node_to_list(&cont_cached_cfg_ptr->module_cfg_params.param_data_list_ptr,
                                                      param_data_ptr,
                                                      &cont_cached_cfg_ptr->module_cfg_params.num_mod_param_cfg)))
      {
         return result;
      }
   }

   /** Add this container node to the list of container pending
    *  send message */
   if (AR_EOK != (result = apm_add_cont_to_pending_msg_send_list(apm_info_ptr->curr_cmd_ctrl_ptr,
                                                                 host_cont_node_ptr,
                                                                 cont_cmd_ctrl_ptr)))
   {
      return result;
   }

   if (ext_utils_ptr->multi_client_vtbl_ptr &&
       ext_utils_ptr->multi_client_vtbl_ptr->apm_multi_client_update_parsed_sg_list_fptr)
   {
      /** Add the subgraph containing the module to the parsed subgraph list */

      if (APM_CMD_DB_DEFAULT_SET_CFG == apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode)
      {
         result = ext_utils_ptr->multi_client_vtbl_ptr
                     ->apm_multi_client_update_parsed_sg_list_fptr(module_node_ptr->host_sub_graph_ptr->sub_graph_id,
                                                                   apm_info_ptr);
      }
   }

   return result;
}

ar_result_t apm_parse_cmd_payload(apm_t *apm_info_ptr)
{
   ar_result_t     result          = AR_EOK;
   ar_result_t     local_result    = AR_EOK;
   bool_t          USE_SYS_Q_FALSE = FALSE;
   uint8_t *       curr_config_ptr;
   uint8_t *       config_end_ptr;
   uint32_t        cmd_opcode;
   apm_cmd_ctrl_t *curr_cmd_ctrl_ptr;
   uint32_t        aligned_param_size;
   apm_ext_utils_t *ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Validate if the cmd control object has been initalized */
   if (!apm_info_ptr->curr_cmd_ctrl_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_parse_cmd_payload(): cmd ctrl obj is NULL / not allocated");

      return AR_EFAILED;
   }

   /** Get the current command control pointer */
   curr_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the GPR command opcode */
   cmd_opcode = curr_cmd_ctrl_ptr->cmd_opcode;

   /** Keep a copy of the payload start address. */
   curr_config_ptr = (uint8_t *)curr_cmd_ctrl_ptr->cmd_payload_ptr;

   /** Get the payload end address */
   config_end_ptr = curr_config_ptr + curr_cmd_ctrl_ptr->cmd_payload_size;

   /** Iterate through the entire payload size */
   do
   {
      /** Get the module header pointer */
      apm_module_param_data_t *mod_data_ptr = (apm_module_param_data_t *)curr_config_ptr;

      switch (mod_data_ptr->module_instance_id)
      {
         case APM_MODULE_INSTANCE_ID:
         {
            local_result = apm_parse_fwk_cfg_params(apm_info_ptr, mod_data_ptr, cmd_opcode);
            break;
         }
         default:
         {
            /** For non-APM module instance IDs, handle them as per
             *  static and dynamic classification */
            if (apm_is_module_static_inst_id(mod_data_ptr->module_instance_id))
            {
               local_result = apm_handle_proxy_mgr_cfg_params(apm_info_ptr, mod_data_ptr, USE_SYS_Q_FALSE);
            }
            else /** Dynamic module instance id */
            {
               switch (cmd_opcode)
               {
                  /** If module calibration is also sent as part of the graph
                   *  open command */
                  case APM_CMD_GRAPH_OPEN:
                  case APM_CMD_SET_CFG:
                  case APM_CMD_GET_CFG:
                  case APM_CMD_REGISTER_CFG:
                  case APM_CMD_DEREGISTER_CFG:
                  case APM_CMD_DB_DEFAULT_SET_CFG:
                  {
                     local_result = apm_aggregate_module_cfg(apm_info_ptr, mod_data_ptr);

                     break;
                  }
                  default:
                  {
                     /** Un-supported opcode, should not hit */
                     break;
                  }
               } /** End of switch (cmd_opcode) */
            }

            break;
         }

      } /** End of switch (mod_data_ptr->module_instance_id) */

      /** If any error occurred, abort parsing the command payload
       *  further */

      if (AR_EOK != local_result)
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_parse_cmd_payload(): Bailing out,  result: %lu", local_result);

         result = local_result;

         break;
      }

      /** All the OOB and in-band module param data is expected to start at 8 byte
       *  aligned boundary. For calculating the start address of next
       *  module instance ID payload, align the param size to 8 bytes. */

      aligned_param_size = ALIGN_8_BYTES(mod_data_ptr->param_size);

      /** Advance the current payload pointer to point to next
       *  module instance param data */
      curr_config_ptr += (sizeof(apm_module_param_data_t) + aligned_param_size);

   } while (curr_config_ptr < config_end_ptr);

   if (ext_utils_ptr->multi_client_vtbl_ptr &&
       ext_utils_ptr->multi_client_vtbl_ptr->apm_multi_client_update_sg_calibration_state_fptr)
   {
      /** Update calibration state of the subgraph node present in the parsed subgraph list */

      if (APM_CMD_DB_DEFAULT_SET_CFG == cmd_opcode)
      {
         result = ext_utils_ptr->multi_client_vtbl_ptr->apm_multi_client_update_sg_calibration_state_fptr(apm_info_ptr);
      }
   }

   return result;
}
