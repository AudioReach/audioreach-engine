/**
 * \file apm_shmem_util.c
 * \brief
 *     This file implements utility functions to manage shared memory between scorpion and Qdsp6, including physical
 *  addresses to virtual address mapping, etc.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "apm_internal.h"
#include "apm_memmap_api.h"

ar_result_t apm_shmem_cmd_handler(uint32_t memory_map_client, spf_msg_t *msg_ptr);

bool_t apm_shmem_is_supported_mem_pool(uint16_t mem_pool_id);

/**==============================================================================
   Global Defines
==============================================================================*/

apm_shmem_utils_vtable_t shmem_util_funcs = {.apm_shmem_cmd_handler_fptr = apm_shmem_cmd_handler,

                                             .apm_shmem_is_supported_mem_pool_fptr = apm_shmem_is_supported_mem_pool };

/*----------------------------------------------------------------------------
 * Externalized Function Definitions
 * -------------------------------------------------------------------------*/

static ar_result_t apm_mem_shared_memory_map_regions_cmd_handler(uint32_t      mem_map_client,
                                                                 gpr_packet_t *pkt_ptr,
                                                                 uint32_t      resp_opcode,
                                                                 POSAL_HEAP_ID heap_id)
{
   ar_result_t                       result = AR_EOK;
   void *                            payload_ptr, *dummy_ptr;
   apm_cmd_shared_mem_map_regions_t *mem_map_regions_payload_ptr;
   uint32_t                          mem_map_handle = 0;
   gpr_packet_t *                    resp_pkt_ptr   = NULL;
   POSAL_MEMORYPOOLTYPE              mem_pool;
   apm_shared_map_region_payload_t **region_pptr;
   bool_t                            is_virtual, is_cached, is_offset_map, is_mem_loaned;
   apm_ext_utils_t *                 ext_utils_ptr;

   payload_ptr = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);

   mem_map_regions_payload_ptr = (apm_cmd_shared_mem_map_regions_t *)payload_ptr;

   switch (mem_map_regions_payload_ptr->mem_pool_id)
   {
      case APM_MEMORY_MAP_SHMEM8_4K_POOL:
      {
         mem_pool = POSAL_MEMORYMAP_SHMEM8_4K_POOL;
         break;
      }
      default:
      {
         result = AR_EBADPARAM;
         AR_MSG(DBG_ERROR_PRIO,
                "spf_shmem_util MemMapRegCmdHandler: Received Invalid PoolID: %d",
                mem_map_regions_payload_ptr->mem_pool_id);

         goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_1;
      }
   }

   AR_MSG(DBG_HIGH_PRIO,
          "spf_shmem_util memory map: mem_pool_id 0x%hx, num_regions 0x%hx, property flag 0x%lx",
          mem_map_regions_payload_ptr->mem_pool_id,
          mem_map_regions_payload_ptr->num_regions,
          mem_map_regions_payload_ptr->property_flag);

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   /* Allocate the response packet */
   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = pkt_ptr->dst_domain_id;
   args.dst_domain_id = pkt_ptr->src_domain_id;
   args.src_port      = pkt_ptr->dst_port;
   args.dst_port      = pkt_ptr->src_port;
   args.token         = pkt_ptr->token;
   args.opcode        = resp_opcode;
   args.payload_size  = sizeof(apm_cmd_rsp_shared_mem_map_regions_t);
   args.ret_packet    = &resp_pkt_ptr;
   args.client_data   = 0;
   result             = __gpr_cmd_alloc_ext(&args);

   if (AR_DID_FAIL(result) || NULL == resp_pkt_ptr)
   {
      result = AR_ENOMEMORY;
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util MemMapRegCmdHandler: memory map response packet allocation failed with error code  = %d!",
             result);

      goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_1;
   }

   /* prepare the Response payload pointer */
   apm_cmd_rsp_shared_mem_map_regions_t *resp_payload_ptr;
   dummy_ptr        = GPR_PKT_GET_PAYLOAD(void, resp_pkt_ptr);
   resp_payload_ptr = (apm_cmd_rsp_shared_mem_map_regions_t *)dummy_ptr;

   dummy_ptr   = ((uint8_t *)payload_ptr + sizeof(apm_cmd_shared_mem_map_regions_t));
   region_pptr = (apm_shared_map_region_payload_t **)&dummy_ptr;

   // allocate posal_memorymap_shm_region_t, why not direct cast? because want to be free from
   // api data structure change or any compiler pack
   posal_memorymap_shm_region_t *phy_regions;
   if (NULL ==
       (phy_regions = (posal_memorymap_shm_region_t *)posal_memory_malloc(sizeof(posal_memorymap_shm_region_t) *
                                                                             mem_map_regions_payload_ptr->num_regions,
                                                                          heap_id)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util MemMapRegCmdHandler: memory map region allocation failed, num_regions = %lu",
             mem_map_regions_payload_ptr->num_regions);

      result = AR_ENOMEMORY;
      goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_2;
   }
   for (int i = 0; i < mem_map_regions_payload_ptr->num_regions; ++i)
   {
      phy_regions[i].shm_addr_lsw = (*region_pptr + i)->shm_addr_lsw;
      phy_regions[i].shm_addr_msw = (*region_pptr + i)->shm_addr_msw;
      phy_regions[i].mem_size     = (*region_pptr + i)->mem_size_bytes;

      AR_MSG(DBG_HIGH_PRIO,
             "spf_shmem_util memory map: region %d, shm_addr_lsw 0x%lx, shm_addr_msw 0x%lx, mem size %ld",
             i,
             phy_regions[i].shm_addr_lsw,
             phy_regions[i].shm_addr_msw,
             phy_regions[i].mem_size);
   }

   is_virtual = (mem_map_regions_payload_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_IS_VIRTUAL) >>
                APM_MEMORY_MAP_SHIFT_IS_VIRTUAL;
   is_cached = !((mem_map_regions_payload_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_IS_UNCACHED) >>
                 APM_MEMORY_MAP_SHIFT_IS_UNCACHED);
   is_offset_map = (mem_map_regions_payload_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_IS_OFFSET_MODE) >>
                   APM_MEMORY_MAP_SHIFT_IS_OFFSET_MODE;
   is_mem_loaned = (mem_map_regions_payload_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_IS_MEM_LOANED) >>
                   APM_MEMORY_MAP_SHIFT_IS_MEM_LOANED;

   if (0 == is_virtual)
   {
      // physical mapping
      if (AR_DID_FAIL(result = posal_memorymap_shm_mem_map(mem_map_client,
                                                           phy_regions,
                                                           mem_map_regions_payload_ptr->num_regions,
                                                           is_cached,
                                                           is_offset_map,
                                                           mem_pool,
                                                           &mem_map_handle,
                                                           heap_id)))
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_shmem_util: Failed to map the physical memory, error code is 0x%x", result);
         goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_3;
      }
   }
   else if (1 == is_virtual)
   {
      // virtual mapping
      if (AR_DID_FAIL(result = posal_memorymap_virtaddr_mem_map(mem_map_client,
                                                                phy_regions,
                                                                mem_map_regions_payload_ptr->num_regions,
                                                                is_cached,
                                                                is_offset_map,
                                                                mem_pool,
                                                                &mem_map_handle,
                                                                heap_id)))
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_shmem_util: Failed to map the virual memory, error code is 0x%x", result);
         goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_3;
      }
   }
   else
   {
      result = AR_EBADPARAM;
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util: invalid property flag received in the payload, error code is 0x%x",
             result);
      goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_3;
   }

   // store the memory map handle in the response payload
   resp_payload_ptr->mem_map_handle = mem_map_handle;
   // register here. is_loaned field will be false on the satellite, that's how we'll avoid heap creation there

   AR_MSG(DBG_HIGH_PRIO,
          "spf_shmem_util memory map success, mem_pool_id 0x%hx, num_regions 0x%hx, property flag 0x%lx, handle "
          "0x%lx",
          mem_map_regions_payload_ptr->mem_pool_id,
          mem_map_regions_payload_ptr->num_regions,
          mem_map_regions_payload_ptr->property_flag,
          mem_map_handle);

   if (is_mem_loaned && is_offset_map)
   {
      if (ext_utils_ptr->offload_vtbl_ptr &&
          ext_utils_ptr->offload_vtbl_ptr->apm_offload_master_memorymap_register_fptr)
      {
         if (AR_EOK !=
             (result =
                 ext_utils_ptr->offload_vtbl_ptr->apm_offload_master_memorymap_register_fptr(mem_map_client,
                                                                                             mem_map_handle,
                                                                                             phy_regions[0].mem_size)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "MDF: Failed to register Loaned Master memory, Handle 0x%lx of size %lu",
                   mem_map_handle,
                   phy_regions[0].mem_size);

            goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_3;
         }
      }
      else /** Offload mem map operations are not supported */
      {
         result = AR_EUNSUPPORTED;

         AR_MSG(DBG_ERROR_PRIO, "MDF: Offload memory map operation is not supported");

         goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_3;
      }
   }

   /* send response */
   if (AR_EOK != (result = __gpr_cmd_async_send(resp_pkt_ptr)))
   {
      result = AR_EFAILED;
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util: failed to send the memory map response command, error code is  = 0x%x",
             result);
      goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_3;
   }

   posal_memory_free(phy_regions);
   __gpr_cmd_free(pkt_ptr);
   return result;

_apm_mem_shared_memory_map_regions_cmd_handler_bail_3:
   posal_memory_free(phy_regions);
_apm_mem_shared_memory_map_regions_cmd_handler_bail_2:
   __gpr_cmd_free(resp_pkt_ptr);
_apm_mem_shared_memory_map_regions_cmd_handler_bail_1:
   __gpr_cmd_end_command(pkt_ptr, result);
   return result;
}

static ar_result_t apm_mem_shared_memory_un_map_regions_cmd_handler(uint32_t mem_map_client, gpr_packet_t *pkt_ptr)
{
   ar_result_t                         result = AR_EOK;
   void *                              payload_ptr;
   apm_cmd_shared_mem_unmap_regions_t *unmap_regions_payload_ptr;
   apm_ext_utils_t *                   ext_utils_ptr;

   payload_ptr = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);

   unmap_regions_payload_ptr = (apm_cmd_shared_mem_unmap_regions_t *)payload_ptr;

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   AR_MSG(DBG_HIGH_PRIO, "apm_shmem_util: memory unmap handle 0x%lx", unmap_regions_payload_ptr->mem_map_handle);

   if (0 == unmap_regions_payload_ptr->mem_map_handle)
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_shmem_util: null memory map handle received in the unmap cmd payload, client token %lu", mem_map_client);
      result = AR_EBADPARAM;
      goto _apm_mem_shared_memory_un_map_regions_cmd_handler_bail_1;
   }

   if (AR_DID_FAIL(result = posal_memorymap_shm_mem_unmap(mem_map_client, unmap_regions_payload_ptr->mem_map_handle)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util: Failed to unmap the physical memory - bailing out, error code is 0x%x, client token %lu, memmap handle "
             "0x%lx",
             result,
             mem_map_client,
             unmap_regions_payload_ptr->mem_map_handle);
      goto _apm_mem_shared_memory_un_map_regions_cmd_handler_bail_1;
   }

   if (ext_utils_ptr->offload_vtbl_ptr &&
       ext_utils_ptr->offload_vtbl_ptr->apm_offload_master_memorymap_check_deregister_fptr)
   {
      // deregister here. have to search if the handle was registered and if so, deregister it
      if (AR_EOK != (result = ext_utils_ptr->offload_vtbl_ptr->apm_offload_master_memorymap_check_deregister_fptr(
                        unmap_regions_payload_ptr->mem_map_handle)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "MDF: Failed to deregister Loaned Master memory, Handle 0x%lx",
                unmap_regions_payload_ptr->mem_map_handle);
         goto _apm_mem_shared_memory_un_map_regions_cmd_handler_bail_1;
      }
   }

_apm_mem_shared_memory_un_map_regions_cmd_handler_bail_1:

   __gpr_cmd_end_command(pkt_ptr, result);

   return result;
}

bool_t apm_shmem_is_supported_mem_pool(uint16_t mem_pool_id)
{
   bool_t result = FALSE;
   switch (mem_pool_id)
   {
      case APM_MEMORY_MAP_SHMEM8_4K_POOL:
      {
         result = TRUE;
         break;
      }
      default:
      {
         result = FALSE;
      }
   }
   return result;
}

ar_result_t apm_shmem_cmd_handler(uint32_t memory_map_client, spf_msg_t *msg_ptr)
{
   ar_result_t   result = AR_EOK;
   gpr_packet_t *gpr_pkt_ptr;
   uint32_t      cmd_opcode;

   /** Get the pointer to GPR command */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   /** Get the GPR command opcode */
   cmd_opcode = gpr_pkt_ptr->opcode;

   switch (cmd_opcode)
   {
      case APM_CMD_SHARED_MEM_MAP_REGIONS:
      {
         result = apm_mem_shared_memory_map_regions_cmd_handler(memory_map_client,
                                                                gpr_pkt_ptr,
                                                                APM_CMD_RSP_SHARED_MEM_MAP_REGIONS,
                                                                APM_INTERNAL_STATIC_HEAP_ID);
         break;
      }
      case APM_CMD_SHARED_MEM_UNMAP_REGIONS:
      {
         result = apm_mem_shared_memory_un_map_regions_cmd_handler(memory_map_client, gpr_pkt_ptr);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_shmem_cmd_handler(): Unsupported cmd opcode: 0x%lX", cmd_opcode);

         result = AR_EUNSUPPORTED;

         break;
      }
   }

   return result;
}

ar_result_t apm_shmem_utils_init(apm_t *apm_info_ptr)
{
   ar_result_t result = AR_EOK;

   apm_info_ptr->ext_utils.shmem_vtbl_ptr = &shmem_util_funcs;

   return result;
}
