/**
 * \file posal_memorymap.c
 * \brief
 *  This file contains a utility for memory mapping and unmapping shared memory, LPM etc.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* ----------------------------------------------------------------------------
 * Include Files
 * ------------------------------------------------------------------------- */
#include "posal.h"

#include "ar_osal_error.h"

#include "posal_globalstate.h"
#include <stringl.h>
#include <errno.h>
#include <sys/mman.h>
#include "spf_hashtable.h"
#ifdef POSAL_MMAP_VFIO
#include "plat_vfio.h"
#endif /* POSAL_MMAP_VFIO */

#ifdef POSAL_MMAP_EXTN
extern void *mdf_mem_base_va_addr;
#endif /* POSAL_MMAP_EXTN */

/* ----------------------------------------------------------------------------
 * Global Declarations/Definitions
 * ------------------------------------------------------------------------- */
/* Enable this macro for debugging */
//#define DEBUG_POSAL_MEMORYMAP
#define POSAL_FIXED_MEM_REGION_THRESHOLD_SIZE_IN_BYTES (1024*1024) //1MB

#define POSAL_MEMORYMAP_HASH_TABLE_SIZE 16
#define POSAL_MEMORYMAP_HASH_TABLE_RESIZE_FACTOR 2

#ifdef POSAL_MMAP_VFIO
#define VFIO_DEV_NAME "soc@0:umd_audio_hlos@1"
struct plat_vfio g_pvfio;
#endif /* POSAL_MMAP_VFIO */

/* -----------------------------------------------------------------------
 ** Constant / Define Declarations
 ** ----------------------------------------------------------------------- */

typedef union
{
   struct
   {
      uint32_t lsw; /** Starting memory address(LSW) of this region. */
      uint32_t msw; /** Starting memory address(MSW) of this region. */
   } mem_addr_32b;

   uint64_t mem_addr_64b; /** Starting memory address 64bits of this region. */
} mem_addr_t;

/** Commands for memory map utility function. */
typedef enum memory_map_cmds {
   CMD_UPDATE_REF_COUNT,
   CMD_GET_MAPPING_MODE,
   CMD_SHM_MEM_UNMAP,
   CMD_GET_SHM_ATTRIB
} memory_map_cmds_t;

/**
@brief One contiguous shared memory region mapped to virtual *
 *     address. This struct immediately follows the
 *     posal_memorymap_node_t.
 *
 */
typedef struct
{
   mem_addr_t shm_addr;
   /** shared memory address. This address has to be 64bit aligned **/

   uint32_t virt_addr;
   /** Corresponding starting virtual address of  the region. */

   uint32_t mem_size;
   /** Size of the region.  */

   uint32_t double_word_padding;
   /** padding to make sure the next region record is also 64 bit aligned */

   void* virt_addr_ptr;
} posal_memorymap_region_record_t;

/**< Hash node structure to hold heap-id to memory count mapping */
typedef struct posal_memorymap_hashnode_t
{
   // Need to be first element
   spf_hash_node_t hash_node;
   /**< Hash node */

   uint32_t client_token;
   /**< Key - client token is used as a key to hashnode */

   posal_memorymap_client_t *client_ptr;
   /**< Value - client_ptr returned to client */

} posal_memorymap_hashnode_t;

/**
 *Hash node structure to hold heap-id to memory count mapping
 *This mapping is used to map the file descriptor received in the lsw
 *in physical address in shm_mem_reg_ptr in the function posal_memorymap_shm_mem_map()
 *to the memory mapping node i.e. posal_memorymap_node_t structure created in the function
 *to hold the details of the memory mapping
 */
typedef struct posal_memorymap_shm_map_hashnode_t
{
   // Need to be first element
   spf_hash_node_t hash_node;
   /**< Hash node */

   uint32_t unique_shmem_id_24bit;
   /**< Key - unique_shmem_id_24bit is used as a key to hashnode */

   posal_memorymap_node_t *shm_node;
   /**< Value - shm map handle returned to client */

} posal_memorymap_shm_map_hashnode_t;

/**< Posal memory profiling main structure */
typedef struct posal_memorymap_internal_t
{
   spf_hashtable_t memmap_ht;
   /**< Hash table to hold client ID to client token mapping */

   spf_hashtable_t shmmap_ht;
   /**< Hash table to hold "unique_shmem_id_24bit" to shm map handle mapping */
} posal_memorymap_internal_t;

posal_memorymap_internal_t          g_posal_memorymap_internal;
posal_memorymap_internal_t *        g_posal_memorymap_internal_ptr = NULL;

/**< Posal memory map get mem rgion attribute from shmm handle struct */
typedef struct mem_region_attrib_from_shmm_handle_struct_t
{
   uint32_t shm_addr_lsw;
   /**< Physical address LSW */

   uint32_t shm_addr_msw;
   /**< Physical address MSW */

   void* req_virt_addr;
   /**< Virtual address that corresponds to the requested physical address. */

   posal_memorymap_mem_region_attrib_t *mem_reg_attrib_ptr;
   /**< posal_memorymap_mem_region_attrib_t struct address */

   uint32_t is_ref_counted;
   /**< does ref_count need to be updated */
} mem_region_attrib_from_shmm_handle_struct_t;

/* -------------------------------------------------------------------------
 * Function Definitions
 * ------------------------------------------------------------------------- */

/**
 *This function verifies if a given client is already registered
 *with the posal_memorymap or not.
 */
static ar_result_t memorymap_util_find_client(uint32_t client_token);

/**
 *This function updates the ref count of a memory map handle
 */

static ar_result_t memorymap_util_shm_update_refcount(uint32_t client_token,
                                                      uint32_t shm_mem_map_handle,
                                                      int16_t  count_value);

static ar_result_t memorymap_util_cmd_handler(uint32_t client_token,
                                              uint32_t shm_mem_map_handle,
                                              uint32_t command,
                                              void *   args);

/**
 *This function adds the memory map node to the client list and
 *appropriately update the posal_globalstate .
 */
static void memorymap_util_add_mem_map_node_to_client(uint32_t client_token, posal_memorymap_node_t *mem_map_node_ptr);

/****************************************************************************
 ** Memory Map
 *****************************************************************************/

static void posal_memorymap_hashnode_free(void *free_context_ptr, spf_hash_node_t *node_ptr)
{
   if (NULL != node_ptr)
   {
      posal_memorymap_hashnode_t *memorymap_node_ptr = (posal_memorymap_hashnode_t *)(node_ptr);
      posal_memory_free(memorymap_node_ptr);
   }
}

static void posal_memorymap_shm_map_hashnode_free(void *free_context_ptr, spf_hash_node_t *node_ptr)
{
   if (NULL != node_ptr)
   {
      posal_memorymap_shm_map_hashnode_t *shmmap_node_ptr = (posal_memorymap_shm_map_hashnode_t *)(node_ptr);
      posal_memory_free(shmmap_node_ptr);
   }
}

void posal_memorymap_global_init()
{
   memset((void *)posal_globalstate.mem_map_client_list, 0, sizeof(posal_globalstate.mem_map_client_list));

   ar_result_t result = AR_EOK;

   g_posal_memorymap_internal_ptr = &g_posal_memorymap_internal;

   result = spf_hashtable_init(&g_posal_memorymap_internal.memmap_ht,
                               POSAL_HEAP_DEFAULT,
                               POSAL_MEMORYMAP_HASH_TABLE_SIZE,
                               POSAL_MEMORYMAP_HASH_TABLE_RESIZE_FACTOR,
                               posal_memorymap_hashnode_free,
                               (void *)g_posal_memorymap_internal_ptr);
   if (result != AR_EOK)
   {
      AR_MSG(DBG_ERROR_PRIO, "posal_memorymap: Failed to create hashtable for posal_memorymap.");
   }

   result = spf_hashtable_init(&g_posal_memorymap_internal.shmmap_ht,
                               POSAL_HEAP_DEFAULT,
                               POSAL_MEMORYMAP_HASH_TABLE_SIZE,
                               POSAL_MEMORYMAP_HASH_TABLE_RESIZE_FACTOR,
                               posal_memorymap_shm_map_hashnode_free,
                               (void *)&g_posal_memorymap_internal_ptr->shmmap_ht);
   if (result != AR_EOK)
   {
      AR_MSG(DBG_ERROR_PRIO, "posal_memorymap: Failed to create hashtable for posal_memorymap_shm_map.");
   }

#ifdef POSAL_MMAP_VFIO
   result = plat_vfio_device_init(VFIO_DEV_NAME, &g_pvfio);
   if (result != AR_EOK)
   {
      AR_MSG(DBG_ERROR_PRIO, "posal_memorymap: plat_vfio_device_init failed with result : %d", result);
   }
#endif /* POSAL_MMAP_VFIO */
}

void posal_memorymap_global_deinit()
{
   posal_mutex_lock(posal_globalstate.mutex);

   posal_memorymap_client_t *pClientNode;

   /* Loop through all the available clients and deallocate the memory */
   for(uint32_t idx = 0; idx < POSAL_MEMORY_MAP_MAX_CLIENTS ; idx++)
   {
      pClientNode = posal_globalstate.mem_map_client_list[idx];
      if(pClientNode)
      {
         posal_mutex_lock((pClientNode->mClientMutex));

         // Unmap all the memory nodes of the client
         // Client token key needs to be used here.
         // Client token is client_idx +1, where client_idx retrieved from pClientNode
         posal_memorymap_unmap_all(pClientNode->client_id + 1);

         posal_globalstate.mem_map_client_list[idx] = NULL;

         posal_mutex_unlock(pClientNode->mClientMutex);
         posal_mutex_destroy(&pClientNode->mClientMutex);

         /*Deallocate client memory */
         posal_memory_free(pClientNode);
      }
   }

   spf_hashtable_deinit(&g_posal_memorymap_internal.memmap_ht);

   spf_hashtable_deinit(&g_posal_memorymap_internal.shmmap_ht);
   memset(&g_posal_memorymap_internal, 0, sizeof(g_posal_memorymap_internal));

   posal_mutex_unlock(posal_globalstate.mutex);

#ifdef POSAL_MMAP_VFIO
   plat_vfio_device_deinit(&g_pvfio);
#endif /* POSAL_MMAP_VFIO */
}

void posal_memorymap_global_unmap_all()
{
   posal_mutex_lock(posal_globalstate.mutex);

   posal_memorymap_client_t *pClientNode;
   /* Loop through all the available clients and deallocate the memory */
   for(uint32_t idx = 0; idx < POSAL_MEMORY_MAP_MAX_CLIENTS ; idx++)
   {
      pClientNode = posal_globalstate.mem_map_client_list[idx];
      if(pClientNode)
      {
         posal_mutex_lock((pClientNode->mClientMutex));

         // Unmap all the memory nodes of the client
         // Client token key needs to be used here.
         // Client token is client_idx +1, where client_idx retrieved from pClientNode
         posal_memorymap_unmap_all(pClientNode->client_id + 1);

         posal_mutex_unlock(pClientNode->mClientMutex);
      }
   }

   posal_mutex_unlock(posal_globalstate.mutex);
}

ar_result_t posal_memorymap_register(uint32_t *client_token_ptr, POSAL_HEAP_ID heap_id)
{
   posal_memorymap_client_t *client_ptr;

   /* add to global debug structure, add new client node to the linked list. */
   posal_mutex_lock(posal_globalstate.mutex);

   /*Check if the client can be registered */
   if (posal_globalstate.num_registered_memmap_clients >= POSAL_MEMORY_MAP_MAX_CLIENTS)
   {
#ifdef DEBUG_POSAL_MEMORYMAP
      AR_MSG(DBG_HIGH_PRIO, "posal_memorymap cannot register client ");
#endif
      /*unlock the mutex */
      posal_mutex_unlock(posal_globalstate.mutex);
      return AR_EFAILED;
   }

   /*Assign available client ID as token */
   *client_token_ptr   = 0;
   uint32_t client_idx = 0;
   for (client_idx = 0; client_idx < POSAL_MEMORY_MAP_MAX_CLIENTS; client_idx++)
   {
      if (NULL == posal_globalstate.mem_map_client_list[client_idx])
      {
         posal_globalstate.num_registered_memmap_clients++;
         break;
      }
   }

   /* If there is no available node */
   if (POSAL_MEMORY_MAP_MAX_CLIENTS == client_idx)
   {
#ifdef DEBUG_POSAL_MEMORYMAP
      AR_MSG(DBG_HIGH_PRIO, "posal_memorymap cannot register client ");
#endif

      /*unlock the mutex */
      posal_mutex_unlock(posal_globalstate.mutex);
      return AR_EFAILED;
   }

   /*End of critical section*/
   posal_mutex_unlock(posal_globalstate.mutex);

   /* allocate space for client */
   client_ptr = (posal_memorymap_client_t *)posal_memory_malloc(sizeof(posal_memorymap_client_t), heap_id);
   if (NULL == client_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO, "posal_memorymap cannot register client ");
      return AR_ENOMEMORY;
   }
   POSAL_ZEROAT(client_ptr);

   /* initialize mutex */
   posal_mutex_create(&(client_ptr->mClientMutex), heap_id);

   posal_memorymap_hashnode_t *new_memorymap_hashnode_ptr =
      posal_memory_malloc(sizeof(posal_memorymap_hashnode_t), POSAL_HEAP_DEFAULT);
   if (NULL == new_memorymap_hashnode_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "posal_memorymap: Failed to allocate memory for hash node.");
      return AR_ENOMEMORY;
   }
   memset(new_memorymap_hashnode_ptr, 0, sizeof(new_memorymap_hashnode_ptr));

   new_memorymap_hashnode_ptr->hash_node.key_ptr  = &new_memorymap_hashnode_ptr->client_token;
   new_memorymap_hashnode_ptr->hash_node.key_size = sizeof(client_idx);
   new_memorymap_hashnode_ptr->hash_node.next_ptr = NULL;

   /* add to global debug structure, add new client node to the linked list. */
   posal_mutex_lock(posal_globalstate.mutex);

   posal_globalstate.mem_map_client_list[client_idx] = client_ptr;
   client_ptr->client_id                              = client_idx;

   new_memorymap_hashnode_ptr->client_token       = client_idx + 1;/* To avoid 0 as key */
   new_memorymap_hashnode_ptr->client_ptr         = client_ptr;

   spf_hashtable_insert(&g_posal_memorymap_internal_ptr->memmap_ht, &new_memorymap_hashnode_ptr->hash_node);

   posal_mutex_unlock(posal_globalstate.mutex);

   /* return the client token to the client for future communication */
   *client_token_ptr = new_memorymap_hashnode_ptr->client_token;

#ifdef DEBUG_POSAL_MEMORYMAP
   AR_MSG(DBG_HIGH_PRIO,
          "posal_memorymap client registered and the client token is 0x%x",
          (unsigned int)*client_token_ptr);
#endif

   return AR_EOK;
}

/**
 *The lsw and msw for physical address in shm_mem_reg_ptr are overwritten
 *in audio-pkt driver. The GPR packet is snooped, the physical address (msw and lsw)
 *is read to get the file descriptor (fd), and the msw is set to 0, and lsw is set to fd
 *So in posal_memorymap_shm_mem_map(), the lsw should be treated as file descriptor for mmap.
 */
ar_result_t posal_memorymap_shm_mem_map(uint32_t                      client_token,
                                        posal_memorymap_shm_region_t *shm_mem_reg_ptr,
                                        uint16_t                      num_shm_reg,
                                        bool_t                        is_cached,
                                        bool_t                        is_offset_map,
                                        POSAL_MEMORYPOOLTYPE          pool_id,
                                        uint32_t *                    shm_mem_map_handle_ptr,
                                        POSAL_HEAP_ID                 heap_id)
{
   posal_mem_map_v2_input_args_t in_args;
   in_args.unique_shmem_id_24bit        = 0;
   in_args.client_token    = client_token;
   in_args.shm_mem_reg_ptr = shm_mem_reg_ptr;
   in_args.num_shm_reg     = num_shm_reg;
   in_args.is_cached       = is_cached;
   in_args.is_offset_map   = is_offset_map;
   in_args.pool_id         = pool_id;
   in_args.heap_id         = heap_id;

   return posal_memorymap_shm_mem_map_v2(&in_args, shm_mem_map_handle_ptr);
}

ar_result_t posal_memorymap_shm_mem_map_v2(posal_mem_map_v2_input_args_t *in_args, uint32_t *shm_mem_map_handle_ptr)
{
   uint32_t                      client_token    = in_args->client_token;
   posal_memorymap_shm_region_t *shm_mem_reg_ptr = in_args->shm_mem_reg_ptr;
   uint16_t                      num_shm_reg     = in_args->num_shm_reg;
   bool_t                        is_cached       = in_args->is_cached;
   bool_t                        is_offset_map   = in_args->is_offset_map;
   POSAL_MEMORYPOOLTYPE          pool_id         = in_args->pool_id;
   POSAL_HEAP_ID                 heap_id         = in_args->heap_id;

   // check if the shmem ID is only 24bit value
   if (0xFF000000 & in_args->unique_shmem_id_24bit)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Received invalid unique_shmem_id_24bit: 0x%lX, has to be 24bit.",
             in_args->unique_shmem_id_24bit);
      return AR_EFAILED;
   }

   // check if the unique id is valid & exists,
   if (in_args->unique_shmem_id_24bit)
   {
      uint32_t temp = 0;
      if (AR_EOK == posal_memorymap_get_mem_map_handle(client_token, in_args->unique_shmem_id_24bit, &temp))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "received invalid unique_shmem_id_24bit: %lu, already exists",
                in_args->unique_shmem_id_24bit);
         return AR_EFAILED;
      }
   }

   if ((num_shm_reg > 1) && is_offset_map)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Posal_memory_map: Cant map memory as offset mode is TRUE but num regions to map are greater than "
             "1(num_shm_reg = %lu)",
             num_shm_reg);
      return AR_EBADPARAM;
   }

   ar_result_t          result;

   /* initialize the memory map handle */
   *shm_mem_map_handle_ptr = 0;

   /************************** Validating input parameters ********************************/

   /* check if number of shared mem regions is non zero */
   if (0 >= num_shm_reg)
   {
      AR_MSG(DBG_FATAL_PRIO,
             "posal_memorymap invalid num of shm regions %d; client token = %lu, pool id = %lu ",
             num_shm_reg,
             client_token,
             pool_id);
      return AR_EBADPARAM;
   }

   /* no lock to access the Client, since the assumption is ideally client register once
    * and does not unregister. Even if it unregisters, Client must call unregister after
    * ensuring all its dynamic services have exit. */
   if (AR_EOK != (result = memorymap_util_find_client(client_token)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "posal_memorymap cannot find the (client token,status) 0x%x 0x%x)",
             (unsigned int)client_token,
             (unsigned int)result);
      return result;
   }

   /* Allocate a node for this mapping (the global linklist for a client's
    * mapped regions is organized as one-node-per-invocation) */

   posal_memorymap_node_t *mem_map_node_ptr = NULL; // Node Pointer

   /* Get the pointer to first phys region: */
   posal_memorymap_region_record_t *cont_phys_regions_ptr = NULL; // Pointer to the first phys region record in the node.

   uint32_t total_node_alloc_size =
      sizeof(posal_memorymap_node_t) + sizeof(posal_memorymap_region_record_t) * (num_shm_reg);
   // Allocate node
   if ((NULL ==
        (mem_map_node_ptr =
            (posal_memorymap_node_t *)posal_memory_aligned_malloc(total_node_alloc_size, 8, heap_id))))
   {
      AR_MSG(DBG_ERROR_PRIO, "posal_memorymap fail to allocate memory for the memory map node");
      return AR_ENOMEMORY;
   }

   // Initialize the node
   memset(mem_map_node_ptr, 0, total_node_alloc_size);

   mem_map_node_ptr->unNumContPhysReg = num_shm_reg;
   mem_map_node_ptr->MemPool          = pool_id;
   // Bookmark the first phys region:
   cont_phys_regions_ptr =
      (posal_memorymap_region_record_t *)((uint8_t *)mem_map_node_ptr + sizeof(posal_memorymap_node_t));
   // Copy addr and size to the record in the node.
   /* Initialize records for each region */

   posal_memorymap_shm_map_hashnode_t *new_shm_map_hashnode_ptr =
      posal_memory_malloc(sizeof(posal_memorymap_shm_map_hashnode_t), POSAL_HEAP_DEFAULT);
   if (NULL == new_shm_map_hashnode_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "posal_memorymap: Failed to allocate memory for shm hash node.");
      return AR_ENOMEMORY;
   }
   memset(new_shm_map_hashnode_ptr, 0, sizeof(new_shm_map_hashnode_ptr));

#ifdef POSAL_MMAP_VFIO
   new_shm_map_hashnode_ptr->hash_node.key_ptr  = &new_shm_map_hashnode_ptr->unique_shmem_id_24bit;
   new_shm_map_hashnode_ptr->hash_node.key_size = sizeof(new_shm_map_hashnode_ptr->unique_shmem_id_24bit);
   new_shm_map_hashnode_ptr->hash_node.next_ptr = NULL;

   new_shm_map_hashnode_ptr->shm_node = mem_map_node_ptr;

   // if client has assigned a shmem id that becomes the key for the shm hash node, else the key is
   if (in_args->unique_shmem_id_24bit)
   {
      mem_map_node_ptr->shmem_id                      = in_args->unique_shmem_id_24bit;
      new_shm_map_hashnode_ptr->unique_shmem_id_24bit = in_args->unique_shmem_id_24bit;
   }
   else
   {
      mem_map_node_ptr->shmem_id                      = (uint32_t)mem_map_node_ptr;
      new_shm_map_hashnode_ptr->unique_shmem_id_24bit = (uint32_t)mem_map_node_ptr;
   }

   /* Create records for each region */
   for (int idx = 0; idx < num_shm_reg; ++idx)
   {
      cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.lsw = shm_mem_reg_ptr[idx].shm_addr_lsw;
      cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.msw = shm_mem_reg_ptr[idx].shm_addr_msw;
      cont_phys_regions_ptr[idx].mem_size                  = shm_mem_reg_ptr[idx].mem_size;

      cont_phys_regions_ptr[idx].virt_addr_ptr = plat_vfio_map_mem(&g_pvfio, 0);
      if (cont_phys_regions_ptr[idx].virt_addr_ptr == MAP_FAILED)
      {
         AR_MSG(DBG_FATAL_PRIO,
                "posal_memorymap_shm_mem_map Failed! (PA 0x%X%8X, size %d)",
                cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.msw,
                cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.lsw,
                cont_phys_regions_ptr[idx].mem_size);
         for (uint16_t j = 0; j < idx; ++j)
         {
            plat_vfio_unmap_mem(&g_pvfio, cont_phys_regions_ptr[j].virt_addr_ptr ,0);
         }

         /* Free the memory allocated node. */
         posal_memory_aligned_free(mem_map_node_ptr);
         posal_memory_free(new_shm_map_hashnode_ptr);
         return AR_ENOMEMORY;
      }
   }
#else

   new_shm_map_hashnode_ptr->hash_node.key_ptr  = &new_shm_map_hashnode_ptr->unique_shmem_id_24bit;
   new_shm_map_hashnode_ptr->hash_node.key_size = sizeof(new_shm_map_hashnode_ptr->unique_shmem_id_24bit);
   new_shm_map_hashnode_ptr->hash_node.next_ptr = NULL;

   new_shm_map_hashnode_ptr->shm_node = mem_map_node_ptr;

   // if client has assigned a shmem id that becomes the key for the shm hash node, else the key is "fd" which was
   // passed in the shm addr lsw field.
   if (in_args->unique_shmem_id_24bit)
   {
      mem_map_node_ptr->shmem_id                      = in_args->unique_shmem_id_24bit;
      new_shm_map_hashnode_ptr->unique_shmem_id_24bit = in_args->unique_shmem_id_24bit;
   }
   else
   {
      mem_map_node_ptr->shmem_id                      = (uint32_t)shm_mem_reg_ptr[0].shm_addr_lsw;
      new_shm_map_hashnode_ptr->unique_shmem_id_24bit = (uint32_t)shm_mem_reg_ptr[0].shm_addr_lsw;
   }

   /* Create records for each region */
   for (int idx = 0; idx < num_shm_reg; ++idx)
   {
      cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.lsw = shm_mem_reg_ptr[idx].shm_addr_lsw;
      cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.msw = shm_mem_reg_ptr[idx].shm_addr_msw;
      cont_phys_regions_ptr[idx].mem_size                  = shm_mem_reg_ptr[idx].mem_size;

#ifdef POSAL_MMAP_EXTN
      uint64_t phy_addr_64bits = ((uint64_t)cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.msw << 32)
         | (cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.lsw);
      void* mmap_va_addr = mmap(NULL, cont_phys_regions_ptr[idx].mem_size,
                                PROT_READ | PROT_WRITE, MAP_SHARED,
                                NOFD, (off_t)(phy_addr_64bits));
      //Ideally mmap_va_addr got from above line should be used. It doesnt seem to work
      //For now - extern the va addr in the osal and get it working for QNX.
      //TODO: To find the proper solution without externing the mdf_mem_base_va_addr
      cont_phys_regions_ptr[idx].virt_addr_ptr = mdf_mem_base_va_addr;
#else
      cont_phys_regions_ptr[idx].virt_addr_ptr  = mmap (NULL, cont_phys_regions_ptr[idx].mem_size,
                        PROT_READ | PROT_WRITE, MAP_SHARED,
                        cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.lsw, 0);
#endif /* POSAL_MMAP_EXTN */

      if (cont_phys_regions_ptr[idx].virt_addr_ptr == MAP_FAILED)
      {
         AR_MSG(DBG_FATAL_PRIO,
                "posal_memorymap_shm_mem_map Failed! (PA 0x%X%8X, size %d). err=%s",
                cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.msw,
                cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.lsw,
                cont_phys_regions_ptr[idx].mem_size,
                strerror(errno));

         for (uint16_t j = 0; j < idx; ++j)
         {
            munmap(cont_phys_regions_ptr[j].virt_addr_ptr, cont_phys_regions_ptr[j].mem_size);
         }
         /* Free the memory allocated node. */
         posal_memory_aligned_free(mem_map_node_ptr);
         posal_memory_free(new_shm_map_hashnode_ptr);
         return AR_ENOMEMORY;
      }
   }
#endif /* POSAL_MMAP_VFIO */

   spf_hashtable_insert(&g_posal_memorymap_internal_ptr->shmmap_ht, &new_shm_map_hashnode_ptr->hash_node);

   /* return mem map handle pointer */
   *shm_mem_map_handle_ptr = new_shm_map_hashnode_ptr->unique_shmem_id_24bit;

   /* Set the mapping mode  */
   if (is_offset_map)
   {
      mem_map_node_ptr->mapping_mode = POSAL_MEMORYMAP_PHYSICAL_OFFSET_MAPPING;
   }
   else
   {
      mem_map_node_ptr->mapping_mode = POSAL_MEMORYMAP_PHYSICAL_ADDR_MAPPING;
   }

   /* add this memory map to the client */
   memorymap_util_add_mem_map_node_to_client(client_token, mem_map_node_ptr);

#ifdef DEBUG_POSAL_MEMORYMAP
   AR_MSG(DBG_HIGH_PRIO,
          "posal_memorymap successfully mapped all regions and added to client's list "
          "(token,ar_handle,num of regions, mapping mode, virt addr mapped) "
          "= (0x%x,0x%x,%d, %d, 0x%lx)",
          (unsigned int)client_token,
          (unsigned int)mem_map_node_ptr,
          num_shm_reg,
          mem_map_node_ptr->mapping_mode,
          cont_phys_regions_ptr[0].virt_addr_ptr);
#endif

   return AR_EOK;
}

ar_result_t posal_memorymap_virtaddr_mem_map(uint32_t                      client_token,
                                             posal_memorymap_shm_region_t *shm_mem_reg_ptr,
                                             uint16_t                      num_shm_reg,
                                             bool_t                        is_cached,
                                             bool_t                        is_offset_map,
                                             POSAL_MEMORYPOOLTYPE          pool_id,
                                             uint32_t *                    shm_mem_map_handle_ptr,
                                             POSAL_HEAP_ID                 heap_id)
{
   posal_mem_map_v2_input_args_t in_args;
   in_args.unique_shmem_id_24bit        = 0;
   in_args.client_token    = client_token;
   in_args.shm_mem_reg_ptr = shm_mem_reg_ptr;
   in_args.num_shm_reg     = num_shm_reg;
   in_args.is_cached       = is_cached;
   in_args.is_offset_map   = is_offset_map;
   in_args.pool_id         = pool_id;
   in_args.heap_id         = heap_id;

   return posal_memorymap_virtaddr_mem_map_v2(&in_args, shm_mem_map_handle_ptr);
}

ar_result_t posal_memorymap_virtaddr_mem_map_v2(posal_mem_map_v2_input_args_t *in_args, uint32_t *shm_mem_map_handle_ptr)
{
   uint32_t                      client_token    = in_args->client_token;
   posal_memorymap_shm_region_t *shm_mem_reg_ptr = in_args->shm_mem_reg_ptr;
   uint16_t                      num_shm_reg     = in_args->num_shm_reg;
   bool_t                        is_offset_map   = in_args->is_offset_map;
   POSAL_MEMORYPOOLTYPE          pool_id         = in_args->pool_id;
   POSAL_HEAP_ID                 heap_id         = in_args->heap_id;

   // check if the shmem ID is only 24bit value
   if (0xFF000000 & in_args->unique_shmem_id_24bit)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Received invalid unique_shmem_id_24bit: 0x%lX, has to be 24bit.",
             in_args->unique_shmem_id_24bit);
      return AR_EFAILED;
   }

   if (in_args->unique_shmem_id_24bit)
   {
      uint32_t temp = 0;
      if (AR_EOK == posal_memorymap_get_mem_map_handle(client_token, in_args->unique_shmem_id_24bit, &temp))
      {
         AR_MSG(DBG_ERROR_PRIO, "received invalid unique_shmem_id_24bit: %lu, already exists", in_args->unique_shmem_id_24bit);
         return AR_EFAILED;
      }
   }

   ar_result_t          rc;
   POSAL_MEMORYPOOLTYPE mem_pool_id = pool_id;

   /* initialize the return handle*/
   *shm_mem_map_handle_ptr = 0;

   /************************** Validating input parameters ********************************/

   /* check if number of shared mem regions is non zero */
   if (0 == num_shm_reg)
   {
      AR_MSG(DBG_FATAL_PRIO,
             "posal_memorymap invalid num of shm regions %d; client token %lu, pool ID %lu",
             num_shm_reg,
             client_token,
             pool_id);
      return AR_EBADPARAM;
   }

   /* no lock to access the Client, since the assumption is ideally client register once and does not unregister.
    * Even if it unregister, Client must call unregister after ensuring all its dynamic services have exit. */
   if (AR_EOK != (rc = memorymap_util_find_client(client_token)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "posal_memorymap cannot find the (client token,status) 0x%x 0x%x)",
             (unsigned int)client_token,
             rc);
      return rc;
   }


   /* Allocate a node for this mapping (the global linklist for a client's
    * mapped regions is organized as one-node-per-invocation) */

   posal_memorymap_node_t *mem_map_node_ptr = NULL; // Node Pointer

   /* Get the pointer to first phys region: */
   posal_memorymap_region_record_t *cont_phys_regions_ptr = NULL; // Pointer to the first phys region record in the node.

   uint32_t total_node_alloc_size =
      sizeof(posal_memorymap_node_t) + sizeof(posal_memorymap_region_record_t) * (num_shm_reg);
   // Allocate node
   if ((NULL ==
        (mem_map_node_ptr =
            (posal_memorymap_node_t *)posal_memory_aligned_malloc(total_node_alloc_size, 8, heap_id))))
   {
      AR_MSG(DBG_ERROR_PRIO, "posal_memorymap fail to allocate memory for the memory map node");
      return AR_ENOMEMORY;
   }

   // Initialize the node
   memset(mem_map_node_ptr, 0, total_node_alloc_size);
   mem_map_node_ptr->unNumContPhysReg = num_shm_reg;
   mem_map_node_ptr->MemPool          = pool_id;
   // Bookmark the first phys region:
   cont_phys_regions_ptr =
      (posal_memorymap_region_record_t *)((uint8_t *)mem_map_node_ptr + sizeof(posal_memorymap_node_t));
   // Copy addr and size to the record in the node.
   /* Initialize records for each region */

   posal_memorymap_shm_map_hashnode_t *new_shm_map_hashnode_ptr =
      posal_memory_malloc(sizeof(posal_memorymap_shm_map_hashnode_t), POSAL_HEAP_DEFAULT);
   if (NULL == new_shm_map_hashnode_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "posal_memorymap: Failed to allocate memory for shm hash node.");
      return AR_ENOMEMORY;
   }
   memset(new_shm_map_hashnode_ptr, 0, sizeof(new_shm_map_hashnode_ptr));

   new_shm_map_hashnode_ptr->hash_node.key_ptr  = &new_shm_map_hashnode_ptr->unique_shmem_id_24bit;
   new_shm_map_hashnode_ptr->hash_node.key_size = sizeof(new_shm_map_hashnode_ptr->unique_shmem_id_24bit);
   new_shm_map_hashnode_ptr->hash_node.next_ptr = NULL;

   if (in_args->unique_shmem_id_24bit)
   {
      mem_map_node_ptr->shmem_id                      = in_args->unique_shmem_id_24bit;
      new_shm_map_hashnode_ptr->unique_shmem_id_24bit = in_args->unique_shmem_id_24bit;
   }
   else
   {
      mem_map_node_ptr->shmem_id                      = (uint32_t)mem_map_node_ptr;
      new_shm_map_hashnode_ptr->unique_shmem_id_24bit = (uint32_t)mem_map_node_ptr;
   }

   new_shm_map_hashnode_ptr->shm_node = mem_map_node_ptr;

   /* Create records for each region */
   for (int idx = 0; idx < num_shm_reg; ++idx)
   {
      cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.lsw = shm_mem_reg_ptr[idx].shm_addr_lsw;
      cont_phys_regions_ptr[idx].shm_addr.mem_addr_32b.msw = shm_mem_reg_ptr[idx].shm_addr_msw;
      cont_phys_regions_ptr[idx].mem_size                  = shm_mem_reg_ptr[idx].mem_size;

      cont_phys_regions_ptr[idx].virt_addr_ptr = (void *)((((uint64_t)(shm_mem_reg_ptr[idx].shm_addr_msw)) << 32) | (shm_mem_reg_ptr[idx].shm_addr_lsw));
   }

   spf_hashtable_insert(&g_posal_memorymap_internal_ptr->shmmap_ht, &new_shm_map_hashnode_ptr->hash_node);

   /* return mem map handle pointer */
   *shm_mem_map_handle_ptr = new_shm_map_hashnode_ptr->unique_shmem_id_24bit;

   /* Set the mapping mode  */
   if (is_offset_map)
   {
      mem_map_node_ptr->mapping_mode = POSAL_MEMORYMAP_VIRTUAL_OFFSET_MAPPING;
   }
   else
   {
      mem_map_node_ptr->mapping_mode = POSAL_MEMORYMAP_VIRTUAL_ADDR_MAPPING;
   }

   /* add this memory map to the client */
   memorymap_util_add_mem_map_node_to_client(client_token, mem_map_node_ptr);

#ifdef DEBUG_POSAL_MEMORYMAP
   AR_MSG(DBG_HIGH_PRIO,
          "posal_memorymap successfully mapped all virt regions and added'"
          "to client's list (token,ar_handle,num of regions) = (0x%x,0x%x,%d)",
          (unsigned int)client_token,
          (unsigned int)mem_map_node_ptr,
          num_shm_reg);
#endif

   return AR_EOK;
}

ar_result_t posal_memorymap_get_mapping_mode(uint32_t                        client_token,
                                             uint32_t                        shm_mem_map_handle,
                                             posal_memorymap_mapping_mode_t *mapping_mode_ptr)
{
   ar_result_t result;

   if (NULL == mapping_mode_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "posal_memorymap mapping_mode_ptr is null while getting the mapping mode");
      return AR_EBADPARAM;
   }

   /*Get mapping mode from the common cmd handler */
   result =
      memorymap_util_cmd_handler(client_token, shm_mem_map_handle, CMD_GET_MAPPING_MODE, (void *)mapping_mode_ptr);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_HIGH_PRIO, "posal_memorymap_get_mapping_mode failed 0x%x", result);
      return result;
   }

#ifdef DEBUG_POSAL_MEMORYMAP
   AR_MSG(DBG_HIGH_PRIO,
          "posal_memorymap posal_memorymap_get_mapping mode"
          "(mem handle, mapping mode, rc)->(0x%x,%d,0x%x)",
          (unsigned int)shm_mem_map_handle,
          (unsigned int)*mapping_mode_ptr,
          result);
#endif

   return AR_EOK;
}

ar_result_t posal_memorymap_shm_mem_unmap(uint32_t client_token, uint32_t shm_mem_map_handle)
{
   ar_result_t result = AR_EOK;

   /* Use utility command handler to unmap the region */
   result = memorymap_util_cmd_handler(client_token, shm_mem_map_handle, CMD_SHM_MEM_UNMAP, NULL);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_HIGH_PRIO, "posal_memorymap_shm_mem_unmap failed 0x%x", result);
      return result;
   }

#ifdef DEBUG_POSAL_MEMORYMAP
   AR_MSG(DBG_HIGH_PRIO,
          "posal_memorymap memory unmap (client token,ar_handle,status)->(0x%x,0x%x,0x%x)",
          (unsigned int)client_token,
          (unsigned int)shm_mem_map_handle,
          result);
#endif
   return result;
}

ar_result_t posal_memorymap_util_region_delete(posal_memorymap_node_t *mem_map_node_ptr, bool_t enable_debug_logs)
{
   return AR_EOK;
}

ar_result_t posal_memorymap_unmap_all(uint32_t client_token)
{
   ar_result_t rc;

   /* no lock to access the Client, since the assumption is ideally client register once and does not unregister.
    * Even if it unregister, Client must call unregister after ensuring all its dynamic services have exit. */
   if (AR_EOK != (rc = memorymap_util_find_client(client_token)))
   {

      return rc;
   }

   posal_memorymap_hashnode_t *memorymap_hashnode_ptr =
      (posal_memorymap_hashnode_t *)spf_hashtable_find(&g_posal_memorymap_internal_ptr->memmap_ht, &client_token, sizeof(client_token));

   posal_memorymap_client_t *client_ptr = memorymap_hashnode_ptr->client_ptr;

   /* lock the access of the list */
   posal_mutex_lock(client_ptr->mClientMutex);

   /*Get the memory map node list head pointer */
   posal_memorymap_node_t *current_mem_map_node_ptr = client_ptr->pMemMapListNode;
   posal_memorymap_node_t *next_mem_map_node_ptr = NULL;

   /* Un-map all regions of the client one at a time. */
   while (current_mem_map_node_ptr)
   {
      if (0 != current_mem_map_node_ptr->ref_count)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "memorymap_util_destory_node, cannot unmap the node(client token,ar_handle,ref count)->(0x%x,0x%x,0x%x)",
                (unsigned int)current_mem_map_node_ptr,
                client_ptr,
                current_mem_map_node_ptr->ref_count);
         rc = AR_ENOTREADY;
         goto shm_mem_unmap_all_bail_out;
      }

      /* Bookmark the first phys region: */
      posal_memorymap_region_record_t *cont_phys_regions_ptr =
         (posal_memorymap_region_record_t *)((uint8_t *)current_mem_map_node_ptr + sizeof(posal_memorymap_node_t));

#ifdef POSAL_MMAP_VFIO
      for (int i = 0; i < current_mem_map_node_ptr->unNumContPhysReg; ++i)
      {
         plat_vfio_unmap_mem(&g_pvfio, cont_phys_regions_ptr[i].virt_addr_ptr ,0);
      }
#else
#ifdef POSAL_MMAP_EXTN
#else
      for (int i = 0; i < current_mem_map_node_ptr->unNumContPhysReg; ++i)
      {
         munmap(cont_phys_regions_ptr[i].virt_addr_ptr, cont_phys_regions_ptr[i].mem_size);
      }
#endif /* POSAL_MMAP_EXTN */
#endif /* POSAL_MMAP_VFIO */

      /* update the Global state */
      posal_atomic_subtract((posal_globalstate.nMemRegions), current_mem_map_node_ptr->unNumContPhysReg);

      /*Move the current pointer to next pointer */
      next_mem_map_node_ptr = current_mem_map_node_ptr->pNext;

      /* free up the resources */
      posal_memory_aligned_free(current_mem_map_node_ptr);
      current_mem_map_node_ptr = next_mem_map_node_ptr;

   }
   client_ptr->pMemMapListNode = NULL;

/* Unlock the access of the list */
shm_mem_unmap_all_bail_out:
   posal_mutex_unlock(client_ptr->mClientMutex);

#ifdef DEBUG_POSAL_MEMORYMAP
   AR_MSG(DBG_HIGH_PRIO,
          "posal_memorymap memory unmap all (client token,status)->(0x%x,0x%x)",
          (unsigned int)client_token,
          rc);
#endif

   return AR_EOK;
}

ar_result_t posal_memorymap_unregister(uint32_t client_token)
{
   ar_result_t result = AR_EOK;

   /* Un map all regions in the client */
   if (AR_EOK != (result = posal_memorymap_unmap_all(client_token)))
   {
#ifdef DEBUG_POSAL_MEMORYMAP
      AR_MSG(DBG_ERROR_PRIO,
             "posal_memorymap failed to unmap all regions of client (token,0x%x)",
             (unsigned int)client_token);
#endif
   }

   /*Still proceed.. if the above step fails, it introduces memory leaks. */
   posal_mutex_lock(posal_globalstate.mutex);

   posal_memorymap_hashnode_t *memorymap_hashnode_ptr =
      (posal_memorymap_hashnode_t *)spf_hashtable_find(&g_posal_memorymap_internal_ptr->memmap_ht, &client_token, sizeof(client_token));

   posal_memorymap_client_t *delete_client_ptr = memorymap_hashnode_ptr->client_ptr;

   /* Check if the client handle is in the table  */
   if (posal_globalstate.mem_map_client_list[delete_client_ptr->client_id] == delete_client_ptr)
   {
      /* destroy the client mutex before freeing the client. */
      posal_mutex_destroy(&(delete_client_ptr->mClientMutex));

      /* Set the client ptr in the table to NULL */
      posal_globalstate.mem_map_client_list[delete_client_ptr->client_id] = NULL;

      /* Decrement the global client count */
      posal_globalstate.num_registered_memmap_clients--;

      /*free the memory */
      posal_memory_free(delete_client_ptr);
   }
   else
   {
      result = AR_EHANDLE;
   }

   /* unlock the global mutex */
   posal_mutex_unlock(posal_globalstate.mutex);

#ifdef DEBUG_POSAL_MEMORYMAP
   AR_MSG(DBG_HIGH_PRIO,
          "posal_memorymap unregister client (client token,status)->(0x%x,0x%x)",
          (unsigned int)client_token,
          result);
#endif

   return result;
}

/*query has to be for offset mode*/
ar_result_t posal_memorymap_get_shmm_handle_and_offset_from_va_offset_map(uint32_t  client_token,
                                                                          uint32_t  va,
                                                                          uint32_t *mem_handle_ptr,
                                                                          uint32_t *offset_ptr)
{
   ar_result_t result = AR_EOK;

   return result;
}

ar_result_t posal_memorymap_get_virtual_addr_from_shm_handle_v2(uint32_t  client_token,
                                                             uint32_t  shm_mem_map_handle,
                                                             uint32_t  shm_addr_lsw,
                                                             uint32_t  shm_addr_msw,
                                                             uint32_t  reg_size,
                                                             bool_t    is_ref_counted,
                                                             void      *virt_addr)
{
   uint64_t *virt_addr_ptr = (uint64_t *)virt_addr;

   ar_result_t                         rc;
   mem_region_attrib_from_shmm_handle_struct_t mra_struct;

   /* Get the memory attributes for the input physical address */
   posal_memorymap_mem_region_attrib_t mem_reg_attrib = {0};

   /*prepare arguments for the common command handler */
   mra_struct.shm_addr_lsw = shm_addr_lsw;
   mra_struct.shm_addr_msw = shm_addr_msw;
   mra_struct.req_virt_addr = NULL;
   mra_struct.mem_reg_attrib_ptr = &mem_reg_attrib;
   mra_struct.is_ref_counted = is_ref_counted;

   rc = memorymap_util_cmd_handler(client_token, shm_mem_map_handle, CMD_GET_SHM_ATTRIB, (void *)&mra_struct);
   if (AR_EOK != rc)
   {
      AR_MSG(DBG_HIGH_PRIO, "posal_memorymap_get_virtual_addr_from_shm_handle_v2 failed ");
      return rc;
   }

   *virt_addr_ptr = (uint64_t)mra_struct.req_virt_addr;

#ifdef DEBUG_POSAL_MEMORYMAP
   AR_MSG(DBG_HIGH_PRIO,
          " posal_memorymap_get_virtual_addr_from_shmm_handle (paddr msw,paddr lsw, vaddr)->(0x%x,0x%x,0x%x)",
          (unsigned int)shm_addr_msw,
          (unsigned int)shm_addr_lsw,
          (uint64_t)*virt_addr_ptr);
#endif

   return rc;
}

ar_result_t posal_memorymap_get_mem_region_attrib_from_shmm_handle(
   uint32_t                             client_token,
   uint32_t                             shm_mem_map_handle,
   uint32_t                             shm_addr_lsw,
   uint32_t                             shm_addr_msw,
   bool_t                               is_ref_counted,
   posal_memorymap_mem_region_attrib_t *mem_reg_attrib_ptr)
{
   ar_result_t rc;
   mem_region_attrib_from_shmm_handle_struct_t mra_struct;

   /*prepare arguments for the common command handler */
   mra_struct.shm_addr_lsw = shm_addr_lsw;
   mra_struct.shm_addr_msw = shm_addr_msw;
   mra_struct.req_virt_addr = NULL;
   mra_struct.mem_reg_attrib_ptr = mem_reg_attrib_ptr;
   mra_struct.is_ref_counted = is_ref_counted;

   rc = memorymap_util_cmd_handler(client_token, shm_mem_map_handle, CMD_GET_SHM_ATTRIB, (void *)&mra_struct);
   if (AR_EOK != rc)
   {
      AR_MSG(DBG_HIGH_PRIO, "posal_memorymap_get_mem_region_attrib_from_shmm_handle failed ");
      return rc;
   }

   return rc;
}

static ar_result_t memorymap_util_find_client(uint32_t client_token)
{
   ar_result_t               result           = AR_EHANDLE;

   POSAL_ASSERT(client_token);

   posal_memorymap_hashnode_t *memorymap_hashnode_ptr =
      (posal_memorymap_hashnode_t *)spf_hashtable_find(&g_posal_memorymap_internal_ptr->memmap_ht, &client_token, sizeof(client_token));

   posal_memorymap_client_t *found_client_ptr = memorymap_hashnode_ptr->client_ptr;

   /* enter the lock */
   posal_mutex_lock(posal_globalstate.mutex);

   if (found_client_ptr == posal_globalstate.mem_map_client_list[found_client_ptr->client_id])
   {
#ifdef DEBUG_POSAL_MEMORYMAP
      AR_MSG(DBG_HIGH_PRIO, "posal_memorymap, client found client_ptr: 0x%x", (uint32_t)found_client_ptr);
#endif
      result = AR_EOK;
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO,
             "posal_memorymap, client not found (client token,status)->(0x%x,0x%x)",
             (unsigned int)client_token,
             result);
   }
   /* unlock the global mutex */
   posal_mutex_unlock(posal_globalstate.mutex);

   return result;
}

ar_result_t posal_memorymap_shm_incr_refcount(uint32_t client_token, uint32_t shm_mem_map_handle)
{
   return memorymap_util_shm_update_refcount(client_token, shm_mem_map_handle, 1);
}

ar_result_t posal_memorymap_shm_decr_refcount(uint32_t client_token, uint32_t shm_mem_map_handle)
{
   return memorymap_util_shm_update_refcount(client_token, shm_mem_map_handle, -1);
}

static ar_result_t memorymap_util_shm_update_refcount(uint32_t client_token,
                                                      uint32_t shm_mem_map_handle,
                                                      int16_t  count_value)
{
   ar_result_t result;
   result = memorymap_util_cmd_handler(client_token, shm_mem_map_handle, CMD_UPDATE_REF_COUNT, &count_value);
   if (AR_EOK != result)
   {
#ifdef DEBUG_POSAL_MEMORYMAP
      AR_MSG(DBG_HIGH_PRIO, "posal_memorymap_shm_update_refcount failed 0x%x", (unsigned int)client_token);
#endif
      return result;
   }
   return result;
}

static void memorymap_util_add_mem_map_node_to_client(uint32_t client_token, posal_memorymap_node_t *mem_map_node_ptr)
{

   posal_memorymap_hashnode_t *memorymap_hashnode_ptr =
      (posal_memorymap_hashnode_t *)spf_hashtable_find(&g_posal_memorymap_internal_ptr->memmap_ht, &client_token, sizeof(client_token));

   posal_memorymap_client_t *c_ptr = memorymap_hashnode_ptr->client_ptr;

   /* add this node list of regions to the client */
   posal_mutex_lock(c_ptr->mClientMutex);

   mem_map_node_ptr->pNext = c_ptr->pMemMapListNode;
   c_ptr->pMemMapListNode  = mem_map_node_ptr;

   posal_mutex_unlock(c_ptr->mClientMutex);

   /* Atomic increment of the global mem region counter */
   posal_atomic_add((posal_globalstate.nMemRegions), (uint32_t)mem_map_node_ptr->unNumContPhysReg);
}

static ar_result_t memorymap_util_cmd_handler(uint32_t client_token,
                                              uint32_t shm_mem_map_handle,
                                              uint32_t command,
                                              void *   args)
{
   ar_result_t rc = AR_EOK;

   /* no lock to access the Client, since the assumption is ideally client register once and does not unregister.
    * Even if it unregister, Client must call unregister after ensuring all its dynamic services have exit. */
   if (AR_EOK != (rc = memorymap_util_find_client(client_token)) || (0 == shm_mem_map_handle))
   {
#ifdef DEBUG_POSAL_MEMORYMAP
      AR_MSG(DBG_HIGH_PRIO, "memorymap_util_cmd_handler failed. Invalid input params.");
#endif
      return rc;
   }

   posal_memorymap_hashnode_t *memorymap_hashnode_ptr =
      (posal_memorymap_hashnode_t *)spf_hashtable_find(&g_posal_memorymap_internal_ptr->memmap_ht, &client_token, sizeof(client_token));

   posal_memorymap_client_t *client_ptr = memorymap_hashnode_ptr->client_ptr;

   /* lock the access of the list */
   posal_mutex_lock(client_ptr->mClientMutex);

   posal_memorymap_node_t *mem_map_node_ptr       = client_ptr->pMemMapListNode;
   posal_memorymap_node_t *prev_node_ptr          = NULL;
   posal_memorymap_node_t *found_mem_map_node_ptr = NULL;
   posal_memorymap_node_t *handle_mem_map_node_ptr = NULL;

   posal_memorymap_shm_map_hashnode_t *shm_map_hashnode_ptr =
      (posal_memorymap_shm_map_hashnode_t *)spf_hashtable_find(&g_posal_memorymap_internal_ptr->shmmap_ht,
                                                               &shm_mem_map_handle,
                                                               sizeof(shm_mem_map_handle));

   handle_mem_map_node_ptr = shm_map_hashnode_ptr->shm_node;

   /* search and compare with the first physical region record in all the memory mapping nodes of the client. */
   while (mem_map_node_ptr)
   {
      if (handle_mem_map_node_ptr->shmem_id == mem_map_node_ptr->shmem_id)
      {
         found_mem_map_node_ptr = handle_mem_map_node_ptr;
         break;
      }

      /*store current as prev and move current to next */
      prev_node_ptr    = mem_map_node_ptr;
      mem_map_node_ptr = mem_map_node_ptr->pNext;
   }

   /* delete the qube memory region and regardless of the result of memorymap_util_region_delete()
    * The node must be deleted from the list */
   if (NULL == found_mem_map_node_ptr)
   {
      /* Could not find it and hence return unsupported. */
      AR_MSG(DBG_ERROR_PRIO,
             "posal_memorymap memmap handle not found (client token,ar_handle,status)->(0x%x,0x%x,0x%x)",
             (unsigned int)client_token,
             (unsigned int)shm_mem_map_handle,
             rc);
      rc = AR_EBADPARAM;
      goto _bailout_1;
   }

   switch (command)
   {
   case CMD_UPDATE_REF_COUNT:
   {
      /* Get the count from the argument*/
      int16_t *count_ptr = (int16_t *)args;

      /* Increment the count on found memory map node */
      found_mem_map_node_ptr->ref_count += *count_ptr;

#ifdef DEBUG_POSAL_MEMORYMAP
      AR_MSG(DBG_HIGH_PRIO,
             "posal_memorymap update ref count (client token,ar_handle,status)->(0x%x,0x%x,0x%x)",
             (unsigned int)client_token,
             (unsigned int)shm_mem_map_handle,
             rc);
#endif
      break;
   }
   case CMD_GET_MAPPING_MODE:
   {
      /* Get the count from the argument*/
      posal_memorymap_mapping_mode_t *mapping_mode_ptr = (posal_memorymap_mapping_mode_t *)args;

      /* update the mapping mode at the return address  */
      *mapping_mode_ptr = (posal_memorymap_mapping_mode_t)found_mem_map_node_ptr->mapping_mode;
      break;
   }
   case CMD_GET_SHM_ATTRIB:
   {
      uint32_t virt_addr = 0;

      mem_region_attrib_from_shmm_handle_struct_t *mra_struct_ptr = (mem_region_attrib_from_shmm_handle_struct_t *)args;

      uint32_t shm_addr_lsw    = mra_struct_ptr->shm_addr_lsw;
      uint32_t shm_addr_msw    = mra_struct_ptr->shm_addr_msw;
      bool_t   is_ref_counted  =  (mra_struct_ptr->is_ref_counted > 0) ? TRUE: FALSE;
      uint64_t phy_addr_64bits = ((uint64_t)shm_addr_msw << 32) | (shm_addr_lsw);
      // the above variable will hold the offset in case of offset mode and address otherwise

      posal_memorymap_mem_region_attrib_t *mem_reg_attrib_ptr = mra_struct_ptr->mem_reg_attrib_ptr;
      // check if the mapping mode is offset based
      if ((POSAL_MEMORYMAP_PHYSICAL_OFFSET_MAPPING == found_mem_map_node_ptr->mapping_mode) ||
          (POSAL_MEMORYMAP_VIRTUAL_OFFSET_MAPPING == found_mem_map_node_ptr->mapping_mode))
      {
#ifdef DEBUG_POSAL_MEMORYMAP
         AR_MSG(DBG_HIGH_PRIO, "offset mapping mode query");
#endif
         /* Book mark the first (and only) region: */
         posal_memorymap_region_record_t *cont_phy_regions_ptr =
            (posal_memorymap_region_record_t *)((uint8_t *)found_mem_map_node_ptr + sizeof(posal_memorymap_node_t));

         uint8_t* base_virtual_addr = (uint8_t*) cont_phy_regions_ptr[0].virt_addr_ptr;

         if ((base_virtual_addr + phy_addr_64bits) >
             (base_virtual_addr + cont_phy_regions_ptr[0].mem_size))
         {
            AR_MSG(DBG_ERROR_PRIO, "offset %lu is out of bounds", phy_addr_64bits);

            rc = AR_EBADPARAM;
            goto _bailout_1;
         }

         mra_struct_ptr->req_virt_addr                 = (void *)(base_virtual_addr + phy_addr_64bits);
         mem_reg_attrib_ptr->req_virt_adrr             = phy_addr_64bits;
         mem_reg_attrib_ptr->base_phy_addr_lsw         = cont_phy_regions_ptr[0].shm_addr.mem_addr_32b.lsw;
         mem_reg_attrib_ptr->base_phy_addr_msw         = cont_phy_regions_ptr[0].shm_addr.mem_addr_32b.msw;
         mem_reg_attrib_ptr->base_virt_addr            = cont_phy_regions_ptr[0].virt_addr;
         mem_reg_attrib_ptr->mem_reg_size              = cont_phy_regions_ptr[0].mem_size;
         mem_reg_attrib_ptr->rem_reg_size              =
               (cont_phy_regions_ptr[0].virt_addr + cont_phy_regions_ptr[0].mem_size) - mem_reg_attrib_ptr->req_virt_adrr;
         virt_addr                                     = mem_reg_attrib_ptr->req_virt_adrr;
      }
      else if ((POSAL_MEMORYMAP_VIRTUAL_ADDR_MAPPING == found_mem_map_node_ptr->mapping_mode) ||
          (POSAL_MEMORYMAP_PHYSICAL_ADDR_MAPPING == found_mem_map_node_ptr->mapping_mode))
      {

         /* Book mark the first (and only) region: */
         posal_memorymap_region_record_t *cont_phy_regions_ptr =
            (posal_memorymap_region_record_t *)((uint8_t *)found_mem_map_node_ptr + sizeof(posal_memorymap_node_t));

         uint8_t* base_virtual_addr = (uint8_t*) cont_phy_regions_ptr[0].virt_addr_ptr;

#ifdef DEBUG_POSAL_MEMORYMAP
         AR_MSG(DBG_HIGH_PRIO, "virt addr mapping mode query: virt addr mapped 0x%lx, incoming virt addr 0x%lx", cont_phy_regions_ptr[0].virt_addr_ptr, phy_addr_64bits);
#endif

         // when GSL and SPF-on-ARM are in same processor, virtual addr = lsw/msw being passed = so called phy_addr_64bits.
         mra_struct_ptr->req_virt_addr                 = (void *)phy_addr_64bits;
         mem_reg_attrib_ptr->req_virt_adrr             = phy_addr_64bits;
         mem_reg_attrib_ptr->base_phy_addr_lsw         = cont_phy_regions_ptr[0].shm_addr.mem_addr_32b.lsw;
         mem_reg_attrib_ptr->base_phy_addr_msw         = cont_phy_regions_ptr[0].shm_addr.mem_addr_32b.msw;
         mem_reg_attrib_ptr->base_virt_addr            = cont_phy_regions_ptr[0].virt_addr;
         mem_reg_attrib_ptr->mem_reg_size              = cont_phy_regions_ptr[0].mem_size;
         mem_reg_attrib_ptr->rem_reg_size              =
               (cont_phy_regions_ptr[0].virt_addr + cont_phy_regions_ptr[0].mem_size) - mem_reg_attrib_ptr->req_virt_adrr;
         virt_addr                                     = mem_reg_attrib_ptr->req_virt_adrr;
      }
      else
      {
         //TODO implementation for address mapping
         AR_MSG(DBG_ERROR_PRIO, "get virtual address not yet implemented for address mapped physical address %lu", phy_addr_64bits);
      }
      /*If the virtual address is not found, return bad param */
      if (NULL == mra_struct_ptr->req_virt_addr)
      {
         rc = AR_EBADPARAM;
         goto _bailout_1;
      }

      // Increment the ref count if needed
      if (is_ref_counted)
      {
         found_mem_map_node_ptr->ref_count++;
      }

#ifdef DEBUG_POSAL_MEMORYMAP
      AR_MSG(DBG_HIGH_PRIO,
             "posal_memorymap_get_mem_region_attrib_from_shmm_handle (paddr msw,paddr lsw, vaddr)->(0x%x,0x%x,0x%x)",
             (unsigned int)shm_addr_msw,
             (unsigned int)shm_addr_lsw,
             (unsigned int)virt_addr);
#endif
      break;
   }
   case CMD_SHM_MEM_UNMAP:
   {

      if (0 != found_mem_map_node_ptr->ref_count)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "memorymap_util_destory_node, cannot unmap the node(client token, ar_handle,ref "
                "count)->(0x%x,0x%x,0x%x)",
                (unsigned int)client_token,
                (unsigned int)shm_mem_map_handle,
                found_mem_map_node_ptr->ref_count);
         rc = AR_ENOTREADY;
         goto _bailout_1;
      }

      if ((POSAL_MEMORYMAP_PHYSICAL_ADDR_MAPPING == found_mem_map_node_ptr->mapping_mode) ||
          (POSAL_MEMORYMAP_PHYSICAL_OFFSET_MAPPING == found_mem_map_node_ptr->mapping_mode))
      {
         /* Bookmark the first phys region: */
         posal_memorymap_region_record_t *cont_phys_regions_ptr =
            (posal_memorymap_region_record_t *)((uint8_t *)found_mem_map_node_ptr + sizeof(posal_memorymap_node_t));

#ifdef POSAL_MMAP_VFIO
         for (int i = 0; i < found_mem_map_node_ptr->unNumContPhysReg; ++i)
         {
            plat_vfio_unmap_mem(&g_pvfio, cont_phys_regions_ptr[i].virt_addr_ptr ,0);
         }
#else
#ifdef POSAL_MMAP_EXTN
#else
         for (int i = 0; i < found_mem_map_node_ptr->unNumContPhysReg; ++i)
         {
            munmap(cont_phys_regions_ptr[i].virt_addr_ptr, cont_phys_regions_ptr[i].mem_size);
         }
#endif /* POSAL_MMAP_EXTN */
#endif /* POSAL_MMAP_VFIO */
      }
#ifdef DEBUG_POSAL_MEMORYMAP
      else
      {
         AR_MSG(DBG_ERROR_PRIO, "This function should not be called for mapping mode %u",mem_map_node_ptr->mapping_mode);
      }
#endif

      /* Remove the node from client nodes list */
      if (NULL == prev_node_ptr)
      {
         /*If it is first node, update the head of the list */
         client_ptr->pMemMapListNode = found_mem_map_node_ptr->pNext;
      }
      else
      {
         /* update the previous node's next pointer */
         prev_node_ptr->pNext = found_mem_map_node_ptr->pNext;
      }

      /* update the Global state */
      posal_atomic_subtract((posal_globalstate.nMemRegions), found_mem_map_node_ptr->unNumContPhysReg);

      /* free up the resources */
      posal_memory_aligned_free(found_mem_map_node_ptr);

      break;
   }
   default:
   {
      AR_MSG(DBG_ERROR_PRIO,
             "posal_memorymap meory map handle not found (client token,ar_handle,status)->(0x%x,0x%x,0x%x)",
             (unsigned int)client_token,
             (unsigned int)shm_mem_map_handle,
             rc);
      break;
   }
   }
/* Unlock the client node */
_bailout_1:
   posal_mutex_unlock(client_ptr->mClientMutex);

   return rc;
}

ar_result_t posal_memorymap_get_mem_map_handle(uint32_t  client_token,
                                               uint32_t  unique_shmem_id_24bit,
                                               uint32_t *shm_mem_map_handle_ptr)
{
   ar_result_t result = AR_EOK;
   /* no lock to access the Client, since the assumption is ideally client register once and does not unregister.
    * Even if it unregister, Client must call unregister after ensuring all its dynamic services have exit. */
   if (AR_EOK != (result = memorymap_util_find_client(client_token)) || (0 == unique_shmem_id_24bit) ||
       (NULL == shm_mem_map_handle_ptr))
   {
      AR_MSG(DBG_HIGH_PRIO, "posal_memorymap_get_mem_map_handle failed. Invalid input params.");
      return result;
   }

   posal_memorymap_hashnode_t *memorymap_hashnode_ptr =
      (posal_memorymap_hashnode_t *)spf_hashtable_find(&g_posal_memorymap_internal_ptr->memmap_ht, &client_token, sizeof(client_token));

   posal_memorymap_client_t *client_ptr = memorymap_hashnode_ptr->client_ptr;

   /* lock the access of the list */
   posal_mutex_lock(client_ptr->mClientMutex);

   posal_memorymap_node_t *mem_map_node_ptr       = client_ptr->pMemMapListNode;
   posal_memorymap_node_t *handle_mem_map_node_ptr = NULL;

   posal_memorymap_shm_map_hashnode_t *shm_map_hashnode_ptr =
      (posal_memorymap_shm_map_hashnode_t *)spf_hashtable_find(&g_posal_memorymap_internal_ptr->shmmap_ht,
                                                               &unique_shmem_id_24bit,
                                                               sizeof(unique_shmem_id_24bit));

   handle_mem_map_node_ptr = shm_map_hashnode_ptr->shm_node;

   /* search and compare with the first physical region record in all the memory mapping nodes of the client. */
   while (mem_map_node_ptr)
   {
      if (handle_mem_map_node_ptr->shmem_id == mem_map_node_ptr->shmem_id)
      {
         *shm_mem_map_handle_ptr = (uint32_t)handle_mem_map_node_ptr->shmem_id;
         return result;
      }

      /*store current as prev and move current to next */
      mem_map_node_ptr = mem_map_node_ptr->pNext;
   }

   AR_MSG(DBG_ERROR_PRIO, "posal_memorymap_get_mem_map_handle, mem_mapping is not found!");
   return AR_EBADPARAM;
}
