/**
 * \file apm_graph_utils.c
 *
 * \brief
 *     This file contains APM Graph Management Utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/**==============================================================================
   Global Defines
==============================================================================*/

#define APM_DBG_GRAPH_UPDATE

#define APM_MAX_SORT_LOOP_ITR    (500)

#include "apm_internal.h"

static ar_result_t apm_add_cont_sg_list_to_graph(apm_cont_graph_t *cont_graph_ptr, apm_container_t *container_node_ptr)
{
   ar_result_t      result = AR_EOK;
   apm_sub_graph_t *sub_graph_node_ptr;
   spf_list_node_t *curr_node_ptr;
   bool_t           node_added;

   /** Update the sub-graph list for this graph */

   /** Get the pointer to sub-graph list for this container */
   curr_node_ptr = container_node_ptr->sub_graph_list_ptr;

   /** Iterate over the sub-graph list */
   while (curr_node_ptr)
   {
      /** Get the sub-graph pointer */
      sub_graph_node_ptr = (apm_sub_graph_t *)curr_node_ptr->obj_ptr;

      /** Add the sub-graph node the host graph's sub-grpah list */
      spf_list_search_and_add_obj(&cont_graph_ptr->sub_graph_list_ptr,
                                  sub_graph_node_ptr,
                                  &node_added,
                                  APM_INTERNAL_STATIC_HEAP_ID,
                                  TRUE /*use_pool*/);

      /** If this node has been added to the graph */
      if (node_added)
      {
         cont_graph_ptr->num_sub_graphs++;
         AR_MSG(DBG_HIGH_PRIO,
                "apm_add_cont_sg_list_to_graph(): Added sub_graph_id[0x%lX] to graph_ptr[0x%lX]",
                sub_graph_node_ptr->sub_graph_id,
                cont_graph_ptr);
      }

      /** Advance to next node */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return result;
}

/** this utility merges sub graphs present in dst cont. graph into src cont. graph's sg list**/
static ar_result_t apm_merge_cont_graph_sg_list(apm_cont_graph_t *src_cont_graph_ptr,
                                                apm_cont_graph_t *dst_cont_graph_ptr)
{
   ar_result_t      result = AR_EOK;
   bool_t           node_added;
   apm_sub_graph_t *sub_graph_node_ptr;
   spf_list_node_t *curr_node_ptr;

   /** Get the pointer to sub-graph list of the destination cont graph ptr */
   curr_node_ptr = dst_cont_graph_ptr->sub_graph_list_ptr;

   /** Itterate over the sub-graph list **/
   while (curr_node_ptr)
   {
      /** Get the sub-graph pointer */
      sub_graph_node_ptr = (apm_sub_graph_t *)curr_node_ptr->obj_ptr;

      /** Add the sub-graph node to the host graph's sub-grpah list */
      spf_list_search_and_add_obj(&src_cont_graph_ptr->sub_graph_list_ptr,
                                  sub_graph_node_ptr,
                                  &node_added,
                                  APM_INTERNAL_STATIC_HEAP_ID,
                                  TRUE /*use_pool*/);

      /** If this node has been added to the graph */
      if (node_added)
      {
         src_cont_graph_ptr->num_sub_graphs++;
         AR_MSG(DBG_HIGH_PRIO,
                "apm_merge_cont_graph_sg_list(): Added sub_graph_id[0x%lX] to graph_ptr[0x%lX] ",
                sub_graph_node_ptr->sub_graph_id,
                src_cont_graph_ptr);
      }

      /** Advance to next node */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return result;
}

static ar_result_t apm_create_cont_graph(apm_graph_info_t *graph_info_ptr, apm_container_t *container_node_ptr)
{
   ar_result_t       result = AR_EOK;
   apm_cont_graph_t *cont_graph_ptr;

   cont_graph_ptr = (apm_cont_graph_t *)posal_memory_malloc(sizeof(apm_cont_graph_t), APM_INTERNAL_STATIC_HEAP_ID);

   if (!cont_graph_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_create_cont_graph(): Failed to allocate memory for container graph");

      return AR_ENOMEMORY;
   }

   /** Clear the allocated structure */
   memset(cont_graph_ptr, 0, sizeof(apm_cont_graph_t));

   /** Add the container to the graph */
   if (AR_EOK != (result = apm_db_add_node_to_list(&cont_graph_ptr->container_list_ptr,
                                                   container_node_ptr,
                                                   &cont_graph_ptr->num_containers)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_create_cont_graph(): Failed to add CONT_ID[0x%lX] to container graph",
             container_node_ptr->container_id);

      goto __apm_bailout_create_cont_graph;
   }

   /** Add this graph to APM graph list  */
   if (AR_EOK != (result = apm_db_add_node_to_list(&graph_info_ptr->cont_graph_list_ptr,
                                                   cont_graph_ptr,
                                                   &graph_info_ptr->num_cont_graphs)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_create_cont_graph(): Failed to add graph node[0x%lX] to global graph list",
             cont_graph_ptr);

      goto __apm_bailout_create_cont_graph;
   }

   /** Update the sub-graph list for this graph */
   if (AR_EOK != (result = apm_add_cont_sg_list_to_graph(cont_graph_ptr, container_node_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_create_cont_graph(): Failed to add sg-id list of CONT_ID[0x%lX] to graph node[0x%lX]",
             container_node_ptr->container_id,
             cont_graph_ptr);

      goto __apm_bailout_create_cont_graph;
   }

   /** Update the graph node for this container */
   container_node_ptr->cont_graph_ptr = cont_graph_ptr;

   /** Update the tail node pointer for this newly created graph */
   cont_graph_ptr->cont_list_tail_node_ptr = cont_graph_ptr->container_list_ptr;

   /** For the first time container addition, mark the graph as
    *  sorted by default. For subsequent container addition,
    *  this flag will be cleared. */
   cont_graph_ptr->graph_is_sorted = TRUE;

   /** Remove this container from list of standalone containers  */
   if (graph_info_ptr->standalone_cont_list_ptr)
   {
      if (FALSE ==
          spf_list_find_delete_node(&graph_info_ptr->standalone_cont_list_ptr, container_node_ptr, TRUE /*pool_used*/))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_create_cont_graph(): Failed to find COND_ID[0x%lX], in standalone cntr list",
                container_node_ptr->container_id);
      }
   }

   return result;

__apm_bailout_create_cont_graph:

   /** TODO: add clean up   */

   return result;
}

static ar_result_t apm_delete_cont_graph(apm_graph_info_t *graph_info_ptr, apm_cont_graph_t *cont_graph_ptr)
{
   ar_result_t result = AR_EOK;

   /** Free up the sub-graph list for this graph, if non-empty */
   if (cont_graph_ptr->sub_graph_list_ptr)
   {
      spf_list_delete_list(&cont_graph_ptr->sub_graph_list_ptr, TRUE /* pool_used */);
   }

   /** Remove this graph node from the APM global graph list */
   apm_db_remove_node_from_list(&graph_info_ptr->cont_graph_list_ptr, cont_graph_ptr, &graph_info_ptr->num_cont_graphs);

   /** Free up graph memory */
   posal_memory_free(cont_graph_ptr);

   return result;
}

#ifdef APM_DBG_GRAPH_UPDATE
static void apm_print_sorted_container_graph(apm_cont_graph_t *cont_graph_ptr)
{
   spf_list_node_t *curr_node_ptr;
   apm_container_t *container_node_ptr;
   apm_sub_graph_t *sub_graph_node_ptr;

   if (!cont_graph_ptr)
   {
      return;
   }

   AR_MSG(DBG_HIGH_PRIO,
          "apm_print_sorted_container_graph: Graph ptr[0x%lX], Num containers[%lu], Num sub-graphs[%lu], "
          "sort_status[%lu]",
          cont_graph_ptr,
          cont_graph_ptr->num_containers,
          cont_graph_ptr->num_sub_graphs,
          cont_graph_ptr->graph_is_sorted);

   /** Get the pointer to the list of containers */
   curr_node_ptr = cont_graph_ptr->container_list_ptr;

   /** Iterate over the container list for this graph */
   while (curr_node_ptr)
   {
      /** Get the pointer to current container in the list */
      container_node_ptr = (apm_container_t *)curr_node_ptr->obj_ptr;

      AR_MSG(DBG_HIGH_PRIO, "apm_print_sorted_container_graph: CONT_ID[0x%lX]", container_node_ptr->container_id);

      /** Advance to next node in the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   /** Get the pointer to the list of sub-graphs */
   curr_node_ptr = cont_graph_ptr->sub_graph_list_ptr;

   /** Iterate over the sub-graph list for this graph */
   while (curr_node_ptr)
   {
      /** Get the pointer to current sub-graph in the list */
      sub_graph_node_ptr = (apm_sub_graph_t *)curr_node_ptr->obj_ptr;

      AR_MSG(DBG_HIGH_PRIO, "apm_print_sorted_container_graph: SG_ID[0x%lX]", sub_graph_node_ptr->sub_graph_id);

      /** Advance to next node in the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return;
}
#endif

static ar_result_t apm_cont_cache_curr_cyclic_data_link(apm_container_t *      self_cont_obj_ptr,
                                                        apm_container_t *      tgt_peer_cont_obj_ptr,
                                                        spf_module_port_type_t self_cont_port_type)
{
   spf_list_node_t *             curr_peer_cont_node_ptr;
   spf_list_node_t *             curr_self_cont_port_list_node_ptr;
   apm_cont_port_connect_info_t *self_cont_out_port_list_obj_ptr;
   apm_container_t *             curr_peer_cont_obj_ptr;

   curr_self_cont_port_list_node_ptr =
      self_cont_obj_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_ACYCLIC][self_cont_port_type].list_ptr;

   /** Iterate of the list input/output data port connection of
    *  self container and list for which peer container is same
    *  as one provided in input argument, move them to separate
    *  book keeping for cyclic links */
   while (curr_self_cont_port_list_node_ptr)
   {
      self_cont_out_port_list_obj_ptr = (apm_cont_port_connect_info_t *)curr_self_cont_port_list_node_ptr->obj_ptr;

      //next_node_ptr = curr_self_cont_port_list_node_ptr->next_ptr;

      /** If the peer container for current link matches with peer
       *  container provided in the input argument */
      curr_peer_cont_node_ptr = self_cont_out_port_list_obj_ptr->peer_cont_list.list_ptr;

      while (curr_peer_cont_node_ptr)
      {
         curr_peer_cont_obj_ptr = curr_peer_cont_node_ptr->obj_ptr;

         if (curr_peer_cont_obj_ptr == tgt_peer_cont_obj_ptr)
         {
            spf_list_move_node_to_another_list(&self_cont_obj_ptr
                                                   ->cont_ports_per_sg_pair[APM_PORT_TYPE_CYCLIC][self_cont_port_type]
                                                   .list_ptr,
                                               curr_self_cont_port_list_node_ptr,
                                               &self_cont_obj_ptr
                                                   ->cont_ports_per_sg_pair[APM_PORT_TYPE_ACYCLIC][self_cont_port_type]
                                                   .list_ptr);

            /** Decrement number of acyclic links  */
            self_cont_obj_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_ACYCLIC][self_cont_port_type].num_nodes--;

            /** Increment number of cyclic links   */
            self_cont_obj_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_CYCLIC][self_cont_port_type].num_nodes++;
         }

         /** Advcance to next node in the list   */
         curr_peer_cont_node_ptr = curr_peer_cont_node_ptr->next_ptr;

      } /** End of while (peer cont list) */

      curr_self_cont_port_list_node_ptr = curr_self_cont_port_list_node_ptr->next_ptr;

   } /** End of while (cont per sg pair port conn list) */

   AR_MSG(DBG_HIGH_PRIO,
          "apm_cont_cache_curr_cyclic_data_link():: Cached cyclic data link between self CONT_ID[0x%lX] and "
          " peer CONT_ID[0x%lX], num acyc_link[%lu], num_cyc_link[%lu], self cont port_type[%lu]",
          self_cont_obj_ptr->container_id,
          tgt_peer_cont_obj_ptr->container_id,
          self_cont_obj_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_ACYCLIC][self_cont_port_type].num_nodes,
          self_cont_obj_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_CYCLIC][self_cont_port_type].num_nodes,
          self_cont_port_type);

   return AR_EOK;
}

static ar_result_t apm_clear_cont_list_graph_sort_info(apm_cont_graph_t *cont_graph_ptr)
{
   ar_result_t      result = AR_EOK;
   spf_list_node_t *curr_node_ptr;
   apm_container_t *curr_cont_obj_ptr;

   /** Get the pointer to list of containers present in the
    *  current connected graph */
   curr_node_ptr = cont_graph_ptr->container_list_ptr;

   /** Iterate over the list of containers present in the connected
    *  graph and clear the graph sort book keeping info  */
   while (curr_node_ptr)
   {
      /** Get the pointer to current container   */
      curr_cont_obj_ptr = (apm_container_t *)curr_node_ptr->obj_ptr;

      /** Clear the book keeping   */
      memset(&curr_cont_obj_ptr->graph_utils, 0, sizeof(apm_cont_graph_utils_t));

      /** Advance to next node in the list   */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return result;
}

static ar_result_t apm_sort_cont_graph_proc_order(apm_graph_info_t *graph_info_ptr, apm_cont_graph_t *cont_graph_ptr)
{
   ar_result_t                   result             = AR_EOK;
   spf_list_node_t *             dfs_temp_stack_ptr = NULL;
   spf_list_node_t *             curr_cont_node_ptr, *curr_peer_cont_node_ptr;
   apm_container_t *             stack_top_cont_obj_ptr;
   apm_container_t *             temp_cont_obj_ptr, *curr_peer_cont_obj_ptr, *prev_peer_cont_obj_ptr;
   spf_list_node_t *             curr_self_port_list_node_ptr;
   apm_cont_port_connect_info_t *self_cont_out_port_list_obj_ptr;
   spf_list_node_t *             sorted_cont_list_ptr = NULL;
   bool_t                        evaluate_next_node_on_stack = FALSE;
   uint32_t                      bailout_cntr = 0;

   /** Get the pointer to list of containers present in the
    *  current container graph */
   curr_cont_node_ptr = cont_graph_ptr->container_list_ptr;

   /** Iterate over un-sorted container list and populate the
    *  out-degree for each container */
   while (curr_cont_node_ptr)
   {
      /** Get the pointer to container graph */
      temp_cont_obj_ptr = (apm_container_t *)curr_cont_node_ptr->obj_ptr;

      /** Initialize the out-degree counter for this container */
      temp_cont_obj_ptr->graph_utils.out_degree = 0;

      /** Get the pointer list of container port connection per
       *  sg-id pair */
      curr_self_port_list_node_ptr =
         temp_cont_obj_ptr->cont_ports_per_sg_pair[APM_OBJ_TYPE_ACYCLIC][PORT_TYPE_DATA_OP].list_ptr;

      /** Iterate over the list of output connections   */
      while (curr_self_port_list_node_ptr)
      {
         /** Get the pointer to o/p connection obj   */
         self_cont_out_port_list_obj_ptr = (apm_cont_port_connect_info_t *)curr_self_port_list_node_ptr->obj_ptr;

         /** Aggregate the number of peer containers */
         temp_cont_obj_ptr->graph_utils.out_degree += self_cont_out_port_list_obj_ptr->peer_cont_list.num_nodes;

#ifdef APM_DBG_GRAPH_UPDATE
         AR_MSG(DBG_HIGH_PRIO,
                "apm_sort_cont_graph_proc_order(): preprocess: CONT_ID[0x%lX] is visited %lu out_degree %lu num_peers "
                "%lu",
                temp_cont_obj_ptr->container_id,
                temp_cont_obj_ptr->graph_utils.node_visted,
                temp_cont_obj_ptr->graph_utils.out_degree,
                self_cont_out_port_list_obj_ptr->peer_cont_list.num_nodes);
#endif

         /** Iterate over the list of peer containers   */
         spf_list_node_t *temp_list_ptr = self_cont_out_port_list_obj_ptr->peer_cont_list.list_ptr;
         while (temp_list_ptr)
         {
            apm_container_t *peer_cntr_ptr = (apm_container_t *)temp_list_ptr->obj_ptr;

#ifdef APM_DBG_GRAPH_UPDATE
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_sort_cont_graph_proc_order(): preprocess: CONT_ID[0x%lX] PEER_CONT_ID[0x%lX]",
                   temp_cont_obj_ptr->container_id,
                   peer_cntr_ptr->container_id);
#endif

            temp_list_ptr = temp_list_ptr->next_ptr;
         }

         /** Advance to next node in the list */
         curr_self_port_list_node_ptr = curr_self_port_list_node_ptr->next_ptr;
      }

      /** Advance the list pointer to point to next container */
      curr_cont_node_ptr = curr_cont_node_ptr->next_ptr;
   }

   /** Iterate until the unsorted contaliner list is non-empty   */
   while (cont_graph_ptr->container_list_ptr)
   {
      /** Sort the list of container using iterative DFS  */
      curr_cont_node_ptr = cont_graph_ptr->container_list_ptr;

      temp_cont_obj_ptr = (apm_container_t *)curr_cont_node_ptr->obj_ptr;

      /** Push current container on temp DFS stack */
      if (AR_EOK !=
          (result =
              spf_list_insert_head(&dfs_temp_stack_ptr, temp_cont_obj_ptr, APM_INTERNAL_STATIC_HEAP_ID, TRUE /** Uses pool*/)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_sort_cont_graph_proc_order(): Failed to push CONT_ID[0x%lX] to dfs stack ",
                temp_cont_obj_ptr->container_id,
                result);

         goto __bailout_sort_cont_graph_proc_order;
      }

#ifdef APM_DBG_GRAPH_UPDATE

      AR_MSG(DBG_HIGH_PRIO,
             "apm_sort_cont_graph_proc_order(): Pushed CONT_ID[0x%lX] to stack",
             temp_cont_obj_ptr->container_id);
#endif

      /** Sorted node: Sorting is finished and it has been placed
       *  in output list
       *  Visited node: Node has been visited once as part of graph
       *  traversal during DFS
       *
       *  During graph traversal If a node is visited twice during
       *  ,then a cycle is detected. This cyclic data link is
       *  removed from regular connections and stored separately in
       *  different book keeping under container. */

      while (dfs_temp_stack_ptr)
      {
         /** Reset the flag to keep track if new node is pushed on to
          *  stack */
         evaluate_next_node_on_stack = FALSE;

         /** Get top of stack   */
         stack_top_cont_obj_ptr = (apm_container_t *)dfs_temp_stack_ptr->obj_ptr;

#ifdef APM_DBG_GRAPH_UPDATE
         AR_MSG(DBG_HIGH_PRIO,
                "apm_sort_cont_graph_proc_order(): Marked CONT_ID[0x%lX] is visited %lu out_degree %lu",
                stack_top_cont_obj_ptr->container_id,
                stack_top_cont_obj_ptr->graph_utils.node_visted,
                stack_top_cont_obj_ptr->graph_utils.out_degree);
#endif

         /** There should be no sorted node present in the temp stack  */
         if (stack_top_cont_obj_ptr->graph_utils.node_sorted)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_sort_cont_graph_proc_order(): Found sorted CONT_ID[0x%lX] in dfs out stack",
                   stack_top_cont_obj_ptr->container_id);

            result = AR_EFAILED;

            goto __bailout_sort_cont_graph_proc_order;
         }
         else if (!stack_top_cont_obj_ptr->graph_utils.node_visted)
         {
            /** If this node has been visited previously, mark it as
             *  visted node */
            stack_top_cont_obj_ptr->graph_utils.node_visted = TRUE;

#ifdef APM_DBG_GRAPH_UPDATE
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_sort_cont_graph_proc_order(): Marked CONT_ID[0x%lX] as visited",
                   stack_top_cont_obj_ptr->container_id);
#endif
         }

         /** For the current node, if there are output connections
          *  present, then push each un-visted o/p, on the stack. If a
          *  visted node is found during this process, cycle has been
          *  detected */
         if (stack_top_cont_obj_ptr->graph_utils.out_degree)
         {
            /** Increment bailout counter to detect any infinite loop.
             *  This may be possible because of any corruptions in
             *  container port handle book keeping. */
            bailout_cntr++;

            if (bailout_cntr > APM_MAX_SORT_LOOP_ITR)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "apm_sort_cont_graph_proc_order(): Possible infinite loop detected, bailing out");

               result = AR_EFAILED;

               goto __bailout_sort_cont_graph_proc_order;
            }

            /** Get the pointer to output data connections for current
             *  container */
            curr_self_port_list_node_ptr =
               stack_top_cont_obj_ptr->cont_ports_per_sg_pair[APM_OBJ_TYPE_ACYCLIC][PORT_TYPE_DATA_OP].list_ptr;

            /** Init prev cont obj ptr to NULL   */
            prev_peer_cont_obj_ptr = NULL;

            /** Iterate over the list of output connections   */
            while (curr_self_port_list_node_ptr)
            {
               /** Get the pointer to o/p connection obj   */
               self_cont_out_port_list_obj_ptr = (apm_cont_port_connect_info_t *)curr_self_port_list_node_ptr->obj_ptr;

               /** Iterate over the list of peer containers   */
               curr_peer_cont_node_ptr = self_cont_out_port_list_obj_ptr->peer_cont_list.list_ptr;

               while (curr_peer_cont_node_ptr)
               {
                  /** Retrieve the o/p peer container obj   */
                  curr_peer_cont_obj_ptr = curr_peer_cont_node_ptr->obj_ptr;

                  /** If peer container is already sorted, don't traverse this
                   *  path again. Or If there are multiple links across same
                   *  containers, push
                   *  this container on dfs stack only once */
                  if ((prev_peer_cont_obj_ptr == curr_peer_cont_obj_ptr) ||
                      curr_peer_cont_obj_ptr->graph_utils.node_sorted)
                  {
                     curr_peer_cont_node_ptr = curr_peer_cont_node_ptr->next_ptr;

                     continue;
                  }

                  /** Set previous to current   */
                  prev_peer_cont_obj_ptr = curr_peer_cont_obj_ptr;

                  /** If peer container has not been visited, push it on temp
                   *  stack */
                  if (!curr_peer_cont_obj_ptr->graph_utils.node_visted)
                  {
                     if (AR_EOK != (result = spf_list_insert_head(&dfs_temp_stack_ptr,
                                                                  curr_peer_cont_obj_ptr,
                                                                  APM_INTERNAL_STATIC_HEAP_ID,
                                                                  TRUE /**Use pool*/)))
                     {
                        AR_MSG(DBG_ERROR_PRIO,
                               "apm_sort_cont_graph_proc_order(): Failed to push peer CONT_ID[0x%lX] node on stack, "
                               "curr "
                               "CONT_ID[0x%lX]",
                               curr_peer_cont_obj_ptr->container_id,
                               stack_top_cont_obj_ptr->container_id);

                        goto __bailout_sort_cont_graph_proc_order;
                     }

#ifdef APM_DBG_GRAPH_UPDATE
                     AR_MSG(DBG_HIGH_PRIO,
                            "apm_sort_cont_graph_proc_order(): Pushed peer CONT_ID[0x%lX] as visited, curr "
                            "CONT_ID[0x%lX]",
                            curr_peer_cont_obj_ptr->container_id,
                            stack_top_cont_obj_ptr->container_id);
#endif
                  }
                  else /** Found visited node */
                  {
                     /** Cycle detected   */

                     AR_MSG(DBG_HIGH_PRIO,
                            "apm_sort_cont_graph_proc_order():: CYCLIC DATA LINK DETECTED :: between CONT_ID[0x%lX] "
                            "and "
                            "peer CONT_ID[0x%lX]",
                            stack_top_cont_obj_ptr->container_id,
                            curr_peer_cont_obj_ptr->container_id);

                     /** Cachec output data links of the current container to the
                      *  peer which has been visited twice as cyclic, in current
                      *  container context */
                     if (AR_EOK != (result = apm_cont_cache_curr_cyclic_data_link(stack_top_cont_obj_ptr,
                                                                                  curr_peer_cont_obj_ptr,
                                                                                  PORT_TYPE_DATA_OP)))
                     {
                        goto __bailout_sort_cont_graph_proc_order;
                     }

                     /** Cachec input data links of the peer container to the
                      *  current self container  as cyclic */
                     if (AR_EOK != (result = apm_cont_cache_curr_cyclic_data_link(curr_peer_cont_obj_ptr,
                                                                                  stack_top_cont_obj_ptr,
                                                                                  PORT_TYPE_DATA_IP)))
                     {
                        goto __bailout_sort_cont_graph_proc_order;
                     }

                     /** Decremnt the output degree for current container */
                     stack_top_cont_obj_ptr->graph_utils.out_degree--;
                  }

                  evaluate_next_node_on_stack = TRUE;

                  break;

                  /** Adavnce to next node in the list   */
                  curr_peer_cont_node_ptr = curr_peer_cont_node_ptr->next_ptr;

               } /** End of while (peer container list) */

               if (evaluate_next_node_on_stack)
               {
                  break;
               }

               /** Advance to next connection node   */
               curr_self_port_list_node_ptr = curr_self_port_list_node_ptr->next_ptr;

            } /** End of while(container out port connection list) */
         }
         else /** No output ports */
         {
            /** Reset bailout counter  */
            bailout_cntr = 0;

            /** If the current container has no output port, we have
             *  reached the leaf node. Mark it as sorted */
            stack_top_cont_obj_ptr->graph_utils.node_sorted = TRUE;

            /** Pop the node from the temp stack and push it to result
             *  stack */
            if (NULL == (temp_cont_obj_ptr = (apm_container_t *)spf_list_pop_head(&dfs_temp_stack_ptr, TRUE)))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "apm_sort_cont_graph_proc_order(): Failed to pop sorted CONT_ID[0x%lX] node on stack top",
                      stack_top_cont_obj_ptr->container_id);

               goto __bailout_sort_cont_graph_proc_order;
            }

#ifdef APM_DBG_GRAPH_UPDATE
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_sort_cont_graph_proc_order(): Popped  CONT_ID[0x%lX] from stack",
                   temp_cont_obj_ptr->container_id);
#endif

            /** Push the latest sorted node on top of list as the sorting
             *  works backwards starting with nodes with zero output */
            if (AR_EOK != (result = spf_list_insert_head(&sorted_cont_list_ptr,
                                                         stack_top_cont_obj_ptr,
                                                         APM_INTERNAL_STATIC_HEAP_ID,
                                                         TRUE /* use_pool*/)))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "apm_sort_cont_graph_proc_order(): Failed to add CONT_ID[0x%lX] to sorted o/p list ",
                      stack_top_cont_obj_ptr->container_id,
                      result);

               goto __bailout_sort_cont_graph_proc_order;
            }

#ifdef APM_DBG_GRAPH_UPDATE
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_sort_cont_graph_proc_order(): Pushed  CONT_ID[0x%lX] to sorted output list",
                   stack_top_cont_obj_ptr->container_id);
#endif

            /** Iterate over the input list of current container and
             *  decrement their out degrees */

            curr_self_port_list_node_ptr =
               stack_top_cont_obj_ptr->cont_ports_per_sg_pair[APM_OBJ_TYPE_ACYCLIC][PORT_TYPE_DATA_IP].list_ptr;

            while (curr_self_port_list_node_ptr)
            {
               self_cont_out_port_list_obj_ptr = (apm_cont_port_connect_info_t *)curr_self_port_list_node_ptr->obj_ptr;

               /** For each list of connections per sub-graph ID pair,
                *  update the out degree of all the upstream containers */

               curr_peer_cont_node_ptr = self_cont_out_port_list_obj_ptr->peer_cont_list.list_ptr;

               while (curr_peer_cont_node_ptr)
               {
                  curr_peer_cont_obj_ptr = curr_peer_cont_node_ptr->obj_ptr;

                  /** Decrement out degree  */
                  curr_peer_cont_obj_ptr->graph_utils.out_degree--;

                  /** Advance to next node in the list   */
                  curr_peer_cont_node_ptr = curr_peer_cont_node_ptr->next_ptr;
               }

               /** Advance to next node in the list   */
               curr_self_port_list_node_ptr = curr_self_port_list_node_ptr->next_ptr;
            }

            /** Delete this container from the list of unsorted
             *  containers */
            if (FALSE == spf_list_find_delete_node(&cont_graph_ptr->container_list_ptr,
                                                   stack_top_cont_obj_ptr,
                                                   TRUE /** Pool used*/))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "apm_sort_cont_graph_proc_order(): CONT_ID[0x%lX] not found in un-sorted list, result: 0x%lX ",
                      stack_top_cont_obj_ptr->container_id,
                      result);

               goto __bailout_sort_cont_graph_proc_order;
            }

#ifdef APM_DBG_GRAPH_UPDATE
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_sort_cont_graph_proc_order(): Removed  CONT_ID[0x%lX] from un-sorted container list",
                   stack_top_cont_obj_ptr->container_id);
#endif

         } /** End of else(out degree == 0) */

      } /** While stack not empty */

   } /** End of while(unsorted container list) */

   /** Save the sorted container list */
   cont_graph_ptr->container_list_ptr = sorted_cont_list_ptr;

   /** Update the tail node for this container list */
   if (AR_EOK !=
       (result = spf_list_get_tail_node(cont_graph_ptr->container_list_ptr, &cont_graph_ptr->cont_list_tail_node_ptr)))
   {
      goto __bailout_sort_cont_graph_proc_order;
   }

   /** Set the graph sorted flag */
   cont_graph_ptr->graph_is_sorted = TRUE;

   /** Clear the temporary book keeping within container for
    *  topo sort */
   result = apm_clear_cont_list_graph_sort_info(cont_graph_ptr);

#ifdef APM_DBG_GRAPH_UPDATE
   if (AR_EOK == result)
   {
      apm_print_sorted_container_graph(cont_graph_ptr);
   }
#endif

   return result;

__bailout_sort_cont_graph_proc_order:

   /** If there is any partially sorted container list, merge it
    *  back to unsorted list of container   */
   if (cont_graph_ptr->container_list_ptr && sorted_cont_list_ptr)
   {
      spf_list_merge_lists(&cont_graph_ptr->container_list_ptr, &sorted_cont_list_ptr);
   }

   /** Clear the temporary book keeping within container for
    *  topo sort */
   apm_clear_cont_list_graph_sort_info(cont_graph_ptr);

   /** Delete temporary output list */
   if (dfs_temp_stack_ptr)
   {
      spf_list_delete_list(&dfs_temp_stack_ptr, TRUE /** Pool used*/);
   }

   return result;
}

static ar_result_t apm_update_standalone_cont_graph(apm_graph_info_t *graph_info_ptr)
{
   ar_result_t      result = AR_EOK;
   spf_list_node_t *curr_node_ptr, *next_node_ptr;
   apm_container_t *container_node_ptr;

   if (!graph_info_ptr->standalone_cont_list_ptr)
   {
      return result;
   }

   /** If standalone container list is non-null, create graph
    *  for each such container */

   /** Get the pointer to start of standalone container list */
   curr_node_ptr = graph_info_ptr->standalone_cont_list_ptr;

   /** Iterate over the standalone contaliner list */
   while (curr_node_ptr)
   {
      /** Get the container node pointer */
      container_node_ptr = (apm_container_t *)curr_node_ptr->obj_ptr;

      /** Get the next node in the list  */
      next_node_ptr = curr_node_ptr->next_ptr;

      /** Create graph for this container. This function also
       *  removes the current container from list of standalone
       *  containers */
      if (AR_EOK != (result = apm_create_cont_graph(graph_info_ptr, container_node_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_update_standalone_cont_graph: Failed to create graph for standalone cont id[0x%lX]",
                container_node_ptr->container_id);

         break;
      }

      /** Advance to next node in the list  */
      curr_node_ptr = next_node_ptr;
   }

   return result;
}

ar_result_t apm_update_cont_graph(apm_graph_info_t *graph_info_ptr,
                                  apm_container_t * src_cont_node_ptr,
                                  apm_container_t * dst_cont_node_ptr,
                                  bool_t            retain_sorted_cont_graph)
{
   ar_result_t       result = AR_EOK;
   spf_list_node_t * curr_node_ptr;
   apm_container_t * curr_dst_cont_node_ptr;
   apm_cont_graph_t *dst_cont_host_graph_ptr;
   apm_container_t * cont_to_remove_from_standalone_list_ptr = NULL;

   /** If both source and destination containers are already
    *  part of a graph, and both the graphs are same, skip this
    *  pair. This will be true if there are more than 1 port
    *  connections between existing src-dst container pair */

   if ((src_cont_node_ptr->cont_graph_ptr && dst_cont_node_ptr->cont_graph_ptr) &&
       (src_cont_node_ptr->cont_graph_ptr == dst_cont_node_ptr->cont_graph_ptr))
   {
      src_cont_node_ptr->cont_graph_ptr->graph_is_sorted = retain_sorted_cont_graph;

#ifdef APM_DBG_GRAPH_UPDATE
      AR_MSG(DBG_HIGH_PRIO,
             "apm_update_cont_graph: Same host graph for src cont id[0x%lX], dst cont id[0x%lX]",
             src_cont_node_ptr->container_id,
             dst_cont_node_ptr->container_id);
#endif

      return AR_EOK;
   }

   /** If both source and destination graph pointers are NULL,
    *  then create new graph node and add these containers to that
    *  graph */
   if (!src_cont_node_ptr->cont_graph_ptr && !dst_cont_node_ptr->cont_graph_ptr)
   {
      /** Create container graph */
      if (AR_EOK != (result = apm_create_cont_graph(graph_info_ptr, src_cont_node_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_update_cont_graph: Failed to create graph for src cont id[0x%lX]",
                src_cont_node_ptr->container_id);

         return result;
      }

      /** Add the destination container to the source container
       *  graph */
      if (AR_EOK != (result = apm_db_add_node_to_list(&src_cont_node_ptr->cont_graph_ptr->container_list_ptr,
                                                      dst_cont_node_ptr,
                                                      &src_cont_node_ptr->cont_graph_ptr->num_containers)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_update_cont_graph: Failed to add dstn cont id[0x%lX] to graph",
                dst_cont_node_ptr->container_id);

         return result;
      }

      /** Remove this container from standalone container list */
      cont_to_remove_from_standalone_list_ptr = dst_cont_node_ptr;

      /** Add the destination container's sub-graph list to the
       *  container graph */
      apm_add_cont_sg_list_to_graph(src_cont_node_ptr->cont_graph_ptr, dst_cont_node_ptr);

      /** Update destination container host graph */
      dst_cont_node_ptr->cont_graph_ptr = src_cont_node_ptr->cont_graph_ptr;

      /** This graph need to be sorted, so clear the sorted flag */
      src_cont_node_ptr->cont_graph_ptr->graph_is_sorted = retain_sorted_cont_graph;

#ifdef APM_DBG_GRAPH_UPDATE

      AR_MSG(DBG_HIGH_PRIO,
             "apm_update_cont_graph: Created new graph for src cont id[0x%lX], added dst cont id[0x%lX], sort_status: "
             "[%lu], graph_ptr[0x%lX]",
             src_cont_node_ptr->container_id,
             dst_cont_node_ptr->container_id,
             src_cont_node_ptr->cont_graph_ptr->graph_is_sorted,
             src_cont_node_ptr->cont_graph_ptr);

#endif
   }

   /** If source container graph pointer is NULL and destination
    *  container graph is present, then add the source to the
    *  same graph as desination container */
   else if ((!src_cont_node_ptr->cont_graph_ptr && dst_cont_node_ptr->cont_graph_ptr) ||
            (src_cont_node_ptr->cont_graph_ptr && !dst_cont_node_ptr->cont_graph_ptr))
   {
      if (!src_cont_node_ptr->cont_graph_ptr)
      {
         apm_db_add_node_to_list(&dst_cont_node_ptr->cont_graph_ptr->container_list_ptr,
                                 src_cont_node_ptr,
                                 &dst_cont_node_ptr->cont_graph_ptr->num_containers);

         /** Remove this container from standalone container list */
         cont_to_remove_from_standalone_list_ptr = src_cont_node_ptr;

         /** Add the source container's sub-graph list to the
          *  container graph */
         apm_add_cont_sg_list_to_graph(dst_cont_node_ptr->cont_graph_ptr, src_cont_node_ptr);

         /** Update source container host graph */
         src_cont_node_ptr->cont_graph_ptr = dst_cont_node_ptr->cont_graph_ptr;

         /** This graph need to be sorted again, so clear the sorted
          *  flag */
         dst_cont_node_ptr->cont_graph_ptr->graph_is_sorted = retain_sorted_cont_graph;

#ifdef APM_DBG_GRAPH_UPDATE

         AR_MSG(DBG_HIGH_PRIO,
                "apm_update_cont_graph: Added src cont id[0x%lX], to graph_ptr[0x%lX], "
                "sort_status[%lu]",
                src_cont_node_ptr->container_id,
                dst_cont_node_ptr->cont_graph_ptr,
                dst_cont_node_ptr->cont_graph_ptr->graph_is_sorted);
#endif
      }
      else if (!dst_cont_node_ptr->cont_graph_ptr)
      {
         /** If destination container graph pointer is NULL and
          *  source container graph is present, then add the destination
          *  to the same graph as source container */
         apm_db_add_node_to_list(&src_cont_node_ptr->cont_graph_ptr->container_list_ptr,
                                 dst_cont_node_ptr,
                                 &src_cont_node_ptr->cont_graph_ptr->num_containers);

         /** Remove this container from standalone container list */
         cont_to_remove_from_standalone_list_ptr = dst_cont_node_ptr;

         /** Add the destination container's sub-graph list to the
          *  container graph */
         apm_add_cont_sg_list_to_graph(src_cont_node_ptr->cont_graph_ptr, dst_cont_node_ptr);

         /** Update destination container host graph */
         dst_cont_node_ptr->cont_graph_ptr = src_cont_node_ptr->cont_graph_ptr;

         /** This graph need to be sorted again, so clear the sorted
          *  flag */
         src_cont_node_ptr->cont_graph_ptr->graph_is_sorted = retain_sorted_cont_graph;

#ifdef APM_DBG_GRAPH_UPDATE

         AR_MSG(DBG_HIGH_PRIO,
                "apm_update_cont_graph: Added dst cont id[0x%lX], to graph_ptr[0x%lX], "
                "sort_status[%lu]",
                dst_cont_node_ptr->container_id,
                src_cont_node_ptr->cont_graph_ptr,
                src_cont_node_ptr->cont_graph_ptr->graph_is_sorted);

#endif
      }
   }

   /** If both source and destination containers are already
    *  part of a graph, and both the graphs are different, then
    *  need to merge the two graphs into source container's
    *  graph. Update all the container's in destination
    *  container's graph to point to source container graph */

   else if ((src_cont_node_ptr->cont_graph_ptr && dst_cont_node_ptr->cont_graph_ptr) &&
            (src_cont_node_ptr->cont_graph_ptr != dst_cont_node_ptr->cont_graph_ptr))
   {
      /** Cache the pointer to destination graph */
      dst_cont_host_graph_ptr = dst_cont_node_ptr->cont_graph_ptr;

      /** Update the host graph for all the containers in
       *  destination list */
      curr_node_ptr = dst_cont_node_ptr->cont_graph_ptr->container_list_ptr;

      /** Iterate over the list of containers in destination graph */
      while (curr_node_ptr)
      {
         curr_dst_cont_node_ptr = (apm_container_t *)curr_node_ptr->obj_ptr;

         /** Update the destination container's host graph */
         curr_dst_cont_node_ptr->cont_graph_ptr = src_cont_node_ptr->cont_graph_ptr;

         /** Advance to next node */
         curr_node_ptr = curr_node_ptr->next_ptr;
      }

      /** Merge container list of source and destination graphs   */
      spf_list_merge_lists(&src_cont_node_ptr->cont_graph_ptr->container_list_ptr,
                           &dst_cont_host_graph_ptr->container_list_ptr);

      /** Update the number of total containers after merge */
      src_cont_node_ptr->cont_graph_ptr->num_containers += dst_cont_host_graph_ptr->num_containers;

      /** Add the destination container's sub-graph list to the source
       *  container graph */
      apm_merge_cont_graph_sg_list(src_cont_node_ptr->cont_graph_ptr, dst_cont_host_graph_ptr);

#ifdef APM_DBG_GRAPH_UPDATE

      AR_MSG(DBG_HIGH_PRIO,
             "apm_update_cont_graph: Src cont[0x%lX], dstn cont[0x%lX], merged graph_ptr[0x%lX], with "
             "graph_ptr[0x%lX], "
             "total cont[%lu], total sg[%lu], sort_status[%lu]",
             src_cont_node_ptr->container_id,
             dst_cont_node_ptr->container_id,
             src_cont_node_ptr->cont_graph_ptr,
             dst_cont_host_graph_ptr,
             src_cont_node_ptr->cont_graph_ptr->num_containers,
             src_cont_node_ptr->cont_graph_ptr->num_sub_graphs,
             retain_sorted_cont_graph);
#endif

      /** Delete destination container's host graph after merge */
      apm_delete_cont_graph(graph_info_ptr, dst_cont_host_graph_ptr);

      /** The source container graph need to be sorted, so clear the
       *  sorted flag */
      src_cont_node_ptr->cont_graph_ptr->graph_is_sorted = retain_sorted_cont_graph;
   }
   else /** Unexpected scenario */
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_update_cont_graph: Un-expected config, Src cont[0x%lX], dstn cont[0x%lX], src graph_ptr[0x%lX], dst "
             "graph_ptr[0x%lX] ",
             src_cont_node_ptr->container_id,
             dst_cont_node_ptr->container_id,
             src_cont_node_ptr->cont_graph_ptr,
             dst_cont_node_ptr->cont_graph_ptr);

      result = AR_EFAILED;
   }

   /** Remove this container from standalone container list */
   if (graph_info_ptr->standalone_cont_list_ptr && cont_to_remove_from_standalone_list_ptr)
   {
      if (FALSE == spf_list_find_delete_node(&graph_info_ptr->standalone_cont_list_ptr,
                                             cont_to_remove_from_standalone_list_ptr,
                                             TRUE /*pool_used*/))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_update_cont_graph(): Failed to find COND_ID[0x%lX], from standalone cntr list",
                cont_to_remove_from_standalone_list_ptr->container_id);
      }
   }

   return result;
}

static ar_result_t apm_clear_cont_cyclic_link_info(apm_cont_graph_t *cont_graph_ptr)
{
   ar_result_t      result = AR_EOK;
   spf_list_node_t *curr_node_ptr;
   apm_container_t *curr_cont_obj_ptr;

   curr_node_ptr = cont_graph_ptr->container_list_ptr;

   /** Iterate over the list of all the containers in the graph.
    *  If there are cyclic link present, merge them with acyclic
    *  link. Because of a graph open/close operation the graph
    *  shape might change result in existing cycles to be
    *  removed or new cycles created.
    *  The operation is required only on data ports. Control
    *  ports are not considered in the operation below. */

   while (curr_node_ptr)
   {
      curr_cont_obj_ptr = (apm_container_t *)curr_node_ptr->obj_ptr;

      for (uint32_t data_port_type = 0; data_port_type < APM_NUM_DATA_PORT_TYPE; data_port_type++)
      {
         /** At this point, container may have all the acyclic data
          *  links closed because of a graph close operation */
         if (curr_cont_obj_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_CYCLIC][data_port_type].list_ptr)
         {
            /** Merge the acyclic and cyclic port link lists */
            if (AR_EOK !=
                (result = spf_list_merge_lists(&curr_cont_obj_ptr
                                                   ->cont_ports_per_sg_pair[APM_PORT_TYPE_ACYCLIC][data_port_type]
                                                   .list_ptr,
                                               &curr_cont_obj_ptr
                                                   ->cont_ports_per_sg_pair[APM_PORT_TYPE_CYCLIC][data_port_type]
                                                   .list_ptr)))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "apm_clear_cont_cyclic_link_info(): Failed to merge cyclic data links with acyclic links, "
                      "CONT_ID[0x%lX], result: 0x%lX",
                      curr_cont_obj_ptr->container_id,
                      result);

               return result;
            }

            /** Increment the number of acyclic data ports  */
            curr_cont_obj_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_ACYCLIC][data_port_type].num_nodes +=
               curr_cont_obj_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_CYCLIC][data_port_type].num_nodes;

            /** Clear the cyclic link info ptr   */
            memset(&curr_cont_obj_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_CYCLIC][data_port_type],
                   0,
                   sizeof(apm_list_t));

            AR_MSG(DBG_MED_PRIO,
                   "apm_clear_cont_cyclic_link_info(): Merged cyclic data links with acyclic links list "
                   "CONT_ID[0x%lX], port_type[%lu]",
                   curr_cont_obj_ptr->container_id,
                   data_port_type);
         }
      }

      /** Advance to next node in the list   */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return result;
}

ar_result_t apm_update_cont_graph_list(apm_graph_info_t *graph_info_ptr)
{
   ar_result_t       result = AR_EOK;
   spf_list_node_t * curr_graph_node_ptr;
   apm_cont_graph_t *cont_graph_ptr;

   /** If list of container graphs is empty and no standalone
    *  containers, nothing needs to be done */
   if (!graph_info_ptr->num_cont_graphs && !graph_info_ptr->standalone_cont_list_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO, "apm_update_cont_graph_list(): Cont graph and stand alone cont graph list is empty");

      return AR_EOK;
   }

   /** Iterate through the list of graphs, identify the graphs
    *  that need to be sorted and sort them individually */

   curr_graph_node_ptr = graph_info_ptr->cont_graph_list_ptr;

   /** Iterate over list of container graphs */
   while (curr_graph_node_ptr)
   {
      cont_graph_ptr = (apm_cont_graph_t *)curr_graph_node_ptr->obj_ptr;

      /** If the container graph is not already sorted */
      if (!cont_graph_ptr->graph_is_sorted)
      {
         /** First clear all the cyclic links as after each graph
          *  change, the cyclic graph needs to be re-evaluated */
         if (AR_EOK != (result = apm_clear_cont_cyclic_link_info(cont_graph_ptr)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_update_cont_graph_list(): Failed to clear container's cyclic data link, result: 0x%lX",
                   result);
            return result;
         }

         if (AR_DID_FAIL(result = apm_sort_cont_graph_proc_order(graph_info_ptr, cont_graph_ptr)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_update_cont_graph_list(): Failed to sort container graph, result: 0x%lX",
                   result);
            return result;
         }
      }

      /** Update the sub-graph list for this graph */

      /** Advance to next graph in the list */
      curr_graph_node_ptr = curr_graph_node_ptr->next_ptr;

   } /** End of while() */

   /** After graph sorting, update the graphs for standalone
    *  containers */
   result = apm_update_standalone_cont_graph(graph_info_ptr);

   return result;
}

ar_result_t apm_remove_cont_from_graph(apm_graph_info_t *graph_info_ptr, apm_container_t *container_node_ptr)
{
   ar_result_t result = AR_EOK;

   /** Remove this container from the associated graph */
   apm_db_remove_node_from_list(&container_node_ptr->cont_graph_ptr->container_list_ptr,
                                container_node_ptr,
                                &container_node_ptr->cont_graph_ptr->num_containers);

   /** Container graph needs to be resorted now as composition
    *  has changed */
   container_node_ptr->cont_graph_ptr->graph_is_sorted = FALSE;

   /** If the number of containers reaches zero, delete this
    *  graph */
   if (!container_node_ptr->cont_graph_ptr->num_containers)
   {
      apm_delete_cont_graph(graph_info_ptr, container_node_ptr->cont_graph_ptr);

      container_node_ptr->cont_graph_ptr = NULL;
   }

   return result;
}

ar_result_t apm_remove_sg_from_cont_graph(apm_graph_info_t *graph_info_ptr, apm_sub_graph_t *target_sg_node_ptr)
{
   ar_result_t       result = AR_EOK;
   spf_list_node_t * curr_graph_node_ptr;
   apm_cont_graph_t *graph_node_ptr;
   spf_list_node_t * curr_sg_node_ptr;
   apm_sub_graph_t * sg_node_ptr;

   /** Get the pointer to container graph list for this
    *  sub-grapah */
   curr_graph_node_ptr = graph_info_ptr->cont_graph_list_ptr;

   while (curr_graph_node_ptr)
   {
      /** Get the graph node */
      graph_node_ptr = (apm_cont_graph_t *)curr_graph_node_ptr->obj_ptr;

      /** Get the list of sub-graphs for this graph */
      curr_sg_node_ptr = graph_node_ptr->sub_graph_list_ptr;

      /** Iterate over the list of sub-graphs for this graph */
      while (curr_sg_node_ptr)
      {
         sg_node_ptr = (apm_sub_graph_t *)curr_sg_node_ptr->obj_ptr;

         /** If the matching sub-graph object found */
         if (sg_node_ptr == target_sg_node_ptr)
         {
            /** Remove this sub-graph from the APM global container graph
             *  list */
            apm_db_remove_node_from_list(&graph_node_ptr->sub_graph_list_ptr,
                                         target_sg_node_ptr,
                                         &graph_node_ptr->num_sub_graphs);

            /** Break the loop if matching sub-graph node found */
            break;
         }

         /** Advance to next node */
         curr_sg_node_ptr = curr_sg_node_ptr->next_ptr;
      }

      /** Get next graph node */
      curr_graph_node_ptr = curr_graph_node_ptr->next_ptr;

   } /** End of while (graph_list) */

   return result;
}

ar_result_t apm_gm_cmd_get_cont_graph_node(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr, spf_list_node_t **graph_node_pptr)
{
   spf_list_node_t * curr_graph_node_ptr;
   apm_cont_graph_t *graph_node_ptr;
   spf_list_node_t * curr_sg_node_ptr;
   apm_sub_graph_t * sub_graph_node_ptr;
   spf_list_node_t * curr_gm_cmd_sg_node_ptr;
   apm_sub_graph_t * gm_cmd_sub_graph_node_ptr;
   spf_list_node_t * curr_port_hdl_sg_node_ptr;
   apm_sub_graph_t * port_hdl_sg_node_ptr;

   if (!apm_cmd_ctrl_ptr || !(*graph_node_pptr))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_gm_cmd_get_cont_graph_node(): Invalid i/p arg, apm_cmd_ctrl_ptr[0x%lX], graph_node_ptr[0x%lX]",
             apm_cmd_ctrl_ptr,
             *graph_node_pptr);

      return AR_EFAILED;
   }

   /** Get the pointer to graph list node */
   curr_graph_node_ptr = *graph_node_pptr;

   /** Iterate over the list of container graphs */
   while (curr_graph_node_ptr)
   {
      /** Get the pointer to graph node obj */
      graph_node_ptr = (apm_cont_graph_t *)curr_graph_node_ptr->obj_ptr;

      /** Get the pointer to list of sub-graph ID's belonging to
       *  this graph */
      curr_sg_node_ptr = graph_node_ptr->sub_graph_list_ptr;

      /** Iterate over the list of sub-graph IDs for the current
       *  container graph */
      while (curr_sg_node_ptr)
      {
         /** Get the pointer to sub-graph node obj */
         sub_graph_node_ptr = (apm_sub_graph_t *)curr_sg_node_ptr->obj_ptr;

         /** Get the pointer to sub-graph ID list under process  */
         curr_gm_cmd_sg_node_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr;

         /** Iterate over the list of sub-graph ID's received directly
          *  as part of the graph management command */
         while (curr_gm_cmd_sg_node_ptr)
         {
            /** Get the pointer to sub-graph node obj */
            gm_cmd_sub_graph_node_ptr = (apm_sub_graph_t *)curr_gm_cmd_sg_node_ptr->obj_ptr;

            /** Check if the sub-graph ID matches */
            if (gm_cmd_sub_graph_node_ptr->sub_graph_id == sub_graph_node_ptr->sub_graph_id)
            {
               /** Update the return graph pointer */
               *graph_node_pptr = curr_graph_node_ptr;

               return AR_EOK;
            }

            /** Advance to next node in the list */
            curr_gm_cmd_sg_node_ptr = curr_gm_cmd_sg_node_ptr->next_ptr;

         } /** End of while(gm cmd sub-graph list) */

         /** Iterate over the list of sub-graph ID's that are impacted
          *  indirectly as part of the data/control link close command */

         curr_port_hdl_sg_node_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr;

         while (curr_port_hdl_sg_node_ptr)
         {
            port_hdl_sg_node_ptr = (apm_sub_graph_t *)curr_port_hdl_sg_node_ptr->obj_ptr;

            /** Check if the sub-graph ID matches */
            if (port_hdl_sg_node_ptr->sub_graph_id == sub_graph_node_ptr->sub_graph_id)
            {
               /** Update the return graph pointer */
               *graph_node_pptr = curr_graph_node_ptr;

               return AR_EOK;
            }

            /** Advance to next node in the list */
            curr_port_hdl_sg_node_ptr = curr_port_hdl_sg_node_ptr->next_ptr;

         } /** End of while(port handle sub-graph list) */

         /** Advance to next sub-graph node */
         curr_sg_node_ptr = curr_sg_node_ptr->next_ptr;

      } /** End of while (container graph sub-graph list) */

      /** Advance to next node */
      curr_graph_node_ptr = curr_graph_node_ptr->next_ptr;

   } /** End of while (container graph list) */

   /** If the execution reaches here, the graph node
    *  corresponding to sub-graph list received as part of GM
    *  command could not be found. Return failure to the caller */
   return AR_EFAILED;
}

ar_result_t apm_set_up_graph_list_traversal(apm_graph_info_t *graph_info_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t       result = AR_EOK;
   spf_list_node_t * curr_cont_node_ptr;
   apm_cont_graph_t *graph_node_ptr;
   spf_list_node_t * curr_graph_node_ptr;

   /** Get the pointer to list of container graphs  */
   curr_graph_node_ptr = graph_info_ptr->cont_graph_list_ptr;

   if (!curr_graph_node_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_set_graph_mgt_cmd_ctrl(): APM graph list is empty, cmd_opcode[0x%08lx]",
             apm_cmd_ctrl_ptr->cmd_opcode);

      return AR_EFAILED;
   }

   /** Get the pointer to the first graph belonging to sub-graph
    *  ID under process for graph management command under
    *  context */
   if (AR_EOK != (result = apm_gm_cmd_get_cont_graph_node(apm_cmd_ctrl_ptr, &curr_graph_node_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_set_up_graph_list_traversal(), Failed to get cont graph node to process, cmd_opcode[0x%08lx]",
             apm_cmd_ctrl_ptr->cmd_opcode);

      return AR_EFAILED;
   }

   /** Get the pointer to graph node obj */
   graph_node_ptr = (apm_cont_graph_t *)curr_graph_node_ptr->obj_ptr;

   /** Initialize the graph mgmt command control state */
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_graph_ptr = curr_graph_node_ptr;

   /** Get the pointer to start of the list of containers within
    *  this graph */
   curr_cont_node_ptr = graph_node_ptr->container_list_ptr;

   /** Initialize the graph mgmt command control state */
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_node_ptr = curr_cont_node_ptr;

   return result;
}

ar_result_t apm_gm_cmd_get_next_cont_in_sorted_list(apm_cmd_ctrl_t *  apm_cmd_ctrl_ptr,
                                                    apm_container_t **container_node_pptr)
{
   ar_result_t       result = AR_EOK;
   spf_list_node_t * curr_cont_node_ptr;
   spf_list_node_t * curr_graph_node_ptr;
   apm_cont_graph_t *graph_node_ptr;

   /** Init the return container pointer */
   *container_node_pptr = NULL;

   /** Get current container in the graph being processed */
   curr_cont_node_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_node_ptr;

   /** Advance the container node ptr */
   curr_cont_node_ptr = curr_cont_node_ptr->next_ptr;

   /** If the container list is non-empty */
   if (curr_cont_node_ptr)
   {
      /** Save the current container being processed */
      apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_node_ptr = curr_cont_node_ptr;

      /** Save the container node return pointer */
      *container_node_pptr = (apm_container_t *)curr_cont_node_ptr->obj_ptr;

      return result;
   }

   /** Fall through if container list of current graph is empty.
    *  Check the next graph in the list */

   /** Get pointer to current graph node being processed */
   curr_graph_node_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_graph_ptr;

   /** Advance the graph node ptr */
   curr_graph_node_ptr = curr_graph_node_ptr->next_ptr;

   /** If the graph list is non-empty, get next graph */
   if (curr_graph_node_ptr)
   {
      /** Get the pointer to the first graph belonging to sub-graph
       *  ID under process for graph management command under
       *  context */
      apm_gm_cmd_get_cont_graph_node(apm_cmd_ctrl_ptr, &curr_graph_node_ptr);

      if (!curr_graph_node_ptr)
      {
         AR_MSG(DBG_MED_PRIO,
                "apm_gm_cmd_get_next_cont_in_sorted_list(): Reached end of list of disjoint container graphs, "
                "cmd_opcode[0x%08lx]",
                apm_cmd_ctrl_ptr->cmd_opcode);

         return result;
      }

      /** Save the current graph being processed */
      apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_graph_ptr = curr_graph_node_ptr;

      graph_node_ptr = (apm_cont_graph_t *)curr_graph_node_ptr->obj_ptr;

      /** Get the pointer to start of the list of containers within
       *  this next graph */
      curr_cont_node_ptr = graph_node_ptr->container_list_ptr;

      /** If the container list is non-empty, cache it */
      if (curr_cont_node_ptr)
      {
         /** Save the current container being processed */
         apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_node_ptr = curr_cont_node_ptr;

         /** Save the container node return pointer */
         *container_node_pptr = (apm_container_t *)curr_cont_node_ptr->obj_ptr;
      }

   } /** End of 'if' APM graph list is not empty */

   return result;
}
