/**
 * \file topo_buf_mgr.c
 *  
 * \brief
 *  
 *     Topology buffer manager
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"

ar_result_t topo_buf_manager_init(gen_topo_t *topo_ptr)
{
   topo_ptr->buf_mgr.prev_destroy_unused_call_ts_us = posal_timer_get_time();
   return AR_EOK;
}

static ar_result_t topo_buf_manager_free_list_nodes(topo_buf_manager_t *buf_mgr_ptr, spf_list_node_t **head_ptr)
{
   if (NULL == head_ptr)
   {
      return AR_EFAILED;
   }

   spf_list_node_t *curr_ptr = *head_ptr;
   while (NULL != curr_ptr)
   {
      spf_list_node_t *next_ptr = curr_ptr->next_ptr;

      int8_t *ptr = (int8_t *)curr_ptr;
      posal_memory_free(ptr);

      buf_mgr_ptr->total_num_bufs_allocated--;

      curr_ptr = next_ptr;
   }

   *head_ptr = NULL;
   return AR_EOK;
}

void topo_buf_manager_deinit(gen_topo_t *topo_ptr)
{
   /* deletes nodes and objects, as memory for list node and object is allocated
    * as one chunk */

   topo_buf_manager_free_list_nodes(&topo_ptr->buf_mgr, (&topo_ptr->buf_mgr.head_node_ptr));

   if (topo_ptr->buf_mgr.total_num_bufs_allocated)
   {
      TBF_MSG(topo_ptr->gu.log_id,
              DBG_ERROR_PRIO,
              "topo_buf_manager_destroy: Not all buffers returned, number of unreturned buffers: %d",
              topo_ptr->buf_mgr.total_num_bufs_allocated);
   }

   memset(&topo_ptr->buf_mgr, 0, sizeof(topo_buf_manager_t));
}

void topo_buf_manager_check_static_buf_assign_temp_list(gen_topo_t *topo_ptr, int8_t **buf_pptr, uint32_t buf_size)
{

   // search and get the buffer from the static buffer list
   if (NULL == topo_ptr->buf_mgr.opt_static_buf_assign_temp_list_ptr)
   {
      return;
   }

   spf_list_node_t *temp_buf_list_ptr = topo_ptr->buf_mgr.opt_static_buf_assign_temp_list_ptr;
   while (NULL != temp_buf_list_ptr)
   {
      topo_buf_manager_element_t *buf_element_ptr = (topo_buf_manager_element_t *)temp_buf_list_ptr->obj_ptr;
      if (buf_size <= buf_element_ptr->size)
      {
         spf_list_node_t *buf_list_closest_size_ptr = temp_buf_list_ptr;

         // remove the buf_list_closest_size_ptr from the list and update the list.
         *buf_pptr = (int8_t *)buf_element_ptr + TBF_BUF_PTR_OFFSET;
         buf_element_ptr->ref_count++;

#ifdef TOPO_BUF_MGR_DEBUG
         TBF_MSG(topo_ptr->gu.log_id,
                 DBG_HIGH_PRIO,
                 "topo_buf_manager_check_static_buf_assign_temp_list: found a statically assigned buffer 0x%p closest "
                 "to requested size: %lu closest buf size: %lu",
                 (uint32_t)*buf_pptr,
                 buf_size,
                 buf_element_ptr->size);
#endif

         spf_list_delete_node_update_head(&buf_list_closest_size_ptr,
                                          &topo_ptr->buf_mgr.opt_static_buf_assign_temp_list_ptr,
                                          TRUE);

         // buffer found
         return;
      }
      LIST_ADVANCE(temp_buf_list_ptr);
   }

#ifdef TOPO_BUF_MGR_DEBUG
   TBF_MSG(topo_ptr->gu.log_id,
           DBG_HIGH_PRIO,
           "topo_buf_manager_check_static_buf_assign_temp_list: Could not find a statically assigned buffer close to "
           "the size: %lu",
           buf_size);
#endif
   // buffer not found
   return;
}

// this function is used to return the buf to a statically assigned buffer to a temp list, so that the same buffer can
// be used for a subsequent port downstream
void topo_buf_manager_static_buf_assign_add_buf_to_temp_free_list(gen_topo_t *topo_ptr, int8_t *buf_ptr)
{
#ifdef TOPO_BUF_MGR_DEBUG
   TBF_MSG(topo_ptr->gu.log_id, DBG_LOW_PRIO, "topo_buf_manager_static_buf_assign_add_buf_to_temp_free_list()");
#endif

   /* getting the address of list node of the returned buffer
    * memory allocated for each buffer node is populated as follows:
    * topo_buf_manager_element_t
    * buffer
    */
   spf_list_node_t *returned_buf_node_ptr = (spf_list_node_t *)(buf_ptr - TBF_BUF_PTR_OFFSET);

#ifdef TOPO_BUF_MGR_DEBUG
   TBF_MSG(topo_ptr->gu.log_id,
           DBG_LOW_PRIO,
           "topo_buf_manager_static_buf_assign_add_buf_to_temp_free_list: returned buffer ptr: 0x%lx",
           buf_ptr);
#endif

   topo_buf_manager_element_t *ret_buf_element_ptr = (topo_buf_manager_element_t *)returned_buf_node_ptr->obj_ptr;
   spf_list_node_t            *cur_list_node_ptr   = topo_ptr->buf_mgr.opt_static_buf_assign_temp_list_ptr;
   uint32_t                    buf_size            = ret_buf_element_ptr->size;

   // if head node is NULL, add to the head of the list.
   if (NULL == topo_ptr->buf_mgr.opt_static_buf_assign_temp_list_ptr)
   {
      spf_list_insert_tail(&(topo_ptr->buf_mgr.opt_static_buf_assign_temp_list_ptr),
                           ret_buf_element_ptr,
                           topo_ptr->heap_id,
                           TRUE);
      return;
   }

#ifdef TOPO_BUF_MGR_DEBUG
   TBF_MSG(topo_ptr->gu.log_id,
           DBG_HIGH_PRIO,
           "topo_buf_manager_static_buf_assign_add_buf_to_temp_free_list: ret_element_size:%lu",
           buf_size);
#endif

   /* Insert the returned buffer in the sorted ascending list of buffers. This loop also checks the elements of
      the same buffer size to make sure buffer is not present in the list already. If not present it will insert after
      last buffer of the same size.

      For example, if list has buffer of sizes, b1[96] -> b2[96] -> b3[192] -> b4[192], and buffer to add is b2[96] it
      will return. Else if bufer to add is b5[96], it will update the list as b1[96] -> b2[96] -> b5[96] -> b3[192] ->
      b4[192]
   */
   while (NULL != cur_list_node_ptr)
   {
      topo_buf_manager_element_t *cur_buf_element_ptr = (topo_buf_manager_element_t *)cur_list_node_ptr->obj_ptr;
#ifdef TOPO_BUF_MGR_DEBUG
      TBF_MSG(topo_ptr->gu.log_id,
              DBG_HIGH_PRIO,
              "topo_buf_manager_static_buf_assign_add_buf_to_temp_free_list: cur_element_size:%lu",
              cur_buf_element_ptr->size);
#endif

      if (cur_buf_element_ptr == ret_buf_element_ptr)
      {
#ifdef TOPO_BUF_MGR_DEBUG
         TBF_MSG(topo_ptr->gu.log_id,
                 DBG_HIGH_PRIO,
                 "topo_buf_manager_static_buf_assign_add_buf_to_temp_free_list: already added to list");
#endif
         return;
      }

      // if returned buffer size is less than current insert before the cur element.
      if (buf_size < cur_buf_element_ptr->size)
      {
#ifdef TOPO_BUF_MGR_DEBUG
         TBF_MSG(topo_ptr->gu.log_id,
                 DBG_HIGH_PRIO,
                 "topo_buf_manager_static_buf_assign_add_buf_to_temp_free_list: inseting before cur_element_size:%lu",
                 cur_buf_element_ptr->size);
#endif
         // Create a new node, and add to the temp list
         spf_list_create_and_insert_before_node(&(topo_ptr->buf_mgr.opt_static_buf_assign_temp_list_ptr),
                                                ret_buf_element_ptr,
                                                cur_list_node_ptr,
                                                topo_ptr->heap_id,
                                                TRUE);
         return;
      }

      // If returned buffer is of largest size, it will be inserted at the end.
      if (NULL == cur_list_node_ptr->next_ptr)
      {
#ifdef TOPO_BUF_MGR_DEBUG
         TBF_MSG(topo_ptr->gu.log_id,
                 DBG_HIGH_PRIO,
                 "topo_buf_manager_static_buf_assign_add_buf_to_temp_free_list: inseting at the tail. last_element_size:%lu",
                 cur_buf_element_ptr->size);
#endif
         // Create a new node, and add to the end of temp list
         spf_list_insert_tail(&(topo_ptr->buf_mgr.opt_static_buf_assign_temp_list_ptr),
                              ret_buf_element_ptr,
                              topo_ptr->heap_id,
                              TRUE);
         return;
      }

      LIST_ADVANCE(cur_list_node_ptr);
   }

   return;
}

void topo_buf_manager_reset_static_assignment_list(gen_topo_t *topo_ptr)
{
#ifdef TOPO_BUF_MGR_DEBUG
   TBF_MSG(topo_ptr->gu.log_id,
           DBG_HIGH_PRIO,
           "topo_buf_manager_reset_static_assignment_list: Resetting temp buf list");
#endif
   // free the temporary free buffer from the static assgiment hlpr list.
   spf_list_delete_list((spf_list_node_t **)&topo_ptr->buf_mgr.opt_static_buf_assign_temp_list_ptr, TRUE);
}