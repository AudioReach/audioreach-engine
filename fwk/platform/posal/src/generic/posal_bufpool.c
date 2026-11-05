/**
 * \file posal_bufpool.c
 * \brief
 *  	This file contains an buffer pool implementation for small allocations primarily for lists and queues
 *
 *
 * \copyright
 *    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *    SPDX-License-Identifier: BSD-3-Clause
 */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "posal.h"
#include "posal_bufpool_i.h"
#if defined(__hexagon__)
#include <hexagon_protos.h>
#endif

//#define DEBUG_BUFPOOL
//#define DEBUG_BUFPOOL_LOW

#define BUF_POOL_MEMLEAK_DEBUG

/* -----------------------------------------------------------------------
** Constant / Define Declarations
** ----------------------------------------------------------------------- */
/* This variable is not protected by a lock based on the assumption that pools
 * will be created statically at bootup from a single thread, and destroy will not
 * be called while any nodes are still pending */
extern posal_bufpool_pool_t *all_pools[POSAL_BUFPOOL_MAX_POOLS];

/* =======================================================================
**                          Function Definitions
** ======================================================================= */
uint32_t posal_bufpool_pool_create(uint16_t              node_size,
                                   POSAL_HEAP_ID         heap_id,
                                   uint32_t              num_arrays,
                                   posal_bufpool_align_t alignment,
                                   uint16_t              nodes_per_arr)
{
   uint32_t    i;
   ar_result_t result = AR_EOK;
   if(nodes_per_arr > POSAL_BUFPOOL_MAX_NODES)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Bufpool error: nodes per arr %lu cannot be greater than %lu",
             nodes_per_arr,
             POSAL_BUFPOOL_MAX_NODES);
      return POSAL_BUFPOOL_INVALID_HANDLE;
   }
   // find first free index
   for (i = 0; i < POSAL_BUFPOOL_MAX_POOLS; i++)
   {
      if (NULL == all_pools[i])
      {
         break;
      }
   }
   if (POSAL_BUFPOOL_MAX_POOLS == i)
   {
      AR_MSG(DBG_FATAL_PRIO, "Bufpool error: Pool creation failure, max pools already created");
      return POSAL_BUFPOOL_INVALID_HANDLE;
   }
   uint32_t              size     = (sizeof(posal_bufpool_pool_t)) + (sizeof(posal_bufpool_nodes_t) * num_arrays);
   posal_bufpool_pool_t *pool_ptr = (posal_bufpool_pool_t *)posal_memory_malloc(size, heap_id);
   if (NULL == pool_ptr)
   {
      AR_MSG(DBG_FATAL_PRIO, "Bufpool error: Pool creation failure, pool malloc failed");
      return POSAL_BUFPOOL_INVALID_HANDLE;
   }
   memset(pool_ptr, 0, size);
   pool_ptr->nodes_ptr          = (posal_bufpool_nodes_t *)(pool_ptr + 1);
   pool_ptr->heap_id            = heap_id;
   pool_ptr->node_size          = node_size;
   pool_ptr->num_of_node_arrays = num_arrays;
   pool_ptr->align_padding_size = (alignment == EIGHT_BYTE_ALIGN) ? 4 : 0;
   pool_ptr->nodes_per_arr      = nodes_per_arr;
   result                       = posal_bufpool_allocate_new_nodes_arr(pool_ptr, 0, i);
   if (AR_DID_FAIL(result))
   {
      posal_memory_free(pool_ptr);
      return POSAL_BUFPOOL_INVALID_HANDLE;
   }
   posal_mutex_create(&pool_ptr->pool_mutex, heap_id);
   all_pools[i]    = pool_ptr;
   uint32_t handle = 0;
   handle |= POSAL_BUFPOOL_HANDLE_MAGIC;
   handle |= i;
   return handle;
}

ar_result_t posal_bufpool_allocate_new_nodes_arr_util_(posal_bufpool_pool_t *pool_ptr, uint32_t index, uint32_t pool_index)
{
   uint32_t sz = ((sizeof(posal_bufpool_node_header_t) + pool_ptr->align_padding_size + pool_ptr->node_size) *
                  pool_ptr->nodes_per_arr);

   pool_ptr->nodes_ptr[index].mem_start_addr = posal_memory_malloc(sz, pool_ptr->heap_id);
   if (NULL == pool_ptr->nodes_ptr[index].mem_start_addr)
   {
      AR_MSG(DBG_FATAL_PRIO, "Bufpool error: Node array malloc failed");
      BUFPOOL_ASSERT();
      return AR_ENOMEMORY;
   }

   AR_MSG(DBG_MED_PRIO,
          "Bufpool %lu: Allocated new list index %lu, with %lu nodes, total size %lu, heap_id 0x%lx",
          pool_index,
          index,
          pool_ptr->nodes_per_arr,
          sz,
          pool_ptr->heap_id);

   pool_ptr->nodes_ptr[index].list_bitmask = 0;
   pool_ptr->allocated_nodes += pool_ptr->nodes_per_arr;
   return AR_EOK;
}

void posal_bufpool_free_nodes_arr(posal_bufpool_pool_t *pool_ptr, uint32_t index)
{
   if ((index >= pool_ptr->num_of_node_arrays) || (NULL == pool_ptr->nodes_ptr[index].mem_start_addr) ||
       (pool_ptr->nodes_ptr[index].list_bitmask))
   {
      AR_MSG(DBG_FATAL_PRIO,
             "Bufpool error: Free array failed addr 0x%lx, bitmask %ld",
             pool_ptr->nodes_ptr[index].mem_start_addr,
             pool_ptr->nodes_ptr[index].list_bitmask);
      BUFPOOL_ASSERT();
      return;
   }

   posal_memory_free(pool_ptr->nodes_ptr[index].mem_start_addr);
   pool_ptr->nodes_ptr[index].mem_start_addr = NULL;
   pool_ptr->allocated_nodes -= pool_ptr->nodes_per_arr;
   return;
}

void posal_bufpool_pool_destroy(uint32_t pool_handle)
{
   uint32_t pool_index;
   if (AR_DID_FAIL(validate_handle(pool_handle, &pool_index)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Bufpool error: Invalid handle %lu", pool_handle);
      BUFPOOL_ASSERT();
      return;
   }
   posal_bufpool_pool_t *pool_ptr = all_pools[pool_index];
   posal_mutex_lock(pool_ptr->pool_mutex);
   AR_MSG(DBG_HIGH_PRIO, "Bufpool: Destroying pool %lu", pool_index);
   for (uint32_t i = 0; i < pool_ptr->num_of_node_arrays; i++)
   {
      if (pool_ptr->nodes_ptr[i].mem_start_addr)
      {
         if (pool_ptr->nodes_ptr[i].list_bitmask)
         {
            // Force to zero
            AR_MSG(DBG_ERROR_PRIO,
                   "Bufpool error: Freeing, but all nodes aren't returned! Pool index %lu, list index %lu, bitmask "
                   "0x%lx",
                   pool_index,
                   i,
                   pool_ptr->nodes_ptr[i].list_bitmask);
            pool_ptr->nodes_ptr[i].list_bitmask = 0;
            BUFPOOL_ASSERT();
         }
         posal_bufpool_free_nodes_arr(pool_ptr, i);
      }
   }
   posal_mutex_unlock(pool_ptr->pool_mutex);
   posal_mutex_destroy(&pool_ptr->pool_mutex);
   posal_memory_free(pool_ptr);
   all_pools[pool_index] = NULL;
   return;
}

void posal_bufpool_print_used_node_ptrs(posal_bufpool_pool_t *pool_ptr, uint32_t  node_arr_index)
{
   uint32_t list_bitmask = pool_ptr->nodes_ptr[node_arr_index].list_bitmask;
   uint32_t node_index = 0;

   while(list_bitmask)
   {
      if(list_bitmask & 1)
      {
         posal_bufpool_node_header_t *node_ptr =
            (posal_bufpool_node_header_t *)((int8_t *)pool_ptr->nodes_ptr[node_arr_index].mem_start_addr +
                                             pool_ptr->align_padding_size +
                                             node_index * (sizeof(posal_bufpool_node_header_t) +
                                                               pool_ptr->align_padding_size + pool_ptr->node_size));

         void* leaked_buf_ptr           = (void *)((int8_t *)node_ptr + sizeof(posal_bufpool_node_header_t));

      AR_MSG(DBG_ERROR_PRIO,
             "Bufpool error: Leaked node %p pool_index %lu node_index %lu magic 0x%lX node_arr_index 0x%lx",
              leaked_buf_ptr, node_ptr->pool_index ,node_ptr->node_index,  node_ptr->magic, node_ptr->node_arr_index );
      }
      list_bitmask = list_bitmask >> 1;
      node_index++;
   }
}

void posal_bufpool_pool_reset_to_base(uint32_t pool_handle)
{
   uint32_t pool_index;
   if (AR_DID_FAIL(validate_handle(pool_handle, &pool_index)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Bufpool error: Invalid handle %lu", pool_handle);
      BUFPOOL_ASSERT();
      return;
   }
   posal_bufpool_pool_t *pool_ptr = all_pools[pool_index];
   posal_mutex_lock(pool_ptr->pool_mutex);
   // List at index 0 should be allocated by default
   if (NULL == pool_ptr->nodes_ptr[0].mem_start_addr)
   {
      posal_bufpool_allocate_new_nodes_arr(pool_ptr, 0, pool_index);
   }
   else if (pool_ptr->nodes_ptr[0].list_bitmask)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Bufpool error: All nodes aren't returned! Pool index %lu, bitmask 0x%lx",
             pool_index,
             pool_ptr->nodes_ptr[0].list_bitmask);
      pool_ptr->nodes_ptr[0].list_bitmask = 0;

#ifndef BUF_POOL_MEMLEAK_DEBUG
      BUFPOOL_ASSERT();
#else
      posal_bufpool_print_used_node_ptrs(pool_ptr, 0);
#endif
   }

   // free all other lists
   for (uint32_t i = 1; i < pool_ptr->num_of_node_arrays; i++)
   {
      if (pool_ptr->nodes_ptr[i].mem_start_addr)
      {
         if (pool_ptr->nodes_ptr[i].list_bitmask)
         {
            // TODO: should we force to zero? this is only used on sim, and if we free even when nodes are not returned
            // there won't be any eventual mem leak seen, though arguably one should be
            AR_MSG(DBG_ERROR_PRIO,
                   "Bufpool error: Freeing, but all nodes aren't returned! Pool index %lu, list index %lu, bitmask "
                   "0x%lx",
                   pool_index,
                   i,
                   pool_ptr->nodes_ptr[i].list_bitmask);
            pool_ptr->nodes_ptr[i].list_bitmask = 0;

#ifndef BUF_POOL_MEMLEAK_DEBUG
            BUFPOOL_ASSERT();
#else
            posal_bufpool_print_used_node_ptrs(pool_ptr, i);
#endif
         }
         else
         {
            posal_bufpool_free_nodes_arr(pool_ptr, i);
         }

      }
   }

   // Reset node counts
   pool_ptr->allocated_nodes = pool_ptr->nodes_per_arr;
   pool_ptr->used_nodes      = 0;
   posal_mutex_unlock(pool_ptr->pool_mutex);
   return;
}

void posal_bufpool_pool_free_unused_lists(uint32_t pool_handle)
{
   uint32_t pool_index;
   if (AR_DID_FAIL(validate_handle(pool_handle, &pool_index)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Bufpool error: Invalid handle %lu", pool_handle);
      BUFPOOL_ASSERT();
      return;
   }
   posal_bufpool_pool_t *pool_ptr = all_pools[pool_index];
   posal_mutex_lock(pool_ptr->pool_mutex);

   // free all other lists
   for (uint32_t i = 0; i < pool_ptr->num_of_node_arrays; i++)
   {
      if ((pool_ptr->nodes_ptr[i].mem_start_addr) && (0 == pool_ptr->nodes_ptr[i].list_bitmask))
      {
         posal_bufpool_free_nodes_arr(pool_ptr, i);
         pool_ptr->allocated_nodes -= pool_ptr->nodes_per_arr;
      }
   }
   posal_mutex_unlock(pool_ptr->pool_mutex);
   return;
}

ar_result_t posal_bufpool_profile_mem_usage(uint32_t pool_handle, uint32_t* bytes_used_ptr, uint32_t* bytes_allocated_ptr)
{
   uint32_t pool_index;

   if (AR_DID_FAIL(validate_handle(pool_handle, &pool_index)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Bufpool error: Invalid handle %lu", pool_handle);
      return AR_EFAILED;
   }

   if (NULL == bytes_used_ptr || NULL == bytes_allocated_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Bufpool error: bytes_used_ptr or bytes_allocated_ptr is NULL");
      return AR_EFAILED;
   }

   uint32_t nodes_used = 0;

   posal_bufpool_pool_t *pool_ptr = all_pools[pool_index];
   posal_mutex_lock(pool_ptr->pool_mutex);

   *bytes_allocated_ptr = pool_ptr->allocated_nodes * pool_ptr->node_size;

   for (uint32_t i = 0; i < pool_ptr->num_of_node_arrays; i++)
   {
      // TODO: hexagon instrinsic here?
      for (uint32_t j = 0; j < 32; j++)
      {
         nodes_used += pool_ptr->nodes_ptr[i].list_bitmask >> j & 1;
      }
   }

   *bytes_used_ptr = nodes_used * pool_ptr->node_size;
   posal_mutex_unlock(pool_ptr->pool_mutex);

   return AR_EOK;
}


uint32_t posal_bufpool_profile_all_mem_usage()
{
   uint32_t bytes = 0;
   for (uint32_t i = 0; i < POSAL_BUFPOOL_MAX_POOLS; ++i)
   {
      posal_bufpool_pool_t* pool_ptr = all_pools[i];
      if (NULL == pool_ptr)
      {
         continue;
      }
      else {
         posal_mutex_lock(pool_ptr->pool_mutex);
         for (uint32_t j = 0; j < pool_ptr->num_of_node_arrays; j++)
         {
            if(NULL != pool_ptr->nodes_ptr[j].mem_start_addr)
            {
               bytes += ((sizeof(posal_bufpool_node_header_t) + pool_ptr->align_padding_size + pool_ptr->node_size) *
                  pool_ptr->nodes_per_arr);
            }
         }
         posal_mutex_unlock(pool_ptr->pool_mutex);
      }
   }
   return bytes;
}
