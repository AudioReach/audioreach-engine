/**
 * \file wc_graph_utils.c
 *
 * \brief
 *
 *     Graph utilities of wear container
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "wc_graph_utils.h"
#include "posal.h"
#include "amdb_static.h"
#include "spf_list_utils.h"
#include "spf_macros.h"
#include "gpr_api_inline.h"
#include "offload_apm_api.h"
#include "rd_sh_mem_ep_api.h"


/** max ports just for bounds check. also internal variables for num ports are 8 bits*/
#define MAX_PORTS 100

#define WCNTR_SG_ID_INVALID 0

//Temporary macro values for support.
//EC module will have 2 input ports but both near and far data will be in same port
//so dont throw error if number of input ports is 2
#define MAX_SUPPOTRED_IN_PORTS 2
#define MAX_SUPPOTRED_OUT_PORTS 4

#define WCNTR_GU_MSG_PREFIX "WCNTR_GU  :%08lX: "

#define WCNTR_GU_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, WCNTR_GU_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

#define DEBUG_GRAPH_SORT

typedef struct module_amdb_handle_t
{
   amdb_module_handle_info_t amdb_handle;
   wcntr_gu_module_t *       module_ptr;
} module_amdb_handle_t;

//TODO: Only for debugging
static void wcntr_gu_print_boundary_ports(wcntr_gu_t *gu_ptr)
{
   uint32_t count = 0;
   for (wcntr_gu_sg_list_t *all_sgs_list_ptr = gu_ptr->sg_list_ptr; (NULL != all_sgs_list_ptr);
        LIST_ADVANCE(all_sgs_list_ptr))
   {

      for (wcntr_gu_output_port_list_t *out_port_list_ptr = all_sgs_list_ptr->sg_ptr->boundary_out_port_list_ptr;
           (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         wcntr_gu_output_port_t *out_port_ptr = (wcntr_gu_output_port_t *)out_port_list_ptr->op_port_ptr;

         WCNTR_GU_MSG(gu_ptr->log_id,
                      DBG_HIGH_PRIO,
                      "in sg_id 0x%lX module instance 0x%lX output port id %u is boundary port",
                      all_sgs_list_ptr->sg_ptr->id,
                      out_port_ptr->cmn.module_ptr->module_instance_id,
                      out_port_ptr->cmn.id);
         count++;
      }

      for (wcntr_gu_input_port_list_t *in_port_list_ptr = all_sgs_list_ptr->sg_ptr->boundary_in_port_list_ptr;
           (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         wcntr_gu_input_port_t *in_port_ptr = (wcntr_gu_input_port_t *)in_port_list_ptr->ip_port_ptr;

         WCNTR_GU_MSG(gu_ptr->log_id,
                      DBG_HIGH_PRIO,
                      "in sg_id 0x%lX module instance 0x%lX input port id %u is boundary port",
                      all_sgs_list_ptr->sg_ptr->id,
                      in_port_ptr->cmn.module_ptr->module_instance_id,
                      in_port_ptr->cmn.id);

         count++;
      }
   }

   if (count == 0)
   {
      WCNTR_GU_MSG(gu_ptr->log_id, DBG_HIGH_PRIO, "wcntr_gu_print_boundary_ports : No boundary ports found");
   }
}
static void wcntr_gu_update_is_siso_module(wcntr_gu_module_t *module_ptr)
{
   if ((1 == module_ptr->num_output_ports) && (1 == module_ptr->max_output_ports) &&
       (1 == module_ptr->num_input_ports) && (1 == module_ptr->max_input_ports))
   {
      module_ptr->flags.is_siso = TRUE;
      return;
   }
   module_ptr->flags.is_siso = FALSE;
}

/*
 * Helper function to update state of graph components.
 * All states are not always valid, e.g. if state is NEW, it should not be changed to updated
 */
static void wcntr_gu_set_status(wcntr_gu_status_t *status_to_update, wcntr_gu_status_t status_value)
{
   // Reject the status update if moving from NEW to UPDATED.
   if ((WCNTR_GU_STATUS_NEW == *status_to_update) && (WCNTR_GU_STATUS_UPDATED == status_value))
   {
      return;
   }
   *status_to_update = status_value;
}

static wcntr_gu_cmn_port_t *wcntr_gu_find_port_by_id(spf_list_node_t *list_ptr, uint32_t id)
{
   while (list_ptr)
   {
      if (list_ptr->obj_ptr)
      {
         wcntr_gu_cmn_port_t *port_ptr = (wcntr_gu_cmn_port_t *)list_ptr->obj_ptr;
         if (port_ptr->id == id)
         {
            return port_ptr;
         }
      }
      LIST_ADVANCE(list_ptr);
   }
   return NULL;
}

wcntr_gu_output_port_t *wcntr_gu_find_output_port(wcntr_gu_module_t *module_ptr, uint32_t id)
{
   if (!module_ptr)
   {
      return NULL;
   }

   return (wcntr_gu_output_port_t *)wcntr_gu_find_port_by_id((spf_list_node_t *)module_ptr->output_port_list_ptr, id);
}

wcntr_gu_input_port_t *wcntr_gu_find_input_port(wcntr_gu_module_t *module_ptr, uint32_t id)
{
   if (!module_ptr)
   {
      return NULL;
   }

   return (wcntr_gu_input_port_t *)wcntr_gu_find_port_by_id((spf_list_node_t *)module_ptr->input_port_list_ptr, id);
}

wcntr_gu_ctrl_port_t *wcntr_gu_find_ctrl_port_by_id(wcntr_gu_module_t *module_ptr, uint32_t id)
{
   if (!module_ptr)
   {
      return NULL;
   }

   wcntr_gu_ctrl_port_list_t *list_ptr = module_ptr->ctrl_port_list_ptr;
   while (list_ptr)
   {
      if (list_ptr->ctrl_port_ptr)
      {
         wcntr_gu_ctrl_port_t *port_ptr = (wcntr_gu_ctrl_port_t *)list_ptr->ctrl_port_ptr;
         if (port_ptr->id == id)
         {
            return port_ptr;
         }
      }
      LIST_ADVANCE(list_ptr);
   }
   return NULL;
}

wcntr_gu_cmn_port_t *wcntr_gu_find_cmn_port_by_id(wcntr_gu_module_t *module_ptr, uint32_t id)
{
   if (wcntr_gu_is_output_port_id(id))
   {
      return (wcntr_gu_cmn_port_t *)wcntr_gu_find_output_port(module_ptr, id);
   }
   else
   {
      return (wcntr_gu_cmn_port_t *)wcntr_gu_find_input_port(module_ptr, id);
   }
}

static ar_result_t wcntr_gu_move_port_to_smallest_available_index(wcntr_gu_module_t *  module_ptr,
                                                                  spf_list_node_t **   list_pptr,
                                                                  wcntr_gu_cmn_port_t *new_port_ptr)
{
   ar_result_t      result          = AR_EOK;
   uint32_t         i               = 0;
   uint32_t         available_index = 0;
   spf_list_node_t *curr_ptr = NULL, *prev_ptr = NULL, *index_found_ptr = NULL, *new_node_ptr = NULL;

   for (curr_ptr = *list_pptr; (NULL != curr_ptr); LIST_ADVANCE(curr_ptr), i++)
   {
      wcntr_gu_cmn_port_t *port_ptr = (wcntr_gu_cmn_port_t *)curr_ptr->obj_ptr;

      // new_port_ptr is at the tail; it has not been assigned an index yet.
      if (port_ptr != new_port_ptr)
      {
         // if prev exists & difference of current and previous indices is more than one,
         // then available index is prev + 1.
         // if prev doesn't exist (first time) & then check if first port's index is far from zero.
         if (prev_ptr)
         {
            if ((port_ptr->index - ((wcntr_gu_cmn_port_t *)prev_ptr->obj_ptr)->index) > 1)
            {
               available_index = ((wcntr_gu_cmn_port_t *)prev_ptr->obj_ptr)->index + 1;
               index_found_ptr = curr_ptr;
               break;
            }
         }
         else // first port
         {
            if (port_ptr->index > 0)
            {
               available_index = 0;
               index_found_ptr = curr_ptr;
               break;
            }
         }
      }
      available_index = i;
      prev_ptr        = curr_ptr;
      new_node_ptr    = curr_ptr; // last node is the new node.
   }

   // go till curr_ptr becomes last element (= new_port_ptr's node)
   for (; (NULL != curr_ptr); LIST_ADVANCE(curr_ptr))
   {
      new_node_ptr = curr_ptr;
   }

   // if we found node with index, then index_found_ptr should be made next node to new_node_ptr
   //    E.g. if A, B, C, D are the nodes and D is new node, B is index_found. We need to make it A, D, B, C
   // if we didn't find then we have new_node at the right place (first position or last position).
   if (index_found_ptr)
   {
      new_node_ptr->next_ptr = index_found_ptr;

      if (new_node_ptr->prev_ptr)
      {
         new_node_ptr->prev_ptr->next_ptr = NULL; // terminate linked list
      }

      if (index_found_ptr->prev_ptr)
      {
         index_found_ptr->prev_ptr->next_ptr = new_node_ptr;
      }

      new_node_ptr->prev_ptr    = index_found_ptr->prev_ptr;
      index_found_ptr->prev_ptr = new_node_ptr;

      if (index_found_ptr == *list_pptr)
      {
         *list_pptr = new_node_ptr;
      }
   }

   new_port_ptr->index = available_index;

   return result;
}

static ar_result_t wcntr_gu_create_insert_port(wcntr_gu_t *          gu_ptr,
                                               wcntr_gu_module_t *   module_ptr,
                                               spf_list_node_t **    list_pptr,
                                               uint32_t              id,
                                               uint32_t              size,
                                               wcntr_gu_cmn_port_t **port_pptr,
                                               POSAL_HEAP_ID         heap_id,
                                               bool_t                is_input,
                                               bool_t                boundary_port)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   wcntr_gu_cmn_port_t *port_ptr;

   // Allocate port
   MALLOC_MEMSET(port_ptr, wcntr_gu_cmn_port_t, size, heap_id, result);
   wcntr_gu_set_status(&port_ptr->gu_status, WCNTR_GU_STATUS_NEW);

   port_ptr->id         = id;
   port_ptr->module_ptr = module_ptr;

   // Add to list
   TRY(result, spf_list_insert_tail(list_pptr, port_ptr, heap_id, TRUE /* use_pool*/))

   // Move it to right position based on index (sorted order)
   TRY(result, wcntr_gu_move_port_to_smallest_available_index(module_ptr, list_pptr, port_ptr));
   port_ptr->boundary_port = boundary_port;
   *port_pptr              = port_ptr;

   if (boundary_port)
   {
      if (is_input)
      {

         TRY(result,
             spf_list_insert_tail(((spf_list_node_t **)&module_ptr->sg_ptr->boundary_in_port_list_ptr),
                                  port_ptr,
                                  heap_id,
                                  TRUE /* use_pool*/))
         module_ptr->sg_ptr->num_boundary_in_ports++;
      }
      else
      {

         TRY(result,
             spf_list_insert_tail(((spf_list_node_t **)&module_ptr->sg_ptr->boundary_out_port_list_ptr),
                                  port_ptr,
                                  heap_id,
                                  TRUE /* use_pool*/))
         module_ptr->sg_ptr->num_boundary_out_ports++;
      }
   }

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }
   return result;
}

static ar_result_t wcntr_gu_create_insert_ctrl_port(wcntr_gu_t *           gu_ptr,
                                                    wcntr_gu_module_t *    module_ptr,
                                                    uint32_t               id,
                                                    uint32_t               size,
                                                    wcntr_gu_ctrl_port_t **ctrl_port_pptr,
                                                    POSAL_HEAP_ID          heap_id,
                                                    bool_t                 is_peer_port_in_same_sg)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t           result = AR_EOK;
   wcntr_gu_ctrl_port_t *port_ptr;

   // Allocate port
   MALLOC_MEMSET(port_ptr, wcntr_gu_ctrl_port_t, size, heap_id, result);
   wcntr_gu_set_status(&port_ptr->gu_status, WCNTR_GU_STATUS_NEW);

   port_ptr->id                      = id;
   port_ptr->module_ptr              = module_ptr;
   port_ptr->is_peer_port_in_same_sg = is_peer_port_in_same_sg;

   VERIFY(result, (module_ptr->num_ctrl_ports <= MAX_PORTS));

   // Add to list
   TRY(result,
       spf_list_insert_tail((spf_list_node_t **)&module_ptr->ctrl_port_list_ptr, port_ptr, heap_id, TRUE /* use_pool*/))
   module_ptr->num_ctrl_ports++;

   // Assign the return pointer.
   *ctrl_port_pptr = port_ptr;

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

static ar_result_t wcntr_gu_create_insert_output_port(wcntr_gu_t *             gu_ptr,
                                                      wcntr_gu_module_t *      module_ptr,
                                                      uint32_t                 id,
                                                      uint32_t                 size,
                                                      wcntr_gu_output_port_t **output_port_pptr,
                                                      POSAL_HEAP_ID            heap_id,
                                                      bool_t                   boundary_port)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   TRY(result,
       wcntr_gu_create_insert_port(gu_ptr,
                                   module_ptr,
                                   ((spf_list_node_t **)&module_ptr->output_port_list_ptr),
                                   id,
                                   size,
                                   (wcntr_gu_cmn_port_t **)output_port_pptr,
                                   heap_id,
                                   FALSE,
                                   boundary_port));

   module_ptr->num_output_ports++;
   wcntr_gu_update_is_siso_module(module_ptr);

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }
   return result;
}

static ar_result_t  __attribute__((noinline)) wcntr_gu_create_insert_input_port(wcntr_gu_t *            gu_ptr,
                                                     wcntr_gu_module_t *     module_ptr,
                                                     uint32_t                id,
                                                     uint32_t                size,
                                                     wcntr_gu_input_port_t **input_port_pptr,
                                                     POSAL_HEAP_ID           heap_id,
                                                     bool_t                  boundary_port)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   TRY(result,
       wcntr_gu_create_insert_port(gu_ptr,
                                   module_ptr,
                                   ((spf_list_node_t **)&module_ptr->input_port_list_ptr),
                                   id,
                                   size,
                                   (wcntr_gu_cmn_port_t **)input_port_pptr,
                                   heap_id,
                                   TRUE,
                                   boundary_port));

   module_ptr->num_input_ports++;
   wcntr_gu_update_is_siso_module(module_ptr);

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }
   return result;
}

static ar_result_t wcntr_gu_parse_subgraph_props(wcntr_gu_t *         gu_ptr,
                                                 wcntr_gu_sg_t *      sg_ptr,
                                                 apm_sub_graph_cfg_t *sg_cmd_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   apm_prop_data_t *sg_prop_ptr = (apm_prop_data_t *)(sg_cmd_ptr + 1);
   for (uint32_t j = 0; j < sg_cmd_ptr->num_sub_graph_prop; j++)
   {
      switch (sg_prop_ptr->prop_id)
      {
         case APM_SUB_GRAPH_PROP_ID_PERF_MODE:
         {
            VERIFY(result, sg_prop_ptr->prop_size >= sizeof(apm_sg_prop_id_perf_mode_t));
            apm_sg_prop_id_perf_mode_t *perf_mode_ptr = (apm_sg_prop_id_perf_mode_t *)(sg_prop_ptr + 1);
            VERIFY(result,
                   ((APM_SG_PERF_MODE_LOW_LATENCY == perf_mode_ptr->perf_mode) ||
                    (APM_SG_PERF_MODE_LOW_POWER == perf_mode_ptr->perf_mode)));

            sg_ptr->perf_mode = (uint8_t)perf_mode_ptr->perf_mode;
            break;
         }
         case APM_SUB_GRAPH_PROP_ID_DIRECTION:
         {
            VERIFY(result, sg_prop_ptr->prop_size >= sizeof(apm_sg_prop_id_direction_t));
            apm_sg_prop_id_direction_t *dir_ptr = (apm_sg_prop_id_direction_t *)(sg_prop_ptr + 1);
            VERIFY(result,
                   ((APM_SUB_GRAPH_DIRECTION_TX == dir_ptr->direction) ||
                    (APM_SUB_GRAPH_DIRECTION_RX == dir_ptr->direction) ||
                    (APM_PROP_ID_DONT_CARE == dir_ptr->direction)));

            sg_ptr->direction = (uint8_t)dir_ptr->direction;
            break;
         }
         case APM_SUB_GRAPH_PROP_ID_SCENARIO_ID:
         {
            VERIFY(result, sg_prop_ptr->prop_size >= sizeof(apm_sg_prop_id_scenario_id_t));
            apm_sg_prop_id_scenario_id_t *sid_ptr = (apm_sg_prop_id_scenario_id_t *)(sg_prop_ptr + 1);
            VERIFY(result,
                   (((APM_SUB_GRAPH_SID_AUDIO_PLAYBACK <= sid_ptr->scenario_id) &&
                     (APM_SUB_GRAPH_SID_SAT_VOICE_CALL >= sid_ptr->scenario_id)) ||
                    (APM_PROP_ID_DONT_CARE == sid_ptr->scenario_id)));

            sg_ptr->sid = (uint8_t)sid_ptr->scenario_id;
            break;
         }
         case APM_SUB_GRAPH_PROP_ID_VSID:
         {
#if 0
            VERIFY(result, sg_prop_ptr->prop_size >= sizeof(apm_sg_prop_id_scenario_id_t));
            apm_sg_prop_id_scenario_id_t *sid_ptr = (apm_sg_prop_id_scenario_id_t *)(sg_prop_ptr + 1);
            VERIFY(result,
                   (((APM_SUB_GRAPH_SID_AUDIO_PLAYBACK <= sid_ptr->scenario_id) &&
                     (APM_SUB_GRAPH_SID_SAT_VOICE_CALL >= sid_ptr->scenario_id)) ||
                    (APM_PROP_ID_DONT_CARE == sid_ptr->scenario_id)));


            sg_ptr->sid = sid_ptr->scenario_id;
#endif
            break;
         }
         default:
         {
            WCNTR_GU_MSG(gu_ptr->log_id,
                         DBG_HIGH_PRIO,
                         "WARNING: Unsupported subgraph property, 0x%X, ignoring",
                         sg_prop_ptr->prop_id);
         }
      }

      sg_prop_ptr = (apm_prop_data_t *)(((uint8_t *)(sg_prop_ptr + 1)) + sg_prop_ptr->prop_size);
   }

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

static ar_result_t wcntr_gu_parse_ctrl_port_props(wcntr_gu_t *                gu_ptr,
                                                  wcntr_gu_ctrl_port_t *      ctrl_port_ptr,
                                                  apm_module_ctrl_link_cfg_t *ctrl_link_cfg_ptr,
                                                  uint32_t                    max_ctrl_port_size,
                                                  uint32_t                    prop_mem_offset)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t      result            = AR_EOK;
   uint32_t         cur_memory_offset = prop_mem_offset;
   apm_prop_data_t *link_prop_ptr     = (apm_prop_data_t *)(ctrl_link_cfg_ptr + 1);
   for (uint32_t j = 0; j < ctrl_link_cfg_ptr->num_props; j++)
   {
      if (cur_memory_offset >= max_ctrl_port_size)
      {
         THROW(result, AR_EFAILED);
      }

      switch (link_prop_ptr->prop_id)
      {
         case APM_MODULE_PROP_ID_CTRL_LINK_INTENT_LIST:
         {
            VERIFY(result, link_prop_ptr->prop_size >= sizeof(apm_module_ctrl_link_prop_id_intent_list_t));

            apm_module_ctrl_link_prop_id_intent_list_t *intent_info_ptr =
               (apm_module_ctrl_link_prop_id_intent_list_t *)(link_prop_ptr + 1);

            ctrl_port_ptr->intent_info_ptr =
               (wcntr_gu_ctrl_port_prop_intent_list_t *)((int8_t *)ctrl_port_ptr + cur_memory_offset);

            ctrl_port_ptr->intent_info_ptr->num_intents = intent_info_ptr->num_intents;

            for (uint32_t i = 0; i < intent_info_ptr->num_intents; i++)
            {
               ctrl_port_ptr->intent_info_ptr->intent_id_list[i] = intent_info_ptr->intent_id_list[i];
            }

            cur_memory_offset +=
               (sizeof(wcntr_gu_ctrl_port_prop_intent_list_t) + (sizeof(uint32_t) * intent_info_ptr->num_intents));

            break;
         }
         default:
         {
            THROW(result, AR_EUNSUPPORTED);
         }
      }

      link_prop_ptr = (apm_prop_data_t *)(((uint8_t *)(link_prop_ptr + 1)) + link_prop_ptr->prop_size);
   }

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

static uint32_t wcntr_gu_get_required_ctrl_port_additional_size(wcntr_gu_t *                gu_ptr,
                                                                apm_module_ctrl_link_cfg_t *ctrl_link_cfg_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result          = AR_EOK;
   uint32_t    additional_size = 0;

   apm_prop_data_t *link_prop_ptr = (apm_prop_data_t *)(ctrl_link_cfg_ptr + 1);
   for (uint32_t j = 0; j < ctrl_link_cfg_ptr->num_props; j++)
   {
      switch (link_prop_ptr->prop_id)
      {
         case APM_MODULE_PROP_ID_CTRL_LINK_INTENT_LIST:
         {
            VERIFY(result, link_prop_ptr->prop_size >= sizeof(apm_module_ctrl_link_prop_id_intent_list_t));

            apm_module_ctrl_link_prop_id_intent_list_t *intent_info_ptr =
               (apm_module_ctrl_link_prop_id_intent_list_t *)(link_prop_ptr + 1);

            additional_size +=
               (sizeof(wcntr_gu_ctrl_port_prop_intent_list_t) + (sizeof(uint32_t) * intent_info_ptr->num_intents));

            break;
         }
         default:
         {
            THROW(result, AR_EUNSUPPORTED);
         }
      }

      link_prop_ptr = (apm_prop_data_t *)(((uint8_t *)(link_prop_ptr + 1)) + link_prop_ptr->prop_size);
   }

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return additional_size;
}

static ar_result_t wcntr_gu_parse_module_props(wcntr_gu_t *              gu_ptr,
                                               spf_msg_cmd_graph_open_t *open_cmd_ptr,
                                               POSAL_HEAP_ID             heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   for (uint32_t i = 0; i < open_cmd_ptr->num_mod_prop_cfg; i++)
   {
      apm_module_prop_cfg_t *apm_module_prop_ptr = open_cmd_ptr->mod_prop_cfg_list_pptr[i];

      wcntr_gu_module_t *module_ptr = wcntr_gu_find_module(gu_ptr, apm_module_prop_ptr->instance_id);
      // module must be created by this time. if not, it's an error (we cannot create at this stage because subgraph is
      // not known here)
      VERIFY(result, module_ptr);

      apm_prop_data_t *module_prop_ptr = (apm_prop_data_t *)(apm_module_prop_ptr + 1);

      for (uint32_t j = 0; j < apm_module_prop_ptr->num_props; j++)
      {
         switch (module_prop_ptr->prop_id)
         {
            case APM_MODULE_PROP_ID_PORT_INFO:
            {
               VERIFY(result, module_prop_ptr->prop_size >= sizeof(apm_module_prop_id_port_info_t));

               apm_module_prop_id_port_info_t *port_info_ptr = (apm_module_prop_id_port_info_t *)(module_prop_ptr + 1);

               // This module can be placed in graph open command.
               // no implementation of the module currently. Just sanity check
               if (MODULE_ID_RD_SHARED_MEM_EP == module_ptr->module_id)
               {
                  VERIFY(result, 1 == port_info_ptr->max_ip_port);
                  VERIFY(result, 1 >= port_info_ptr->max_op_port);
               }
               else
               {
                  // Verify that number of ports is contained (as it's used for mem allocs)
                  VERIFY(result,
                         (port_info_ptr->max_ip_port <= MAX_SUPPOTRED_IN_PORTS) &&
                            (port_info_ptr->max_op_port <= MAX_SUPPOTRED_OUT_PORTS));
               }

               module_ptr->max_input_ports  = port_info_ptr->max_ip_port;
               module_ptr->max_output_ports = port_info_ptr->max_op_port;

               /**
                * Even though some modules can behave as (source or SISO) or (sink or SISO),
                * once graph is drawn, their behavior is fixed.
                * E.g. a source module must have max-input-ports = 0, even though h2xml may have 1.
                * For dangling/incomplete connections, max-ports != 0, but num_ports = 0.
                *    for such modules, it's ok to stall processing.
                *
                * Also relying on num_ports can cause ambiguity when graph shape changes at run time.
                */

               if ((0 == module_ptr->max_output_ports) && (0 != module_ptr->max_input_ports))
               {
                  module_ptr->flags.is_sink = TRUE;
               }
               else if ((0 == module_ptr->max_input_ports) && (0 != module_ptr->max_output_ports))
               {
                  module_ptr->flags.is_source = TRUE;
               }

               wcntr_gu_update_is_siso_module(module_ptr);

               if (module_ptr->flags.is_source)
               {
                  TRY(result,
                      spf_list_insert_tail(((spf_list_node_t **)&(gu_ptr->src_module_list_ptr)),
                                           module_ptr,
                                           heap_id,
                                           TRUE /* use_pool*/));
               }

               if (module_ptr->flags.is_sink)
               {
                  TRY(result,
                      spf_list_insert_tail(((spf_list_node_t **)&(gu_ptr->snk_module_list_ptr)),
                                           module_ptr,
                                           heap_id,
                                           TRUE /* use_pool*/));
               }

               break;
            }
            default:
            {
               THROW(result, AR_EUNSUPPORTED);
               break;
            }
         }

         module_prop_ptr = (apm_prop_data_t *)(((uint8_t *)(module_prop_ptr + 1)) + module_prop_ptr->prop_size);
      }
   }
   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

// Check if module is connected or not and update the internal field.
static void wcntr_gu_update_module_is_connected(wcntr_gu_module_t *module_ptr)
{
   if ((module_ptr->max_input_ports > 0 && module_ptr->num_input_ports == 0) ||
       (module_ptr->max_output_ports > 0 && module_ptr->num_output_ports == 0))
   {
      module_ptr->flags.is_connected = FALSE;
   }
   else
   {
      module_ptr->flags.is_connected = TRUE;
   }
}

static void wcntr_gu_check_and_release_amdb_handle(wcntr_gu_module_t *module_ptr)
{
   if (module_ptr->amdb_handle)
   {
      // Release the handle since we no longer need it.
      spf_list_node_t           module_handle_info_list = { 0 };
      amdb_module_handle_info_t module_handle_info;
      module_handle_info.interface_type = module_ptr->itype; // Ignored
      module_handle_info.module_type    = 0;                 // Ignored
      module_handle_info.module_id      = 0;                 // Ignored
      module_handle_info.handle_ptr     = (void *)module_ptr->amdb_handle;
      module_handle_info.result         = AR_EOK;

      module_handle_info_list.prev_ptr = NULL;
      module_handle_info_list.next_ptr = NULL;
      module_handle_info_list.obj_ptr  = (void *)&module_handle_info;

      amdb_release_module_handles(&module_handle_info_list);
   }
}

ar_result_t wcntr_gu_prepare_for_module_loading(wcntr_gu_t *       gu_ptr,
                                                spf_list_node_t ** amdb_h_list_pptr,
                                                wcntr_gu_module_t *module_ptr,
                                                POSAL_HEAP_ID      heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   if (module_ptr->module_id == MODULE_ID_RD_SHARED_MEM_EP)
   {

      WCNTR_GU_MSG(gu_ptr->log_id,
                   DBG_HIGH_PRIO,
                   "found MODULE_ID_RD_SHARED_MEM_EP. Not adding to list");
      return AR_EOK;
   }

   module_amdb_handle_t *amdb_handle_ptr = NULL;
   MALLOC_MEMSET(amdb_handle_ptr, module_amdb_handle_t, sizeof(module_amdb_handle_t), heap_id, result);

   TRY(result, spf_list_insert_tail(amdb_h_list_pptr, amdb_handle_ptr, heap_id, TRUE /* use_pool*/));

   amdb_handle_ptr->amdb_handle.module_id  = module_ptr->module_id;
   amdb_handle_ptr->amdb_handle.handle_ptr = NULL;
   amdb_handle_ptr->amdb_handle.result     = AR_EFAILED;
   amdb_handle_ptr->module_ptr             = module_ptr;

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t wcntr_gu_handle_module_loading(spf_list_node_t **amdb_h_list_pptr, uint32_t log_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   if (!(*amdb_h_list_pptr))
   {
      return AR_EOK;
   }

   /*
    * Note: This call will block till all modules with 'preload = 0' are loaded by the AMDB. This loading
    * happens using a thread pool using threads of very low priority. This can cause the current thread
    * to be blocked because of a low priority thread. If this is not desired, a callback function
    * should be provided that can be used by the AMDB to signal when the modules are loaded. The current
    * thread can then handle other tasks in parallel.
    */
   amdb_request_module_handles(*amdb_h_list_pptr, NULL, NULL);

   // verify query results & free list elements once done.
   while (*amdb_h_list_pptr)
   {
      module_amdb_handle_t *amdb_handle_ptr = (module_amdb_handle_t *)(*amdb_h_list_pptr)->obj_ptr;
      if ((AR_DID_FAIL(amdb_handle_ptr->amdb_handle.result)) ||
          ((AMDB_INTERFACE_TYPE_CAPI != amdb_handle_ptr->amdb_handle.interface_type) &&
           (AMDB_INTERFACE_TYPE_STUB != amdb_handle_ptr->amdb_handle.interface_type)))
      {
         WCNTR_GU_MSG(log_id,
                      DBG_ERROR_PRIO,
                      "AMDB query result failed 0x%lx (must be 0), interface type 0x%lx (must be 2 or 3)",
                      amdb_handle_ptr->amdb_handle.result,
                      amdb_handle_ptr->amdb_handle.interface_type);
		 
           // Release the handle immedietly to avoid memleak
         if (NULL != amdb_handle_ptr->amdb_handle.handle_ptr)
         {
            spf_list_node_t module_handle_info_list = { 0 };
            // Reuse the same obj since it has the proper handle and interface type
            module_handle_info_list.obj_ptr = (void *)amdb_handle_ptr;
            amdb_release_module_handles(&module_handle_info_list);
         }

         // If loading fails or the interface type is not supported mark as failure
         result |= AR_EFAILED;
      }
      else
      {
         // if we already have an amdb_handle associated with the module - we need to release it
         wcntr_gu_check_and_release_amdb_handle(amdb_handle_ptr->module_ptr);

         VERIFY(result,
                ((amdb_handle_ptr->amdb_handle.module_type < UINT8_MAX) &&
                 (amdb_handle_ptr->amdb_handle.interface_type < UINT8_MAX)));

         amdb_handle_ptr->module_ptr->module_type = amdb_handle_ptr->amdb_handle.module_type;
         amdb_handle_ptr->module_ptr->itype       = (uint8_t)amdb_handle_ptr->amdb_handle.interface_type;
         amdb_handle_ptr->module_ptr->amdb_handle = (void *)amdb_handle_ptr->amdb_handle.handle_ptr;

         // could be stub or capiv2 handle. stubs must be bypassed by containers (works only for SISO).
      }
      spf_list_delete_node_and_free_obj(amdb_h_list_pptr, NULL, TRUE /* pool_used */);
   }

   CATCH(result, WCNTR_GU_MSG_PREFIX, log_id)
   {
   }

   return result;
}

bool_t wcntr_gu_check_if_peer_miid_is_on_a_remote_proc(apm_imcl_peer_domain_info_t **imcl_peer_domain_info_pptr,
                                                       uint32_t                      num_offloaded_imcl_peers,
                                                       uint32_t                      peer_iid,
                                                       uint32_t *                    remote_domain_id_ptr)
{
   if (!num_offloaded_imcl_peers)
   {
      return FALSE;
   }

   for (uint32_t i = 0; i < num_offloaded_imcl_peers; i++)
   {
      apm_imcl_peer_domain_info_t *peer_info_ptr = imcl_peer_domain_info_pptr[i];
      if (peer_iid == peer_info_ptr->module_iid)
      {
         *remote_domain_id_ptr = peer_info_ptr->domain_id;
         return TRUE;
      }
   }

   return FALSE;
}

static ar_result_t wcntr_gu_create_control_link(wcntr_gu_t *                  gu_ptr,
                                                apm_module_ctrl_link_cfg_t *  cmd_ctrl_conn_ptr,
                                                wcntr_gu_sizes_t *            sizes_ptr,
                                                apm_imcl_peer_domain_info_t **imcl_peer_domain_info_pptr,
                                                uint32_t                      num_offloaded_imcl_peers,
                                                POSAL_HEAP_ID                 heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   wcntr_gu_module_t *peer_mod_ptr[2]         = { NULL };
   uint32_t           additional_size         = 0;
   bool_t             is_peer_port_in_same_sg = TRUE;
   peer_mod_ptr[0]                            = wcntr_gu_find_module(gu_ptr, cmd_ctrl_conn_ptr->peer_1_mod_iid);
   peer_mod_ptr[1]                            = wcntr_gu_find_module(gu_ptr, cmd_ctrl_conn_ptr->peer_2_mod_iid);
   //   POSAL_HEAP_ID peer_heap_id   = heap_id; //initialize to my heap id
   uint32_t peer_mod_ctrl_port_id[2] = { cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id,
                                         cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id };
   wcntr_gu_ctrl_port_t *peer_ctrl_port_ptr[2] = { NULL };

   uint32_t peer_1_mod_module_sg_id = WCNTR_SG_ID_INVALID;
   uint32_t peer_2_mod_module_sg_id = WCNTR_SG_ID_INVALID;

   if (peer_mod_ptr[0])
   {
      peer_1_mod_module_sg_id = peer_mod_ptr[0]->sg_ptr->id;
   }
   if (peer_mod_ptr[1])
   {
      peer_2_mod_module_sg_id = peer_mod_ptr[1]->sg_ptr->id;
   }

   if (peer_1_mod_module_sg_id != peer_2_mod_module_sg_id && peer_1_mod_module_sg_id != WCNTR_SG_ID_INVALID &&
       peer_2_mod_module_sg_id != WCNTR_SG_ID_INVALID)
   {
      is_peer_port_in_same_sg = FALSE;
   }

   WCNTR_GU_MSG(gu_ptr->log_id,
                DBG_HIGH_PRIO,
                "create_control_link (mid,port_id) peer_1  (0x%lx,0x%lx),peer_2 (0x%lx,0x%lx) "
                "is_peer_port_in_same_sg %u",
                cmd_ctrl_conn_ptr->peer_1_mod_iid,
                cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id,
                cmd_ctrl_conn_ptr->peer_2_mod_iid,
                cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id,
                is_peer_port_in_same_sg);

   bool_t   peer_port_found = FALSE;
   uint32_t host_domain_id, remote_domain_id;
   __gpr_cmd_get_host_domain_id(&host_domain_id);
   remote_domain_id = host_domain_id; // AKR: Initialized to same-proc IMCL

   // Control Port-IDs cannot be zero.
   VERIFY(result, AR_PORT_ID_INVALID != peer_mod_ctrl_port_id[0]);
   VERIFY(result, AR_PORT_ID_INVALID != peer_mod_ctrl_port_id[1]);

   // Handle only control connections in this context.
   VERIFY(result,
          (AR_PORT_DATA_TYPE_CONTROL ==
           spf_get_bits(peer_mod_ctrl_port_id[0], AR_PORT_DATA_TYPE_MASK, AR_PORT_DATA_TYPE_SHIFT)));
   VERIFY(result,
          (AR_PORT_DATA_TYPE_CONTROL ==
           spf_get_bits(peer_mod_ctrl_port_id[1], AR_PORT_DATA_TYPE_MASK, AR_PORT_DATA_TYPE_SHIFT)));

   /** If the module or the port is part of this gu_ptr (container),
    *  then it's internal connection, otherwise, it's an external connection. */

   // Create internal port structures for both the peer1 and peer2.
   additional_size = wcntr_gu_get_required_ctrl_port_additional_size(gu_ptr, cmd_ctrl_conn_ptr);
   for (uint32_t iter = 0; iter < 2; iter++)
   {
      if (peer_mod_ptr[iter])
      {
         // if port is not present, create and add to list.
         peer_ctrl_port_ptr[iter] = wcntr_gu_find_ctrl_port_by_id(peer_mod_ptr[iter], peer_mod_ctrl_port_id[iter]);
         if (peer_ctrl_port_ptr[iter])
         {
            peer_port_found = TRUE;
         }
         else
         {
            uint32_t base_ctrl_port_size = MAX(sizes_ptr->ctrl_port_size, sizeof(wcntr_gu_ctrl_port_t));

            TRY(result,
                wcntr_gu_create_insert_ctrl_port(gu_ptr,
                                                 peer_mod_ptr[iter],
                                                 peer_mod_ctrl_port_id[iter],
                                                 base_ctrl_port_size + additional_size,
                                                 &peer_ctrl_port_ptr[iter],
                                                 heap_id,
                                                 is_peer_port_in_same_sg));

            // Parse ctrl link properties from the graph open cmd.
            wcntr_gu_parse_ctrl_port_props(gu_ptr,
                                           peer_ctrl_port_ptr[iter],
                                           cmd_ctrl_conn_ptr,
                                           base_ctrl_port_size + additional_size,
                                           base_ctrl_port_size);

            // Mark module and sg as updated
            wcntr_gu_set_status(&peer_mod_ptr[iter]->gu_status, WCNTR_GU_STATUS_UPDATED);
            wcntr_gu_set_status(&peer_mod_ptr[iter]->sg_ptr->gu_status, WCNTR_GU_STATUS_UPDATED);
         }
      }
      else
      {
         // the module must be in some other container.
         // it's not possible to get connection definition without module being defined.
      }
   }

   /** if src and dst modules are NULL, then they don't belong to this container or are not created yet.
    *  if src is non-NULL, dst is NULL, then the src is the ext output port = output module
    *  if src is NULL, dst is non-NULL, then the dst is the ext input port = input module
    *  if src and dst modules are both non-NULL, then both belong to the same container */
   if (peer_mod_ptr[0] && peer_mod_ptr[1])
   {
      // There should not be any existing connection because we are creating new connection
      VERIFY(result, !(peer_ctrl_port_ptr[0]->peer_ctrl_port_ptr || peer_ctrl_port_ptr[1]->peer_ctrl_port_ptr));
      peer_ctrl_port_ptr[0]->peer_ctrl_port_ptr = peer_ctrl_port_ptr[1];
      peer_ctrl_port_ptr[1]->peer_ctrl_port_ptr = peer_ctrl_port_ptr[0];
   }
   else if (!peer_mod_ptr[0] && !peer_mod_ptr[1]) // If both are NULL.
   {
      WCNTR_GU_MSG(gu_ptr->log_id, DBG_HIGH_PRIO, "Control link doesn't belong to this container");
   }
   else if (peer_mod_ptr[0] || peer_mod_ptr[1]) // if one of the peer module
                                                // is not present in this
                                                // container.
   {
      WCNTR_GU_MSG(gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "Control link: one of the modules doesn't belong to this container. Not supported");
      return AR_EFAILED;
   }

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

static ar_result_t  wcntr_gu_handle_connection(wcntr_gu_t *      gu_ptr,
                                              uint32_t          src_mod_inst_id,
                                              uint32_t          src_mod_op_port_id,
                                              uint32_t          dst_mod_inst_id,
                                              uint32_t          dst_mod_ip_port_id,
                                              wcntr_gu_sizes_t *sizes_ptr,
                                              POSAL_HEAP_ID     heap_id,
                                              POSAL_HEAP_ID     peer_heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result              = AR_EOK;
   wcntr_gu_output_port_t *src_output_port_ptr = NULL;
   wcntr_gu_input_port_t * dst_input_port_ptr  = NULL;
   bool_t                  src_port_found      = FALSE;
   bool_t                  dst_port_found      = FALSE;

   wcntr_gu_module_t *src_module_ptr = (0 == src_mod_inst_id) ? NULL : wcntr_gu_find_module(gu_ptr, src_mod_inst_id);
   wcntr_gu_module_t *dst_module_ptr = (0 == dst_mod_inst_id) ? NULL : wcntr_gu_find_module(gu_ptr, dst_mod_inst_id);

   uint32_t src_module_sg_id = WCNTR_SG_ID_INVALID;
   uint32_t dst_module_sg_id = WCNTR_SG_ID_INVALID;
   bool_t   boundary_port    = FALSE;

   if (src_module_ptr)
   {
      src_module_sg_id = src_module_ptr->sg_ptr->id;
   }
   if (dst_module_ptr)
   {
      dst_module_sg_id = dst_module_ptr->sg_ptr->id;
   }

   if (src_module_sg_id != dst_module_sg_id && src_module_sg_id != WCNTR_SG_ID_INVALID &&
       dst_module_sg_id != WCNTR_SG_ID_INVALID)
   {
      boundary_port = TRUE;
   }
   // Port-IDs cannot be zero.
   VERIFY(result, AR_PORT_ID_INVALID != src_mod_op_port_id);
   VERIFY(result, AR_PORT_ID_INVALID != dst_mod_ip_port_id);

   // Handle only data connections in this context.
   VERIFY(result,
          (AR_PORT_DATA_TYPE_DATA ==
           spf_get_bits(src_mod_op_port_id, AR_PORT_DATA_TYPE_MASK, AR_PORT_DATA_TYPE_SHIFT)));
   VERIFY(result,
          (AR_PORT_DATA_TYPE_DATA ==
           spf_get_bits(dst_mod_ip_port_id, AR_PORT_DATA_TYPE_MASK, AR_PORT_DATA_TYPE_SHIFT)));

   // Direction check input port ID even ID.
   VERIFY(result,
          (AR_PORT_DIR_TYPE_OUTPUT == spf_get_bits(src_mod_op_port_id, AR_PORT_DIR_TYPE_MASK, AR_PORT_DIR_TYPE_SHIFT)));
   VERIFY(result,
          (AR_PORT_DIR_TYPE_INPUT == spf_get_bits(dst_mod_ip_port_id, AR_PORT_DIR_TYPE_MASK, AR_PORT_DIR_TYPE_SHIFT)));

   /** If the module or the port is part of this gu_ptr (container),
    *  then it's internal connection, otherwise, it's an external connection. */
   if (src_module_ptr)
   {
      // if port is not present, create and add to list.
      src_output_port_ptr = wcntr_gu_find_output_port(src_module_ptr, src_mod_op_port_id);
      if (src_output_port_ptr)
      {
         src_port_found = TRUE;
      }
      else
      {
         VERIFY(result, src_module_ptr->max_output_ports > src_module_ptr->num_output_ports);
         TRY(result,
             wcntr_gu_create_insert_output_port(gu_ptr,
                                                src_module_ptr,
                                                src_mod_op_port_id,
                                                MAX(sizes_ptr->out_port_size, sizeof(wcntr_gu_output_port_t)),
                                                &src_output_port_ptr,
                                                heap_id,
                                                boundary_port));

         // Mark module and sg as updated
         wcntr_gu_set_status(&src_module_ptr->gu_status, WCNTR_GU_STATUS_UPDATED);
         wcntr_gu_set_status(&src_module_ptr->sg_ptr->gu_status, WCNTR_GU_STATUS_UPDATED);
      }
   }
   else
   {
      // the module must be in some other container.
      // it's not possible to get connection definition without module being defined.
   }

   if (dst_module_ptr)
   {
      dst_input_port_ptr = wcntr_gu_find_input_port(dst_module_ptr, dst_mod_ip_port_id);
      if (dst_input_port_ptr)
      {
         dst_port_found = TRUE;
         // if internal connection is already present, error out
         VERIFY(result, !(src_port_found && dst_port_found));
      }
      else
      {

		 VERIFY(result, dst_module_ptr->max_input_ports > dst_module_ptr->num_input_ports); 
         TRY(result,
             wcntr_gu_create_insert_input_port(gu_ptr,
                                               dst_module_ptr,
                                               dst_mod_ip_port_id,
                                               MAX(sizes_ptr->in_port_size, sizeof(wcntr_gu_input_port_t)),
                                               &dst_input_port_ptr,
                                               heap_id,
                                               boundary_port));


         // Mark module and sg as updated
         wcntr_gu_set_status(&dst_module_ptr->gu_status, WCNTR_GU_STATUS_UPDATED);
         wcntr_gu_set_status(&dst_module_ptr->sg_ptr->gu_status, WCNTR_GU_STATUS_UPDATED);
      }
   }
   else
   {
      // the module must be in some other container.
      // it's not possible to get connection definition without module being defined.
   }

   /** if src and dst modules are NULL, then they don't belong to this container or are not created yet.
    *  if src is non-NULL, dst is NULL, then the src is the ext output port = output module
    *  if src is NULL, dst is non-NULL, then the dst is the ext input port = input module
    *  if src and dst modules are both non-NULL, then both belong to the same container */
   if (src_module_ptr && dst_module_ptr)
   {
      // There should not be any existing connection because we are creating new connection
      VERIFY(result, !(src_output_port_ptr->conn_in_port_ptr || dst_input_port_ptr->conn_out_port_ptr));
      src_output_port_ptr->conn_in_port_ptr = dst_input_port_ptr;
      dst_input_port_ptr->conn_out_port_ptr = src_output_port_ptr;
   }
   else if (src_module_ptr) // sink module/port in not in this container
   {
      AR_MSG(DBG_ERROR_PRIO, "Sink module not in this container. No support for creating external output port");
      return AR_EFAILED;
   }
   else if (dst_module_ptr) // source module/port is not in this container
   {
      AR_MSG(DBG_ERROR_PRIO, "Source module not in this container.No support for creating external input port");
      return AR_EFAILED;
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "connection doesn't belong to this container");
   }

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   AR_MSG(DBG_HIGH_PRIO,
          "GU  :%08lX:  handled data conncection (miid,port_id) src ( 0x%X, 0x%X) "
          "dst ( 0x%X, 0x%X), boundary_port %u",
          gu_ptr->log_id,
          src_mod_inst_id,
          src_mod_op_port_id,
          dst_mod_inst_id,
          dst_mod_ip_port_id,
          boundary_port);

   return result;
}

/* Function to reset the markers for all the internal ports of a graph.
   Port marker is used sometimes to optimize the graph traversal. The marker is set when a port is visited during
   graph traversal. So all the port markers must be reset to FALSE before the next graph operation is handled. */
ar_result_t wcntr_gu_reset_graph_port_markers(wcntr_gu_t *gu_ptr)
{
   wcntr_gu_module_list_t *mod_list_ptr = NULL;
   wcntr_gu_module_t *     module_ptr   = NULL;
   wcntr_gu_sg_list_t *    sg_list_ptr  = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      mod_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr;
      while (mod_list_ptr)
      {
         bool_t module_done = TRUE;
         module_ptr         = mod_list_ptr->module_ptr;

         // Reset all the input ports of the module.
         wcntr_gu_input_port_list_t *ip_port_list_ptr = module_ptr->input_port_list_ptr;
         while (ip_port_list_ptr)
         {
            if (!ip_port_list_ptr->ip_port_ptr->cmn.mark)
            {
               module_done = FALSE;
            }
            ip_port_list_ptr->ip_port_ptr->cmn.mark = FALSE;
            LIST_ADVANCE(ip_port_list_ptr);
         }

         // Reset all the output ports of the module.
         wcntr_gu_output_port_list_t *op_port_list_ptr = module_ptr->output_port_list_ptr;
         while (op_port_list_ptr)
         {
            if (!op_port_list_ptr->op_port_ptr->cmn.mark)
            {
               module_done = FALSE;
            }
            op_port_list_ptr->op_port_ptr->cmn.mark = FALSE;
            LIST_ADVANCE(op_port_list_ptr);
         }

         if (!module_done)
         {
            WCNTR_GU_MSG(gu_ptr->log_id,
                         DBG_HIGH_PRIO,
                         "Warning: Module instance id 0x%08lx found with unmarked edges",
                         module_ptr->module_instance_id);
         }

         LIST_ADVANCE(mod_list_ptr);
      }
      LIST_ADVANCE(sg_list_ptr);
   }
   return AR_EOK;
}

ar_result_t wcntr_gu_find_edge_modules(wcntr_gu_t *             gu_ptr,
                                       wcntr_gu_module_list_t **edge_module_list_pptr,
                                       POSAL_HEAP_ID            heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result       = AR_EOK;
   wcntr_gu_sg_list_t *    sg_list_ptr  = NULL;
   wcntr_gu_module_list_t *mod_list_ptr = NULL;

   wcntr_gu_module_list_t *edge_module_list_ptr = NULL;

#ifdef DEBUG_GRAPH_SORT
   WCNTR_GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "Checking external modules...");
#endif

   VERIFY(result, edge_module_list_pptr);

#ifdef DEBUG_GRAPH_SORT
   WCNTR_GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "Checking source modules...");
#endif

   /* Find any source modules */

   // This avoids adding to the floating module list twice.
	  bool_t insert_floating_modules = (NULL == gu_ptr->floating_modules_list_ptr);


   sg_list_ptr = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      mod_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr;
      while (mod_list_ptr)
      {
         // We want source modules to be in the sorted list, but not floating modules
         if ((0 == mod_list_ptr->module_ptr->num_input_ports) && (0 < mod_list_ptr->module_ptr->num_output_ports))
         {
#ifdef DEBUG_GRAPH_SORT
            WCNTR_GU_MSG(gu_ptr->log_id,
                         DBG_MED_PRIO,
                         "Adding source module to edge list, inst id 0x%08lx",
                         mod_list_ptr->module_ptr->module_instance_id);
#endif
            TRY(result,
                spf_list_insert_tail((spf_list_node_t **)&edge_module_list_ptr,
                                     (void *)mod_list_ptr->module_ptr,
                                     heap_id,
                                     TRUE /* use_pool*/));
         }

		          // This avoids adding to the floating module list twice.
         if (insert_floating_modules)
         {

         if ((0 == mod_list_ptr->module_ptr->num_input_ports) && (0 == mod_list_ptr->module_ptr->num_output_ports))
         {
#ifdef DEBUG_GRAPH_SORT
            WCNTR_GU_MSG(gu_ptr->log_id,
                         DBG_MED_PRIO,
                         "Adding floating module to floating modules list, inst id 0x%08lx",
                         mod_list_ptr->module_ptr->module_instance_id);
#endif
            TRY(result,
                spf_list_insert_tail((spf_list_node_t **)&gu_ptr->floating_modules_list_ptr,
                                     (void *)mod_list_ptr->module_ptr,
                                     heap_id,
                                     TRUE /* use_pool*/));
         }
         	}
         LIST_ADVANCE(mod_list_ptr);
      }
      LIST_ADVANCE(sg_list_ptr);
   }

   // Return edge modules.
   *edge_module_list_pptr = edge_module_list_ptr;

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t wcntr_gu_update_sorted_list(wcntr_gu_t *gu_ptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result               = AR_EOK;
   wcntr_gu_module_list_t *edge_module_list_ptr = NULL;
   wcntr_gu_sg_list_t *    sg_list_ptr          = NULL;
   wcntr_gu_module_list_t *mod_list_ptr         = NULL;
   wcntr_gu_module_t *     module_ptr           = NULL;

   // If sorted list is already allocated, need to free it
   if (gu_ptr->sorted_module_list_ptr)
   {
      spf_list_delete_list((spf_list_node_t **)&gu_ptr->sorted_module_list_ptr, TRUE /* use_pool*/);
      gu_ptr->sort_status = FALSE;
   }

   // Start with modules that have no internal connections
   // These are either source modules or modules that are connected to an external input port
   TRY(result, wcntr_gu_find_edge_modules(gu_ptr, &edge_module_list_ptr, heap_id));

#ifdef DEBUG_GRAPH_SORT
   WCNTR_GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "Running sort...");
#endif

   /* Now run sort */
   while (edge_module_list_ptr)
   {
      // Add this module to sorted list
      TRY(result,
          spf_list_insert_tail((spf_list_node_t **)&gu_ptr->sorted_module_list_ptr,
                               (void *)edge_module_list_ptr->module_ptr,
                               heap_id,
                               TRUE /* use_pool*/));

#ifdef DEBUG_GRAPH_SORT
      WCNTR_GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "Removing links...");
#endif
      // Now mark all vertices for its connected modules
      wcntr_gu_output_port_list_t *op_port_list_ptr = edge_module_list_ptr->module_ptr->output_port_list_ptr;

      while (op_port_list_ptr)
      {
         bool_t mark_done = TRUE;

         // Mark the downstream module's input port that is connected to this output port
         op_port_list_ptr->op_port_ptr->conn_in_port_ptr->cmn.mark = TRUE;
         // Check if all input ports for the module are marked. If yes, it should be moved to the sorted list
         wcntr_gu_input_port_list_t *ip_port_list_ptr =
            op_port_list_ptr->op_port_ptr->conn_in_port_ptr->cmn.module_ptr->input_port_list_ptr;
         while (ip_port_list_ptr)
         {
            if (!ip_port_list_ptr->ip_port_ptr->cmn.mark)
            {
               mark_done = FALSE;
               break;
            }
            LIST_ADVANCE(ip_port_list_ptr);
         }
         if (mark_done)
         {
            // Insert this module into the edge list as well
            TRY(result,
                spf_list_insert_tail((spf_list_node_t **)&edge_module_list_ptr,
                                     (void *)op_port_list_ptr->op_port_ptr->conn_in_port_ptr->cmn.module_ptr,
                                     heap_id,
                                     TRUE /* use_pool*/));
         }
         LIST_ADVANCE(op_port_list_ptr);
      }
      spf_list_delete_node((spf_list_node_t **)&edge_module_list_ptr, TRUE /*pool_used */);
   }

   // Sort complete, validate that all nodes are actually marked, then clear marks for next sort
   // Also validate if the module is fully connected, print a message if not
   TRY(result, wcntr_gu_reset_graph_port_markers(gu_ptr));

   // Check and update is a module is fully connected.
   sg_list_ptr = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      mod_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr;
      while (mod_list_ptr)
      {
         module_ptr = mod_list_ptr->module_ptr;

         wcntr_gu_update_module_is_connected(module_ptr);
         if (!module_ptr->flags.is_connected)
         {
            WCNTR_GU_MSG(gu_ptr->log_id,
                         DBG_HIGH_PRIO,
                         "Warning: Module instance id 0x%08lx not fully connected !",
                         module_ptr->module_instance_id);
         }

         LIST_ADVANCE(mod_list_ptr);
      }
      LIST_ADVANCE(sg_list_ptr);
   }

   // set sorted flag
   gu_ptr->sort_status = TRUE;

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
      // free up the sorted list and mark it unsorted in case of failure
      spf_list_delete_list((spf_list_node_t **)&gu_ptr->sorted_module_list_ptr, TRUE /* pool_used */);
      gu_ptr->sort_status = FALSE;
   }

// spf_list_delete_list((spf_list_node_t**)&edge_module_list_ptr);

#ifdef GRAPH_EASY
   wcntr_gu_print_graph(gu_ptr);
#endif

   return result;
}

wcntr_gu_module_t *wcntr_gu_find_module(wcntr_gu_t *gu_ptr, uint32_t module_instance_id)
{
   wcntr_gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      wcntr_gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr;
      while (module_list_ptr)
      {
         if (module_list_ptr->module_ptr->module_instance_id == module_instance_id)
         {
            return module_list_ptr->module_ptr;
         }
         LIST_ADVANCE(module_list_ptr);
      }
      LIST_ADVANCE(sg_list_ptr);
   }
   return NULL;
}

ar_result_t wcntr_gu_parse_data_link(wcntr_gu_t *             gu_ptr,
                                     apm_module_conn_cfg_t *  data_link_ptr,
                                     wcntr_gu_output_port_t **src_mod_out_port_pptr,
                                     wcntr_gu_input_port_t ** dst_mod_in_port_pptr)
{
   wcntr_gu_module_t *src_module_ptr = NULL;
   wcntr_gu_module_t *dst_module_ptr = NULL;

   if (NULL != data_link_ptr)
   {
      src_module_ptr = (wcntr_gu_module_t *)wcntr_gu_find_module(gu_ptr, data_link_ptr->src_mod_inst_id);
      dst_module_ptr = (wcntr_gu_module_t *)wcntr_gu_find_module(gu_ptr, data_link_ptr->dst_mod_inst_id);
      if ((NULL == src_module_ptr) || (NULL == dst_module_ptr))
      {
         WCNTR_GU_MSG(gu_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "GU: Mod not found Src (ID-ptr) (0x%X-0x%X), Dst (ID, ptr) (0x%X, 0x%X)",
                      data_link_ptr->src_mod_inst_id,
                      src_module_ptr,
                      data_link_ptr->dst_mod_inst_id,
                      dst_module_ptr);
         return AR_EFAILED;
      }

      *src_mod_out_port_pptr = wcntr_gu_find_output_port(src_module_ptr, data_link_ptr->src_mod_op_port_id);
      *dst_mod_in_port_pptr  = wcntr_gu_find_input_port(dst_module_ptr, data_link_ptr->dst_mod_ip_port_id);

      if ((NULL == *src_mod_out_port_pptr) || (NULL == *dst_mod_in_port_pptr))
      {
         WCNTR_GU_MSG(gu_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "GU: port not found Src (ID-ptr) (0x%X-0x%X), Dst (ID, ptr) (0x%X, 0x%X)",
                      data_link_ptr->src_mod_op_port_id,
                      *src_mod_out_port_pptr,
                      data_link_ptr->dst_mod_ip_port_id,
                      *dst_mod_in_port_pptr);
         return AR_EFAILED;
      }
   }
   else
   {
      return AR_EFAILED;
   }
   return AR_EOK;
}

ar_result_t wcntr_gu_parse_ctrl_link(wcntr_gu_t *                gu_ptr,
                                     apm_module_ctrl_link_cfg_t *ctrl_link_ptr,
                                     wcntr_gu_ctrl_port_t **     peer_1_ctrl_port_pptr,
                                     wcntr_gu_ctrl_port_t **     peer_2_ctrl_port_pptr)
{
   wcntr_gu_module_t *peer_1_module_ptr = NULL;
   wcntr_gu_module_t *peer_2_module_ptr = NULL;

   if (NULL != ctrl_link_ptr)
   {
      peer_1_module_ptr = (wcntr_gu_module_t *)wcntr_gu_find_module(gu_ptr, ctrl_link_ptr->peer_1_mod_iid);
      peer_2_module_ptr = (wcntr_gu_module_t *)wcntr_gu_find_module(gu_ptr, ctrl_link_ptr->peer_2_mod_iid);

      if ((NULL == peer_1_module_ptr) || (NULL == peer_2_module_ptr))
      {
         WCNTR_GU_MSG(gu_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "GU: Mod not found Peer 1 (ID-ptr) (0x%X-0x%X), Peer 2 (ID, ptr) (0x%X, 0x%X)",
                      ctrl_link_ptr->peer_1_mod_iid,
                      peer_1_module_ptr,
                      ctrl_link_ptr->peer_2_mod_iid,
                      peer_2_module_ptr);
         return AR_EFAILED;
      }

      *peer_1_ctrl_port_pptr = wcntr_gu_find_ctrl_port_by_id(peer_1_module_ptr, ctrl_link_ptr->peer_1_mod_ctrl_port_id);
      *peer_2_ctrl_port_pptr = wcntr_gu_find_ctrl_port_by_id(peer_2_module_ptr, ctrl_link_ptr->peer_2_mod_ctrl_port_id);

      if ((NULL == *peer_1_ctrl_port_pptr) || (NULL == *peer_2_ctrl_port_pptr))
      {
         WCNTR_GU_MSG(gu_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "GU: port not found Peer 1 (ID-ptr) (0x%X-0x%X), Peer 2 (ID, ptr) (0x%X, 0x%X)",
                      ctrl_link_ptr->peer_1_mod_ctrl_port_id,
                      *peer_1_ctrl_port_pptr,
                      ctrl_link_ptr->peer_2_mod_ctrl_port_id,
                      *peer_2_ctrl_port_pptr);
         return AR_EFAILED;
      }
   }
   else
   {
      return AR_EFAILED;
   }
   return AR_EOK;
}

static wcntr_gu_cmn_port_t *wcntr_gu_find_port_by_index(spf_list_node_t *list_ptr, uint32_t index)
{
   while (list_ptr)
   {
      if (list_ptr->obj_ptr)
      {
         wcntr_gu_cmn_port_t *port_ptr = (wcntr_gu_cmn_port_t *)list_ptr->obj_ptr;
         if (port_ptr->index == index)
         {
            return port_ptr;
         }
      }
      LIST_ADVANCE(list_ptr);
   }
   return NULL;
}

wcntr_gu_output_port_t *wcntr_gu_find_output_port_by_index(wcntr_gu_module_t *module_ptr, uint32_t index)
{
   if (!module_ptr)
   {
      return NULL;
   }

   return (wcntr_gu_output_port_t *)wcntr_gu_find_port_by_index((spf_list_node_t *)module_ptr->output_port_list_ptr,
                                                                index);
}

wcntr_gu_input_port_t *wcntr_gu_find_input_port_by_index(wcntr_gu_module_t *module_ptr, uint32_t index)
{
   if (!module_ptr)
   {
      return NULL;
   }

   return (wcntr_gu_input_port_t *)wcntr_gu_find_port_by_index((spf_list_node_t *)module_ptr->input_port_list_ptr,
                                                               index);
}

wcntr_gu_sg_t *wcntr_gu_find_subgraph(wcntr_gu_t *gu_ptr, uint32_t id)
{
   wcntr_gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      if (sg_list_ptr->sg_ptr)
      {
         wcntr_gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;
         if (sg_ptr->id == id)
         {
            return sg_ptr;
         }
      }
      LIST_ADVANCE(sg_list_ptr);
   }

   return NULL;
}

bool_t wcntr_gu_is_port_handle_found_in_spf_array(uint32_t       num_handles,
                                                  spf_handle_t **port_handles_array_pptr,
                                                  spf_handle_t * given_handle)
{
   for (uint32_t i = 0; i < num_handles; i++)
   {
      if (port_handles_array_pptr[i] == given_handle)
      {
         return TRUE;
      }
   }
   return FALSE;
}

bool_t wcntr_gu_is_sg_id_found_in_spf_array(spf_cntr_sub_graph_list_t *spf_sg_array_ptr, uint32_t sg_id)
{
   if (NULL == spf_sg_array_ptr)
   {
      return FALSE;
   }

   uint32_t *sg_id_base_ptr = spf_sg_array_ptr->sg_id_list_ptr;
   for (uint32_t i = 0; i < spf_sg_array_ptr->num_sub_graph; i++)
   {
      if (sg_id == *(sg_id_base_ptr + i))
      {
         return TRUE;
      }
   }

   return FALSE;
}

/**
 * sg_list_ptr == NULL means destroy all subgraphs
 *
 */
ar_result_t wcntr_gu_destroy_graph(wcntr_gu_t *gu_ptr, spf_msg_cmd_graph_mgmt_t *cmd_gmgmt_ptr, POSAL_HEAP_ID heap_id)
{
   ar_result_t result = AR_EOK;

   // For optimization, skip module destruction if there are no sgs to destroy.
   bool_t no_sgs_to_destroy = FALSE;

   // if cmd_gmgmt_ptr is NULL, then spf_sg_array_ptr is NULL, all SGs are destroyed.
   // it's possible to have sg_id_list with zero SGs
   spf_cntr_sub_graph_list_t *spf_sg_array_ptr = (NULL != cmd_gmgmt_ptr) ? &cmd_gmgmt_ptr->sg_id_list : NULL;

   if (!gu_ptr)
   {
      return AR_EOK;
   }

   // If spf_sg_array_ptr is given, but num_subgraph is zero, then there are no modules to destroy so can skip
   // destroying.
   if ((NULL != spf_sg_array_ptr) && (0 == spf_sg_array_ptr->num_sub_graph))
   {
      no_sgs_to_destroy = TRUE;
   }

   if (!no_sgs_to_destroy)
   {
      wcntr_gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr;
      while (sg_list_ptr)
      {
         wcntr_gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;

         if (NULL == sg_ptr)
         {
            LIST_ADVANCE(sg_list_ptr);
            continue;
         }

         // If spf_sg_array_ptr is not set, destroy everything.
         if (spf_sg_array_ptr)
         {
            // if this subgraph is not in the given list of subgraphs, then move on
            if (!wcntr_gu_is_sg_id_found_in_spf_array(spf_sg_array_ptr, sg_ptr->id))
            {
               LIST_ADVANCE(sg_list_ptr);
               continue;
            }
         }

         wcntr_gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr;
         while (module_list_ptr)
         {
            wcntr_gu_module_t *module_ptr = module_list_ptr->module_ptr;

            // Free input ports
            wcntr_gu_input_port_list_t *ip_port_list_ptr = module_ptr->input_port_list_ptr;
            while (ip_port_list_ptr)
            {
               // Set connected port for upstream ports to NULL, those ports will get cleaned up later
               wcntr_gu_input_port_t *in_port_ptr = ip_port_list_ptr->ip_port_ptr;
               if (in_port_ptr)
               {
                  if (in_port_ptr->conn_out_port_ptr)
                  {
                     in_port_ptr->conn_out_port_ptr->conn_in_port_ptr = NULL;
                  }
                  if (in_port_ptr->cmn.boundary_port)
                  {
                     WCNTR_GU_MSG(gu_ptr->log_id,
                                  DBG_LOW_PRIO,
                                  SPF_LOG_PREFIX "subgraph 0x%lX removing boundary input port node from list",
                                  sg_ptr->id);
                     spf_list_find_delete_node((spf_list_node_t **)&in_port_ptr->cmn.module_ptr->sg_ptr
                                                  ->boundary_in_port_list_ptr,
                                               (void *)in_port_ptr,
                                               TRUE /* pool_used */);
                     in_port_ptr->cmn.module_ptr->sg_ptr->num_boundary_in_ports--;
                  }
               }
               LIST_ADVANCE(ip_port_list_ptr);
            }
            spf_list_delete_list_and_free_objs(((spf_list_node_t **)&module_ptr->input_port_list_ptr),
                                               TRUE /* pool_used */);
            module_ptr->num_input_ports = 0;
            wcntr_gu_update_is_siso_module(module_ptr);

            // Free output ports
            wcntr_gu_output_port_list_t *op_port_list_ptr = module_ptr->output_port_list_ptr;
            while (op_port_list_ptr)
            {
               wcntr_gu_output_port_t *out_port_ptr = op_port_list_ptr->op_port_ptr;
               if (out_port_ptr)
               {
                  if (out_port_ptr->conn_in_port_ptr)
                  {
                     out_port_ptr->conn_in_port_ptr->conn_out_port_ptr = NULL;
                  }

                  if (out_port_ptr->cmn.boundary_port)
                  {
                     WCNTR_GU_MSG(gu_ptr->log_id,
                                  DBG_LOW_PRIO,
                                  SPF_LOG_PREFIX "subgraph 0x%lX removing boundary output port node from list",
                                  sg_ptr->id);
                     spf_list_find_delete_node((spf_list_node_t **)&out_port_ptr->cmn.module_ptr->sg_ptr
                                                  ->boundary_out_port_list_ptr,
                                               (void *)out_port_ptr,
                                               TRUE /* pool_used */);
                     out_port_ptr->cmn.module_ptr->sg_ptr->num_boundary_out_ports--;
                  }
               }
               LIST_ADVANCE(op_port_list_ptr);
            }
            spf_list_delete_list_and_free_objs(((spf_list_node_t **)&module_ptr->output_port_list_ptr),
                                               TRUE /* pool_used */);
            module_ptr->num_output_ports = 0;
            wcntr_gu_update_is_siso_module(module_ptr);

            // Free control ports
            wcntr_gu_ctrl_port_list_t *ctrl_port_list_ptr = module_ptr->ctrl_port_list_ptr;
            while (ctrl_port_list_ptr)
            {
               wcntr_gu_ctrl_port_t *ctrl_port_ptr = ctrl_port_list_ptr->ctrl_port_ptr;
               if (ctrl_port_ptr && ctrl_port_ptr->peer_ctrl_port_ptr)
               {
                  // Remove the currently destroyed port handle from the peer handle.
                  ctrl_port_ptr->peer_ctrl_port_ptr->peer_ctrl_port_ptr = NULL;
               }
               LIST_ADVANCE(ctrl_port_list_ptr);
            }
            spf_list_delete_list_and_free_objs(((spf_list_node_t **)&module_ptr->ctrl_port_list_ptr),
                                               TRUE /* pool_used */);
            module_ptr->num_ctrl_ports = 0;

            wcntr_gu_check_and_release_amdb_handle(module_ptr);

            if (module_ptr->flags.is_source)
            {
               spf_list_find_delete_node((spf_list_node_t **)&gu_ptr->src_module_list_ptr,
                                         (void *)module_ptr,
                                         TRUE /* pool_used */);
            }

            if (module_ptr->flags.is_sink)
            {
               spf_list_find_delete_node((spf_list_node_t **)&gu_ptr->snk_module_list_ptr,
                                         (void *)module_ptr,
                                         TRUE /* pool_used */);
            }

            WCNTR_GU_MSG(gu_ptr->log_id,
                         DBG_LOW_PRIO,
                         SPF_LOG_PREFIX "Destroyed Module 0x%lX",
                         module_ptr->module_instance_id);

            // Free module
            sg_ptr->num_modules--;
            spf_list_delete_node_and_free_obj(((spf_list_node_t **)&module_list_ptr),
                                              (spf_list_node_t **)&sg_ptr->module_list_ptr,
                                              TRUE /* pool_used */);
         }

         WCNTR_GU_MSG(gu_ptr->log_id, DBG_LOW_PRIO, SPF_LOG_PREFIX "Destroyed subgraph 0x%lX", sg_ptr->id);

         // Free subgraph
         gu_ptr->num_subgraphs--;
         spf_list_delete_node_and_free_obj((spf_list_node_t **)&sg_list_ptr,
                                           (spf_list_node_t **)&gu_ptr->sg_list_ptr,
                                           TRUE /* pool_used */);
      }

      // Only need to free the list, module are already freed in loop above
      spf_list_delete_list((spf_list_node_t **)&gu_ptr->sorted_module_list_ptr, TRUE /* pool_used */);
      spf_list_delete_list((spf_list_node_t **)&gu_ptr->floating_modules_list_ptr, TRUE /* pool_used */);
      gu_ptr->sort_status = FALSE;
   }

   /* Now iterate through remaining modules and free up any ports that also need to be cleaned up
    * This will happen when a container contains multiple subgraphs and there are internal ports that
    * need to be destroyed in modules that were connected to the destroyed subgraphs
    */
   {
      wcntr_gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr;
      while (sg_list_ptr)
      {
         wcntr_gu_sg_t *         sg_ptr          = sg_list_ptr->sg_ptr;
         wcntr_gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr;
         while (module_list_ptr)
         {
            bool_t             module_updated = FALSE;
            wcntr_gu_module_t *module_ptr     = module_list_ptr->module_ptr;

            wcntr_gu_input_port_list_t *ip_port_list_ptr = module_ptr->input_port_list_ptr;
            while (ip_port_list_ptr)
            {
               // If a port has neither an internal or external connection, it should be freed here
               wcntr_gu_input_port_t *in_port_ptr = ip_port_list_ptr->ip_port_ptr;

               if (!in_port_ptr->conn_out_port_ptr)
               {
                  WCNTR_GU_MSG(gu_ptr->log_id,
                               DBG_LOW_PRIO,
                               "Checking connections of internal port id 0x%lx miid 0x%lx, ext in ptr 0x%p, conn out "
                               "0x%p ",
                               in_port_ptr->cmn.id,
                               in_port_ptr->cmn.module_ptr->module_instance_id,
                               in_port_ptr->conn_out_port_ptr);

                  module_updated = TRUE;

                  if (in_port_ptr->cmn.boundary_port)
                  {
                     WCNTR_GU_MSG(gu_ptr->log_id,
                                  DBG_LOW_PRIO,
                                  SPF_LOG_PREFIX "Removing corresponding  boundary input port node from list");
                     spf_list_find_delete_node((spf_list_node_t **)&in_port_ptr->cmn.module_ptr->sg_ptr
                                                  ->boundary_in_port_list_ptr,
                                               (void *)in_port_ptr,
                                               TRUE /* pool_used */);
                     in_port_ptr->cmn.module_ptr->sg_ptr->num_boundary_in_ports--;
                  }
                  spf_list_delete_node_and_free_obj((spf_list_node_t **)&ip_port_list_ptr,
                                                    (spf_list_node_t **)&module_ptr->input_port_list_ptr,
                                                    TRUE /* pool_used */);
                  module_ptr->num_input_ports--;
                  wcntr_gu_update_is_siso_module(module_ptr);
               }
               else
               {
                  LIST_ADVANCE(ip_port_list_ptr);
               }
            }
            /*            gu_update_port_indices(gu_ptr,
                              (spf_list_node_t*)module_ptr->input_port_list_ptr,
                              GU_PORT_TYPE_INPUT_PORT,
                              module_ptr->num_input_ports);*/

            wcntr_gu_output_port_list_t *op_port_list_ptr = module_ptr->output_port_list_ptr;
            while (op_port_list_ptr)
            {
               wcntr_gu_output_port_t *out_port_ptr = op_port_list_ptr->op_port_ptr;

               if (!out_port_ptr->conn_in_port_ptr)
               {
                  WCNTR_GU_MSG(gu_ptr->log_id,
                               DBG_LOW_PRIO,
                               "Checking connections of internal port id 0x%lx miid 0x%lx, conn in ptr 0x%p",
                               out_port_ptr->cmn.id,
                               out_port_ptr->cmn.module_ptr->module_instance_id,
                               out_port_ptr->conn_in_port_ptr);

                  if (out_port_ptr->cmn.boundary_port)
                  {
                     WCNTR_GU_MSG(gu_ptr->log_id,
                                  DBG_LOW_PRIO,
                                  SPF_LOG_PREFIX "Removing corresponding  boundary input port node from list ");
                     spf_list_find_delete_node((spf_list_node_t **)&out_port_ptr->cmn.module_ptr->sg_ptr
                                                  ->boundary_out_port_list_ptr,
                                               (void *)out_port_ptr,
                                               TRUE /* pool_used */);
                     out_port_ptr->cmn.module_ptr->sg_ptr->num_boundary_out_ports--;
                  }

                  module_updated = TRUE;
                  spf_list_delete_node_and_free_obj((spf_list_node_t **)&op_port_list_ptr,
                                                    (spf_list_node_t **)&module_ptr->output_port_list_ptr,
                                                    TRUE /* pool_used */);
                  module_ptr->num_output_ports--;
                  wcntr_gu_update_is_siso_module(module_ptr);
               }
               else
               {
                  LIST_ADVANCE(op_port_list_ptr);
               }
            }

            wcntr_gu_ctrl_port_list_t *ctrl_port_list_ptr = module_ptr->ctrl_port_list_ptr;
            while (ctrl_port_list_ptr)
            {
               wcntr_gu_ctrl_port_t *ctrl_port_ptr = ctrl_port_list_ptr->ctrl_port_ptr;
               if (!ctrl_port_ptr->peer_ctrl_port_ptr)
               {
                  WCNTR_GU_MSG(gu_ptr->log_id,
                               DBG_LOW_PRIO,
                               "Checking connections of internal port id 0x%lx miid 0x%lx, ext out ptr 0x%p, conn in "
                               "ptr 0x%p ",
                               ctrl_port_ptr->id,
                               ctrl_port_ptr->module_ptr->module_instance_id,
                               ctrl_port_ptr->peer_ctrl_port_ptr);

                  module_updated = TRUE;
                  spf_list_delete_node_and_free_obj((spf_list_node_t **)&ctrl_port_list_ptr,
                                                    (spf_list_node_t **)&module_ptr->ctrl_port_list_ptr,
                                                    TRUE /* pool_used */);
                  module_ptr->num_ctrl_ports--;
               }
               else
               {
                  LIST_ADVANCE(ctrl_port_list_ptr);
               }
            }

            if (module_updated)
            {
               // Update is_connected if module is no longer connected.
               wcntr_gu_update_module_is_connected(module_ptr);

               // Indices of existing ports are retained as is, when one of the ports goes away. No need to update.
               wcntr_gu_set_status(&module_ptr->gu_status, WCNTR_GU_STATUS_UPDATED);
            }

            LIST_ADVANCE(module_list_ptr);
         }
         LIST_ADVANCE(sg_list_ptr);
      }
   }

   //TODO remove this in final check-in
   wcntr_gu_print_boundary_ports(gu_ptr);

   /**
    * if there are more SGs, sort them
    */
   if ((!no_sgs_to_destroy) && gu_ptr->sg_list_ptr)
   {
      wcntr_gu_update_sorted_list(gu_ptr, heap_id);
      // wcntr_gu_print_graph(gu_ptr);
   }
   return result;
}

ar_result_t wcntr_gu_create_graph(wcntr_gu_t *              gu_ptr,
                                  spf_msg_cmd_graph_open_t *open_cmd_ptr,
                                  wcntr_gu_sizes_t *        sizes_ptr,
                                  POSAL_HEAP_ID             heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t         result              = AR_EOK;
   wcntr_gu_sg_t *     sg_ptr              = NULL;
   spf_list_node_t *   amdb_h_list_ptr     = NULL;
   apm_modules_list_t *apm_module_list_ptr = NULL;

   /** create subgraphs & update subgraph properties */
   for (uint32_t i = 0; i < open_cmd_ptr->num_sub_graphs; i++)
   {
      apm_sub_graph_cfg_t *sg_cmd_ptr = open_cmd_ptr->sg_cfg_list_pptr[i];

      sg_ptr = wcntr_gu_find_subgraph(gu_ptr, sg_cmd_ptr->sub_graph_id);
      if (!sg_ptr)
      {
         VERIFY(result, (gu_ptr->num_subgraphs < UINT8_MAX));

         MALLOC_MEMSET(sg_ptr, wcntr_gu_sg_t, MAX(sizes_ptr->sg_size, sizeof(wcntr_gu_sg_t)), heap_id, result);

         sg_ptr->id        = sg_cmd_ptr->sub_graph_id;
         sg_ptr->gu_ptr    = gu_ptr;
         sg_ptr->perf_mode = APM_SG_PERF_MODE_LOW_POWER; // Assume low power by default

         wcntr_gu_set_status(&sg_ptr->gu_status, WCNTR_GU_STATUS_NEW);
         gu_ptr->num_subgraphs++;

         TRY(result,
             spf_list_insert_tail(((spf_list_node_t **)&(gu_ptr->sg_list_ptr)), sg_ptr, heap_id, TRUE /* use_pool*/));
      }

      TRY(result, wcntr_gu_parse_subgraph_props(gu_ptr, sg_ptr, sg_cmd_ptr));

      WCNTR_GU_MSG(gu_ptr->log_id,
                   DBG_HIGH_PRIO,
                   SPF_LOG_PREFIX "CreateGraph: SG id: 0x%08lx, perf_mode %lu, direction %lu, sid 0x%lx",
                   sg_ptr->id,
                   sg_ptr->perf_mode,
                   sg_ptr->direction,
                   sg_ptr->sid);
   }

   /** find the subgraph corresponding to this module list
    *  subgraph may or may not be created by this time. */
   sg_ptr = NULL;
   for (uint32_t i = 0; i < open_cmd_ptr->num_modules_list; i++)
   {
      apm_module_list_ptr = open_cmd_ptr->mod_list_pptr[i];

      if (!apm_module_list_ptr)
      {
         continue;
      }

      // find subgraph by ID
      sg_ptr = wcntr_gu_find_subgraph(gu_ptr, apm_module_list_ptr->sub_graph_id);

      // subgraph must be defined before any modules in it
      VERIFY(result, sg_ptr);

      wcntr_gu_set_status(&sg_ptr->gu_status, WCNTR_GU_STATUS_UPDATED);

      apm_module_cfg_t *cmd_module_ptr = (apm_module_cfg_t *)(apm_module_list_ptr + 1);

      for (uint32_t j = 0; j < apm_module_list_ptr->num_modules; j++)
      {
         // check if module is already in any subgraph; if so, don't create
         wcntr_gu_module_t *module_ptr = wcntr_gu_find_module(gu_ptr, cmd_module_ptr->instance_id);
         if (module_ptr)
         {
            VERIFY(result, module_ptr->module_id == cmd_module_ptr->module_id);
         }
         else
         {
            VERIFY(result,
                   (AR_INVALID_INSTANCE_ID != cmd_module_ptr->instance_id) &&
                      (cmd_module_ptr->instance_id >= AR_DYNAMIC_INSTANCE_ID_RANGE_BEGIN));
            // create modules and store.
            MALLOC_MEMSET(module_ptr,
                          wcntr_gu_module_t,
                          MAX(sizes_ptr->module_size, sizeof(wcntr_gu_module_t)),
                          heap_id,
                          result);
            wcntr_gu_set_status(&module_ptr->gu_status, WCNTR_GU_STATUS_NEW);

            // Note: same pointer is stored at 2 places (sorted module list and sg_ptr->module list).
            TRY(result,
                spf_list_insert_tail(((spf_list_node_t **)&(sg_ptr->module_list_ptr)),
                                     module_ptr,
                                     heap_id,
                                     TRUE /* use_pool*/));

            module_ptr->sg_ptr = sg_ptr;
            sg_ptr->num_modules++;

            module_ptr->module_instance_id = cmd_module_ptr->instance_id;
            module_ptr->module_id          = cmd_module_ptr->module_id;

            TRY(result, wcntr_gu_prepare_for_module_loading(gu_ptr, &amdb_h_list_ptr, module_ptr,heap_id));

            WCNTR_GU_MSG(gu_ptr->log_id,
                         DBG_HIGH_PRIO,
                         SPF_LOG_PREFIX "CreateGraph: Module id: 0x%08lx, Module Instance id: 0x%lx, SG_id: 0x%lx",
                         module_ptr->module_id,
                         module_ptr->module_instance_id,
                         module_ptr->sg_ptr->id);
         }
         cmd_module_ptr++;
      }
   }

   /**  update module properties */
   TRY(result, wcntr_gu_parse_module_props(gu_ptr, open_cmd_ptr, heap_id));

   /**
    * create connections. for every module store src and sink (module, port)
    */
   for (uint32_t i = 0; i < open_cmd_ptr->num_module_conn; i++)
   {
      apm_module_conn_cfg_t *cmd_conn_ptr = open_cmd_ptr->mod_conn_list_pptr[i];

      POSAL_HEAP_ID peer_heap_id = heap_id; // initialize to my heap id

	  TRY(result,
          wcntr_gu_handle_connection(gu_ptr,
                                     cmd_conn_ptr->src_mod_inst_id,
                                     cmd_conn_ptr->src_mod_op_port_id,
                                     cmd_conn_ptr->dst_mod_inst_id,
                                     cmd_conn_ptr->dst_mod_ip_port_id,
                                     sizes_ptr,
                                     heap_id,
                                     peer_heap_id));

      WCNTR_GU_MSG(gu_ptr->log_id,
                   DBG_HIGH_PRIO,
                   SPF_LOG_PREFIX
                   "CreateGraph: Data connection source (0x%lx, 0x%lx), destination (0x%lx, 0x%lx) result %u",
                   cmd_conn_ptr->src_mod_inst_id,
                   cmd_conn_ptr->src_mod_op_port_id,
                   cmd_conn_ptr->dst_mod_inst_id,
                   cmd_conn_ptr->dst_mod_ip_port_id,result);
   }

   /**
    * create control links. for every module store local and peer control port handles.
    */
   for (uint32_t i = 0; i < open_cmd_ptr->num_module_ctrl_links; i++)
   {
      apm_module_ctrl_link_cfg_t *cmd_ctrl_conn_ptr = open_cmd_ptr->mod_ctrl_link_cfg_list_pptr[i];

      TRY(result,
          wcntr_gu_create_control_link(gu_ptr,
                                       cmd_ctrl_conn_ptr,
                                       sizes_ptr,
                                       open_cmd_ptr->imcl_peer_domain_info_pptr,
                                       open_cmd_ptr->num_offloaded_imcl_peers,
                                       heap_id));

      WCNTR_GU_MSG(gu_ptr->log_id,
                   DBG_HIGH_PRIO,
                   SPF_LOG_PREFIX
                   "CreateGraph: Control connection peer1 (0x%lx, 0x%lx), peer2 (0x%lx, 0x%lx)",
                   cmd_ctrl_conn_ptr->peer_1_mod_iid,
                   cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id,
                   cmd_ctrl_conn_ptr->peer_2_mod_iid,
                   cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id);
   }


      /** AMDB loading or finding module type */
   TRY(result, wcntr_gu_handle_module_loading(&amdb_h_list_ptr, gu_ptr->log_id));

   /* Update sorted module list, if sorted module list already existed (means for subsequent graph opens) */
   if (gu_ptr->sorted_module_list_ptr)
   {
      TRY(result, wcntr_gu_update_sorted_list(gu_ptr, heap_id));
      // wcntr_gu_print_graph(gu_ptr);
   }



   wcntr_gu_print_boundary_ports(gu_ptr);

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
      // only the ones created in this create must be destroyed. not everything.
      wcntr_gu_destroy_graph(gu_ptr, NULL, heap_id);
      spf_list_delete_list_and_free_objs(&amdb_h_list_ptr, TRUE /* pool_used */);
   }

   return result;
}

ar_result_t wcntr_gu_respond_to_graph_open(wcntr_gu_t *gu_ptr, spf_msg_t *cmd_msg_ptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t                   result = AR_EOK;
   spf_msg_t                     rsp_msg;
   spf_msg_header_t *            cmd_header_ptr = (spf_msg_header_t *)cmd_msg_ptr->payload_ptr;
   spf_msg_header_t *            rsp_header_ptr;
   spf_cntr_port_connect_info_t *open_rsp_ptr;
   // spf_msg_cmd_graph_open_t *    open_cmd_ptr = (spf_msg_cmd_graph_open_t *)&cmd_header_ptr->payload_start;

   uint32_t host_domain_id;
   __gpr_cmd_get_host_domain_id(&host_domain_id);

   uint32_t rsp_size = sizeof(spf_cntr_port_connect_info_t);

   rsp_size = GET_SPF_MSG_REQ_SIZE(rsp_size);

   TRY(result,
       spf_msg_create_msg(&rsp_msg,
                          &rsp_size,
                          SPF_MSG_RSP_GRAPH_OPEN,
                          cmd_header_ptr->rsp_handle_ptr,
                          &cmd_header_ptr->token,
                          NULL,
                          heap_id));

   rsp_header_ptr                           = (spf_msg_header_t *)rsp_msg.payload_ptr;
   rsp_header_ptr->payload_size             = rsp_size;
   open_rsp_ptr                             = (spf_cntr_port_connect_info_t *)&rsp_header_ptr->payload_start;
   open_rsp_ptr->num_ip_data_port_conn      = 0;
   open_rsp_ptr->num_op_data_port_conn      = 0;
   open_rsp_ptr->num_ctrl_port_conn         = 0;
   open_rsp_ptr->ip_data_port_conn_list_ptr = NULL;
   open_rsp_ptr->op_data_port_conn_list_ptr = NULL;
   open_rsp_ptr->ctrl_port_conn_list_ptr    = NULL;

   TRY(result, spf_msg_send_response(&rsp_msg));

   spf_msg_return_msg(cmd_msg_ptr);

   CATCH(result, WCNTR_GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }
   return result;
}

void wcntr_gu_print_graph(wcntr_gu_t *gu_ptr)
{
   wcntr_gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      wcntr_gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr;
      WCNTR_GU_MSG(gu_ptr->log_id,
                   DBG_MED_PRIO,
                   "SG id: 0x%08lx, num modules %d",
                   sg_list_ptr->sg_ptr->id,
                   sg_list_ptr->sg_ptr->num_modules);
      WCNTR_GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "SG module list:");
      while (module_list_ptr)
      {
         WCNTR_GU_MSG(gu_ptr->log_id,
                      DBG_MED_PRIO,
                      "Module id: 0x%08lx, Module Instance id: 0x%lx, SG_id: 0x%lx",
                      module_list_ptr->module_ptr->module_id,
                      module_list_ptr->module_ptr->module_instance_id,
                      sg_list_ptr->sg_ptr->id);
         LIST_ADVANCE(module_list_ptr);
      }
      LIST_ADVANCE(sg_list_ptr);
   }
   if (gu_ptr->sorted_module_list_ptr)
   {
      WCNTR_GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "Sorted list of modules:");
      wcntr_gu_module_list_t *sorted_list_ptr = gu_ptr->sorted_module_list_ptr;
      while (sorted_list_ptr)
      {
         wcntr_gu_module_t *module_ptr = sorted_list_ptr->module_ptr;
         WCNTR_GU_MSG(gu_ptr->log_id,
                      DBG_MED_PRIO,
                      "Module ID: 0x%08lx, Module Instance id: 0x%08lx, input ports %lu, output ports %lu",
                      module_ptr->module_id,
                      module_ptr->module_instance_id,
                      module_ptr->num_input_ports,
                      module_ptr->num_output_ports);
#ifdef GRAPH_EASY
         WCNTR_GU_MSG(gu_ptr->log_id,
                      DBG_HIGH_PRIO,
                      "GRAPH_EASY: node: { title: \"(0x%x, 0x%x, 0x%x )\"}",
                      gu_ptr->log_id,
                      module_ptr->module_instance_id,
                      module_ptr->module_id);
#endif
         wcntr_gu_output_port_list_t *op_port_list_ptr = module_ptr->output_port_list_ptr;
         while (op_port_list_ptr)
         {
            wcntr_gu_output_port_t *op_port_ptr = op_port_list_ptr->op_port_ptr;

            {
               WCNTR_GU_MSG(gu_ptr->log_id,
                            DBG_MED_PRIO,
                            "Port index %lu id 0x%lX, downstream module id 0x%08lX, downstream module instance id "
                            "0x%08lX, downstream port index %lu, id 0x%lX",
                            op_port_ptr->cmn.index,
                            op_port_ptr->cmn.id,
                            op_port_ptr->conn_in_port_ptr->cmn.module_ptr->module_id,
                            op_port_ptr->conn_in_port_ptr->cmn.module_ptr->module_instance_id,
                            op_port_ptr->conn_in_port_ptr->cmn.index,
                            op_port_ptr->conn_in_port_ptr->cmn.id);
#ifdef GRAPH_EASY
               WCNTR_GU_MSG(gu_ptr->log_id,
                            DBG_HIGH_PRIO,
                            "GRAPH_EASY: edge:  { label: \"(op: 0x%x, ip: 0x%x)\" source: \"(0x%x, 0x%x)\" target: "
                            "\"(0x%x, "
                            "0x%x)\"}",
                            op_port_ptr->cmn.id,
                            op_port_ptr->conn_in_port_ptr->cmn.id,
                            gu_ptr->log_id,
                            module_ptr->module_instance_id,
                            gu_ptr->log_id,
                            op_port_ptr->conn_in_port_ptr->cmn.module_ptr->module_instance_id);
#endif
            }
            LIST_ADVANCE(op_port_list_ptr);
         }

         wcntr_gu_ctrl_port_list_t *ctrl_port_list_ptr = module_ptr->ctrl_port_list_ptr;
         while (ctrl_port_list_ptr)
         {
            wcntr_gu_ctrl_port_t *ctrl_port_ptr = ctrl_port_list_ptr->ctrl_port_ptr;
            {
               WCNTR_GU_MSG(gu_ptr->log_id,
                            DBG_MED_PRIO,
                            SPF_LOG_PREFIX "Control Port id 0x%lX, peer module id 0x%08lX, peer module instance id "
                                           "0x%08lX, peer port id 0x%lX",
                            ctrl_port_ptr->id,
                            ctrl_port_ptr->peer_ctrl_port_ptr->module_ptr->module_id,
                            ctrl_port_ptr->peer_ctrl_port_ptr->module_ptr->module_instance_id,
                            ctrl_port_ptr->peer_ctrl_port_ptr->id);
            }
            LIST_ADVANCE(ctrl_port_list_ptr);
         }
         LIST_ADVANCE(sorted_list_ptr);
      }
   }

   if (gu_ptr->floating_modules_list_ptr)
   {
      WCNTR_GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "list of floating modules:");
      wcntr_gu_module_list_t *floating_modules_list_ptr = gu_ptr->floating_modules_list_ptr;
      while (floating_modules_list_ptr)
      {
         wcntr_gu_module_t *module_ptr = floating_modules_list_ptr->module_ptr;
         WCNTR_GU_MSG(gu_ptr->log_id,
                      DBG_MED_PRIO,
                      "Floating Module ID: 0x%08lx, Module Instance id: 0x%08lx",
                      module_ptr->module_id,
                      module_ptr->module_instance_id);
         LIST_ADVANCE(floating_modules_list_ptr);
      }
   }
}
