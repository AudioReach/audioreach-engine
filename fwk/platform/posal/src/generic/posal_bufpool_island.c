
/**
 * \file posal_bufpool_island.c
 * \brief
 *    This file contains an buffer pool implementation for small allocations primarily for lists and queues
 *
 *
 * \copyright
 *    Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *    SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "posal.h"
#include "posal_bufpool_i.h"
#if defined(__hexagon__)
#include <hexagon_protos.h>
#endif
/* -----------------------------------------------------------------------
** Constant / Define Declarations
** ----------------------------------------------------------------------- */
/* This variable is not protected by a lock based on the assumption that pools
 * will be created statically at bootup from a single thread, and destroy will not
 * be called while any nodes are still pending */
posal_bufpool_pool_t *all_pools[POSAL_BUFPOOL_MAX_POOLS];
/* Number of free nodes that should be present in overall pool for a node array to be freed
 * 48 includes the 32 nodes about to be freed, plus 16 more in other lists, to ensure that we don't
 * get stuck freeing-reallocating too frequently
 */
const uint32_t POSAL_BUFPOOL_FREE_THRESHOLD = 48;

/* -----------------------------------------------------------------------
** Function Definitions
** ----------------------------------------------------------------------- */

ar_result_t validate_handle(uint32_t handle, uint32_t *index)
{
   if (POSAL_BUFPOOL_HANDLE_MAGIC != (handle & (0xffff0000uL)) || ((handle & 0xffff) > POSAL_BUFPOOL_MAX_POOLS))
   {
      return AR_EFAILED;
   }

   *index = handle & 0xffffuL;

   if (*index >= POSAL_BUFPOOL_MAX_POOLS)
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "Bufpool error: Invalid pool_index %lu", *index);
      return AR_EFAILED;
   }

   if (NULL == all_pools[*index])
   {
      return AR_EFAILED;
   }
   return AR_EOK;
}

static void toggle_bit_in_list_bitmask_at_idx(posal_bufpool_pool_t *pool_ptr, uint32_t index, uint32_t pos)
{
   pool_ptr->nodes_ptr[index].list_bitmask ^= (1 << pos);
}

static uint32_t get_first_zero_idx_in_list_bitmask(posal_bufpool_pool_t *pool_ptr, uint32_t index)
{
#if defined(__hexagon__)
   return Q6_R_ct1_R(pool_ptr->nodes_ptr[index].list_bitmask);
#else
   uint32_t num  = pool_ptr->nodes_ptr[index].list_bitmask;
   uint32_t zero = 0;
   if (~zero == num)
   {
      return POSAL_BUFPOOL_MAX_NODES;
   }

   uint32_t temp        = (~num) & (num + 1);
   uint32_t shift_count = 0;

   while (temp > 0)
   {
      temp >>= 1;
      shift_count++;
   }
   return shift_count - 1;
#endif
}

ar_result_t posal_bufpool_allocate_new_nodes_arr(posal_bufpool_pool_t *pool_ptr, uint32_t index, uint32_t pool_index)
{
   if (index >= pool_ptr->num_of_node_arrays)
   {
      return AR_EFAILED;
   }

   if (pool_ptr->nodes_ptr[index].mem_start_addr)
   {
      return AR_EOK;
   }

   // exit island before allocating memory
   if (POSAL_IS_ISLAND_HEAP_ID(pool_ptr->heap_id))
   {
      posal_island_trigger_island_exit();
   }

   return posal_bufpool_allocate_new_nodes_arr_util_(pool_ptr,index,  pool_index);
}

void *posal_bufpool_get_node(uint32_t pool_handle)
{
   uint32_t    pool_index;
   ar_result_t result;

   if (AR_DID_FAIL(validate_handle(pool_handle, &pool_index)))
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "Bufpool error: Invalid handle %lu", pool_handle);
      BUFPOOL_ASSERT();
      return NULL;
   }

   posal_bufpool_pool_t *pool_ptr       = all_pools[pool_index];
   void *                node_to_return = NULL;
   uint32_t              next_node_idx;

   posal_mutex_lock(pool_ptr->pool_mutex);
   for (uint32_t i = 0; i < pool_ptr->num_of_node_arrays; i++)
   {
      if (NULL == pool_ptr->nodes_ptr[i].mem_start_addr)
      {
         if (AR_DID_FAIL(result = posal_bufpool_allocate_new_nodes_arr(pool_ptr, i, pool_index)))
         {
            posal_mutex_unlock(pool_ptr->pool_mutex);
            BUFPOOL_ASSERT();
            return NULL;
         }
         // TODO: Can just be set to zero, but need to revisit this whole part anyway
         next_node_idx = get_first_zero_idx_in_list_bitmask(pool_ptr, i);
      }
      else
      {
         next_node_idx = get_first_zero_idx_in_list_bitmask(pool_ptr, i);
         if (pool_ptr->nodes_per_arr == next_node_idx)
         {
            // need to init the next list go to it
            // if already allocated, this function will just return
            if (AR_DID_FAIL(result = posal_bufpool_allocate_new_nodes_arr(pool_ptr, i + 1, pool_index)))
            {
               posal_mutex_unlock(pool_ptr->pool_mutex);
               BUFPOOL_ASSERT();
               return NULL;
            }
            continue;
         }
      }
      // return from current list
      posal_bufpool_node_header_t *node_ptr =
         (posal_bufpool_node_header_t *)((int8_t *)pool_ptr->nodes_ptr[i].mem_start_addr +
                                         pool_ptr->align_padding_size +
                                         next_node_idx * (sizeof(posal_bufpool_node_header_t) +
                                                          pool_ptr->align_padding_size + pool_ptr->node_size));
      // update node header
      node_ptr->node_arr_index = i;
      node_ptr->node_index     = next_node_idx;
      node_ptr->pool_index     = pool_index;
      node_ptr->magic          = POSAL_BUFPOOL_NODE_MAGIC;
      node_to_return           = (void *)((int8_t *)node_ptr + sizeof(posal_bufpool_node_header_t));
#ifdef DEBUG_BUFPOOL_LOW
      AR_MSG_ISLAND(DBG_MED_PRIO,
             "Node assigned, pool idx %lu, list_idx: %lu, bit_pos: 0x%lx, pointer: 0x%lx, tid: 0x%lx ",
             pool_index,
             i,
             next_node_idx,
             node_ptr,
             posal_thread_get_curr_tid());
#endif

      // setting the bit at this index
      toggle_bit_in_list_bitmask_at_idx(pool_ptr, i, next_node_idx);
      pool_ptr->used_nodes++;
      break;
   }
   posal_mutex_unlock(pool_ptr->pool_mutex);
   return node_to_return;
}

void posal_bufpool_return_node(void *node_to_return)
{
   posal_bufpool_node_header_t *node_ptr =
      (posal_bufpool_node_header_t *)((int8_t *)node_to_return - sizeof(posal_bufpool_node_header_t));
#ifdef DEBUG_BUFPOOL_LOW
   AR_MSG_ISLAND(DBG_MED_PRIO, "Node being returned, pointer 0x%lx", node_ptr);
#endif
   uint32_t pool_index = node_ptr->pool_index;
   if ((pool_index >= POSAL_BUFPOOL_MAX_POOLS) || (NULL == all_pools[pool_index]))
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "Bufpool error: Invalid pool index %lu", pool_index);
      BUFPOOL_ASSERT();
      return;
   }

   posal_bufpool_pool_t *pool_ptr = all_pools[pool_index];
   posal_mutex_lock(pool_ptr->pool_mutex);
   uint32_t list_index = node_ptr->node_arr_index;
   uint32_t node_index = node_ptr->node_index;
   if (node_ptr->magic != POSAL_BUFPOOL_NODE_MAGIC)
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO,
             "Bufpool error: Node address invalid or node corrupted! Pool %lu, list %lu node %lu",
             pool_index,
             list_index,
             node_index);
      posal_mutex_unlock(pool_ptr->pool_mutex);
      BUFPOOL_ASSERT();
      return;
   }
   if (!(pool_ptr->nodes_ptr[list_index].list_bitmask & (1 << node_index)))
   {
      // node was not allocated to begin with
      AR_MSG_ISLAND(DBG_ERROR_PRIO,
             "Bufpool error: Attempt to free unallocated node pool %lu, list %lu, node %lu",
             pool_index,
             list_index,
             node_index);

      posal_mutex_unlock(pool_ptr->pool_mutex);
      BUFPOOL_ASSERT();
      return;
   }
   // Clear the bit
   toggle_bit_in_list_bitmask_at_idx(pool_ptr, list_index, node_index);

#ifdef DEBUG_BUFPOOL_LOW
   AR_MSG_ISLAND(DBG_MED_PRIO,
          "Node freed, pool idx %lu, list_idx: %lu, bit_pos: 0x%lx, pointer: 0x%lx, tid: 0x%lx ",
          pool_index,
          list_index,
          node_index,
          node_ptr,
          posal_thread_get_curr_tid());
#endif

   pool_ptr->used_nodes--;

   // check if you want to free this list
   if ((!pool_ptr->nodes_ptr[list_index].list_bitmask) &&
       (pool_ptr->allocated_nodes - pool_ptr->used_nodes > POSAL_BUFPOOL_FREE_THRESHOLD))
   {
#ifdef DEBUG_BUFPOOL
      AR_MSG_ISLAND(DBG_MED_PRIO,
             "Bufpool: Freeing list pool idx %lu, list_idx: %lu, free nodes %lu ",
             pool_index,
             list_index,
             pool_ptr->allocated_nodes - pool_ptr->used_nodes);
#endif
      if (POSAL_IS_ISLAND_HEAP_ID(pool_ptr->heap_id))
      {
         posal_island_trigger_island_exit();
      }
      posal_bufpool_free_nodes_arr(pool_ptr, list_index);
   }
   posal_mutex_unlock(pool_ptr->pool_mutex);
   return;
}

bool_t posal_bufpool_is_address_in_bufpool(void *ptr, uint32_t pool_handle)
{
   uint32_t pool_index;
   uint32_t list_arr_size = 0;

   if (AR_DID_FAIL(validate_handle(pool_handle, &pool_index)))
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "Bufpool error: Invalid handle %lu", pool_handle);
      BUFPOOL_ASSERT();
      return FALSE;
   }
   posal_bufpool_pool_t *pool_ptr = all_pools[pool_index];
   list_arr_size = ((sizeof(posal_bufpool_node_header_t) + pool_ptr->align_padding_size + pool_ptr->node_size) *
                    pool_ptr->nodes_per_arr);
   posal_mutex_lock(pool_ptr->pool_mutex);
   for (uint32_t i = 0; i < pool_ptr->num_of_node_arrays; i++)
   {
      void *start_addr = pool_ptr->nodes_ptr[i].mem_start_addr;
      if (start_addr)
      {
         if ((ptr >= start_addr) && (ptr < (void *)((uintptr_t)start_addr + list_arr_size)))
         {
            posal_mutex_unlock(pool_ptr->pool_mutex);
            return TRUE;
         }
      }
   }
   posal_mutex_unlock(pool_ptr->pool_mutex);
   return FALSE;
}
