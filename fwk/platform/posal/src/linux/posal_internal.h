/**
 *  \file posal_internal.h
 * \brief
 *      Internal definitions
 *
 * \copyright
 *      Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *      SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef POSAL_INTERNAL_H
#define POSAL_INTERNAL_H
/* ----------------------------------------------------------------------------
 * Include Files
 * ------------------------------------------------------------------------- */
#include "posal_memory.h"
#include <malloc.h>
#include "posal_internal_inline.h"

/* ----------------------------------------------------------------------------
 * Global Declarations/Definitions
 * ------------------------------------------------------------------------- */
// Number of pools of list nodes, same as number of heap ids it will use
#define SPF_POSAL_Q_NUM_POOLS (2)

extern uint32_t g_posal_queue_bufpool_handle[SPF_POSAL_Q_NUM_POOLS];

#define SIZE_OF_ARRAY(a) (sizeof(a) / sizeof((a)[0]))

/* -------------------------------------------------------------------------
 * Function Declarations/Definitions
 * ------------------------------------------------------------------------- */
static inline void posal_queuepool_set_handle_from_heap_id(POSAL_HEAP_ID heap_id, uint32_t handle)
{
   heap_id                               = GET_ACTUAL_HEAP_ID(heap_id);
   if (heap_id < SIZE_OF_ARRAY(g_posal_queue_bufpool_handle))
   {
		g_posal_queue_bufpool_handle[heap_id] = handle;
   }
}

static inline uint32_t posal_queuepool_get_handle_from_heap_id(POSAL_HEAP_ID heap_id)
{
   heap_id = GET_ACTUAL_HEAP_ID(heap_id);
   if (heap_id < SIZE_OF_ARRAY(g_posal_queue_bufpool_handle))
   {
		return g_posal_queue_bufpool_handle[heap_id];
   }
   return 0;
}

/* -----------------------------------------------------------------------
 ** Data Structures & types
 ** ----------------------------------------------------------------------- */

/** Internal thread object structure**/
typedef struct
{
  int64_t tid;
  pthread_t thread_handle;
  void *arg;
  ar_result_t (*pfStartRoutine)(void *);
  void *stack_ptr;
  void *thread_profile_obj_ptr;
} _thread_args_t;

/* -----------------------------------------------------------------------
** Constant / Define Declarations
** ----------------------------------------------------------------------- */

/** Structure that contains information for registering an interrupt.

  The client must allocate the memory for this structure before calling
  posal_interrupt_register(). The client is not required to initialize any
  structure variables.
*/
typedef struct
{

   char_t ist_thread_name[16];
   /**< IST thread name. */

   uint16_t intr_id;
   /**< Interrupt to register. */

   posal_thread_t thread_id;
   /**< Used to join the IST thread. */

   uint32_t ist_state;
   /**< Checks whether the IST registered the interrupt successfully. */

   void (*ist_callback)(void *);
   /**< Pointer to the thread entry function. */

   uint32_t semaphore;
   /**< Semaphore used to synchronize a caller and an IST. */

   void *arg_ptr;
   /**< Pointer to the thread arguments sent by the client. @newpagetable */
} posal_interrupt_ist_internal_t;

/* Main work loop for IST thread */
ar_result_t posal_interrupt_workloop(void *arg_ptr);

/**
  Initializes the global memory map client list.

  @return

  @dependencies
  None.
*/
void posal_memorymap_global_init();

/**
  De-initializes the global memory map client list.
  De registers the client and un maps all the nodes.

  @return

  @dependencies
  None.
*/
void posal_memorymap_global_deinit();

/**
  Initializes the heap table entries for the heap manager.

  @return
  None.

  @dependencies
  None.
*/
void posal_heap_table_init(void);

/**
  Initializes the variables used for maintaining memory usage statistics for
  AVS and non-AVS threads.

  @return
  None.

  @dependencies
  None.
*/

void posal_memory_stats_init(void);

/**
  De-initializes the variables used for maintaining memory usage statistics for
  AVS and non-AVS threads.

  @return
  None.

  @dependencies
  None.
*/
void posal_memory_stats_deinit(void);

/**
  Updates the stats.

  @return
  None.

  @dependencies
  None.
*/
void posal_memory_stats_update(void *ptr, uint32_t is_malloc, uint32_t bytes, POSAL_HEAP_ID origheapId);

/**
  Prints backtrace.

  @return
  None.

  @dependencies
  None.
*/
void posal_print_backtrace(uint32_t size, void *start_addr);

#endif // POSAL_BUFMGR_I_H
