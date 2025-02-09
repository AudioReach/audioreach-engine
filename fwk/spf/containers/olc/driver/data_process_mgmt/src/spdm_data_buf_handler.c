/**
 * \file spdm_data_buf_handler.c
 * \brief
 *     This file contains Satellite Graph Management functions for buffer handling
 *  for data path
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spdm_i.h"
#include "sprm.h"

/* =======================================================================
Static Function Definitions
========================================================================== */

static bool_t spdm_get_data_buf_node_given_token(spf_list_node_t *      data_buf_list_ptr,
                                                 uint32_t               num_data_buf_list,
                                                 data_buf_pool_node_t **data_buf_node_ptr,
                                                 uint32_t               token)
{

   spf_list_node_t *     curr_node_ptr;
   data_buf_pool_node_t *cmd_hndl_node_ptr;
   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = data_buf_list_ptr;

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      cmd_hndl_node_ptr = (data_buf_pool_node_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == cmd_hndl_node_ptr)
      {
         return FALSE;
      }

      if (token == cmd_hndl_node_ptr->token)
      {
         *data_buf_node_ptr = cmd_hndl_node_ptr;
         return TRUE;
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return FALSE;
}

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

bool_t spdm_get_available_data_buf_node(spf_list_node_t *      data_buf_list_ptr,
                                        uint32_t               num_data_buf_list,
                                        data_buf_pool_node_t **data_buf_node_ptr)
{

   spf_list_node_t *     curr_node_ptr     = NULL;
   data_buf_pool_node_t *cmd_hndl_node_ptr = NULL;

   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = data_buf_list_ptr;

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      cmd_hndl_node_ptr = (data_buf_pool_node_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == cmd_hndl_node_ptr)
      {
         return FALSE;
      }

      if ((FALSE == cmd_hndl_node_ptr->buf_in_use))
      {
         *data_buf_node_ptr = cmd_hndl_node_ptr;
         return TRUE;
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return FALSE;
}

bool_t spdm_get_data_buf_node_from_list(spf_list_node_t *      data_buf_list_ptr,
                                        uint32_t               num_data_buf_list,
                                        data_buf_pool_node_t **data_buf_node_ptr)
{

   spf_list_node_t *     curr_node_ptr     = NULL;
   data_buf_pool_node_t *cmd_hndl_node_ptr = NULL;

   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = data_buf_list_ptr;

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      cmd_hndl_node_ptr = (data_buf_pool_node_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == cmd_hndl_node_ptr)
      {
         return FALSE;
      }

      //if ((FALSE == cmd_hndl_node_ptr->buf_in_use))
      {
         *data_buf_node_ptr = cmd_hndl_node_ptr;
         return TRUE;
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return FALSE;
}

static bool_t spdm_get_empty_data_buf_node(spf_list_node_t *      data_buf_list_ptr,
                                           uint32_t               num_data_buf_list,
                                           data_buf_pool_node_t **data_buf_node_ptr)
{

   spf_list_node_t *     curr_node_ptr;
   data_buf_pool_node_t *cmd_hndl_node_ptr;
   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = data_buf_list_ptr;

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      cmd_hndl_node_ptr = (data_buf_pool_node_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == cmd_hndl_node_ptr)
      {
         return FALSE;
      }

      if (TRUE == cmd_hndl_node_ptr->pending_alloc)
      {
         *data_buf_node_ptr = cmd_hndl_node_ptr;
         return TRUE;
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return FALSE;
}

/* Function to allocate the shared memory for the buffer nodes */
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

/* Function to add a buffer node to the buffer pool */
static ar_result_t spdm_add_node_to_data_pool(spgm_info_t *          spgm_ptr,
                                              shmem_data_buf_pool_t *data_pool_ptr,
                                              uint32_t               num_buf_nodes_to_add)
{
   ar_result_t           result        = AR_EOK;
   data_buf_pool_node_t *cur_node_ptr  = NULL;
   uint32_t              num_nodes_add = 0;
   uint32_t              token         = 0;
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
      token                       = posal_atomic_increment(spgm_ptr->token_instance);
      cur_node_ptr->token         = token;

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

/* function to remove the buffer node from the buffer pool list
 * basically frees the shared memory associated with the node
 * Then removes the node from the list
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
                   "DATA_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "failed to free the shared memory for the data buffer "
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
                   "DATA_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "failed to remove the node from the buf pool list "
                   "data types (wr:0/rd:1) %lu: port index 0x%x",
                   spm_id_ptr->cont_id,
                   spm_id_ptr->sat_pd,
                   data_pool_ptr->data_type,
                   data_pool_ptr->port_index);
      return result;
   }

   return result;
}

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

/* function to remove the buffer node from the buffer pool*/
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
            if (AR_EOK != (result = spdm_destroy_buffer_node(spgm_ptr, data_pool_ptr, data_buf_node_ptr, &spgm_ptr->sgm_id)))
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

/* function to remove the buffer node from the buffer pool*/
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
         //if (FALSE == data_buf_node_ptr->buf_in_use)
         {
            if (AR_EOK != (result = spdm_destroy_buffer_node(spgm_ptr, data_pool_ptr, data_buf_node_ptr, &spgm_ptr->sgm_id)))
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

/* This function is to update the data buffer pool
 * If the buffer size has changed, we remove all the buffer and create them new
 * If the size is same, but there is a need to increase/reduce the buffers, corresponding
 * functions are called  based on the requirement
 */
static ar_result_t spdm_update_data_buf_list(spgm_info_t *          spgm_ptr,
                                             shmem_data_buf_pool_t *data_pool_ptr,
                                             bool_t                 buf_size_changed)
{
   ar_result_t result           = AR_EOK;
   bool_t      add_buf_nodes    = FALSE;
   bool_t      remove_buf_nodes = FALSE;
   uint32_t    num_buf_nodes    = 0;
   add_buf_nodes                = (data_pool_ptr->num_data_buf_in_list < data_pool_ptr->req_num_data_buf);
   remove_buf_nodes             = (data_pool_ptr->num_data_buf_in_list > data_pool_ptr->req_num_data_buf);

   if (TRUE == add_buf_nodes)
   {
      /* add the additional buffers required */
      num_buf_nodes = data_pool_ptr->req_num_data_buf - data_pool_ptr->num_data_buf_in_list;
      if (AR_EOK != (result = spdm_add_node_to_data_pool(spgm_ptr, data_pool_ptr, num_buf_nodes)))
      {
         return result;
      }
   }
   else if (TRUE == remove_buf_nodes)
   {
      /* remove the buffers which are not further required */
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
 * This function is used to calculate the number of shared memory buffers
 * between OLC & Satellite graph
 */
static ar_result_t spdm_update_buffer_size(shmem_data_buf_pool_t *data_buf_pool_ptr,
                                           uint32_t               new_buf_size,
                                           sdm_ipc_data_type_t    data_type,
                                           uint32_t               port_index,
                                           uint32_t *             is_buf_size_changed)
{
   /* Get the frame size in num frames  */
   ar_result_t result     = AR_EOK;
   *is_buf_size_changed   = 0;
   uint32_t prev_buf_size = data_buf_pool_ptr->buf_size;

   data_buf_pool_ptr->buf_size = ALIGN_128_BYTES(new_buf_size); // VB : todo based on media format

   *is_buf_size_changed = (prev_buf_size != data_buf_pool_ptr->buf_size) ? 1 : 0;

   return result;
}

/**
 * This function is used to calculate the number of shared memory buffers
 * between OLC & Satellite graph
 */
static ar_result_t spdm_update_num_ipc_buffers(shmem_data_buf_pool_t *data_pool_ptr,
                                               sdm_ipc_data_type_t    data_type,
                                               uint32_t               port_index)
{
   /* Get the frame size in num frames  */
   ar_result_t result       = AR_EOK;
   uint32_t    new_num_bufs = 0;

   // TODO: VB update this function to return the buffer size based on the media format.

   if (IPC_WRITE_DATA == data_type)
   {
      new_num_bufs = 1;
   }
   else
   {
      new_num_bufs = 1;
   }

   data_pool_ptr->req_num_data_buf = new_num_bufs;

   return result;
}

/* Function to validate the argumenets */
static ar_result_t spdm_alloc_ipc_data_buffers_validate_arg(spgm_info_t *       spgm_ptr,
                                                            uint32_t            port_index,
                                                            sdm_ipc_data_type_t data_type)
{
   ar_result_t result = AR_EOK;
   if (NULL == spgm_ptr)
   {
      return AR_EUNEXPECTED;
   }

   if (port_index > SPDM_MAX_IO_PORTS)
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "invalid port index, allocate ipc data buffer failed for (wr:0/rd:1) %lu: ",
                  data_type);

      return AR_EFAILED;
   }

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

/* Function to allocate/reallocate the shared memory data buffer pool for the data processing.
 * - The function is used for read/write and all the port's.
 * - Creates the buffer pool when the media format is received for first time.
 * - Recreate the buffer pool for any change in the number of buffers or size of the buffers
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

   if (AR_EOK != (result = spdm_get_data_pool_ptr(spgm_ptr, port_index, data_type, &data_pool_ptr)))
   {
      return result;
   }

   // Update the Number of buffer.
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

ar_result_t sgm_recreate_output_buffers(spgm_info_t *spgm_ptr, uint32_t new_buf_size, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   spdm_alloc_ipc_data_buffers(spgm_ptr, new_buf_size, port_index, IPC_READ_DATA);
   return result;
}

ar_result_t sgm_send_all_read_buffers(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t           result                 = AR_EOK;
   ar_result_t           temp_result            = AR_EOK;
   data_buf_pool_node_t *read_data_buf_node_ptr = NULL;
   read_data_port_obj_t *rd_data_ptr            = NULL;

   rd_data_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];

   while (TRUE)
   {
      if (AR_EOK != (temp_result = spdm_get_databuf_from_rd_datapool(spgm_ptr, rd_data_ptr)))
      {
         break;
      }

      read_data_buf_node_ptr             = rd_data_ptr->db_obj.active_buf_node_ptr;
      read_data_buf_node_ptr->buf_in_use = FALSE;
      read_data_buf_node_ptr->offset     = 0;
      result                             = spdm_send_read_data_buffer(spgm_ptr, read_data_buf_node_ptr, rd_data_ptr);
      rd_data_ptr->db_obj.active_buf_node_ptr = NULL;
   }

   return result;
}

ar_result_t sgm_send_n_read_buffers(spgm_info_t *spgm_ptr, uint32_t port_index, uint32_t num_buf_to_send)
{
   ar_result_t           result                 = AR_EOK;
   data_buf_pool_node_t *read_data_buf_node_ptr = NULL;
   read_data_port_obj_t *rd_data_ptr            = NULL;

   rd_data_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];

   for (uint32_t num_buf_sent = 0; num_buf_sent < num_buf_to_send; num_buf_sent++)
   {
      if (AR_EOK != (result = spdm_get_databuf_from_rd_datapool(spgm_ptr, rd_data_ptr)))
      {
         break;
         // suppress the error here. Its not fatal
      }

      read_data_buf_node_ptr             = rd_data_ptr->db_obj.active_buf_node_ptr;
      read_data_buf_node_ptr->buf_in_use = FALSE;
      read_data_buf_node_ptr->offset     = 0;
      result                             = spdm_send_read_data_buffer(spgm_ptr, read_data_buf_node_ptr, rd_data_ptr);
      rd_data_ptr->db_obj.active_buf_node_ptr = NULL;
   }

   return result;
}
