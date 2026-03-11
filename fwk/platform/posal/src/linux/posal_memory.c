/**
 * \file posal_memory.c
 * \brief
 *  	This file contains a utility for memory allocation.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* ----------------------------------------------------------------------------
 * Include Files
 * ------------------------------------------------------------------------- */
#include "posal.h"
#include "posal_internal.h"
#include "posal_globalstate.h"
#include "posal_mem_prof.h"
#include "posal_memory_i.h"

/* ----------------------------------------------------------------------------
 * Global Declarations/Definitions
 * ------------------------------------------------------------------------- */
#define TRACK_MEM_STATS_TRUE TRUE

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/

bool_t posal_check_if_addr_within_heap_idx_range(uint32_t heap_table_idx, void *target_addr)
{
   bool_t addr_within_range = FALSE;

   return addr_within_range;
}

/*----------------------------------------------------------------------------------------------------------------------
 test framework has a reference to posal_memory_malloc_internal such that track_mem_stats can be passed as False
for test fwk. For all other purposes posal_memory_aligned_malloc should be directly used
the inline function helps heap tracker to look for original caller. Looks like heap walker looks at 2 levels deep in the
stack only
----------------------------------------------------------------------------------------------------------------------*/
static inline void *posal_memory_malloc_inline(uint32_t unBytes, POSAL_HEAP_ID origheapId, bool_t track_mem_stats)
{
   void *ptr = NULL;

   POSAL_HEAP_ID heapId         = GET_ACTUAL_HEAP_ID(origheapId);
   uint32_t      appended_bytes = unBytes;

   /*  check for zero size memory requests.*/
   if (0 == unBytes)
   {
      AR_MSG(DBG_ERROR_PRIO, "Thread 0x%x trying to allocate zero size memory!", posal_thread_get_curr_tid());
      return NULL;
   }

   /* check for requested simulated malloc failure (for robustness testing)
    * Decrement counter only if
    *   a) It is not steady state (Value = -1) (i.e. non-test mode) AND
    *   b) Malloc Fail Count is non zero, i.e. Mem failure has not yet been simulated  */
   if ((0 != posal_globalstate.nSimulatedMallocFailCount) && (-1 != posal_globalstate.nSimulatedMallocFailCount))
   {
      if (0 == --(posal_globalstate.nSimulatedMallocFailCount))
      {
         AR_MSG(DBG_ERROR_PRIO, "Simulated out of memory failure!!");
         return NULL;
      }
   }

   posal_mem_prof_pre_process_malloc(origheapId, &heapId, &appended_bytes);
#ifdef DEBUG_POSAL_MEMORY
   AR_MSG(DBG_HIGH_PRIO,
          "POSAL: Before Malloc origheapId = 0x%X, unBytes = %lu, heap id = 0x%X, appended bytes = %lu, count = %ld",
          origheapId,
          unBytes,
          heapId,
          appended_bytes,
          posal_globalstate.nSimulatedMallocFailCount);
#endif /* DEBUG_POSAL_MEMORY*/

   /* check if the heap-id is valid heap-id */
   if (heapId >= POSAL_HEAP_OUT_OF_RANGE)
   {
      AR_MSG(DBG_ERROR_PRIO, "HeapID provided is not valid!!");
      goto __posal_memory_malloc_end;
   }

   if (0 == posal_globalstate.nSimulatedMallocFailCount)
   {
      AR_MSG(DBG_ERROR_PRIO, "Already simulated memory failure! Returning NULL!");
      ptr = NULL;
      goto __posal_memory_malloc_end;
   }

   ptr = malloc(appended_bytes);

__posal_memory_malloc_end:
   posal_mem_prof_post_process_malloc(ptr, origheapId, (appended_bytes != unBytes));
   if (NULL == ptr)
   {

      AR_MSG(DBG_ERROR_PRIO,
             "POSAL: Malloc failed Requested Bytes: %lu, appended_bytes: %lu, threadID: 0x%p, origheapId: 0x%X, heapID "
             "%d",
             unBytes,
             appended_bytes,
             posal_thread_get_curr_tid(),
             origheapId,
             heapId);
   }
   else
   {
      if (track_mem_stats)
      {
         posal_memory_stats_update(ptr, IS_MALLOC, appended_bytes, origheapId);
      }
   }
#ifdef DEBUG_POSAL_MEMORY
   // AR_MSG(DBG_HIGH_PRIO,
   //        "POSAL: After Malloc ptr = 0x%X, origheapId = 0x%X, unBytes = %lu, heap id = 0x%X, appended bytes = %lu",
   //        ptr,
   //        origheapId,
   //        unBytes,
   //        heapId,
   //        appended_bytes);

#endif /* DEBUG_POSAL_MEMORY*/
   return ptr;
}

void *posal_memory_malloc_internal(uint32_t unBytes, POSAL_HEAP_ID origheapId, bool_t track_mem_stats)
{
   return posal_memory_malloc_inline(unBytes, origheapId, track_mem_stats);
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
void posal_memory_free_internal(void *ptr, bool_t track_mem_stats)
{

   uint32_t      heap_table_idx;
   POSAL_HEAP_ID heap_id;
   /* check for NULL.*/
   if (!ptr)
   {
      // AR_MSG( DBG_ERROR_PRIO, "Thread 0x%x trying to free null pointer!", posal_thread_get_tid());
      return;
   }

   posal_mem_prof_process_free(ptr);

   if (track_mem_stats)
   {
      posal_memory_stats_update(ptr, IS_FREE, 0, POSAL_DEFAULT_HEAP_INDEX);
   }
   free(ptr);
}

/*----------------------------------------------------------------------------------------------------------------------
see comment in the other inline function
----------------------------------------------------------------------------------------------------------------------*/
static inline void *posal_memory_aligned_malloc_inline(uint32_t      unBytes,
                                                       uint32_t      unAlignSize,
                                                       POSAL_HEAP_ID origheapId,
                                                       bool_t        track_mem_stats)
{
   POSAL_HEAP_ID heapId = GET_ACTUAL_HEAP_ID(origheapId);

   if (heapId >= POSAL_HEAP_OUT_OF_RANGE)
      return NULL;

   /* only allow alignment to 4 or more bytes.*/
   if (unAlignSize <= 4)
   {
      unAlignSize = 4;
   }

   /* keep out the crazy stuff.*/
   if (unAlignSize > (1 << 30))
   {
      return NULL;
   }

   char *   ptr, *ptr2, *aligned_ptr;
   uint64_t align_mask = ~(((uint64_t)unAlignSize) - 1);

   /* allocate enough for requested bytes + alignment wasteage + 1 word for storing offset*/
   /* (which will be just before the aligned ptr) */
   ptr = (char *)posal_memory_malloc_inline(unBytes + unAlignSize + sizeof(int), origheapId, track_mem_stats);
   if (ptr == NULL)
      return (NULL);
   /* allocate enough for requested bytes + alignment wasteage + 1 word for storing offset */

   ptr2        = ptr + sizeof(int);
   aligned_ptr = (char *)((uint64_t)(ptr2 - 1) & align_mask) + (uint64_t)unAlignSize;

   /* save offset to raw pointer from aligned pointer */
   ptr2           = aligned_ptr - sizeof(int);
   *((int *)ptr2) = (int)(aligned_ptr - ptr);

   return (aligned_ptr);
}

void *posal_memory_aligned_malloc_internal(uint32_t      unBytes,
                                           uint64_t      unAlignSize,
                                           POSAL_HEAP_ID origheapId,
                                           bool_t        track_mem_stats)
{
   return posal_memory_aligned_malloc_inline(unBytes, unAlignSize, origheapId, track_mem_stats);
}
/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
void posal_memory_aligned_free_internal(void *ptr, bool_t track_mem_stats)
{
   // Check for NULL first before using the ptr to calculate offsets
   if (NULL == ptr)
   {
      return;
   }

   uint32_t *pTemp = (uint32_t *)ptr;
   uint32_t *ptr2  = pTemp - 1;

   /* Get the base pointer address */
   pTemp -= *ptr2 / sizeof(uint32_t);

   /* Free the memory */
   posal_memory_free_internal(pTemp, track_mem_stats);
}

ar_result_t posal_memory_heapmgr_create(POSAL_HEAP_ID *heap_id_ptr,
                                        void *         heap_start_ptr,
                                        uint32_t       heap_size,
                                        bool_t         is_init_heap_needed)
{
   return AR_EOK;
}

ar_result_t posal_memory_heapmgr_destroy(POSAL_HEAP_ID origheapId)
{
   return AR_EOK;
}

void *posal_memory_malloc(uint32_t unBytes, POSAL_HEAP_ID origheapId)
{
   return posal_memory_malloc_inline(unBytes, origheapId, TRACK_MEM_STATS_TRUE);
}

void posal_memory_free(void *ptr)
{
   posal_memory_free_internal(ptr, TRACK_MEM_STATS_TRUE);
}

void *posal_memory_aligned_malloc(uint32_t unBytes, uint32_t unAlignSize, POSAL_HEAP_ID origheapId)
{
   return posal_memory_aligned_malloc_inline(unBytes, unAlignSize, origheapId, TRACK_MEM_STATS_TRUE);
}

void posal_memory_aligned_free(void *ptr)
{
   posal_memory_aligned_free_internal(ptr, TRACK_MEM_STATS_TRUE);
}

void posal_memory_aligned_free_v2(void *ptr, POSAL_HEAP_ID heapId)
{
   return posal_memory_aligned_free(ptr);
}
