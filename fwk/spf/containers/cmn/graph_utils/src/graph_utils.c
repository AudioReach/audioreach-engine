/**
 * \file graph_utils.c
 *
 * \brief
 *
 *     Graph utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "graph_utils.h"
#include "posal.h"
#include "amdb_static.h"
#include "spf_list_utils.h"
#include "wr_sh_mem_ep_api.h"
#include "rd_sh_mem_ep_api.h"
#include "wr_sh_mem_client_api.h"
#include "rd_sh_mem_client_api.h"
#include "spf_macros.h"
#include "gpr_api_inline.h"
#include "offload_apm_api.h"
#include "spf_svc_utils.h"

/** max ports just for bounds check. also internal variables for num ports are 8 bits*/
#define MAX_PORTS 100

#define GU_MSG_PREFIX "GU  :%08lX: "

#define GU_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, GU_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

//#define DEBUG_GRAPH_SORT
//#define DEBUG_MODE_LOG

typedef struct module_amdb_handle_t
{
   amdb_module_handle_info_t amdb_handle;
   gu_module_t *             module_ptr;
} module_amdb_handle_t;

static ar_result_t gu_update_module_sink_src_info(gu_t *gu_ptr, gu_module_t *module_ptr);
static ar_result_t gu_attach_module(gu_t *gu_ptr, gu_output_port_t *prev_con_op_ptr, POSAL_HEAP_ID heap_id);

static void gu_update_is_siso_module(gu_module_t *module_ptr)
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
static void gu_set_status(gu_status_t *status_to_update, gu_status_t status_value)
{
   // Reject the status update if moving from NEW to UPDATED.
   if ((GU_STATUS_NEW == *status_to_update) && (GU_STATUS_UPDATED == status_value))
   {
      return;
   }
   *status_to_update = status_value;
}

static gu_cmn_port_t *gu_find_port_by_id(spf_list_node_t *list_ptr, uint32_t id)
{
   while (list_ptr)
   {
      if (list_ptr->obj_ptr)
      {
         gu_cmn_port_t *port_ptr = (gu_cmn_port_t *)list_ptr->obj_ptr;
         if (port_ptr->id == id)
         {
            return port_ptr;
         }
      }
      LIST_ADVANCE(list_ptr);
   }
   return NULL;
}

// Find if thre is any port (module_ptr, id) in the pending list
static gu_cmn_port_t *gu_find_pending_port(gu_t *gu_ptr, gu_module_t *module_ptr, uint32_t id)
{
   if (!module_ptr || !gu_ptr->async_gu_ptr)
   {
      return NULL;
   }

   for (spf_list_node_t *pending_port_list_ptr = (spf_list_node_t *)gu_ptr->async_gu_ptr->port_list_ptr;
        pending_port_list_ptr;
        LIST_ADVANCE(pending_port_list_ptr))
   {
      gu_cmn_port_t *cmn_port_ptr = (gu_cmn_port_t *)pending_port_list_ptr->obj_ptr;
      if (id == cmn_port_ptr->id && module_ptr == cmn_port_ptr->module_ptr)
      {
         return cmn_port_ptr;
      }
   }

   return NULL;
}

gu_output_port_t *gu_find_output_port(gu_module_t *module_ptr, uint32_t id)
{
   if (!module_ptr)
   {
      return NULL;
   }

   return (gu_output_port_t *)gu_find_port_by_id((spf_list_node_t *)module_ptr->output_port_list_ptr, id);
}

gu_input_port_t *gu_find_input_port(gu_module_t *module_ptr, uint32_t id)
{
   if (!module_ptr)
   {
      return NULL;
   }

   return (gu_input_port_t *)gu_find_port_by_id((spf_list_node_t *)module_ptr->input_port_list_ptr, id);
}

gu_ext_in_port_t *gu_get_ext_in_port_from_cmn_port(gu_cmn_port_t *cmn_port_ptr)
{
   if (gu_is_output_port_id(cmn_port_ptr->id))
   {
      return NULL;
   }
   gu_input_port_t *in_port_ptr = (gu_input_port_t *)cmn_port_ptr;
   return in_port_ptr->ext_in_port_ptr;
}

gu_ext_out_port_t *gu_get_ext_out_port_from_cmn_port(gu_cmn_port_t *cmn_port_ptr)
{
   if (!gu_is_output_port_id(cmn_port_ptr->id))
   {
      return NULL;
   }
   gu_output_port_t *out_port_ptr = (gu_output_port_t *)cmn_port_ptr;
   return out_port_ptr->ext_out_port_ptr;
}

gu_cmn_port_t *gu_find_cmn_port_by_id(gu_module_t *module_ptr, uint32_t id)
{
   if (gu_is_output_port_id(id))
   {
      return (gu_cmn_port_t *)gu_find_output_port(module_ptr, id);
   }
   else
   {
      return (gu_cmn_port_t *)gu_find_input_port(module_ptr, id);
   }
}

static ar_result_t gu_move_port_to_smallest_available_index(gu_module_t *     module_ptr,
                                                            spf_list_node_t **list_pptr,
                                                            gu_cmn_port_t *   new_port_ptr)
{
   ar_result_t      result          = AR_EOK;
   uint32_t         i               = 0;
   uint32_t         available_index = 0;
   spf_list_node_t *curr_ptr = NULL, *prev_ptr = NULL, *index_found_ptr = NULL, *new_node_ptr = NULL;

   for (curr_ptr = *list_pptr; (NULL != curr_ptr); LIST_ADVANCE(curr_ptr), i++)
   {
      gu_cmn_port_t *port_ptr = (gu_cmn_port_t *)curr_ptr->obj_ptr;

      // new_port_ptr is at the tail; it has not been assigned an index yet.
      if (port_ptr != new_port_ptr)
      {
         // if prev exists & difference of current and previous indices is more than one,
         // then available index is prev + 1.
         // if prev doesn't exist (first time) & then check if first port's index is far from zero.
         if (prev_ptr)
         {
            if ((port_ptr->index - ((gu_cmn_port_t *)prev_ptr->obj_ptr)->index) > 1)
            {
               available_index = ((gu_cmn_port_t *)prev_ptr->obj_ptr)->index + 1;
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

// function to allocate a sg resource
static ar_result_t gu_sg_allocate_resource(gu_sg_t *                 sg_ptr,
                                           uint32_t                  size,
                                           gu_sg_mem_resource_type_t type,
                                           POSAL_HEAP_ID             heap_id)
{
   gu_sg_mem_resource_t *ptr = NULL;

   if (!sg_ptr)
   {
      return AR_EFAILED;
   }

   if (0 == size)
   {
      return AR_EOK;
   }

   ptr = (gu_sg_mem_resource_t *)posal_memory_malloc(size + ALIGN_8_BYTES(sizeof(gu_sg_mem_resource_t)), heap_id);
   if (!ptr)
   {
      return AR_ENOMEMORY;
   }

   ptr->curr_ptr           = (int8_t *)ptr + ALIGN_8_BYTES(sizeof(gu_sg_mem_resource_t));
   ptr->available_mem_size = size;
   ptr->type               = type;

   // insert the module memory pointer to the resources list.
   return spf_list_insert_tail(((spf_list_node_t **)&(sg_ptr->resources_ptr)), ptr, heap_id, TRUE /* use_pool*/);
}

// function to get memory block from the sg resource.
static void *gu_sg_get_resource_mem(gu_sg_t *sg_ptr, uint32_t size, gu_sg_mem_resource_type_t type)
{
   if (!sg_ptr)
   {
      return NULL;
   }

   spf_list_node_t *sg_resource_node_ptr = sg_ptr->resources_ptr;
   void *           mem_ptr              = NULL;

   while (sg_resource_node_ptr)
   {
      gu_sg_mem_resource_t *sg_resource_ptr = sg_resource_node_ptr->obj_ptr;

      if (sg_resource_ptr->type == type)
      {
         if (sg_resource_ptr->available_mem_size >= ALIGN_8_BYTES(size))
         {
            mem_ptr = (void *)sg_resource_ptr->curr_ptr;
            sg_resource_ptr->available_mem_size -= ALIGN_8_BYTES(size);
            sg_resource_ptr->curr_ptr += ALIGN_8_BYTES(size);
            return mem_ptr;
         }
      }

      LIST_ADVANCE(sg_resource_node_ptr);
   }

   return mem_ptr;
}

// function to check if pointer is assigned from sg memory resource or not.
static bool_t gu_sg_is_resource_memory(gu_sg_t *sg_ptr, int8_t *ptr)
{
   spf_list_node_t *sg_resource_node_ptr = sg_ptr->resources_ptr;

   while (sg_resource_node_ptr)
   {
      gu_sg_mem_resource_t *sg_resource_ptr = sg_resource_node_ptr->obj_ptr;
      int8_t *              start_ptr       = (int8_t *)sg_resource_ptr;
      int8_t *              end_ptr         = sg_resource_ptr->curr_ptr;

      if (ptr > start_ptr && ptr < end_ptr)
      {
         return TRUE;
      }

      LIST_ADVANCE(sg_resource_node_ptr);
   }
   return FALSE;
}

static ar_result_t gu_create_data_port(gu_t *          gu_ptr,
                                       gu_module_t *   module_ptr,
                                       uint32_t        id,
                                       uint32_t        size,
                                       gu_cmn_port_t **port_pptr,
                                       POSAL_HEAP_ID   heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   gu_cmn_port_t *port_ptr = *port_pptr;

   if (NULL == port_ptr)
   {
      // Allocate port
      MALLOC_MEMSET(port_ptr, gu_cmn_port_t, size, heap_id, result);
   }
   else
   {
      // port is already allocated, just memeset.
      memset(port_ptr, 0, size);
   }

   gu_set_status(&port_ptr->gu_status, GU_STATUS_NEW);

   port_ptr->id         = id;
   port_ptr->module_ptr = module_ptr;

   *port_pptr = port_ptr;

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }
   return result;
}

static ar_result_t gu_insert_data_port(gu_t *            gu_ptr,
                                       gu_module_t *     module_ptr,
                                       spf_list_node_t **list_pptr,
                                       gu_cmn_port_t *   port_ptr,
                                       POSAL_HEAP_ID     heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   // Add to list
   TRY(result, spf_list_insert_tail(list_pptr, port_ptr, heap_id, TRUE /* use_pool*/))

   // Move it to right position based on index (sorted order)
   TRY(result, gu_move_port_to_smallest_available_index(module_ptr, list_pptr, port_ptr));

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }
   return result;
}

static ar_result_t gu_create_ctrl_port(gu_t *           gu_ptr,
                                       gu_module_t *    module_ptr,
                                       uint32_t         id,
                                       uint32_t         size,
                                       gu_ctrl_port_t **ctrl_port_pptr,
                                       POSAL_HEAP_ID    operating_heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t     result = AR_EOK;
   gu_ctrl_port_t *port_ptr;

   // Allocate port
   MALLOC_MEMSET(port_ptr, gu_ctrl_port_t, size, operating_heap_id, result); // allocate object in operating_heap_id
   gu_set_status(&port_ptr->gu_status, GU_STATUS_NEW);

   port_ptr->id                = id;
   port_ptr->operating_heap_id = operating_heap_id;
   port_ptr->module_ptr        = module_ptr;

   VERIFY(result, (module_ptr->num_ctrl_ports <= MAX_PORTS));

   // Assign the return pointer.
   *ctrl_port_pptr = port_ptr;

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

static ar_result_t gu_insert_output_port(gu_t *            gu_ptr,
                                         gu_module_t *     module_ptr,
                                         gu_output_port_t *output_port_ptr,
                                         POSAL_HEAP_ID     heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   TRY(result,
       gu_insert_data_port(gu_ptr,
                           module_ptr,
                           ((spf_list_node_t **)&module_ptr->output_port_list_ptr),
                           (gu_cmn_port_t *)output_port_ptr,
                           heap_id));

   module_ptr->num_output_ports++;

   // Mark module and sg as updated
   gu_set_status(&module_ptr->gu_status, GU_STATUS_UPDATED);
   gu_set_status(&module_ptr->sg_ptr->gu_status, GU_STATUS_UPDATED);

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }
   return result;
}

static ar_result_t gu_insert_input_port(gu_t *           gu_ptr,
                                        gu_module_t *    module_ptr,
                                        gu_input_port_t *input_port_ptr,
                                        POSAL_HEAP_ID    heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   TRY(result,
       gu_insert_data_port(gu_ptr,
                           module_ptr,
                           ((spf_list_node_t **)&module_ptr->input_port_list_ptr),
                           (gu_cmn_port_t *)input_port_ptr,
                           heap_id));

   module_ptr->num_input_ports++;

   // Mark module and sg as updated
   gu_set_status(&module_ptr->gu_status, GU_STATUS_UPDATED);
   gu_set_status(&module_ptr->sg_ptr->gu_status, GU_STATUS_UPDATED);

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }
   return result;
}

static ar_result_t gu_parse_subgraph_props(gu_t *gu_ptr, gu_sg_t *sg_ptr, apm_sub_graph_cfg_t *sg_cmd_ptr)
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
         case APM_SUBGRAPH_PROP_ID_CLOCK_SCALE_FACTOR:
         {

            VERIFY(result, sg_prop_ptr->prop_size <= sizeof(apm_subgraph_prop_id_clock_scale_factor_t));

            apm_subgraph_prop_id_clock_scale_factor_t *duty_cycling_mode_ptr =
               (apm_subgraph_prop_id_clock_scale_factor_t *)(sg_prop_ptr + 1);

            VERIFY(result,
                   ((0 == duty_cycling_mode_ptr->enable_duty_cycling) ||
                    (1 == duty_cycling_mode_ptr->enable_duty_cycling)));

            sg_ptr->duty_cycling_mode_enabled          = (bool_t)duty_cycling_mode_ptr->enable_duty_cycling;
            sg_ptr->duty_cycling_clock_scale_factor_q4 = duty_cycling_mode_ptr->duty_cycling_clock_scale_factor_q4;
            sg_ptr->clock_scale_factor_q4              = duty_cycling_mode_ptr->clock_scale_factor_q4;

            break;
         }
         case APM_SUBGRAPH_PROP_ID_BW_SCALE_FACTOR:
         {
            VERIFY(result, sg_prop_ptr->prop_size <= sizeof(apm_subgraph_prop_id_bw_scale_factor_t));

            apm_subgraph_prop_id_bw_scale_factor_t *bw_scale_factor_ptr =
               (apm_subgraph_prop_id_bw_scale_factor_t *)(sg_prop_ptr + 1);
            sg_ptr->bus_scale_factor_q4 = bw_scale_factor_ptr->bus_scale_factor_q4;
            break;
         }
         default:
         {
            GU_MSG(gu_ptr->log_id,
                   DBG_HIGH_PRIO,
                   "WARNING: Unsupported subgraph property, 0x%X, ignoring",
                   sg_prop_ptr->prop_id);
         }
      }

      sg_prop_ptr = (apm_prop_data_t *)(((uint8_t *)(sg_prop_ptr + 1)) + sg_prop_ptr->prop_size);
   }

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

static ar_result_t gu_parse_ctrl_port_props(gu_t *                      gu_ptr,
                                            gu_ctrl_port_t *            ctrl_port_ptr,
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
      switch (link_prop_ptr->prop_id)
      {
         case APM_MODULE_PROP_ID_CTRL_LINK_INTENT_LIST:
         {
            if (cur_memory_offset >= max_ctrl_port_size)
            {
               THROW(result, AR_EFAILED);
            }

            VERIFY(result, link_prop_ptr->prop_size >= sizeof(apm_module_ctrl_link_prop_id_intent_list_t));

            apm_module_ctrl_link_prop_id_intent_list_t *intent_info_ptr =
               (apm_module_ctrl_link_prop_id_intent_list_t *)(link_prop_ptr + 1);

            ctrl_port_ptr->intent_info_ptr =
               (gu_ctrl_port_prop_intent_list_t *)((int8_t *)ctrl_port_ptr + cur_memory_offset);

            ctrl_port_ptr->intent_info_ptr->num_intents = intent_info_ptr->num_intents;

            for (uint32_t i = 0; i < intent_info_ptr->num_intents; i++)
            {
               ctrl_port_ptr->intent_info_ptr->intent_id_list[i] = intent_info_ptr->intent_id_list[i];
            }

            cur_memory_offset +=
               (sizeof(gu_ctrl_port_prop_intent_list_t) + (sizeof(uint32_t) * intent_info_ptr->num_intents));

            break;
         }
         default:
         {
            break;
         }
      }

      link_prop_ptr = (apm_prop_data_t *)(((uint8_t *)(link_prop_ptr + 1)) + link_prop_ptr->prop_size);
   }

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

// function get operating heap id for this control port connection
static ar_result_t gu_get_configured_heap_id_ctrl_port(gu_t *                      gu_ptr,
                                                       apm_module_ctrl_link_cfg_t *ctrl_link_cfg_ptr,
                                                       POSAL_HEAP_ID *             heap_id_ptr)
{
   POSAL_HEAP_ID heap_id_token = GET_TRACKING_ID_FROM_HEAP_ID(*heap_id_ptr);

   // set the configured heap id to default (non-island) heap
   POSAL_HEAP_ID configured_heap_id = (POSAL_HEAP_ID)MODIFY_HEAP_ID_FOR_MEM_TRACKING(heap_id_token, POSAL_HEAP_DEFAULT);

   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   // determine operating heap id based on link property.
   apm_prop_data_t *link_prop_ptr = (apm_prop_data_t *)(ctrl_link_cfg_ptr + 1);
   for (uint32_t j = 0; j < ctrl_link_cfg_ptr->num_props; j++)
   {
      switch (link_prop_ptr->prop_id)
      {
         case APM_MODULE_PROP_ID_CTRL_LINK_HEAP_ID:
         {
            VERIFY(result, link_prop_ptr->prop_size >= sizeof(apm_module_prop_id_ctrl_link_heap_id_t));

            apm_module_prop_id_ctrl_link_heap_id_t *heap_info_ptr =
               (apm_module_prop_id_ctrl_link_heap_id_t *)(link_prop_ptr + 1);

            switch (heap_info_ptr->heap_id)
            {
               case APM_HEAP_ID_LOW_POWER:
               {
                  configured_heap_id = (POSAL_HEAP_ID)MODIFY_HEAP_ID_FOR_MEM_TRACKING(heap_id_token,
                                                                                      GET_HEAP_ID_WITH_ISLAND_INFO(
                                                                                         posal_get_island_heap_id()));
                  break;
               }
               case APM_HEAP_ID_DEFAULT:
               { // already set to default heap
                  break;
               }
               default:
               {
                  GU_MSG(gu_ptr->log_id, DBG_ERROR_PRIO, "got invalid heap id configuration. using default heap");
                  break;
               }
            }
            break;
         }
         default:
         {
            break;
         }
      }

      link_prop_ptr = (apm_prop_data_t *)(((uint8_t *)(link_prop_ptr + 1)) + link_prop_ptr->prop_size);
   }

   GU_MSG(gu_ptr->log_id,
          DBG_MED_PRIO,
          "control link between MIID 0x%x and MIID 0x%x: "
          "configured_heap_id 0x%x",
          ctrl_link_cfg_ptr->peer_1_mod_iid,
          ctrl_link_cfg_ptr->peer_2_mod_iid,
          configured_heap_id);

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id){}

      *heap_id_ptr = configured_heap_id;
   return AR_EOK;
}

static uint32_t gu_get_required_ctrl_port_additional_size(gu_t *gu_ptr, apm_module_ctrl_link_cfg_t *ctrl_link_cfg_ptr)
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
               (sizeof(gu_ctrl_port_prop_intent_list_t) + (sizeof(uint32_t) * intent_info_ptr->num_intents));

            break;
         }
         default:
         {
            break;
         }
      }

      link_prop_ptr = (apm_prop_data_t *)(((uint8_t *)(link_prop_ptr + 1)) + link_prop_ptr->prop_size);
   }

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return additional_size;
}

static ar_result_t gu_parse_module_props(gu_t *gu_ptr, spf_msg_cmd_graph_open_t *open_cmd_ptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   for (uint32_t i = 0; i < open_cmd_ptr->num_mod_prop_cfg; i++)
   {
      apm_module_prop_cfg_t *apm_module_prop_ptr = open_cmd_ptr->mod_prop_cfg_list_pptr[i];

      gu_module_t *module_ptr = gu_find_module(gu_ptr, apm_module_prop_ptr->instance_id);
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

               if (MODULE_ID_WR_SHARED_MEM_EP == module_ptr->module_id)
               {
                  VERIFY(result, 1 >= port_info_ptr->max_ip_port);
                  VERIFY(result, 1 == port_info_ptr->max_op_port);
               }
               else if (MODULE_ID_RD_SHARED_MEM_EP == module_ptr->module_id)
               {
                  VERIFY(result, 1 == port_info_ptr->max_ip_port);
                  VERIFY(result, 1 >= port_info_ptr->max_op_port);
               }
               else if ((MODULE_ID_WR_SHARED_MEM_CLIENT == module_ptr->module_id))
               {
                  VERIFY(result, 1 == port_info_ptr->max_ip_port);
                  VERIFY(result, 1 == port_info_ptr->max_op_port);
                  module_ptr->max_input_ports  = port_info_ptr->max_ip_port;
                  module_ptr->max_output_ports = 0;
               }
               else if ((MODULE_ID_RD_SHARED_MEM_CLIENT == module_ptr->module_id))
               {
                  VERIFY(result, 1 == port_info_ptr->max_ip_port);
                  VERIFY(result, 1 == port_info_ptr->max_op_port);
                  module_ptr->max_input_ports  = 0;
                  module_ptr->max_output_ports = port_info_ptr->max_op_port;
               }
               else
               {
                  // Verify that number of ports is contained (as it's used for mem allocs)
                  VERIFY(result,
                         (port_info_ptr->max_ip_port <= MAX_PORTS) && (port_info_ptr->max_op_port <= MAX_PORTS));
                  module_ptr->max_input_ports  = port_info_ptr->max_ip_port;
                  module_ptr->max_output_ports = port_info_ptr->max_op_port;
               }

               break;
            }
            case APM_MODULE_PROP_ID_HEAP_ID:
            {
               apm_module_prop_id_heap_id_t *module_heap_id_ptr = (apm_module_prop_id_heap_id_t *)(module_prop_ptr + 1);

               POSAL_HEAP_ID heap_id_token = GET_TRACKING_ID_FROM_HEAP_ID(heap_id);

               switch (module_heap_id_ptr->heap_id)
               {
                  case APM_HEAP_ID_DEFAULT:
                  {
                     if (POSAL_IS_ISLAND_HEAP_ID(heap_id))
                     {
                        AR_MSG(DBG_ERROR_PRIO, "Island container does not support module in default power");
                        THROW(result, AR_EUNSUPPORTED);
                     }
                     else
                     {
                        module_ptr->module_heap_id = posal_get_heap_id(POSAL_MEM_TYPE_DEFAULT);
                     }
                     break;
                  }
                  case APM_HEAP_ID_LOW_POWER:
                  {
                     if (POSAL_IS_ISLAND_HEAP_ID(heap_id))
                     {
                        module_ptr->module_heap_id = posal_get_heap_id(POSAL_MEM_TYPE_LOW_POWER);
                     }
                     else
                     {
                        AR_MSG(DBG_ERROR_PRIO, "Default container does not support module in low power");
                        THROW(result, AR_EUNSUPPORTED);
                     }
                     break;
                  }
                  case APM_HEAP_ID_LOW_POWER_2:
                  {
                     if (POSAL_IS_ISLAND_HEAP_ID(heap_id))
                     {
                        module_ptr->module_heap_id = posal_get_heap_id(POSAL_MEM_TYPE_LOW_POWER_2);
                     }
                     else
                     {
                        AR_MSG(DBG_ERROR_PRIO, "Default container does not support module in lower power 2");
                        THROW(result, AR_EUNSUPPORTED);
                     }
                     break;
                  }
               }

               module_ptr->module_heap_id =
                  (POSAL_HEAP_ID)MODIFY_HEAP_ID_FOR_MEM_TRACKING(heap_id_token, module_ptr->module_heap_id);

               GU_MSG(gu_ptr->log_id,
                      DBG_HIGH_PRIO,
                      "APM_MODULE_PROP_ID_HEAP_ID: Module_id: 0x%lx - heap_id: 0x%lx",
                      module_ptr->module_id,
                      module_ptr->module_heap_id);
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
   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

static void gu_check_and_release_amdb_handle(gu_module_t *module_ptr)
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

ar_result_t gu_prepare_for_module_loading(gu_t *            gu_ptr,
                                          spf_list_node_t **amdb_h_list_pptr,
                                          gu_module_t *     module_ptr,
                                          bool_t            is_placeholder_replaced,
                                          uint32_t          real_module_id,
                                          POSAL_HEAP_ID     heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   module_amdb_handle_t *amdb_handle_ptr = NULL;
   MALLOC_MEMSET(amdb_handle_ptr, module_amdb_handle_t, sizeof(module_amdb_handle_t), heap_id, result);

   TRY(result, spf_list_insert_tail(amdb_h_list_pptr, amdb_handle_ptr, heap_id, TRUE /* use_pool*/));

   amdb_handle_ptr->amdb_handle.module_id  = (TRUE == is_placeholder_replaced) ? real_module_id : module_ptr->module_id;
   amdb_handle_ptr->amdb_handle.handle_ptr = NULL;
   amdb_handle_ptr->amdb_handle.result     = AR_EFAILED;
   amdb_handle_ptr->module_ptr             = module_ptr;

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t gu_handle_module_loading(spf_list_node_t **amdb_h_list_pptr, uint32_t log_id)
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
         GU_MSG(log_id,
                DBG_ERROR_PRIO,
                "AMDB query failed, result 0x%lx (must be 0), interface type 0x%lx (must be 2 or 3)",
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
         gu_check_and_release_amdb_handle(amdb_handle_ptr->module_ptr);

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

   CATCH(result, GU_MSG_PREFIX, log_id)
   {
   }

   return result;
}

bool_t gu_check_if_peer_miid_is_on_a_remote_proc(apm_imcl_peer_domain_info_t **imcl_peer_domain_info_pptr,
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

/*If the connection is across containers, we need to check if the peer container
     uses a different heap for its allocations */
static ar_result_t gu_check_ext_ctrl_conn_and_get_peer_heap_id(uint32_t                  log_id,
                                                               spf_msg_cmd_graph_open_t *open_cmd_ptr,
                                                               uint32_t                  peer_1_mod_inst_id,
                                                               uint32_t                  peer_2_mod_inst_id,
                                                               POSAL_HEAP_ID *           heap_id_ptr)
{
   if (!open_cmd_ptr || !open_cmd_ptr->num_mxd_heap_id_ctrl_links)
   {
      return AR_EOK;
   }

   /** create subgraphs & update subgraph properties */
   for (uint32_t i = 0; i < open_cmd_ptr->num_mxd_heap_id_ctrl_links; i++)
   {
      apm_mxd_heap_id_link_cfg_t *cfg_ptr  = open_cmd_ptr->mxd_heap_id_ctrl_links_cfg_pptr[i];
      apm_module_ctrl_link_cfg_t *conn_ptr = (apm_module_ctrl_link_cfg_t *)cfg_ptr->conn_ptr;
      if ((peer_1_mod_inst_id == conn_ptr->peer_1_mod_iid) && (peer_2_mod_inst_id == conn_ptr->peer_2_mod_iid))
      {
         *heap_id_ptr =
            (POSAL_HEAP_ID)MODIFY_HEAP_ID_FOR_MEM_TRACKING(*heap_id_ptr,
                                                           gu_get_heap_id_from_heap_prop(cfg_ptr->heap_id.heap_id));

         GU_MSG(log_id,
                DBG_HIGH_PRIO,
                "Got external control connection between miids 0x%lx and 0x%lx with peer heap id = %lu",
                peer_1_mod_inst_id,
                peer_2_mod_inst_id,
                (uint32_t)*heap_id_ptr);
      }
   }
   return AR_EOK;
}

static ar_result_t gu_create_control_link(gu_t *                        gu_ptr_,
                                          apm_module_ctrl_link_cfg_t *  cmd_ctrl_conn_ptr,
                                          gu_sizes_t *                  sizes_ptr,
                                          apm_imcl_peer_domain_info_t **imcl_peer_domain_info_pptr,
                                          uint32_t                      num_offloaded_imcl_peers,
                                          POSAL_HEAP_ID                 heap_id,
                                          POSAL_HEAP_ID                 operating_heap_id)
{

   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   ar_result_t result = AR_EOK;

   // open_gu_ptr: it is main gu_ptr if asynchronous open is not going on.
   // open_gu_ptr: it is async gu_ptr if asynchronous open is going on
   gu_t *open_gu_ptr = get_gu_ptr_for_current_command_context(gu_ptr_);

   gu_module_t *   peer_mod_ptr[2]          = { NULL };
   uint32_t        additional_size          = 0;
   uint32_t        peer_mod_ctrl_port_id[2] = { cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id,
                                         cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id };
   gu_ctrl_port_t *peer_ctrl_port_ptr[2]    = { NULL };
   // bool_t          peer_port_found       = FALSE;
   uint32_t host_domain_id, remote_domain_id;
   __gpr_cmd_get_host_domain_id(&host_domain_id);
   remote_domain_id = host_domain_id; // AKR: Initialized to same-proc IMCL

   peer_mod_ptr[0] = gu_find_module(open_gu_ptr, cmd_ctrl_conn_ptr->peer_1_mod_iid);
   peer_mod_ptr[1] = gu_find_module(open_gu_ptr, cmd_ctrl_conn_ptr->peer_2_mod_iid);
   if (open_gu_ptr != gu_ptr_)
   {
      if (!peer_mod_ptr[0])
      {
         peer_mod_ptr[0] = gu_find_module(gu_ptr_, cmd_ctrl_conn_ptr->peer_1_mod_iid);
      }

      if (!peer_mod_ptr[1])
      {
         peer_mod_ptr[1] = gu_find_module(gu_ptr_, cmd_ctrl_conn_ptr->peer_2_mod_iid);
      }
   }

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

   /** If the module or the port is part of this open_gu_ptr (container),
    *  then it's internal connection, otherwise, it's an external connection. */

   // Create internal port structures for both the peer1 and peer2.
   additional_size = gu_get_required_ctrl_port_additional_size(open_gu_ptr, cmd_ctrl_conn_ptr);
   for (uint32_t iter = 0; iter < 2; iter++)
   {
      if (peer_mod_ptr[iter])
      {
         if (gu_find_ctrl_port_by_id(peer_mod_ptr[iter], peer_mod_ctrl_port_id[iter]))
         {
            THROW(result, AR_EDUPLICATE);
         }

         uint32_t base_ctrl_port_size = MAX(sizes_ptr->ctrl_port_size, sizeof(gu_ctrl_port_t));

         TRY(result,
             gu_create_ctrl_port(open_gu_ptr,
                                 peer_mod_ptr[iter],
                                 peer_mod_ctrl_port_id[iter],
                                 base_ctrl_port_size + additional_size,
                                 &peer_ctrl_port_ptr[iter],
                                 operating_heap_id));

         // Parse ctrl link properties from the graph open cmd.
         gu_parse_ctrl_port_props(open_gu_ptr,
                                  peer_ctrl_port_ptr[iter],
                                  cmd_ctrl_conn_ptr,
                                  base_ctrl_port_size + additional_size,
                                  base_ctrl_port_size);
      }
   }

   /** if src and dst modules are NULL, then they don't belong to this container or are not created yet.
    *  if src is non-NULL, dst is NULL, then the src is the ext output port = output module
    *  if src is NULL, dst is non-NULL, then the dst is the ext input port = input module
    *  if src and dst modules are both non-NULL, then both belong to the same container */
   if (peer_mod_ptr[0] && peer_mod_ptr[1])
   {
      // There should not be any existing connection because we are creating new connection
      if (peer_ctrl_port_ptr[0]->peer_ctrl_port_ptr || peer_ctrl_port_ptr[1]->peer_ctrl_port_ptr)
      {
         THROW(result, AR_EDUPLICATE);
      }

      peer_ctrl_port_ptr[0]->peer_ctrl_port_ptr = peer_ctrl_port_ptr[1];
      peer_ctrl_port_ptr[1]->peer_ctrl_port_ptr = peer_ctrl_port_ptr[0];
      peer_ctrl_port_ptr[0]->ext_ctrl_port_ptr  = NULL;
      peer_ctrl_port_ptr[1]->ext_ctrl_port_ptr  = NULL;
   }
   else if (!peer_mod_ptr[0] && !peer_mod_ptr[1]) // If both are NULL.
   {
      GU_MSG(open_gu_ptr->log_id, DBG_HIGH_PRIO, "Control link doesn't belong to this container");
   }
   else if (peer_mod_ptr[0] || peer_mod_ptr[1]) // if one of the peer module is not present in this container.
   {
      gu_ctrl_port_t *loc_peer_ctrl_port_ptr =
         (peer_ctrl_port_ptr[0] == NULL) ? peer_ctrl_port_ptr[1] : peer_ctrl_port_ptr[0];

      uint32_t local_peer_mod_iid =
         (peer_mod_ptr[0] == NULL) ? cmd_ctrl_conn_ptr->peer_2_mod_iid : cmd_ctrl_conn_ptr->peer_1_mod_iid;
      uint32_t remote_peer_mod_iid =
         (peer_mod_ptr[0] == NULL) ? cmd_ctrl_conn_ptr->peer_1_mod_iid : cmd_ctrl_conn_ptr->peer_2_mod_iid;
      uint32_t remote_peer_port_id = (peer_mod_ptr[0] == NULL) ? cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id
                                                               : cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id;

      // now, we need to figure out if our remote peer is in the same domain or if this is an inter-domain link
      if (gu_check_if_peer_miid_is_on_a_remote_proc(imcl_peer_domain_info_pptr,
                                                    num_offloaded_imcl_peers,
                                                    remote_peer_mod_iid,
                                                    &remote_domain_id))
      {
         GU_MSG(open_gu_ptr->log_id,
                DBG_HIGH_PRIO,
                "Creating an inter-domain ctrl link between miid [0x%lx] in host domain %lu "
                "with miid [0x%lx] in peer domain %lu",
                local_peer_mod_iid,
                host_domain_id,
                remote_peer_mod_iid,
                remote_domain_id);
      }

      if (loc_peer_ctrl_port_ptr->ext_ctrl_port_ptr || loc_peer_ctrl_port_ptr->peer_ctrl_port_ptr)
      {
         THROW(result, AR_EDUPLICATE);
      }

      {
         gu_ext_ctrl_port_t *ext_ctrl_port_ptr = NULL;

         loc_peer_ctrl_port_ptr->ext_ctrl_port_ptr = NULL;

         MALLOC_MEMSET(ext_ctrl_port_ptr,
                       gu_ext_ctrl_port_t,
                       MAX(sizes_ptr->ext_ctrl_port_size, sizeof(gu_ext_ctrl_port_t)),
                       operating_heap_id, // allocate object in operating heap_id
                       result);

         gu_set_status(&ext_ctrl_port_ptr->gu_status, GU_STATUS_NEW);

         ext_ctrl_port_ptr->peer_handle.module_instance_id = remote_peer_mod_iid;
         ext_ctrl_port_ptr->peer_handle.port_id            = remote_peer_port_id;
         ext_ctrl_port_ptr->peer_domain_id                 = remote_domain_id; // same as host if not offloaded

         // storing peer_heap id as operating heap_id
         ext_ctrl_port_ptr->peer_handle.heap_id = operating_heap_id;

         // handle, this_handle, is populated by container
         ext_ctrl_port_ptr->sg_ptr = loc_peer_ctrl_port_ptr->module_ptr->sg_ptr;

         gu_init_ext_ctrl_port(ext_ctrl_port_ptr, loc_peer_ctrl_port_ptr);

         VERIFY(result, (open_gu_ptr->num_ext_ctrl_ports < UINT8_MAX));

         TRY(result,
             spf_list_insert_tail(((spf_list_node_t **)&(open_gu_ptr->ext_ctrl_port_list_ptr)),
                                  ext_ctrl_port_ptr,
                                  heap_id, // allocate node in heap_id
                                  TRUE /* use_pool*/));
         open_gu_ptr->num_ext_ctrl_ports++;
      }
   }

   // insert the control port to the module
   for (uint32_t iter = 0; iter < 2; iter++)
   {
      if (peer_mod_ptr[iter] && peer_ctrl_port_ptr[iter])
      {
         SPF_CRITICAL_SECTION_START(open_gu_ptr);

         TRY(result,
             spf_list_insert_tail((spf_list_node_t **)&peer_mod_ptr[iter]->ctrl_port_list_ptr,
                                  peer_ctrl_port_ptr[iter],
                                  heap_id,
                                  TRUE /* use_pool*/)) // allocate node in heap_id
         peer_mod_ptr[iter]->num_ctrl_ports++;

         // Mark module and sg as updated
         gu_set_status(&peer_mod_ptr[iter]->gu_status, GU_STATUS_UPDATED);
         gu_set_status(&peer_mod_ptr[iter]->sg_ptr->gu_status, GU_STATUS_UPDATED);

         SPF_CRITICAL_SECTION_END(open_gu_ptr);
      }
   }

   CATCH(result, GU_MSG_PREFIX, open_gu_ptr->log_id)
   {
      GU_MSG(open_gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed CreateGraph: Control connection peer1 (0x%lx, 0x%lx), peer2 (0x%lx, 0x%lx)",
             cmd_ctrl_conn_ptr->peer_1_mod_iid,
             cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id,
             cmd_ctrl_conn_ptr->peer_2_mod_iid,
             cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id);
      SPF_CRITICAL_SECTION_END(open_gu_ptr);
   }

   return result;
}

/*If the connection is across containers, we need to check if the peer container
     uses a different heap for its allocations */
static POSAL_HEAP_ID gu_check_ext_data_conn_and_get_peer_heap_id(uint32_t                  log_id,
                                                                 spf_msg_cmd_graph_open_t *open_cmd_ptr,
                                                                 uint32_t                  src_mod_inst_id,
                                                                 uint32_t                  dst_mod_inst_id,
                                                                 POSAL_HEAP_ID             self_heap_id)
{
   /** create subgraphs & update subgraph properties */
   for (uint32_t i = 0; i < open_cmd_ptr->num_mxd_heap_id_data_links; i++)
   {
      apm_mxd_heap_id_link_cfg_t *cfg_ptr  = open_cmd_ptr->mxd_heap_id_data_links_cfg_pptr[i];
      apm_module_conn_cfg_t *     conn_ptr = (apm_module_conn_cfg_t *)cfg_ptr->conn_ptr;
      if ((src_mod_inst_id == conn_ptr->src_mod_inst_id) && (dst_mod_inst_id == conn_ptr->dst_mod_inst_id))
      {
         // Extract top tracking bits from given original heap id and use it for tracking
         // Basically peer container heap-id based allocations are tracked under this container.
         uint32_t      peer_heap_id          = gu_get_heap_id_from_heap_prop(cfg_ptr->heap_id.heap_id);
         POSAL_HEAP_ID tracking_peer_heap_id = MODIFY_HEAP_ID_FOR_MEM_TRACKING(self_heap_id, peer_heap_id);

         GU_MSG(log_id,
                DBG_HIGH_PRIO,
                "Got external data connection between module 0x%lx and module 0x%lx with peer heap id = 0x%lx",
                src_mod_inst_id,
                dst_mod_inst_id,
                (uint32_t)tracking_peer_heap_id);

         return tracking_peer_heap_id;
      }
   }

   // if mixed heap info is not sent by APM, then use self heap id for the peer as well.
   return self_heap_id;
}

static ar_result_t gu_handle_connection(gu_t *        gu_ptr_,
                                        uint32_t      src_mod_inst_id,
                                        uint32_t      src_mod_op_port_id,
                                        uint32_t      dst_mod_inst_id,
                                        uint32_t      dst_mod_ip_port_id,
                                        gu_sizes_t *  sizes_ptr,
                                        POSAL_HEAP_ID heap_id,
                                        POSAL_HEAP_ID peer_heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   // open_gu_ptr: it is main gu_ptr if asynchronous open is not going on.
   // open_gu_ptr: it is async gu_ptr if asynchronous open is going on
   gu_t *open_gu_ptr = get_gu_ptr_for_current_command_context(gu_ptr_);

   gu_output_port_t *src_output_port_ptr = NULL;
   gu_input_port_t * dst_input_port_ptr  = NULL;

   gu_module_t *src_module_ptr = (0 == src_mod_inst_id) ? NULL : gu_find_module(open_gu_ptr, src_mod_inst_id);
   gu_module_t *dst_module_ptr = (0 == dst_mod_inst_id) ? NULL : gu_find_module(open_gu_ptr, dst_mod_inst_id);

   if (open_gu_ptr != gu_ptr_)
   {
      if (!src_module_ptr)
      {
         src_module_ptr = gu_find_module(gu_ptr_, src_mod_inst_id);
      }

      if (!dst_module_ptr)
      {
         dst_module_ptr = gu_find_module(gu_ptr_, dst_mod_inst_id);
      }
   }

   gu_sg_t *src_module_sg_ptr         = (src_module_ptr) ? src_module_ptr->sg_ptr : NULL;
   gu_sg_t *dst_module_sg_ptr         = (dst_module_ptr) ? dst_module_ptr->sg_ptr : NULL;
   bool_t   is_sg_internal_connection = (src_module_sg_ptr && (src_module_sg_ptr == dst_module_sg_ptr)) ? TRUE : FALSE;

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

   if ((src_module_ptr && (gu_find_pending_port(open_gu_ptr, src_module_ptr, src_mod_op_port_id) ||
                           gu_find_output_port(src_module_ptr, src_mod_op_port_id))) ||
       (dst_module_ptr && (gu_find_pending_port(open_gu_ptr, dst_module_ptr, dst_mod_ip_port_id) ||
                           gu_find_input_port(dst_module_ptr, dst_mod_ip_port_id))))
   {
      THROW(result, AR_EDUPLICATE);
   }

   /** If the module or the port is part of this gu_ptr (container),
    *  then it's internal connection, otherwise, it's an external connection. */
   if (src_module_ptr)
   {
      VERIFY(result, src_module_ptr->max_output_ports > src_module_ptr->num_output_ports);

      // If it is an internal connection then assign the memory from sg mem resource.
      if (is_sg_internal_connection)
      {
         src_output_port_ptr =
            (gu_output_port_t *)gu_sg_get_resource_mem(src_module_sg_ptr,
                                                       MAX(sizes_ptr->out_port_size, sizeof(gu_output_port_t)),
                                                       SG_MEM_RESOURCE_PORT);
      }

      TRY(result,
          gu_create_data_port(open_gu_ptr,
                              src_module_ptr,
                              src_mod_op_port_id,
                              MAX(sizes_ptr->out_port_size, sizeof(gu_output_port_t)),
                              (gu_cmn_port_t **)&src_output_port_ptr,
                              heap_id));

      // If module is new then port can be inserted to the module, if not then it will be inserted later synchronous
      // with data path handling. Module may be running in parallel to data-path so can not update any module
      // information here.
      // if async handling is not enabled then isert the port now.
      if (GU_STATUS_NEW == src_module_ptr->gu_status || !open_gu_ptr->async_gu_ptr)
      {
         TRY(result, gu_insert_output_port(open_gu_ptr, src_module_ptr, src_output_port_ptr, heap_id));

         if (!is_sg_internal_connection)
         {
            src_module_ptr->flags.is_ds_at_sg_or_cntr_boundary = TRUE;
         }
      }
      else
      {
         // push to the pending port list.
         TRY(result,
             spf_list_insert_tail(((spf_list_node_t **)&open_gu_ptr->async_gu_ptr->port_list_ptr),
                                  src_output_port_ptr,
                                  heap_id,
                                  TRUE /* use_pool*/))
      }
   }
   else
   {
      // the module must be in some other container.
      // it's not possible to get connection definition without module being defined.
   }

   if (dst_module_ptr)
   {
      VERIFY(result, dst_module_ptr->max_input_ports > dst_module_ptr->num_input_ports);

      // If it is an internal connection then assign the memory from sg mem resource.
      if (is_sg_internal_connection)
      {
         dst_input_port_ptr =
            (gu_input_port_t *)gu_sg_get_resource_mem(dst_module_sg_ptr,
                                                      MAX(sizes_ptr->in_port_size, sizeof(gu_input_port_t)),
                                                      SG_MEM_RESOURCE_PORT);
      }

      TRY(result,
          gu_create_data_port(open_gu_ptr,
                              dst_module_ptr,
                              dst_mod_ip_port_id,
                              MAX(sizes_ptr->in_port_size, sizeof(gu_input_port_t)),
                              (gu_cmn_port_t **)&dst_input_port_ptr,
                              heap_id));

      // If module is new then port can be inserted now, if not then it will be inserted later synchronous with data
      // path handling. Module may be running in parallel to data-path so can not update any module information here.
      // if async handling is not enabled then insert the port now.
      if (GU_STATUS_NEW == dst_module_ptr->gu_status || !open_gu_ptr->async_gu_ptr)
      {
         TRY(result, gu_insert_input_port(open_gu_ptr, dst_module_ptr, dst_input_port_ptr, heap_id));

         if (!is_sg_internal_connection)
         {
            dst_module_ptr->flags.is_us_at_sg_or_cntr_boundary = TRUE;
         }
      }
      else
      {
         // push to the pending port list.
         TRY(result,
             spf_list_insert_tail(((spf_list_node_t **)&open_gu_ptr->async_gu_ptr->port_list_ptr),
                                  dst_input_port_ptr,
                                  heap_id,
                                  TRUE /* use_pool*/))
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
      src_output_port_ptr->conn_in_port_ptr = dst_input_port_ptr;
      dst_input_port_ptr->conn_out_port_ptr = src_output_port_ptr;
      src_output_port_ptr->ext_out_port_ptr = NULL;
      dst_input_port_ptr->ext_in_port_ptr   = NULL;
   }
   else if (src_module_ptr) // sink module/port in not in this container
   {
      gu_ext_out_port_t *ext_out_port_ptr = NULL;

      src_output_port_ptr->conn_in_port_ptr = NULL;

      MALLOC_MEMSET(ext_out_port_ptr,
                    gu_ext_out_port_t,
                    MAX(sizes_ptr->ext_out_port_size, sizeof(gu_ext_out_port_t)),
                    gu_get_downgraded_heap_id(heap_id,
                                              peer_heap_id), /* Use downgraded heap id for ext port allocations */
                    result);

      gu_set_status(&ext_out_port_ptr->gu_status, GU_STATUS_NEW);

      // handle, this_handle, is populated by container
      ext_out_port_ptr->sg_ptr                               = src_module_sg_ptr;
      ext_out_port_ptr->downstream_handle.module_instance_id = dst_mod_inst_id;
      ext_out_port_ptr->downstream_handle.port_id            = dst_mod_ip_port_id;
      ext_out_port_ptr->downstream_handle.heap_id            = peer_heap_id;

      VERIFY(result, (open_gu_ptr->num_ext_out_ports < UINT8_MAX));
      TRY(result,
          spf_list_insert_tail(((spf_list_node_t **)&(open_gu_ptr->ext_out_port_list_ptr)),
                               ext_out_port_ptr,
                               heap_id,
                               TRUE /* use_pool*/));

      open_gu_ptr->num_ext_out_ports++;

      gu_init_ext_out_port(ext_out_port_ptr, src_output_port_ptr);
   }
   else if (dst_module_ptr) // source module/port is not in this container
   {
      gu_ext_in_port_t *ext_in_port_ptr = NULL;

      dst_input_port_ptr->conn_out_port_ptr = NULL;

      MALLOC_MEMSET(ext_in_port_ptr,
                    gu_ext_in_port_t,
                    MAX(sizes_ptr->ext_in_port_size, sizeof(gu_ext_in_port_t)),
                    gu_get_downgraded_heap_id(heap_id,
                                              peer_heap_id), /* Use downgraded heap id for ext port allocations */
                    result);
      gu_set_status(&ext_in_port_ptr->gu_status, GU_STATUS_NEW);

      // handle, this_handle, is populated by container
      ext_in_port_ptr->sg_ptr                             = dst_module_sg_ptr;
      ext_in_port_ptr->upstream_handle.module_instance_id = src_mod_inst_id;
      ext_in_port_ptr->upstream_handle.port_id            = src_mod_op_port_id;
      ext_in_port_ptr->upstream_handle.heap_id            = peer_heap_id;

      VERIFY(result, (open_gu_ptr->num_ext_in_ports < UINT8_MAX));

      TRY(result,
          spf_list_insert_tail(((spf_list_node_t **)&(open_gu_ptr->ext_in_port_list_ptr)),
                               ext_in_port_ptr,
                               heap_id,
                               TRUE /* use_pool*/));

      open_gu_ptr->num_ext_in_ports++;
      gu_init_ext_in_port(ext_in_port_ptr, dst_input_port_ptr);
   }
   else
   {
      AR_MSG(DBG_HIGH_PRIO, "connection doesn't belong to this container");
   }

   CATCH(result, GU_MSG_PREFIX, open_gu_ptr->log_id)
   {
      GU_MSG(open_gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "FAILED CreateGraph: Data connection source (0x%lx, 0x%lx), destination (0x%lx, 0x%lx)",
             src_mod_inst_id,
             src_mod_op_port_id,
             dst_mod_inst_id,
             dst_mod_ip_port_id);
   }

   return result;
}

/**
 * graph boundary module such as SH MEM EP module doesn't get input connection (rx path)
 * and output connection (tx path).
 * But we need to create ports such that dataQ/bufQ are created.
 */
static ar_result_t gu_handle_graph_boundary_modules(gu_t *        gu_ptr,
                                                    gu_module_t * module_ptr,
                                                    gu_sizes_t *  sizes_ptr,
                                                    POSAL_HEAP_ID heap_id)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   if (MODULE_ID_WR_SHARED_MEM_EP == module_ptr->module_id)
   {
      /** Assign the max input ports. at this time module prop are not parsed. */
      module_ptr->max_input_ports  = 1;
      module_ptr->max_output_ports = 1;
      /**
       * for write shared memory end point module, the input port from client is not specified in connection list.
       * hence it has to be created specially.
       */
      TRY(result,
          gu_handle_connection(gu_ptr,
                               0, /** src = client */
                               1, /** arbitrary output port id */
                               module_ptr->module_instance_id,
                               2, /** arbitrary input port id (WR SH MEM module has no publicly defined input port) */
                               sizes_ptr,
                               heap_id,
                               heap_id /*peer_heap_id*/));
   }
   else if (MODULE_ID_RD_SHARED_MEM_EP == module_ptr->module_id)
   {
      /** Assign the max input ports. at this time module prop are not parsed. */
      module_ptr->max_input_ports  = 1;
      module_ptr->max_output_ports = 1;
      /**
       * for read shared memory end point module, the output port from client is not specified in connection list.
       * hence it has to be created specially.
       */
      TRY(result,
          gu_handle_connection(gu_ptr,
                               module_ptr->module_instance_id,
                               1, /** arbitrary input port id (RD SH MEM module has no publicly defined input port) */
                               0, /** dest = client */
                               2, /** arbitrary output port id */
                               sizes_ptr,
                               heap_id,
                               heap_id /*peer_heap_id*/));
   }

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

/* Function to reset the markers for all the internal ports of a graph.
   Port marker is used sometimes to optimize the graph traversal. The marker is set when a port is visited during
   graph traversal. So all the port markers must be reset to FALSE before the next graph operation is handled. */
ar_result_t gu_reset_graph_port_markers(gu_t *gu_ptr)
{
   gu_module_list_t *mod_list_ptr = NULL;
   gu_module_t *     module_ptr   = NULL;
   gu_sg_list_t *    sg_list_ptr  = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      mod_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr;
      while (mod_list_ptr)
      {
         // bool_t module_done = TRUE;
         module_ptr = mod_list_ptr->module_ptr;

         // Reset all the input ports of the module.
         gu_input_port_list_t *ip_port_list_ptr = module_ptr->input_port_list_ptr;
         while (ip_port_list_ptr)
         {
            ip_port_list_ptr->ip_port_ptr->cmn.flags.mark = FALSE;
            LIST_ADVANCE(ip_port_list_ptr);
         }

         // Reset all the output ports of the module.
         gu_output_port_list_t *op_port_list_ptr = module_ptr->output_port_list_ptr;
         while (op_port_list_ptr)
         {
            op_port_list_ptr->op_port_ptr->cmn.flags.mark = FALSE;
            LIST_ADVANCE(op_port_list_ptr);
         }

#ifdef GU_DEBUG
         if (!module_done)
         {
            GU_MSG(gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   "Warning: Module instance id 0x%08lx found with unmarked edges",
                   module_ptr->module_instance_id);
         }
#endif

         LIST_ADVANCE(mod_list_ptr);
      }
      LIST_ADVANCE(sg_list_ptr);
   }
   return AR_EOK;
}

static ar_result_t gu_update_module_sink_src_info(gu_t *gu_ptr, gu_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   if ((0 != module_ptr->max_input_ports) &&
       ((0 == module_ptr->max_output_ports) ||
        ((0 == module_ptr->min_output_ports) && (0 == module_ptr->num_output_ports))))
   {
      module_ptr->flags.is_sink = TRUE;
   }
   else
   {
      module_ptr->flags.is_sink = FALSE;
   }

   if ((0 != module_ptr->max_output_ports) &&
       ((0 == module_ptr->max_input_ports) ||
        ((0 == module_ptr->min_input_ports) && (0 == module_ptr->num_input_ports))))
   {
      module_ptr->flags.is_source = TRUE;
   }
   else
   {
      module_ptr->flags.is_source = FALSE;
   }

   gu_update_is_siso_module(module_ptr);

   return result;
}

static void gu_query_for_elementary_static_prop(gu_t *gu_ptr, gu_module_t *module_ptr)
{
   capi_err_t      err_code = CAPI_EOK;
   capi_proplist_t init_proplist;
   capi_prop_t     static_prop[1];
   capi_proplist_t static_proplist;
   uint32_t        i = 0;

   /* Query for elementary property*/
   capi_is_elementary_t is_elementary;
   memset(&is_elementary, 0, sizeof(capi_is_elementary_t));

   init_proplist.props_num = 0;
   init_proplist.prop_ptr  = NULL;

   static_prop[i].id                      = CAPI_IS_ELEMENTARY;
   static_prop[i].payload.actual_data_len = 0;
   static_prop[i].payload.max_data_len    = sizeof(capi_is_elementary_t);
   static_prop[i].payload.data_ptr        = (int8_t *)&is_elementary;
   static_prop[i].port_info.is_valid      = FALSE;
   i++;

   static_proplist.props_num = i;
   static_proplist.prop_ptr  = static_prop;

   err_code = amdb_capi_get_static_properties_f((void *)module_ptr->amdb_handle, &init_proplist, &static_proplist);

   // Ignore errors to this get static properties
   if (CAPI_SUCCEEDED(err_code))
   {
      if (is_elementary.is_elementary)
      {
         module_ptr->flags.is_elementary = TRUE;
         GU_MSG(gu_ptr->log_id,
                DBG_MED_PRIO,
                SPF_LOG_PREFIX "Found elementary module mid 0x%lX",
                module_ptr->module_instance_id);
      }
   }
}

static void gu_query_for_min_port_static_prop(gu_t *gu_ptr, gu_module_t *module_ptr)
{
   capi_proplist_t init_proplist;
   capi_prop_t     static_prop[1];
   capi_proplist_t static_proplist;
   uint32_t        i = 0;

   /* Query for min input and output port property, set it to invalid
    * If module doesn't update the values through a property it stays as invalid
    * By default we cannot assume min output ports = min(1, max output ports)
    * This is because for multiport modules, they might have a requirement of min input ports > 1. eg:EC
    * Thus unless modules explicitly sets, we keep min ports as invalid and
    * usage of min port across framework is only to check if = 0 */
   capi_min_port_num_info_t min_port_info;
   // Initialize to invalid value
   min_port_info.num_min_input_ports  = CAPI_INVALID_VAL;
   min_port_info.num_min_output_ports = CAPI_INVALID_VAL;

   init_proplist.props_num = 0;
   init_proplist.prop_ptr  = NULL;

   static_prop[i].id                      = CAPI_MIN_PORT_NUM_INFO;
   static_prop[i].payload.actual_data_len = 0;
   static_prop[i].payload.max_data_len    = sizeof(capi_min_port_num_info_t);
   static_prop[i].payload.data_ptr        = (int8_t *)&min_port_info;
   static_prop[i].port_info.is_valid      = FALSE;
   i++;

   static_proplist.props_num = i;
   static_proplist.prop_ptr  = static_prop;

   amdb_capi_get_static_properties_f((void *)module_ptr->amdb_handle, &init_proplist, &static_proplist);

   // Ignore errors to this get static properties
   if ((CAPI_INVALID_VAL != min_port_info.num_min_input_ports) &&
       (CAPI_INVALID_VAL != min_port_info.num_min_output_ports))
   {
      module_ptr->min_input_ports  = (uint8_t)min_port_info.num_min_input_ports;
      module_ptr->min_output_ports = (uint8_t)min_port_info.num_min_output_ports;
      GU_MSG(gu_ptr->log_id,
             DBG_HIGH_PRIO,
             SPF_LOG_PREFIX "Query for min ports: Module mid 0x%lX: min inp ports %d , min output ports %d",
             module_ptr->module_instance_id,
             module_ptr->min_input_ports,
             module_ptr->min_output_ports);
   }
   else // Set explicitly to invalid value
   {
      module_ptr->min_input_ports  = (uint8_t)CAPI_INVALID_VAL;
      module_ptr->min_output_ports = (uint8_t)CAPI_INVALID_VAL;
   }
}

/**
 * Loops through all modules which haven't been queried yet (were just created) and calls get_static_properties to
 * check
 * if they are elementary modules. Stores the result in a flag.
 * Also queries for minimum number of inp/out ports, stores in module struct.
 */
static ar_result_t gu_query_for_module_static_prop(gu_t *gu_ptr, POSAL_HEAP_ID heap_id)
{
   ar_result_t result = AR_EOK;
   for (gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr; sg_list_ptr; LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *mod_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; mod_list_ptr;
           LIST_ADVANCE(mod_list_ptr))
      {
         gu_module_t *module_ptr     = mod_list_ptr->module_ptr;
         bool_t       can_query_capi = (AMDB_INTERFACE_TYPE_STUB != module_ptr->itype) &&
                                 (AMDB_MODULE_TYPE_FRAMEWORK != module_ptr->module_type) &&
                                 (NULL != module_ptr->amdb_handle);

         if ((GU_STATUS_NEW == module_ptr->gu_status) && (can_query_capi))
         {
            gu_query_for_elementary_static_prop(gu_ptr, module_ptr);

            gu_query_for_min_port_static_prop(gu_ptr, module_ptr);
         }

         if (GU_STATUS_DEFAULT != module_ptr->gu_status)
         {
            gu_update_module_sink_src_info(gu_ptr, module_ptr);
         }
      }
   }

   return result;
}

static void gu_parse_and_attach_elementary_module(gu_t *        gu_ptr,
                                                  gu_module_t * src_module_ptr,
                                                  gu_module_t * dst_module_ptr,
                                                  POSAL_HEAP_ID heap_id)
{
   gu_module_t *module_ptr = NULL;

   // Check elementary module on downstream connection.
   if (src_module_ptr && src_module_ptr->flags.is_elementary && (!src_module_ptr->host_output_port_ptr))
   {
#ifdef DEBUG_MODE_LOG
      GU_MSG(gu_ptr->log_id,
             DBG_MED_PRIO,
             "Checking src module from connection for attachment %lx",
             src_module_ptr->module_instance_id);
#endif

      module_ptr = src_module_ptr;
   }
   // Check elementary module on upstream connection
   else if (dst_module_ptr && dst_module_ptr->flags.is_elementary && (!dst_module_ptr->host_output_port_ptr))
   {
#ifdef DEBUG_MODE_LOG
      GU_MSG(gu_ptr->log_id,
             DBG_MED_PRIO,
             "Checking dst module from connection for attachment %lx",
             dst_module_ptr->module_instance_id);
#endif
      module_ptr = dst_module_ptr;
   }
   else
   {
      return;
   }
#ifdef DEBUG_MODE_LOG
   GU_MSG(gu_ptr->log_id,
          DBG_MED_PRIO,
          "Parsing elementary modules - found elementary module miid %lx",
          module_ptr->module_instance_id);
#endif

   if (!module_ptr->flags.is_siso)
   {
#ifdef DEBUG_MODE_LOG
      GU_MSG(gu_ptr->log_id,
             DBG_MED_PRIO,
             "Parsing elementary module miid %lx - can't attach since elementary module is not SISO.",
             module_ptr->module_instance_id);
#endif
      return;
   }

   gu_input_port_t * cur_in_port_ptr  = module_ptr->input_port_list_ptr->ip_port_ptr;
   gu_output_port_t *cur_out_port_ptr = module_ptr->output_port_list_ptr->op_port_ptr;

   if (!cur_in_port_ptr->conn_out_port_ptr)
   {
#ifdef DEBUG_MODE_LOG
      GU_MSG(gu_ptr->log_id,
             DBG_MED_PRIO,
             "Parsing elementary point module miid %lx - can't attach to previous module, no previous module.",
             module_ptr->module_instance_id);
#endif
      return;
   }

   gu_output_port_t *prev_con_op_ptr = cur_in_port_ptr->conn_out_port_ptr;
   gu_module_t *     prev_module_ptr = prev_con_op_ptr->cmn.module_ptr;

   if (prev_module_ptr->host_output_port_ptr)
   {
#ifdef DEBUG_MODE_LOG
      GU_MSG(gu_ptr->log_id,
             DBG_MED_PRIO,
             "Parsing elementary point module miid %lx - can't attach to previous module miid 0x%lx which "
             "itself is an attached module.",
             module_ptr->module_instance_id,
             prev_module_ptr->module_instance_id);
#endif
      return;
   }

   if (prev_module_ptr->sg_ptr != module_ptr->sg_ptr)
   {
#ifdef DEBUG_MODE_LOG
      GU_MSG(gu_ptr->log_id,
             DBG_MED_PRIO,
             "Parsing elementary point module miid %lx - can't attach to previous module miid 0x%lx belonging "
             "to different subgraph.",
             module_ptr->module_instance_id,
             prev_module_ptr->module_instance_id);
#endif
      return;
   }

   if (prev_con_op_ptr->attached_module_ptr)
   {
#ifdef GU_DEBUG
      GU_MSG(gu_ptr->log_id,
             DBG_MED_PRIO,
             "Parsing elementary module miid %lx - can't attach to previous module miid 0x%lx which "
             "already has an attached module miid 0x%lx (this shouldn't happen)",
             module_ptr->module_instance_id,
             prev_module_ptr->module_instance_id,
             prev_con_op_ptr->attached_module_ptr->module_instance_id);
#endif
      return;
   }

   if (cur_out_port_ptr->attached_module_ptr)
   {
#ifdef GU_DEBUG
      GU_MSG(gu_ptr->log_id,
             DBG_MED_PRIO,
             "Parsing elementary point module miid %lx - can't attach to previous module, it is already hosting "
             "module 0x%x.",
             module_ptr->module_instance_id,
             cur_out_port_ptr->attached_module_ptr->module_instance_id);
#endif
      return;
   }

   if (cur_out_port_ptr->conn_in_port_ptr)
   {
      prev_con_op_ptr->cmn.flags.elementary_pending_attachment = TRUE;
      // Make sure that the next module is not marked for attachment if current module is being attached.
      // This ensures the second elementary module of two consecutive modules doesn't try to attach itself to
      // previous module
      cur_out_port_ptr->cmn.flags.elementary_pending_attachment = FALSE;
      gu_attach_module(gu_ptr, prev_con_op_ptr, heap_id);
   }
   else if (cur_out_port_ptr->ext_out_port_ptr)
   {
      // Attach immediately if the external port is already connected. Otherwise, wait until connect comes to attach.
      prev_con_op_ptr->cmn.flags.elementary_pending_attachment = TRUE;
      if (NULL != cur_out_port_ptr->ext_out_port_ptr->downstream_handle.spf_handle_ptr)
      {
         gu_attach_module(gu_ptr, prev_con_op_ptr, heap_id);
      }
      else
      {
         GU_MSG(gu_ptr->log_id,
                DBG_MED_PRIO,
                "Parsing elementary module miid %lx - elementary is pending for external elementary module.",
                module_ptr->module_instance_id);
      }
   }

   return;
}
/**
 * Loops thorugh connections and search for any elementary modules downstream. We will try to attach them to their
 * upstream peer module.
 */
static ar_result_t gu_parse_elementary_modules(gu_t *                    gu_ptr,
                                               spf_msg_cmd_graph_open_t *open_cmd_ptr,
                                               POSAL_HEAP_ID             heap_id)
{
   ar_result_t result = AR_EOK;

   for (uint32_t i = 0; i < open_cmd_ptr->num_module_conn; i++)
   {
      apm_module_conn_cfg_t *cmd_conn_ptr = open_cmd_ptr->mod_conn_list_pptr[i];
      uint32_t               dst_miid     = cmd_conn_ptr->dst_mod_inst_id;
      uint32_t               src_miid     = cmd_conn_ptr->src_mod_inst_id;

      gu_module_t *src_module_ptr = (0 == src_miid) ? NULL : gu_find_module(gu_ptr, src_miid);
      gu_module_t *dst_module_ptr = (0 == dst_miid) ? NULL : gu_find_module(gu_ptr, dst_miid);

      gu_parse_and_attach_elementary_module(gu_ptr, src_module_ptr, dst_module_ptr, heap_id);
   }

   return result;
}

/**
 * Attaches module to the prevoius output port by removing that module's connections and connecting the
 * previous output port directly to the next input port. The attatched module will keep input and output
 * ports intact which have NULL connected_port_ptr or ext_port_ptr (dangling ports).
 */
static ar_result_t gu_attach_module(gu_t *gu_ptr, gu_output_port_t *prev_con_op_ptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   if (prev_con_op_ptr && prev_con_op_ptr->cmn.flags.elementary_pending_attachment)
   {
      DBG_VERIFY(result, prev_con_op_ptr->conn_in_port_ptr);

      gu_module_t *attached_module_ptr = prev_con_op_ptr->conn_in_port_ptr->cmn.module_ptr;

      DBG_VERIFY(result, attached_module_ptr);

      gu_input_port_t * cur_in_port_ptr = attached_module_ptr->input_port_list_ptr->ip_port_ptr;
      gu_output_port_t *cur_out_port_ptr =
         attached_module_ptr->output_port_list_ptr ? attached_module_ptr->output_port_list_ptr->op_port_ptr : NULL;

      // cur_out_port_ptr would have been non-NULL to assign elementary_pending_attachment == TRUE.
      VERIFY(result, cur_in_port_ptr && cur_out_port_ptr);

      if (cur_out_port_ptr->conn_in_port_ptr)
      {
         gu_input_port_t *next_con_ip_ptr = cur_out_port_ptr->conn_in_port_ptr;

         // Nullify the elementary module's port connections.
         cur_in_port_ptr->conn_out_port_ptr = NULL;
         cur_out_port_ptr->conn_in_port_ptr = NULL;

         // Setup previous and next module ports to connect to each other.
         prev_con_op_ptr->conn_in_port_ptr  = next_con_ip_ptr;
         next_con_ip_ptr->conn_out_port_ptr = prev_con_op_ptr;

         GU_MSG(gu_ptr->log_id,
                DBG_MED_PRIO,
                "Elementary module miid 0x%lX - attached to internal output port id 0x%lx miid "
                "0x%lX",
                attached_module_ptr->module_instance_id,
                prev_con_op_ptr->cmn.id,
                prev_con_op_ptr->cmn.module_ptr->module_instance_id);

         prev_con_op_ptr->attached_module_ptr      = attached_module_ptr;
         attached_module_ptr->host_output_port_ptr = prev_con_op_ptr;
      }
      else if (cur_out_port_ptr->ext_out_port_ptr)
      {
         gu_ext_out_port_t *elem_ext_out_port_ptr = cur_out_port_ptr->ext_out_port_ptr;

         // Nullify the elementary module's port connections.
         cur_in_port_ptr->conn_out_port_ptr = NULL;
         cur_out_port_ptr->ext_out_port_ptr = NULL;

         // Setup the external port to connect to the previous output port.
         prev_con_op_ptr->conn_in_port_ptr = NULL;

         prev_con_op_ptr->ext_out_port_ptr       = elem_ext_out_port_ptr;
         elem_ext_out_port_ptr->int_out_port_ptr = prev_con_op_ptr;

         GU_MSG(gu_ptr->log_id,
                DBG_MED_PRIO,
                "Elementary module miid 0x%lX - attached to external output port id 0x%lx miid 0x%lX.",
                attached_module_ptr->module_instance_id,
                prev_con_op_ptr->cmn.id,
                prev_con_op_ptr->cmn.module_ptr->module_instance_id);

         prev_con_op_ptr->attached_module_ptr      = attached_module_ptr;
         attached_module_ptr->host_output_port_ptr = prev_con_op_ptr;
      }

      prev_con_op_ptr->cmn.flags.elementary_pending_attachment = FALSE;

      // move the flag "downstream is at sg or container boundary" to the host module
      prev_con_op_ptr->cmn.module_ptr->flags.is_ds_at_sg_or_cntr_boundary |=
         attached_module_ptr->flags.is_ds_at_sg_or_cntr_boundary;
      attached_module_ptr->flags.is_ds_at_sg_or_cntr_boundary = FALSE;
   }

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t gu_detach_module(gu_t *gu_ptr, gu_module_t *attached_module_ptr, POSAL_HEAP_ID heap_id)
{
   gu_output_port_t *host_output_port_ptr = attached_module_ptr->host_output_port_ptr;

   if (host_output_port_ptr->ext_out_port_ptr)
   {
      gu_ext_out_port_t *ext_out_port_ptr       = host_output_port_ptr->ext_out_port_ptr;
      gu_input_port_t *  attached_module_ip_ptr = attached_module_ptr->input_port_list_ptr->ip_port_ptr;
      gu_output_port_t * attached_module_op_ptr = attached_module_ptr->output_port_list_ptr->op_port_ptr;

      host_output_port_ptr->conn_in_port_ptr    = attached_module_ip_ptr;
      attached_module_ip_ptr->conn_out_port_ptr = host_output_port_ptr;

      attached_module_op_ptr->ext_out_port_ptr = ext_out_port_ptr;
      ext_out_port_ptr->int_out_port_ptr       = attached_module_op_ptr;
      host_output_port_ptr->ext_out_port_ptr   = NULL;

      host_output_port_ptr->attached_module_ptr = NULL;
      attached_module_ptr->host_output_port_ptr = NULL;

      GU_MSG(gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "Deattached elemantary module 0x%x from host module 0x%x, port id 0x%x",
             attached_module_ptr->module_instance_id,
             host_output_port_ptr->cmn.module_ptr->module_instance_id,
             host_output_port_ptr->cmn.id);

      bool_t node_added;
      spf_list_search_and_add_obj((spf_list_node_t **)&gu_ptr->sorted_module_list_ptr,
                                  (void *)attached_module_ptr,
                                  &node_added,
                                  heap_id,
                                  TRUE);

      gu_ptr->sort_status = GU_SORT_UPDATED;

      host_output_port_ptr->cmn.module_ptr->flags.is_ds_at_sg_or_cntr_boundary = FALSE;
      attached_module_ptr->flags.is_ds_at_sg_or_cntr_boundary                  = TRUE;
   }
   else if (host_output_port_ptr->conn_in_port_ptr)
   {
      gu_input_port_t * next_ip_ptr            = host_output_port_ptr->conn_in_port_ptr;
      gu_input_port_t * attached_module_ip_ptr = attached_module_ptr->input_port_list_ptr->ip_port_ptr;
      gu_output_port_t *attached_module_op_ptr = attached_module_ptr->output_port_list_ptr->op_port_ptr;

      host_output_port_ptr->conn_in_port_ptr    = attached_module_ip_ptr;
      attached_module_ip_ptr->conn_out_port_ptr = host_output_port_ptr;

      attached_module_op_ptr->conn_in_port_ptr = next_ip_ptr;
      next_ip_ptr->conn_out_port_ptr           = attached_module_op_ptr;

      host_output_port_ptr->attached_module_ptr = NULL;
      attached_module_ptr->host_output_port_ptr = NULL;

      GU_MSG(gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "Deattached elemantary module 0x%x from host module 0x%x, port id 0x%x",
             attached_module_ptr->module_instance_id,
             host_output_port_ptr->cmn.module_ptr->module_instance_id,
             host_output_port_ptr->cmn.id);

      spf_list_node_t *next_module_node_ptr = NULL;
      spf_list_find_list_node((spf_list_node_t *)gu_ptr->sorted_module_list_ptr,
                              (void *)next_ip_ptr->cmn.module_ptr,
                              &next_module_node_ptr);

      spf_list_create_and_insert_before_node((spf_list_node_t **)&gu_ptr->sorted_module_list_ptr,
                                             (void *)attached_module_ptr,
                                             next_module_node_ptr,
                                             heap_id,
                                             TRUE);

      gu_ptr->sort_status = GU_SORT_UPDATED;

      attached_module_ptr->flags.is_ds_at_sg_or_cntr_boundary |=
         host_output_port_ptr->cmn.module_ptr->flags.is_ds_at_sg_or_cntr_boundary;
      host_output_port_ptr->cmn.module_ptr->flags.is_ds_at_sg_or_cntr_boundary = FALSE;
   }
   else
   {
      GU_MSG(gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "WARNING: attached module 0x%x is a sink module and shouldn't get deattached dynamically. Not "
             "handling this case for now.",
             attached_module_ptr->module_instance_id);

      return AR_EFAILED;
   }

   return AR_EOK;
}
/**
 * Checks if any modules are pending attachement and attaches them if necessary. This is called from
 * connect() graph management command context to attach external output port modules since they should
 * not be attached until peer handles are exchanged.
 * Caller's responsibility to ensure that function is executed inside lock
 */
ar_result_t gu_attach_pending_elementary_modules(gu_t *gu_ptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   for (gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr; sg_list_ptr; LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gu_module_t *prev_module_ptr = module_list_ptr->module_ptr;

         for (gu_output_port_list_t *out_port_list_ptr = prev_module_ptr->output_port_list_ptr; out_port_list_ptr;
              LIST_ADVANCE(out_port_list_ptr))
         {
            gu_output_port_t *prev_con_op_ptr = out_port_list_ptr->op_port_ptr;
            if (prev_con_op_ptr && prev_con_op_ptr->cmn.flags.elementary_pending_attachment)
            {
               // Internal output ports can always be attached.
               if (!prev_con_op_ptr->ext_out_port_ptr)
               {
                  TRY(result, gu_attach_module(gu_ptr, prev_con_op_ptr, heap_id));
               }
               // External ports can only be attached once they are connected.
               else
               {
                  if (prev_con_op_ptr->ext_out_port_ptr->downstream_handle.spf_handle_ptr)
                  {
                     TRY(result, gu_attach_module(gu_ptr, prev_con_op_ptr, heap_id));
                  }
               }

               if (gu_ptr->sorted_module_list_ptr)
               {
                  // remove attached module from the sorted list
                  spf_list_find_delete_node((spf_list_node_t **)&gu_ptr->sorted_module_list_ptr,
                                            prev_con_op_ptr->attached_module_ptr,
                                            TRUE);
                  gu_ptr->sort_status = GU_SORT_UPDATED;
               }
            }
         }
      }
   }

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

/**
 * Returns the gu_external_output_port structure of the port_ptr, with the following exception:
 * If the container graph is: [A -> B (-> C)] where C is attached,
 *  - When querying C, return the external output port.
 *  - When querying B, do not return the external output port.
 *  - ext_out_port_ptr is NULL if the common port is an input port.
 *
 * This query returns the external output port as the graph appears externally.
 */
ar_result_t gu_get_ext_out_port_for_last_module(gu_t *              gu_ptr,
                                                gu_cmn_port_t *     port_ptr,
                                                gu_ext_out_port_t **ext_out_port_pptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   VERIFY(result, ext_out_port_pptr && port_ptr);
   *ext_out_port_pptr = NULL;

   if (gu_is_output_port_id(port_ptr->id))
   {
      gu_output_port_t *out_port_ptr = (gu_output_port_t *)port_ptr;

      // If this output port hosts an attached module, return NULL for this port - return the external output port of
      // this
      // port when querying the attached output port.
      if (out_port_ptr->attached_module_ptr)
      {
         return result;
      }

      // If this output port is attached, then return the host port's ext_out_port_ptr.
      if (out_port_ptr->cmn.module_ptr->host_output_port_ptr)
      {
         *ext_out_port_pptr = out_port_ptr->cmn.module_ptr->host_output_port_ptr->ext_out_port_ptr;
         return result;
      }

      // Normal cases return this port's external output port.
      *ext_out_port_pptr = out_port_ptr->ext_out_port_ptr;
   }

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t gu_find_edge_modules(gu_t *gu_ptr, gu_module_list_t **edge_module_list_pptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t       result       = AR_EOK;
   gu_sg_list_t *    sg_list_ptr  = NULL;
   gu_module_list_t *mod_list_ptr = NULL;

   // Look for modules that are connected to only external ports and add them to edge module list
   gu_ext_in_port_list_t *port_list_ptr        = gu_ptr->ext_in_port_list_ptr;
   gu_module_list_t *     edge_module_list_ptr = NULL;

#ifdef DEBUG_GRAPH_SORT
   GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "Checking external modules...");
#endif

   VERIFY(result, edge_module_list_pptr);

   while (port_list_ptr)
   {
      bool_t                is_external_only     = TRUE;
      gu_module_t *         module_ptr           = port_list_ptr->ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr;
      gu_input_port_list_t *mod_in_port_list_ptr = module_ptr->input_port_list_ptr;
      while (mod_in_port_list_ptr)
      {
         // If ext_in_port_ptr is null, module has an internal connection
         // If an external port is marked TRUE, then it already in the edge module list.
         if (!mod_in_port_list_ptr->ip_port_ptr->ext_in_port_ptr || mod_in_port_list_ptr->ip_port_ptr->cmn.flags.mark)
         {
            is_external_only = FALSE;
            break;
         }
         LIST_ADVANCE(mod_in_port_list_ptr);
      }
      if (is_external_only)
      {
#ifdef DEBUG_GRAPH_SORT
         GU_MSG(gu_ptr->log_id,
                DBG_MED_PRIO,
                "Adding externally connected module to edge list, inst id 0x%08lx",
                module_ptr->module_instance_id);
#endif
         TRY(result,
             spf_list_insert_tail((spf_list_node_t **)&edge_module_list_ptr,
                                  (void *)module_ptr,
                                  heap_id,
                                  TRUE /* use_pool*/));

         mod_in_port_list_ptr = module_ptr->input_port_list_ptr;
         while (mod_in_port_list_ptr)
         {
            mod_in_port_list_ptr->ip_port_ptr->cmn.flags.mark = TRUE;
            LIST_ADVANCE(mod_in_port_list_ptr);
         }
      }
      else
      {
         // If not external only module, mark the current port to TRUE.
         // This case is handles, multi-port modules whose input ports are mix of internal and external connections.
         port_list_ptr->ext_in_port_ptr->int_in_port_ptr->cmn.flags.mark = TRUE;
      }

      LIST_ADVANCE(port_list_ptr);
   }

#ifdef DEBUG_GRAPH_SORT
   GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "Checking source modules...");
#endif

   /* Find any source modules */

   sg_list_ptr = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      mod_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr;
      while (mod_list_ptr)
      {
         // We want source modules to be in the sorted list, but not floating modules
         /*Three cases where source modules should be added to sorted module list:
          * Case 1: Src module with output connection*/
         if ((0 == mod_list_ptr->module_ptr->num_input_ports) && (0 < mod_list_ptr->module_ptr->num_output_ports))
         {
#ifdef DEBUG_GRAPH_SORT
            GU_MSG(gu_ptr->log_id,
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
         /* Case 2: Src module with no output conn
          * Case 3: Sink module with no input conn*/
         else if (((0 == mod_list_ptr->module_ptr->num_input_ports) &&
                   (0 == mod_list_ptr->module_ptr->min_output_ports)) ||
                  ((0 == mod_list_ptr->module_ptr->num_input_ports) &&
                   (0 == mod_list_ptr->module_ptr->min_input_ports)))
         {
            GU_MSG(gu_ptr->log_id,
                   DBG_MED_PRIO,
                   "Adding source module to edge list, inst id 0x%08lx, num inp ports %d, min out portd %d, min inp "
                   "ports %d",
                   mod_list_ptr->module_ptr->module_instance_id,
                   mod_list_ptr->module_ptr->num_input_ports,
                   mod_list_ptr->module_ptr->min_output_ports,
                   mod_list_ptr->module_ptr->min_input_ports);

            TRY(result,
                spf_list_insert_tail((spf_list_node_t **)&edge_module_list_ptr,
                                     (void *)mod_list_ptr->module_ptr,
                                     heap_id,
                                     TRUE /* use_pool*/));
         }

         // Place floating modules
         else if ((0 == mod_list_ptr->module_ptr->num_input_ports) && (0 == mod_list_ptr->module_ptr->num_output_ports))
         {
#ifdef DEBUG_GRAPH_SORT
            GU_MSG(gu_ptr->log_id,
                   DBG_MED_PRIO,
                   "Adding floating module to edge list, inst id 0x%08lx",
                   mod_list_ptr->module_ptr->module_instance_id);
#endif
            TRY(result,
                spf_list_insert_tail((spf_list_node_t **)&edge_module_list_ptr,
                                     (void *)mod_list_ptr->module_ptr,
                                     heap_id,
                                     TRUE /* use_pool*/));
         }

         LIST_ADVANCE(mod_list_ptr);
      }
      LIST_ADVANCE(sg_list_ptr);
   }

   // Return edge modules.
   *edge_module_list_pptr = edge_module_list_ptr;

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t gu_update_sorted_list(gu_t *gu_ptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t       result               = AR_EOK;
   gu_module_list_t *edge_module_list_ptr = NULL;

   gu_ptr->sort_status = GU_SORT_UPDATED;

   // If sorted list is already allocated, need to free it
   if (gu_ptr->sorted_module_list_ptr)
   {
      spf_list_delete_list((spf_list_node_t **)&gu_ptr->sorted_module_list_ptr, TRUE /* use_pool*/);
   }

   // Start with modules that have no internal connections
   // These are either source modules or modules that are connected to an external input port
   TRY(result, gu_find_edge_modules(gu_ptr, &edge_module_list_ptr, heap_id));

#ifdef DEBUG_GRAPH_SORT
   GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "Running sort...");
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
      GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "Removing links...");
#endif
      // Now mark all vertices for its connected modules
      gu_output_port_list_t *op_port_list_ptr = edge_module_list_ptr->module_ptr->output_port_list_ptr;

      while (op_port_list_ptr)
      {
         bool_t mark_done = TRUE;

         // Check if this port has an external downstream connection, if so, move on to the next port
         if (op_port_list_ptr->op_port_ptr->ext_out_port_ptr)
         {
            LIST_ADVANCE(op_port_list_ptr);
            continue;
         }

         // Mark the downstream module's input port that is connected to this output port
         op_port_list_ptr->op_port_ptr->conn_in_port_ptr->cmn.flags.mark = TRUE;
         // Check if all input ports for the module are marked. If yes, it should be moved to the sorted list
         gu_input_port_list_t *ip_port_list_ptr =
            op_port_list_ptr->op_port_ptr->conn_in_port_ptr->cmn.module_ptr->input_port_list_ptr;
         while (ip_port_list_ptr)
         {
            if (!ip_port_list_ptr->ip_port_ptr->cmn.flags.mark)
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
   TRY(result, gu_reset_graph_port_markers(gu_ptr));

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
      // free up the sorted list and mark it unsorted in case of failure
      spf_list_delete_list((spf_list_node_t **)&gu_ptr->sorted_module_list_ptr, TRUE /* pool_used */);
   }

   // spf_list_delete_list((spf_list_node_t**)&edge_module_list_ptr);

   return result;
}

static void gu_propagate_path_index(gu_t *gu_ptr, gu_module_t *module_ptr, uint16_t path_index);
static void gu_propagate_path_index_forward(gu_t *gu_ptr, gu_output_port_t *op_port_ptr, uint16_t path_index);
static void gu_propagate_path_index_backward(gu_t *gu_ptr, gu_input_port_t *ip_port_ptr, uint16_t path_index);

static void gu_propagate_path_index(gu_t *gu_ptr, gu_module_t *module_ptr, uint16_t path_index)
{
   module_ptr->path_index = path_index;

   GU_MSG(gu_ptr->log_id,
          DBG_LOW_PRIO,
          "Updating module miid 0x%lx to have a path_index %hu",
          module_ptr->module_instance_id,
          module_ptr->path_index);

   for (gu_input_port_list_t *mod_in_port_list_ptr = module_ptr->input_port_list_ptr; mod_in_port_list_ptr;
        LIST_ADVANCE(mod_in_port_list_ptr))
   {
      gu_propagate_path_index_backward(gu_ptr, mod_in_port_list_ptr->ip_port_ptr, module_ptr->path_index);
   }

   for (gu_output_port_list_t *mod_out_port_list_ptr = module_ptr->output_port_list_ptr; mod_out_port_list_ptr;
        LIST_ADVANCE(mod_out_port_list_ptr))
   {
      if (mod_out_port_list_ptr->op_port_ptr->attached_module_ptr)
      {
         mod_out_port_list_ptr->op_port_ptr->attached_module_ptr->path_index = module_ptr->path_index;
      }
      gu_propagate_path_index_forward(gu_ptr, mod_out_port_list_ptr->op_port_ptr, module_ptr->path_index);
   }
}

static void gu_propagate_path_index_backward(gu_t *gu_ptr, gu_input_port_t *ip_port_ptr, uint16_t path_index)
{
   // if there is no upstream connection or if path index is already assigned then break;
   if (!ip_port_ptr->conn_out_port_ptr || path_index == ip_port_ptr->conn_out_port_ptr->cmn.module_ptr->path_index)
   {
      return;
   }

   gu_propagate_path_index(gu_ptr, ip_port_ptr->conn_out_port_ptr->cmn.module_ptr, path_index);
}

static void gu_propagate_path_index_forward(gu_t *gu_ptr, gu_output_port_t *op_port_ptr, uint16_t path_index)
{
   // if there is no upstream connection or if path index is already assigned then break;
   if (!op_port_ptr->conn_in_port_ptr || path_index == op_port_ptr->conn_in_port_ptr->cmn.module_ptr->path_index)
   {
      return;
   }

   gu_propagate_path_index(gu_ptr, op_port_ptr->conn_in_port_ptr->cmn.module_ptr, path_index);
}

void gu_update_parallel_paths(gu_t *gu_ptr)
{
   uint8_t  INVALID_PATH_INDEX = 0xFF;
   uint16_t path_index         = 0;

   gu_ptr->num_parallel_paths = 0;

   // Assign all path index to INVALID_PATH_INDEX.
   for (gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr; sg_list_ptr; LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; module_list_ptr;
           LIST_ADVANCE(module_list_ptr))
      {
         module_list_ptr->module_ptr->path_index = INVALID_PATH_INDEX;
      }
   }

   // Assign path index
   for (gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr; sg_list_ptr; LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; module_list_ptr;
           LIST_ADVANCE(module_list_ptr))
      {
         // skip propagation from attached module.
         if (module_list_ptr->module_ptr->host_output_port_ptr)
         {
            continue;
         }

         // skip to the next module if path-index is already assigned.
         if (INVALID_PATH_INDEX != module_list_ptr->module_ptr->path_index)
         {
            continue;
         }

         // if there is a dangling module then don't assign a new path index otherwise tgp will always be satisfied for
         // the path.
         if (!module_list_ptr->module_ptr->input_port_list_ptr && !module_list_ptr->module_ptr->output_port_list_ptr)
         {
            module_list_ptr->module_ptr->path_index = 0;
            GU_MSG(gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   "Updating module miid 0x%lx to have a path_index %hu",
                   module_list_ptr->module_ptr->module_instance_id,
                   module_list_ptr->module_ptr->path_index);
            continue;
         }

         gu_propagate_path_index(gu_ptr, module_list_ptr->module_ptr, path_index);

         path_index++;

         if (path_index == INVALID_PATH_INDEX)
         {
            GU_MSG(gu_ptr->log_id, DBG_HIGH_PRIO, "reached maximum limit to support parallel paths");
            gu_ptr->num_parallel_paths = path_index;
            path_index                 = 0; // looping over
         }
      }
   }

   if (0 == gu_ptr->num_parallel_paths)
   {
      gu_ptr->num_parallel_paths = path_index;
   }

   GU_MSG(gu_ptr->log_id, DBG_HIGH_PRIO, "Total number of parallel paths %hu", gu_ptr->num_parallel_paths);
}

gu_ext_ctrl_port_t *gu_get_ext_ctrl_port_for_inter_proc_imcl(gu_t *   gu_ptr,
                                                             uint32_t local_miid,
                                                             uint32_t remote_domain_id,
                                                             uint32_t ctrl_port_id)
{
   gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr;
      while (module_list_ptr)
      {
         if (local_miid == module_list_ptr->module_ptr->module_instance_id)
         {
            gu_ctrl_port_list_t *ctrl_port_list_ptr = module_list_ptr->module_ptr->ctrl_port_list_ptr;
            while (ctrl_port_list_ptr)
            {
               gu_ctrl_port_t *ctrl_port_ptr = ctrl_port_list_ptr->ctrl_port_ptr;
               if (ctrl_port_ptr->ext_ctrl_port_ptr) // must be non-null for inter - proc IMCL
               {
                  gu_ext_ctrl_port_t *ext_ctrl_port_ptr = ctrl_port_ptr->ext_ctrl_port_ptr;
                  if ((ctrl_port_id == ext_ctrl_port_ptr->int_ctrl_port_ptr->id) &&
                      (remote_domain_id == ext_ctrl_port_ptr->peer_domain_id))
                  {
                     return ext_ctrl_port_ptr;
                  }
               }
               LIST_ADVANCE(ctrl_port_list_ptr);
            }
         }
         LIST_ADVANCE(module_list_ptr);
      }
      LIST_ADVANCE(sg_list_ptr);
   }
   return NULL;
}

gu_module_t *gu_find_module(gu_t *gu_ptr, uint32_t module_instance_id)
{
   gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr;
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

ar_result_t gu_parse_data_link(gu_t *                 gu_ptr,
                               apm_module_conn_cfg_t *data_link_ptr,
                               gu_output_port_t **    src_mod_out_port_pptr,
                               gu_input_port_t **     dst_mod_in_port_pptr)
{
   gu_module_t *src_module_ptr = NULL;
   gu_module_t *dst_module_ptr = NULL;

   if (NULL != data_link_ptr)
   {
      src_module_ptr = (gu_module_t *)gu_find_module(gu_ptr, data_link_ptr->src_mod_inst_id);
      dst_module_ptr = (gu_module_t *)gu_find_module(gu_ptr, data_link_ptr->dst_mod_inst_id);
      if ((NULL == src_module_ptr) || (NULL == dst_module_ptr))
      {
         GU_MSG(gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "GU: Mod not found Src (ID-ptr) (0x%X-0x%X), Dst (ID, ptr) (0x%X, 0x%X)",
                data_link_ptr->src_mod_inst_id,
                src_module_ptr,
                data_link_ptr->dst_mod_inst_id,
                dst_module_ptr);
         return AR_EFAILED;
      }

      *src_mod_out_port_pptr = gu_find_output_port(src_module_ptr, data_link_ptr->src_mod_op_port_id);
      *dst_mod_in_port_pptr  = gu_find_input_port(dst_module_ptr, data_link_ptr->dst_mod_ip_port_id);

      if ((NULL == *src_mod_out_port_pptr) || (NULL == *dst_mod_in_port_pptr))
      {
         GU_MSG(gu_ptr->log_id,
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

ar_result_t gu_parse_ctrl_link(gu_t *                      gu_ptr,
                               apm_module_ctrl_link_cfg_t *ctrl_link_ptr,
                               gu_ctrl_port_t **           peer_1_ctrl_port_pptr,
                               gu_ctrl_port_t **           peer_2_ctrl_port_pptr)
{
   gu_module_t *peer_1_module_ptr = NULL;
   gu_module_t *peer_2_module_ptr = NULL;

   if (NULL != ctrl_link_ptr)
   {
      peer_1_module_ptr = (gu_module_t *)gu_find_module(gu_ptr, ctrl_link_ptr->peer_1_mod_iid);
      peer_2_module_ptr = (gu_module_t *)gu_find_module(gu_ptr, ctrl_link_ptr->peer_2_mod_iid);

      if ((NULL == peer_1_module_ptr) || (NULL == peer_2_module_ptr))
      {
         GU_MSG(gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "GU: Mod not found Peer 1 (ID-ptr) (0x%X-0x%X), Peer 2 (ID, ptr) (0x%X, 0x%X)",
                ctrl_link_ptr->peer_1_mod_iid,
                peer_1_module_ptr,
                ctrl_link_ptr->peer_2_mod_iid,
                peer_2_module_ptr);
         return AR_EFAILED;
      }

      *peer_1_ctrl_port_pptr = gu_find_ctrl_port_by_id(peer_1_module_ptr, ctrl_link_ptr->peer_1_mod_ctrl_port_id);
      *peer_2_ctrl_port_pptr = gu_find_ctrl_port_by_id(peer_2_module_ptr, ctrl_link_ptr->peer_2_mod_ctrl_port_id);

      if ((NULL == *peer_1_ctrl_port_pptr) || (NULL == *peer_2_ctrl_port_pptr))
      {
         GU_MSG(gu_ptr->log_id,
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

gu_cmn_port_t *gu_find_port_by_index(spf_list_node_t *list_ptr, uint32_t index)
{
   while (list_ptr)
   {
      if (list_ptr->obj_ptr)
      {
         gu_cmn_port_t *port_ptr = (gu_cmn_port_t *)list_ptr->obj_ptr;
         if (port_ptr->index == index)
         {
            return port_ptr;
         }
      }
      LIST_ADVANCE(list_ptr);
   }
   return NULL;
}

gu_output_port_t *gu_find_output_port_by_index(gu_module_t *module_ptr, uint32_t index)
{
   if (!module_ptr)
   {
      return NULL;
   }

   return (gu_output_port_t *)gu_find_port_by_index((spf_list_node_t *)module_ptr->output_port_list_ptr, index);
}

gu_input_port_t *gu_find_input_port_by_index(gu_module_t *module_ptr, uint32_t index)
{
   if (!module_ptr)
   {
      return NULL;
   }

   return (gu_input_port_t *)gu_find_port_by_index((spf_list_node_t *)module_ptr->input_port_list_ptr, index);
}

gu_sg_t *gu_find_subgraph(gu_t *gu_ptr, uint32_t id)
{
   gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      if (sg_list_ptr->sg_ptr)
      {
         gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;
         if (sg_ptr->id == id)
         {
            return sg_ptr;
         }
      }
      LIST_ADVANCE(sg_list_ptr);
   }

   return NULL;
}

bool_t gu_is_port_handle_found_in_spf_array(uint32_t       num_handles,
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

bool_t gu_is_sg_id_found_in_spf_array(spf_cntr_sub_graph_list_t *spf_sg_array_ptr, uint32_t sg_id)
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

// function to cleanup the dangling data ports.
// caller's responsibility to manage the lock before calling this function.
void gu_cleanup_dangling_data_ports(gu_t *gu_ptr)
{
   for (gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr; NULL != sg_list_ptr; LIST_ADVANCE(sg_list_ptr))
   {
      gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;

      for (gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr; NULL != module_list_ptr;
           LIST_ADVANCE(module_list_ptr))
      {
         gu_module_t *module_ptr                          = module_list_ptr->module_ptr;
         bool_t       is_module_ds_at_sg_or_cntr_boundary = FALSE;
         bool_t       is_module_us_at_sg_or_cntr_boundary = FALSE;
         bool_t       data_port_updated                   = FALSE;

         if (!module_ptr->host_output_port_ptr) // Clear Output and Input ports if module is not an attached module.
         {
            gu_input_port_list_t *ip_port_list_ptr = module_ptr->input_port_list_ptr;
            while (ip_port_list_ptr)
            {
               // If a port has neither an internal or external connection, it should be freed here
               gu_input_port_t *in_port_ptr = ip_port_list_ptr->ip_port_ptr;

               if ((!in_port_ptr->ext_in_port_ptr) && (!in_port_ptr->conn_out_port_ptr))
               {
                  GU_MSG(gu_ptr->log_id,
                         DBG_LOW_PRIO,
                         "Destroying connections of Module, port 0x%lx, 0x%lx, ext in ptr 0x%p, conn out ptr "
                         "0x%p",
                         in_port_ptr->cmn.module_ptr->module_instance_id,
                         in_port_ptr->cmn.id,
                         in_port_ptr->ext_in_port_ptr,
                         in_port_ptr->conn_out_port_ptr);

                  data_port_updated = TRUE;
                  spf_list_delete_node_and_free_obj((spf_list_node_t **)&ip_port_list_ptr,
                                                    (spf_list_node_t **)&module_ptr->input_port_list_ptr,
                                                    TRUE /* pool_used */);
                  module_ptr->num_input_ports--;
               }
               else
               {
                  if (in_port_ptr->ext_in_port_ptr || sg_ptr != in_port_ptr->conn_out_port_ptr->cmn.module_ptr->sg_ptr)
                  {
                     is_module_us_at_sg_or_cntr_boundary = TRUE;
                  }

                  LIST_ADVANCE(ip_port_list_ptr);
               }
            }

            gu_output_port_list_t *op_port_list_ptr = module_ptr->output_port_list_ptr;
            while (op_port_list_ptr)
            {
               gu_output_port_t *out_port_ptr = op_port_list_ptr->op_port_ptr;

               if ((!out_port_ptr->ext_out_port_ptr) && (!out_port_ptr->conn_in_port_ptr))
               {
                  // If a host module has an unconnected output port, reattach the module.
                  if (out_port_ptr->attached_module_ptr)
                  {
                     gu_module_t *    attached_module_ptr    = out_port_ptr->attached_module_ptr;
                     gu_input_port_t *attached_module_ip_ptr = attached_module_ptr->input_port_list_ptr->ip_port_ptr;

                     GU_MSG(gu_ptr->log_id,
                            DBG_LOW_PRIO,
                            "Detatching module miid 0x%lx from module, port (0x%lx, 0x%lx) since "
                            "downstream got disconnected",
                            attached_module_ptr->module_instance_id,
                            out_port_ptr->cmn.module_ptr->module_instance_id,
                            out_port_ptr->cmn.id);

                     out_port_ptr->conn_in_port_ptr            = attached_module_ip_ptr;
                     attached_module_ip_ptr->conn_out_port_ptr = out_port_ptr;

                     out_port_ptr->attached_module_ptr         = NULL;
                     attached_module_ptr->host_output_port_ptr = NULL;

                     // We need to destroy the internal output port of the attached module to reflect normal graph
                     // shape - when downstream is disconnected the last module should not have an output port data
                     // structure.
                     gu_output_port_t *attached_module_op_ptr =
                        attached_module_ptr->output_port_list_ptr
                           ? attached_module_ptr->output_port_list_ptr->op_port_ptr
                           : NULL;
                     if (attached_module_op_ptr)
                     {
                        // Elementary modules will have one output port, so destroying first node in the output port
                        // list.
                        GU_MSG(gu_ptr->log_id,
                               DBG_LOW_PRIO,
                               "Destroying output port of Module, port 0x%lx, 0x%lx (previously attached)",
                               attached_module_op_ptr->cmn.module_ptr->module_instance_id,
                               attached_module_op_ptr->cmn.id);

                        spf_list_delete_node_and_free_obj((spf_list_node_t **)&attached_module_ptr
                                                             ->output_port_list_ptr,
                                                          (spf_list_node_t **)&attached_module_ptr
                                                             ->output_port_list_ptr,
                                                          TRUE /* pool_used */);

                        // Attached module's output port is destroyed, it must be at sg or container boundary
                        attached_module_ptr->flags.is_ds_at_sg_or_cntr_boundary = TRUE;

                        attached_module_ptr->num_output_ports--;
                        gu_update_module_sink_src_info(gu_ptr, attached_module_ptr);
                     }

                     LIST_ADVANCE(op_port_list_ptr);
                  }
                  else
                  {
                     GU_MSG(gu_ptr->log_id,
                            DBG_LOW_PRIO,
                            "Destroying connections of Module, port 0x%lx, 0x%lx, ext out ptr 0x%p, conn in ptr "
                            "0x%p",
                            out_port_ptr->cmn.module_ptr->module_instance_id,
                            out_port_ptr->cmn.id,
                            out_port_ptr->ext_out_port_ptr,
                            out_port_ptr->conn_in_port_ptr);

                     spf_list_delete_node_and_free_obj((spf_list_node_t **)&op_port_list_ptr,
                                                       (spf_list_node_t **)&module_ptr->output_port_list_ptr,
                                                       TRUE /* pool_used */);
                     module_ptr->num_output_ports--;
                     data_port_updated = TRUE;
                  }
               }
               else
               {
                  if (out_port_ptr->ext_out_port_ptr ||
                      sg_ptr != out_port_ptr->conn_in_port_ptr->cmn.module_ptr->sg_ptr)
                  {
                     is_module_ds_at_sg_or_cntr_boundary = TRUE;
                  }
                  LIST_ADVANCE(op_port_list_ptr);
               }
            }
         }
         else
         {
            GU_MSG(gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   "Did not check i/p and o/p connections of miid 0x%lx which is an attatched module.",
                   module_ptr->module_instance_id);
         }

         if (data_port_updated)
         {
            // Update sink source info for all modules which are affected by closed sgs
            gu_update_module_sink_src_info(gu_ptr, module_ptr);
         }

         module_ptr->flags.is_ds_at_sg_or_cntr_boundary = is_module_ds_at_sg_or_cntr_boundary;
         module_ptr->flags.is_us_at_sg_or_cntr_boundary = is_module_us_at_sg_or_cntr_boundary;

      } // module-loop
   }    // sg-loop
}

// function to cleanup the dangling control ports.
// caller's responsibility to manage the lock before calling this function.
void gu_cleanup_danling_control_ports(gu_t *gu_ptr)
{
   for (gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr; NULL != sg_list_ptr; LIST_ADVANCE(sg_list_ptr))
   {
      gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;

      for (gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr; NULL != module_list_ptr;
           LIST_ADVANCE(module_list_ptr))
      {
         gu_module_t *module_ptr = module_list_ptr->module_ptr;
         // For Ctrl ports we need to clear them. regardless of attached or not attached.
         gu_ctrl_port_list_t *ctrl_port_list_ptr = module_ptr->ctrl_port_list_ptr;
         while (ctrl_port_list_ptr)
         {
            gu_ctrl_port_t *ctrl_port_ptr = ctrl_port_list_ptr->ctrl_port_ptr;
            if ((!ctrl_port_ptr->ext_ctrl_port_ptr) && (!ctrl_port_ptr->peer_ctrl_port_ptr))
            {
               GU_MSG(gu_ptr->log_id,
                      DBG_LOW_PRIO,
                      "Destroying connections of Module, port 0x%lx, 0x%lx, ext ctrl ptr 0x%p, peer ctrl ptr "
                      "0x%p",
                      ctrl_port_ptr->module_ptr->module_instance_id,
                      ctrl_port_ptr->id,
                      ctrl_port_ptr->ext_ctrl_port_ptr,
                      ctrl_port_ptr->peer_ctrl_port_ptr);

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

      } // module-loop
   }    // sg-loop
}
/**
 * sg_list_ptr == NULL means destroy all subgraphs
 *
 */
ar_result_t gu_destroy_graph(gu_t *gu_ptr, bool_t b_destroy_everything)
{
   ar_result_t result = AR_EOK;

   if (!gu_ptr)
   {
      return AR_EOK;
   }

   {
      // check and destroy external input ports
      gu_ext_in_port_list_t *list_ptr = gu_ptr->ext_in_port_list_ptr;
      while (list_ptr)
      {
         gu_ext_in_port_t *ext_in_port_ptr = list_ptr->ext_in_port_ptr;

         if (!(b_destroy_everything || (GU_STATUS_CLOSING == ext_in_port_ptr->gu_status) ||
               (GU_STATUS_CLOSING == ext_in_port_ptr->sg_ptr->gu_status)))
         {
            // Advance the list to next entry
            LIST_ADVANCE(list_ptr);
            continue;
         }

         if (NULL != ext_in_port_ptr->upstream_handle.spf_handle_ptr)
         {
            GU_MSG(gu_ptr->log_id, DBG_ERROR_PRIO, "Destroy issued when connection to upstream is still present");
            // this is an error because peer port has ref to this memory
         }

         // Set connected external port to null
         gu_deinit_ext_in_port(ext_in_port_ptr);
         gu_ptr->num_ext_in_ports--;
         spf_list_delete_node_and_free_obj((spf_list_node_t **)&list_ptr,
                                           (spf_list_node_t **)&gu_ptr->ext_in_port_list_ptr,
                                           TRUE /* pool_used */);
      }
   }

   {
      // check and destroy external output ports
      gu_ext_out_port_list_t *list_ptr = gu_ptr->ext_out_port_list_ptr;
      while (list_ptr)
      {
         gu_ext_out_port_t *ext_out_port_ptr = list_ptr->ext_out_port_ptr;

         if (!(b_destroy_everything || (GU_STATUS_CLOSING == ext_out_port_ptr->gu_status) ||
               (GU_STATUS_CLOSING == ext_out_port_ptr->sg_ptr->gu_status)))
         {
            // Advance the list to next entry
            LIST_ADVANCE(list_ptr);
            continue;
         }

         if (NULL != ext_out_port_ptr->downstream_handle.spf_handle_ptr)
         {
            GU_MSG(gu_ptr->log_id, DBG_ERROR_PRIO, "Destroy issued when connection to downstream is still present");
            // this is an error because peer port has ref to this memory
         }

         // Set connected external port to null
         gu_deinit_ext_out_port(ext_out_port_ptr);
         gu_ptr->num_ext_out_ports--;
         spf_list_delete_node_and_free_obj((spf_list_node_t **)&list_ptr,
                                           (spf_list_node_t **)&gu_ptr->ext_out_port_list_ptr,
                                           TRUE /* pool_used */);
      }
   }

   {
      // check and destroy external control ports
      gu_ext_ctrl_port_list_t *list_ptr = gu_ptr->ext_ctrl_port_list_ptr;
      while (list_ptr)
      {
         gu_ext_ctrl_port_t *ext_ctrl_port_ptr = list_ptr->ext_ctrl_port_ptr;

         if (!(b_destroy_everything || (GU_STATUS_CLOSING == ext_ctrl_port_ptr->gu_status) ||
               (GU_STATUS_CLOSING == ext_ctrl_port_ptr->sg_ptr->gu_status)))
         {
            // Advance the list to next entry
            LIST_ADVANCE(list_ptr);
            continue;
         }

         if (NULL != ext_ctrl_port_ptr->peer_handle.spf_handle_ptr)
         {
            GU_MSG(gu_ptr->log_id, DBG_ERROR_PRIO, "Destroy issued when control connection to peer is still present");
            // this is an error because peer port has ref to this memory
         }

         gu_deinit_ext_ctrl_port(ext_ctrl_port_ptr);
         gu_ptr->num_ext_ctrl_ports--;
         spf_list_delete_node_and_free_obj((spf_list_node_t **)&list_ptr,
                                           (spf_list_node_t **)&gu_ptr->ext_ctrl_port_list_ptr,
                                           TRUE /* pool_used */);
      }
   }

   {
      // check and destroy Subgraphs
      gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr;
      while (sg_list_ptr)
      {
         gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;

         LIST_ADVANCE(sg_list_ptr);

         if (!(b_destroy_everything || (GU_STATUS_CLOSING == sg_ptr->gu_status)))
         {
            continue;
         }

         gu_ptr->num_subgraphs--;
         spf_list_find_delete_node((spf_list_node_t **)&gu_ptr->sg_list_ptr, sg_ptr, TRUE /*pool_used*/);

         gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr;
         while (module_list_ptr)
         {
            gu_module_t *module_ptr = module_list_ptr->module_ptr;

            // remove the module from the SG list
            sg_ptr->num_modules--;
            spf_list_delete_node(((spf_list_node_t **)&module_list_ptr), TRUE /* pool_used */);

            // remove the closing modules from the sorted module list
            if (gu_ptr->sorted_module_list_ptr)
            {
               spf_list_find_delete_node((spf_list_node_t **)&gu_ptr->sorted_module_list_ptr, module_ptr, TRUE);
               gu_ptr->sort_status = GU_SORT_UPDATED;
            }

            // Free input ports
            gu_input_port_list_t *ip_port_list_ptr = module_ptr->input_port_list_ptr;
            while (ip_port_list_ptr)
            {
               // Set connected port for upstream ports to NULL, those ports will get cleaned up later
               gu_input_port_t *in_port_ptr = ip_port_list_ptr->ip_port_ptr;
               if (in_port_ptr && in_port_ptr->conn_out_port_ptr)
               {
                  in_port_ptr->conn_out_port_ptr->conn_in_port_ptr = NULL;
               }

               // port ptr assigned from the sg resource will be freed at the end.
               if (!gu_sg_is_resource_memory(sg_ptr, (int8_t *)in_port_ptr))
               {
                  posal_memory_free(in_port_ptr);
               }

               LIST_ADVANCE(ip_port_list_ptr);
            }
            spf_list_delete_list(((spf_list_node_t **)&module_ptr->input_port_list_ptr), TRUE /* pool_used */);
            module_ptr->num_input_ports = 0;

            // Free output ports
            gu_output_port_list_t *op_port_list_ptr = module_ptr->output_port_list_ptr;
            while (op_port_list_ptr)
            {
               gu_output_port_t *out_port_ptr = op_port_list_ptr->op_port_ptr;
               if (out_port_ptr)
               {
                  if (out_port_ptr->conn_in_port_ptr)
                  {
                     out_port_ptr->conn_in_port_ptr->conn_out_port_ptr = NULL;
                  }
                  out_port_ptr->attached_module_ptr = NULL;
               }

               // port ptr assigned from the sg resource will be freed at the end.
               if (!gu_sg_is_resource_memory(sg_ptr, (int8_t *)out_port_ptr))
               {
                  posal_memory_free(out_port_ptr);
               }

               LIST_ADVANCE(op_port_list_ptr);
            }
            spf_list_delete_list(((spf_list_node_t **)&module_ptr->output_port_list_ptr), TRUE /* pool_used */);
            module_ptr->num_output_ports = 0;

            // Free control ports
            gu_ctrl_port_list_t *ctrl_port_list_ptr = module_ptr->ctrl_port_list_ptr;
            while (ctrl_port_list_ptr)
            {
               gu_ctrl_port_t *ctrl_port_ptr = ctrl_port_list_ptr->ctrl_port_ptr;
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

            gu_check_and_release_amdb_handle(module_ptr);

            module_ptr->host_output_port_ptr = NULL;

            GU_MSG(gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   SPF_LOG_PREFIX "Destroyed Module 0x%lX",
                   module_ptr->module_instance_id);
         }
         sg_ptr->module_list_ptr = NULL;

         // free the resources.
         spf_list_delete_list_and_free_objs(&sg_ptr->resources_ptr, TRUE /* pool_used */);

         GU_MSG(gu_ptr->log_id, DBG_LOW_PRIO, SPF_LOG_PREFIX "Destroyed subgraph 0x%lX", sg_ptr->id);

         // Free subgraph
         posal_memory_free(sg_ptr);
      }
   }

   /* Now iterate through remaining modules and free up any ports that also need to be cleaned up
    * This will happen when a container contains multiple subgraphs and there are internal ports that
    * need to be destroyed in modules that were connected to the destroyed subgraphs
    */
   gu_cleanup_dangling_data_ports(gu_ptr);

   gu_cleanup_danling_control_ports(gu_ptr);

   // don't need to sort modules, modules which are closed are removed from the list already.
   // gu_print_graph(gu_ptr);

   return result;
}

/* In this function we check if any duplicate subgraph/module/data-link/ctrl-link is being opened.
 * This will also help to avoid unnecessary clean ups for existing ports during clean up.
 * (Eg: A->B link is there now again same link is opened, earlier we would have cleared existing A and B ) */
ar_result_t gu_validate_graph_open_cmd(gu_t *gu_ptr, spf_msg_cmd_graph_open_t *open_cmd_ptr)
{
   apm_module_conn_cfg_t *     cmd_conn_ptr;
   apm_module_ctrl_link_cfg_t *cmd_ctrl_conn_ptr;
   gu_module_t *               module_ptr;

   for (uint32_t i = 0; i < open_cmd_ptr->num_sub_graphs; i++)
   {
      if (gu_find_subgraph(gu_ptr, open_cmd_ptr->sg_cfg_list_pptr[i]->sub_graph_id))
      {
         GU_MSG(gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "gu_validate_graph_open_cmd: subgraph ID 0x%x already present.",
                open_cmd_ptr->sg_cfg_list_pptr[i]->sub_graph_id);
         return AR_EDUPLICATE;
      }
   }

   for (uint32_t i = 0; i < open_cmd_ptr->num_modules_list; i++)
   {
      apm_modules_list_t *apm_module_list_ptr = open_cmd_ptr->mod_list_pptr[i];

      if (!apm_module_list_ptr)
      {
         continue;
      }

      apm_module_cfg_t *cmd_module_ptr = (apm_module_cfg_t *)(apm_module_list_ptr + 1);

      // check if all modules are valid in the module list.
      for (uint32_t j = 0; j < apm_module_list_ptr->num_modules; j++)
      {
         // check if module is already in any subgraph; if so, don't create
         gu_module_t *module_ptr = gu_find_module(gu_ptr, cmd_module_ptr->instance_id);

         if (module_ptr)
         {
            GU_MSG(gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "gu_validate_graph_open_cmd: module IID 0x%x already present.",
                   cmd_module_ptr->instance_id);
            return AR_EDUPLICATE;
         }

         if (cmd_module_ptr->instance_id < AR_DYNAMIC_INSTANCE_ID_RANGE_BEGIN)
         {
            GU_MSG(gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "gu_validate_graph_open_cmd: module IID 0x%x out of range.",
                   cmd_module_ptr->instance_id);
            return AR_EBADPARAM;
         }

         cmd_module_ptr++;
      }
   }

   for (uint32_t i = 0; i < open_cmd_ptr->num_module_conn; i++)
   {
      cmd_conn_ptr = open_cmd_ptr->mod_conn_list_pptr[i];

      module_ptr = (0 == cmd_conn_ptr->src_mod_inst_id) ? NULL : gu_find_module(gu_ptr, cmd_conn_ptr->src_mod_inst_id);

      if (module_ptr)
      {
         if (gu_find_output_port(module_ptr, cmd_conn_ptr->src_mod_op_port_id))
         {
            GU_MSG(gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "gu_validate_graph_open_cmd: Data connection from source (0x%lx, 0x%lx) already present, can not "
                   "connect to the destination (0x%lx, 0x%lx)",
                   cmd_conn_ptr->src_mod_inst_id,
                   cmd_conn_ptr->src_mod_op_port_id,
                   cmd_conn_ptr->dst_mod_inst_id,
                   cmd_conn_ptr->dst_mod_ip_port_id);
            return AR_EDUPLICATE;
         }
      }

      module_ptr = (0 == cmd_conn_ptr->dst_mod_inst_id) ? NULL : gu_find_module(gu_ptr, cmd_conn_ptr->dst_mod_inst_id);

      if (module_ptr)
      {
         if (gu_find_input_port(module_ptr, cmd_conn_ptr->dst_mod_ip_port_id))
         {
            GU_MSG(gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "gu_validate_graph_open_cmd: Data connection to the destination (0x%lx, 0x%lx) already present, can "
                   "not connet from source (0x%lx, 0x%lx)",
                   cmd_conn_ptr->dst_mod_inst_id,
                   cmd_conn_ptr->dst_mod_ip_port_id,
                   cmd_conn_ptr->src_mod_inst_id,
                   cmd_conn_ptr->src_mod_op_port_id);
            return AR_EDUPLICATE;
         }
      }
   }

   for (uint32_t i = 0; i < open_cmd_ptr->num_module_ctrl_links; i++)
   {
      cmd_ctrl_conn_ptr = open_cmd_ptr->mod_ctrl_link_cfg_list_pptr[i];

      module_ptr =
         (0 == cmd_ctrl_conn_ptr->peer_1_mod_iid) ? NULL : gu_find_module(gu_ptr, cmd_ctrl_conn_ptr->peer_1_mod_iid);

      if (module_ptr)
      {
         if (gu_find_ctrl_port_by_id(module_ptr, cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id))
         {
            GU_MSG(gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "gu_validate_graph_open_cmd: Control connection from (0x%lx, 0x%lx) already present, can not "
                   "connect to (0x%lx, 0x%lx)",
                   cmd_ctrl_conn_ptr->peer_1_mod_iid,
                   cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id,
                   cmd_ctrl_conn_ptr->peer_2_mod_iid,
                   cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id);
            return AR_EDUPLICATE;
         }
      }

      module_ptr =
         (0 == cmd_ctrl_conn_ptr->peer_2_mod_iid) ? NULL : gu_find_module(gu_ptr, cmd_ctrl_conn_ptr->peer_2_mod_iid);

      if (module_ptr)
      {
         if (gu_find_ctrl_port_by_id(module_ptr, cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id))
         {
            GU_MSG(gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "gu_validate_graph_open_cmd: Control connection from (0x%lx, 0x%lx) already present, can not "
                   "connect to (0x%lx, 0x%lx)",
                   cmd_ctrl_conn_ptr->peer_2_mod_iid,
                   cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id,
                   cmd_ctrl_conn_ptr->peer_1_mod_iid,
                   cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id);
            return AR_EDUPLICATE;
         }
      }
   }

   return AR_EOK;
}

ar_result_t gu_create_graph(gu_t *                    gu_ptr,
                            spf_msg_cmd_graph_open_t *open_cmd_ptr,
                            gu_sizes_t *              sizes_ptr,
                            POSAL_HEAP_ID             heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t         result              = AR_EOK;
   gu_sg_t *           sg_ptr              = NULL;
   spf_list_node_t *   amdb_h_list_ptr     = NULL;
   apm_modules_list_t *apm_module_list_ptr = NULL;

   // open_gu_ptr: it is main gu_ptr if asynchronous open is not going on.
   // open_gu_ptr: it is async gu_ptr if asynchronous open is going on
   gu_t *open_gu_ptr = get_gu_ptr_for_current_command_context(gu_ptr);

   /** create subgraphs & update subgraph properties */
   for (uint32_t i = 0; i < open_cmd_ptr->num_sub_graphs; i++)
   {
      apm_sub_graph_cfg_t *sg_cmd_ptr = open_cmd_ptr->sg_cfg_list_pptr[i];

      VERIFY(result, (open_gu_ptr->num_subgraphs < UINT8_MAX));

      MALLOC_MEMSET(sg_ptr, gu_sg_t, MAX(sizes_ptr->sg_size, sizeof(gu_sg_t)), heap_id, result);

      sg_ptr->id                                 = sg_cmd_ptr->sub_graph_id;
      sg_ptr->perf_mode                          = APM_SG_PERF_MODE_LOW_POWER; // Assume low power by default
      sg_ptr->clock_scale_factor_q4              = UNITY_Q4;
      sg_ptr->bus_scale_factor_q4                = UNITY_Q4;
      sg_ptr->duty_cycling_clock_scale_factor_q4 = UNITY_Q4;

      gu_set_status(&sg_ptr->gu_status, GU_STATUS_NEW);
      open_gu_ptr->num_subgraphs++;

      TRY(result,
          spf_list_insert_tail(((spf_list_node_t **)&(open_gu_ptr->sg_list_ptr)), sg_ptr, heap_id, TRUE /* use_pool*/));

      TRY(result, gu_parse_subgraph_props(open_gu_ptr, sg_ptr, sg_cmd_ptr));

      GU_MSG(open_gu_ptr->log_id,
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

      // find subgraph by ID in open_gu_ptr, new module can only be created in new SGs so don't need to search main
      // gu_ptr
      sg_ptr = gu_find_subgraph(open_gu_ptr, apm_module_list_ptr->sub_graph_id);

      // subgraph must be defined before any modules in it
      VERIFY(result, sg_ptr);

      gu_set_status(&sg_ptr->gu_status, GU_STATUS_UPDATED);

      // allocate memory for all the modules.
      uint32_t one_module_mem_size = ALIGN_8_BYTES(MAX(sizes_ptr->module_size, sizeof(gu_module_t)));
      uint32_t all_module_mem_size = one_module_mem_size * apm_module_list_ptr->num_modules;

      apm_module_cfg_t *cmd_module_ptr = (apm_module_cfg_t *)(apm_module_list_ptr + 1);

      // allocate resource for module memory.
      TRY(result, gu_sg_allocate_resource(sg_ptr, all_module_mem_size, SG_MEM_RESOURCE_MODULE, heap_id));

      for (uint32_t j = 0; j < apm_module_list_ptr->num_modules; j++)
      {
         gu_module_t *module_ptr = NULL;

         module_ptr = (gu_module_t *)gu_sg_get_resource_mem(sg_ptr, one_module_mem_size, SG_MEM_RESOURCE_MODULE);
         VERIFY(result, (NULL != module_ptr));

         memset(module_ptr, 0, one_module_mem_size);

         gu_set_status(&module_ptr->gu_status, GU_STATUS_NEW);

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
         module_ptr->module_heap_id     = heap_id;

         module_ptr->min_input_ports  = (uint8_t)CAPI_INVALID_VAL;
         module_ptr->min_output_ports = (uint8_t)CAPI_INVALID_VAL;

         TRY(result, gu_handle_graph_boundary_modules(open_gu_ptr, module_ptr, sizes_ptr, heap_id));

         TRY(result, gu_prepare_for_module_loading(open_gu_ptr, &amdb_h_list_ptr, module_ptr, FALSE, 0, heap_id));

         GU_MSG(open_gu_ptr->log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX "CreateGraph: Module id: 0x%08lx, Module Instance id: 0x%lx, SG_id: 0x%lx",
                module_ptr->module_id,
                module_ptr->module_instance_id,
                module_ptr->sg_ptr->id);

         cmd_module_ptr++;
      }
   }

   /**  update module properties */
   TRY(result, gu_parse_module_props(open_gu_ptr, open_cmd_ptr, heap_id));

   // allocate memory for all the internal port per subgraph
   gu_sg_list_t *sg_list_ptr = open_gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      uint32_t port_mem_resource_size = 0;

      // update the memory requirement for all the internal ports per subgraph.
      for (uint32_t i = 0; i < open_cmd_ptr->num_module_conn; i++)
      {
         apm_module_conn_cfg_t *cmd_conn_ptr = open_cmd_ptr->mod_conn_list_pptr[i];

         /*port memory resource is allocated only for internal ports within a SG therefore don't need to search the
          * main_gu_ptr*/
         gu_module_t *src_module_ptr =
            (0 == cmd_conn_ptr->src_mod_inst_id) ? NULL : gu_find_module(open_gu_ptr, cmd_conn_ptr->src_mod_inst_id);
         gu_module_t *dst_module_ptr =
            (0 == cmd_conn_ptr->dst_mod_inst_id) ? NULL : gu_find_module(open_gu_ptr, cmd_conn_ptr->dst_mod_inst_id);

         // both module should be part of the same subgraph connected through internal link.
         if (src_module_ptr && dst_module_ptr && (sg_list_ptr->sg_ptr == src_module_ptr->sg_ptr) &&
             (sg_list_ptr->sg_ptr == dst_module_ptr->sg_ptr))
         {
            port_mem_resource_size += ALIGN_8_BYTES(sizes_ptr->in_port_size);
            port_mem_resource_size += ALIGN_8_BYTES(sizes_ptr->out_port_size);
         }
      }

      // allocate resource for module memory.
      TRY(result, gu_sg_allocate_resource(sg_list_ptr->sg_ptr, port_mem_resource_size, SG_MEM_RESOURCE_PORT, heap_id));

      LIST_ADVANCE(sg_list_ptr);
   }

   /**
    * create connections. for every module store src and sink (module, port)
    */
   for (uint32_t i = 0; i < open_cmd_ptr->num_module_conn; i++)
   {
      apm_module_conn_cfg_t *cmd_conn_ptr = open_cmd_ptr->mod_conn_list_pptr[i];

      /*If the connection is across containers, we need to check if the peer container
        uses a different heap for its allocations */
      POSAL_HEAP_ID peer_heap_id = gu_check_ext_data_conn_and_get_peer_heap_id(open_gu_ptr->log_id,
                                                                               open_cmd_ptr,
                                                                               cmd_conn_ptr->src_mod_inst_id,
                                                                               cmd_conn_ptr->dst_mod_inst_id,
                                                                               heap_id);

      TRY(result,
          gu_handle_connection(gu_ptr, /*pass the main gu_ptr otherwise new connection between existing SG and new SG
                                          can't be established*/
                               cmd_conn_ptr->src_mod_inst_id,
                               cmd_conn_ptr->src_mod_op_port_id,
                               cmd_conn_ptr->dst_mod_inst_id,
                               cmd_conn_ptr->dst_mod_ip_port_id,
                               sizes_ptr,
                               heap_id,
                               peer_heap_id));

      GU_MSG(open_gu_ptr->log_id,
             DBG_HIGH_PRIO,
             SPF_LOG_PREFIX "CreateGraph: Data connection source (0x%lx, 0x%lx), destination (0x%lx, 0x%lx)",
             cmd_conn_ptr->src_mod_inst_id,
             cmd_conn_ptr->src_mod_op_port_id,
             cmd_conn_ptr->dst_mod_inst_id,
             cmd_conn_ptr->dst_mod_ip_port_id);
   }

   /**
    * create control links. for every module store local and peer control port handles.
    */
   for (uint32_t i = 0; i < open_cmd_ptr->num_module_ctrl_links; i++)
   {
      apm_module_ctrl_link_cfg_t *cmd_ctrl_conn_ptr = open_cmd_ptr->mod_ctrl_link_cfg_list_pptr[i];

      // used for control port related resource allocation.
      POSAL_HEAP_ID operating_heap_id = heap_id;

      // heap id of the container hosting the peer module
      POSAL_HEAP_ID peer_heap_id = heap_id;

      // heap id configured by user for the control link. It will be same for both peer
      POSAL_HEAP_ID configured_heap_id = heap_id;

      // if self heap_id is island then no need to get the peer_heap id. It can also be default to island.
      // but if self heap_id is non-island then need to check if peer is island or not.
      if (!POSAL_IS_ISLAND_HEAP_ID(heap_id))
      {
         /*If the connection is across containers, we need to check if the peer container
           uses a different heap for its allocations */
         gu_check_ext_ctrl_conn_and_get_peer_heap_id(open_gu_ptr->log_id,
                                                     open_cmd_ptr,
                                                     cmd_ctrl_conn_ptr->peer_1_mod_iid,
                                                     cmd_ctrl_conn_ptr->peer_2_mod_iid,
                                                     &peer_heap_id);

         // if peer_heap_id is island then need to allocate all the resources in island
         if (POSAL_IS_ISLAND_HEAP_ID(peer_heap_id))
         {
            operating_heap_id = peer_heap_id;
         }
      }

      gu_get_configured_heap_id_ctrl_port(open_gu_ptr, cmd_ctrl_conn_ptr, &configured_heap_id);

      // if at this point operating_heap_id is island but configured_heap_id is non-island then force all allocation in
      // non-island
      if (!POSAL_IS_ISLAND_HEAP_ID(configured_heap_id) && POSAL_IS_ISLAND_HEAP_ID(operating_heap_id))
      {
         operating_heap_id = configured_heap_id;
      }

      TRY(result,
          gu_create_control_link(gu_ptr, /*pass the main gu_ptr otherwise new connection between existing SG and new SG
                                          can't be established*/
                                 cmd_ctrl_conn_ptr,
                                 sizes_ptr,
                                 open_cmd_ptr->imcl_peer_domain_info_pptr,
                                 open_cmd_ptr->num_offloaded_imcl_peers,
                                 heap_id,             // heap id to allocate list node
                                 operating_heap_id)); // heap id to allocate actual control port related objects

      GU_MSG(open_gu_ptr->log_id,
             DBG_HIGH_PRIO,
             SPF_LOG_PREFIX "CreateGraph: Control connection peer1 (0x%lx, 0x%lx), peer2 (0x%lx, 0x%lx)",
             cmd_ctrl_conn_ptr->peer_1_mod_iid,
             cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id,
             cmd_ctrl_conn_ptr->peer_2_mod_iid,
             cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id);
   }

   /** AMDB loading or finding module type */
   TRY(result, gu_handle_module_loading(&amdb_h_list_ptr, open_gu_ptr->log_id));

   /** Look through any newly-loaded modules and query get_static_properites to determine if
    * it is an elementary module and if it raises minimum ports */
   TRY(result, gu_query_for_module_static_prop(open_gu_ptr, heap_id));

   /**  Try to attach elementary modules to other modules and remove them from graph connections. */
   gu_parse_elementary_modules(open_gu_ptr, open_cmd_ptr, heap_id);

   // sorting is needed only if new modules connections are created or new sub graph is opened.
   // sort order will not change if only external ports are opened.
   // defer the sorting in async processing
   if ((gu_ptr == open_gu_ptr) && (open_cmd_ptr->num_module_conn > 0 || open_cmd_ptr->num_sub_graphs))
   {
      /* Update sorted module list.*/
      TRY(result, gu_update_sorted_list(open_gu_ptr, heap_id));
   }

   CATCH(result, GU_MSG_PREFIX, open_gu_ptr->log_id)
   {
      // if failure happens then container will initiate the cleanup.
      spf_list_delete_list_and_free_objs(&amdb_h_list_ptr, TRUE /* pool_used */);
   }

   return result;
}

// function to insert the pending ports to their respective modules.
static ar_result_t gu_insert_pending_ports(gu_t *gu_ptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   if (!gu_ptr->async_gu_ptr || !gu_ptr->async_gu_ptr->port_list_ptr)
   {
      return result;
   }

   for (gu_cmn_port_list_t *cmn_port_list_ptr = gu_ptr->async_gu_ptr->port_list_ptr; cmn_port_list_ptr;
        LIST_ADVANCE(cmn_port_list_ptr))
   {
      gu_module_t *  src_module_ptr = NULL;
      gu_module_t *  dst_module_ptr = NULL;
      gu_cmn_port_t *cmn_port_ptr   = (gu_cmn_port_t *)cmn_port_list_ptr->cmn_port_ptr;
      gu_module_t *  module_ptr     = (gu_module_t *)cmn_port_ptr->module_ptr;
      bool_t         is_input =
         (AR_PORT_DIR_TYPE_INPUT == spf_get_bits(cmn_port_ptr->id, AR_PORT_DIR_TYPE_MASK, AR_PORT_DIR_TYPE_SHIFT))
            ? TRUE
            : FALSE;

      if (is_input)
      {
         gu_input_port_t *in_port_ptr = (gu_input_port_t *)cmn_port_ptr;
         TRY(result, gu_insert_input_port(gu_ptr, module_ptr, in_port_ptr, heap_id));
         module_ptr->flags.is_us_at_sg_or_cntr_boundary = TRUE;

         dst_module_ptr = module_ptr;
         src_module_ptr = in_port_ptr->conn_out_port_ptr ? in_port_ptr->conn_out_port_ptr->cmn.module_ptr : NULL;
      }
      else
      {
         gu_output_port_t *out_port_ptr = (gu_output_port_t *)cmn_port_ptr;
         TRY(result, gu_insert_output_port(gu_ptr, module_ptr, out_port_ptr, heap_id));
         module_ptr->flags.is_ds_at_sg_or_cntr_boundary = TRUE;

         src_module_ptr = module_ptr;
         dst_module_ptr = out_port_ptr->conn_in_port_ptr ? out_port_ptr->conn_in_port_ptr->cmn.module_ptr : NULL;
      }

      gu_update_module_sink_src_info(gu_ptr, module_ptr);

      gu_parse_and_attach_elementary_module(gu_ptr, src_module_ptr, dst_module_ptr, heap_id);
   }

   spf_list_delete_list((spf_list_node_t **)&(gu_ptr->async_gu_ptr->port_list_ptr), TRUE);

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

/* Function to parse the failed open command and mark the relavant SG and external ports for cleanup.
 * */
void gu_prepare_cleanup_for_graph_open_failure(gu_t *gu_ptr, spf_msg_cmd_graph_open_t *open_cmd_ptr)
{
   if (!open_cmd_ptr)
   {
      return;
   }

   for (uint32_t i = 0; i < open_cmd_ptr->num_sub_graphs; i++)
   {
      if (!open_cmd_ptr->sg_cfg_list_pptr[i])
      {
         continue;
      }

      uint32_t sg_id  = open_cmd_ptr->sg_cfg_list_pptr[i]->sub_graph_id;
      gu_sg_t *sg_ptr = gu_find_subgraph(gu_ptr, sg_id);

      if (sg_ptr)
      {
         sg_ptr->gu_status = GU_STATUS_CLOSING;

         GU_MSG(gu_ptr->log_id, DBG_LOW_PRIO, "GRAPH_OPEN_FAILURE: sgid 0x%x marked for cleanup", sg_id);
      }
   }

   for (uint32_t i = 0; i < open_cmd_ptr->num_module_conn; i++)
   {
      apm_module_conn_cfg_t *cmd_conn_ptr = open_cmd_ptr->mod_conn_list_pptr[i];

      gu_module_t *src_module_ptr =
         (0 == cmd_conn_ptr->src_mod_inst_id) ? NULL : gu_find_module(gu_ptr, cmd_conn_ptr->src_mod_inst_id);
      gu_module_t *dst_module_ptr =
         (0 == cmd_conn_ptr->dst_mod_inst_id) ? NULL : gu_find_module(gu_ptr, cmd_conn_ptr->dst_mod_inst_id);

      gu_output_port_t *src_output_port_ptr = gu_find_output_port(src_module_ptr, cmd_conn_ptr->src_mod_op_port_id);
      gu_input_port_t * dst_input_port_ptr  = gu_find_input_port(dst_module_ptr, cmd_conn_ptr->dst_mod_ip_port_id);

      /**
       * 1. internal connection with only one port(either source and destination) other port creation failed.
       * (Don't need to worry about this case as gu_destroy_graph takes care of destroying any unconnected ports)
       *
       * 2. internal connection with both the ports, something else failed during graph-open.
       * (connection will be cleared as part of the subgraph-close later on.
       * gu_destroy_graph doesn't handle internal module connection.)
       *
       * 3. External input port connection.
       * 4. External output port connection.
       */

      // internal connection will be cleared during sg destroy
      if (src_output_port_ptr && dst_input_port_ptr)
      {
         continue;
      }

      // external output port
      if (src_output_port_ptr)
      {
         if (src_output_port_ptr->ext_out_port_ptr &&
             (src_output_port_ptr->ext_out_port_ptr->downstream_handle.module_instance_id ==
              cmd_conn_ptr->dst_mod_inst_id) &&
             (src_output_port_ptr->ext_out_port_ptr->downstream_handle.port_id == cmd_conn_ptr->dst_mod_ip_port_id))
         {
            src_output_port_ptr->ext_out_port_ptr->gu_status = GU_STATUS_CLOSING;

            GU_MSG(gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   "GRAPH_OPEN_FAILURE: external output port 0x%x-0x%x marked for cleanup",
                   src_module_ptr->module_instance_id,
                   src_output_port_ptr->cmn.id);
         }
      }

      // external input port
      else if (dst_input_port_ptr)
      {
         if (dst_input_port_ptr->ext_in_port_ptr &&
             (dst_input_port_ptr->ext_in_port_ptr->upstream_handle.module_instance_id ==
              cmd_conn_ptr->src_mod_inst_id) &&
             (dst_input_port_ptr->ext_in_port_ptr->upstream_handle.port_id == cmd_conn_ptr->src_mod_op_port_id))
         {
            dst_input_port_ptr->ext_in_port_ptr->gu_status = GU_STATUS_CLOSING;

            GU_MSG(gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   "GRAPH_OPEN_FAILURE: external input port 0x%x-0x%x marked for cleanup",
                   dst_module_ptr->module_instance_id,
                   dst_input_port_ptr->cmn.id);
         }
      }
   }

   for (uint32_t i = 0; i < open_cmd_ptr->num_module_ctrl_links; i++)
   {
      apm_module_ctrl_link_cfg_t *cmd_ctrl_conn_ptr = open_cmd_ptr->mod_ctrl_link_cfg_list_pptr[i];

      gu_module_t *peer1_mod_ptr = gu_find_module(gu_ptr, cmd_ctrl_conn_ptr->peer_1_mod_iid);
      gu_module_t *peer2_mod_ptr = gu_find_module(gu_ptr, cmd_ctrl_conn_ptr->peer_2_mod_iid);

      gu_ctrl_port_t *peer1_ctrl_port_ptr =
         gu_find_ctrl_port_by_id(peer1_mod_ptr, cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id);
      gu_ctrl_port_t *peer2_ctrl_port_ptr =
         gu_find_ctrl_port_by_id(peer2_mod_ptr, cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id);

      // internal connection will be cleared during sg destroy
      // gu_destroy_graph clears any unconnected port.
      if (peer1_ctrl_port_ptr && peer2_ctrl_port_ptr)
      {
         continue;
      }
      else if (!peer1_ctrl_port_ptr && !peer2_ctrl_port_ptr)
      {
         // both null shouldn't happen.
         continue;
      }
      else
      {
         gu_ctrl_port_t *local_ctrl_port_ptr = (peer1_ctrl_port_ptr) ? peer1_ctrl_port_ptr : peer2_ctrl_port_ptr;
         uint32_t        remote_module_iid =
            (peer1_ctrl_port_ptr) ? cmd_ctrl_conn_ptr->peer_2_mod_iid : cmd_ctrl_conn_ptr->peer_1_mod_iid;
         uint32_t remote_ctrl_port_id = (peer1_ctrl_port_ptr) ? cmd_ctrl_conn_ptr->peer_2_mod_ctrl_port_id
                                                              : cmd_ctrl_conn_ptr->peer_1_mod_ctrl_port_id;

         if (local_ctrl_port_ptr->ext_ctrl_port_ptr &&
             (local_ctrl_port_ptr->ext_ctrl_port_ptr->peer_handle.module_instance_id == remote_module_iid) &&
             (local_ctrl_port_ptr->ext_ctrl_port_ptr->peer_handle.port_id == remote_ctrl_port_id))
         {
            local_ctrl_port_ptr->ext_ctrl_port_ptr->gu_status = GU_STATUS_CLOSING;

            GU_MSG(gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   "GRAPH_OPEN_FAILURE: external control port 0x%x-0x%x marked for cleanup",
                   local_ctrl_port_ptr->module_ptr->module_instance_id,
                   local_ctrl_port_ptr->id);
         }
      }
   }
}

ar_result_t gu_respond_to_graph_open(gu_t *gu_ptr, spf_msg_t *cmd_msg_ptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t                   result = AR_EOK;
   spf_msg_t                     rsp_msg;
   spf_msg_header_t *            cmd_header_ptr = (spf_msg_header_t *)cmd_msg_ptr->payload_ptr;
   spf_msg_header_t *            rsp_header_ptr;
   spf_cntr_port_connect_info_t *open_rsp_ptr;
   spf_msg_cmd_graph_open_t *    open_cmd_ptr = (spf_msg_cmd_graph_open_t *)&cmd_header_ptr->payload_start;

   uint32_t host_domain_id;
   __gpr_cmd_get_host_domain_id(&host_domain_id);

   // this allocates more than required in case SH MEM EP are involved.
   uint32_t num_ip_port_conn   = gu_ptr->num_ext_in_ports;
   uint32_t num_op_port_conn   = gu_ptr->num_ext_out_ports;
   uint32_t num_ctrl_port_conn = gu_ptr->num_ext_ctrl_ports;

   uint32_t rsp_size = sizeof(spf_cntr_port_connect_info_t) +
                       (num_ip_port_conn + num_op_port_conn + num_ctrl_port_conn) * sizeof(spf_module_port_conn_t);
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
   open_rsp_ptr->ip_data_port_conn_list_ptr = (spf_module_port_conn_t *)(open_rsp_ptr + 1);
   open_rsp_ptr->op_data_port_conn_list_ptr = (open_rsp_ptr->ip_data_port_conn_list_ptr + num_ip_port_conn);
   open_rsp_ptr->ctrl_port_conn_list_ptr    = (open_rsp_ptr->op_data_port_conn_list_ptr + num_op_port_conn);

   /**
    * This looping on open_cmd_ptr ensures we don't return SH MEM EP module's ports (which were created internally) to
    * APM.
    */
   for (uint32_t i = 0; i < open_cmd_ptr->num_module_conn; i++)
   {
      apm_module_conn_cfg_t *cmd_conn_ptr = open_cmd_ptr->mod_conn_list_pptr[i];

      gu_module_t *     src_module_ptr      = gu_find_module(gu_ptr, cmd_conn_ptr->src_mod_inst_id);
      gu_module_t *     dst_module_ptr      = gu_find_module(gu_ptr, cmd_conn_ptr->dst_mod_inst_id);
      gu_output_port_t *src_output_port_ptr = gu_find_output_port(src_module_ptr, cmd_conn_ptr->src_mod_op_port_id);
      gu_input_port_t * dst_input_port_ptr  = gu_find_input_port(dst_module_ptr, cmd_conn_ptr->dst_mod_ip_port_id);

      if (src_module_ptr && src_output_port_ptr && src_output_port_ptr->ext_out_port_ptr)
      {
         gu_ext_out_port_t *     ext_out_port_ptr = src_output_port_ptr->ext_out_port_ptr;
         spf_module_port_conn_t *op_port_conn_list_ptr =
            &open_rsp_ptr->op_data_port_conn_list_ptr[open_rsp_ptr->num_op_data_port_conn];

         op_port_conn_list_ptr->self_mod_port_hdl.port_ctx_hdl   = &ext_out_port_ptr->this_handle;
         op_port_conn_list_ptr->self_mod_port_hdl.module_inst_id = cmd_conn_ptr->src_mod_inst_id;
         op_port_conn_list_ptr->self_mod_port_hdl.module_port_id = cmd_conn_ptr->src_mod_op_port_id;
         op_port_conn_list_ptr->self_mod_port_hdl.port_type      = PORT_TYPE_DATA_OP;
         op_port_conn_list_ptr->self_mod_port_hdl.sub_graph_id   = ext_out_port_ptr->sg_ptr->id;

         op_port_conn_list_ptr->peer_mod_port_hdl.port_ctx_hdl   = NULL;
         op_port_conn_list_ptr->peer_mod_port_hdl.module_inst_id = cmd_conn_ptr->dst_mod_inst_id;
         op_port_conn_list_ptr->peer_mod_port_hdl.module_port_id = cmd_conn_ptr->dst_mod_ip_port_id;
         op_port_conn_list_ptr->peer_mod_port_hdl.port_type      = PORT_TYPE_DATA_IP;
         op_port_conn_list_ptr->peer_mod_port_hdl.sub_graph_id   = 0; // this module doesn't belong to the container.

         open_rsp_ptr->num_op_data_port_conn++;
      }

      if (dst_module_ptr && dst_input_port_ptr && dst_input_port_ptr->ext_in_port_ptr)
      {
         gu_ext_in_port_t *      ext_in_port_ptr = dst_input_port_ptr->ext_in_port_ptr;
         spf_module_port_conn_t *ip_port_conn_list_ptr =
            &open_rsp_ptr->ip_data_port_conn_list_ptr[open_rsp_ptr->num_ip_data_port_conn];

         ip_port_conn_list_ptr->self_mod_port_hdl.port_ctx_hdl   = &ext_in_port_ptr->this_handle;
         ip_port_conn_list_ptr->self_mod_port_hdl.module_inst_id = cmd_conn_ptr->dst_mod_inst_id;
         ip_port_conn_list_ptr->self_mod_port_hdl.module_port_id = cmd_conn_ptr->dst_mod_ip_port_id;
         ip_port_conn_list_ptr->self_mod_port_hdl.port_type      = PORT_TYPE_DATA_IP;
         ip_port_conn_list_ptr->self_mod_port_hdl.sub_graph_id   = ext_in_port_ptr->sg_ptr->id;

         ip_port_conn_list_ptr->peer_mod_port_hdl.port_ctx_hdl   = NULL;
         ip_port_conn_list_ptr->peer_mod_port_hdl.module_inst_id = cmd_conn_ptr->src_mod_inst_id;
         ip_port_conn_list_ptr->peer_mod_port_hdl.module_port_id = cmd_conn_ptr->src_mod_op_port_id;
         ip_port_conn_list_ptr->peer_mod_port_hdl.port_type      = PORT_TYPE_DATA_OP;
         ip_port_conn_list_ptr->peer_mod_port_hdl.sub_graph_id   = 0; // this module doesn't belong to the container.

         open_rsp_ptr->num_ip_data_port_conn++;
      }
   }

   for (uint32_t i = 0; i < open_cmd_ptr->num_module_ctrl_links; i++)
   {
      apm_module_ctrl_link_cfg_t *cmd_ctrl_link_ptr = open_cmd_ptr->mod_ctrl_link_cfg_list_pptr[i];

      gu_module_t *peer1_module_ptr = gu_find_module(gu_ptr, cmd_ctrl_link_ptr->peer_1_mod_iid);
      gu_module_t *peer2_module_ptr = gu_find_module(gu_ptr, cmd_ctrl_link_ptr->peer_2_mod_iid);

      gu_ctrl_port_t *peer1_port_ptr =
         gu_find_ctrl_port_by_id(peer1_module_ptr, cmd_ctrl_link_ptr->peer_1_mod_ctrl_port_id);
      gu_ctrl_port_t *peer2_port_ptr =
         gu_find_ctrl_port_by_id(peer2_module_ptr, cmd_ctrl_link_ptr->peer_2_mod_ctrl_port_id);

      if (peer1_module_ptr && peer1_port_ptr && peer1_port_ptr->ext_ctrl_port_ptr)
      {
         gu_ext_ctrl_port_t *ext_ctrl_port_ptr = peer1_port_ptr->ext_ctrl_port_ptr;

         spf_module_port_conn_t *ctrl_port_conn_list_ptr =
            &open_rsp_ptr->ctrl_port_conn_list_ptr[open_rsp_ptr->num_ctrl_port_conn];

         ctrl_port_conn_list_ptr->self_mod_port_hdl.port_ctx_hdl   = &ext_ctrl_port_ptr->this_handle;
         ctrl_port_conn_list_ptr->self_mod_port_hdl.module_inst_id = cmd_ctrl_link_ptr->peer_1_mod_iid;
         ctrl_port_conn_list_ptr->self_mod_port_hdl.module_port_id = cmd_ctrl_link_ptr->peer_1_mod_ctrl_port_id;
         ctrl_port_conn_list_ptr->self_mod_port_hdl.domain_id      = host_domain_id;
         ctrl_port_conn_list_ptr->self_mod_port_hdl.port_type      = PORT_TYPE_CTRL_IO;
         ctrl_port_conn_list_ptr->self_mod_port_hdl.sub_graph_id   = peer1_port_ptr->module_ptr->sg_ptr->id;

         ctrl_port_conn_list_ptr->peer_mod_port_hdl.port_ctx_hdl   = NULL;
         ctrl_port_conn_list_ptr->peer_mod_port_hdl.module_inst_id = cmd_ctrl_link_ptr->peer_2_mod_iid;
         ctrl_port_conn_list_ptr->peer_mod_port_hdl.module_port_id = cmd_ctrl_link_ptr->peer_2_mod_ctrl_port_id;
         ctrl_port_conn_list_ptr->peer_mod_port_hdl.domain_id      = ext_ctrl_port_ptr->peer_domain_id;
         ctrl_port_conn_list_ptr->peer_mod_port_hdl.port_type      = PORT_TYPE_CTRL_IO;
         ctrl_port_conn_list_ptr->peer_mod_port_hdl.sub_graph_id   = 0; // this module doesn't belong to the container.

         open_rsp_ptr->num_ctrl_port_conn++;
      }

      if (peer2_module_ptr && peer2_port_ptr && peer2_port_ptr->ext_ctrl_port_ptr)
      {
         gu_ext_ctrl_port_t *ext_ctrl_port_ptr = peer2_port_ptr->ext_ctrl_port_ptr;

         spf_module_port_conn_t *ctrl_port_conn_list_ptr =
            &open_rsp_ptr->ctrl_port_conn_list_ptr[open_rsp_ptr->num_ctrl_port_conn];

         ctrl_port_conn_list_ptr->self_mod_port_hdl.port_ctx_hdl   = &ext_ctrl_port_ptr->this_handle;
         ctrl_port_conn_list_ptr->self_mod_port_hdl.module_inst_id = cmd_ctrl_link_ptr->peer_2_mod_iid;
         ctrl_port_conn_list_ptr->self_mod_port_hdl.module_port_id = cmd_ctrl_link_ptr->peer_2_mod_ctrl_port_id;
         ctrl_port_conn_list_ptr->self_mod_port_hdl.domain_id      = host_domain_id;
         ctrl_port_conn_list_ptr->self_mod_port_hdl.port_type      = PORT_TYPE_CTRL_IO;
         ctrl_port_conn_list_ptr->self_mod_port_hdl.sub_graph_id   = peer2_port_ptr->module_ptr->sg_ptr->id;

         ctrl_port_conn_list_ptr->peer_mod_port_hdl.port_ctx_hdl   = NULL;
         ctrl_port_conn_list_ptr->peer_mod_port_hdl.module_inst_id = cmd_ctrl_link_ptr->peer_1_mod_iid;
         ctrl_port_conn_list_ptr->peer_mod_port_hdl.module_port_id = cmd_ctrl_link_ptr->peer_1_mod_ctrl_port_id;
         ctrl_port_conn_list_ptr->peer_mod_port_hdl.domain_id      = ext_ctrl_port_ptr->peer_domain_id;
         ctrl_port_conn_list_ptr->peer_mod_port_hdl.port_type      = PORT_TYPE_CTRL_IO;
         ctrl_port_conn_list_ptr->peer_mod_port_hdl.sub_graph_id   = 0; // this module doesn't belong to the container.

         open_rsp_ptr->num_ctrl_port_conn++;
      }
   }

   GU_MSG(gu_ptr->log_id,
          DBG_MED_PRIO,
          "GRAPH_OPEN: seding response num_ip_hdl %lu, num_op_hdl %lu, num_ctr_hdl %lu",
          open_rsp_ptr->num_ip_data_port_conn,
          open_rsp_ptr->num_op_data_port_conn,
          open_rsp_ptr->num_ctrl_port_conn);

   TRY(result, spf_msg_send_response(&rsp_msg));

   spf_msg_return_msg(cmd_msg_ptr);

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }
   return result;
}

// function to clone the gu_ptr and copy only the necessary information so that the cloned gu can be used to add new
// SG/ports while original gu is being used in the data-path
// Once the graph-open is handled on the cloned-gu, it must be merged with the original gu using
// "gu_finish_async_create"
ar_result_t gu_prepare_async_create(gu_t *gu_ptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   if (!gu_ptr || gu_ptr->async_gu_ptr)
   {
      return AR_EBADPARAM;
   }

   // if there is no subgraph present then data-path can't be running in parallel so no need to prepare for async
   // handling.
   if (0 == gu_ptr->num_subgraphs)
   {
      return AR_EOK;
   }

   gu_async_graph_t *async_gu_ptr = NULL;
   MALLOC_MEMSET(async_gu_ptr, gu_async_graph_t, sizeof(gu_async_graph_t), heap_id, result);

   gu_t *dst_gu_ptr                  = &async_gu_ptr->gu;
   dst_gu_ptr->log_id                = gu_ptr->log_id;
   dst_gu_ptr->container_instance_id = gu_ptr->container_instance_id;

   // self reference
   dst_gu_ptr->async_gu_ptr = async_gu_ptr;

   // don't need to copy external ports. new ports will be added into the async_gu and later merged into the primary gu.

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
      if (async_gu_ptr)
      {
         MFREE_NULLIFY(async_gu_ptr);
      }
   }
   else
   {
      gu_ptr->async_gu_ptr = async_gu_ptr;
   }

   return result;
}

// function to clone the gu_ptr and copy only the necessary information so that the cloned gu can be used to handle
// close while main gu is being used in the data-path Once the close is handled on the cloned-gu, it must be
// destroyed using gu_finish_async_destroy.
// caller's responsibility to call this function inside lock.
ar_result_t gu_prepare_async_destroy(gu_t *gu_ptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   if (!gu_ptr || gu_ptr->async_gu_ptr)
   {
      return AR_EBADPARAM;
   }

   MALLOC_MEMSET(gu_ptr->async_gu_ptr, gu_async_graph_t, sizeof(gu_async_graph_t), heap_id, result);

   gu_t *dst_gu_ptr                  = &gu_ptr->async_gu_ptr->gu;
   dst_gu_ptr->log_id                = gu_ptr->log_id;
   dst_gu_ptr->container_instance_id = gu_ptr->container_instance_id;

   // self reference
   dst_gu_ptr->async_gu_ptr = gu_ptr->async_gu_ptr;

   // todo: also copy the lock information to protect critical section.

   {
      gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr;
      while (sg_list_ptr)
      {
         gu_sg_list_t *next_sg_list_ptr = sg_list_ptr->next_ptr;

         if (GU_STATUS_CLOSING == sg_list_ptr->sg_ptr->gu_status)
         {
            spf_list_move_node_to_another_list((spf_list_node_t **)&dst_gu_ptr->sg_list_ptr,
                                               (spf_list_node_t *)sg_list_ptr,
                                               (spf_list_node_t **)&gu_ptr->sg_list_ptr);
            gu_ptr->num_subgraphs--;
            dst_gu_ptr->num_subgraphs++;

            for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; NULL != module_list_ptr;
                 LIST_ADVANCE(module_list_ptr))
            {
               // remove the closing modules from the sorted module list
               if (gu_ptr->sorted_module_list_ptr)
               {
                  spf_list_find_delete_node((spf_list_node_t **)&gu_ptr->sorted_module_list_ptr,
                                            module_list_ptr->module_ptr,
                                            TRUE);
                  gu_ptr->sort_status = GU_SORT_UPDATED;
               }
            }
         }

         sg_list_ptr = next_sg_list_ptr;
      }
   }

   {
      gu_ext_in_port_list_t *ext_in_list_ptr = gu_ptr->ext_in_port_list_ptr;
      while (ext_in_list_ptr)
      {
         gu_ext_in_port_list_t *next_ext_in_list_ptr = ext_in_list_ptr->next_ptr;

         if (GU_STATUS_CLOSING == ext_in_list_ptr->ext_in_port_ptr->gu_status ||
             GU_STATUS_CLOSING == ext_in_list_ptr->ext_in_port_ptr->sg_ptr->gu_status)
         {
            spf_list_move_node_to_another_list((spf_list_node_t **)&dst_gu_ptr->ext_in_port_list_ptr,
                                               (spf_list_node_t *)ext_in_list_ptr,
                                               (spf_list_node_t **)&gu_ptr->ext_in_port_list_ptr);
            gu_ptr->num_ext_in_ports--;
            dst_gu_ptr->num_ext_in_ports++;
         }

         ext_in_list_ptr = next_ext_in_list_ptr;
      }
   }

   {
      gu_ext_out_port_list_t *ext_out_list_ptr = gu_ptr->ext_out_port_list_ptr;
      while (ext_out_list_ptr)
      {
         gu_ext_out_port_list_t *next_ext_out_list_ptr = ext_out_list_ptr->next_ptr;

         if (GU_STATUS_CLOSING == ext_out_list_ptr->ext_out_port_ptr->gu_status ||
             GU_STATUS_CLOSING == ext_out_list_ptr->ext_out_port_ptr->sg_ptr->gu_status)
         {
            spf_list_move_node_to_another_list((spf_list_node_t **)&dst_gu_ptr->ext_out_port_list_ptr,
                                               (spf_list_node_t *)ext_out_list_ptr,
                                               (spf_list_node_t **)&gu_ptr->ext_out_port_list_ptr);
            gu_ptr->num_ext_out_ports--;
            dst_gu_ptr->num_ext_out_ports++;
         }

         ext_out_list_ptr = next_ext_out_list_ptr;
      }
   }

   {
      gu_ext_ctrl_port_list_t *ext_ctrl_list_ptr = gu_ptr->ext_ctrl_port_list_ptr;
      while (ext_ctrl_list_ptr)
      {
         gu_ext_ctrl_port_list_t *next_ext_ctrl_list_ptr = ext_ctrl_list_ptr->next_ptr;

         if (GU_STATUS_CLOSING == ext_ctrl_list_ptr->ext_ctrl_port_ptr->gu_status ||
             GU_STATUS_CLOSING == ext_ctrl_list_ptr->ext_ctrl_port_ptr->sg_ptr->gu_status)
         {
            spf_list_move_node_to_another_list((spf_list_node_t **)&dst_gu_ptr->ext_ctrl_port_list_ptr,
                                               (spf_list_node_t *)ext_ctrl_list_ptr,
                                               (spf_list_node_t **)&gu_ptr->ext_ctrl_port_list_ptr);
            gu_ptr->num_ext_ctrl_ports--;
            dst_gu_ptr->num_ext_ctrl_ports++;
         }

         ext_ctrl_list_ptr = next_ext_ctrl_list_ptr;
      }
   }

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   return result;
}

// function to merge the cloned gu back to the original gu
// caller's reponsibility to call this inside lock
// src_gu_pptr: cloned gu, which was used for graph creation.
// dst_gu_ptr: original gu which is being used in the data-path parallely
ar_result_t gu_finish_async_create(gu_t *gu_ptr, POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result           = AR_EOK;
   bool_t      b_sorting_needed = FALSE;
   if (!gu_ptr)
   {
      return AR_EBADPARAM;
   }

   if (!gu_ptr->async_gu_ptr)
   {
      return AR_EOK;
   }

   gu_t *src_gu_ptr = &gu_ptr->async_gu_ptr->gu;

   // if new internal links or SG are opened then need to sort the module list
   b_sorting_needed = (gu_ptr->async_gu_ptr->port_list_ptr || (src_gu_ptr->num_subgraphs)) ? TRUE : FALSE;

   gu_ptr->num_subgraphs += src_gu_ptr->num_subgraphs;
   spf_list_merge_lists(((spf_list_node_t **)&(gu_ptr->sg_list_ptr)), ((spf_list_node_t **)&(src_gu_ptr->sg_list_ptr)));

   gu_ptr->num_ext_in_ports += src_gu_ptr->num_ext_in_ports;
   spf_list_merge_lists(((spf_list_node_t **)&(gu_ptr->ext_in_port_list_ptr)),
                        ((spf_list_node_t **)&(src_gu_ptr->ext_in_port_list_ptr)));

   gu_ptr->num_ext_out_ports += src_gu_ptr->num_ext_out_ports;
   spf_list_merge_lists(((spf_list_node_t **)&(gu_ptr->ext_out_port_list_ptr)),
                        ((spf_list_node_t **)&(src_gu_ptr->ext_out_port_list_ptr)));

   gu_ptr->num_ext_ctrl_ports += src_gu_ptr->num_ext_ctrl_ports;
   spf_list_merge_lists(((spf_list_node_t **)&(gu_ptr->ext_ctrl_port_list_ptr)),
                        ((spf_list_node_t **)&(src_gu_ptr->ext_ctrl_port_list_ptr)));

   TRY(result, gu_insert_pending_ports(gu_ptr, heap_id));

   if (b_sorting_needed)
   {
      TRY(result, gu_update_sorted_list(gu_ptr, heap_id));
   }

   CATCH(result, GU_MSG_PREFIX, gu_ptr->log_id)
   {
   }

   MFREE_NULLIFY(gu_ptr->async_gu_ptr);

   return result;
}

ar_result_t gu_finish_async_destroy(gu_t *gu_ptr)
{
   if (!gu_ptr || !gu_ptr->async_gu_ptr)
   {
      return AR_EBADPARAM;
   }

   gu_async_graph_t *async_gu_ptr = gu_ptr->async_gu_ptr;

   gu_ptr->async_gu_ptr = NULL;

   gu_destroy_graph(&async_gu_ptr->gu, FALSE /*b_destroy_everything*/);
   MFREE_NULLIFY(async_gu_ptr);

   return AR_EOK;
}

void gu_print_graph(gu_t *gu_ptr)
{
   gu_sg_list_t *sg_list_ptr = gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr;
      GU_MSG(gu_ptr->log_id,
             DBG_MED_PRIO,
             "SG id: 0x%08lx, num modules %d",
             sg_list_ptr->sg_ptr->id,
             sg_list_ptr->sg_ptr->num_modules);
      GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "SG module list:");
      while (module_list_ptr)
      {
         GU_MSG(gu_ptr->log_id,
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
      GU_MSG(gu_ptr->log_id, DBG_MED_PRIO, "Sorted list of modules:");
      gu_module_list_t *sorted_list_ptr = gu_ptr->sorted_module_list_ptr;
      while (sorted_list_ptr)
      {
         gu_module_t *module_ptr = sorted_list_ptr->module_ptr;
         GU_MSG(gu_ptr->log_id,
                DBG_MED_PRIO,
                "Module ID: 0x%08lx, Module Instance id: 0x%08lx, input ports %lu, output ports %lu",
                module_ptr->module_id,
                module_ptr->module_instance_id,
                module_ptr->num_input_ports,
                module_ptr->num_output_ports);
         gu_output_port_list_t *op_port_list_ptr = module_ptr->output_port_list_ptr;
         while (op_port_list_ptr)
         {
            gu_output_port_t *op_port_ptr = op_port_list_ptr->op_port_ptr;

            if (op_port_ptr->attached_module_ptr)
            {
               GU_MSG(gu_ptr->log_id,
                      DBG_MED_PRIO,
                      "Port index %lu id 0x%lX has attached elementary module MID 0x%lx, MIID 0x%lx",
                      op_port_ptr->cmn.index,
                      op_port_ptr->cmn.id,
                      op_port_ptr->attached_module_ptr->module_id,
                      op_port_ptr->attached_module_ptr->module_instance_id);
            }

            if (op_port_ptr->ext_out_port_ptr)
            {
               GU_MSG(gu_ptr->log_id,
                      DBG_MED_PRIO,
                      "Port index %lu id 0x%lX, connected to external port, handle: 0x%08lX",
                      op_port_ptr->cmn.index,
                      op_port_ptr->cmn.id,
                      op_port_ptr->ext_out_port_ptr);
            }
            else
            {
               GU_MSG(gu_ptr->log_id,
                      DBG_MED_PRIO,
                      "Port index %lu id 0x%lX, downstream module id 0x%08lX, downstream module instance id "
                      "0x%08lX, downstream port index %lu, id 0x%lX",
                      op_port_ptr->cmn.index,
                      op_port_ptr->cmn.id,
                      op_port_ptr->conn_in_port_ptr->cmn.module_ptr->module_id,
                      op_port_ptr->conn_in_port_ptr->cmn.module_ptr->module_instance_id,
                      op_port_ptr->conn_in_port_ptr->cmn.index,
                      op_port_ptr->conn_in_port_ptr->cmn.id);
            }
            LIST_ADVANCE(op_port_list_ptr);
         }

         gu_ctrl_port_list_t *ctrl_port_list_ptr = module_ptr->ctrl_port_list_ptr;
         while (ctrl_port_list_ptr)
         {
            gu_ctrl_port_t *ctrl_port_ptr = ctrl_port_list_ptr->ctrl_port_ptr;
            if (ctrl_port_ptr->ext_ctrl_port_ptr)
            {
               GU_MSG(gu_ptr->log_id,
                      DBG_MED_PRIO,
                      SPF_LOG_PREFIX "Control Port id 0x%lX, connected to external port, handle: 0x%08lX",
                      ctrl_port_ptr->id,
                      ctrl_port_ptr->ext_ctrl_port_ptr);
            }
            else
            {
               GU_MSG(gu_ptr->log_id,
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
}