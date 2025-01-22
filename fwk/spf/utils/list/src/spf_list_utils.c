/**
 * \file spf_list_utils.c
 * \brief
 *     This file contains implementation of graphite data structure utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_list_utils.h"
#include "spf_list_utils.h"
#include "spf_list_utils_i.h"
#include "posal_bufpool.h"

/*==============================================================================
   MACROS
==============================================================================*/

/*==============================================================================
   Type Definitions
==============================================================================*/

/*==============================================================================
   Globals
==============================================================================*/
// Handles returned from posal bufpool create
extern uint32_t bufpool_handles[SPF_LIST_NUM_POOLS];
/*==============================================================================
   Local Function definitions
==============================================================================*/

static void set_handle_from_heap_id(POSAL_HEAP_ID heap_id, uint32_t handle)
{
   heap_id = GET_ACTUAL_HEAP_ID(heap_id);
   if(heap_id >= SPF_LIST_NUM_POOLS)
   {
      return;
   }
   bufpool_handles[heap_id] = handle;
}

void spf_list_buf_pool_init(POSAL_HEAP_ID heap_id, uint32_t num_arrays, uint16_t nodes_per_arr)
{
   uint32_t handle = posal_bufpool_pool_create(sizeof(spf_list_node_t), heap_id, num_arrays, FOUR_BYTE_ALIGN, nodes_per_arr);
   if (POSAL_BUFPOOL_INVALID_HANDLE == handle)
   {
      AR_MSG(DBG_FATAL_PRIO, "Received invalid handle from posal bufpool!");
   }
   set_handle_from_heap_id(heap_id, handle);
/* Commenting FATAL_PRIO as Slate_SIM doesn't have memorymap and usecase will halt*/
#ifndef AUDIOSSMODE
   AR_MSG(DBG_MED_PRIO, "Created List Pool - heap_id = %lu, Handle 0x%lx", heap_id, handle);
#endif
}

// deinits all lists that haven't been freed
void spf_list_buf_pool_deinit(POSAL_HEAP_ID heap_id)
{
   posal_bufpool_pool_destroy(get_handle_from_heap_id(heap_id));
}

// deinits all lists from list_idx to max allocated
// used for test framework leak detection
void spf_list_buf_pool_reset(POSAL_HEAP_ID heap_id)
{
   posal_bufpool_pool_reset_to_base(get_handle_from_heap_id(heap_id));
}
/*

uint32_t spf_list_get_num_created_lists()
{
   uint32_t count = 0;
   for (uint32_t i = 0; i < SPF_MAX_NUM_LISTS; i++)
   {
      if (NULL != global_list_pool.list_arr[i].mem_start_addr)
      {
         count++;
      }
   }
   return count;
}
*/

/*
  Insert a node into the head of the list

  param[in] head_ptr: pointer to the head of module list
  param[in] obj_ptr: pointer to the instance that needs to be inserted

  return: none
*/
ar_result_t spf_list_insert_head(spf_list_node_t **head_ptr, void *obj_ptr, POSAL_HEAP_ID heap_id, bool_t use_pool)
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
   new_node_ptr->next_ptr = *head_ptr;

   // Update old head node
   if (*head_ptr)
   {
      (*head_ptr)->prev_ptr = new_node_ptr;
   }

   // Update to new head node
   *head_ptr = new_node_ptr;

   return AR_EOK;
}

/*
  Insert a node into the head of the list.
  Updates head node ptr if required.

  param[in] head_pptr: pointer to the head_ptr of module list
  param[in] obj_ptr:   pointer to the instance that needs to be inserted

  return: none
*/
ar_result_t spf_list_create_and_insert_before_node(spf_list_node_t **head_pptr,
                                                   void *            obj_ptr,
                                                   spf_list_node_t * node_ptr,
                                                   POSAL_HEAP_ID     heap_id,
                                                   bool_t            use_pool)
{
   if ((NULL == head_pptr) || (NULL == obj_ptr) || (NULL == node_ptr))
   {
      return AR_EBADPARAM;
   }
   /*allocate the space for the node*/
   spf_list_node_t *new_node_ptr = spf_list_create_new_node(obj_ptr, heap_id, use_pool);
   if (NULL == new_node_ptr)
   {
      return AR_ENOMEMORY;
   }

   return spf_list_insert_before_node(head_pptr, new_node_ptr, node_ptr );
}

ar_result_t spf_list_attach_at_tail(spf_list_node_t **head_ptr, spf_list_node_t *node_ptr)
{
   if ((NULL == head_ptr) || (NULL == node_ptr))
   {
      return AR_EBADPARAM;
   }

   if (NULL == *head_ptr)
   {
      *head_ptr = node_ptr;
      return AR_EOK;
   }

   // Find tail
   spf_list_node_t *curr_ptr = *head_ptr, *tail_ptr;
   (void)spf_list_get_tail_node(curr_ptr, &tail_ptr);

   tail_ptr->next_ptr = node_ptr;
   node_ptr->prev_ptr = tail_ptr;

   return AR_EOK;
}

/*
  pops the head of the list. Returns the instance pointer for the first node
  and deletes the head node.

  param[in] head_ptr: pointer to the head of module list

  return: instance pointed of the node at the head
*/
void *spf_list_pop_head(spf_list_node_t **head_ptr, bool_t pool_used)
{
   if ((NULL == head_ptr) || (NULL == *head_ptr))
   {
      return NULL;
   }

   void *           obj_ptr  = (*head_ptr)->obj_ptr;
   spf_list_node_t *temp_ptr = *head_ptr;

   // If list has only one node, then temp->next may be NULL.
   if (temp_ptr->next_ptr)
   {
      temp_ptr->next_ptr->prev_ptr = NULL;
   }

   *head_ptr = (*head_ptr)->next_ptr;

   spf_list_node_free(temp_ptr, pool_used);

   return obj_ptr;
}

/*
  delete the complete list, specified by the head pointer. Each of the nodes is freed up

  param[in] head_ptr: head of the list to be deleted
  param[in] delete_instance: delete the instances pointed to by the nodes as well

  return: AR_EOK on success, error code on failure
*/
static ar_result_t spf_list_delete_list_instances(spf_list_node_t **head_ptr, bool_t delete_instance, bool_t pool_used)
{
   if (NULL == head_ptr)
   {
      return AR_EFAILED;
   }

   spf_list_node_t *curr_ptr = *head_ptr;
   while (NULL != curr_ptr)
   {
      spf_list_node_t *next_ptr = curr_ptr->next_ptr;

      if (delete_instance)
      {
         posal_memory_free(curr_ptr->obj_ptr);
      }

      spf_list_node_free(curr_ptr, pool_used);
      curr_ptr = next_ptr;
   }

   *head_ptr = NULL;
   return AR_EOK;
}

/*
  Check if the node for the particular instance is present in the list

  param[in] head_ptr: pointer to the head of module list
  param[in] obj_ptr: pointer to the instance that needs to be searched/deleted

  return: TRUE if particular instance present, else false
*/
bool_t spf_list_contains_node(spf_list_node_t **head_ptr, void *obj_ptr)
{
   return spf_list_search_delete_node(head_ptr,
                                      obj_ptr,
                                      FALSE,
                                      TRUE /*pool_used: doesn't matter as we are not deleting*/);
}



/*
  delete the complete list, specified by the head pointer, instance memories are not
  freed up

  param[in] head_ptr: head of the list to be deleted

  return: AR_EOK on success, error code on failure
*/
ar_result_t spf_list_delete_list(spf_list_node_t **head_ptr, bool_t pool_used)
{
   return spf_list_delete_list_instances(head_ptr, FALSE, pool_used);
}

ar_result_t spf_list_delete_node_and_free_obj(spf_list_node_t **node_pptr,
                                              spf_list_node_t **head_node_pptr,
                                              bool_t            pool_used)
{
   if (NULL == node_pptr || NULL == *node_pptr)
   {
      return AR_EBADPARAM;
   }

   // Free obj memory, no need for null check, free will check
   posal_memory_free((*node_pptr)->obj_ptr);

   // Update connections
   spf_list_node_t *prev_node_ptr = (*node_pptr)->prev_ptr;
   spf_list_node_t *next_node_ptr = (*node_pptr)->next_ptr;
   if (NULL != prev_node_ptr)
   {
      prev_node_ptr->next_ptr = next_node_ptr;
   }
   if (NULL != next_node_ptr)
   {
      next_node_ptr->prev_ptr = prev_node_ptr;
   }

   bool_t deleting_head = (head_node_pptr && (*head_node_pptr == *node_pptr));
   spf_list_node_free(*node_pptr, pool_used);

   // This needs to be done after spf_list_node_free in case head_node_pptr == node_pptr.
   if (deleting_head)
   {
      *head_node_pptr = next_node_ptr;
   }

   // Update input argument
   *node_pptr = next_node_ptr;
   return AR_EOK;
}

/*
 * Deletes the current node, and updates it to point to the next node
 */
ar_result_t spf_list_delete_node(spf_list_node_t **node_pptr, bool_t pool_used)
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

   spf_list_node_free(*node_pptr, pool_used);
   *node_pptr = next_ptr;
   return AR_EOK;
}

ar_result_t spf_list_delete_list_and_free_objs(spf_list_node_t **head_pptr, bool_t pool_used)
{
   return spf_list_delete_list_instances(head_pptr, TRUE, pool_used);
}

/*
  Removes node from the list

  param[in] node_ptr      : pointer to the node that needs to be removed from the list

  return: none
*/
ar_result_t spf_list_remove_node_from_list(spf_list_node_t *node_ptr, bool_t pool_used)
{
   ar_result_t result = AR_EOK;

   result = spf_list_remove_node_from_list_only(node_ptr);
   if (AR_DID_FAIL(result))
   {
      return result;
   }

   spf_list_node_free(node_ptr, pool_used);

   return AR_EOK;
}

/*
  Add obj to the list. If the obj is already present, skip adding to
  the list.

  param[in] head_ptr: pointer to the head of module list
  param[in] obj_ptr: pointer to the instance that needs to be deleted
  param[in] heap_id: heap ID for allocating the list node

  return: MM Error code
*/
ar_result_t spf_list_search_and_add_obj(spf_list_node_t **head_ptr,
                                        void *            obj_ptr,
                                        bool_t *          node_added,
                                        POSAL_HEAP_ID     heap_id,
                                        bool_t            use_pool)
{
   spf_list_node_t *curr_ptr, *tail_ptr;
   spf_list_node_t *new_node_ptr;

   if ((NULL == head_ptr) || (NULL == obj_ptr))
   {
      return AR_EFAILED;
   }

   *node_added = FALSE;

   curr_ptr = *head_ptr;

   while (curr_ptr)
   {
      if (curr_ptr->obj_ptr == obj_ptr)
      {
         return AR_EOK;
      }

      tail_ptr = curr_ptr;
      curr_ptr = curr_ptr->next_ptr;
   }

   /*allocate the space for the node*/
   new_node_ptr = spf_list_create_new_node(obj_ptr, heap_id, use_pool);

   if (NULL == new_node_ptr)
   {
      return AR_ENOMEMORY;
   }

   if (NULL == *head_ptr)
   {
      *head_ptr = new_node_ptr;

      *node_added = TRUE;

      return AR_EOK;
   }

   tail_ptr->next_ptr     = new_node_ptr;
   new_node_ptr->prev_ptr = tail_ptr;

   *node_added = TRUE;

   return AR_EOK;
}

ar_result_t spf_list_find_list_node(spf_list_node_t *head_ptr, void *obj_ptr, spf_list_node_t **obj_list_node_pptr)
{
   if (!head_ptr || !obj_ptr || !obj_list_node_pptr)
   {
      return AR_EBADPARAM;
   }

   *obj_list_node_pptr = NULL;
   for (spf_list_node_t *list_ptr = head_ptr; list_ptr; LIST_ADVANCE(list_ptr))
   {
      void *cur_obj_ptr = list_ptr->obj_ptr;
      if (cur_obj_ptr == obj_ptr)
      {
         *obj_list_node_pptr = list_ptr;
         return AR_EOK;
      }
   }

   // Node obj not found, return failure.
   return AR_EFAILED;
}

//function to count the elements in a spf list.
uint32_t spf_list_count_elements(spf_list_node_t *head_ptr)
{
   uint32_t count = 0;
   if (NULL == head_ptr)
   {
      return 0;
   }

   while (head_ptr)
   {
      count++;
      head_ptr = head_ptr->next_ptr;
   }

   return count;
}

ar_result_t spf_list_pool_profile_memory(uint32_t* bytes_used_ptr, uint32_t* bytes_allocated_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t bytes_used, bytes_allocated;
   for (uint32_t i = 0; i < SPF_LIST_NUM_POOLS; i++)
   {
      bytes_used = 0;
      bytes_allocated = 0;
      result = posal_bufpool_profile_mem_usage(bufpool_handles[i], &bytes_used, &bytes_allocated);

      if (AR_EOK != result)
      {
         AR_MSG(DBG_ERROR_PRIO, "Failed to get fwk pool memory usage");
         break;
      }

      *bytes_used_ptr += bytes_used;
      *bytes_allocated_ptr += bytes_allocated;
   }
   return result;
}