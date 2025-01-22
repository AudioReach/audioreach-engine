/**
 * \file posal_queue.h
 * \brief
 *  	 This file contains the queue utilities. Queues must be created and added
 *  	 to a channel before they can be used. Queues are pushed from the back and can be
 *  	 popped from either front(FIFO) or back(LIFO). Queues must be destroyed when
 *  	 they are no longer needed.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef POSAL_QUEUE_H
#define POSAL_QUEUE_H

#include "posal_types.h"
#include "posal_std.h"
#include "posal_channel.h"
#include "posal_mutex.h"
#include "posal_signal.h"
#include "posal_memory.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/** @addtogroup posal_queue
@{ */

//#define DEBUG_WITH_QUEUE_NAME
/** Maximum number of characters to use in resource names
  (e.g., thread names).
*/
#define POSAL_DEFAULT_NAME_LEN 16

//#ifndef ALIGN_8_BYTES
//#define ALIGN_8_BYTES(a) ((a + 7) & (0xFFFFFFF8))
//#endif

/** Queue that transfers messages between threads.
  The queue implementation is signal based and thread safe. Each queue node is
  always 8 bytes. The number of queue nodes is sized up to powers of 2.

  A queue must be associated with a channel.
 */

typedef struct posal_queue_t posal_queue_t;
typedef struct posal_queue_element_t posal_queue_element_t;


/** Structure containing the attributes to be associated with type posal_queue_t
 */
typedef struct posal_queue_init_attr_t
{
   char_t        name[POSAL_DEFAULT_NAME_LEN];
   /**< Name of the queue. */
   int32_t       max_nodes;
   /**< Max number of queue nodes. */
   int32_t       prealloc_nodes;
   /**< Number of preallocated nodes */
   POSAL_HEAP_ID heap_id;
   /**< Heap ID from which nodes are to be allocated. */
   bool_t        is_priority_queue;
   /**< FALSE: default FIFO queue, TRUE: Priority queue. */
}posal_queue_init_attr_t;

/*
 * Should be called at boot to create pool of nodes to be used
 * for the queue
 */

ar_result_t posal_queue_pool_setup(POSAL_HEAP_ID, uint32_t num_arrays, uint16_t nodes_per_arr);

/*
 * Called only for test framework to not cause mem leaks to be detected
 */
void posal_queue_pool_reset(POSAL_HEAP_ID heap_id);

/*
 * Called only for test framework to not cause mem leaks to be detected
 */
void posal_queue_pool_destroy(POSAL_HEAP_ID heap_id);

/**
  Create a queue with specific attributes.

  @datatypes
  posal_queue_t, posal_queue_init_attr_t

  @param[in]     attr_ptr   Pointer to the attributes of the queue to be initialized
  @param[in,out] q_ptr      Pointer to the initialized queue.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  None. @newpage
*/
ar_result_t posal_queue_create_v1(posal_queue_t** queue_pptr, posal_queue_init_attr_t *attr_ptr);

/**
  Initializes queue with specific attributes at location q_ptr. Requires at least posal_queue_get_size() bytes allocated
  at q_ptr.

  @datatypes
  posal_queue_t, posal_queue_init_attr_t

  @param[in,out] q_ptr      Pointer to the initialized queue.
  @param[in]     attr_ptr   Pointer to the attributes of the queue to be initialized

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  None. @newpage
*/
ar_result_t posal_queue_init(posal_queue_t* q_ptr, posal_queue_init_attr_t *attr_ptr);

/**
  Destroys a queue.

  @note1hang This function will be deprecated in a future release, at which
             time posal_queue_deinit() must be used instead.

  @datatypes
  posal_queue_t

  @param[in,out] q_ptr   Pointer to the queue.

  @return
  None.

  @dependencies
  Before calling this function, the object must be created and initialized.
*/
void posal_queue_destroy(posal_queue_t* q_ptr);

/**
  Deinits a queue.


  @datatypes
  posal_queue_t

  @param[in,out] q_ptr   Pointer to the queue.

  @return
  None.

  @dependencies
  Before calling this function, the object must be created and initialized.
*/
void posal_queue_deinit(posal_queue_t* q_ptr);

/**
  Disables a queue.

  @datatypes
  posal_queue_t

  @param[in] q_ptr   Pointer to the queue.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
ar_result_t posal_queue_disable(posal_queue_t* q_ptr);

/**
  Enables/Disables the signing for a queue. By default, signaling is enabled.

  @datatypes
  posal_queue_t

  @param[in] q_ptr      Pointer to the queue.
  @param[in] is_enable  True: enables the signaling, False: disables the signaling

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
ar_result_t posal_queue_enable_disable_signaling(posal_queue_t *q_ptr, bool_t is_enable);

/**
  Pushes an item onto a queue.

  @datatypes
  posal_queue_t

  @param[in] q_ptr     Pointer to the queue.
  @param[in] pPayload  Pointer to the address (location) of the item. \n
                       The item is pushed (copied) onto the queue, not the
                       queue address.

  @detdesc
  This function is nonblocking. The user is responsible for not overflowing
  the queue to avoid getting an unwanted assertion.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
ar_result_t posal_queue_push_back(posal_queue_t* q_ptr, posal_queue_element_t* payload_ptr);

/**
  Pushes an item to Front of the queue.

  @datatypes
  posal_queue_t

  @param[in] q_ptr     Pointer to the queue.
  @param[in] pPayload  Pointer to the address (location) of the item. \n
                       The item is pushed (copied) onto the queue, not the
                       queue address.

  @detdesc
  This function is nonblocking. The user is responsible for not overflowing
  the queue to avoid getting an unwanted assertion.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
ar_result_t posal_queue_insert_front(posal_queue_t *q_ptr, posal_queue_element_t *payload_ptr);

/**
  Pushes an item onto a priority queue.

  @datatypes
  posal_queue_t

  @param[in] q_ptr     Pointer to the queue.
  @param[in] pPayload  Pointer to the address (location) of the item. \n
                       The item is pushed (copied) onto the queue, not the
                       queue address.
  @param[in] priority  Priority number, higher value is higher priority.

  @detdesc
  This function is nonblocking. The user is responsible for not overflowing
  the queue to avoid getting an unwanted assertion.

  Queue should have been initialized with priority attribute.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
ar_result_t posal_queue_push_back_with_priority(posal_queue_t *q_ptr, posal_queue_element_t *payload_ptr, uint32_t priority);

/**
  Pops an item from the front of a queue.

  @datatypes
  posal_queue_t

  @param[in] q_ptr        Pointer to the queue.
  @param[in] pPayload  Pointer to the target address (location) for the item
                       that is popped.

  @detdesc
  This function is nonblocking and returns AR_ENOMORE if it is empty.
  @par
  Typically, the client calls this function only after waiting for a channel
  and checking whether this queue contains any items.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
ar_result_t posal_queue_pop_front(posal_queue_t* q_ptr,  posal_queue_element_t* payload_ptr);

/**
  Peeks the next item in the queue based on the iterator position.

  @datatypes
  posal_queue_t

  @param[in] q_ptr     Pointer to the queue.
  @param[in] pPayload  Pointer to the target address (location) for the
                       address of the item that is peeked into.
  @param[in] iterator_pptr Pointer to the target address of iterator.

  @detdesc
  This function is nonblocking and returns AR_ENOMORE if queue empty or iterator reached
  to the last element.
  @par
  Client should keep the queue locked while using iterator,
  this is to prevent any other thread to push or pop from the queue.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, queue object must be created and initialized.
  @newpage
*/
ar_result_t posal_queue_peek_forward(posal_queue_t *q_ptr, posal_queue_element_t **payload_ptr, void **iterator_pptr);

/**
  Peeks an item from the front of a queue.

  @datatypes
  posal_queue_t

  @param[in] q_ptr        Pointer to the queue.
  @param[in] pPayload  Pointer to the target address (location) for the
                       address of the item that is peeked into.

  @detdesc
  This function is nonblocking and returns AR_ENOMORE if it is empty.
  @par
  Typically, the client calls this function only after waiting for a channel
  and checking whether this queue contains any items.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
ar_result_t posal_queue_peek_front(posal_queue_t* q_ptr, posal_queue_element_t **payload_ptr);

/**
  Pops an item from the back of a queue.

  @datatypes
  posal_queue_t

  @param[in] q_ptr        Pointer to the queue.
  @param[in] pPayload  Pointer to the target address (location) for the item
                       that is popped.

  @detdesc
  This function is for LIFO queues. It is nonblocking and returns AR_ENOMORE
  if it is empty.
  @par
  Typically, the client calls this function only after waiting for a channel
  and checking whether this queue contains any items.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
ar_result_t posal_queue_pop_back(posal_queue_t* q_ptr,  posal_queue_element_t* payload_ptr );

/**
  Queries a queue for its channel.

  @datatypes
  posal_queue_t

  @param[in] q_ptr   Pointer to the queue.

  @return
  A handle to the channel containing the queue.

  @dependencies
  Before calling this function, the object must be created and initialized.
*/
posal_channel_t posal_queue_get_channel(posal_queue_t* q_ptr);

/**
  Queries a queue for its channel bitfield.

  @datatypes
  posal_queue_t

  @param[in] q_ptr   Pointer to the queue.

  @detdesc
  This value is a 32-bit value with a single bit=1.
  @par
  Use this function to find the values to be ORed together to form an enable
  bitfield for a combination of queues in a channel.

  @return
  A bitfield with this queue's bit set to 1.

  Before calling this function, the object must be created and initialized.
*/
uint32_t posal_queue_get_channel_bit(posal_queue_t* q_ptr);

/**
  Checks if a trigger is received for the queue.

  @datatypes
  posal_queue_t

  @param[in] q_ptr          Pointer to the queue.
  @detdesc
  This is a non blocking operation which polls/checks if a signal is set on the
  queue indicating that the queue is not empty.

  @return
  Returns a non zero value if the queue signal is set. The value is equal to
  queue's bit mask.

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
uint32_t posal_queue_poll(posal_queue_t* q_ptr);

/**
  Gets the memory size required by a queue.
  @return
  Returns the size in bytes required for the queue
  @newpage
*/
uint32_t posal_queue_get_size();


/****************************************************************************
** Queue helpers
*****************************************************************************/
/** Setup the default attributes for the queue */
static inline void posal_queue_attr_init(posal_queue_init_attr_t *attr_ptr)
{
   attr_ptr->name[0]           = 0;
   attr_ptr->prealloc_nodes    = 0;
   attr_ptr->max_nodes         = 0;
   attr_ptr->heap_id           = POSAL_HEAP_DEFAULT;
   attr_ptr->is_priority_queue = FALSE;
}

/** Setup the attribute 'name' for the queue */
static inline void posal_queue_attr_set_name(posal_queue_init_attr_t *attr_ptr, char_t *name_ptr)
{
   posal_strlcpy(attr_ptr->name, name_ptr, POSAL_DEFAULT_NAME_LEN);
}

/** Setup the attribute 'max_nodes' for the queue */
static inline void posal_queue_attr_set_max_nodes(posal_queue_init_attr_t *attr_ptr, int max_nodes)
{
   attr_ptr->max_nodes = max_nodes;
}

/** Setup the attribute 'prealloc_nodes' for the queue */
static inline void posal_queue_attr_set_prealloc_nodes(posal_queue_init_attr_t *attr_ptr, int prealloc_nodes)
{
   attr_ptr->prealloc_nodes = prealloc_nodes;
}

/** Setup the attribute 'heap_id' for the queue */
static inline void posal_queue_attr_set_heap_id(posal_queue_init_attr_t *attr_ptr, POSAL_HEAP_ID heap_id)
{
   attr_ptr->heap_id = heap_id;
}

/** Setup the attribute 'heap_id' for the queue */
static inline void posal_queue_attr_set_priority_queue_mode(posal_queue_init_attr_t *attr_ptr, bool_t is_priority_queue)
{
   attr_ptr->is_priority_queue = is_priority_queue ? TRUE : FALSE;
}

/**
  Locks the mutext for the queue.

  @datatypes
  posal_queue_t

  @param[in] q_ptr          Pointer to the queue.

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
void posal_queue_lock_mutex(posal_queue_t* q_ptr);

/**
  Unlocks the mutext for the queue.

  @datatypes
  posal_queue_t

  @param[in] q_ptr          Pointer to the queue.

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
void posal_queue_unlock_mutex(posal_queue_t* q_ptr);




/** Function to set up all the available queue attributes   */
ar_result_t posal_queue_set_attributes(posal_queue_init_attr_t *q_attr_ptr,
                                       POSAL_HEAP_ID            heap_id,
                                       uint32_t                 num_max_q_elem,
                                       uint32_t                 num_max_prealloc_q_elem,
                                       char_t *                 q_name_ptr);

/** @} */ /* end_addtogroup posal_queue */

/** function to get the fullness of queue
 *
 * It returns the current number of element in the queue
 */
uint32_t posal_queue_get_queue_fullness(posal_queue_t* q_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef POSAL_QUEUE_H
