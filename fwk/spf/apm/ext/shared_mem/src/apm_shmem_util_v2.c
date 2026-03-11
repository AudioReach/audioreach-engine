/**
 * \file apm_shmem_util.c
 * \brief
 *     This file implements utility functions to manage shared memory between scorpion and Qdsp6, including physical
 *  addresses to virtual address mapping, etc.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "apm_internal.h"
#include "apm_memmap_api.h"
#include "posal.h"
#include "apm_offload_memmap_handler.h"
#include "apm_offload_memmap_utils.h"
#include "apm_memmap_api.h"
#include "apm_cmd_utils.h"
#include "apm_ext_cmn.h"
#include "apm.h"
#include "apm_shmem_util_i.h"

#define REQUIRES_RESP_HANDLING TRUE

/**==============================================================================
   Global Defines
==============================================================================*/

#ifdef APM_SHMEM_ONLY_V2_SUPPORTED

bool_t apm_shmem_is_supported_mem_pool(uint16_t mem_pool_id);

apm_shmem_utils_vtable_t shmem_util_v2_funcs = { .apm_shmem_cmd_handler_fptr = apm_shmem_cmd_handler_v2,
                                                 .apm_shmem_is_supported_mem_pool_fptr =
                                                    apm_shmem_is_supported_mem_pool };

ar_result_t apm_shmem_utils_init(apm_t *apm_info_ptr)
{
   ar_result_t result = AR_EOK;

   apm_info_ptr->ext_utils.shmem_vtbl_ptr = &shmem_util_v2_funcs;

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
#endif

static ar_result_t apm_forward_gpr_cmd_to_dest_proc_domain(apm_t        *apm_info_ptr,
                                                           spf_msg_t    *msg_ptr,
                                                           gpr_packet_t *incoming_pkt_ptr,
                                                           void         *incoming_gpr_payload_ptr,
                                                           uint32_t      dest_proc_domain_id,
                                                           uint32_t      gpr_payload_size,
                                                           bool_t        requires_response_handling)
{
   ar_result_t   result           = AR_EOK;
   gpr_packet_t *outgoing_pkt_ptr = NULL;

   if (requires_response_handling)
   {
      /** Allocate command handler resources  */
      if (AR_EOK != (result = apm_allocate_cmd_hdlr_resources(apm_info_ptr, msg_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_shem_utils_v2: Failed to allocate cmd rsc");

         __gpr_cmd_end_command(incoming_pkt_ptr, result);
         return result;
      }
   }

   // allocate the GPR packet to Send to peer DSP
   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = incoming_pkt_ptr->dst_domain_id; // current DSP thats handling client commands
   args.src_port      = incoming_pkt_ptr->dst_port;
   args.dst_domain_id = dest_proc_domain_id;
   args.dst_port      = incoming_pkt_ptr->dst_port;
   args.client_data   = 0;
   args.token         = apm_info_ptr->curr_cmd_ctrl_ptr->list_idx;

   // Will use this token during response tracking and for bookkeeping
   args.opcode       = incoming_pkt_ptr->opcode;
   args.payload_size = gpr_payload_size;
   args.ret_packet   = &outgoing_pkt_ptr;

   result = __gpr_cmd_alloc_ext(&args);
   if (AR_DID_FAIL(result) || NULL == outgoing_pkt_ptr)
   {
      result = AR_ENOMEMORY;
      AR_MSG(DBG_ERROR_PRIO, "apm_shem_utils_v2: Allocating mapping command failed with %lu", result);
      goto __bailout_1;
   }

   /* prepare the cmd payload */
   int8_t *out_pkt_payload_ptr = GPR_PKT_GET_PAYLOAD(int8_t, outgoing_pkt_ptr);

   memscpy(out_pkt_payload_ptr, gpr_payload_size, incoming_gpr_payload_ptr, gpr_payload_size);

   AR_MSG(DBG_LOW_PRIO,
      "APM in proc domain 0x%X forwared opcode 0x%lx payload size %lu to dest proc 0x%X client_token: 0x%lx",
      incoming_pkt_ptr->dst_domain_id,
      incoming_pkt_ptr->opcode,
      gpr_payload_size,
      dest_proc_domain_id,
      incoming_pkt_ptr->token);

   if (AR_EOK != (result = __gpr_cmd_async_send(outgoing_pkt_ptr)))
   {
      result = AR_EFAILED;
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shem_utils_v2: forwarding command to proc domain id %lu failed with %lu ",
             dest_proc_domain_id,
             result);
      goto __bailout_2;
   }

   if (requires_response_handling)
   {
      /** Increment the number of commands issues */
      apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.num_cmd_issued++;

      /** If at least 1 command issued to container, set the
       *  command response pending status */
      apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending = CMD_RSP_PENDING;
   }

   return result;

__bailout_2:
   __gpr_cmd_free(outgoing_pkt_ptr);

__bailout_1:
   __gpr_cmd_end_command(incoming_pkt_ptr, result);

   if (requires_response_handling)
   {
      apm_deallocate_cmd_hdlr_resources(apm_info_ptr, apm_info_ptr->curr_cmd_ctrl_ptr);
   }

   return result;
}

static ar_result_t apm_mem_shared_memory_map_regions_cmd_handler_v2(apm_t        *apm_info_ptr,
                                                                    spf_msg_t    *msg_ptr,
                                                                    gpr_packet_t *pkt_ptr,
                                                                    POSAL_HEAP_ID heap_id)
{

   ar_result_t result          = AR_EOK;
   void       *payload_ptr     = NULL;
   uint32_t    mem_map_client  = apm_info_ptr->memory_map_client;
   uint32_t    heap_mngr_type  = APM_MEMORY_MAP_LOANED_MEMORY_HEAP_MNGR_TYPE_DEFAULT;
   uint32_t    memory_map_type = APM_MEMORY_MAP_MEMORY_ADDRESS_TYPE_PA_OR_VA;
   POSAL_MEMORYPOOLTYPE             mem_pool;
   apm_shared_map_region_payload_t *region_ptr;

   bool_t is_virtual, is_cached, is_offset_map, is_mem_loaned;

   payload_ptr = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);

   apm_cmd_shared_mem_map_regions_v2_t *mem_map_cmd_v2_ptr = (apm_cmd_shared_mem_map_regions_v2_t *)payload_ptr;

   // check the processor domain id
   uint32_t host_domain_id = 0;
   __gpr_cmd_get_host_domain_id(&host_domain_id);
   uint32_t dest_proc_domain_id = mem_map_cmd_v2_ptr->proc_domain_id;
   if (!((host_domain_id == dest_proc_domain_id) || (APM_PROP_ID_DONT_CARE == dest_proc_domain_id) ||
         (0 == dest_proc_domain_id)))
   {
      // forward the command to peer processor and return

      // total gpr packet payload size
      uint32_t gpr_payload_size = sizeof(apm_cmd_shared_mem_map_regions_v2_t) +
                                  (mem_map_cmd_v2_ptr->num_regions * sizeof(apm_shared_map_region_payload_t));

      AR_MSG(DBG_LOW_PRIO,
             "apm_shmem_util_v2: APM in proc domain 0x%X successfully forwared APM_CMD_SHARED_MEM_MAP_REGIONS_V2 "
             "unique_shm_id %lu to dest proc domain 0x%X",
             host_domain_id,
             mem_map_cmd_v2_ptr->unique_shm_id,
             dest_proc_domain_id);

      result = apm_forward_gpr_cmd_to_dest_proc_domain(apm_info_ptr,
                                                       msg_ptr,
                                                       pkt_ptr,
                                                       payload_ptr,
                                                       dest_proc_domain_id,
                                                       gpr_payload_size,
                                                       REQUIRES_RESP_HANDLING);
      return result;
   }

   /* Else handles the command locally */

   switch (mem_map_cmd_v2_ptr->mem_pool_id)
   {
      case APM_MEMORY_MAP_SHMEM8_4K_POOL:
      {
         mem_pool = POSAL_MEMORYMAP_SHMEM8_4K_POOL;
         break;
      }
      default:
      {
         result = AR_EBADPARAM;
         AR_MSG(DBG_ERROR_PRIO, "apm_shmem_util_v2: Received Invalid PoolID: %d", mem_map_cmd_v2_ptr->mem_pool_id);

         goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_1;
      }
   }

   AR_MSG(DBG_HIGH_PRIO,
          "apm_shmem_util_v2: mem_pool_id 0x%hx, num_regions 0x%hx, property flag 0x%lx",
          mem_map_cmd_v2_ptr->mem_pool_id,
          mem_map_cmd_v2_ptr->num_regions,
          mem_map_cmd_v2_ptr->property_flag);

   is_virtual =
      (mem_map_cmd_v2_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_IS_VIRTUAL) >> APM_MEMORY_MAP_SHIFT_IS_VIRTUAL;
   is_cached =
      !((mem_map_cmd_v2_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_IS_UNCACHED) >> APM_MEMORY_MAP_SHIFT_IS_UNCACHED);
   is_offset_map = (mem_map_cmd_v2_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_IS_OFFSET_MODE) >>
                   APM_MEMORY_MAP_SHIFT_IS_OFFSET_MODE;

   is_mem_loaned =
      (mem_map_cmd_v2_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_IS_MEM_LOANED) >> APM_MEMORY_MAP_SHIFT_IS_MEM_LOANED;

   heap_mngr_type = (mem_map_cmd_v2_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_LOANED_MEMORY_HEAP_MNGR_TYPE) >>
                    APM_MEMORY_MAP_SHIFT_LOANED_MEMORY_HEAP_MNGR_TYPE;

   memory_map_type = (mem_map_cmd_v2_ptr->property_flag & APM_MEMORY_MAP_BIT_MASK_MEMORY_ADDRESS_TYPE) >>
                     APM_MEMORY_MAP_SHIFT_MEMORY_ADDRESS_TYPE;

   AR_MSG(DBG_HIGH_PRIO,
          "apm_shmem_util_v2: property flag 0x%lx,"
          " is_pa_or_va  %lu (pa:va - 0:1), is_cached %lu,"
          "is_offset_map %lu, is_mem_loaned %lu, "
          "heap_mgr_type %lu, memory_map_type %lu ",
          mem_map_cmd_v2_ptr->property_flag,
          is_virtual,
          !(is_cached),
          is_offset_map,
          is_mem_loaned,
          heap_mngr_type,
          memory_map_type);

   // Invalid configuration if offset mode is not set and memory is loaned its not a valid configuration
   if (is_mem_loaned && !is_offset_map)
   {
      result = AR_EBADPARAM;
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util_v2: Received invalid is_mem_loaned: %lu is_offset_map: %lu",
             is_mem_loaned,
             is_offset_map);
      goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_1;
   }

   // allocate posal_memorymap_shm_region_t, why not direct cast? because want to be free from
   // api data structure change or any compiler pack
   posal_memorymap_shm_region_t *phy_regions;
   if (NULL == (phy_regions = (posal_memorymap_shm_region_t *)posal_memory_malloc(sizeof(posal_memorymap_shm_region_t) *
                                                                                     mem_map_cmd_v2_ptr->num_regions,
                                                                                  heap_id)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util_v2: memory map region allocation failed, num_regions = %lu",
             mem_map_cmd_v2_ptr->num_regions);

      result = AR_ENOMEMORY;
      goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_1;
   }

   region_ptr = (apm_shared_map_region_payload_t *)(mem_map_cmd_v2_ptr + 1);
   for (int i = 0; i < mem_map_cmd_v2_ptr->num_regions; ++i)
   {
      phy_regions[i].shm_addr_lsw = (region_ptr + i)->shm_addr_lsw;
      phy_regions[i].shm_addr_msw = (region_ptr + i)->shm_addr_msw;
      phy_regions[i].mem_size     = (region_ptr + i)->mem_size_bytes;

      AR_MSG(DBG_HIGH_PRIO,
             "apm_shmem_util_v2: region %d, shm_addr_lsw 0x%lx, shm_addr_msw 0x%lx, mem size %ld",
             i,
             phy_regions[i].shm_addr_lsw,
             phy_regions[i].shm_addr_msw,
             phy_regions[i].mem_size);
   }

   posal_mem_map_v2_input_args_t in_args = { 0 };
   in_args.unique_shmem_id_24bit         = mem_map_cmd_v2_ptr->unique_shm_id;
   in_args.client_token                  = mem_map_client;
   in_args.shm_mem_reg_ptr               = phy_regions;
   in_args.num_shm_reg                   = mem_map_cmd_v2_ptr->num_regions;
   in_args.is_cached                     = is_cached;
   in_args.is_offset_map                 = is_offset_map;
   in_args.pool_id                       = mem_pool;
   in_args.heap_id                       = heap_id;

   uint32_t mem_map_handle = 0; // its a dummy variable not really used in context of v2 apis.
   if (0 == is_virtual)
   {
      // physical mapping
      if (AR_DID_FAIL(result = posal_memorymap_shm_mem_map_v2(&in_args, &mem_map_handle)))
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_shmem_util_v2: Failed to map the physical memory, error code is 0x%x", result);
         goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_2;
      }
   }
   else if (1 == is_virtual)
   {
      // virtual mapping
      if (AR_DID_FAIL(result = posal_memorymap_virtaddr_mem_map_v2(&in_args, &mem_map_handle)))
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_shmem_util_v2: Failed to map the virual memory, error code is 0x%x", result);
         goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_2;
      }
   }
   else
   {
      result = AR_EBADPARAM;
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util_v2: invalid property flag received in the payload, error code is 0x%x",
             result);
      goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_2;
   }

   AR_MSG(DBG_HIGH_PRIO,
          "apm_shmem_util_v2: memory map success, mem_pool_id 0x%hx, num_regions 0x%hx, property flag 0x%lx, handle "
          "0x%lx",
          mem_map_cmd_v2_ptr->mem_pool_id,
          mem_map_cmd_v2_ptr->num_regions,
          mem_map_cmd_v2_ptr->property_flag,
          mem_map_handle);

   // if its loaned memory create a heap manage it for the MDF usecases.
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
             mem_map_cmd_v2_ptr->property_flag,
             is_virtual,
             is_cached,
             is_offset_map,
             is_mem_loaned,
             heap_mngr_type,
             memory_map_type);

      if (ext_utils_ptr->offload_vtbl_ptr &&
          ext_utils_ptr->offload_vtbl_ptr->apm_offload_memorymap_register_loaned_memory_fptr)
      {
         if (AR_EOK !=
             (result = ext_utils_ptr->offload_vtbl_ptr
                          ->apm_offload_memorymap_register_loaned_memory_fptr(mem_map_client,
                                                                              mem_map_cmd_v2_ptr->unique_shm_id,
                                                                              phy_regions[0].mem_size,
                                                                              heap_mgr_type_i)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_shem_utils_v2: Failed to register Loaned memory, Handle 0x%lx of size %lu",
                   mem_map_handle,
                   phy_regions[0].mem_size);

            goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_2;
         }
      }
      else /** Offload mem map operations are not supported */
      {
         result = AR_EUNSUPPORTED;

         AR_MSG(DBG_ERROR_PRIO, "apm_shem_utils_v2: Offload memory map operation is not supported");

         goto _apm_mem_shared_memory_map_regions_cmd_handler_bail_2;
      }
   }

_apm_mem_shared_memory_map_regions_cmd_handler_bail_2:
   posal_memory_free(phy_regions);
_apm_mem_shared_memory_map_regions_cmd_handler_bail_1:
   __gpr_cmd_end_command(pkt_ptr, result);
   return result;
}

static ar_result_t apm_mem_shared_memory_unmap_regions_cmd_handler_v2(apm_t        *apm_info_ptr,
                                                                      spf_msg_t    *msg_ptr,
                                                                      gpr_packet_t *pkt_ptr)
{
   ar_result_t result = AR_EOK;
   void       *payload_ptr;

   apm_ext_utils_t *ext_utils_ptr;
   uint32_t         unique_shm_id;
   uint32_t         mem_map_client = apm_info_ptr->memory_map_client;

   payload_ptr = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);

   apm_cmd_shared_mem_unmap_regions_v2_t *unmap_cmd_ptr = (apm_cmd_shared_mem_unmap_regions_v2_t *)payload_ptr;

   // check the processor domain id
   uint32_t host_domain_id = 0;
   __gpr_cmd_get_host_domain_id(&host_domain_id);
   uint32_t dest_proc_domain_id = unmap_cmd_ptr->proc_domain_id;
   if (!((host_domain_id == dest_proc_domain_id) || (APM_PROP_ID_DONT_CARE == dest_proc_domain_id) ||
         (0 == dest_proc_domain_id)))
   {
      // forward the command to peer processor and return

      // total gpr packet payload size
      uint32_t gpr_payload_size = sizeof(apm_cmd_shared_mem_unmap_regions_v2_t);

      AR_MSG(DBG_LOW_PRIO,
             "apm_shmem_util_v2: APM in proc domain 0x%X forwarding APM_CMD_SHARED_MEM_UNMAP_REGIONS_V2 "
             "unique_shm_id 0x%lx to dest proc domain 0x%X",
             host_domain_id,
             unmap_cmd_ptr->unique_shm_id,
             dest_proc_domain_id);

      result = apm_forward_gpr_cmd_to_dest_proc_domain(apm_info_ptr,
                                                       msg_ptr,
                                                       pkt_ptr,
                                                       payload_ptr,
                                                       dest_proc_domain_id,
                                                       gpr_payload_size,
                                                       REQUIRES_RESP_HANDLING);
      return result;
   }

   /* Else handles the command locally */

   unique_shm_id = unmap_cmd_ptr->unique_shm_id;

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   AR_MSG(DBG_HIGH_PRIO, "apm_shmem_util_v2: Unmap v2 unique_shm_id: 0x%lx", unique_shm_id);

   if (0 == unique_shm_id)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util_v2: null memory map handle received in the unmap cmd payload, client token %lu "
             "unique_shm_id: 0x%lx",
             mem_map_client,
             unique_shm_id);
      result = AR_EBADPARAM;
      goto _apm_mem_shared_memory_un_map_regions_cmd_handler_bail_1;
   }

   // todo: introduce v2 unamp to pass shm id ?
   if (AR_DID_FAIL(result = posal_memorymap_shm_mem_unmap(mem_map_client, unique_shm_id)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util_v2: Failed to unmap the physical memory - bailing out, error code is 0x%x, client token "
             "%lu, "
             "memmap handle "
             "0x%lx",
             result,
             mem_map_client,
             unique_shm_id);
      goto _apm_mem_shared_memory_un_map_regions_cmd_handler_bail_1;
   }

   if (ext_utils_ptr->offload_vtbl_ptr &&
       ext_utils_ptr->offload_vtbl_ptr->apm_offload_memorymap_check_deregister_loaned_memory_fptr)
   {
      // deregister here. have to search if the handle was registered and if so, deregister it
      if (AR_EOK !=
          (result = ext_utils_ptr->offload_vtbl_ptr->apm_offload_memorymap_check_deregister_loaned_memory_fptr(
              unique_shm_id)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_shmem_util_v2: Failed to deregister loaned memory from offload drv unique_shm_id:0x%lx",
                unique_shm_id);
         goto _apm_mem_shared_memory_un_map_regions_cmd_handler_bail_1;
      }
   }

_apm_mem_shared_memory_un_map_regions_cmd_handler_bail_1:
   __gpr_cmd_end_command(pkt_ptr, result);
   return result;
}

static ar_result_t apm_mem_shared_memory_reg_access_info_handler(apm_t        *apm_info_ptr,
                                                                 spf_msg_t    *msg_ptr,
                                                                 gpr_packet_t *pkt_ptr)
{
   ar_result_t result = AR_EOK;
   void       *payload_ptr;

   apm_ext_utils_t *ext_utils_ptr;
   uint32_t         unique_shm_id;
   uint32_t         mem_map_client    = apm_info_ptr->memory_map_client;
   payload_ptr                        = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);

   apm_cmd_shared_mem_region_access_info_t *reg_info_cmd_ptr = (apm_cmd_shared_mem_region_access_info_t *)payload_ptr;

   // check the processor domain id
   uint32_t host_domain_id = 0;
   __gpr_cmd_get_host_domain_id(&host_domain_id);
   uint32_t dest_proc_domain_id = reg_info_cmd_ptr->proc_domain_id;
   if (!((host_domain_id == dest_proc_domain_id) || (APM_PROP_ID_DONT_CARE == dest_proc_domain_id) ||
         (0 == dest_proc_domain_id)))
   {
      // forward the command to peer processor and return

      // total gpr packet payload size
      uint32_t gpr_payload_size = sizeof(apm_cmd_shared_mem_region_access_info_t) +
                                  (sizeof(uint32_t) * reg_info_cmd_ptr->num_peer_proc_domain_ids);

      AR_MSG(DBG_LOW_PRIO,
             "apm_shmem_util_v2: APM in proc domain 0x%X forwarding APM_CMD_SHARED_MEM_REGION_ACCESS_INFO "
             "unique_shm_id: 0x%lx to dest proc domain 0x%X",
             host_domain_id,
             reg_info_cmd_ptr->unique_shm_id,
             dest_proc_domain_id);

      result = apm_forward_gpr_cmd_to_dest_proc_domain(apm_info_ptr,
                                                       msg_ptr,
                                                       pkt_ptr,
                                                       payload_ptr,
                                                       dest_proc_domain_id,
                                                       gpr_payload_size,
                                                       REQUIRES_RESP_HANDLING);
      return result;
   }

   /* Else handles the command locally */
   unique_shm_id = reg_info_cmd_ptr->unique_shm_id;

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   AR_MSG(DBG_HIGH_PRIO,
          "apm_shmem_util_v2: Peer access info v2 received for unique_shm_id: 0x%lx, num_peer_proc_domain_ids: %lu",
          unique_shm_id,
          reg_info_cmd_ptr->num_peer_proc_domain_ids);

   if ((0 == unique_shm_id))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_shmem_util_v2: null Invalid info received client token %lu unique_shm_id: 0x%lx",
             mem_map_client,
             unique_shm_id);
      result = AR_EBADPARAM;
      goto _apm_shm_peer_access_info_cmd_err_bail_1;
   }

   if (ext_utils_ptr->offload_vtbl_ptr &&
       ext_utils_ptr->offload_vtbl_ptr->apm_offload_memorymap_update_peer_domains_access_info_fptr)
   {
      // deregister here. have to search if the handle was registered and if so, deregister it
      if (AR_EOK !=
          (result =
              ext_utils_ptr->offload_vtbl_ptr
                 ->apm_offload_memorymap_update_peer_domains_access_info_fptr(unique_shm_id,
                                                                              reg_info_cmd_ptr
                                                                                 ->num_peer_proc_domain_ids,
                                                                              reg_info_cmd_ptr->peer_proc_domain_list)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_shmem_util_v2: Failed to set peer domains access info to offload driver unique_shm_id: 0x%lx ",
                unique_shm_id);
         goto _apm_shm_peer_access_info_cmd_err_bail_1;
      }
   }

_apm_shm_peer_access_info_cmd_err_bail_1:
   __gpr_cmd_end_command(pkt_ptr, result);
   return result;
}

ar_result_t apm_shmem_cmd_handler_v2(apm_t *apm_info_ptr, spf_msg_t *msg_ptr)
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
      case APM_CMD_SHARED_MEM_MAP_REGIONS_V2:
      {
         result = apm_mem_shared_memory_map_regions_cmd_handler_v2(apm_info_ptr,
                                                                   msg_ptr,
                                                                   gpr_pkt_ptr,
                                                                   APM_INTERNAL_STATIC_HEAP_ID);
         break;
      }
      case APM_CMD_SHARED_MEM_UNMAP_REGIONS_V2:
      {
         result = apm_mem_shared_memory_unmap_regions_cmd_handler_v2(apm_info_ptr, msg_ptr, gpr_pkt_ptr);
         break;
      }
      case APM_CMD_SHARED_MEM_REGION_ACCESS_INFO:
      {
         result = apm_mem_shared_memory_reg_access_info_handler(apm_info_ptr, msg_ptr, gpr_pkt_ptr);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_shmem_cmd_handler(): Unsupported sh mem v2 cmd opcode: 0x%lX", cmd_opcode);
         result = AR_EUNSUPPORTED;
         break;
      }
   }

   return result;
}