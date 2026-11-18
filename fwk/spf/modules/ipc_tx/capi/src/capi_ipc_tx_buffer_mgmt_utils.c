/* =========================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
 * =========================================================================*/

/**
 * \file capi_ipc_tx_buffer_mgmt_utils.cpp
 *
 * C source file to implement the buffer management utilities for IPC_TX module
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------

 * =========================================================================*/

/**----------------------------------------------------------------------------
 ** Include Files
 ** -------------------------------------------------------------------------*/

#include "capi_ipc_tx_utils.h"

/**----------------------------------------------------------------------------
 ** Function Definitions
 ** -------------------------------------------------------------------------*/
static capi_err_t capi_ipc_tx_send_buffer_to_self(capi_ipc_tx_t *me_ptr, uint32_t buffer_index)
{
   capi_err_t capi_result = CAPI_EOK;

   data_cmd_rsp_wr_sh_mem_ep_data_buffer_done_v2_t write_done_payload;

   write_done_payload.data_buf_addr_lsw   = me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.offset;
   write_done_payload.data_buf_addr_msw   = 0;
   write_done_payload.data_mem_map_handle = me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.sat_handle;

   write_done_payload.data_status = 0;

   write_done_payload.md_buf_addr_lsw   = 0;
   write_done_payload.md_buf_addr_msw   = 0;
   write_done_payload.md_mem_map_handle = write_done_payload.md_mem_map_handle; // todo: need to update later

   write_done_payload.md_status = 0;
#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              " Sending write_done_payload.data_buf_addr_lsw 0x%lx, write_done_payload.data_buf_addr_msw 0x%lx, "
              "write_done_payload.data_mem_map_handle 0x%lx, write_done_payload.data_status 0x%lx",
              write_done_payload.data_buf_addr_lsw,
              write_done_payload.data_buf_addr_msw,
              write_done_payload.data_mem_map_handle,
              write_done_payload.data_status);
   IPC_TX_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              " Sending write_done_payload.md_buf_addr_lsw 0x%lx, write_done_payload.md_buf_addr_msw 0x%lx, "
              "write_done_payload.md_mem_map_handle 0x%lx, write_done_payload.md_status 0x%lx",
              write_done_payload.md_buf_addr_lsw,
              write_done_payload.md_buf_addr_msw,
              write_done_payload.md_mem_map_handle,
              write_done_payload.md_status);
#endif
   me_ptr->base_token++;
   // Send the first buffer to the IPC TX module
   gpr_cmd_alloc_send_t args;
   args.src_domain_id = me_ptr->ipc_tx_link_info.self_proc_domain_id;
   args.dst_domain_id = me_ptr->ipc_tx_link_info.self_proc_domain_id;
   args.src_port      = me_ptr->ipc_tx_link_info.self_module_iid;
   args.dst_port      = me_ptr->ipc_tx_link_info.self_module_iid;
   args.token         = me_ptr->base_token;
   args.opcode        = DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2;
   args.payload       = &write_done_payload;
   args.payload_size  = sizeof(write_done_payload);
   args.client_data   = 0;
   capi_result        = __gpr_cmd_alloc_send(&args); // fully creates and sends a msg

   if (capi_result != CAPI_EOK)
   {
      IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Sending buffer from ipc tx to self failed with 0x%lx", capi_result);
      return capi_result;
   }
   else
   {
      IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Sent buffer from ipc tx to self with token 0x%lx", me_ptr->base_token);
   }
   return CAPI_EOK;
}

/**
 * @brief Resets and frees a shared memory buffer handle.
 *
 * @param buf_handle Pointer to the shared memory buffer handle to be reset and freed.
 *
 * @return None
 */
static void capi_ipc_tx_reset_and_free_shmem_handle(shmem_handle_t *buf_handle)
{
   if (buf_handle != NULL)
   {
      // Free the allocated shared memory
      apm_offload_memory_free(&buf_handle->mem_attr);
      // Reset the shared memory handle
      memset(buf_handle, 0, sizeof(shmem_handle_t));
   }
}

capi_err_t capi_ipc_tx_free_shmem(capi_ipc_tx_t *me_ptr, uint32_t buffer_index)
{
   capi_err_t capi_result = CAPI_EOK;
   // uint32_t buffer_index = me_ptr->sh_buf_info.curr_buff_index;

   // Check if the MDF shared mefmory is a valid memory
   if ((NULL != me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].shm_mem_ptr) &&
       (NULL != me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.ret_ptr))
   {
      shmem_handle_t *buf_handle = &me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index];
      // Reset and free the shared memory handle
      capi_ipc_tx_reset_and_free_shmem_handle(buf_handle);
      // Reduce the number of buffers to detroy
      me_ptr->sh_buf_info.num_bufs_to_destroy--;
      IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Freed the shared memory of buffer index %u", buffer_index);
      if (me_ptr->sh_buf_info.num_bufs_to_destroy == 0)
      {
         me_ptr->sh_buf_info.num_ipc_bufs_created = me_ptr->sh_buf_info.num_ipc_bufs_needed;
      }
   }
   else
   {
      IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Shared memory of buffer index %u is already freed", buffer_index);
   }
   return capi_result;
}

capi_err_t capi_ipc_tx_check_recreate_buffer(capi_ipc_tx_t      *me_ptr,
                                             capi_stream_data_t *input[],
                                             uint32_t           *meta_data_size_ptr)
{
   capi_err_t             capi_result    = CAPI_EOK;
   capi_stream_data_v2_t *input_data_ptr = (capi_stream_data_v2_t *)input[0];
   uint32_t               meta_data_size = *meta_data_size_ptr;

   // Get the metadata size from the input data
   if (AR_EOK !=
       (capi_result = ipc_tx_get_write_meta_data_size(me_ptr, input_data_ptr->metadata_list_ptr, &meta_data_size)))
   {
      IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "write_data: failed to get metadata size");
      return capi_result;
   }

   // Check if the shared buffer needs to be recreated based on the metadata size
   uint32_t new_meta_data_buf_size = ALIGN_128_BYTES(meta_data_size);
   if (me_ptr->sh_buf_info.metadata_buff_size < new_meta_data_buf_size)
   {

      IPC_TX_MSG(me_ptr->miid,
                 DBG_MED_PRIO,
                 "write meta data processing: recreate MD buffer  %lu(new) > %lu(old) ",
                 new_meta_data_buf_size,
                 me_ptr->sh_buf_info.metadata_buff_size);

                 // Allocate a new shared buffer with the updated metadata size
      if (
         CAPI_EOK !=
         (capi_result =
             capi_ipc_tx_handle_buffer_reallocation(me_ptr,
                                                    me_ptr->sh_buf_info.data_buff_size +
                                                       new_meta_data_buf_size))) // TBD: maybe we can directly call
                                                                                 // capi_ipc_tx_handle_buffer_reallocation
      {
         IPC_TX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "write_data: failed to manage shared memory WR buffer size %lu",
                    new_meta_data_buf_size);
         return capi_result;
      }
   }

   *meta_data_size_ptr                    = meta_data_size;
   me_ptr->sh_buf_info.metadata_buff_size = new_meta_data_buf_size;

   return capi_result;
}

capi_err_t capi_ipc_tx_handle_buffer_reallocation(capi_ipc_tx_t *me_ptr, uint32_t new_shmem_size)
{
   uint8_t *curr_buf_ptr = me_ptr->sh_buf_info.curr_buff =
      (uint8_t *)me_ptr->sh_buf_info.shared_mem_buf_handle[me_ptr->sh_buf_info.curr_buff_index].shm_mem_ptr;
   uint32_t num_bufs = me_ptr->sh_buf_info.num_ipc_bufs_created;

   if (!curr_buf_ptr)
   {
      IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "shr buf ptr is not created");
      return CAPI_EOK;
   }

   for (uint32_t buf_index = 0; buf_index < num_bufs; ++buf_index)
   {
      shmem_handle_t *buf = &me_ptr->sh_buf_info.shared_mem_buf_handle[buf_index];
      if (buf->shm_mem_ptr == curr_buf_ptr)
      {
         if (buf->shm_alloc_size != new_shmem_size)
         {
            // Reset and free the shared memory handle
            capi_ipc_tx_reset_and_free_shmem_handle(buf);

            apm_offload_ret_info_t ret_info;
            void                  *new_shmem_buf_ptr =
               apm_offload_memory_malloc(me_ptr->ipc_tx_link_info.peer_proc_domain_id, new_shmem_size, &ret_info);
            if (!new_shmem_buf_ptr)
            {
               IPC_TX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Memory allocation of size %u failed for returned buffer of index %u.",
                          new_shmem_size,
                          buf_index);
               return AR_ENOMEMORY;
            }

            buf->mem_attr       = ret_info;
            buf->shm_alloc_size = new_shmem_size;
            buf->shm_mem_ptr    = new_shmem_buf_ptr;
            buf->status         = IPC_BUF_IN_USE;

            IPC_TX_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "Resized returned buffer of index %u to a new size of %u.",
                       buf_index,
                       new_shmem_size);
         }
         else
         {
            buf->status = IPC_BUF_IN_USE;
            IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Returned buffer is valid and reused.");
         }

         return CAPI_EOK;
      }
   }

   IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Returned buffer not found in buffer list.");
   return CAPI_EFAILED;
}

static capi_err_t capi_ipc_tx_create_new_buffers(capi_ipc_tx_t *me_ptr, uint32_t new_num_bufs, uint32_t new_shmem_size)
{
   capi_err_t capi_result  = CAPI_EOK;
   uint32_t   old_num_bufs = me_ptr->sh_buf_info.num_ipc_bufs_created;

   // Allocate a new array of shared memory handles with the new buffer count size
   shmem_handle_t *new_mem_handle_arr = (shmem_handle_t *)posal_memory_malloc(new_num_bufs * sizeof(shmem_handle_t),
                                                                              (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);

   if (!new_mem_handle_arr)
   {
      IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to allocate new mem handle array.");
      return AR_ENOMEMORY;
   }

   // Copy the existing shared memory handles to the new array
   memscpy(new_mem_handle_arr,
           old_num_bufs * sizeof(shmem_handle_t),
           me_ptr->sh_buf_info.shared_mem_buf_handle,
           old_num_bufs * sizeof(shmem_handle_t));

   // Free the old array of shared memory handles
   posal_memory_free(me_ptr->sh_buf_info.shared_mem_buf_handle);

   // Update the new array of shared memory handles
   me_ptr->sh_buf_info.shared_mem_buf_handle = new_mem_handle_arr;

   for (uint32_t buf_index = old_num_bufs; buf_index < new_num_bufs; ++buf_index)
   {
      apm_offload_ret_info_t ret_info;
      memset(&ret_info, 0, sizeof(ret_info));
      void                  *new_shmem_buf_ptr =
         apm_offload_memory_malloc(me_ptr->ipc_tx_link_info.peer_proc_domain_id, new_shmem_size, &ret_info);
      if (!new_shmem_buf_ptr)
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to create buffer %lu", buf_index);
         return AR_ENOMEMORY;
      }

      me_ptr->sh_buf_info.shared_mem_buf_handle[buf_index].mem_attr       = ret_info;
      me_ptr->sh_buf_info.shared_mem_buf_handle[buf_index].shm_alloc_size = new_shmem_size;
      me_ptr->sh_buf_info.shared_mem_buf_handle[buf_index].shm_mem_ptr    = new_shmem_buf_ptr;
      me_ptr->sh_buf_info.shared_mem_buf_handle[buf_index].status         = IPC_BUF_IN_USE;

      capi_result = capi_ipc_tx_send_buffer_to_self(me_ptr, buf_index);

      if (capi_result != CAPI_EOK)
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "failed to send the buffer to self of index %lu", buf_index);
         return capi_result;
      }
      else
      {
         IPC_TX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Buffer sent to self. index = %lu, &mem_attr 0x%lx, shm_alloc_size %d, shm_mem_ptr 0x%p",
                    buf_index,
                    &ret_info,
                    me_ptr->sh_buf_info.shared_mem_buf_handle[buf_index].shm_alloc_size,
                    me_ptr->sh_buf_info.shared_mem_buf_handle[buf_index].shm_mem_ptr);
      }
   }

   if (FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED != me_ptr->output_trigger_info)
   {
      me_ptr->output_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED; // now the container will start waiting on output
                                                                     // port after receiving MF and creation of buffers
   }

   me_ptr->sh_buf_info.num_ipc_bufs_created = new_num_bufs;
   return CAPI_EOK;
}

static void capi_ipc_tx_mark_buffers_for_destroy(capi_ipc_tx_t *me_ptr, uint32_t new_num_bufs)
{
   uint32_t old_num_bufs = me_ptr->sh_buf_info.num_ipc_bufs_created;

   for (uint32_t buf_index = new_num_bufs; buf_index < old_num_bufs; buf_index++)
   {
      me_ptr->sh_buf_info.shared_mem_buf_handle[buf_index].status = IPC_BUF_READY_TO_DESTROY;
   }
}

capi_err_t capi_ipc_tx_manage_buffer(capi_ipc_tx_t *me_ptr, uint32_t new_data_size, uint32_t new_md_size)
{
   capi_err_t capi_result  = CAPI_EOK;
   uint32_t   old_num_bufs = me_ptr->sh_buf_info.num_ipc_bufs_created;
   uint32_t   new_num_bufs = me_ptr->sh_buf_info.num_ipc_bufs_needed;

   me_ptr->sh_buf_info.shmem_alloc_size = ALIGN_128_BYTES(new_data_size) + ALIGN_128_BYTES(new_md_size);

   // Case 1: Create new buffers if needed
   if (new_num_bufs > old_num_bufs)
   {
      capi_result = capi_ipc_tx_create_new_buffers(me_ptr, new_num_bufs, me_ptr->sh_buf_info.shmem_alloc_size);
      if (capi_result != CAPI_EOK)
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "failed to create new buffers");
         return capi_result;
      }
   }
   else if (new_num_bufs < old_num_bufs)
   { // Case 2: Mark excess buffers for destroy
      me_ptr->sh_buf_info.num_bufs_to_destroy = old_num_bufs - new_num_bufs;
      capi_ipc_tx_mark_buffers_for_destroy(me_ptr, new_num_bufs);
   }
   else
   { // Case 3: Handle destroy or resizing of buffer
      capi_result = capi_ipc_tx_handle_buffer_reallocation(me_ptr, me_ptr->sh_buf_info.shmem_alloc_size);
      if (capi_result != CAPI_EOK)
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "failed to handle resizing of shared buffer");
         return capi_result;
      }
      // me_ptr->output_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED; // todo review once
   }

   if (me_ptr->sh_buf_info.overrun_buffer_size != new_data_size)
   {
      posal_memory_free(me_ptr->sh_buf_info.overrun_buffer_ptr);
      me_ptr->sh_buf_info.overrun_buffer_ptr = NULL;

      me_ptr->sh_buf_info.overrun_buffer_ptr =
         posal_memory_malloc(new_data_size, (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);

      if (!me_ptr->sh_buf_info.overrun_buffer_ptr)
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to allocate overrun buffer of size %lu", new_data_size);
         return AR_ENOMEMORY;
      }

      IPC_TX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "Allocated overrun buffer 0x%p size %lu",
                 me_ptr->sh_buf_info.overrun_buffer_ptr,
                 new_data_size);

      me_ptr->sh_buf_info.overrun_buffer_size = new_data_size;
   }

   // Update the new sizes
   me_ptr->sh_buf_info.data_buff_size     = ALIGN_128_BYTES(new_data_size);
   me_ptr->sh_buf_info.metadata_buff_size = ALIGN_128_BYTES(new_md_size);

   IPC_TX_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "new data size %d, md size %d, total %d",
              new_data_size,
              new_md_size,
              me_ptr->sh_buf_info.shmem_alloc_size);
   IPC_TX_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "aligned: new data size %d, md size %d",
              me_ptr->sh_buf_info.data_buff_size,
              me_ptr->sh_buf_info.metadata_buff_size);

   return CAPI_EOK;
}
