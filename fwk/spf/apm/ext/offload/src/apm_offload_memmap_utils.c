/**
 * \file apm_offload_memmap_utils.c
 *
 * \brief
 *     This file contains utilities for memory mapping and unmapping of shared memory, in the Multi-DSP-Framwork (MDF).
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
   Includes
========================================================================== */
#include "apm_offload_memmap_utils.h"
#include "apm_graph_properties.h"
#include "gpr_api_inline.h"

#include "apm_i.h"

/*Uncomment the following line to enable debug messages*/
#define APM_OFFLOAD_MAP_UTILS_DBG
/****************************************************************************
 * Macro Declarations
 ****************************************************************************/

#define APM_OFFLOAD_MEM_ALIGNMENT_BYTES 128 // varies per product

#define MAX_MASTER_MAPS 5 // Can be tuned. Don't expect more than 1 or 2
// as per today's requirement
#define MAX_SATELLITE_DOMAINS 10 // ADSP,SDSP, MDSP, CDSP, APPS, APPS2 (one is master)

static const uint32_t satellite_domain_list[MAX_SATELLITE_DOMAINS] =
{
         APM_PROC_DOMAIN_ID_MDSP,
         APM_PROC_DOMAIN_ID_ADSP, /** alias APM_PROC_DOMAIN_ID_ADSP_0 */
         APM_PROC_DOMAIN_ID_ADSP_1,
         APM_PROC_DOMAIN_ID_ADSP_2,
         APM_PROC_DOMAIN_ID_APPS,
         APM_PROC_DOMAIN_ID_SDSP,
         APM_PROC_DOMAIN_ID_CDSP,
         APM_PROC_DOMAIN_ID_GDSP_0,
         APM_PROC_DOMAIN_ID_GDSP_1,
         APM_PROC_DOMAIN_ID_APPS_2
};

#define MAX_NUM_UNLOANED_MAPS 10

/** Memset to FFFFFFFF*/
#define APM_OFFLOAD_MEMSET(ptr, size)                                                                                  \
   do                                                                                                                  \
   {                                                                                                                   \
      memset(ptr, APM_OFFLOAD_INVALID_VAL, size);                                                                      \
   } while (0)

/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/
typedef struct master_info_t
{
   /* Memmap handle on the master's side*/
   uint32_t master_handle;
   /*Denotes the size of the managed heap */
   uint32_t heap_size;
   /*Denotes the start address of the managed heap*/
   uint32_t heap_start_addr_va;
   /*Denotes the heap ID to be used for this memory access */
   POSAL_HEAP_ID heap_id;
} master_info_t;

typedef struct sat_info_t
{
   /* Memmap handle on the satellite side*/
   uint32_t sat_handle;
   /*Index in the master book where you'll find this satellite's master mem_handle*/
   uint32_t master_book_idx;
} sat_info_t;

typedef struct unloaned_map_t
{
   uint32_t master_handle;
   uint32_t sat_handle;
} unloaned_map_t;

struct apm_offload_mem_mgr_t
{
   /*maintains the num_active_maps in each domain map*/
   master_info_t master_book[MAX_MASTER_MAPS];
   /* Book-keeping to store the master handle -sat handle maps for each domain */
   sat_info_t sat_book[MAX_SATELLITE_DOMAINS][MAX_MASTER_MAPS];
   /*For Client managed mem, we only store handles to be used for persistent calibs*/
   unloaned_map_t unloaned_book[MAX_SATELLITE_DOMAINS][MAX_NUM_UNLOANED_MAPS];
   /*Mutex to have a thread safe access to the domain_map*/
   posal_mutex_t map_mutex;
};
typedef struct apm_offload_mem_mgr_t apm_offload_mem_mgr_t;

/* =======================================================================
   GLOBALS
========================================================================== */
apm_offload_mem_mgr_t g_offload_mem_mgr; // Global

/* =======================================================================
   Static Function Prototypes
========================================================================== */
static uint32_t apm_offload_get_sat_domain_list_index(uint32_t sat_domain_id);

static uint32_t apm_offload_get_first_open_master_slot();

static uint32_t apm_offload_find_master_handle_idx(uint32_t master_handle);

static uint32_t apm_offload_get_first_open_sat_slot(sat_info_t *sat_book_ptr);

static uint32_t apm_offload_get_first_open_unloaned_slot(unloaned_map_t *unloaned_book_ptr);

static uint32_t apm_offload_find_unloaned_sat_handle_idx(unloaned_map_t *unloaned_book_ptr, uint32_t sat_handle);

static uint32_t apm_offload_find_unloaned_master_handle_idx(unloaned_map_t *unloaned_book_ptr, uint32_t master_handle);

static void apm_offload_unloaned_book_cleanup(uint32_t sat_domain_id);

static void apm_offload_sat_book_cleanup(uint32_t sat_domain_id);

/* =======================================================================
   Memory Map Function Implementations
========================================================================== */

/* Function to memset the global structute, create the mutex*/
ar_result_t apm_offload_global_mem_mgr_init(POSAL_HEAP_ID heap_id)
{
   AR_MSG(DBG_HIGH_PRIO, "In APM Offload Global Mem Mgr Init");
   APM_OFFLOAD_MEMSET(&g_offload_mem_mgr, sizeof(apm_offload_mem_mgr_t));
   posal_mutex_create(&g_offload_mem_mgr.map_mutex, heap_id);
   return AR_EOK;
}
// same master memhandle should not come again and again. If this API is called, all that validation should've been
// done by the memmap APIs if the same region is being mapped again. This will trust that the Master memhandle is unique
ar_result_t apm_offload_master_memorymap_register(uint32_t     mem_map_client,
                                                  uint32_t     master_handle,
                                                  uint32_t     mem_size,
                                                  posal_heap_t heap_mgr_type)
{
   ar_result_t   result         = AR_EOK;
   uint64_t      virt_addr      = 0;
   POSAL_HEAP_ID region_heap_id = APM_INTERNAL_STATIC_HEAP_ID;

   posal_mutex_lock(g_offload_mem_mgr.map_mutex);
   uint32_t idx = apm_offload_get_first_open_master_slot();
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);

   if (APM_OFFLOAD_INVALID_VAL == idx)
   {
      AR_MSG(DBG_ERROR_PRIO, "Offload: All available Slots for Loaned Master Handles are full. Cannot Register");
      return AR_ENOMEMORY;
   }

   // now query the VA, create the heap mgr
   result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(mem_map_client,
                                                                master_handle,
                                                                0 /*lsw offset*/,
                                                                0 /*msw offset*/,
                                                                mem_size,
                                                                FALSE,
                                                                &virt_addr);
   if ((AR_DID_FAIL(result)) || (0 == virt_addr))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: APM Loaned Master MemMap Register: Failed to get virtual addr for 0 offset Master "
             "Mem Handle = 0x%lx",
             master_handle);
      return result;
   }
#ifdef APM_OFFLOAD_MAP_UTILS_DBG
   AR_MSG(DBG_HIGH_PRIO, "Offload: APM Loaned MemMap Register: VA Query returned VA= 0x%lx", virt_addr);
#endif // APM_OFFLOAD_MAP_UTILS_DBG

   if (AR_DID_FAIL(result = posal_memory_heapmgr_create_v2(&region_heap_id,
                                                           (void *)virt_addr,
                                                           mem_size,
                                                           TRUE,
                                                           heap_mgr_type,
                                                           NULL,
                                                           NULL,
                                                           NULL)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Offload: APM Loaned MemMap Register: Failed to create heap manager");
      return result;
   }

   // populate the master book
   posal_mutex_lock(g_offload_mem_mgr.map_mutex);
   g_offload_mem_mgr.master_book[idx].master_handle      = master_handle;
   g_offload_mem_mgr.master_book[idx].heap_size          = mem_size;
   g_offload_mem_mgr.master_book[idx].heap_start_addr_va = (uint32_t)virt_addr;
   g_offload_mem_mgr.master_book[idx].heap_id            = region_heap_id;
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);

   AR_MSG(DBG_HIGH_PRIO,
          "Offload: apm_offload_master_memorymap_register: Successful for Master Mem handle = 0x%lx "
          "heap_id = %d, mem_size 0x%lu",
          master_handle,
          region_heap_id,
          mem_size);

   return result;
}

// same sat memhandle should not come again and again. If this API is called, all that validation should've been
// done by the memmap APIs if the same region is being mapped again. This will trust that the Sat memhandle is unique
// add it to the first open spot
ar_result_t apm_offload_satellite_memorymap_register(uint32_t master_handle,
                                                     uint32_t sat_handle,
                                                     uint32_t sat_domain_id)
{
   ar_result_t result = AR_EOK;
   uint32_t    master_idx, sat_domain_book_index = APM_OFFLOAD_INVALID_VAL, sat_idx = APM_OFFLOAD_INVALID_VAL;
   sat_info_t *sat_book_ptr = NULL;
   // first find the master handle
   posal_mutex_lock(g_offload_mem_mgr.map_mutex);

   master_idx = apm_offload_find_master_handle_idx(master_handle);

   if (APM_OFFLOAD_INVALID_VAL == master_idx)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: Master Handle 0x%lx not found in the book to during satellite mapping. Failing",
             master_handle);
      posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
      return AR_EFAILED;
   }

   sat_domain_book_index = apm_offload_get_sat_domain_list_index(sat_domain_id);
   if (APM_OFFLOAD_INVALID_VAL == sat_domain_book_index)
   {
      AR_MSG(DBG_ERROR_PRIO, "Offload: Unsupported Satellite domain ID %lu", sat_domain_id);
      posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
      return AR_EFAILED;
   }

   sat_book_ptr = &(g_offload_mem_mgr.sat_book[sat_domain_book_index][0]);
   // we have to find an open slot here
   sat_idx = apm_offload_get_first_open_sat_slot(sat_book_ptr);

   if (APM_OFFLOAD_INVALID_VAL == sat_idx)
   {
      // This cannot happen; if this were to be the case, master handles should also have overflown
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: All available Slots for Satellite Handles are full in Sat Domain %d. Cannot Register",
             sat_domain_id);
      posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
      return AR_EFAILED;
   }
   // Let's fill the details
   sat_book_ptr[sat_idx].sat_handle      = sat_handle;
   sat_book_ptr[sat_idx].master_book_idx = master_idx;
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);

   AR_MSG(DBG_HIGH_PRIO,
          "Offload: Registered Satellite handle 0x%lx with master handle 0x%lx for Sat Domain ID %d. Master Index is "
          "%d, "
          "satellite index is %d",
          sat_handle,
          master_handle,
          sat_domain_id,
          master_idx,
          sat_idx);

#if 0 // testing malloc
   ////////////
   //////Malloc test
   apm_offload_ret_info_t ret_1;
   void *               ret_ptr_1;
   ret_ptr_1 = apm_offload_memory_malloc(1, 512, &ret_1);
   AR_MSG(DBG_HIGH_PRIO,
          "AMITH: Rcvd Malloc Ptr 0x%lx, Ret Info. Master hdl = 0x%lx, Sat hdl 0x%lx, offset = %lu",
          ret_ptr_1,
          ret_1.master_handle,
          ret_1.sat_handle,
          ret_1.offset);

   AR_MSG(DBG_HIGH_PRIO,
          "OFFSET FUNC CHECK: VA OFFSET = %lu",
          apm_offload_get_va_offset_from_sat_handle(1, ret_1.sat_handle, ret_ptr_1));

   apm_offload_ret_info_t ret_2;
   void *               ret_ptr_2;
   ret_ptr_2 = apm_offload_memory_malloc(1, 512, &ret_2);
   AR_MSG(DBG_HIGH_PRIO,
          "AMITH: Rcvd Malloc Ptr 0x%lx, Ret Info. Master hdl = 0x%lx, Sat hdl 0x%lx, offset = %d",
          ret_ptr_2,
          ret_2.master_handle,
          ret_2.sat_handle,
          ret_2.offset);

   apm_offload_ret_info_t ret_3;
   void *               ret_ptr_3;
   ret_ptr_3 = apm_offload_memory_malloc(1, 512, &ret_3);
   AR_MSG(DBG_HIGH_PRIO,
          "AMITH: Rcvd Malloc Ptr 0x%lx, Ret Info. Master hdl = 0x%lx, Sat hdl 0x%lx, offset = %d",
          ret_ptr_3,
          ret_3.master_handle,
          ret_3.sat_handle,
          ret_3.offset);

   apm_offload_memory_free(ret_1);
   apm_offload_memory_free(ret_2);
   apm_offload_memory_free(ret_3);
   /////////////
#endif // testing malloc
   return result;
}

ar_result_t apm_offload_satellite_memorymap_check_deregister(uint32_t unmap_master_handle, uint32_t sat_domain_id)
{
   ar_result_t    result                      = AR_EOK;
   sat_info_t *   sat_book_ptr                = NULL;
   master_info_t *master_book_ptr             = NULL;
   bool_t         sat_unmapped                = FALSE;
   uint32_t       sat_domain_remap_list_index = APM_OFFLOAD_INVALID_VAL;

   sat_domain_remap_list_index = apm_offload_get_sat_domain_list_index(sat_domain_id);
   if (APM_OFFLOAD_INVALID_VAL == sat_domain_remap_list_index)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: De-register memory map corresponding to master_handle 0x%lx, "
             " Unsupported Satellite domain ID %lu",
             unmap_master_handle,
             sat_domain_id);
      return AR_EFAILED;
   }

   sat_book_ptr    = &(g_offload_mem_mgr.sat_book[sat_domain_remap_list_index][0]);
   master_book_ptr = &(g_offload_mem_mgr.master_book[0]);
   posal_mutex_lock(g_offload_mem_mgr.map_mutex);

   // at each sat map for this domain, we need to check AT the master_idx, if the master handle == unmap_master_handle
   for (uint32_t i = 0; i < MAX_MASTER_MAPS; i++)
   {
      uint32_t idx = sat_book_ptr[i].master_book_idx;
      if (APM_OFFLOAD_INVALID_VAL == idx)
      {
         AR_MSG(DBG_MED_PRIO,
                "Offload: Satellite Handle for domain id %lu was either never mapped or is already unmapped",
                sat_domain_id);
         continue;
      }
      if (unmap_master_handle == master_book_ptr[idx].master_handle)
      {
         AR_MSG(DBG_HIGH_PRIO,
                "Offload: DeRegistered Satellite handle 0x%lx corresponding to master_handle 0x%lx for Sat Domain ID "
                "%d."
                "satellite index is %d",
                sat_book_ptr[i].sat_handle,
                unmap_master_handle,
                sat_domain_id,
                i);

         sat_book_ptr[i].sat_handle      = APM_OFFLOAD_INVALID_VAL;
         sat_book_ptr[i].master_book_idx = APM_OFFLOAD_INVALID_VAL;
         sat_unmapped                    = TRUE;
         break;
      }
   } // endloop
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);

   // check if the master_handle belongs to unloaned_book and dereg the same
   if (!sat_unmapped)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: Unloaned Book Entry DEREG, Satellite domain ID %lu master_handle 0x%x",
             sat_domain_id,
             unmap_master_handle);

      uint32_t sat_domain_remap_list_index = APM_OFFLOAD_INVALID_VAL;
      sat_domain_remap_list_index          = apm_offload_get_sat_domain_list_index(sat_domain_id);
      if (APM_OFFLOAD_INVALID_VAL == sat_domain_remap_list_index)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Offload: Unloaned Book Entry DEREG, Unsupported Satellite domain ID %lu",
                sat_domain_id);
         return AR_EFAILED;
      }

      posal_mutex_lock(g_offload_mem_mgr.map_mutex);

      unloaned_map_t *unloaned_book_ptr = &(g_offload_mem_mgr.unloaned_book[sat_domain_remap_list_index][0]);
      uint32_t        idx = apm_offload_find_unloaned_master_handle_idx(unloaned_book_ptr, unmap_master_handle);
      if (APM_OFFLOAD_INVALID_VAL == idx)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Offload: unmap_master_handle 0x%lx not found in sat domain %lu unloaned map",
                unmap_master_handle,
                sat_domain_id);
         posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
         return AR_EFAILED;
      }
      else
      {

         AR_MSG(DBG_ERROR_PRIO,
                "Offload: Unloaned Book Entry DEREG, sat_handle 0x%x master_handle 0x%x",
                unloaned_book_ptr[idx].sat_handle,
                unmap_master_handle);

         apm_offload_unloaned_mem_deregister(sat_domain_id, unloaned_book_ptr[idx].sat_handle);

         posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
         return result;
      }
   }

   if (!sat_unmapped)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: Didn't find mapped entry for satellite domain %lu and master_handle 0x%lx",
             sat_domain_id,
             unmap_master_handle);
      result = AR_EFAILED;
   }

   return result;
}

static void apm_offload_unloaned_book_cleanup(uint32_t sat_domain_id)
{
   unloaned_map_t *unloaned_book_ptr           = NULL;
   uint32_t        sat_domain_remap_list_index = APM_OFFLOAD_INVALID_VAL;

   sat_domain_remap_list_index = apm_offload_get_sat_domain_list_index(sat_domain_id);
   if (APM_OFFLOAD_INVALID_VAL == sat_domain_remap_list_index)
   {
      AR_MSG(DBG_ERROR_PRIO, "Offload: unloaned book cleanup, Unsupported Satellite domain ID %lu", sat_domain_id);
      return;
   }

   unloaned_book_ptr = &(g_offload_mem_mgr.unloaned_book[sat_domain_remap_list_index][0]);

   posal_mutex_lock(g_offload_mem_mgr.map_mutex);
   for (uint32_t j = 0; j < MAX_NUM_UNLOANED_MAPS; j++)
   {
      if (APM_OFFLOAD_INVALID_VAL == unloaned_book_ptr[j].master_handle)
      {
         continue;
      }
      AR_MSG(DBG_HIGH_PRIO,
             "Offload: Unloaned Book Entry DEREG for sat domain id %lu at index %lu: Sat handle = 0x%lx",
             sat_domain_id,
             j,
             unloaned_book_ptr[j].sat_handle);
      unloaned_book_ptr[j].master_handle = APM_OFFLOAD_INVALID_VAL;
      unloaned_book_ptr[j].sat_handle    = APM_OFFLOAD_INVALID_VAL;
   }
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
}

static void apm_offload_sat_book_cleanup(uint32_t sat_domain_id)
{
   sat_info_t *   sat_book_ptr                = NULL;
   master_info_t *master_book_ptr             = NULL;
   uint32_t       sat_domain_remap_list_index = APM_OFFLOAD_INVALID_VAL;

   sat_domain_remap_list_index = apm_offload_get_sat_domain_list_index(sat_domain_id);
   if (APM_OFFLOAD_INVALID_VAL == sat_domain_remap_list_index)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: satellite domain book cleanup, Unsupported Satellite domain ID %lu",
             sat_domain_id);
      return;
   }

   sat_book_ptr    = &(g_offload_mem_mgr.sat_book[sat_domain_remap_list_index][0]);
   master_book_ptr = &(g_offload_mem_mgr.master_book[0]);

   posal_mutex_lock(g_offload_mem_mgr.map_mutex);
   for (uint32_t i = 0; i < MAX_MASTER_MAPS; i++)
   {
      uint32_t idx = sat_book_ptr[i].master_book_idx;
      if (APM_OFFLOAD_INVALID_VAL == idx)
      {
#ifdef APM_OFFLOAD_MAP_UTILS_DBG
         AR_MSG(DBG_MED_PRIO,
                "Offload: Satellite Handle for domain id %lu was either never mapped or is already unmapped",
                sat_domain_id);
#endif
         continue;
      }

      AR_MSG(DBG_HIGH_PRIO,
             "Offload: DeRegistered Satellite handle 0x%lx corresponding to master_handle 0x%lx for Sat Domain ID "
             "%d."
             "satellite index is %d",
             sat_book_ptr[i].sat_handle,
             master_book_ptr[idx].master_handle,
             sat_domain_id,
             i);

      sat_book_ptr[i].sat_handle      = APM_OFFLOAD_INVALID_VAL;
      sat_book_ptr[i].master_book_idx = APM_OFFLOAD_INVALID_VAL;
   } // endloop
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
}

ar_result_t apm_offload_sat_cleanup(uint32_t sat_domain_id)
{
   ar_result_t result = AR_EOK;
   apm_offload_sat_book_cleanup(sat_domain_id);
   apm_offload_unloaned_book_cleanup(sat_domain_id);
   return result;
}

// This function goes over the master book to see if the handle was mapped. If so, it deletes the heap.
// else it just returns EOK.
ar_result_t apm_offload_master_memorymap_check_deregister(uint32_t master_handle)
{
   ar_result_t result = AR_EOK;

   posal_mutex_lock(g_offload_mem_mgr.map_mutex);
   uint32_t idx = apm_offload_find_master_handle_idx(master_handle);

   if (APM_OFFLOAD_INVALID_VAL == idx)
   {
      AR_MSG(DBG_MED_PRIO, "Offload: Master Handle 0x%lx not found in the book to unmap. Returning", master_handle);
      posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
      return AR_EOK;
   }

#ifdef APM_OFFLOAD_MAP_UTILS_DBG
   AR_MSG(DBG_HIGH_PRIO,
          "Offload: apm_offload_master_memorymap_check_deregister: Found the unmap region in the master book, index "
          "%lu",
          idx);
#endif // APM_OFFLOAD_MAP_UTILS_DBG
   /// else we need to delete the heapmgr
   posal_memory_heapmgr_destroy(g_offload_mem_mgr.master_book[idx].heap_id);

   APM_OFFLOAD_MEMSET(&g_offload_mem_mgr.master_book[idx], sizeof(master_info_t));
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);

   return result;
}

uint32_t apm_offload_get_sat_handle_from_master_handle(uint32_t master_handle, uint32_t sat_domain_id)
{
   uint32_t       sat_handle                  = APM_OFFLOAD_INVALID_VAL;
   sat_info_t *   sat_book_ptr                = NULL;
   master_info_t *master_book_ptr             = NULL;
   uint32_t       sat_domain_remap_list_index = APM_OFFLOAD_INVALID_VAL;

   sat_domain_remap_list_index = apm_offload_get_sat_domain_list_index(sat_domain_id);
   if (APM_OFFLOAD_INVALID_VAL == sat_domain_remap_list_index)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: get sat handle from master handle, Unsupported Satellite domain ID %lu",
             sat_domain_id);
      return APM_OFFLOAD_INVALID_VAL;
   }

   posal_mutex_lock(g_offload_mem_mgr.map_mutex);
   sat_book_ptr    = &(g_offload_mem_mgr.sat_book[sat_domain_remap_list_index][0]);
   master_book_ptr = &(g_offload_mem_mgr.master_book[0]);

   // at each sat map for this domain, we need to check AT the master_idx, if the master handle == unmap_master_handle
   for (uint32_t i = 0; i < MAX_MASTER_MAPS; i++)
   {
      uint32_t idx = sat_book_ptr[i].master_book_idx;
      if (APM_OFFLOAD_INVALID_VAL == idx)
      {
         AR_MSG(DBG_MED_PRIO,
                "Offload: Satellite Handle for domain id %lu was either never mapped or already unmapped",
                sat_domain_id);
         continue;
      }
      if (master_handle == master_book_ptr[idx].master_handle)
      {
         sat_handle = sat_book_ptr[i].sat_handle;
         break;
      }
   }

   if (sat_handle == APM_OFFLOAD_INVALID_VAL)
   {
      uint32_t sat_domain_remap_list_index = APM_OFFLOAD_INVALID_VAL;
      sat_domain_remap_list_index          = apm_offload_get_sat_domain_list_index(sat_domain_id);
      if (APM_OFFLOAD_INVALID_VAL == sat_domain_remap_list_index)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Offload: Unloaned Book Entry DEREG, Unsupported Satellite domain ID %lu",
                sat_domain_id);
         return AR_EFAILED;
      }

      unloaned_map_t *unloaned_book_ptr = &(g_offload_mem_mgr.unloaned_book[sat_domain_remap_list_index][0]);
      uint32_t        idx               = apm_offload_find_unloaned_master_handle_idx(unloaned_book_ptr, master_handle);

      if (APM_OFFLOAD_INVALID_VAL == idx)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Offload: Master handle 0x%lx not found in sat domain %lu unloaned map",
                master_handle,
                sat_domain_id);
         posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
         return sat_handle;
      }

      AR_MSG(DBG_ERROR_PRIO,
             "Offload: SAT HANDLE found master_handle: 0x%x 0x%x",
             master_handle,
             g_offload_mem_mgr.unloaned_book[sat_domain_remap_list_index][idx].sat_handle);
      sat_handle = g_offload_mem_mgr.unloaned_book[sat_domain_remap_list_index][idx].sat_handle;
   }
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
   return sat_handle;
}

ar_result_t apm_offload_unloaned_mem_register(uint32_t sat_domain_id, uint32_t master_handle, uint32_t sat_handle)
{
   ar_result_t result                      = AR_EOK;
   uint32_t    sat_domain_remap_list_index = APM_OFFLOAD_INVALID_VAL;

   sat_domain_remap_list_index = apm_offload_get_sat_domain_list_index(sat_domain_id);
   if (APM_OFFLOAD_INVALID_VAL == sat_domain_remap_list_index)
   {
      AR_MSG(DBG_ERROR_PRIO, "Offload: Unloaned Book Entry REG, Unsupported Satellite domain ID %lu", sat_domain_id);
      return AR_EFAILED;
   }

   posal_mutex_lock(g_offload_mem_mgr.map_mutex);
   unloaned_map_t *unloaned_book_ptr = &(g_offload_mem_mgr.unloaned_book[sat_domain_remap_list_index][0]);
   uint32_t        idx               = apm_offload_get_first_open_unloaned_slot(unloaned_book_ptr);
   if (APM_OFFLOAD_INVALID_VAL == idx)
   {
      AR_MSG(DBG_ERROR_PRIO, "Offload: All slots in the unloaned book are full. Error");
      posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
      return AR_EFAILED;
   }
   unloaned_book_ptr[idx].master_handle = master_handle;
   unloaned_book_ptr[idx].sat_handle    = sat_handle;
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);

   AR_MSG(DBG_HIGH_PRIO,
          "Offload: Unloaned Book Entry REG for sat domain id %lu at index %lu: Master Handle = 0x%lx, Sat handle = "
          "0x%lx",
          sat_domain_id,
          idx,
          master_handle,
          sat_handle);
   return result;
}

/*amith TBD: Call from APM_DEREG_SHARED from OLC when needed*/
ar_result_t apm_offload_unloaned_mem_deregister(uint32_t sat_domain_id, uint32_t sat_handle)
{
   ar_result_t result                      = AR_EOK;
   uint32_t    sat_domain_remap_list_index = APM_OFFLOAD_INVALID_VAL;

   sat_domain_remap_list_index = apm_offload_get_sat_domain_list_index(sat_domain_id);
   if (APM_OFFLOAD_INVALID_VAL == sat_domain_remap_list_index)
   {
      AR_MSG(DBG_ERROR_PRIO, "Offload: Unloaned Book Entry DEREG, Unsupported Satellite domain ID %lu", sat_domain_id);
      return AR_EFAILED;
   }

   posal_mutex_lock(g_offload_mem_mgr.map_mutex);
   unloaned_map_t *unloaned_book_ptr = &(g_offload_mem_mgr.unloaned_book[sat_domain_remap_list_index][0]);
   uint32_t        idx               = apm_offload_find_unloaned_sat_handle_idx(unloaned_book_ptr, sat_handle);
   if (APM_OFFLOAD_INVALID_VAL == idx)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: Sat handle 0x%lx not found in sat domain %lu unloaned map",
             sat_handle,
             sat_domain_id);
      posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
      return AR_EFAILED;
   }
   /*reset to INVALID VAL*/
   APM_OFFLOAD_MEMSET(&unloaned_book_ptr[idx], sizeof(unloaned_map_t));
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);

   AR_MSG(DBG_HIGH_PRIO,
          "Offload: Unloaned Book Entry DEREG for sat domain id %lu at index %lu: Sat handle = 0x%lx",
          sat_domain_id,
          idx,
          sat_handle);

   return result;
}

uint32_t apm_offload_get_persistent_sat_handle(uint32_t sat_domain_id, uint32_t master_handle)
{
   uint32_t not_found                   = APM_OFFLOAD_INVALID_VAL;
   uint32_t sat_domain_remap_list_index = APM_OFFLOAD_INVALID_VAL;

   sat_domain_remap_list_index = apm_offload_get_sat_domain_list_index(sat_domain_id);
   if (APM_OFFLOAD_INVALID_VAL == sat_domain_remap_list_index)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: get unloaned book sat_handle, Unsupported Satellite domain ID %lu",
             sat_domain_id);
      return AR_EFAILED;
   }
   posal_mutex_lock(g_offload_mem_mgr.map_mutex);
   unloaned_map_t *unloaned_book_ptr = &(g_offload_mem_mgr.unloaned_book[sat_domain_remap_list_index][0]);

   for (uint32_t i = 0; i < MAX_NUM_UNLOANED_MAPS; i++)
   {
      if (master_handle == unloaned_book_ptr[i].master_handle)
      {
         posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
         return unloaned_book_ptr[i].sat_handle;
      }
   }
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);
   AR_MSG(DBG_ERROR_PRIO,
          "Offload: Could not find master handle 0x%lx in unloaned book for sat_domain_id %lu",
          master_handle,
          sat_domain_id);
   return not_found;
}

ar_result_t apm_offload_global_mem_mgr_deinit()
{
   AR_MSG(DBG_HIGH_PRIO, "In APM Offload Global Mem Mgr Deinit");
   posal_mutex_destroy(&g_offload_mem_mgr.map_mutex);
   APM_OFFLOAD_MEMSET(&g_offload_mem_mgr, sizeof(apm_offload_mem_mgr_t));
   return AR_EOK;
}

/* =======================================================================
   Memory MAP Utility Function Implementations
========================================================================== */
static uint32_t apm_offload_get_sat_domain_list_index(uint32_t sat_domain_id)
{
   uint32_t not_found = APM_OFFLOAD_INVALID_VAL;
   for (uint32_t i = 0; i < MAX_SATELLITE_DOMAINS; i++)
   {
      if (sat_domain_id == satellite_domain_list[i])
      {
         return i;
      }
   }

   return not_found;
}

static uint32_t apm_offload_get_first_open_master_slot()
{
   uint32_t not_found = APM_OFFLOAD_INVALID_VAL;

   for (uint32_t i = 0; i < MAX_MASTER_MAPS; i++)
   {
      if (APM_OFFLOAD_INVALID_VAL == g_offload_mem_mgr.master_book[i].master_handle)
      {
         return i;
      }
   }
   return not_found;
}

static uint32_t apm_offload_find_master_handle_idx(uint32_t master_handle)
{
   uint32_t not_found = APM_OFFLOAD_INVALID_VAL;

   for (uint32_t i = 0; i < MAX_MASTER_MAPS; i++)
   {
      if (master_handle == g_offload_mem_mgr.master_book[i].master_handle)
      {
         return i;
      }
   }
   return not_found;
}

static uint32_t apm_offload_get_first_open_sat_slot(sat_info_t *sat_book_ptr)
{
   uint32_t not_found = APM_OFFLOAD_INVALID_VAL;

   for (uint32_t i = 0; i < MAX_MASTER_MAPS; i++)
   {
      if (APM_OFFLOAD_INVALID_VAL == sat_book_ptr[i].sat_handle)
      {
         return i;
      }
   }
   return not_found;
}

static uint32_t apm_offload_get_first_open_unloaned_slot(unloaned_map_t *unloaned_book_ptr)
{
   uint32_t not_found = APM_OFFLOAD_INVALID_VAL;

   for (uint32_t i = 0; i < MAX_NUM_UNLOANED_MAPS; i++)
   {
      if (APM_OFFLOAD_INVALID_VAL == unloaned_book_ptr[i].master_handle)
      {
         return i;
      }
   }
   return not_found;
}

static uint32_t apm_offload_find_unloaned_sat_handle_idx(unloaned_map_t *unloaned_book_ptr, uint32_t sat_handle)
{
   uint32_t not_found = APM_OFFLOAD_INVALID_VAL;

   for (uint32_t i = 0; i < MAX_NUM_UNLOANED_MAPS; i++)
   {
      if (sat_handle == unloaned_book_ptr[i].sat_handle)
      {
         return i;
      }
   }
   return not_found;
}

static uint32_t apm_offload_find_unloaned_master_handle_idx(unloaned_map_t *unloaned_book_ptr, uint32_t master_handle)
{
   uint32_t not_found = APM_OFFLOAD_INVALID_VAL;

   for (uint32_t i = 0; i < MAX_NUM_UNLOANED_MAPS; i++)
   {
      if (master_handle == unloaned_book_ptr[i].master_handle)
      {
         return i;
      }
   }
   return not_found;
}

/* =======================================================================
   Memory Function Implementations
========================================================================== */
void *apm_offload_memory_malloc(uint32_t sat_domain_id, uint32_t req_size, apm_offload_ret_info_t *ret_info_ptr)
{
   void *        ret_ptr                     = NULL;
   bool_t        found_mem                   = FALSE;
   uint32_t      master_idx                  = APM_OFFLOAD_INVALID_VAL;
   POSAL_HEAP_ID master_heap                 = APM_INTERNAL_STATIC_HEAP_ID;
   uint32_t      heap_start_addr             = 0;
   uint32_t      sat_domain_remap_list_index = APM_OFFLOAD_INVALID_VAL;

   sat_domain_remap_list_index = apm_offload_get_sat_domain_list_index(sat_domain_id);
   if (APM_OFFLOAD_INVALID_VAL == sat_domain_remap_list_index)
   {
      AR_MSG(DBG_ERROR_PRIO, "Offload: alloc memory, Unsupported Satellite domain ID %lu", sat_domain_id);
      return NULL;
   }

   // TODO: Add apm api to check suppported domain id's, removing gpr api
   //   bool_t is_sat_domain_supported = TRUE;
   //   __gpr_cmd_is_supported_domain(sat_domain_id, &is_sat_domain_supported);
   //   if (FALSE == is_sat_domain_supported)
   //   {
   //      AR_MSG(DBG_ERROR_PRIO, "Offload: Sat domain id %lu not supported. Malloc won't happen.", sat_domain_id);
   //      return NULL;
   //   }
   /*Traverse through the sub array for this sat domain in the sat book. Go to the master Index associated,
     Try to malloc with that heap ID. If it fails go to the next sat handle in the book, try with the corresponding
     master map. Repeat this process until all are exhausted. IF succesful, break and fill the rest of the details*/

   sat_info_t *sat_book_ptr = NULL;

   // update the size to align the size to cache line
   req_size  =  req_size + (APM_OFFLOAD_MEM_ALIGNMENT_BYTES  - req_size % APM_OFFLOAD_MEM_ALIGNMENT_BYTES);

   posal_mutex_lock(g_offload_mem_mgr.map_mutex);
   sat_book_ptr = &(g_offload_mem_mgr.sat_book[sat_domain_remap_list_index][0]);

   for (uint32_t i = 0; i < MAX_MASTER_MAPS; i++)
   {
      master_idx = sat_book_ptr[i].master_book_idx;
      if (APM_OFFLOAD_INVALID_VAL == master_idx)
      {
#ifdef APM_OFFLOAD_MEMORY_DBG
         AR_MSG(DBG_ERROR_PRIO, "sat book at index %lu not valid", i);
#endif
         continue;
      }

      if (req_size > g_offload_mem_mgr.master_book[master_idx].heap_size)
      {
#ifdef APM_OFFLOAD_MEMORY_DBG
         AR_MSG(DBG_MED_PRIO,
                "Offload: Req Size: %lu, Master idx %lu has heap size %lu. Continuing",
                req_size,
                master_idx,
                g_offload_mem_mgr.master_book[master_idx].heap_size);
#endif
         continue;
      }

      master_heap = g_offload_mem_mgr.master_book[master_idx].heap_id;
      if (NULL == (ret_ptr = posal_memory_aligned_malloc(req_size, APM_OFFLOAD_MEM_ALIGNMENT_BYTES, master_heap)))
      {
#ifdef APM_OFFLOAD_MEMORY_DBG
         AR_MSG(DBG_MED_PRIO,
                "Offload: Malloc for Req Size: %lu, Master idx %lu from heap id %lu failed. Continuing to next map",
                req_size,
                master_idx,
                master_heap);
#endif
         continue;
      }
      found_mem       = TRUE;
      heap_start_addr = g_offload_mem_mgr.master_book[master_idx].heap_start_addr_va;

      /*Fill the ret info*/
      ret_info_ptr->master_handle = g_offload_mem_mgr.master_book[master_idx].master_handle;
      ret_info_ptr->sat_handle    = sat_book_ptr[i].sat_handle;
      ret_info_ptr->offset        = (uint32_t)ret_ptr - heap_start_addr;
      ret_info_ptr->heap_id       = (uint32_t)master_heap;
      ret_info_ptr->ret_ptr       = ret_ptr;
      break;
   }
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);

   if (!found_mem)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: Unable to Malloc Memory of size %lu, for sat domain %lu",
             req_size,
             sat_domain_id);
      return NULL;
   }
#ifdef APM_OFFLOAD_MEMORY_DBG
   AR_MSG(DBG_HIGH_PRIO,
          "Offload: apm_offload_memory_malloc:Successfully malloced memory from heap %lu of size %lu, ret_ptr = 0x%lx, "
          "Ret "
          "Info: "
          "Master Handle = 0x%lx, Sat Handle = 0x%lx, Offset = 0x%lx",
          master_heap,
          req_size,
          ret_ptr,
          ret_info_ptr->master_handle,
          ret_info_ptr->sat_handle,
          ret_info_ptr->offset);
#endif // APM_OFFLOAD_MAP_UTILS_DBG
   return ret_ptr;
}


void apm_offload_memory_free(apm_offload_ret_info_t *shmem_info_ptr)
{
   if ((shmem_info_ptr) && (shmem_info_ptr->ret_ptr))
   {
#ifdef APM_OFFLOAD_MEMORY_DBG
      AR_MSG(DBG_HIGH_PRIO, "Freeing Offload memory ptr 0x%lx", shmem_info_ptr->ret_ptr);
#endif
      posal_memory_aligned_free_v2(shmem_info_ptr->ret_ptr, shmem_info_ptr->heap_id);
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "Freeing Offload memory ptr, invalid arguments");
   }
}

/* =======================================================================
   Memory Utility Function Implementations
========================================================================== */
/*Caller must check for error return value 0xFFFFFFFF*/
uint32_t apm_offload_get_va_offset_from_sat_handle(uint32_t sat_domain_id, uint32_t sat_handle, void *address)
{
   uint32_t    offset                      = 0;
   sat_info_t *sat_book_ptr                = NULL;
   bool_t      found_handle                = FALSE;
   uint32_t    heap_start_addr             = 0;
   uint32_t    master_idx                  = APM_OFFLOAD_INVALID_VAL;
   uint32_t    sat_domain_remap_list_index = APM_OFFLOAD_INVALID_VAL;

   sat_domain_remap_list_index = apm_offload_get_sat_domain_list_index(sat_domain_id);
   if (APM_OFFLOAD_INVALID_VAL == sat_domain_remap_list_index)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: apm_offload_get_va_offset_from_sat_handle, Unsupported Satellite domain ID %lu",
             sat_domain_id);
      return AR_EFAILED;
   }

   posal_mutex_lock(g_offload_mem_mgr.map_mutex);
   sat_book_ptr = &(g_offload_mem_mgr.sat_book[sat_domain_remap_list_index][0]);
   for (uint32_t i = 0; i < MAX_MASTER_MAPS; i++)
   {
      if (sat_book_ptr[i].sat_handle != sat_handle)
      {
         continue;
      }
      found_handle    = TRUE;
      master_idx      = sat_book_ptr[i].master_book_idx;
      heap_start_addr = g_offload_mem_mgr.master_book[master_idx].heap_start_addr_va;
      offset          = (uint32_t)address - heap_start_addr;
   }
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);

   if (!found_handle)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Offload: apm_offload_get_va_offset_from_sat_handle: Could not find the sat_handle 0x%lx in the sat map.");
      return APM_OFFLOAD_INVALID_VAL;
   }

   return offset;
}

// This function goes over the master book, checks all valid handles which are mapped and it deletes the heap.
ar_result_t apm_offload_master_memorymap_deregister_all()
{
   ar_result_t result = AR_EOK;
   uint32_t    master_handle;

   posal_mutex_lock(g_offload_mem_mgr.map_mutex);

   for (uint32_t idx = 0; idx < MAX_MASTER_MAPS; idx++)
   {
      master_handle = g_offload_mem_mgr.master_book[idx].master_handle;
      if (APM_OFFLOAD_INVALID_VAL == master_handle)
      {
         AR_MSG(DBG_MED_PRIO, "Offload: Master Handle is not valid at index %lu", idx);
         continue;
      }
      else
      {
         AR_MSG(DBG_MED_PRIO, "Offload: Master Handle 0x%lx found in the book at index %lu", master_handle, idx);
      }

#ifdef APM_OFFLOAD_MAP_UTILS_DBG
      AR_MSG(DBG_HIGH_PRIO,
             "Offload: apm_offload_master_memorymap_check_deregister_all: "
             "Master Handle 0x%lx found in the book at index "
             "%lu , destroying the heap",
             master_handle,
             idx);
#endif // APM_OFFLOAD_MAP_UTILS_DBG
      /// else we need to delete the heapmgr
      posal_memory_heapmgr_destroy(g_offload_mem_mgr.master_book[idx].heap_id);

      APM_OFFLOAD_MEMSET(&g_offload_mem_mgr.master_book[idx], sizeof(master_info_t));
   }
   posal_mutex_unlock(g_offload_mem_mgr.map_mutex);

   return result;
}

ar_result_t apm_offload_mem_mgr_reset(void)
{
   ar_result_t result = AR_EOK;
   AR_MSG(DBG_HIGH_PRIO, "Offload: apm_offload_mem_mgr reset");
   result = apm_offload_master_memorymap_deregister_all();
   APM_OFFLOAD_MEMSET(&g_offload_mem_mgr.sat_book, MAX_SATELLITE_DOMAINS * MAX_MASTER_MAPS * sizeof(sat_info_t));
   APM_OFFLOAD_MEMSET(&g_offload_mem_mgr.unloaned_book,
                      MAX_SATELLITE_DOMAINS * MAX_NUM_UNLOANED_MAPS * sizeof(sat_info_t));
   return result;
}

/*Amith TBD: We may need certain simple utils to handle persistent calibs - Add when needed. Unloaned Book will be
 * used*/
