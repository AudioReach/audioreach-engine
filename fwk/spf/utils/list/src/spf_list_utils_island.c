/**
 * \file spf_list_utils_island.c
 * \brief
 *     This file contains implementation of graphite data structure utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*==============================================================================
  Include Files
==============================================================================*/

#include "spf_list_utils.h"
#include "spf_list_utils.h"
#include "spf_list_utils_i.h"
#include "posal_bufpool.h"

//#define DEBUG_LIST_UTIL
// clang-format on

/*==============================================================================
   Globals
==============================================================================*/
// Handles returned from posal bufpool create
uint32_t bufpool_handles[SPF_LIST_NUM_POOLS];

/*==============================================================================
   Function Definitions
==============================================================================*/

uint32_t get_handle_from_heap_id(POSAL_HEAP_ID heap_id)
{
   POSAL_HEAP_ID temp_heap_id = GET_ACTUAL_HEAP_ID(heap_id);

   // if heap id is not default island heap, list pool should be overriden with default
   if (POSAL_IS_ISLAND_HEAP_ID(heap_id))
   {
      temp_heap_id = GET_ACTUAL_HEAP_ID(posal_get_island_heap_id());
   }

   if (temp_heap_id >= SPF_LIST_NUM_POOLS)
   {
      return 0;
   }

   return bufpool_handles[temp_heap_id];
}

spf_list_node_t *spf_list_get_node(POSAL_HEAP_ID heap_id)
{
   uint32_t handle = get_handle_from_heap_id(heap_id);

   spf_list_node_t *out_node_ptr = (spf_list_node_t *)posal_bufpool_get_node(handle);
   if (NULL == out_node_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error! Failed to get list node from bufpool handle %lu", handle);
   }
   return out_node_ptr;
}

void spf_list_node_return(spf_list_node_t *node_ptr)
{
   if (NULL == node_ptr)
   {
      return;
   }
   posal_bufpool_return_node(node_ptr);
}

void spf_list_node_free(spf_list_node_t *node_ptr, bool_t pool_used)
{
   if (pool_used)
   {
      spf_list_node_return(node_ptr);
   }
   else
   {
      posal_memory_free(node_ptr);
   }
}

spf_list_node_t *spf_list_create_new_node(void *obj_ptr, POSAL_HEAP_ID heap_id, bool_t use_pool)
{
   /*allocate the space for the node*/
   spf_list_node_t *new_node_ptr;

   if (use_pool)
   {
      new_node_ptr = spf_list_get_node(heap_id);
   }
   else
   {
      new_node_ptr = (spf_list_node_t *)posal_memory_malloc(sizeof(spf_list_node_t), heap_id);
   }
   if (NULL == new_node_ptr)
   {
      return new_node_ptr;
   }

   /* Populate the list node */
   new_node_ptr->obj_ptr  = obj_ptr;
   new_node_ptr->next_ptr = NULL;
   new_node_ptr->prev_ptr = NULL;

   return new_node_ptr;
}
/*
  merges 2 lists and sets the src to NULL
  does this only if list heads are different

  param[in] dest_list_ptr: head of the list to be merged into
  param[in] src_list_ptr: head of the list that needs to be merged into destination list

  return: AR_EOK on success, error code on failure
*/
ar_result_t spf_list_merge_lists_internal(spf_list_node_t **dest_list_ptr, spf_list_node_t **src_list_ptr)
{
   // don't merge if list heads are same, but still set the src list to NULL in all cases.
   if (*dest_list_ptr != *src_list_ptr)
   {
      if (NULL == *dest_list_ptr)
      {
         *dest_list_ptr = *src_list_ptr;
      }
      else if (NULL != *src_list_ptr)
      {
         // Both lists are valid, so append src to tail of dest
         spf_list_node_t *curr_ptr = *dest_list_ptr, *tail_ptr;
         (void)spf_list_get_tail_node(curr_ptr, &tail_ptr);
         tail_ptr->next_ptr        = *src_list_ptr;
         (*src_list_ptr)->prev_ptr = tail_ptr;
      }
   }
   /*set the source list to be empty*/
   *src_list_ptr = NULL;

   return AR_EOK;
}

ar_result_t spf_list_remove_node_from_list_only(spf_list_node_t *node_ptr)
{
   if (NULL == node_ptr)
   {
      return AR_EBADPARAM;
   }

   spf_list_node_t *prev_node_ptr, *next_node_ptr;

   // Update connections
   prev_node_ptr = node_ptr->prev_ptr;
   next_node_ptr = node_ptr->next_ptr;
   if (NULL != prev_node_ptr)
   {
      prev_node_ptr->next_ptr = next_node_ptr;
   }
   if (NULL != next_node_ptr)
   {
      next_node_ptr->prev_ptr = prev_node_ptr;
   }
   return AR_EOK;
}

/*
  Adds the node to the top of the list

  param[in] head_ptr: pointer to the head of module list
  param[in] node_ptr: pointer to the node that needs to be added

  return: none
*/
ar_result_t spf_list_push_node(spf_list_node_t **head_ptr, spf_list_node_t *node_ptr)
{
   if ((NULL == head_ptr) || (NULL == node_ptr))
   {
      return AR_EBADPARAM;
   }

   node_ptr->next_ptr = *head_ptr;
   node_ptr->prev_ptr = NULL;

   // Update old head node
   if (*head_ptr)
   {
      (*head_ptr)->prev_ptr = node_ptr;
   }

   // Update to new head node
   *head_ptr = node_ptr;

   return AR_EOK;
}

/**
 * src_head_pptr is the pointer to the head pointer to which node_ptr originally belongs
 */
void spf_list_move_node_to_another_list(spf_list_node_t **list_pptr,
                                        spf_list_node_t * node_ptr,
                                        spf_list_node_t **src_head_pptr)
{
   if ((NULL == list_pptr) || (NULL == node_ptr))
   {
      return;
   }
   spf_list_node_t *next_ptr = node_ptr->next_ptr;
   spf_list_node_t *prev_ptr = node_ptr->prev_ptr;
   // detach node, while maintaining continuity of the list.
   node_ptr->next_ptr = NULL;
   node_ptr->prev_ptr = NULL;
   if (next_ptr)
   {
      next_ptr->prev_ptr = prev_ptr;
   }
   if (prev_ptr)
   {
      prev_ptr->next_ptr = next_ptr;
   }

   spf_list_node_t *temp_ptr = node_ptr; // below func sets sec arg to null.
   spf_list_merge_lists(list_pptr, &temp_ptr);

   // change the header pointer to the next
   if (*src_head_pptr == node_ptr)
   {
      *src_head_pptr = next_ptr;
   }
}

ar_result_t spf_list_get_tail_node(spf_list_node_t *head_ptr, spf_list_node_t **tail_pptr)
{
   if (NULL == head_ptr)
   {
      *tail_pptr = NULL;
      return AR_EBADPARAM;
   }

   while (head_ptr->next_ptr)
   {
      head_ptr = head_ptr->next_ptr;
   }
   *tail_pptr = head_ptr;
   return AR_EOK;
}

/*
  Insert a node into the tail of the list

  param[in] head_ptr: pointer to the head of module list
  param[in] obj_ptr: pointer to the instance that needs to be inserted

  return: none
*/
ar_result_t spf_list_insert_tail(spf_list_node_t **head_ptr, void *obj_ptr, POSAL_HEAP_ID heap_id, bool_t use_pool)
{
   if ((NULL == head_ptr) || (NULL == obj_ptr))
   {
      return AR_EBADPARAM;
   }

   /*allocate the space for the node*/
   spf_list_node_t *new_node_ptr = spf_list_create_new_node(obj_ptr, heap_id, use_pool);
   if (NULL == new_node_ptr)
   {
      return AR_ENOMEMORY;
   }

   if (NULL == *head_ptr)
   {
      *head_ptr = new_node_ptr;
      return AR_EOK;
   }

   // Find tail
   spf_list_node_t *curr_ptr = *head_ptr, *tail_ptr;
   (void)spf_list_get_tail_node(curr_ptr, &tail_ptr);

   tail_ptr->next_ptr     = new_node_ptr;
   new_node_ptr->prev_ptr = tail_ptr;

   return AR_EOK;
}

/*
 * Deletes the current node, and updates it to point to the next node
 */
ar_result_t spf_list_delete_node_update_head(spf_list_node_t **node_pptr,
                                             spf_list_node_t **head_node_pptr,
                                             bool_t            pool_used)
{
   if (!node_pptr || !(*node_pptr))
   {
      return AR_EBADPARAM;
   }
   spf_list_node_t *next_ptr = (*node_pptr)->next_ptr;
   spf_list_node_t *prev_ptr = (*node_pptr)->prev_ptr;

   if (NULL != prev_ptr)
   {
      prev_ptr->next_ptr = next_ptr;
   }
   if (NULL != next_ptr)
   {
      next_ptr->prev_ptr = prev_ptr;
   }

   if (head_node_pptr && (*head_node_pptr == *node_pptr))
   {
      *head_node_pptr = next_ptr;
   }

   spf_list_node_free(*node_pptr, pool_used);
   *node_pptr = next_ptr;
   return AR_EOK;
}

bool_t spf_list_node_is_addr_from_heap(void *ptr, POSAL_HEAP_ID heap_id)
{
   if (posal_bufpool_is_address_in_bufpool(ptr, get_handle_from_heap_id(heap_id)))
   {
      return TRUE;
   }
   return FALSE;
}

ar_result_t spf_list_realloc_replace_node(spf_list_node_t **head_pptr,
                                          spf_list_node_t **node_pptr,
                                          void *            new_obj_ptr,
                                          bool_t            pool_used,
                                          bool_t            use_pool,
                                          POSAL_HEAP_ID     heap_id)
{
   if ((NULL == head_pptr) || (NULL == *node_pptr))
   {
      return AR_EBADPARAM;
   }

   spf_list_node_t *prev_ptr = (*node_pptr)->prev_ptr;
   spf_list_node_t *next_ptr = (*node_pptr)->next_ptr;

   spf_list_node_t *temp_ptr = spf_list_create_new_node(new_obj_ptr, heap_id, use_pool);
   if (!temp_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Couldn't create list node.");
      return AR_EFAILED;
   }

   temp_ptr->next_ptr = next_ptr;
   temp_ptr->prev_ptr = prev_ptr;

   if (next_ptr)
   {
      next_ptr->prev_ptr = temp_ptr;
   }

   if (prev_ptr)
   {
      prev_ptr->next_ptr = temp_ptr;
   }

   if (*node_pptr == *head_pptr)
   {
      *head_pptr = temp_ptr;
   }

   // return/free the old list node
   spf_list_node_free(*node_pptr, pool_used);
   return AR_EOK;
}

/*
  Insert the node before a given node (updates head if needed)

  param[in/out] head_pptr  : pointer to the head of module list
  param[in] new_node_ptr  : pointer to the instance that needs to be inserted
  param[in] node_ptr      : pointer to the node before which the new node should be inserted

  return: none
*/
ar_result_t spf_list_insert_before_node(spf_list_node_t **head_pptr,
                                        spf_list_node_t * new_node_ptr,
                                        spf_list_node_t * node_ptr)
{
   if ((NULL == head_pptr) || (NULL == new_node_ptr) || (NULL == node_ptr))
   {
      return AR_EBADPARAM;
   }

   new_node_ptr->next_ptr = node_ptr;
   new_node_ptr->prev_ptr = node_ptr->prev_ptr;
   if (node_ptr->prev_ptr)
   {
      node_ptr->prev_ptr->next_ptr = new_node_ptr;
   }

   node_ptr->prev_ptr = new_node_ptr;

   // Update to new head node
   if (*head_pptr == node_ptr)
   {
      *head_pptr = new_node_ptr;
   }
   return AR_EOK;
}

/*
  search for/delete the node for the particular instance from the list

  param[in] head_ptr: pointer to the head of module list
  param[in] obj_ptr: pointer to the instance that needs to be deleted
  param[in] to_delete: indicates if the found node needs to be deleted

  return: TRUE if particular instance present, else false
*/
bool_t spf_list_search_delete_node(spf_list_node_t **head_ptr, void *obj_ptr, bool_t to_delete, bool_t pool_used)
{
   if ((NULL == head_ptr) || (NULL == obj_ptr))
   {
      return FALSE;
   }

   spf_list_node_t *curr_ptr = *head_ptr;

   while (NULL != curr_ptr)
   {
      if (curr_ptr->obj_ptr == obj_ptr)
      {
         if (to_delete)
         {
            if (curr_ptr == *head_ptr)
            {
               // Next node becomes head, set prev to null
               *head_ptr = curr_ptr->next_ptr;

               if (curr_ptr->next_ptr)
               {
                  curr_ptr->next_ptr->prev_ptr = NULL;
               }
            }
            else
            {
               curr_ptr->prev_ptr->next_ptr = curr_ptr->next_ptr;

               if (curr_ptr->next_ptr)
               {
                  curr_ptr->next_ptr->prev_ptr = curr_ptr->prev_ptr;
               }
            }
            spf_list_node_free(curr_ptr, pool_used);
         }
         return TRUE;
      }
      curr_ptr = curr_ptr->next_ptr;
   }
   return FALSE;
}

/*
  Delete the node for the particular instance from the list

  param[in] head_ptr: pointer to the head of module list
  param[in] obj_ptr: pointer to the instance that needs to be searched/deleted

  return: TRUE if particular instance present, else false
*/
bool_t spf_list_find_delete_node(spf_list_node_t **head_ptr, void *obj_ptr, bool_t pool_used)
{
   return spf_list_search_delete_node(head_ptr, obj_ptr, TRUE, pool_used);
}