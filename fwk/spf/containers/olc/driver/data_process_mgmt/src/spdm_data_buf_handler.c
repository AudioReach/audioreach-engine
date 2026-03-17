/**
 * \file spdm_data_buf_handler.c
 * \brief
 * This file contains Satellite Graph Management functions for buffer handling
 * for data path
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spdm_i.h"
#include "sprm.h"

/* =======================================================================
   Static Function Definitions
   ========================================================================== */

/**
 * \brief Retrieves a buffer node based on a given token.
 * \param[in] data_buf_list_ptr Pointer to the list of buffer nodes.
 * \param[in] num_data_buf_list Number of buffer nodes in the list.
 * \param[out] data_buf_node_ptr Pointer to the buffer node.
 * \param[in] token Token to identify the buffer node.
 * \return TRUE if the buffer node is found, FALSE otherwise.
 * 
 * This function traverses the list of buffer nodes to find a node that matches
 * the given token. It starts from the beginning of the list and checks each
 * node's token against the provided token. If a matching node is found, the
 * function sets the data_buf_node_ptr to point to the found node and returns
 * TRUE. If no matching node is found, the function returns FALSE.
 */
static bool_t spdm_get_data_buf_node_given_token(spf_list_node_t *      data_buf_list_ptr,
                                                 uint32_t               num_data_buf_list,
                                                 data_buf_pool_node_t **data_buf_node_ptr,
                                                 uint32_t               token)
{

   spf_list_node_t *     curr_node_ptr;
   data_buf_pool_node_t *cmd_hndl_node_ptr;
   
   // Get the pointer to start of the list of module list nodes 
   curr_node_ptr = data_buf_list_ptr;

   // Check if the module instance  exists 
   while (curr_node_ptr)
   {
      cmd_hndl_node_ptr = (data_buf_pool_node_t *)curr_node_ptr->obj_ptr;

      // validate the instance pointer 
      if (NULL == cmd_hndl_node_ptr)
      {
         return FALSE;
      }
      if (token == cmd_hndl_node_ptr->token)
      {
         *data_buf_node_ptr = cmd_hndl_node_ptr;
         return TRUE;
      }

      // Else, keep traversing the list 
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return FALSE;
}

/**
 * \brief Retrieves a buffer node using the token and data pool pointer.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] data_pool_ptr Pointer to the shared memory data buffer pool.
 * \param[in] token Token to identify the buffer node.
 * \param[out] data_buf_node_ptr Pointer to the buffer node.
 * \return AR_EOK if successful, error code otherwise.
 * 
 * This function uses the token to find a buffer node within the specified
 * data pool. It first validates the data pool pointer to ensure it is not NULL.
 * Then, it calls the spdm_get_data_buf_node_given_token function to perform the
 * search. If the buffer node is found, it is returned via the data_buf_node_ptr
 * parameter. If the buffer node is not found, the function returns AR_EUNEXPECTED.
 */
ar_result_t spdm_get_data_buf_node(spgm_info_t *          spgm_ptr,
                                   shmem_data_buf_pool_t *data_pool_ptr,
                                   uint32_t               token,
                                   data_buf_pool_node_t **data_buf_node_ptr)
{
   ar_result_t result     = AR_EOK;
   bool_t      found_node = FALSE;

   if (NULL == data_pool_ptr)
   {
      return AR_EBADPARAM;
   }

   found_node = spdm_get_data_buf_node_given_token(data_pool_ptr->port_db_list_ptr,
                                                   data_pool_ptr->num_data_buf_in_list,
                                                   data_buf_node_ptr,
                                                   token);

   if (FALSE == found_node)
   {
      return AR_EUNEXPECTED;
   }

   return result;
}

/**
 * \brief Retrieves the data pool pointer based on the port index and data type.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] port_index Index of the port.
 * \param[in] data_type Type of data (read/write).
 * \param[out] data_pool_ptr Pointer to the shared memory data buffer pool.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function determines the appropriate data pool pointer based on the
 * port index and data type (read or write). It sets the data pool pointer
 * accordingly. If the data type is IPC_WRITE_DATA, the function retrieves the
 * write data port object and sets the data pool pointer to its buffer pool.
 * If the data type is IPC_READ_DATA, the function retrieves the read data port
 * object and sets the data pool pointer to its buffer pool.
 */
ar_result_t spdm_get_data_pool_ptr(spgm_info_t *           spgm_ptr,
                                   uint32_t                port_index,
                                   uint32_t                data_type,
                                   shmem_data_buf_pool_t **data_pool_ptr)
{
   ar_result_t            result = AR_EOK;
   write_data_port_obj_t *wr_ptr = NULL;
   read_data_port_obj_t * rd_ptr = NULL;

   if (IPC_WRITE_DATA == data_type)
   {
      wr_ptr         = spgm_ptr->process_info.wdp_obj_ptr[port_index];
      *data_pool_ptr = &wr_ptr->db_obj.buf_pool;
   }
   else if (IPC_READ_DATA == data_type)
   {
      rd_ptr         = spgm_ptr->process_info.rdp_obj_ptr[port_index];
      *data_pool_ptr = &rd_ptr->db_obj.buf_pool;
   }

   return result;
}

/**
 * \brief Finds an available buffer node that is not in use.
 * \param[in] data_buf_list_ptr Pointer to the list of buffer nodes.
 * \param[in] num_data_buf_list Number of buffer nodes in the list.
 * \param[out] data_buf_node_ptr Pointer to the buffer node.
 * \return TRUE if an available buffer node is found, FALSE otherwise.
 *
 * This function traverses the list of buffer nodes to find a node that is not
 * currently in use. It starts from the beginning of the list and checks each
 * node's usage status. If an available node is found, the function sets the
 * data_buf_node_ptr to point to the found node and returns TRUE. If no available
 * node is found, the function returns FALSE.
 */
bool_t spdm_get_available_data_buf_node(spf_list_node_t *      data_buf_list_ptr,
                                        uint32_t               num_data_buf_list,
                                        data_buf_pool_node_t **data_buf_node_ptr)
{
   spf_list_node_t *     curr_node_ptr     = NULL;
   data_buf_pool_node_t *cmd_hndl_node_ptr = NULL;

   // Get the pointer to start of the list of module list nodes
   curr_node_ptr = data_buf_list_ptr;

   // Check if the module instance exists
   while (curr_node_ptr)
   {
      cmd_hndl_node_ptr = (data_buf_pool_node_t *)curr_node_ptr->obj_ptr;

      // Validate the instance pointer
      if (NULL == cmd_hndl_node_ptr)
      {
         return FALSE;
      }
      if ((FALSE == cmd_hndl_node_ptr->buf_in_use))
      {
         *data_buf_node_ptr = cmd_hndl_node_ptr;
         return TRUE;
      }
      // Else, keep traversing the list
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return FALSE;
}

/**
 * \brief Retrieves a buffer node from the list.
 * \param[in] data_buf_list_ptr Pointer to the list of buffer nodes.
 * \param[in] num_data_buf_list Number of buffer nodes in the list.
 * \param[out] data_buf_node_ptr Pointer to the buffer node.
 * \return TRUE if the buffer node is found, FALSE otherwise.
 *
 * This function traverses the list of buffer nodes to find any node. It starts
 * from the beginning of the list and checks each node's instance pointer. If a
 * valid instance pointer is found, the function sets the data_buf_node_ptr to
 * point to the found node and returns TRUE. If no valid instance pointer is
 * found, the function returns FALSE.
 *
 * Steps performed by the function:
 * 1. Initialize the current node pointer to the start of the list.
 * 2. Traverse the list of buffer nodes.
 * 3. Validate the instance pointer of each node.
 * 4. If a valid instance pointer is found, set the data_buf_node_ptr to point
 *    to the found node and return TRUE.
 * 5. If no valid instance pointer is found, return FALSE.
 */
bool_t spdm_get_data_buf_node_from_list(spf_list_node_t *      data_buf_list_ptr,
                                        uint32_t               num_data_buf_list,
                                        data_buf_pool_node_t **data_buf_node_ptr)
{
   spf_list_node_t *     curr_node_ptr     = NULL;
   data_buf_pool_node_t *cmd_hndl_node_ptr = NULL;

   // Get the pointer to start of the list of module list nodes
   curr_node_ptr = data_buf_list_ptr;

   // Check if the module instance exists
   while (curr_node_ptr)
   {
      cmd_hndl_node_ptr = (data_buf_pool_node_t *)curr_node_ptr->obj_ptr;

      // Validate the instance pointer
      if (NULL == cmd_hndl_node_ptr)
      {
         return FALSE;
      }
      *data_buf_node_ptr = cmd_hndl_node_ptr;
      return TRUE;
      // Else, keep traversing the list
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return FALSE;
}

/**
 * \brief Retrieves an empty buffer node from the list.
 * \param[in] data_buf_list_ptr Pointer to the list of buffer nodes.
 * \param[in] num_data_buf_list Number of buffer nodes in the list.
 * \param[out] data_buf_node_ptr Pointer to the buffer node.
 * \return TRUE if an empty buffer node is found, FALSE otherwise.
 *
 * This function traverses the list of buffer nodes to find a node that is marked
 * as pending allocation. It starts from the beginning of the list and checks each
 * node's pending allocation status. If an empty node is found, the function sets
 * the data_buf_node_ptr to point to the found node and returns TRUE. If no empty
 * node is found, the function returns FALSE.
 *
 * Steps performed by the function:
 * 1. Initialize the current node pointer to the start of the list.
 * 2. Traverse the list of buffer nodes.
 * 3. Validate the instance pointer of each node.
 * 4. Check the pending allocation status of each node.
 * 5. If an empty node is found, set the data_buf_node_ptr to point to the found
 *    node and return TRUE.
 * 6. If no empty node is found, return FALSE.
 */
static bool_t spdm_get_empty_data_buf_node(spf_list_node_t *      data_buf_list_ptr,
                                           uint32_t               num_data_buf_list,
                                           data_buf_pool_node_t **data_buf_node_ptr)
{
   spf_list_node_t *     curr_node_ptr;
   data_buf_pool_node_t *cmd_hndl_node_ptr;

   // Get the pointer to start of the list of module list nodes
   curr_node_ptr = data_buf_list_ptr;

   // Check if the module instance exists
   while (curr_node_ptr)
   {
      cmd_hndl_node_ptr = (data_buf_pool_node_t *)curr_node_ptr->obj_ptr;

      // Validate the instance pointer
      if (NULL == cmd_hndl_node_ptr)
      {
         return FALSE;
      }
      if (TRUE == cmd_hndl_node_ptr->pending_alloc)
      {
         *data_buf_node_ptr = cmd_hndl_node_ptr;
         return TRUE;
      }
      // Else, keep traversing the list
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return FALSE;
}

/**
 * \brief Allocates shared memory for buffer nodes.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] data_pool_ptr Pointer to the shared memory data buffer pool.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function allocates shared memory for buffer nodes that are marked for
 * allocation. It traverses the list of buffer nodes and allocates memory for
 * each node that requires it. The function ensures that the shared memory
 * allocation is successful and updates the buffer node accordingly.
 *
 * Steps performed by the function:
 * 1. Traverse the list of buffer nodes to find nodes marked for allocation.
 * 2. For each node requiring allocation, allocate shared memory.
 * 3. Update the buffer node with the allocated memory details.
 * 4. Return the result of the allocation operation.
 */
static ar_result_t spdm_alloc_data_pool_shm(spgm_info_t *spgm_ptr, shmem_data_buf_pool_t *data_pool_ptr)
{
   ar_result_t           result            = AR_EOK;
   bool_t                found_node        = FALSE;
   data_buf_pool_node_t *data_buf_node_ptr = NULL;

   do
   {
      found_node = spdm_get_empty_data_buf_node(data_pool_ptr->port_db_list_ptr,
                                                data_pool_ptr->num_data_buf_in_list,
                                                &data_buf_node_ptr);
      if (TRUE == found_node)
      {
         if (data_buf_node_ptr->ipc_data_buf.shm_mem_ptr == NULL)
         {
            if (AR_EOK != (result = sgm_shmem_alloc(data_pool_ptr->buf_size + 2 * GAURD_PROTECTION_BYTES,
                                                    spgm_ptr->sgm_id.sat_pd,
                                                    &data_buf_node_ptr->ipc_data_buf)))
            {
               return result;
            }
            data_buf_node_ptr->data_buf_size = data_pool_ptr->buf_size;
            data_buf_node_ptr->pending_alloc = FALSE;
         }
         else
         {
            data_buf_node_ptr->pending_alloc = TRUE; // This case should not happen
         }
      }
   } while (found_node);

   return result;
}

/**
 * \brief Adds buffer nodes to the data pool.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] data_pool_ptr Pointer to the shared memory data buffer pool.
 * \param[in] num_buf_nodes_to_add Number of buffer nodes to add.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function adds a specified number of buffer nodes to the shared memory
 * data buffer pool. It allocates memory for each buffer node, initializes the
 * node, assigns a unique token, and adds the node to the list of buffer nodes
 * in the data pool.
 *
 * Steps performed by the function:
 * 1. Validate the input parameters to ensure they are not NULL.
 * 2. Iterate through the specified number of buffer nodes to add.
 * 3. Allocate memory for each buffer node using the POSAL memory allocation function.
 * 4. Initialize the buffer node by setting its fields to default values.
 * 5. Assign a unique token to the buffer node using the sgm_get_unique_token function.
 * 6. Add the buffer node to the list of buffer nodes in the data pool using the
 *    sgm_util_add_node_to_list function.
 * 7. Return the result of the operation, indicating success or failure.
 */
static ar_result_t spdm_add_node_to_data_pool(spgm_info_t *          spgm_ptr,
                                              shmem_data_buf_pool_t *data_pool_ptr,
                                              uint32_t               num_buf_nodes_to_add)
{
   ar_result_t           result        = AR_EOK;
   data_buf_pool_node_t *cur_node_ptr  = NULL;
   uint32_t              num_nodes_add = 0;
   uint32_t              token         = 0;

   // Validate input parameters
   if (NULL == spgm_ptr || NULL == data_pool_ptr)
   {
      return AR_EBADPARAM;
   }

   uint32_t              port_index    = data_pool_ptr->port_index;

   while (num_nodes_add < num_buf_nodes_to_add)
   {
      cur_node_ptr = (data_buf_pool_node_t *)posal_memory_malloc((sizeof(data_buf_pool_node_t)),
                                                                 (POSAL_HEAP_ID)spgm_ptr->cu_ptr->heap_id);
      if (NULL == cur_node_ptr)
      {
         OLC_SDM_MSG(OLC_SDM_ID,
                     DBG_ERROR_PRIO,
                     "create data node failed to allocate memory"
                     "pool data type (wr:0/rd:1) %lu:",
                     data_pool_ptr->data_type);
         return AR_ENOMEMORY;
      }
      memset(cur_node_ptr, 0, sizeof(data_buf_pool_node_t));
      cur_node_ptr->pending_alloc = TRUE;
      sgm_get_unique_token(spgm_ptr, &token);
      cur_node_ptr->token = token;
      if (AR_EOK != (result = sgm_util_add_node_to_list(spgm_ptr,
                                                        &data_pool_ptr->port_db_list_ptr,
                                                        cur_node_ptr,
                                                        &data_pool_ptr->num_data_buf_in_list)))
      {
         return result;
      }
      num_nodes_add++;
   }
   return result;
}

/**
 * \brief Destroys a buffer node, freeing its associated shared memory.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] data_pool_ptr Pointer to the shared memory data buffer pool.
 * \param[in] data_buf_node_ptr Pointer to the buffer node to be destroyed.
 * \param[in] spm_id_ptr Pointer to the SPGM ID info structure.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function destroys a buffer node by freeing its associated shared memory
 * and removing the node from the list of buffer nodes in the data pool.
 *
 * Steps performed by the function:
 * 1. Free the shared memory associated with the buffer node using the sgm_shmem_free function.
 * 2. Remove the buffer node from the list of buffer nodes in the data pool using the
 *    sgm_util_remove_node_from_list function.
 * 3. Return the result of the operation, indicating success or failure.
 */
static ar_result_t spdm_destroy_buffer_node(spgm_info_t *          spgm_ptr,
                                            shmem_data_buf_pool_t *data_pool_ptr,
                                            data_buf_pool_node_t * data_buf_node_ptr,
                                            spgm_id_info_t *       spm_id_ptr)
{
   ar_result_t result = AR_EOK;

   if (AR_EOK != (result = sgm_shmem_free(&data_buf_node_ptr->ipc_data_buf)))
   {
      OLC_SPGM_MSG(spm_id_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "DATA_MGMT: CONT_ID[0x%lX] sat pd [0x%lX] failed to free the shared memory for the data buffer "
                   "data types (wr:0/rd:1) %lu: port index 0x%x",
                   spm_id_ptr->cont_id,
                   spm_id_ptr->sat_pd,
                   data_pool_ptr->data_type,
                   data_pool_ptr->port_index);
      return result;
   }

   if (AR_EOK != (result = sgm_util_remove_node_from_list(spgm_ptr,
                                                          &data_pool_ptr->port_db_list_ptr,
                                                          data_buf_node_ptr,
                                                          &data_pool_ptr->num_data_buf_in_list)))
   {
      OLC_SPGM_MSG(spm_id_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "DATA_MGMT: CONT_ID[0x%lX] sat pd [0x%lX] failed to remove the node from the buf pool list "
                   "data types (wr:0/rd:1) %lu: port index 0x%x",
                   spm_id_ptr->cont_id,
                   spm_id_ptr->sat_pd,
                   data_pool_ptr->data_type,
                   data_pool_ptr->port_index);
      return result;
   }

   return result;
}

/**
 * \brief Removes deprecated buffer nodes from the data pool.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] data_pool_ptr Pointer to the shared memory data buffer pool.
 * \param[in] data_buf_node_ptr Pointer to the buffer node to be removed.
 * \param[in] spm_id_ptr Pointer to the SPGM ID info structure.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function removes deprecated buffer nodes from the data pool by destroying
 * the buffer node and freeing its associated shared memory.
 *
 * Steps performed by the function:
 * 1. Destroy the buffer node using the spdm_destroy_buffer_node function.
 * 2. Return the result of the operation, indicating success or failure.
 */
ar_result_t spdm_remove_deprecated_node_from_data_pool(spgm_info_t *          spgm_ptr,
                                                       shmem_data_buf_pool_t *data_pool_ptr,
                                                       data_buf_pool_node_t * data_buf_node_ptr,
                                                       spgm_id_info_t *       spm_id_ptr)
{
   ar_result_t result = AR_EOK;

   if (AR_EOK != (result = spdm_destroy_buffer_node(spgm_ptr, data_pool_ptr, data_buf_node_ptr, spm_id_ptr)))
   {
      return result;
   }

   return result;
}
/**
 * \brief Removes a specified number of buffer nodes from the data pool.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] data_pool_ptr Pointer to the shared memory data buffer pool.
 * \param[in] num_buf_nodes_to_remove Number of buffer nodes to remove.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function removes a specified number of buffer nodes from the data pool.
 * It traverses the list of buffer nodes to find nodes that are not in use and
 * removes them by destroying the buffer node and freeing its associated shared
 * memory.
 *
 * Steps performed by the function:
 * 1. Validate the input parameters to ensure they are not NULL.
 * 2. Check if the number of buffer nodes in the list is less than the required
 *    number of buffer nodes.
 * 3. Traverse the list of buffer nodes to find nodes that are not in use.
 * 4. Destroy the buffer node using the spdm_destroy_buffer_node function.
 * 5. Free the memory associated with the buffer node.
 * 6. Return the result of the operation, indicating success or failure.
 */
ar_result_t spdm_remove_node_from_data_pool(spgm_info_t *          spgm_ptr,
                                            shmem_data_buf_pool_t *data_pool_ptr,
                                            uint32_t               num_buf_nodes_to_remove)
{
   ar_result_t           result             = AR_EOK;
   bool_t                found_node         = FALSE;
   uint32_t              num_bufs_to_remove = 0;
   data_buf_pool_node_t *data_buf_node_ptr  = NULL;

   if (data_pool_ptr->num_data_buf_in_list < data_pool_ptr->req_num_data_buf)
   {
      return AR_EUNEXPECTED;
   }

   num_bufs_to_remove = num_buf_nodes_to_remove;

   while (num_bufs_to_remove)
   {
      found_node = spdm_get_available_data_buf_node(data_pool_ptr->port_db_list_ptr,
                                                    data_pool_ptr->num_data_buf_in_list,
                                                    &data_buf_node_ptr);
      if (TRUE == found_node)
      {
         // Indicates that the buffer is with OLC
         if (FALSE == data_buf_node_ptr->buf_in_use)
         {
            if (AR_EOK !=
                (result = spdm_destroy_buffer_node(spgm_ptr, data_pool_ptr, data_buf_node_ptr, &spgm_ptr->sgm_id)))
            {
               return result;
            }
            // Indicate the buffer is not with OLC.
            // We will mark for deprecation and delete the node once the
            // buffer is available with OLC
            posal_memory_free(data_buf_node_ptr);
            data_buf_node_ptr = NULL;
            found_node        = FALSE;
            num_bufs_to_remove--;
         }
      }
      else
      {
         result = AR_EUNEXPECTED;
         break;
      }
   }

   return result;
}

/**
 * \brief Removes all buffer nodes from the data pool.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] data_pool_ptr Pointer to the shared memory data buffer pool.
 * \param[in] num_buf_nodes_to_remove Number of buffer nodes to remove.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function removes all buffer nodes from the data pool. It traverses the
 * list of buffer nodes to find nodes and removes them by destroying the buffer
 * node and freeing its associated shared memory.
 *
 * Steps performed by the function:
 * 1. Validate the input parameters to ensure they are not NULL.
 * 2. Check if the number of buffer nodes in the list is less than the required
 *    number of buffer nodes.
 * 3. Traverse the list of buffer nodes to find nodes.
 * 4. Destroy the buffer node using the spdm_destroy_buffer_node function.
 * 5. Free the memory associated with the buffer node.
 * 6. Return the result of the operation, indicating success or failure.
 */
ar_result_t spdm_remove_all_node_from_data_pool(spgm_info_t *          spgm_ptr,
                                                shmem_data_buf_pool_t *data_pool_ptr,
                                                uint32_t               num_buf_nodes_to_remove)
{
   ar_result_t           result             = AR_EOK;
   bool_t                found_node         = FALSE;
   uint32_t              num_bufs_to_remove = 0;
   data_buf_pool_node_t *data_buf_node_ptr  = NULL;

   if (data_pool_ptr->num_data_buf_in_list < data_pool_ptr->req_num_data_buf)
   {
      return AR_EUNEXPECTED;
   }

   num_bufs_to_remove = num_buf_nodes_to_remove;

   while (num_bufs_to_remove)
   {
      found_node = spdm_get_data_buf_node_from_list(data_pool_ptr->port_db_list_ptr,
                                                    data_pool_ptr->num_data_buf_in_list,
                                                    &data_buf_node_ptr);
      if (TRUE == found_node)
      {
         // Indicates that the buffer is with OLC
         // if (FALSE == data_buf_node_ptr->buf_in_use)
         {
            if (AR_EOK !=
                (result = spdm_destroy_buffer_node(spgm_ptr, data_pool_ptr, data_buf_node_ptr, &spgm_ptr->sgm_id)))
            {
               return result;
            }
            // Indicate the buffer is not with OLC.
            // We will mark for deprecation and delete the node once the
            // buffer is available with OLC
            posal_memory_free(data_buf_node_ptr);
            data_buf_node_ptr = NULL;
            found_node        = FALSE;
            num_bufs_to_remove--;
         }
      }
      else
      {
         result = AR_EUNEXPECTED;
         break;
      }
   }

   return result;
}

/**
 * \brief Updates the buffer pool based on changes in buffer size or number of buffers.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] data_pool_ptr Pointer to the shared memory data buffer pool.
 * \param[in] buf_size_changed Flag indicating if the buffer size has changed.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function updates the buffer pool based on changes in buffer size or number
 * of buffers. It adds or removes buffer nodes as needed and allocates shared memory
 * for the updated buffers.
 *
 * Steps performed by the function:
 * 1. Determine if additional buffer nodes need to be added or removed.
 * 2. Add the required buffer nodes using the spdm_add_node_to_data_pool function.
 * 3. Remove the unnecessary buffer nodes using the spdm_remove_node_from_data_pool function.
 * 4. Allocate shared memory for the updated buffers using the spdm_alloc_data_pool_shm function.
 * 5. Return the result of the operation, indicating success or failure.
 */
static ar_result_t spdm_update_data_buf_list(spgm_info_t *          spgm_ptr,
                                             shmem_data_buf_pool_t *data_pool_ptr,
                                             bool_t                 buf_size_changed)
{
   ar_result_t result           = AR_EOK;
   bool_t      add_buf_nodes    = FALSE;
   bool_t      remove_buf_nodes = FALSE;
   uint32_t    num_buf_nodes    = 0;

   add_buf_nodes    = (data_pool_ptr->num_data_buf_in_list < data_pool_ptr->req_num_data_buf);
   remove_buf_nodes = (data_pool_ptr->num_data_buf_in_list > data_pool_ptr->req_num_data_buf);

   if (TRUE == add_buf_nodes)
   {
      // Add the additional buffers required
      num_buf_nodes = data_pool_ptr->req_num_data_buf - data_pool_ptr->num_data_buf_in_list;
      if (AR_EOK != (result = spdm_add_node_to_data_pool(spgm_ptr, data_pool_ptr, num_buf_nodes)))
      {
         return result;
      }
   }
   else if (TRUE == remove_buf_nodes)
   {
      // Remove the buffers which are not further required
      num_buf_nodes = data_pool_ptr->num_data_buf_in_list - data_pool_ptr->req_num_data_buf;
      if (AR_EOK != (result = spdm_remove_node_from_data_pool(spgm_ptr, data_pool_ptr, num_buf_nodes)))
      {
         return result;
      }
   }

   if (add_buf_nodes)
   {
      // Allocate shared memory for the updated buffers
      if (AR_EOK != (result = spdm_alloc_data_pool_shm(spgm_ptr, data_pool_ptr)))
      {
         return result;
      }
   }

   return result;
}

/**
 * \brief Updates the buffer size in the data pool.
 * \param[in] data_buf_pool_ptr Pointer to the shared memory data buffer pool.
 * \param[in] new_buf_size New buffer size to be set.
 * \param[in] data_type Type of data (read/write).
 * \param[in] port_index Index of the port.
 * \param[out] is_buf_size_changed Pointer to a flag indicating if the buffer size has changed.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function updates the buffer size in the data pool. It calculates the new
 * buffer size, aligns it to 128 bytes, and updates the buffer pool. The function
 * also sets a flag indicating if the buffer size has changed.
 *
 * Steps performed by the function:
 * 1. Calculate the new buffer size and align it to 128 bytes.
 * 2. Update the buffer pool with the new buffer size.
 * 3. Set the flag indicating if the buffer size has changed.
 * 4. Return the result of the operation, indicating success or failure.
 */
static ar_result_t spdm_update_buffer_size(shmem_data_buf_pool_t *data_buf_pool_ptr,
                                           uint32_t               new_buf_size,
                                           sdm_ipc_data_type_t    data_type,
                                           uint32_t               port_index,
                                           uint32_t *             is_buf_size_changed)
{
   ar_result_t result        = AR_EOK;
   uint32_t    prev_buf_size = data_buf_pool_ptr->buf_size;

   // Align the new buffer size to 128 bytes
   data_buf_pool_ptr->buf_size = ALIGN_128_BYTES(new_buf_size);

   // Set the flag indicating if the buffer size has changed
   *is_buf_size_changed = (prev_buf_size != data_buf_pool_ptr->buf_size) ? 1 : 0;

   return result;
}

/**
 * \brief Updates the number of IPC buffers required in the data pool.
 * \param[in] data_pool_ptr Pointer to the shared memory data buffer pool.
 * \param[in] data_type Type of data (read/write).
 * \param[in] port_index Index of the port.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function updates the number of IPC buffers required in the data pool.
 * It calculates the new number of buffers based on the data type and updates
 * the buffer pool.
 *
 * Steps performed by the function:
 * 1. Calculate the new number of buffers based on the data type.
 * 2. Update the buffer pool with the new number of buffers.
 * 3. Return the result of the operation, indicating success or failure.
 */
static ar_result_t spdm_update_num_ipc_buffers(shmem_data_buf_pool_t *data_pool_ptr,
                                               sdm_ipc_data_type_t    data_type,
                                               uint32_t               port_index)
{
   ar_result_t result       = AR_EOK;
   uint32_t    new_num_bufs = 0;

   // Calculate the new number of buffers based on the data type
   if (IPC_WRITE_DATA == data_type)
   {
      new_num_bufs = 1;
   }
   else
   {
      new_num_bufs = 1;
   }

   // Update the buffer pool with the new number of buffers
   data_pool_ptr->req_num_data_buf = new_num_bufs;

   return result;
}

/**
 * \brief Validates the arguments for allocating IPC data buffers.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] port_index Index of the port.
 * \param[in] data_type Type of data (read/write).
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function validates the arguments for allocating IPC data buffers. It
 * checks if the SPGM pointer is NULL, if the port index is valid, and if the
 * data type is valid.
 *
 * Steps performed by the function:
 * 1. Check if the SPGM pointer is NULL.
 * 2. Check if the port index is valid.
 * 3. Check if the data type is valid.
 * 4. Return the result of the validation, indicating success or failure.
 */
static ar_result_t spdm_alloc_ipc_data_buffers_validate_arg(spgm_info_t *       spgm_ptr,
                                                            uint32_t            port_index,
                                                            sdm_ipc_data_type_t data_type)
{
   ar_result_t result = AR_EOK;

   // Check if the SPGM pointer is NULL
   if (NULL == spgm_ptr)
   {
      return AR_EUNEXPECTED;
   }

   // Check if the port index is valid
   if (port_index > SPDM_MAX_IO_PORTS)
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "invalid port index, allocate ipc data buffer failed for (wr:0/rd:1) %lu:",
                  data_type);
      return AR_EFAILED;
   }

   // Check if the data type is valid
   if (!((IPC_WRITE_DATA == data_type) || (IPC_READ_DATA == data_type)))
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "invalid data types (wr:0/rd:1), allocate ipc data buffer failed",
                  data_type);
      return AR_EFAILED;
   }

   return result;
}

/**
 * \brief Allocates or reallocates shared memory data buffer pools for data processing.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] new_buf_size New buffer size to be set.
 * \param[in] port_index Index of the port.
 * \param[in] data_type Type of data (read/write).
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function allocates or reallocates shared memory data buffer pools for data
 * processing. It validates the arguments, retrieves the data pool pointer, updates
 * the number of buffers and buffer size, and updates the buffer pool accordingly.
 *
 * Steps performed by the function:
 * 1. Validate the arguments using the spdm_alloc_ipc_data_buffers_validate_arg function.
 * 2. Retrieve the data pool pointer using the spdm_get_data_pool_ptr function.
 * 3. Update the number of buffers using the spdm_update_num_ipc_buffers function.
 * 4. Update the buffer size using the spdm_update_buffer_size function.
 * 5. Update the buffer pool using the spdm_update_data_buf_list function.
 * 6. Return the result of the operation, indicating success or failure.
 */
ar_result_t spdm_alloc_ipc_data_buffers(spgm_info_t *       spgm_ptr,
                                        uint32_t            new_buf_size,
                                        uint32_t            port_index,
                                        sdm_ipc_data_type_t data_type)
{
   ar_result_t            result              = AR_EOK;
   shmem_data_buf_pool_t *data_pool_ptr       = NULL;
   uint32_t               is_buf_size_changed = 0;

   if (port_index >= SPDM_MAX_IO_PORTS)
   {
      return AR_EUNEXPECTED;
   }

   if (AR_EOK != (result = spdm_alloc_ipc_data_buffers_validate_arg(spgm_ptr, port_index, data_type)))
   {
      return result;
   }

   // Retrieve the data pool pointer
   if (AR_EOK != (result = spdm_get_data_pool_ptr(spgm_ptr, port_index, data_type, &data_pool_ptr)))
   {
      return result;
   }

   // Update the number of buffers
   if (AR_EOK != (result = spdm_update_num_ipc_buffers(data_pool_ptr, (sdm_ipc_data_type_t)data_type, port_index)))
   {
      return result;
   }

   // Update the buffer size. Media format update can happen
   if (AR_EOK != (result = spdm_update_buffer_size(data_pool_ptr,
                                                   new_buf_size,
                                                   (sdm_ipc_data_type_t)data_type,
                                                   port_index,
                                                   &is_buf_size_changed)))
   {
      return result;
   }

   // If the buffer size or number of buffers changed, we need to update the buffer pool
   // for buffer size change, we need to deprecate the old buffers and add new buffers with new buffer size
   // Shared memory is released for deprecated buffers and allocated for the new allocated buffers
   // For change in the number of buffers, we either increase or decrease the numbers of buffers in the pool
   // as per the updated requirement
   if (AR_EOK != (result = spdm_update_data_buf_list(spgm_ptr, data_pool_ptr, is_buf_size_changed)))
   {
      return result;
   }

   return result;
}

/**
 * \brief Recreates output buffers for the specified port.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] new_buf_size New buffer size to be set.
 * \param[in] port_index Index of the port.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function recreates output buffers for the specified port. It allocates
 * IPC data buffers for read data processing.
 *
 * Steps performed by the function:
 * 1. Allocate IPC data buffers for read data processing using the spdm_alloc_ipc_data_buffers function.
 * 2. Return the result of the operation, indicating success or failure.
 */
ar_result_t sgm_recreate_output_buffers(spgm_info_t *spgm_ptr, uint32_t new_buf_size, uint32_t port_index)
{
   ar_result_t result = AR_EOK;

   result = spdm_alloc_ipc_data_buffers(spgm_ptr, new_buf_size, port_index, IPC_READ_DATA);

   return result;
}

/**
 * \brief Sends all read buffers for the specified port.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] port_index Index of the port.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function sends all read buffers for the specified port. It retrieves
 * data buffers from the read data pool and sends them for read data processing.
 *
 * Steps performed by the function:
 * 1. Retrieve data buffers from the read data pool using the spdm_get_databuf_from_rd_datapool function.
 * 2. Send the read data buffer using the spdm_send_read_data_buffer function.
 * 3. Mark the buffer as not in use and reset its offset.
 * 4. Return the result of the operation, indicating success or failure.
 */
ar_result_t sgm_send_all_read_buffers(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t           result                 = AR_EOK;
   ar_result_t           temp_result            = AR_EOK;
   data_buf_pool_node_t *read_data_buf_node_ptr = NULL;
   read_data_port_obj_t *rd_data_ptr            = NULL;

   rd_data_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];

   while (TRUE)
   {
      temp_result = spdm_get_databuf_from_rd_datapool(spgm_ptr, rd_data_ptr);
      if (AR_EOK != temp_result)
      {
         break;
      }

      read_data_buf_node_ptr             = rd_data_ptr->db_obj.active_buf_node_ptr;
      read_data_buf_node_ptr->buf_in_use = FALSE;
      read_data_buf_node_ptr->offset     = 0;

      result = spdm_send_read_data_buffer(spgm_ptr, read_data_buf_node_ptr, rd_data_ptr);
      rd_data_ptr->db_obj.active_buf_node_ptr = NULL;
   }

   return result;
}

/**
 * \brief Sends a specified number of read buffers for the specified port.
 * \param[in] spgm_ptr Pointer to the SPGM info structure.
 * \param[in] port_index Index of the port.
 * \param[in] num_buf_to_send Number of buffers to send.
 * \return AR_EOK if successful, error code otherwise.
 *
 * This function sends a specified number of read buffers for the specified port.
 * It retrieves data buffers from the read data pool and sends them for read data
 * processing.
 *
 * Steps performed by the function:
 * 1. Iterate through the specified number of buffers to send.
 * 2. Retrieve data buffers from the read data pool using the spdm_get_databuf_from_rd_datapool function.
 * 3. Send the read data buffer using the spdm_send_read_data_buffer function.
 * 4. Mark the buffer as not in use and reset its offset.
 * 5. Return the result of the operation, indicating success or failure.
 */
ar_result_t sgm_send_n_read_buffers(spgm_info_t *spgm_ptr, uint32_t port_index, uint32_t num_buf_to_send)
{
   ar_result_t           result                 = AR_EOK;
   data_buf_pool_node_t *read_data_buf_node_ptr = NULL;
   read_data_port_obj_t *rd_data_ptr            = NULL;

   rd_data_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];

   for (uint32_t num_buf_sent = 0; num_buf_sent < num_buf_to_send; num_buf_sent++)
   {
      result = spdm_get_databuf_from_rd_datapool(spgm_ptr, rd_data_ptr);
      if (AR_EOK != result)
      {
         break;
      }

      read_data_buf_node_ptr             = rd_data_ptr->db_obj.active_buf_node_ptr;
      read_data_buf_node_ptr->buf_in_use = FALSE;
      read_data_buf_node_ptr->offset     = 0;

      result = spdm_send_read_data_buffer(spgm_ptr, read_data_buf_node_ptr, rd_data_ptr);
      rd_data_ptr->db_obj.active_buf_node_ptr = NULL;
   }

   return result;
}
