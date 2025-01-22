/**
 * \file posal_queue_island.c
 * \brief
 *     This file contains an efficient queue of 8-uint8_t nodes.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "posal.h"
#include "posal_queue_i.h"
#include "posal_target_i.h"
#include "posal_internal.h"
#ifdef DEBUG_POSAL_QUEUE
#include "posal_globalstate.h"
#endif
/* -----------------------------------------------------------------------
** Global / Define Declarations
** ----------------------------------------------------------------------- */
uint32_t g_posal_queue_bufpool_handle[SPF_POSAL_Q_NUM_POOLS];

ar_result_t posal_queue_push_back(posal_queue_t *q_ptr, posal_queue_element_t *payload_ptr)
{
   posal_queue_internal_t *queue_ptr = (posal_queue_internal_t *)q_ptr;
   if ((NULL == queue_ptr) || (NULL == queue_ptr->channel_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "Q SEND: channel not initialized on Q");
      return AR_EBADPARAM;
   }
   // grab the mutex
   posal_queue_mutex_lock(queue_ptr);

#ifdef QUEUE_DISABLE_NEEDED
   if (TRUE == queue_ptr->disable_flag)
   {
      posal_queue_mutex_unlock(queue_ptr);
      AR_MSG(DBG_HIGH_PRIO, "Q DISABLED: Cannot enqueue the data");
      return AR_EFAILED;
   }
#endif // QUEUE_DISABLE_NEEDED

#ifdef DEBUG_POSAL_QUEUE
   uint32_t unOpcode = (uint32_t)(*payload_ptr >> 32);
   // filter out POSAL_DATA_BUFFER to avoid the flood.
   if (posal_globalstate.bEnableQLogging && 0x0000000AL != unOpcode)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "Q PushBk: Thrd=0x%x, Q=0x%x, Msg=0x%x 0x%x",
             posal_thread_get_id(),
             queue_ptr,
             unOpcode,
             (uint32_t)(*payload_ptr));
   }
#endif // DEBUG_POSAL_QUEUE
#ifdef DEBUG_QUEUE_LOW
   AR_MSG(DBG_HIGH_PRIO,
          "Queue push 0x%x, payload 0x%x, active %lu, allocated %lu, max %lu",
          queue_ptr,
          payload_ptr->q_payload_ptr,
          queue_ptr->active_nodes,
          queue_ptr->num_nodes,
          queue_ptr->max_nodes);
#endif
   // Check for full queue
   if (queue_ptr->active_nodes == queue_ptr->max_nodes)
   {
      AR_MSG(DBG_ERROR_PRIO, "Queue error: OVERFLOWED QUEUE: Q=0x%p", queue_ptr);
      posal_queue_mutex_unlock(queue_ptr);
      return AR_ENEEDMORE;
   }
   posal_queue_element_list_t *node_ptr;
   // all currently allocated nodes are in use, get one more
   if (queue_ptr->active_nodes == queue_ptr->num_nodes)
   {
#ifdef DEBUG_POSAL_QUEUE_POOL
      AR_MSG(DBG_MED_PRIO,
             "Queue pool: Allocating new node, num nodes %lu qname %s",
             queue_ptr->num_nodes,
             queue_ptr->name);
#endif
      node_ptr = posal_queue_create_node(queue_ptr);
      if (NULL == node_ptr)
      {
         AR_MSG(DBG_FATAL_PRIO, "Queue error: unable to get node");
         posal_queue_mutex_unlock(queue_ptr);
         return AR_ENOMEMORY;
      }

      if (0 == queue_ptr->num_nodes)
      {
         // assign all the list pointers to this node
         queue_ptr->active_head_ptr = node_ptr;
         queue_ptr->active_tail_ptr = node_ptr;

         node_ptr->prev_ptr = node_ptr;
         node_ptr->next_ptr = node_ptr;
      }
      else // add new node to the list
      {
         node_ptr->prev_ptr = queue_ptr->active_tail_ptr;
         node_ptr->next_ptr = queue_ptr->active_tail_ptr->next_ptr;

         queue_ptr->active_tail_ptr->next_ptr->prev_ptr = node_ptr;
         queue_ptr->active_tail_ptr->next_ptr           = node_ptr;
         queue_ptr->active_tail_ptr                     = node_ptr;
      }
      queue_ptr->num_nodes++;
   }
   else
   {
      // move to the next element only if currently pointing at an element, i.e. active_nodes>0
      if (0 < queue_ptr->active_nodes)
      {
         queue_ptr->active_tail_ptr = queue_ptr->active_tail_ptr->next_ptr;
      }
   }

   // by the time we are here, active_tail_ptr is the place where we write the current payload

   queue_ptr->active_tail_ptr->elem = *payload_ptr;
   queue_ptr->active_nodes++;

   // if signaling is disabled then don't set the signal
   if (!queue_ptr->disable_signaling)
   {
      // send the signal
      posal_channel_internal_t *ch_ptr = (posal_channel_internal_t *)queue_ptr->channel_ptr;
      posal_signal_set_target_inline(&ch_ptr->anysig, queue_ptr->channel_bit);
      // AR_MSG(DBG_MED_PRIO, "queue_push_back: queue_ptr=0x%p, *ch_ptr=0x%p, &ch_ptr->anysig=0x%p",queue_ptr, *ch_ptr,
      // &ch_ptr->anysig);
   }
   // release the mutex
   posal_queue_mutex_unlock(queue_ptr);

   return AR_EOK;
}

ar_result_t posal_queue_pop_front(posal_queue_t *q_ptr, posal_queue_element_t *payload_ptr)
{
   posal_queue_internal_t *queue_ptr = (posal_queue_internal_t *)q_ptr;

   if (NULL == queue_ptr->channel_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Q SEND: channel not initialized on Q");
      return AR_EBADPARAM;
   }
   // grab the mutex
   posal_queue_mutex_lock(queue_ptr);
   // make sure not empty (this is non-blocking mq).
   if (0 == queue_ptr->active_nodes)
   {
      posal_queue_mutex_unlock(queue_ptr);
      return AR_ENEEDMORE;
   }

   *payload_ptr = queue_ptr->active_head_ptr->elem;
   // point to next entry to read
   queue_ptr->active_nodes--;

#ifdef DEBUG_POSAL_QUEUE
   uint32_t unOpcode = (uint32_t)(*payload_ptr >> 32);
   // filter out POSAL_DATA_BUFFER to avoid the flood.
   if (posal_globalstate.bEnableQLogging && 0x0000000AL != unOpcode)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "Q PopFnt: Thrd=0x%x, Q=0x%x, Msg=0x%x 0x%x",
             qurt_thread_get_id(),
             queue_ptr,
             unOpcode,
             (uint32_t)(*payload_ptr));
   }
#endif // DEBUG_POSAL_QUEUE

#ifdef DEBUG_QUEUE_LOW
   AR_MSG(DBG_HIGH_PRIO,
          "Queue pop front 0x%x, payload 0x%x, active %lu, allocated %lu, max %lu",
          queue_ptr,
          payload_ptr->q_payload_ptr,
          queue_ptr->active_nodes,
          queue_ptr->num_nodes,
          queue_ptr->max_nodes);
#endif
   // if mq is empty, clear signal.
   if (0 == queue_ptr->active_nodes)
   {
      posal_channel_internal_t *ch_ptr = (posal_channel_internal_t *)queue_ptr->channel_ptr;
      posal_signal_clear_with_bitmask_target_inline(&ch_ptr->anysig, queue_ptr->channel_bit);
   }
   else
   {
      // only advance the active head if there is still a node for it to point to. otherwise leave it where it is.
      queue_ptr->active_head_ptr = queue_ptr->active_head_ptr->next_ptr;
   }

   // release the mutex
   posal_queue_mutex_unlock(queue_ptr);

   return AR_EOK;
}

posal_channel_t posal_queue_get_channel(posal_queue_t *q_ptr)
{
   posal_queue_internal_t *queue_ptr = (posal_queue_internal_t *)q_ptr;
   return queue_ptr->channel_ptr;
}

uint32_t posal_queue_get_channel_bit(posal_queue_t *q_ptr)
{
   posal_queue_internal_t *queue_ptr = (posal_queue_internal_t *)q_ptr;
   return queue_ptr->channel_bit;
}

uint32_t posal_queue_poll(posal_queue_t *q_ptr)
{
   posal_queue_internal_t   *queue_ptr = (posal_queue_internal_t *)q_ptr;
   posal_channel_internal_t *ch_ptr    = (posal_channel_internal_t *)queue_ptr->channel_ptr;
   return (queue_ptr->channel_bit & posal_signal_get_target_inline(&ch_ptr->anysig));
}

inline void posal_queue_lock_mutex(posal_queue_t *q_ptr)
{
   posal_queue_internal_t *queue_ptr = (posal_queue_internal_t *)q_ptr;
   posal_queue_mutex_lock(queue_ptr);
}

inline void posal_queue_unlock_mutex(posal_queue_t *q_ptr)
{
   posal_queue_internal_t *queue_ptr = (posal_queue_internal_t *)q_ptr;
   posal_queue_mutex_unlock(queue_ptr);
}

inline uint32_t posal_queue_get_size()
{
   return ALIGN_8_BYTES(sizeof(posal_queue_internal_t));
}

uint32_t posal_queue_get_queue_fullness(posal_queue_t *q_ptr)
{
   posal_queue_internal_t *queue_ptr = (posal_queue_internal_t *)q_ptr;
   if (NULL != queue_ptr)
   {
      return queue_ptr->active_nodes;
   }

   return 0;
}

ar_result_t posal_queue_insert_front(posal_queue_t *q_ptr, posal_queue_element_t *payload_ptr)
{
   posal_queue_internal_t *queue_ptr = (posal_queue_internal_t *)q_ptr;
   if ((NULL == queue_ptr) || (NULL == queue_ptr->channel_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "Q SEND: channel not initialized on Q");
      return AR_EBADPARAM;
   }
   // grab the mutex
   posal_queue_mutex_lock(queue_ptr);

#ifdef QUEUE_DISABLE_NEEDED
   if (TRUE == queue_ptr->disable_flag)
   {
      posal_queue_mutex_unlock(queue_ptr);
      AR_MSG(DBG_HIGH_PRIO, "Q DISABLED: Cannot enqueue the data");
      return AR_EFAILED;
   }
#endif // QUEUE_DISABLE_NEEDED

#ifdef DEBUG_POSAL_QUEUE
   uint32_t unOpcode = (uint32_t)(*payload_ptr >> 32);
   // filter out POSAL_DATA_BUFFER to avoid the flood.
   if (posal_globalstate.bEnableQLogging && 0x0000000AL != unOpcode)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "Q PushBk: Thrd=0x%x, Q=0x%x, Msg=0x%x 0x%x",
             posal_thread_get_id(),
             queue_ptr,
             unOpcode,
             (uint32_t)(*payload_ptr));
   }
#endif // DEBUG_POSAL_QUEUE
#ifdef DEBUG_QUEUE_LOW
   AR_MSG(DBG_HIGH_PRIO,
          "Queue push front 0x%x, payload 0x%x, active %lu, allocated %lu, max %lu",
          queue_ptr,
          payload_ptr->q_payload_ptr,
          queue_ptr->active_nodes,
          queue_ptr->num_nodes,
          queue_ptr->max_nodes);
#endif
   // Check for full queue
   if (queue_ptr->active_nodes == queue_ptr->max_nodes)
   {
      AR_MSG(DBG_ERROR_PRIO, "Queue error: OVERFLOWED QUEUE: Q=0x%p", queue_ptr);
      posal_queue_mutex_unlock(queue_ptr);
      return AR_ENEEDMORE;
   }

   posal_queue_element_list_t *node_ptr;
   // all currently allocated nodes are in use, get one more
   if (queue_ptr->active_nodes == queue_ptr->num_nodes)
   {
#ifdef DEBUG_POSAL_QUEUE_POOL
      AR_MSG(DBG_MED_PRIO,
             "Queue pool: Allocating new node, num nodes %lu qname %s",
             queue_ptr->num_nodes,
             queue_ptr->name);
#endif
      node_ptr = posal_queue_create_node(queue_ptr);
      if (NULL == node_ptr)
      {
         AR_MSG(DBG_FATAL_PRIO, "Queue error: unable to get node");
         posal_queue_mutex_unlock(queue_ptr);
         return AR_ENOMEMORY;
      }

      if (0 == queue_ptr->num_nodes)
      {
         // assign all the list pointers to this node
         queue_ptr->active_head_ptr = node_ptr;
         queue_ptr->active_tail_ptr = node_ptr;

         node_ptr->prev_ptr = node_ptr;
         node_ptr->next_ptr = node_ptr;
      }
      else // add new node to the front of the list list
      {
         node_ptr->next_ptr = queue_ptr->active_head_ptr;
         node_ptr->prev_ptr = queue_ptr->active_head_ptr->prev_ptr;

         queue_ptr->active_head_ptr->prev_ptr->next_ptr = node_ptr;
         queue_ptr->active_head_ptr->prev_ptr           = node_ptr;
         queue_ptr->active_head_ptr                     = node_ptr;
      }
      queue_ptr->num_nodes++;
   }
   else
   {
      // move to the next element only if currently pointing at an element, i.e. active_nodes>0
      if (0 < queue_ptr->active_nodes)
      {
         queue_ptr->active_head_ptr = queue_ptr->active_head_ptr->prev_ptr;
      }
   }

   // by the time we are here, active_tail_ptr is the place where we write the current payload
   queue_ptr->active_head_ptr->elem = *payload_ptr;
   queue_ptr->active_nodes++;

   // if signaling is disabled then don't set the signal
   if (!queue_ptr->disable_signaling)
   {
      // send the signal
      posal_channel_internal_t *ch_ptr = (posal_channel_internal_t *)queue_ptr->channel_ptr;
      posal_signal_set_target_inline(&ch_ptr->anysig, queue_ptr->channel_bit);
      // AR_MSG(DBG_MED_PRIO, "queue_push_back: queue_ptr=0x%p, *ch_ptr=0x%p, &ch_ptr->anysig=0x%p",queue_ptr, *ch_ptr,
      // &ch_ptr->anysig);
   }
   // release the mutex
   posal_queue_mutex_unlock(queue_ptr);

   return AR_EOK;
}
