/**
 * \file apm_shmem_util.c
 * \brief
 *     This file implements utility functions to manage shared memory between scorpion and Qdsp6, including physical
 *  addresses to virtual address mapping, etc.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "apm_internal.h"
#include "apm_memmap_api.h"

ar_result_t apm_shmem_cmd_handler(uint32_t memory_map_client, spf_msg_t *msg_ptr);

bool_t apm_shmem_is_supported_mem_pool(uint16_t mem_pool_id);

/**==============================================================================
   Global Defines
==============================================================================*/

apm_shmem_utils_vtable_t shmem_util_funcs = { .apm_shmem_cmd_handler_fptr           = apm_shmem_cmd_handler,
                                              .apm_shmem_is_supported_mem_pool_fptr = apm_shmem_is_supported_mem_pool };

/*----------------------------------------------------------------------------
 * Externalized Function Definitions
 * -------------------------------------------------------------------------*/
#if 0
static ar_result_t apm_shmem_handle_set_loaned_memory(uint32_t mem_map_client,
      apm_cmd_loan_global_shared_mem_t *loan_mem_payload_ptr)
{
   uint32_t mem_map_handle = 0;
   ar_result_t result = AR_EOK;

   if (0 == loan_mem_payload_ptr->loaned_mem_block_size)
   {
      return AR_EOK; // no loaned memory.
   }

   if (AR_DID_FAIL(
         result = posal_memorymap_get_mem_map_handle(mem_map_client, loan_mem_payload_ptr->shmem_id, &mem_map_handle)))
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_shmem_util: Failed to handle loaned memory as mapping not found. 0x%x", result);
      return result;
   }

   /** todo:
    *  create heap manager for the loaned memory and associate it with the shmem_id/mem_map_handle.
    */

   return AR_EOK;
}

static ar_result_t apm_shmem_handle_unset_loaned_memory(uint32_t mem_map_client,
      apm_cmd_unloan_global_shared_mem_t *unloan_mem_payload_ptr)
{
   uint32_t mem_map_handle = 0;
   ar_result_t result = AR_EOK;

   if (AR_DID_FAIL(
         result = posal_memorymap_get_mem_map_handle(mem_map_client, unloan_mem_payload_ptr->shmem_id,
                                                     &mem_map_handle)))
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_shmem_util: Failed to handle loaned memory as mapping not found. 0x%x", result);
      return result;
   }

   /** todo:
    *  delete heap manager for the loaned memory after ensuring all memory is freed
    */

   return AR_EOK;
}
#endif

static ar_result_t apm_mem_shared_memory_map_regions_cmd_handler(uint32_t      mem_map_client,
                                                                 gpr_packet_t *pkt_ptr,
                                                                 POSAL_HEAP_ID heap_id)
{
   ar_result_t                      result = AR_EOK;
   void *                           payload_ptr, *dummy_ptr;
   apm_cmd_shared_mem_map_regions_t common_mem_map_region_payload = { 0 };
   uint32_t                         mem_map_handle                = 0;
   uint32_t                         resp_opcode, resp_size;
   uint32_t                         heap_mngr_type  = APM_MEMORY_MAP_LOANED_MEMORY_HEAP_MNGR_TYPE_DEFAULT;
   uint32_t                         memory_map_type = APM_MEMORY_MAP_MEMORY_ADDRESS_TYPE_PA_OR_VA;

   gpr_packet_t *                    resp_pkt_ptr = NULL;
   POSAL_MEMORYPOOLTYPE              mem_pool;
   apm_shared_map_region_payload_t **region_pptr;

   bool_t is_virtual, is_cached, is_offset_map, is_mem_loaned;
   bool_t is_global_shmem_map_cmd = (APM_CMD_GLOBAL_SHARED_MEM_MAP_REGIONS == pkt_ptr->opcode) ? TRUE : FALSE;

   payload_ptr = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);

   if (is_global_shmem_map_cmd)
   {
      apm_cmd_global_shared_mem_map_regions_t *mem_map_regions_payload_ptr =
         (apm_cmd_global_shared_mem_map_regions_t *)payload_ptr;
      uint32_t temp = 0;

      common_mem_map_region_payload.mem_pool_id   = mem_map_regions_payload_ptr->mem_pool_id;
      common_mem_map_region_payload.num_regions   = mem_map_regions_payload_ptr->num_regions;
      common_mem_map_region_payload.property_flag = mem_map_regions_payload_ptr->property_flag;

      dummy_ptr   = ((uint8_t *)payload_ptr + sizeof(apm_cmd_global_shared_mem_map_regions_t));
      region_pptr = (apm_shared_map_region_payload_t **)&dummy_ptr;

      resp_opcode = GPR_IBASIC_RSP_RESULT;
      resp_size   = sizeof(gpr_ibasic_rsp_result_t);

      if (0 == mem_map_regions_payload_ptr->shmem_id ||
          AR_EOK == posal_memorymap_get_mem_map_handle(mem_map_client, mem_map_regions_payload_ptr->shmem_id, &temp))
      {
         AR_MSG(DBG_ERROR_PRIO, "received invalid shmem_id: %lu", mem_map_regions_payload_ptr->shmem_id);
         result = AR_EBADPARAM;
         goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_1;
      }
   }
   else
   {
      apm_cmd_shared_mem_map_regions_t *mem_map_regions_payload_ptr = (apm_cmd_shared_mem_map_regions_t *)payload_ptr;
      common_mem_map_region_payload                                 = *mem_map_regions_payload_ptr;

      dummy_ptr   = ((uint8_t *)payload_ptr + sizeof(apm_cmd_shared_mem_map_regions_t));
      region_pptr = (apm_shared_map_region_payload_t **)&dummy_ptr;

      resp_opcode = APM_CMD_RSP_SHARED_MEM_MAP_REGIONS;
      resp_size   = sizeof(apm_cmd_rsp_shared_mem_map_regions_t);
   }

   switch (common_mem_map_region_payload.mem_pool_id)
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
                common_mem_map_region_payload.mem_pool_id);

         goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_1;
      }
   }

   AR_MSG(DBG_HIGH_PRIO,
          "spf_shmem_util memory map: mem_pool_id 0x%hx, num_regions 0x%hx, property flag 0x%lx",
          common_mem_map_region_payload.mem_pool_id,
          common_mem_map_region_payload.num_regions,
          common_mem_map_region_payload.property_flag);

   is_virtual = (common_mem_map_region_payload.property_flag & APM_MEMORY_MAP_BIT_MASK_IS_VIRTUAL) >>
                APM_MEMORY_MAP_SHIFT_IS_VIRTUAL;
   is_cached     = !((common_mem_map_region_payload.property_flag & APM_MEMORY_MAP_BIT_MASK_IS_UNCACHED) >>
                 APM_MEMORY_MAP_SHIFT_IS_UNCACHED);
   is_offset_map = (common_mem_map_region_payload.property_flag & APM_MEMORY_MAP_BIT_MASK_IS_OFFSET_MODE) >>
                   APM_MEMORY_MAP_SHIFT_IS_OFFSET_MODE;

   if (!is_global_shmem_map_cmd)
   {
      is_mem_loaned = (common_mem_map_region_payload.property_flag & APM_MEMORY_MAP_BIT_MASK_IS_MEM_LOANED) >>
                      APM_MEMORY_MAP_SHIFT_IS_MEM_LOANED;

      heap_mngr_type =
         (common_mem_map_region_payload.property_flag & APM_MEMORY_MAP_BIT_MASK_LOANED_MEMORY_HEAP_MNGR_TYPE) >>
         APM_MEMORY_MAP_SHIFT_LOANED_MEMORY_HEAP_MNGR_TYPE;

      memory_map_type = (common_mem_map_region_payload.property_flag & APM_MEMORY_MAP_BIT_MASK_MEMORY_ADDRESS_TYPE) >>
                        APM_MEMORY_MAP_SHIFT_MEMORY_ADDRESS_TYPE;

      AR_MSG(DBG_HIGH_PRIO,
             "spf_shmem_util memory map, property flag 0x%lx,"
             " is_pa_or_va  %lu (pa:va - 0:1), is_cached %lu,"
             "is_offset_map %lu, is_mem_loaned %lu, "
             "heap_mgr_type %lu, memory_map_type %lu ",
             common_mem_map_region_payload.property_flag,
             is_virtual,
             !(is_cached),
             is_offset_map,
             is_mem_loaned,
             heap_mngr_type,
             memory_map_type);
   }

   /* Allocate the response packet */
   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = pkt_ptr->dst_domain_id;
   args.dst_domain_id = pkt_ptr->src_domain_id;
   args.src_port      = pkt_ptr->dst_port;
   args.dst_port      = pkt_ptr->src_port;
   args.token         = pkt_ptr->token;
   args.opcode        = resp_opcode;
   args.payload_size  = resp_size;
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

   // allocate posal_memorymap_shm_region_t, why not direct cast? because want to be free from
   // api data structure change or any compiler pack
   posal_memorymap_shm_region_t *phy_regions;
   if (NULL ==
       (phy_regions = (posal_memorymap_shm_region_t *)posal_memory_malloc(sizeof(posal_memorymap_shm_region_t) *
                                                                             common_mem_map_region_payload.num_regions,
                                                                          heap_id)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util MemMapRegCmdHandler: memory map region allocation failed, num_regions = %lu",
             common_mem_map_region_payload.num_regions);

      result = AR_ENOMEMORY;
      goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_2;
   }
   for (int i = 0; i < common_mem_map_region_payload.num_regions; ++i)
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

   if (0 == is_virtual)
   {
      // physical mapping
      if (AR_DID_FAIL(result = posal_memorymap_shm_mem_map(mem_map_client,
                                                           phy_regions,
                                                           common_mem_map_region_payload.num_regions,
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
                                                                common_mem_map_region_payload.num_regions,
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

   AR_MSG(DBG_HIGH_PRIO,
          "spf_shmem_util memory map success, mem_pool_id 0x%hx, num_regions 0x%hx, property flag 0x%lx, handle "
          "0x%lx",
          common_mem_map_region_payload.mem_pool_id,
          common_mem_map_region_payload.num_regions,
          common_mem_map_region_payload.property_flag,
          mem_map_handle);

   if (is_global_shmem_map_cmd)
   {
      gpr_ibasic_rsp_result_t *                resp_payload_ptr;
      apm_cmd_global_shared_mem_map_regions_t *mem_map_regions_payload_ptr =
         (apm_cmd_global_shared_mem_map_regions_t *)payload_ptr;

      if (AR_DID_FAIL(
             result =
                posal_memorymap_set_shmem_id(mem_map_client, mem_map_handle, mem_map_regions_payload_ptr->shmem_id)))
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_shmem_util: Failed to set shmem_id to mem_map_handle 0x%x", result);
         goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_3;
      }

      /* prepare the Response payload pointer */
      resp_payload_ptr = (gpr_ibasic_rsp_result_t *)GPR_PKT_GET_PAYLOAD(void, resp_pkt_ptr);

      resp_payload_ptr->opcode = pkt_ptr->opcode;
      resp_payload_ptr->status = AR_EOK;
   }
   else
   {
      apm_cmd_rsp_shared_mem_map_regions_t *resp_payload_ptr;

      // register here. is_loaned field will be false on the satellite, that's how we'll avoid heap creation there
      if (is_mem_loaned && is_offset_map)
      {

         /** Get the pointer to ext utils vtbl   */
         apm_ext_utils_t *ext_utils_ptr   = apm_get_ext_utils_ptr();
         posal_heap_t     heap_mgr_type_i = POSAL_HEAP_NON_ISLAND;

         if (APM_MEMORY_MAP_LOANED_MEMORY_HEAP_MNGR_TYPE_DEFAULT == heap_mngr_type)
         {
            heap_mgr_type_i = POSAL_HEAP_NON_ISLAND;
         }
         else if (APM_MEMORY_MAP_LOANED_MEMORY_HEAP_MNGR_TYPE_SAFE_HEAP == heap_mngr_type)
         {
            heap_mgr_type_i = POSAL_HEAP_NON_ISLAND_SAFE_HEAP;
         }

         AR_MSG(DBG_HIGH_PRIO,
                "spf_shmem_util memory map, property flag 0x%lx,"
                " is_pa_or_va  %lu (pa:va - 0:1), is_cached (0) %lu,"
                "is_offset_map(1) %lu, is_mem_loaned(1) %lu, "
                "heap_mgr_type %lu, memory_map_type %lu ",
                common_mem_map_region_payload.property_flag,
                is_virtual,
                is_cached,
                is_offset_map,
                is_mem_loaned,
                heap_mngr_type,
                memory_map_type);

         if (ext_utils_ptr->offload_vtbl_ptr &&
             ext_utils_ptr->offload_vtbl_ptr->apm_offload_master_memorymap_register_fptr)
         {
            if (AR_EOK !=
                (result =
                    ext_utils_ptr->offload_vtbl_ptr->apm_offload_master_memorymap_register_fptr(mem_map_client,
                                                                                                mem_map_handle,
                                                                                                phy_regions[0].mem_size,
                                                                                                heap_mgr_type_i)))
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

      /* prepare the Response payload pointer */
      resp_payload_ptr = (apm_cmd_rsp_shared_mem_map_regions_t *)GPR_PKT_GET_PAYLOAD(void, resp_pkt_ptr);
      // store the memory map handle in the response payload
      resp_payload_ptr->mem_map_handle = mem_map_handle;
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
   ar_result_t result = AR_EOK;
   void *      payload_ptr;

   apm_ext_utils_t *ext_utils_ptr;
   uint32_t         mem_map_handle;
   bool_t is_global_shmem_unmap_cmd = (APM_CMD_GLOBAL_SHARED_MEM_UNMAP_REGIONS == pkt_ptr->opcode) ? TRUE : FALSE;

   payload_ptr = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);

   if (is_global_shmem_unmap_cmd)
   {
      apm_cmd_global_shared_mem_unmap_regions_t *unmap_regions_payload_ptr =
         (apm_cmd_global_shared_mem_unmap_regions_t *)payload_ptr;

      if (AR_DID_FAIL(result = posal_memorymap_get_mem_map_handle(mem_map_client,
                                                                  unmap_regions_payload_ptr->shmem_id,
                                                                  &mem_map_handle)))
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_shmem_util: Failed to get mem_map_handle 0x%x", result);
         goto _apm_mem_shared_memory_un_map_regions_cmd_handler_bail_1;
      }
   }
   else
   {
      apm_cmd_shared_mem_unmap_regions_t *unmap_regions_payload_ptr = (apm_cmd_shared_mem_unmap_regions_t *)payload_ptr;
      mem_map_handle                                                = unmap_regions_payload_ptr->mem_map_handle;
   }

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   AR_MSG(DBG_HIGH_PRIO, "apm_shmem_util: memory unmap handle 0x%lx", mem_map_handle);

   if (0 == mem_map_handle)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util: null memory map handle received in the unmap cmd payload, client token %lu",
             mem_map_client);
      result = AR_EBADPARAM;
      goto _apm_mem_shared_memory_un_map_regions_cmd_handler_bail_1;
   }

   if (AR_DID_FAIL(result = posal_memorymap_shm_mem_unmap(mem_map_client, mem_map_handle)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util: Failed to unmap the physical memory - bailing out, error code is 0x%x, client token %lu, "
             "memmap handle "
             "0x%lx",
             result,
             mem_map_client,
             mem_map_handle);
      goto _apm_mem_shared_memory_un_map_regions_cmd_handler_bail_1;
   }

   if (!is_global_shmem_unmap_cmd && ext_utils_ptr->offload_vtbl_ptr &&
       ext_utils_ptr->offload_vtbl_ptr->apm_offload_master_memorymap_check_deregister_fptr)
   {
      // deregister here. have to search if the handle was registered and if so, deregister it
      if (AR_EOK != (result = ext_utils_ptr->offload_vtbl_ptr->apm_offload_master_memorymap_check_deregister_fptr(
                        mem_map_handle)))
      {
         AR_MSG(DBG_ERROR_PRIO, "MDF: Failed to deregister Loaned Master memory, Handle 0x%lx", mem_map_handle);
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
      case APM_CMD_GLOBAL_SHARED_MEM_MAP_REGIONS:
      {
         result =
            apm_mem_shared_memory_map_regions_cmd_handler(memory_map_client, gpr_pkt_ptr, APM_INTERNAL_STATIC_HEAP_ID);
         break;
      }
      case APM_CMD_SHARED_MEM_UNMAP_REGIONS:
      case APM_CMD_GLOBAL_SHARED_MEM_UNMAP_REGIONS:
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
