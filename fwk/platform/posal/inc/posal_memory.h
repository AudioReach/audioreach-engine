/**
 * \file posal_memory.h
 * \brief
 *       This file contains utilities for memory allocation and release. This file provides memory allocation functions
 * and macros for both C and C++.
 *
 * \copyright
 *    Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *    SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef POSAL_MEMORY_H
#define POSAL_MEMORY_H

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "posal_types.h"
#include "posal_tgt_util.h"

#ifdef __cplusplus
#include <new>
extern "C" {
#endif //__cplusplus

/** @addtogroup posal_memory
@{ */

/*--------------------------------------------------------------*/
/* Macro definitions                                            */
/* -------------------------------------------------------------*/

/* Enable for memory debugging. to debug using table : #define DEBUG_POSAL_MEMORY_USING_TABLE 1 in posal_global_state.h
 */
// #define DEBUG_POSAL_MEMORY 1
// #define DEBUG_POSAL_MEM_PROF 1
//#define HEAP_PROFILING 1

//#define DEBUG_POSAL_MALLOC_CALLSTACK

#define POSAL_HEAP_ID_ISLAND_BIT_MASK ((uint32_t)0x8)

#define HEAP_ID_MASK ((uint32_t)0x7)

#define HEAP_ID_SHIFT (u32_popcount_u64(POSAL_HEAP_ID_ISLAND_BIT_MASK | HEAP_ID_MASK))

#define HEAP_ID_MASK_WITH_ISLAND_INFO (POSAL_HEAP_ID_ISLAND_BIT_MASK | HEAP_ID_MASK)

// Gives 0 (default heap), 1 (Q6 TCM), 2 (LPASS TCM), ... 7
#define GET_ACTUAL_HEAP_ID(heapId) ((POSAL_HEAP_ID)((uint32_t)heapId & HEAP_ID_MASK))

// Gives 0 (default heap), 0x9 (Q6 TCM), 0xA (LPASS TCM)  [0x8 is not a valid heap id]
// [We already know what's island heap from actual heap ID, but for ease of checking bit3 is set in island heap-ids]
#define GET_HEAP_ID_WITH_ISLAND_INFO(heapId) ((POSAL_HEAP_ID)((uint32_t)heapId & HEAP_ID_MASK_WITH_ISLAND_INFO))

// Gives top 24 bit tracking information
#define GET_TRACKING_ID_FROM_HEAP_ID(heapId) ((uint32_t)heapId & ~HEAP_ID_MASK_WITH_ISLAND_INFO)

#define MODIFY_HEAP_ID_FOR_MEM_TRACKING(tracking_id, actual_heap_id) ((GET_TRACKING_ID_FROM_HEAP_ID(tracking_id)) | actual_heap_id)

#define MODIFY_STATIC_MODULE_HEAP_ID_FOR_MEM_TRACKING(module_id, actual_heap_id) (GET_TRACKING_ID_FROM_HEAP_ID(module_id << HEAP_ID_SHIFT) | actual_heap_id)

#ifdef HEAP_PROFILING
// For tracking purposes, use 1 as the ID for Fwk allocations
// Using tracking_id=1 may throw off IRM.
#define MODIFY_HEAP_ID_FOR_FWK_ALLOC_FOR_MEM_TRACKING(actual_heap_id) MODIFY_STATIC_MODULE_HEAP_ID_FOR_MEM_TRACKING(0xA, actual_heap_id)
#else
#define MODIFY_HEAP_ID_FOR_FWK_ALLOC_FOR_MEM_TRACKING(actual_heap_id) (actual_heap_id)
#endif

/** Index of the default heap. @newpage */
#define POSAL_DEFAULT_HEAP_INDEX 0

#define POSAL_HEAP_MGR_HEAP_INDEX_START 1
#define POSAL_HEAP_MGR_HEAP_INDEX_END POSAL_HEAP_MGR_MAX_NUM_HEAPS
#define IS_MALLOC 1
#define IS_FREE 0

/*Returns heap ID based on heap table index */
#define HEAP_ID_FROM_HEAP_TABLE_INDEX(index) (index + POSAL_HEAP_MGR_HEAP_INDEX_START)

/*Returns  heap table index based on actual heap ID */
#define HEAP_TABLE_INDEX_FROM_HEAP_ID(actual_heap_id) (actual_heap_id - POSAL_HEAP_MGR_HEAP_INDEX_START)


//#define ENABLE_SAFE_HEAP  1

// Specific memory alignment is not needed, alignment requirement is zero
#define POSAL_MEM_ALIGN_UNSPECIFIED 0

/* -----------------------------------------------------------------------
** Global definitions/forward declarations
** ----------------------------------------------------------------------- */

/* Memory Types supported */
typedef enum
{
   POSAL_MEM_TYPE_DEFAULT, // Default memory type
   POSAL_MEM_TYPE_LOW_POWER, // Low-power memory type
   POSAL_MEM_TYPE_LOW_POWER_2, // Low-power 2 memory type
   POSAL_MEM_TYPE_NUM_SUPPORTED
} posal_mem_t;

typedef enum
{
   POSAL_HEAP_NON_ISLAND,          // non island (DDR) memory
   POSAL_HEAP_ISLAND,              // island memory (TCM, LLC) supported
   POSAL_HEAP_LPM,                 // LPM memory
   POSAL_HEAP_NON_ISLAND_SAFE_HEAP // non island (DDR) memory safe heap
} posal_heap_t;

/** ID of the available heap in the system.  */
typedef enum
{
   POSAL_HEAP_DEFAULT = 0,
   /**< Default heap value. */

   POSAL_HEAP_OUT_OF_RANGE = POSAL_HEAP_MGR_MAX_NUM_HEAPS + 1,
   /**< Heap value is out of range. @newpagetable */ // keep at end

   POSAL_HEAP_INVALID = 0xFFFFFFFF // to make this 32 bits

} POSAL_HEAP_ID;

/* Global declaration of the island heap id variable */
extern POSAL_HEAP_ID spf_mem_island_heap_id;

/* Macro that checks whether a specific heap id is an island heap id or not */
#define POSAL_IS_ISLAND_HEAP_ID(heap_id) (((uint32_t)heap_id & POSAL_HEAP_ID_ISLAND_BIT_MASK)== POSAL_HEAP_ID_ISLAND_BIT_MASK)
/**
  Allocates memory with an option to specify a heap identifier.

  @datatypes
  #POSAL_HEAP_ID

  @param[in] unBytes Number of bytes to allocate.
  @param[in] heapId   ID of the heap from which to allocate memory.

  @return
  Pointer to the allocated block, or NULL if the request failed.

  @dependencies
  None
*/
void *posal_memory_malloc(uint32_t unBytes, POSAL_HEAP_ID heapId);

/**
  Frees memory that was allocated only with posal_memory_malloc().

  @param[in] ptr  Pointer to the memory to free.

  @return
  None.

  @dependencies
  None.
  @newpage
*/
void posal_memory_free(void *ptr);

/**
  Allocates memory with options to align to a power of 2 and to
  specify a heap identifier.

  @datatypes
  #POSAL_HEAP_ID

  @param[in] unBytes      Number of bytes to allocate.
  @param[in] unAlignBits  Number of alignment bytes.
                          This value must be a power of 2.
  @param[in] heapId       ID of the heap from which to allocate memory.

  @return
  Pointer to the allocated block, or NULL if the request failed.

  @dependencies
  None.
*/
void *posal_memory_aligned_malloc(uint32_t unBytes, uint32_t unAlignBits, POSAL_HEAP_ID heapId);

/**
  Frees memory that was allocated only with posal_memory_aligned_malloc().

  @param[in] ptr - Pointer to the aligned memory to free.

  @return
  None.

  @dependencies
  Before calling this function, the object must be created and initialized.
  @newpage
*/
void posal_memory_aligned_free(void *ptr);

/**
  Determines if memory allocated was from Island heap id (TCM).

  @param[in] ptr - Pointer to the memory to check.

  @return
  Boolean determining if memory was allocated from TCM

  @dependencies
  Before calling this function, the object must be created and initialized.
*/
bool_t posal_is_memory_in_tcm(void *ptr);

/**
This function returns TRUE or FALSE whether the input VA falls in TCM memory or not.

@return  TRUE if address is within TCM heap manager addresses.
         FALSE if address is not in TCM heap manager addresses.
*/
bool_t posal_check_addr_from_tcm_island_heap_mgr(void *virt_addr_ptr);

/**
This function returns name of TCM island based on heap id

@return  char array representing name of TCM heap id
*/
char *posal_tcm_island_heap_mgr_get_name(POSAL_HEAP_ID origheapId);

/** @} */ /* end_addtogroup posal_memory */

#ifdef __cplusplus

/**
  Allocates a C++ object using posal_memory_malloc().

  This macro takes the pointer, type, and heap ID and does the following:
  - Allocates memory
  - Follows with a <i>placement new</i>, which calls the constructor of the
    object

  This macro takes extra arguments that can be passed to the constructor
  of the C++ object. This does not cause any exceptions. If the
  allocation fails, the returned pointer is NULL.

  @param[out] pObject   Pointer to set to the new object location.
  @param[in]  typeType  Type of class to construct.
  @param[in]  heapId    POSAL_HEAP_ID is requested for allocating the
                        new object.
  @param[in]  ...       Variable argument list for parameters that are
                        passed to the constructor.
  @hideinitializer
*/
#define posal_memory_new(pObject, typeType, heapId, ...)                                                               \
   {                                                                                                                   \
      void *pObj = posal_memory_malloc(sizeof(typeType), heapId);                                                      \
      (pObject)  = (pObj) ? (new (pObj)(typeType)(__VA_ARGS__)) : NULL;                                                \
   }

/**
  Destroys an object created with posal_memory_new().
  After destroying the object, the macro frees the memory and sets the
  pointer to NULL.

  @param[in,out] pObject   Pointer to the object to be destroyed.
  @param[in]     typeType  Class of the object to be destroyed.

  @hideinitializer @newpage
*/
#define posal_memory_delete(pObject, typeType)                                                                         \
   {                                                                                                                   \
      if (pObject)                                                                                                     \
      {                                                                                                                \
         (pObject)->~typeType();                                                                                       \
         posal_memory_free(pObject);                                                                                   \
         (pObject) = NULL;                                                                                             \
      }                                                                                                                \
   }

/**
  Macro that takes a pointer to the intended class, a pointer to the allocated
  memory, and the type of class, and then invokes the constructor of the class.

  This macro can take extra arguments that might be passed to the
  constructor of the C++ object. This does not cause any exceptions.

  @param[out] pObj      Pointer to the intended class whose constructor
                        is to be invoked.
  @param[in]  pMemory   Pointer to memory that has been allocated.
  @param[in]  typeType  Type of class to construct.
  @param[in]  ...       Variable argument list for parameters passed to
                        the constructor.

  @hideinitializer
*/
#define posal_memory_placement_new(pObj, pMemory, typeType, ...)                                                       \
   {                                                                                                                   \
      (pObj) = (new (pMemory)(typeType)(__VA_ARGS__));                                                                 \
   }

/**
  Destroys an object created with posal_memory_placement_new().
  This macro invokes the destructor of the intended class and sets the
  pointer to NULL.

  @param[in,out] pObject   Pointer to the object to be destroyed.
  @param[in]     typeType  Class of the object to be destroyed.

  @hideinitializer
*/
#define posal_memory_placement_delete(pObject, typeType)                                                               \
   {                                                                                                                   \
      if (pObject)                                                                                                     \
      {                                                                                                                \
         (pObject)->~typeType();                                                                                       \
         (pObject) = NULL;                                                                                             \
      }                                                                                                                \
   }

#endif //__cplusplus for new and delete

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef POSAL_MEMORY_H
