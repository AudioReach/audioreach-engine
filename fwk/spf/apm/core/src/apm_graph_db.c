/**
 * \file apm_graph_db.c
 *
 * \brief
 *     This file contains APM Graph Data Base Mangement Utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**==============================================================================
   Global Defines
==============================================================================*/

#include "apm_graph_db.h"
#include "apm_ext_cmn.h"
#include "apm_i.h"

static const uint32_t apm_obj_hash_tbl_size[] =
{
   APM_MODULE_HASH_TBL_SIZE,
   /** Table size for APM_OBJ_TYPE_MODULE = 0 */

   APM_CONT_HASH_TBL_SIZE
   /** Table size for APM_OBJ_TYPE_CONTAINER = 1 */
};

ar_result_t apm_db_add_node_to_list(spf_list_node_t **list_head_pptr, void *list_node_ptr, uint32_t *node_cntr_ptr)
{
   ar_result_t result = AR_EOK;

   /** Add the node to the list */
   if (AR_EOK != (result = spf_list_insert_tail(list_head_pptr, list_node_ptr, APM_INTERNAL_STATIC_HEAP_ID, TRUE /* use_pool*/)))
   {
      AR_MSG(DBG_ERROR_PRIO, "FAILED to add node to the list, result: 0x%lx", result);

      return AR_EFAILED;
   }

   /** Increment the node counter */
   (*node_cntr_ptr)++;

   return result;
}

ar_result_t apm_db_search_and_add_node_to_list(spf_list_node_t **list_head_pptr,
                                               void *            list_node_ptr,
                                               uint32_t *        node_cntr_ptr)
{
   ar_result_t result        = AR_EOK;
   bool_t      is_node_added = FALSE;

   /** Search and add the node to the list */
   if (AR_EOK !=
       (result = spf_list_search_and_add_obj(list_head_pptr, list_node_ptr, &is_node_added, APM_INTERNAL_STATIC_HEAP_ID, TRUE)))
   {
      AR_MSG(DBG_ERROR_PRIO, "FAILED to add node to the list, result: 0x%lx", result);

      return result;
   }

   /** Increment the node counter, if the node is added to the
    *  list */
   if (is_node_added)
   {
      (*node_cntr_ptr)++;
   }

   return result;
}

ar_result_t apm_db_remove_node_from_list(spf_list_node_t **list_head_pptr, void *list_node_ptr, uint32_t *node_cntr_ptr)
{
   ar_result_t result     = AR_EOK;
   bool_t      node_found = FALSE;

   /** Remove the node from the list */
   if (FALSE == (node_found = spf_list_find_delete_node(list_head_pptr, list_node_ptr, TRUE /*pool_used*/)))
   {
      AR_MSG(DBG_MED_PRIO, "APM Remove Node: Target node not present in the list, result: 0x%lx", result);

      return AR_EOK;
   }

   /** Decrement the node counter */
   (*node_cntr_ptr)--;

   return result;
}

ar_result_t apm_db_add_obj_to_list(spf_list_node_t **   list_pptr,
                                   void *               obj_ptr,
                                   uint32_t             obj_id,
                                   apm_graph_obj_type_t obj_type,
                                   uint32_t *           obj_cntr_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    index;

   /** Validate the graph obj type */
   if (obj_type > APM_OBJ_TYPE_MAX)
   {
      AR_MSG(DBG_ERROR_PRIO, "obj_type[%lu] is out of bound, max supported[%lu]", obj_type, APM_OBJ_TYPE_MAX);

      return AR_EFAILED;
   }

   /** Get the list index as per obj ID  */
   index = APM_HASH_FUNC(obj_id, apm_obj_hash_tbl_size[obj_type]);

   /** Add object to the list */
   result = apm_db_add_node_to_list(&list_pptr[index], obj_ptr, obj_cntr_ptr);

   return result;
}

ar_result_t apm_db_remove_obj_from_list(spf_list_node_t **   list_pptr,
                                        void *               obj_ptr,
                                        uint32_t             obj_id,
                                        apm_graph_obj_type_t obj_type,
                                        uint32_t *           obj_cntr_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    index;

   /** Get the module list index */
   index = APM_HASH_FUNC(obj_id, apm_obj_hash_tbl_size[obj_type]);

   if (!list_pptr[index])
   {
      AR_MSG(DBG_ERROR_PRIO, "List ptr is NULL, obj_type[%lu], obj_id[%lu]", obj_type, obj_id);

      return AR_EFAILED;
   }

   /** Remove object from the list */
   result = apm_db_remove_node_from_list(&list_pptr[index], obj_ptr, obj_cntr_ptr);

   return result;
}

ar_result_t apm_db_get_sub_graph_node(apm_graph_info_t *graph_info_ptr,
                                      uint32_t          sub_graph_id,
                                      apm_sub_graph_t **sub_graph_pptr,
                                      apm_db_query_t    query_type)
{
   ar_result_t      result = AR_EOK;
   spf_list_node_t *curr_ptr;
   apm_sub_graph_t *sg_list_node_ptr;

   /** Validate input arguments */
   if (!graph_info_ptr || !sub_graph_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Graph info ptr [0x%lX] and/or sub graph pptr [0x%lX] is/are NULL",
             graph_info_ptr,
             sub_graph_pptr);

      return AR_EFAILED;
   }

   /** Set the pointer to sub-graph node to NULL */
   *sub_graph_pptr = NULL;

   curr_ptr = graph_info_ptr->sub_graph_list_ptr;

   /** Check if the Container instance already exists */
   while (NULL != curr_ptr)
   {
      sg_list_node_ptr = (apm_sub_graph_t *)curr_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == sg_list_node_ptr)
      {
         /** return NULL handle */
         *sub_graph_pptr = NULL;

         return AR_EFAILED;
      }

      /** Check if the SG ID matches for any of existing sub-graphs */
      if ((sg_list_node_ptr->sub_graph_id) == (sub_graph_id))
      {
         /** Container instance found */
         *sub_graph_pptr = sg_list_node_ptr;

         return result;
      }

      /** Else, keep traversing the list */
      curr_ptr = curr_ptr->next_ptr;
   }

   if (APM_DB_OBJ_QUERY == query_type)
   {
      /** The execution falls through here when the subgraph  is not
       *  found and if the query is to check the APM database if the
       *  sub graph ID is present or not in the database
       * it returns error if Sub graph handle is not found in the database*/

      AR_MSG(DBG_HIGH_PRIO, "GRAPH_MGMT: Sub-graph SG_ID: 0x%lX does not exist", sub_graph_id);

      result = AR_EFAILED;
   }

   return result;
}

ar_result_t apm_db_get_container_node(apm_graph_info_t *graph_info_ptr,
                                      uint32_t          container_id,
                                      apm_container_t **container_pptr,
                                      apm_db_query_t    query_type)
{
   ar_result_t      result = AR_EOK;
   spf_list_node_t *curr_ptr;
   apm_container_t *cont_list_node_ptr;
   uint32_t         index;
   apm_ext_utils_t *ext_utils_ptr;

   /** Validate input arguments */
   if (!graph_info_ptr || !container_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Graph info ptr [0x%lX] and/or container pptr [0x%lX] is/are NULL",
             graph_info_ptr,
             container_pptr);

      return AR_EFAILED;
   }

   /** Get the pointer to ext utils vtbl  */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   /** check if container is in satellite list, then need to get the parent container id
    *   In the master process domain context, satellite containers would be represented by
    *  the offload container in the APM. OLC container node would need to be returned when
    *  the container node is requested give the container ID. OLC will have the information
    *  of all the satellite containers associated with it.
    */
   if (graph_info_ptr->num_satellite_container > 0)
   {
      uint32_t parent_cont_id = 0;

      if (ext_utils_ptr->offload_vtbl_ptr &&
          ext_utils_ptr->offload_vtbl_ptr->apm_db_get_sat_contaniners_parent_cont_id_fptr)
      {
         if (ext_utils_ptr->offload_vtbl_ptr
                ->apm_db_get_sat_contaniners_parent_cont_id_fptr(graph_info_ptr->sat_container_list_ptr,
                                                                 container_id,
                                                                 &parent_cont_id))
         {
            container_id = parent_cont_id;
         }
      }
   }

   /** Set the pointer to sub-graph node to NULL */
   *container_pptr = NULL;

   index = APM_HASH_FUNC(container_id, APM_CONT_HASH_TBL_SIZE);

   curr_ptr = graph_info_ptr->container_list_ptr[index];

   /** Check if the Container instance already exists */
   while (NULL != curr_ptr)
   {
      cont_list_node_ptr = (apm_container_t *)curr_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == cont_list_node_ptr)
      {
         /** return NULL handle */
         *container_pptr = NULL;

         return AR_EFAILED;
      }

      /** Check if the SG ID matches for any of existing sub-graphs */
      if ((cont_list_node_ptr->container_id) == (container_id))
      {
         /** Container instance found */
         *container_pptr = cont_list_node_ptr;

         return result;
      }

      /** Else, keep traversing the list */
      curr_ptr = curr_ptr->next_ptr;
   }

   if (APM_DB_OBJ_QUERY == query_type)
   {
      /** The execution falls here when the container ID is not found
       *  and if the query is to check the APM database if the
       *  container ID is present or not in the database
       * it returns error if container ID is not found in database*/

      AR_MSG(DBG_HIGH_PRIO, "GRAPH_MGMT: Queried container id[0x%lX] does not exist", container_id);

      result = AR_EFAILED;
   }

   return result;
}

ar_result_t apm_db_get_module_node(apm_graph_info_t *graph_info_ptr,
                                   uint32_t          mod_instance_id,
                                   apm_module_t **   module_pptr,
                                   apm_db_query_t    query_type)
{
   ar_result_t      result = AR_EOK;
   spf_list_node_t *curr_ptr;
   apm_module_t *   module_list_node_ptr;
   uint32_t         index;

   /** Validate input arguments */
   if (!graph_info_ptr || !module_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Graph info ptr [0x%lX] and/or module pptr [0x%lX] is/are NULL",
             graph_info_ptr,
             module_pptr);

      return AR_EFAILED;
   }

   /** Set the pointer to sub-graph node to NULL */
   *module_pptr = NULL;

   /** Get the module list index */
   index = APM_HASH_FUNC(mod_instance_id, APM_MODULE_HASH_TBL_SIZE);

   /** Get the modue list pointer */
   curr_ptr = graph_info_ptr->module_list_ptr[index];

   /** Check if the Container instance already exists */
   while (NULL != curr_ptr)
   {
      module_list_node_ptr = (apm_module_t *)curr_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == module_list_node_ptr)
      {
         /** return NULL handle */
         *module_pptr = NULL;

         return AR_EFAILED;
      }

      /** Check if the module instance ID matches  for any of
       *  existing modules in the data base */
      if ((module_list_node_ptr->instance_id) == (mod_instance_id))
      {
         /** Container instance found */
         *module_pptr = module_list_node_ptr;

         return result;
      }

      /** Else, keep traversing the list */
      curr_ptr = curr_ptr->next_ptr;
   }

   if (APM_DB_OBJ_QUERY == query_type)
   {
      /** The execution falls here when the module ID is not found
       *  and if the query is to check the APM database if the module
       * ID is present or not in the database it returns error if
       * module ID handle is not found in the database */

      AR_MSG(DBG_HIGH_PRIO, "GRAPH_MGMT: Queried Module M_IID[0x%lX] does not exist", mod_instance_id);

      result = AR_EFAILED;
   }

   return result;
}

ar_result_t apm_db_get_cont_port_info_node(spf_list_node_t *              cont_port_info_list_ptr,
                                           uint32_t                       self_port_sg_id,
                                           uint32_t                       peer_port_sg_id,
                                           apm_cont_port_connect_info_t **port_connect_info_node_pptr)
{
   ar_result_t                   result = AR_EOK;
   spf_list_node_t *             curr_ptr;
   apm_cont_port_connect_info_t *cont_list_node_ptr;

   /** Set the pointer to container port connection info node to
    *  NULL */
   *port_connect_info_node_pptr = NULL;

   /** Return error if both the sub-graph ID's are NULL */
   if (!self_port_sg_id && !peer_port_sg_id)
   {
      return AR_EFAILED;
   }

   curr_ptr = cont_port_info_list_ptr;

   /** Check if the Container instance already exists */
   while (NULL != curr_ptr)
   {
      cont_list_node_ptr = (apm_cont_port_connect_info_t *)curr_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == cont_list_node_ptr)
      {
         /** return NULL handle */
         *port_connect_info_node_pptr = NULL;

         return AR_EOK;
      }

      /** If both the sub-graph ID's are valid */
      if (self_port_sg_id && peer_port_sg_id)
      {
         if ((cont_list_node_ptr->self_sg_obj_ptr) && (cont_list_node_ptr->peer_sg_obj_ptr))
         {
            /** Check if the SG ID matches */
            if ((cont_list_node_ptr->self_sg_obj_ptr->sub_graph_id) == (self_port_sg_id) &&
                (cont_list_node_ptr->peer_sg_obj_ptr->sub_graph_id) == (peer_port_sg_id))
            {
               /** Port connection instance found */
               *port_connect_info_node_pptr = cont_list_node_ptr;

               return AR_EOK;
            }
         }
      }
      else if ((self_port_sg_id) && (cont_list_node_ptr->self_sg_obj_ptr)) /** If only source sub-graph ID is valid */
      {
         /** Check if the SG ID matches */
         if (cont_list_node_ptr->self_sg_obj_ptr->sub_graph_id == self_port_sg_id)
         {
            /** Port connection instance found */
            *port_connect_info_node_pptr = cont_list_node_ptr;

            return AR_EOK;
         }
      }
      else if ((peer_port_sg_id) && (cont_list_node_ptr->peer_sg_obj_ptr)) /** If only peer sub-graph ID is valid */
      {
         /** Check if the SG ID matches */
         if (cont_list_node_ptr->peer_sg_obj_ptr->sub_graph_id == peer_port_sg_id)
         {
            /** Port connection instance found */
            *port_connect_info_node_pptr = cont_list_node_ptr;

            return AR_EOK;
         }
      }

      /** Else, keep traversing the list */
      curr_ptr = curr_ptr->next_ptr;
   }

   return result;
}

ar_result_t apm_db_get_module_list_node(spf_list_node_t *        module_list_ptr,
                                        uint32_t                 sub_graph_id,
                                        uint32_t                 container_id,
                                        apm_pspc_module_list_t **mod_list_node_pptr)
{
   ar_result_t             result = AR_EOK;
   spf_list_node_t *       curr_node_ptr;
   apm_pspc_module_list_t *mode_list_node_ptr;

   /** Validate input arguments */
   if (!mod_list_node_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Module list node pptr[0x%lX] is NULL", mod_list_node_pptr);

      return AR_EFAILED;
   }

   /** Set the pointer to module list to NULL */
   *mod_list_node_pptr = NULL;

   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = module_list_ptr;

   /** Check if the Container instance already exists */
   while (curr_node_ptr)
   {
      mode_list_node_ptr = (apm_pspc_module_list_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == mode_list_node_ptr)
      {
         /** return NULL handle */
         *mod_list_node_pptr = NULL;

         return AR_EFAILED;
      }

      /** Check if the SG ID matches */
      if ((mode_list_node_ptr->sub_graph_id == sub_graph_id) && (mode_list_node_ptr->container_id == container_id))
      {
         /** Module list node instance found */
         *mod_list_node_pptr = mode_list_node_ptr;

         return AR_EOK;
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return result;
}

ar_result_t apm_db_get_cont_port_conn(spf_list_node_t *        cont_port_info_list_ptr,
                                      uint32_t                 module_iid,
                                      uint32_t                 port_id,
                                      spf_module_port_conn_t **port_conn_info_pptr,
                                      apm_db_query_t           obj_query_type)
{
   ar_result_t                   result = AR_EOK;
   spf_list_node_t *             curr_conn_list_ptr;
   spf_list_node_t *             curr_conn_node_ptr;
   apm_cont_port_connect_info_t *port_conn_list_per_sg_ptr;
   spf_module_port_conn_t *      port_conn_list_ptr;

   /** Validate input arguments */
   if (!port_id || !module_iid || !cont_port_info_list_ptr || !port_conn_info_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Invalid i/p arg, miid[0x%lX], port_id[0x%lX], port_db_list[0x%lX], port_conn_node[0x%lX]",
             module_iid,
             port_id,
             cont_port_info_list_ptr,
             port_conn_info_pptr);

      return AR_EFAILED;
   }

   /** Clear the return pointer */
   *port_conn_info_pptr = NULL;

   /** Get the pointer to start of the container port connection
    *  list per sg ID pair */
   curr_conn_list_ptr = cont_port_info_list_ptr;

   /** Iterate over the container port connection list per
    *  sub-graph ID pair */
   while (NULL != curr_conn_list_ptr)
   {
      port_conn_list_per_sg_ptr = (apm_cont_port_connect_info_t *)curr_conn_list_ptr->obj_ptr;

      /** Get the pointer to the list of port handles for the
       *  current SG ID pair */
      curr_conn_node_ptr = port_conn_list_per_sg_ptr->port_conn_list_ptr;

      /** Iterate over the list of port handles for current SG ID
       *  pair */
      while (curr_conn_node_ptr)
      {
         /** Get the pointer to port connection object */
         port_conn_list_ptr = (spf_module_port_conn_t *)curr_conn_node_ptr->obj_ptr;

         /** Check if the container's self port ID matches with input
          *  port ID, if so return this handle */
         if ((port_conn_list_ptr->self_mod_port_hdl.module_port_id == port_id) &&
             (port_conn_list_ptr->self_mod_port_hdl.module_inst_id == module_iid))
         {
            /** Populate the return pointer */
            *port_conn_info_pptr = port_conn_list_ptr;

            /** Break the loop and return */
            return AR_EOK;
         }

         /** Keep traversting the list of port handles */
         curr_conn_node_ptr = curr_conn_node_ptr->next_ptr;
      }

      /** keep traversing the list port port handles per SG ID pair */
      curr_conn_list_ptr = curr_conn_list_ptr->next_ptr;
   }

   /** Check if the query mandates the queried object must
    *  exist, if not its failure */
   if (APM_DB_OBJ_QUERY == obj_query_type)
   {
      /** If the execution falls through, this implies matching
       *  port handle could not be found, return error */
      result = AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO,
          " ::WARNING:: apm_db_get_cont_port_hdl(): No matching port handle found for port_id[0x%lx], result: 0x%lX",
          port_id,
          result);

   return result;
}

uint32_t apm_db_get_num_instances(apm_graph_info_t *graph_info_ptr)
{
   if (NULL == graph_info_ptr)
   {
      return 0;
   }
   else
   {
      return graph_info_ptr->num_containers + graph_info_ptr->num_modules;
   }
}
