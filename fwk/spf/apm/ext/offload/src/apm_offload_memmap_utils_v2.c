/**
 * \file apm_offload_memmap_utils_v2.c
 *
 * \brief
 *     This file contains utilities for memory mapping and unmapping of
 *     shared memory, in the Multi-DSP-Framwork (MDF).
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
   Includes
========================================================================== */
#include "apm_offload_memmap_utils.h"
#include "apm_graph_properties.h"
#include "gpr_api_inline.h"
#include "posal.h"
#include "apm_offload_memmap_handler.h"
#include "apm_offload_memmap_utils.h"
#include "apm_memmap_api.h"
#include "apm_cmd_utils.h"
#include "apm_ext_cmn.h"
#include "apm.h"

#include "apm_i.h"

/*Uncomment the following line to enable debug messages*/
#define APM_OFFLOAD_MEM_MAP_DBG

/****************************************************************************
 * Macro Declarations
 ****************************************************************************/

#define APM_OFFLOAD_MEM_ALIGNMENT_BYTES 128 // varies per product
#define MAX_NUM_PEER_PROC_DOMAINS 5

/** Memset to FFFFFFFF*/
#define APM_OFFLOAD_MEMSET(ptr, size)                                                                                  \
   do                                                                                                                  \
   {                                                                                                                   \
      memset(ptr, APM_OFFLOAD_INVALID_VAL, size);                                                                      \
   } while (0)

/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/

typedef struct shm_region_info_t
{
   uint32_t unique_shm_id;
   /* shm id for this memory which is unique across DSPs.

      Important: mem map handle will also be same across DSPs and is same as "unique_shm_id"   */

   uint32_t heap_size;
   /*Denotes the size of the managed heap. This field is valid only if memory is loaned */

   uint32_t heap_start_addr_va;
   /*Denotes the start address of the managed heap. This field is valid only if memory is loaned */

   POSAL_HEAP_ID heap_id;
   /*Denotes the heap ID to be used for this memory access. This field is valid only if memory is loaned */

   bool_t is_loaned;
   /*Indicates if the shared memory is client loaned/client owned */

   uint32_t num_peer_proc_domains;
   /* Number of peer proc domains that have access/mapping to this shared memory region. */

   uint32_t peer_proc_domain_list[MAX_NUM_PEER_PROC_DOMAINS];
   /* List of peer proc domains that have access/mapping to this shared memory region. */

} shm_region_info_t;

typedef struct apm_offload_mem_mgr_v2_t
{
   // list of shm regions registered with offload manager.
   spf_list_node_t *shm_mem_list_ptr;

   /*Mutex to have a thread safe access to the domain_map*/
   posal_mutex_t map_mutex;
} apm_offload_mem_mgr_v2_t;

/* =======================================================================
   GLOBALS
========================================================================== */
apm_offload_mem_mgr_v2_t g_offload_mem_mgr_v2; // Global

/* =======================================================================
   Static Function Prototypes
========================================================================== */

static shm_region_info_t *apm_offload_allocate_shm_region_info(uint32_t unique_shm_id);

static shm_region_info_t *apm_offload_get_shm_reg_info_from_id(uint32_t unique_shm_id);

static ar_result_t apm_offload_free_shm_reg_info(shm_region_info_t *handle);

static bool_t apm_offload_check_peer_proc_domains_access_list(shm_region_info_t *shm_info_ptr,
                                                              uint32_t           peer_proc_domain_id);

/* =======================================================================
   Memory Map Function Implementations
========================================================================== */

/* Registers the loaned shared memory region info in the offload memory manager driver. Mainly creates a heap for the
 * loaned memory and caches the heap ID for offloading use case purposes.*/
ar_result_t apm_offload_memorymap_register_loaned_memory(uint32_t     mem_map_client,
                                                         uint32_t     unique_shm_id,
                                                         uint32_t     mem_size,
                                                         posal_heap_t heap_mgr_type)
{
   ar_result_t   result         = AR_EOK;
   uint64_t      virt_addr      = 0;
   POSAL_HEAP_ID region_heap_id = APM_INTERNAL_STATIC_HEAP_ID;

   posal_mutex_lock(g_offload_mem_mgr_v2.map_mutex);

   shm_region_info_t *shm_info_ptr = apm_offload_get_shm_reg_info_from_id(unique_shm_id);
   if (NULL != shm_info_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "Offload: Warning! Already registered loaned sh mem region with unique_shm_id: 0x%lx",
             unique_shm_id);
      posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);
      return AR_EOK;
   }

   shm_info_ptr = apm_offload_allocate_shm_region_info(unique_shm_id);
   posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);

   if (NULL == shm_info_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_offload_memorymap_register_loaned_memory: All available Slots for loaned mem unique_shm_id 0x%lx are "
             "full. cannot register",
             unique_shm_id);
      return AR_ENOMEMORY;
   }

   // now query the VA, create the heap mgr
   result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(mem_map_client,
                                                                unique_shm_id, // with v2 mem map handle is considered
                                                                               // same as unique shm id
                                                                0 /*lsw offset*/,
                                                                0 /*msw offset*/,
                                                                mem_size,
                                                                FALSE,
                                                                &virt_addr);
   if ((AR_DID_FAIL(result)) || (0 == virt_addr))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_offload_memorymap_register_loaned_memory: Failed to get virtual addr for 0 offset mem map handle "
             "0x%lx",
             unique_shm_id);
      apm_offload_free_shm_reg_info(shm_info_ptr);
      return result;
   }
#ifdef APM_OFFLOAD_MEM_MAP_DBG
   AR_MSG(DBG_HIGH_PRIO, "apm_offload_memorymap_register_loaned_memory: VA Query returned va = 0x%lx", virt_addr);
#endif

   if (AR_DID_FAIL(result = posal_memory_heapmgr_create_v2(&region_heap_id,
                                                           (void *)virt_addr,
                                                           mem_size,
                                                           TRUE,
                                                           heap_mgr_type,
                                                           NULL,
                                                           NULL,
                                                           0)))
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_offload_memorymap_register_loaned_memory: Failed to create heap manager");
      apm_offload_free_shm_reg_info(shm_info_ptr);
      return result;
   }

   // cache the loaned memory information in the global db.
   posal_mutex_lock(g_offload_mem_mgr_v2.map_mutex);
   shm_info_ptr->heap_size          = mem_size;
   shm_info_ptr->heap_start_addr_va = (uint32_t)virt_addr;
   shm_info_ptr->heap_id            = region_heap_id;
   shm_info_ptr->is_loaned          = TRUE;
   posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);

   AR_MSG(DBG_HIGH_PRIO,
          "apm_offload_memorymap_register_loaned_memory: Successful for unique_shm_id = 0x%lx heap_id = 0x%lx, "
          "size 0x%lu",
          unique_shm_id,
          region_heap_id,
          mem_size);

   return result;
}

/* Deregisters info related to the loaned shared memory. And also destroy the heap created for the loaned region. If the
 * shared memory region was not mapped as not loaned, this function return AR_EOK. */
ar_result_t apm_offload_memorymap_check_deregister_loaned_memory(uint32_t unique_shm_id)
{
   ar_result_t   result         = AR_EOK;

   posal_mutex_lock(g_offload_mem_mgr_v2.map_mutex);

   shm_region_info_t *shm_info_ptr = apm_offload_get_shm_reg_info_from_id(unique_shm_id);
   if (NULL == shm_info_ptr)
   {
#ifdef APM_OFFLOAD_MEM_MAP_DBG
      AR_MSG(DBG_HIGH_PRIO,
             "Offload: Couldn't find registered loaned sh mem region with unique_shm_id: 0x%lx",
             unique_shm_id);
#endif
      posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);
      return AR_EOK;
   }

#ifdef APM_OFFLOAD_MEM_MAP_DBG
   AR_MSG(DBG_LOW_PRIO, "Offload: De-registering unique_shm_id: 0x%lx", unique_shm_id);
#endif

   // delete the heapmgr
   AR_MSG(DBG_LOW_PRIO,
          "Offload: heap_id: %lu is_destroying ? %lu for unique_shm_id: 0x%lx",
          shm_info_ptr->is_loaned,
          shm_info_ptr->heap_id,
          unique_shm_id);

   if (shm_info_ptr->is_loaned)
   {
      posal_memory_heapmgr_destroy(shm_info_ptr->heap_id);
   }

   apm_offload_free_shm_reg_info(shm_info_ptr);

   posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);

   AR_MSG(DBG_LOW_PRIO,
          "Offload: Successfully de-registered loaned shm region with unique_shm_id: 0x%lx",
          unique_shm_id);

   return result;
}

/* Sets the list of peer proc domains that have access to the given loaned shared memory region. If the shared memory
 * region was not mapped as not loaned, this function return AR_EOK. */
ar_result_t apm_offload_memorymap_update_peer_domains_access_info(uint32_t unique_shm_id,
                                                                  uint32_t num_peer_proc_domains,
                                                                  uint32_t peer_proc_domain_list[])
{
   ar_result_t   result         = AR_EOK;

   // unsupported number of proc domains.
   if (num_peer_proc_domains > MAX_NUM_PEER_PROC_DOMAINS)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "peer_access_info: Unexpected unique_shm_id: 0x%lx num_peer_proc_domains: 0x%lu > %lu(MAX) ",
             unique_shm_id,
             num_peer_proc_domains,
             MAX_NUM_PEER_PROC_DOMAINS);
      return AR_EFAILED;
   }
   // for persistent calib shared memory is not loaned but can be mapped across mulitple process domains
   // hence registering the proc domain list info event though memory is not loaned.
   posal_mutex_lock(g_offload_mem_mgr_v2.map_mutex);
   shm_region_info_t *shm_info_ptr = apm_offload_get_shm_reg_info_from_id(unique_shm_id);
   bool_t             is_new_slot  = FALSE;
   if (NULL == shm_info_ptr)
   {
      shm_info_ptr = apm_offload_allocate_shm_region_info(unique_shm_id);
      if (NULL == shm_info_ptr)
      {
         posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);
         AR_MSG(DBG_ERROR_PRIO,
                "peer_access_info: All available Slots for are full unique_shm_id 0x%lx cannot be registered",
                unique_shm_id);
         return AR_ENOMEMORY;
      }
      is_new_slot = TRUE;
   }

   AR_MSG(DBG_LOW_PRIO,
          "peer_access_info: is new slot ? %lu for unique_shm_id: 0x%lx num_peer_proc_domains: 0x%lu",
          is_new_slot,
          unique_shm_id,
          num_peer_proc_domains);

   // append the peer proc domain list in the db
   shm_info_ptr->num_peer_proc_domains = num_peer_proc_domains;

   for (uint32_t i = 0; i < num_peer_proc_domains; i++)
   {
      shm_info_ptr->peer_proc_domain_list[i] = peer_proc_domain_list[i];

#ifdef APM_OFFLOAD_MEM_MAP_DBG
      AR_MSG(DBG_LOW_PRIO,
             "peer_access_info: Adding peer domain for unique_shm_id: 0x%lx i.e peer_proc_domain_list[%lu]: 0x%x",
             unique_shm_id,
             i,
             peer_proc_domain_list[i]);
#endif
   }
   posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);

   AR_MSG(DBG_HIGH_PRIO,
          "peer_access_info: Successfully registered peer proc domains for unique_shm_id: 0x%lx, "
          "num_peer_proc_domains: 0x%lu",
          unique_shm_id,
          num_peer_proc_domains);

   return result;
}

ar_result_t apm_offload_mem_basic_rsp_handler_v2(apm_t          *apm_info_ptr,
                                                 apm_cmd_ctrl_t *apm_cmd_ctrl_ptr,
                                                 gpr_packet_t   *rsp_gpr_pkt_ptr)
{
   ar_result_t              result          = AR_EOK;
   gpr_packet_t            *cmd_gpr_pkt_ptr = NULL;
   gpr_ibasic_rsp_result_t *rsp_payload_ptr = NULL;

   rsp_payload_ptr = GPR_PKT_GET_PAYLOAD(gpr_ibasic_rsp_result_t, rsp_gpr_pkt_ptr);
   cmd_gpr_pkt_ptr = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /** We can get the basic response in two main cases:
   1. A response for the MDF Unmap command, or
   2. Perhaps a failure for the MDF MAP command */
   switch (rsp_payload_ptr->opcode)
   {
      case APM_CMD_SHARED_MEM_UNMAP_REGIONS_V2:
      {
         result = rsp_payload_ptr->status;

#ifdef APM_OFFLOAD_MEM_MAP_DBG
         // handle success and failure case
         if (AR_EOK == result)
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_offload_mem_basic_rsp_handler_v2: EOK result for Unmap from peer proc_domain: 0x%x",
                   cmd_gpr_pkt_ptr->src_domain_id);
         }
         else
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_offload_mem_basic_rsp_handler_v2: NOT EOK result for Unmap from peer proc_domain: 0x%x",
                   cmd_gpr_pkt_ptr->src_domain_id);
         }
#endif // APM_OFFLOAD_MEM_MAP_DBG
         break;
      }
      case APM_CMD_SHARED_MEM_MAP_REGIONS_V2:
      {
         // handle just the failure case.
         if (AR_EOK != rsp_payload_ptr->status)
         {
            result = rsp_payload_ptr->status;
#ifdef APM_OFFLOAD_MEM_MAP_DBG
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_offload_mem_basic_rsp_handler_v2: NOT EOK result for MAP from peer proc_domain: 0x%x",
                   cmd_gpr_pkt_ptr->src_domain_id);
#endif // APM_OFFLOAD_MEM_MAP_DBG
         }
         // else case should never really happen
         break;
      }
      case APM_CMD_SHARED_MEM_REGION_ACCESS_INFO:
      {
         // handle just the failure case.
         if (AR_EOK != rsp_payload_ptr->status)
         {
            result = rsp_payload_ptr->status;
#ifdef APM_OFFLOAD_MEM_MAP_DBG
            AR_MSG(DBG_HIGH_PRIO,
                   "APM: apm_offload_mem_basic_rsp_handler_v2: NOT EOK result for region Access info from peer "
                   "proc_domain: 0x%x",
                   cmd_gpr_pkt_ptr->src_domain_id);
#endif // APM_OFFLOAD_MEM_MAP_DBG
         }
         // else case should never really happen
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "APM: apm_offload_mem_basic_rsp_handler_v2: Unexpected response for cmd opcode 0x%lx",
                rsp_payload_ptr->opcode);
         break;
      }
   }

   AR_MSG(DBG_HIGH_PRIO,
          "apm_offload_mem_basic_rsp_handler_v2: APM proc domain 0x%X received basic response from proc domain 0x%X "
          "opcode 0x%lx result 0x%lx client_token 0x%lx",
          cmd_gpr_pkt_ptr->dst_domain_id,
          rsp_gpr_pkt_ptr->src_domain_id,
          rsp_payload_ptr->opcode,
          rsp_payload_ptr->status,
          cmd_gpr_pkt_ptr->token);

   __gpr_cmd_end_command(cmd_gpr_pkt_ptr, result); // forward the response to Apps client
   __gpr_cmd_free(rsp_gpr_pkt_ptr);
   apm_deallocate_cmd_hdlr_resources(apm_info_ptr, apm_cmd_ctrl_ptr);

   return result;
}

/* =======================================================================
   Memory MAP Utility Function Implementations
========================================================================== */

static shm_region_info_t *apm_offload_allocate_shm_region_info(uint32_t unique_shm_id)
{
   shm_region_info_t *new_ptr = posal_memory_malloc(sizeof(shm_region_info_t), APM_INTERNAL_STATIC_HEAP_ID);
   if (NULL == new_ptr)
   {
      return NULL;
   }

   APM_OFFLOAD_MEMSET(new_ptr, sizeof(shm_region_info_t));

   new_ptr->unique_shm_id = unique_shm_id;

   ar_result_t result = spf_list_insert_tail((spf_list_node_t **)&g_offload_mem_mgr_v2.shm_mem_list_ptr,
                                             new_ptr,
                                             APM_INTERNAL_STATIC_HEAP_ID,
                                             TRUE);
   if (AR_FAILED(result))
   {
      posal_memory_free(new_ptr);
      return NULL;
   }

   return new_ptr;
}

static shm_region_info_t *apm_offload_get_shm_reg_info_from_id(uint32_t unique_shm_id)
{
   for (spf_list_node_t *cur_node_ptr = g_offload_mem_mgr_v2.shm_mem_list_ptr; (NULL != cur_node_ptr);
        LIST_ADVANCE(cur_node_ptr))
   {
      shm_region_info_t *shm_info_ptr = (shm_region_info_t *)cur_node_ptr->obj_ptr;

      if (unique_shm_id == shm_info_ptr->unique_shm_id)
      {
#ifdef APM_OFFLOAD_MEM_MAP_DBG
         AR_MSG(DBG_HIGH_PRIO,
                "Offload: Found registered sh mem region with unique_shm_id: 0x%lx, node_ptr: 0x%lx",
                unique_shm_id,
                cur_node_ptr);
#endif
         return shm_info_ptr;
      }
   }

#ifdef APM_OFFLOAD_MEM_MAP_DBG
   AR_MSG(DBG_HIGH_PRIO,
          "Offload: Couldn't find registered loaned sh mem region with unique_shm_id: 0x%lx",
          unique_shm_id);
#endif

   return NULL;
}

static bool_t apm_offload_check_peer_proc_domains_access_list(shm_region_info_t *shm_info_ptr,
                                                              uint32_t           peer_proc_domain_id)
{
   uint32_t unique_shm_id = shm_info_ptr->unique_shm_id;
   if ((APM_OFFLOAD_INVALID_VAL == unique_shm_id) || (0 == unique_shm_id))
   {
#ifdef APM_OFFLOAD_MEM_MAP_DBG
      AR_MSG(DBG_ERROR_PRIO, "Skip the search. Invalid unique_shm_id 0x%lx.", unique_shm_id);
#endif
      return FALSE;
   }

   for (uint32_t i = 0; i < shm_info_ptr->num_peer_proc_domains; i++)
   {
      if (peer_proc_domain_id == shm_info_ptr->peer_proc_domain_list[i])
      {
#ifdef APM_OFFLOAD_MEM_MAP_DBG
         AR_MSG(DBG_HIGH_PRIO,
                "Offload: Found sh mem region with unique_shm_id: 0x%lx, idx: %lu mapped to the peer_proc_domain_id: "
                "0x%x",
                shm_info_ptr->unique_shm_id,
                i,
                peer_proc_domain_id);
#endif

         return TRUE;
      }
   }

#ifdef APM_OFFLOAD_MEM_MAP_DBG
   AR_MSG(DBG_HIGH_PRIO,
          "Offload: Couldn't find registered sh mem region i.e mapped to peer_proc_domain_id: 0x%lx",
          peer_proc_domain_id);
#endif

   return FALSE;
}

static ar_result_t apm_offload_free_shm_reg_info(shm_region_info_t *shm_info_ptr)
{
   spf_list_find_delete_node(&g_offload_mem_mgr_v2.shm_mem_list_ptr, shm_info_ptr, TRUE);

   posal_memory_free(shm_info_ptr);
   return AR_EOK;
}

/* =======================================================================
   Memory Function Implementations
========================================================================== */
void *apm_offload_memory_malloc_v2(uint32_t                peer_proc_domain_id,
                                   uint32_t                req_size,
                                   apm_offload_ret_info_t *ret_info_ptr)
{
   void         *ret_ptr         = NULL;
   bool_t        found_mem       = FALSE;
   POSAL_HEAP_ID peer_heap_id    = APM_INTERNAL_STATIC_HEAP_ID;
   uint32_t      heap_start_addr = 0;

   // update the size to align the size to cache line
   req_size = req_size + (APM_OFFLOAD_MEM_ALIGNMENT_BYTES - req_size % APM_OFFLOAD_MEM_ALIGNMENT_BYTES);

   posal_mutex_lock(g_offload_mem_mgr_v2.map_mutex);

   for (spf_list_node_t *cur_node_ptr = g_offload_mem_mgr_v2.shm_mem_list_ptr; (NULL != cur_node_ptr);
        LIST_ADVANCE(cur_node_ptr))
   {
      shm_region_info_t *shm_info_ptr = (shm_region_info_t *)cur_node_ptr->obj_ptr;

      // cannot allocated from unloaned memory
      if (FALSE == shm_info_ptr->is_loaned)
      {
         continue;
      }

      if ((FALSE == apm_offload_check_peer_proc_domains_access_list(shm_info_ptr, peer_proc_domain_id)))
      {
         continue;
      }

      uint32_t unique_shm_id = shm_info_ptr->unique_shm_id;

      if (req_size > shm_info_ptr->heap_size)
      {
#ifdef APM_OFFLOAD_MEM_MAP_DBG
         AR_MSG(DBG_MED_PRIO,
                "apm_offload_memory_malloc_v2: Req Size: %lu is less than unique_shm_id=0x%lx has heap size %lu. "
                "Continuing....",
                req_size,
                unique_shm_id,
                shm_info_ptr->heap_size);
#endif
         continue;
      }

      peer_heap_id = shm_info_ptr->heap_id;
      if (NULL == (ret_ptr = posal_memory_aligned_malloc(req_size, APM_OFFLOAD_MEM_ALIGNMENT_BYTES, peer_heap_id)))
      {
#ifdef APM_OFFLOAD_MEM_MAP_DBG
         AR_MSG(DBG_MED_PRIO,
                "apm_offload_memory_malloc_v2: malloc of size: %lu, from unique_shm_id=0x%lx from heap id %lu "
                "failed. Continuing to next map",
                req_size,
                unique_shm_id,
                peer_heap_id);
#endif
         continue;
      }

      found_mem       = TRUE;
      heap_start_addr = shm_info_ptr->heap_start_addr_va;

      /*Fill the V2 ret info*/
      ret_info_ptr->is_handle_type_v2 = TRUE;
      ret_info_ptr->master_handle     = shm_info_ptr->unique_shm_id;
      ret_info_ptr->sat_handle = shm_info_ptr->unique_shm_id; // mem map handle is same as unique shm id with v2 apis.
      ret_info_ptr->offset     = (uint32_t)ret_ptr - heap_start_addr;
      ret_info_ptr->heap_id    = (uint32_t)peer_heap_id;
      ret_info_ptr->ret_ptr    = ret_ptr;
      break;
   }
   posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);

   if (!found_mem)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_offload_memory_malloc_v2: Unable to Malloc Memory of size %lu, for peer proc domain %lu",
             req_size,
             peer_proc_domain_id);
      return NULL;
   }
#ifdef APM_OFFLOAD_MEM_MAP_DBG
   AR_MSG(DBG_HIGH_PRIO,
          "apm_offload_memory_malloc_v2: Successfully allocated memory from heap %lu of size %lu, ret_ptr = "
          "0x%lx, ret info: mem_map_handle = 0x%lx, unique_shm_id = 0x%lx, offset = 0x%lx",
          peer_heap_id,
          req_size,
          ret_ptr,
          ret_info_ptr->sat_handle,
          ret_info_ptr->master_handle,
          ret_info_ptr->offset);
#endif
   return ret_ptr;
}

void apm_offload_memory_free_v2(apm_offload_ret_info_t *shmem_info_ptr)
{
   if ((shmem_info_ptr) && (shmem_info_ptr->ret_ptr))
   {
#ifdef APM_OFFLOAD_MEM_MAP_DBG
      AR_MSG(DBG_HIGH_PRIO,
             "apm_offload_memory_free_v2: Freeing memory ptr 0x%lx allocated from unique_shm_id: 0x%lx heap_id: 0x%lx",
             shmem_info_ptr->ret_ptr,
             shmem_info_ptr->master_handle,
             shmem_info_ptr->heap_id);
#endif
      posal_memory_aligned_free_v2(shmem_info_ptr->ret_ptr, shmem_info_ptr->heap_id);
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_offload_memory_free_v2: Freeing memory ptr, invalid arguments");
   }
}

/** This functions unmaps and destroys the heap associated with all the loaned shared memory regions that have been
 * registered. */
static ar_result_t apm_offload_memorymap_deregister_all_v2()
{
   ar_result_t result = AR_EOK;

   posal_mutex_lock(g_offload_mem_mgr_v2.map_mutex);

   for (spf_list_node_t *cur_node_ptr = g_offload_mem_mgr_v2.shm_mem_list_ptr; (NULL != cur_node_ptr);
        LIST_ADVANCE(cur_node_ptr))
   {
      shm_region_info_t *shm_info_ptr = (shm_region_info_t *)cur_node_ptr->obj_ptr;

      AR_MSG(DBG_HIGH_PRIO,
             "apm_offload_memorymap_deregister_all_v2: deregistering loaned shm unique_shm_id=0x%lx "
             "in the global table destroying the heap",
             shm_info_ptr->unique_shm_id);

      /// else we need to delete the heapmgr
      posal_memory_heapmgr_destroy(shm_info_ptr->heap_id);
   }

   // free all the nodes and the objects
   spf_list_delete_list_and_free_objs(&g_offload_mem_mgr_v2.shm_mem_list_ptr, TRUE);

   posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);

   return result;
}

ar_result_t apm_offload_mem_mgr_reset_v2(void)
{
   ar_result_t result = AR_EOK;
   AR_MSG(DBG_HIGH_PRIO, "Offload: apm_offload_mem_mgr_reset_v2 reset");
   result = apm_offload_memorymap_deregister_all_v2();
   return result;
}

/* Function to memset the global structute, create the mutex*/
ar_result_t apm_offload_global_mem_mgr_init_v2(POSAL_HEAP_ID heap_id)
{

   AR_MSG(DBG_HIGH_PRIO, "In APM Offload Global Mem Mgr Init V2");
   memset(&g_offload_mem_mgr_v2, 0, sizeof(apm_offload_mem_mgr_v2_t));
   posal_mutex_create(&g_offload_mem_mgr_v2.map_mutex, heap_id);

   return AR_EOK;
}

/* Function to memset the global structute, create the mutex*/
ar_result_t apm_offload_global_mem_mgr_deinit_v2()
{
   AR_MSG(DBG_HIGH_PRIO, "In APM Offload Global Mem Mgr Deinit V2");
   apm_offload_memorymap_deregister_all_v2();
   posal_mutex_destroy(&g_offload_mem_mgr_v2.map_mutex);
   memset(&g_offload_mem_mgr_v2, 0, sizeof(apm_offload_mem_mgr_v2_t));

   return AR_EOK;
}

uint32_t apm_offload_get_persistent_sat_handle_v2(uint32_t sat_domain_id, uint32_t master_handle)
{
   // for v2 handle is same for all dsps, check if the sat domain is in the access list for the current shm region
   posal_mutex_lock(g_offload_mem_mgr_v2.map_mutex);

   for (spf_list_node_t *cur_node_ptr = g_offload_mem_mgr_v2.shm_mem_list_ptr; (NULL != cur_node_ptr);
        LIST_ADVANCE(cur_node_ptr))
   {
      shm_region_info_t *shm_info_ptr = (shm_region_info_t *)cur_node_ptr->obj_ptr;

      // if memory is mapped with v2 api, mem map handle will be same as unqiue shm id set by the client.
      if (master_handle != shm_info_ptr->unique_shm_id)
      {
         continue;
      }

      for (uint32_t i = 0; i < shm_info_ptr->num_peer_proc_domains; i++)
      {
         if (sat_domain_id == shm_info_ptr->peer_proc_domain_list[i])
         {
            posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);
            AR_MSG(DBG_LOW_PRIO,
                   "Offload: Found mem map handle 0x%lx for the peer domain id 0x%lx",
                   master_handle,
                   sat_domain_id);

            // all DSPs have same handle hence returning the same
            return master_handle;
         }
      }

      posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);
      // satellite domain not present in the access info list
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: Could not find peer domain id %lu in access list of master handle 0x%lx",
             sat_domain_id,
             master_handle);

      return APM_OFFLOAD_INVALID_VAL;
   }

   posal_mutex_unlock(g_offload_mem_mgr_v2.map_mutex);

   // mem map handle not found return Error
   AR_MSG(DBG_ERROR_PRIO,
          "Offload: Could not find a registered shm mem map handle 0x%lx for peer domain id  0x%lx ",
          master_handle,
          sat_domain_id);

   return APM_OFFLOAD_INVALID_VAL;
}