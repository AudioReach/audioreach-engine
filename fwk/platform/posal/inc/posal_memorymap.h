/**
 * \file posal_memorymap.h
 * \brief
 *  	 This file contains utilities for memory mapping and unmapping of shared memory.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *    SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef POSAL_MEMORYMAP_H
#define POSAL_MEMORYMAP_H

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "posal_memory.h"
#include "posal_cache.h"
#include "posal_mutex.h"
#include "posal_types.h"
#include "ar_error_codes.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/** @addtogroup posal_memorymap
@{ */

//***************************************************************************
// POSAL_MEMORYPOOLTYPE
//***************************************************************************
extern const char posal_memorymap_pool_name[][32];

/** Valid memory map pool IDs. */
typedef enum
{
   POSAL_MEMORYMAP_DEFAULTPHY_POOL = 0,                    /**< Default physical pool. */
   POSAL_MEMORYMAP_AUDIO_DYNAMIC_POOL,                     /**< Dynamic Pool - created during usecase */
   POSAL_MEMORYMAP_SMI_POOL,                               /**< Stacked memory interface pool. */
   POSAL_MEMORYMAP_SHMEM8_4K_POOL,                         /**< Shared memory; 8-byte addressable,
                                                                    4 KB aligned memory pool. */
   POSAL_MEMORYMAP_POOL_OUT_OF_RANGE /**< Out of range. */ // keep at end
} POSAL_MEMORYPOOLTYPE;

/** Valid memory mapping modes. */
typedef enum
{
   POSAL_MEMORYMAP_PHYSICAL_ADDR_MAPPING = 0,
   /**< Physical memory is mapped. */

   POSAL_MEMORYMAP_PHYSICAL_OFFSET_MAPPING,
   /*Address offset is mapped - Phy map*/

   POSAL_MEMORYMAP_VIRTUAL_ADDR_MAPPING,
   /**< Virtual memory is mapped. */

   POSAL_MEMORYMAP_VIRTUAL_OFFSET_MAPPING
   /*Address offset is mapped - Virt map*/
} posal_memorymap_mapping_mode_t;

/** Linked list of memory regions.
 */
typedef struct posal_memorymap_node_t posal_memorymap_node_t;

struct posal_memorymap_node_t
{
   uint32_t shmem_id;
   /**< A unique identifier to map with this shared memory regions
    * */

   uint32_t MemPool;
   /**< Memory pool from which the memory region is created. */

   uint16_t unNumContPhysReg;
   /**< Number of physical memory regions in this node. */

   int16_t ref_count;
   /**< Reference count that the client can increment to lock this memory map
        handle.

        Unmapping can only be performed if ref_count reaches zero.

        The client must decrement ref_count when it does not use this memory
        map handle. */

   uint32_t mapping_mode;
   /**< Specifies whether the mapping is physical or virtual, or if it is a
        physical offset. */

   uint32_t reserved;
   /**< reserved field to ensure this structure size is 64 bytes aligned. */

   posal_memorymap_node_t *pNext;
   /**< Pointer to the next node in the linked list.

        @tblsubhd{If unNumContPhysReg is greater than 1}
        In addition to the number of posal_memorymap_region_record_t
        structures following this structure, one more ContPhysReg follows to
        represent the master region for all ContPhysRegs (called the <i>
        virtual memory region</i>).

        When freeing the regions, all of the following space is also freed. */
};

/** Contiguous shared memory region, with the start address and size.
 */
typedef struct
{
   uint32_t shm_addr_lsw;
   /**< Lower 32 bits of the shared memory address of the memory region to
        map. */

   uint32_t shm_addr_msw;
   /**< Upper 32 bits of the shared memory address of the memory region to
        map.

        The 64-bit number formed by shm_addr_lsw and shm_addr_msw word must be
        contiguous memory, and it must be 4 KB aligned.

        @values
        - For a 32-bit shared memory address, this field must be set to 0.
        - For a 36-bit shared memory address, bits 31 to 4 must be set to 0.
        - For a 64-bit shared memory address, any 32 bit value.
        @tablebulletend */

   uint32_t mem_size;
   /**< Size of the shared memory region.

        Number of bytes in the shared memory region.

        @values Multiples of 4 KB

        Underlying operating system must always map the regions as virtual contiguous memory, but
        the memory size must be in multiples of 4 KB to avoid gaps in the
        virtually contiguous mapped memory. @newpagetable */
} posal_memorymap_shm_region_t;

/** Memory mapped region attributes. */
typedef struct
{
   uint32_t base_phy_addr_lsw;
   /**< Lower 32 bits of the 64-bit memory region start (base) physical
        address. */

   uint32_t base_phy_addr_msw;
   /**< Upper 32 bits of the 64-bitmemory region start (base) physical
        address.

         The 64-bit number formed by mem_reg_base_phy_addr_lsw and
         mem_reg_base_phy_addr_msw word must be contiguous memory, and it must
         be 4 KB aligned.

         @values
         - For a 32-bit shared memory address, this field must be set to 0.
         - For a 36-bit shared memory address, bits 31 to 4 must be set to 0.
         - For a 64-bit shared memory address, any 32-bit value.
         @tablebulletend */

   uint32_t mem_reg_size;
   /**< Size of the shared memory region.

        Number of bytes in the shared memory region.

        @values Multiples of 4 KB

        Underlying operating system must always map the regions as virtual contiguous memory, but
        the memory size must be in multiples of 4 KB to avoid gaps in the
        virtually contiguous mapped memory. */

   uint32_t base_virt_addr;
   /**< Memory region start (base) virtual address. */

   uint32_t req_virt_adrr;
   /**< Virtual address that corresponds to the requested physical address. */

   uint32_t rem_reg_size;
   /**< Remaining memory region size from the requested physical address,
        including the requested physical address:

        ([mem_reg_base_phy_addr_msw,mem_reg_base_phy_addr_lsw] +
        mem_reg_size - [requested physical address]) @newpagetable */

} posal_memorymap_mem_region_attrib_t;

/**
  Registers a client with posal_memorymap.

  @param[out] client_token_ptr  Pointer to an instance of
                         posal_memorymap_client_t that is
                         created and returned as a handle/token to the client.
                         \n
                         This handle uniquely identifies the client, and the
                         client must use this handle for future communication
                         with posal_memorymap.
  @param[in]  heap_id    Heap id used for malloc.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  None.
*/
ar_result_t posal_memorymap_register(uint32_t *client_token_ptr, POSAL_HEAP_ID heap_id);

/**
  Deletes all regions tagged to this client and unregisters this client from
  posal_memorymap.

  @param[in] client_token  Client token.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the client object must be registered. @newpage
*/
ar_result_t posal_memorymap_unregister(uint32_t client_token);

/**
  Maps the shared memory and adds the memory region to the client linked list.
  This function enables physical address mapping only.

  @datatypes
  posal_memorymap_shm_region_t \n
  #POSAL_MEMORYPOOLTYPE

  @param[in]  client_token           Client token.
  @param[in]  shm_mem_reg_ptr        Pointer to an array of shared memory
                                     regions to map.
  @param[in]  num_shm_reg            Number of shared memory regions in the
                                     array.
  @param[in]  is_cached              Indicates if mem is cached or uncached
  @param[in]  is_offset_map          Indicates if the mapping is offset based
                                     as opposed to pointer based.
  @param[in]  pool_id                Memory pool ID to which this region is
                                     mapped.
  @param[out] shm_mem_map_handle_ptr Pointer to the memory map handle of the
                                     shared memory region created. This handle
                                     can be used later to unmap the shared
                                     memory.
  @param[in]  heap_id                Heap id used for malloc.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the client object must be registered. @newpage
*/
ar_result_t posal_memorymap_shm_mem_map(uint32_t                      client_token,
                                        posal_memorymap_shm_region_t *shm_mem_reg_ptr,
                                        uint16_t                      num_shm_reg,
                                        bool_t                        is_cached,
                                        bool_t                        is_offset_map,
                                        POSAL_MEMORYPOOLTYPE          pool_id,
                                        uint32_t                     *shm_mem_map_handle_ptr,
                                        POSAL_HEAP_ID                 heap_id);

/**
  Maps the shared virtual address and adds the memory region to the client
  linked list. This function must be used to map a virtual address.

  @datatypes
  posal_memorymap_shm_region_t \n
  #POSAL_MEMORYPOOLTYPE

  @param[in]  client_token           Client token.
  @param[in]  shm_mem_reg_ptr        Pointer to an array of shared memory
                                     regions to map.
  @param[in]  num_shm_reg            Number of shared memory regions in the
                                     array.
  @param[in]  is_cached              Indicates if mem is cached or uncached
  @param[in]  is_offset_map          Indicates if the mapping is offset based
                                     as opposed to pointer based.
  @param[in]  pool_id                Memory pool ID to which this region is
                                     mapped.
  @param[out] shm_mem_map_handle_ptr Pointer to the memory map handle of the
                                     shared memory region created. This handle
                                     can be used later to unmap the shared
                                     memory.
  @param[in] heap_id                 heap id required for malloc.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the client object must be registered. @newpage
*/
ar_result_t posal_memorymap_virtaddr_mem_map(uint32_t                      client_token,
                                             posal_memorymap_shm_region_t *shm_mem_reg_ptr,
                                             uint16_t                      num_shm_reg,
                                             bool_t                        is_cached,
                                             bool_t                        is_offset_map,
                                             POSAL_MEMORYPOOLTYPE          pool_id,
                                             uint32_t                     *shm_mem_map_handle_ptr,
                                             POSAL_HEAP_ID                 heap_id);

typedef struct posal_mem_map_v2_input_args_t
{
   uint32_t                      unique_shmem_id_24bit;
   /** Unique shared memory Id only LSB 24 Bits must be valid. if unique shm id is set, this will be returned as the
    * mem map handle in the output arguments. */

   uint32_t                      client_token;
   /** Posal memorymap driver's registered client token */

   posal_memorymap_shm_region_t *shm_mem_reg_ptr;
   /** Pointer to an array of shared memory regions to map. */

   uint16_t                      num_shm_reg;
   /** Number of shared memory regions in the array. */

   bool_t                        is_cached;
   /** Indicates if mem is cached or uncached */

   bool_t                        is_offset_map;
   /** Indicates if the mapping is offset based as opposed to pointer based.*/

   POSAL_MEMORYPOOLTYPE          pool_id;
   /** Memory pool ID to which this region is mapped.*/

   POSAL_HEAP_ID                 heap_id;
   /**  heap id required for any allocations required to perform the memory map command. */

} posal_mem_map_v2_input_args_t;

/**
  Maps the shared memory and adds the memory region to the client linked list.
  This function enables physical address mapping only.

  @datatypes
  posal_memorymap_shm_region_t \n
  #POSAL_MEMORYPOOLTYPE

  @param[in]  posal_mem_map_v2_input_args_t    structure containing input arguments
  @param[out] ret_mem_map_handle_ptr           Handle returned by the driver. Usually driver returns "unique_shmem_id_24bit"
                                               if client is passing unique shm id in the posal_mem_map_v2_input_args_t.
                                               If the client doesnt set any "unique_shmem_id_24bit" in input args, then driver
                                               creates its own unique shm handle and returns it.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the client object must be registered. @newpage
*/

ar_result_t posal_memorymap_shm_mem_map_v2(posal_mem_map_v2_input_args_t *input_args_ptr,
                                           uint32_t                      *ret_mem_map_handle_ptr);

/**
Maps the shared virtual address and adds the memory region to the client
linked list. This function must be used to map a virtual address.

@datatypes
posal_memorymap_shm_region_t \n
#POSAL_MEMORYPOOLTYPE

  @param[in]  posal_mem_map_v2_input_args_t    structure containing input arguments
  @param[out] ret_mem_map_handle_ptr           Handle returned by the driver. Usually driver returns "shmem_id"
                                               if client is passing unique shm id in the posal_mem_map_v2_input_args_t.
                                               If the client doesnt set any "shmem_id" in input args, then driver
                                               creates its own unique shm handle and returns it.

@return
0 -- Success
@par
Nonzero -- Failure

@dependencies
Before calling this function, the client object must be registered. @newpage
*/
ar_result_t posal_memorymap_virtaddr_mem_map_v2(posal_mem_map_v2_input_args_t *input_args_ptr,
                                                uint32_t                      *ret_mem_map_handle_ptr);

/**
 Gets the associated mem_map_handle for a given client based on the unique_shmem_id_24bit.

 @param[in]  client_token           Client token.
 @param[in]  unique_shmem_id_24bit               Shared memory id set by the client.
 @param[out] shm_mem_map_handle_ptr  Pointer to the memory map handle of the
                                     shared memory region created.

 @return
 0 -- Success
 @par
 Nonzero -- Failure

 @dependencies
 Before calling this function, the client object must be registered and
 valid shared memory regions should be created with unique_shmem_id_24bit.
 */
ar_result_t posal_memorymap_get_mem_map_handle(uint32_t  client_token,
                                               uint32_t  unique_shmem_id_24bit,
                                               uint32_t *shm_mem_map_handle_ptr);

/**
  Gets the memory mapping mode for a specified memory map handle.

  @datatypes
  #posal_memorymap_mapping_mode_t

  @param[in]  client_token        Client token.
  @param[in]  shm_mem_map_handle  Memory map handle of the shared memory region
                                  created when calling one of the following:
                                  - posal_memorymap_shm_mem_map()
                                  - posal_memorymap_virtaddr_mem_map()
                                  @tablebulletend
  @param[out] mapping_mode_ptr    Pointer to the memory mapping mode.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the client object must be registered. @newpage
*/
ar_result_t posal_memorymap_get_mapping_mode(uint32_t                        client_token,
                                             uint32_t                        shm_mem_map_handle,
                                             posal_memorymap_mapping_mode_t *mapping_mode_ptr);

/**
  Unmaps the memory region and deletes the entry from the client-given memory
  map handle.

  @param[in] client_token        Client token.
  @param[in] shm_mem_map_handle  Memory map handle of the shared memory region
                                 created when calling
                                 posal_memorymap_shm_mem_map().

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
    Before calling this function, the client object must be registered, and the
    corresponding memory must be mapped.
*/
ar_result_t posal_memorymap_shm_mem_unmap(uint32_t client_token, uint32_t shm_mem_map_handle);

/**
  Gets the corresponding virtual address for a specified shared memory address
  and memory map handle. If "is_ref_count" is set to TRUE then increments the
  reference count each time a virtual address is requested from the memory map handle.
  Client must decrement reference count using posal_memorymap_shm_decr_refcount()
  to release reference to the virtual addr.

  @param[in]  client_token        Client token.
  @param[in]  shm_mem_map_handle  Memory map handle of the shared memory region
                                  created when calling
                                  posal_memorymap_shm_mem_map().
  @param[in]  shm_addr_lsw        LSW of the mapped region for any shared
                                  memory address.
  @param[in]  shm_addr_msw        MSW of the mapped region for any shared
                                  memory address.
  @param[in]  is_ref_counted      Flag to indicate if ref count needs to be
                                  incremented. If set to TRUE, need to decrement
                                  the ref count at the time of release.
  @param[out] virt_addr_ptr       Pointer to the equivalent virtual address
                                  returned.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  If is_ref_counted= TRUE, must decrement the reference count to free the virtual address.
  posal_memorymap_shm_decr_refcount().
  If is_ref_counted= FALSE, should not decrememnt ref count later.
  @newpage
*/
ar_result_t posal_memorymap_get_virtual_addr_from_shm_handle(uint32_t  client_token,
                                                             uint32_t  shm_mem_map_handle,
                                                             uint32_t  shm_addr_lsw,
                                                             uint32_t  shm_addr_msw,
                                                             uint32_t  reg_size,
                                                             bool_t    is_ref_counted,
                                                             uint32_t *virt_addr_ptr);
/**
  Gets the corresponding virtual address for a specified shared memory address
  and memory map handle. If "is_ref_count" is set to TRUE then increments the
  reference count each time a virtual address is requested from the memory map handle.
  Client must decrement reference count using posal_memorymap_shm_decr_refcount()
  to release reference to the virtual addr.

  @param[in]  client_token        Client token.
  @param[in]  shm_mem_map_handle  Memory map handle of the shared memory region
                                  created when calling
                                  posal_memorymap_shm_mem_map().
  @param[in]  shm_addr_lsw        LSW of the mapped region for any shared
                                  memory address.
  @param[in]  shm_addr_msw        MSW of the mapped region for any shared
                                  memory address.
  @param[in]  is_ref_counted      Flag to indicate if ref count needs to be
                                  incremented. If set to TRUE, need to decrement
                                  the ref count at the time of release.
  @param[out] virt_addr_ptr       Pointer to the equivalent virtual address
                                  returned.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  If is_ref_counted= TRUE, must decrement the reference count to free the virtual address.
  posal_memorymap_shm_decr_refcount().
  If is_ref_counted= FALSE, should not decrememnt ref count later.
  @newpage
*/
ar_result_t posal_memorymap_get_virtual_addr_from_shm_handle_v2(uint32_t client_token,
                                                                uint32_t shm_mem_map_handle,
                                                                uint32_t shm_addr_lsw,
                                                                uint32_t shm_addr_msw,
                                                                uint32_t reg_size,
                                                                bool_t   is_ref_counted,
                                                                void    *virt_addr_ptr);
/**
  Gets the corresponding mem_map_handle for a specified VA address. It also
  returns the offset from the base virt_addr of the node.

  @param[in]  client_token        Client token.
  @param[in]  va                  Queried VA.
  @param[out] mem_handle_ptr      Pointer to the needed mem_handle
                                  of which the VA is a part of.
  @param[out] offset_ptr          Pointer to the needed offset of the VA
                                  from the base va.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @newpage
*/
ar_result_t posal_memorymap_get_shmm_handle_and_offset_from_va_offset_map(uint32_t  client_token,
                                                                          uint32_t  va,
                                                                          uint32_t *mem_handle_ptr,
                                                                          uint32_t *offset_ptr);

/**
  Gets the region attributes for a specified shared memory address and memory
  map handle.

  @datatypes
  posal_memorymap_mem_region_attrib_t

  @param[in]  client_token        Client token.
  @param[in]  shm_mem_map_handle  Memory map handle of the shared memory region
                                  created when calling
                                  posal_memorymap_shm_mem_map().
  @param[in]  shm_addr_lsw        LSW of the mapped region for any shared
                                  memory address.
  @param[in]  shm_addr_msw        MSW of the mapped region for any shared
                                  memory address.
  @param[out] mem_reg_attrib_ptr  Pointer to the memory region attribute whose
                                  fields are filled by this function.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the client object must be registered, and the
  corresponding memory must be mapped.
*/
ar_result_t posal_memorymap_get_mem_region_attrib_from_shmm_handle(
   uint32_t                             client_token,
   uint32_t                             shm_mem_map_handle,
   uint32_t                             shm_addr_lsw,
   uint32_t                             shm_addr_msw,
   bool_t                               is_ref_counted,
   posal_memorymap_mem_region_attrib_t *mem_reg_attrib_ptr);

/**
 *This function deletes a memory region using QURT APIs. This
 *function is not thread safe. The caller must provide thread
 *safety.
 */
ar_result_t posal_memorymap_util_region_delete(posal_memorymap_node_t *mem_map_node_ptr, bool_t enable_debug_logs);

/**
  Unmaps all memory regions and deletes all nodes of the requested client.

  @param[in] client_token  Client token.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the client object must be registered. @newpage
*/
ar_result_t posal_memorymap_unmap_all(uint32_t client_token);

/**
  Unmaps all memory regions for all clients, and deletes all of their nodes.


  @dependencies
  Before calling this function, the client's objects must be registered. @newpage
*/
void posal_memorymap_global_unmap_all();

/**
  Given a virtual address, this function does the following:
  - Retrieves the corresponding mapping entry from the page table
  - Adjusts the offset based on the page size
  - Returns the full 64-bit physical address back to the user

  @param[in] virt_addr    Starting virtual address.

  @return
  Physical address if the address is mapped; otherwise, 0.

  @dependencies
  Before calling this function, the client object must be registered, and the
  corresponding memory must be mapped.
*/
uint64_t posal_memorymap_get_physical_addr(uint32_t virt_addr);

/**
  Given a virtual address, this function does the following:
  - Retrieves the corresponding mapping entry from the page table
  - Adjusts the offset based on the page size
  - Returns the full 64-bit physical address back to the user

  @param[in] virt_addr    Starting virtual address.

  @return
  Physical address if the address is mapped; otherwise, 0.

  @dependencies
  Before calling this function, the client object must be registered, and the
  corresponding memory must be mapped.
*/
uint64_t posal_memorymap_get_physical_addr_v2(posal_mem_addr_t virt_addr);

/**
  Increments the reference count of the memory map handle of a client.

  @param[in] client_token        Client token.
  @param[in] shm_mem_map_handle  Memory map handle of the shared memory region
                                 created when calling
                                 posal_memorymap_shm_mem_map().

  @detdesc
  Incrementing this reference count suggests that the memory region abstracted
  by this memory map handle is in use.
  @par
  A nonzero reference count prevents the aDSP client from unmapping specific
  memory map regions.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the client object must be registered, and the
  corresponding memory must be mapped. @newpage
*/
ar_result_t posal_memorymap_shm_incr_refcount(uint32_t client_token, uint32_t shm_mem_map_handle);

/**
  Decrements the reference count of the memory map handle of a client.

  @param[in] client_token        Client token.
  @param[in] shm_mem_map_handle  Memory map handle of the shared memory region
                                 created when calling
                                 posal_memorymap_shm_mem_map().

  @detdesc
  Decrementing this reference count suggests that this client is relinquishing
  the memory region abstracted by this memory map handle (the client no longer
  requires this region).
  @par
  The reference count must reach zero for the memory region to be unmapped.

  @return
  0 -- Success
  @par
  Nonzero -- Failure

  @dependencies
  Before calling this function, the client object must be registered, and the
  corresponding memory must be mapped.
*/
ar_result_t posal_memorymap_shm_decr_refcount(uint32_t client_token, uint32_t shm_mem_map_handle);

/** @} */ /* end_addtogroup posal_memorymap */

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef POSAL_MEMORYMAP_H
