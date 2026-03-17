/**
 * \file apm_memmap_api.h
 * \brief
 *    This file contains the structure definitions for the memory map commands
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _APM_MEMMAP_API_H_
#define _APM_MEMMAP_API_H_

#include "ar_defs.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/** @addtogroup spf_apm_memory_map
@{ */

/** Commands the APM to map multiple shared memory regions.
    All the mapped regions must be from the same memory pool.

    @gpr_hdr_fields
    Opcode -- APM_CMD_SHARED_MEM_MAP_REGIONS

    @msgpayload{apm_cmd_shared_mem_map_regions_t}
    @tablens{weak__apm__cmd__shared__mem__map__regions__t}

    @par Memory region configuration (apm_shared_map_region_payload_t)
    @table{weak__apm__shared__map__region__payload__t}

    @return
    #APM_CMD_RSP_SHARED_MEM_MAP_REGIONS.

    @dependencies
    None. @newpage
*/
#define APM_CMD_SHARED_MEM_MAP_REGIONS 0x0100100C

/** Commands the APM to map multiple shared memory regions from
 a memory pool and associate it with the specified shmem_id.
 SPF-client is expected to map these memory regions with other SPF instances
 if needed.

 @gpr_hdr_fields
 Opcode -- APM_CMD_GLOBAL_SHARED_MEM_MAP_REGIONS

 @msgpayload{apm_cmd_global_shared_mem_map_regions_t}
 @tablens{weak__apm__cmd__global_shared__mem__map__regions__t}

 @par Memory region configuration (apm_shared_map_region_payload_t)
 @table{weak__apm__shared__map__region__payload__t}

 @return
 GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

 @dependencies
 None. @newpage
 */
#define APM_CMD_GLOBAL_SHARED_MEM_MAP_REGIONS 0x01001059

/** Identifier for the shared memory 8_4K pool.
    @inputfile{group__weakb__property__flag__values.tex}
*/
#define APM_MEMORY_MAP_SHMEM8_4K_POOL 3

/** Commands the APM to map multiple MDF shared memory regions.
    All the mapped regions must be from the same memory pool.

    @gpr_hdr_fields
    Opcode -- APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS

    @msgpayload{apm_cmd_shared_satellite_mem_map_regions_t}
    @tablens{weak__apm__cmd__shared__satellite__mem__map__regions__t}

    @par Memory region configuration (apm_shared_map_region_payload_t)
    @table{weak__apm__shared__map__region__payload__t}

    @return
    #APM_CMD_RSP_SHARED_SATELLITE_MEM_MAP_REGIONS.

    @dependencies
    None. @newpage
*/
#define APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS 0x01001026

/** @weakgroup weakb_property_flag_values
@{ */

/** Definition of the IsVirtual flag shift value. */
#define APM_MEMORY_MAP_SHIFT_IS_VIRTUAL 0

/** Definition of the IsVirtual flag bitmask. */
#define APM_MEMORY_MAP_BIT_MASK_IS_VIRTUAL (0x00000001UL)

/** Definition of the physical memory flag. */
#define APM_MEMORY_MAP_PHYSICAL_MEMORY 0

/** Definition of the virtual memory flag. */
#define APM_MEMORY_MAP_VIRTUAL_MEMORY 1

/** Definition of the is_uncached flag shift value. */
#define APM_MEMORY_MAP_SHIFT_IS_UNCACHED 1

/** Definition of the is_uncached flag bitmask. */
#define APM_MEMORY_MAP_BIT_MASK_IS_UNCACHED (0x00000002UL)

/** Definition of the is_offset_mode flag shift value. */
#define APM_MEMORY_MAP_SHIFT_IS_OFFSET_MODE 2

/** Definition of the is_offset_mode flag bitmask. */
#define APM_MEMORY_MAP_BIT_MASK_IS_OFFSET_MODE (0x00000004UL)

/** Definition of the is_client_loaned_memory flag shift value. */
#define APM_MEMORY_MAP_SHIFT_IS_MEM_LOANED 3

/** Definition of the is_client_loaned_memory flag bitmask. */
#define APM_MEMORY_MAP_BIT_MASK_IS_MEM_LOANED (0x00000008UL)

/** Definition of the loaned_memory_heap_manager_type flag shift value. */
#define APM_MEMORY_MAP_SHIFT_LOANED_MEMORY_HEAP_MNGR_TYPE 4

/** Definition of the loaned_memory_heap_manager_type flag bitmask. */
#define APM_MEMORY_MAP_BIT_MASK_LOANED_MEMORY_HEAP_MNGR_TYPE (0x00000030UL)

/** Definition of the Loaned memory Default Heap Manager. */
#define APM_MEMORY_MAP_LOANED_MEMORY_HEAP_MNGR_TYPE_DEFAULT 0

/** Definition of the Loaned memory Safe Heap Manager. */
#define APM_MEMORY_MAP_LOANED_MEMORY_HEAP_MNGR_TYPE_SAFE_HEAP 1

/** Definition of the Loaned memory Heap Manager type value reserved. */
#define APM_MEMORY_MAP_LOANED_MEMORY_HEAP_MNGR_TYPE_RESERVED1  2
#define APM_MEMORY_MAP_LOANED_MEMORY_HEAP_MNGR_TYPE_RESERVED2  3

/** Definition of the memory address type flag shift value. */
#define APM_MEMORY_MAP_SHIFT_MEMORY_ADDRESS_TYPE 6

/** Definition of the memory address type flag bitmask. */
#define APM_MEMORY_MAP_BIT_MASK_MEMORY_ADDRESS_TYPE (0x000001C0UL)

/** Definition of the memory address type default
 *  physical/virtual address (PA/VA ) represented by BIT_0 */
#define APM_MEMORY_MAP_MEMORY_ADDRESS_TYPE_PA_OR_VA 0

/** Definition of the memory address type  - file descriptor(FD) . */
#define APM_MEMORY_MAP_MEMORY_ADDRESS_TYPE_FD 1

/** Definition of the memory address type - max value. */
#define APM_MEMORY_MAP_MEMORY_ADDRESS_TYPE_MAX 7

/** @} */ /* end_weakgroup weakb_property_flag_values */

/** @weakgroup weak_apm_cmd_global_shared_mem_map_regions_t
 @{ */
/** Immediately following this structure are num_regions of
 #apm_shared_map_region_payload_t.
 */
#include "spf_begin_pack.h"
struct apm_cmd_global_shared_mem_map_regions_t
{
   uint32_t shmem_id;
   /**< A unique ID assigned by the client for these memory regions.
    All subsequent commands #AR_SPF_MSG_GLOBAL_SH_MEM will use
    this unique ID to refer these shared memory regions.
    */

   uint16_t mem_pool_id;
   /**< Type of memory on which this memory region is mapped.

    @valuesbul
    - #APM_MEMORY_MAP_SHMEM8_4K_POOL
    - Other values are reserved

    The memory ID implicitly defines the characteristics of the memory.
    Characteristics can include alignment type, permissions, and so on.

    APM_MEMORY_MAP_SHMEM8_4K_POOL is shared memory, byte addressable, and
    4 KB aligned. */

   uint16_t num_regions;
   /**< Number of physical regions to map.
    * Immediately following this structure are num_regions of
      #apm_shared_map_region_payload_t.

    @values > 0

    If the is_offset_mode bit in the property_flag is set, num_regions can
    only be set to 1.*/

   uint32_t property_flag;
   /**< Configures one common property for all regions in the payload. No
    two regions in the same command can have different properties.

    @values 0x00000000 through 0x00000003

    @contcell
    @subhead{Bit 0 -- IsVirtual flag}

    Indicates physical or virtual mapping:
    - 0 -- The shared memory address provided in
    #apm_shared_map_region_payload_t is a physical address. The
    shared memory must be mapped (a hardware TLB entry), and a
    software entry must be added for internal bookkeeping.
    - 1 -- The shared memory address provided in the map
    payload [usRegions] is a virtual address. The shared memory
    must not be mapped (because the hardware TLB entry is already
    available). But a software entry must be added for internal
    bookkeeping. \n @vertspace{2}
    This bit value (1) is useful if two services within the aDSP
    are communicating via the GPR. They can directly communicate
    via the virtual address instead of the physical address. The
    virtual regions must be contiguous.

    @subhead{Bit 1 -- is\_uncached flag}

    Indicates whether the memory is mapped as cached memory or uncached:
    - 0 -- Cached
    - 1 -- Uncached

    Most memories must be mapped as cached. Some special cases require
    uncached memory.

    @subhead{Bit 2 -- is\_offset\_mode flag}

    Indicates whether the memory will be subsequently referred to with
    offsets:
    - 0 -- Subsequent commands using this mapped regions
    will use physical or virtual addresses.\n
    @vertspace{2}
    For example, buf_addr_lsw and buf_addr_msw in
    apm_cmd_loan_global_shared_mem_t or ar_spf_msg_global_sh_mem_t
    will be physical or virtual addresses.
    - 1 -- Subsequent commands using this mapped regions
    will use offsets.\n
    @vertspace{2}
    For example, buf_addr_lsw and buf_addr_msw in
    apm_cmd_loan_global_shared_mem_t or ar_spf_msg_global_sh_mem_t
    will be offsets. The offset is from the base address, which is
    defined as the address given by apm_shared_map_region_payload_t.

    @subhead{Bits 31 to 3}

    Reserved and must be set to 0. */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_global_shared_mem_map_regions_t */
typedef struct apm_cmd_global_shared_mem_map_regions_t apm_cmd_global_shared_mem_map_regions_t;

/** @weakgroup weak_apm_cmd_shared_mem_map_regions_t
@{ */
/** Immediately following this structure are num_regions of
    #apm_shared_map_region_payload_t.
*/
#include "spf_begin_pack.h"
struct apm_cmd_shared_mem_map_regions_t
{
   uint16_t mem_pool_id;
   /**< Type of memory on which this memory region is mapped.

        @valuesbul
        - #APM_MEMORY_MAP_SHMEM8_4K_POOL
        - Other values are reserved

        The memory ID implicitly defines the characteristics of the memory.
        Characteristics can include alignment type, permissions, and so on.

        APM_MEMORY_MAP_SHMEM8_4K_POOL is shared memory, byte addressable, and
        4 KB aligned. */

   uint16_t num_regions;
   /**< Number of regions to map.

        @values > 0

        If the is_offset_mode bit in the property_flag is set, num_regions can
        only be set to 1.*/

   uint32_t property_flag;
   /**< Configures one common property for all regions in the payload. No
        two regions in the same command can have different properties.

        @values 0x00000000 through 0x00000007

        @contcell
        @subhead{Bit 0 -- IsVirtual flag}

        Indicates physical or virtual mapping:
          - 0 -- The shared memory address provided in
                 #apm_shared_map_region_payload_t is a physical address. The
                 shared memory must be mapped (a hardware TLB entry), and a
                 software entry must be added for internal bookkeeping.
          - 1 -- The shared memory address provided in the map
                 payload [usRegions] is a virtual address. The shared memory
                 must not be mapped (because the hardware TLB entry is already
                 available). But a software entry must be added for internal
                 bookkeeping. \n @vertspace{2}
                 This bit value (1) is useful if two services within the aDSP
                 are communicating via the APR. They can directly communicate
                 via the virtual address instead of the physical address. The
                 virtual regions must be contiguous.

        @subhead{Bit 1 -- is\_uncached flag}

        Indicates whether the memory is mapped as cached memory or uncached:
          - 0 -- Cached
          - 1 -- Uncached

        Most memories must be mapped as cached. Some special cases require
        uncached memory.

        @subhead{Bit 2 -- is\_offset\_mode flag}

        Indicates whether the memory will be subsequently referred to with
        offsets:
          - 0 -- Subsequent commands using mem_map_handle returned by
                 #APM_CMD_RSP_SHARED_MEM_MAP_REGIONS will use physical or
                 virtual addresses. \n @vertspace{2}
                 For example, buf_addr_lsw and buf_addr_msw in
                 data_cmd_wr_sh_mem_ep_data_buffer_t will be physical or
                 virtual addresses.
          - 1 -- Subsequent commands using mem_map_handle returned by
                 APM_CMD_RSP_SHARED_MEM_MAP_REGIONS will use offsets. \n
                 @vertspace{2}
                 For example, buf_addr_lsw and buf_addr_msw in
                 data_cmd_wr_sh_mem_ep_data_buffer_t will be offsets. The
                 offset is from the base address, which is defined as the
                 address given by apm_shared_map_region_payload_t.

        After mapping a physical region using this command, a mem_map_handle is
        returned. Subsequently, the client uses mem_map_handle in any command
        using addresses or offsets that belong to a memory region mapped in
        this command.

        @contcell
        @subhead{Bit 3 -- is\_client\_loaned\_memory flag}

        Specifies whether the client manages the memory or allows the APM to
        manage the memory:
          - 0 -- Client owned memory (the shared memory is managed by the
                 client). @vertspace{3}
          - 1 -- Client loaned memory (the shared memory is mapped by the
                 client and managed by the APM).

        For loaned memory, the client must relinquish the memory access
        permissions for multi-DSP framework (MDF) use cases.

        @contcell
        @subhead{Bit 4-5 -- loaned\_memory\_heap\_configuration}

        Specifies the heap configuration for the memory loaned by client and
        managed by APM ( .i.e., this configuration is valid only when Bit_3 is SET)
          - 0 -- Default Heap ( Regular heap, functionally backward compatible )
          - 1 -- Safe Heap    ( When the satellite is considered as un-secure
                                Process Domain, configuring as Safe Heap is
                                recommended )
          - 2/3 -- Values reserved for future use

         -It is recommended to use the same heap configuration for all the MDF memory maps
         for a pair for the master-satellite combination.
         -If multiple Satellite Process Domains (PD's) are present in a product
         with same loaned shared memory mapped to all the satellite PD's,
         and one of the satellite PD is termed as un-secure PD,
         configuring as Safe Heap is recommended.
         -In general, configuration as Default Heap is recommended.

        @contcell
        @subhead{Bit 6-8 -- memory\_address\_type}

         Specifies the memory address type. In the Default configuration (0), would ensure backward compatibility.
           -  0     � Physical/Virtual Address
           -  1     � File descriptor
           -  2-7   � Values reserved for Future

		 It is important to note, the Signal Processing Framework (a.k.a AudioReach SPF)  would not have any
		 mechanism to convert the memory address type or validate the same.
         This configuration would enable the HLOS system, specifically GSL in userspace and the audio packet
         driver with kernel space access, to determine if FD (file descriptor) to PA (physical address)
         conversion is required in the audio packet driver. This determination is based on the actual needs
         of the underlying process domain, which requires memory addresses accordingly.


        @subhead{Bits 31 to 9}

        Reserved and must be set to 0. */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_shared_mem_map_regions_t */
typedef struct apm_cmd_shared_mem_map_regions_t apm_cmd_shared_mem_map_regions_t;

/** @weakgroup weak_apm_cmd_shared_satellite_mem_map_regions_t
@{ */
/** Immediately following this structure are num_regions of
    %apm_shared_map_region_payload_t.
*/
#include "spf_begin_pack.h"
struct apm_cmd_shared_satellite_mem_map_regions_t
{
   uint32_t master_mem_handle;
   /**< Specifies the memory handle of the master processor that corresponds to
        the shared memory region being mapped to the satellite process domain.

        This handle must have been sent in the response after mapping the
        memory to the master processor. */

   uint32_t satellite_proc_domain_id;
   /**< Specifies the satellite process domain ID to which the memory is
        mapped.

        @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_APPS
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROC_DOMAIN_ID_GDSP_0
        - #APM_PROC_DOMAIN_ID_GDSP_1
        - #APM_PROC_DOMAIN_ID_APPS_2
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2   @tablebulletend */

   uint16_t mem_pool_id;
   /**< Type of memory on which this memory region is mapped.

        @valuesbul
        - #APM_MEMORY_MAP_SHMEM8_4K_POOL
        - Other values are reserved

        The memory ID implicitly defines the characteristics of the memory.
        Characteristics can include alignment type, permissions, etc.

        APM_MEMORY_MAP_SHMEM8_4K_POOL is shared memory, byte addressable, and
        4 KB aligned. */

   uint16_t num_regions;
   /**< Number of regions to map.

        @values 1

        @note1 If the is_offset_mode bit in the property_flag is set,
               num_regions can only be 1.*/

   uint32_t property_flag;
   /**< Configures one common property for all regions in the payload. No
        two regions in the same command can have different properties.

        @values 0x00000000 through 0x00000007

        @contcell
        @subhead{Bit 0 -- IsVirtual flag}
        Indicates physical or virtual mapping:
          - 0 -- The shared memory address provided in
                 %apm_shared_map_region_payload_t is a physical address. The
                 shared memory must be mapped (a hardware TLB entry), and a
                 software entry must be added for internal bookkeeping.
          - 1 -- The shared memory address provided in the map
                 payload [usRegions] is a virtual address. The shared memory
                 must not be mapped (because the hardware TLB entry is already
                 available). But a software entry must be added for internal
                 bookkeeping. \n @vertspace{2}
                 This bit value is useful if two services within the aDSP
                 are communicating via the APR. They can directly communicate
                 via the virtual address instead of the physical address. The
                 virtual regions must be contiguous.

        @subhead{Bit 1 -- is\_uncached flag}
        Indicates whether the memory is mapped as cached memory or uncached:
          - 0 -- Cached
          - 1 -- Uncached

        Most memories must be mapped as cached. Some special cases require
        uncached memory.

        @subhead{Bit 2 -- is\_offset\_mode flag}
        Indicates whether the memory is subsequently referred to with offsets:
          - 0 -- Subsequent commands using mem_map_handle returned by
                 #APM_CMD_RSP_SHARED_MEM_MAP_REGIONS will use physical or
                 virtual addresses. \n @vertspace{2}
                 For example, buf_addr_lsw and buf_addr_msw in
                 data_cmd_wr_sh_mem_ep_data_buffer_t are physical or virtual
                 addresses.
          - 1 -- Subsequent commands using mem_map_handle returned by
                 APM_CMD_RSP_SHARED_MEM_MAP_REGIONS will use offsets. \n
                 @vertspace{2}
                 For example, buf_addr_lsw and buf_addr_msw in
                 data_cmd_wr_sh_mem_ep_data_buffer_t are offsets. The offset
                 is from the base address, which is defined as the address
                 given by %apm_shared_map_region_payload_t.

        After mapping a physical region using this command, a mem_map_handle is
        returned. Subsequently, the client uses mem_map_handle in any command
        using addresses or offsets that belong to a memory region mapped in
        this command.

        @contcell
        @subhead{Bit 3 -- is\_client\_loaned\_memory flag}
        Specifies whether the client manages the memory or allows the APM to
        manage the memory:
          - 0 -- Client owned memory (the shared memory is managed by the
                 client)
          - 1 -- Client loaned memory (the shared memory is mapped by the
                 client and managed by the APM)

        For loaned memory, the client must relinquish the memory access
        permissions for MDF use cases.

        @contcell
        @subhead{Bit 4-5 -- loaned\_memory\_heap\_configuration}

        Specifies the heap configuration for the memory loaned by client and
        managed by APM ( .i.e., this configuration is valid only when Bit_3 is SET)
          - 0 -- Default Heap ( Regular heap, functionally backward compatible )
          - 1 -- Safe Heap    ( When the satellite is considered as un-secure
                                Process Domain, configuring as Safe Heap is
                                recommended )
          - 2/3 -- Values reserved for future use

         -It is recommended to use the same heap configuration for all the MDF memory maps
         for a pair for the master-satellite combination.
         -If multiple Satellite Process Domains (PD's) are present in a product
         with same loaned shared memory mapped to all the satellite PD's,
         and one of the satellite PD is termed as un-secure PD,
         configuring as Safe Heap is recommended.
         -In general, configuration as Default Heap is recommended.

        @contcell
        @subhead{Bit 6-8 -- memory\_address\_type}

         Specifies the memory address type. In the Default configuration (0), would ensure backward compatibility.
           -  0     � Physical/virtual Address
           -  1     � File descriptor
           -  2-7   � Values reserved for Future

		 It is important to note, the Signal Processing Framework (a.k.a AudioReach SPF)  would not have any
		 mechanism to convert the memory address type or validate the same.
         This configuration would enable the HLOS system, specifically GSL in userspace and the audio packet
         driver with kernel space access, to determine if FD (file descriptor) to PA (physical address)
         conversion is required in the audio packet driver. This determination is based on the actual needs
         of the underlying process domain, which requires memory addresses accordingly.


        @subhead{Bits 31 to 9}
        Reserved and must be set to 0. */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_shared_satellite_mem_map_regions_t */
typedef struct apm_cmd_shared_satellite_mem_map_regions_t apm_cmd_shared_satellite_mem_map_regions_t;

/** @weakgroup weak_apm_shared_map_region_payload_t
@{ */
/* Payload of the map region: used by the
    apm_cmd_shared_mem_map_regions_t structure.
*/
#include "spf_begin_pack.h"
struct apm_shared_map_region_payload_t
{
   uint32_t shm_addr_lsw;
   /**< Lower 32 bits of the shared memory address of the memory region to
        map. This word must be contiguous memory, and it must be
        4&nbsp;KB aligned.

        @values Non-zero 32-bit value */

   uint32_t shm_addr_msw;
   /**< Upper 32 bits of the shared memory address of the memory region to
        map. This word must be contiguous memory, and it must be
        4&nbsp;KB aligned.

        @valuesbul
        - For a 32-bit shared memory address, this field must be set to 0.
        - For a 36-bit address, bits 31 to 4 must be set to 0.
        - For a 64-bit shared memory address, non-zero 32-bit value.
        @tablebulletend */

   uint32_t mem_size_bytes;
   /**< Number of bytes in the region.

        @values Multiples of 4 KB

        The APM always maps the regions as virtual contiguous memory, but
        the memory size must be in multiples of 4 KB to avoid gaps in the
        virtually contiguous mapped memory. */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_shared_map_region_payload_t */
typedef struct apm_shared_map_region_payload_t apm_shared_map_region_payload_t;

/** Returns the memory map handle in response to an
    #APM_CMD_SHARED_MEM_MAP_REGIONS command.

    @gpr_hdr_fields
    Opcode -- APM_CMD_RSP_SHARED_MEM_MAP_REGIONS

    @msgpayload{apm_cmd_rsp_shared_mem_map_regions_t}
    @table{weak__apm__cmd__rsp__shared__mem__map__regions__t}

    @return
    None.

    @dependencies
    An #APM_CMD_SHARED_MEM_MAP_REGIONS command must have been issued. @newpage
*/
#define APM_CMD_RSP_SHARED_MEM_MAP_REGIONS 0x02001001

/** @weakgroup weak_apm_cmd_rsp_shared_mem_map_regions_t
@{ */
#include "spf_begin_pack.h"
struct apm_cmd_rsp_shared_mem_map_regions_t
{
   uint32_t mem_map_handle;
   /**< Unique identifier for the shared memory address.
        The APM returns this handle through #APM_CMD_SHARED_MEM_MAP_REGIONS.

        @values Any 32-bit value

        The APM uses this handle to retrieve the shared memory attributes.
        This handle can be an abstract representation of the shared memory
        regions that are being mapped. */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_rsp_shared_mem_map_regions_t */
typedef struct apm_cmd_rsp_shared_mem_map_regions_t apm_cmd_rsp_shared_mem_map_regions_t;

/** Returns the memory map handle in response to an
    #APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS command.

    @gpr_hdr_fields
    Opcode -- APM_CMD_RSP_SHARED_SATELLITE_MEM_MAP_REGIONS

    @msgpayload{apm_cmd_rsp_shared_satellite_mem_map_regions_t}
    @table{weak__apm__cmd__rsp__shared__satellite__mem__map__regions__t}

    @return
    None.

    @dependencies
    An #APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS command must have been
    issued. @newpage
*/
#define APM_CMD_RSP_SHARED_SATELLITE_MEM_MAP_REGIONS 0x02001009

/** @weakgroup weak_apm_cmd_rsp_shared_satellite_mem_map_regions_t
@{ */
#include "spf_begin_pack.h"
struct apm_cmd_rsp_shared_satellite_mem_map_regions_t
{
   uint32_t mem_map_handle;
   /**< Unique identifier for the shared memory address.

        The satellite process domain uses this handle to retrieve the
        shared memory attributes. This handle can be an abstract
        representation of the shared memory regions that are being mapped. */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_rsp_shared_satellite_mem_map_regions_t */
typedef struct apm_cmd_rsp_shared_satellite_mem_map_regions_t apm_cmd_rsp_shared_satellite_mem_map_regions_t;

/** Commands the APM to unmap global shared memory regions that were
 previously mapped via #APM_CMD_GLOBAL_SHARED_MEM_MAP_REGIONS.

 Client is expected to send this unmap command only when the use case is ended.
 This is to ensure no module is actively using the global shared memory.

 @gpr_hdr_fields
 Opcode -- APM_CMD_GLOBAL_SHARED_MEM_UNMAP_REGIONS

 @msgpayload{apm_cmd_global_shared_mem_unmap_regions_t}
 @table{weak__apm__cmd__global__shared__mem__unmap__regions__t}

 @return
 GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

 @dependencies
 An #APM_CMD_GLOBAL_SHARED_MEM_MAP_REGIONS command must have been issued. @newpage
 */
#define APM_CMD_GLOBAL_SHARED_MEM_UNMAP_REGIONS 0x0100105A

/** @weakgroup weak_apm_cmd_global_shared_mem_unmap_regions_t
 @{ */
#include "spf_begin_pack.h"
struct apm_cmd_global_shared_mem_unmap_regions_t
{
   uint32_t shmem_id;
   /**< Unique identifier for the global shared memory.
    This is the same ID which was sent in #APM_CMD_GLOBAL_SHARED_MEM_MAP_REGIONS.

    @values Any 32-bit value

    The APM uses this handle to retrieve the associated shared
    memory regions and unmap them. */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_global_shared_mem_unmap_regions_t */
typedef struct apm_cmd_global_shared_mem_unmap_regions_t apm_cmd_global_shared_mem_unmap_regions_t;

/** Commands the APM to unmap multiple shared memory regions that were
    previously mapped via #APM_CMD_SHARED_MEM_MAP_REGIONS.

    @gpr_hdr_fields
    Opcode -- APM_CMD_SHARED_MEM_UNMAP_REGIONS

    @msgpayload{apm_cmd_shared_mem_unmap_regions_t}
    @table{weak__apm__cmd__shared__mem__unmap__regions__t}

    @return
    GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

    @dependencies
    An #APM_CMD_SHARED_MEM_MAP_REGIONS command must have been issued. @newpage
*/
#define APM_CMD_SHARED_MEM_UNMAP_REGIONS 0x0100100D

/** @weakgroup weak_apm_cmd_shared_mem_unmap_regions_t
@{ */
#include "spf_begin_pack.h"
struct apm_cmd_shared_mem_unmap_regions_t
{
   uint32_t mem_map_handle;
   /**< Unique identifier for the shared memory address.
        The APM returns this handle through #APM_CMD_SHARED_MEM_MAP_REGIONS.

        @values Any 32-bit value

        The APM uses this handle to retrieve the associated shared
        memory regions and unmap them. */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_shared_mem_unmap_regions_t */
typedef struct apm_cmd_shared_mem_unmap_regions_t apm_cmd_shared_mem_unmap_regions_t;

/** Commands the APM to unmap multiple shared memory regions
    that were previously mapped via #APM_CMD_SHARED_MEM_MAP_REGIONS.

    @gpr_hdr_fields
    Opcode -- APM_CMD_SHARED_SATELLITE_MEM_UNMAP_REGIONS

    @msgpayload{apm_cmd_shared_satellite_mem_unmap_regions_t}
    @table{weak__apm__cmd__shared__satellite__mem__unmap__regions__t}

    @return
    GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

    @dependencies
    An #APM_CMD_SHARED_MEM_MAP_REGIONS command must have been issued.
*/
#define APM_CMD_SHARED_SATELLITE_MEM_UNMAP_REGIONS 0x01001027

/** @weakgroup weak_apm_cmd_shared_satellite_mem_unmap_regions_t
@{ */
#include "spf_begin_pack.h"
struct apm_cmd_shared_satellite_mem_unmap_regions_t
{
   uint32_t satellite_proc_domain_id;
   /**< Specifies the satellite process domain ID whose memory must be
        unmapped.

        @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_APPS
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROC_DOMAIN_ID_GDSP_0
        - #APM_PROC_DOMAIN_ID_GDSP_1
        - #APM_PROC_DOMAIN_ID_APPS_2
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2   @tablebulletend */

   uint32_t master_mem_handle;
   /**< Unique identifier for the shared memory address.

        @values Any 32-bit value

        The satellite process domain uses this handle to retrieve the
        associated shared memory regions and unmap them. The APM returns this
        handle through #APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS. */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_shared_satellite_mem_unmap_regions_t */
typedef struct apm_cmd_shared_satellite_mem_unmap_regions_t apm_cmd_shared_satellite_mem_unmap_regions_t;

#if 0
/** This Command specifies memory block loaned to the SPF instance from the specified global
 shared memory which is previously mapped with command #APM_CMD_GLOBAL_SHARED_MEM_MAP_REGIONS.
 This memory can be used by the SPF modules to communicate within the specified global
 shared memory use case.

 @gpr_hdr_fields
 Opcode -- APM_CMD_LOAN_GLOBAL_SHARED_MEM

 @msgpayload{apm_cmd_loan_global_shared_mem_t}
 @tablens{weak__apm__cmd__loan__global_shared__mem__t}

 @return
 GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

 @dependencies
 None. @newpage
 */
#define APM_CMD_LOAN_GLOBAL_SHARED_MEM 0x0100105C

/** @weakgroup weak_apm_cmd_loan_global_shared_mem_t
@{ */
#include "spf_begin_pack.h"
struct apm_cmd_loan_global_shared_mem_t
{
   uint32_t shmem_id;
   /**< Unique identifier for the global shared memory.
    This is the same ID which was sent in #APM_CMD_GLOBAL_SHARED_MEM_MAP_REGIONS.

    @values Any 32-bit value
    */

   uint32_t loaned_mem_addr_lsw;
   /**< Lower 32 bits of the physical/Virtual/byte-offset address value.

    This refers to the loaned memory for SPF from the specified global shared memory.

    If global shared memory was mapped with offset property (Bit 2 is set)
    then this value is a byte offset from the base virtual address of
    global shared memory.

    If global shared memory was not mapped with offset property (Bit 2 is unset)
    then this value either refers to a physical or a virtual address depending on the
    Bit 0 configuration in the global shared memory map property.

    This memory can be used by the SPF for sending any out of band
    command/event from SPF back to the client or another SPF instance.

    @values Non-zero 32-bit value */

   uint32_t loaned_mem_addr_msw;
   /**< Upper 32 bits of the physical/Virtual address value.

    @valuesbul
    - For offset based addressing, this field must be set to 0.
    - For a 32-bit shared memory address, this field must be set to 0.
    - For a 36-bit address, bits 31 to 4 must be set to 0.
    - For a 64-bit shared memory address, non-zero 32-bit value.
    @tablebulletend */

   uint32_t loaned_mem_block_size;
   /**< Number of bytes loaned to the SPF from the specified global shared memory.
    Base virtual address of this memory block will be retrieved from the
    physical/virtual/byte-offset address value specified in this payload.
    */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_loan_global_shared_mem_t */
typedef struct apm_cmd_loan_global_shared_mem_t apm_cmd_loan_global_shared_mem_t;

/** This Command specifies memory block(s) which was previously loaned to the SPF instance
 from the specified global shared memory will no longer be available.

 @gpr_hdr_fields
 Opcode -- APM_CMD_UNLOAN_GLOBAL_SHARED_MEM

 @msgpayload{apm_cmd_unloan_global_shared_mem_t}
 @tablens{weak__apm__cmd__unloan__global_shared__mem__t}

 @return
 GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

 @dependencies
 None. @newpage
 */
#define APM_CMD_UNLOAN_GLOBAL_SHARED_MEM 0x0100105D

/** @weakgroup weak_apm_cmd_unloan_global_shared_mem_t
@{ */
#include "spf_begin_pack.h"
struct apm_cmd_unloan_global_shared_mem_t
{
   uint32_t shmem_id;
   /**< Unique identifier for the global shared memory.
    This is the same ID which was sent in #APM_CMD_LOAN_GLOBAL_SHARED_MEM.

    @values Any 32-bit value
    */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_unloan_global_shared_mem_t*/
typedef struct apm_cmd_unloan_global_shared_mem_t apm_cmd_unloan_global_shared_mem_t;

#endif

/** Commands the APM to map shared memory regions.
    All the mapped regions must be from the same memory pool.

    @gpr_hdr_fields
    Opcode -- APM_CMD_SHARED_MEM_MAP_REGIONS_V2

    @msgpayload{apm_cmd_shared_mem_map_regions_v2_t}
    @tablens{weak__apm__cmd__shared__mem__map__regions__v2__t}

    @par Memory region configuration (apm_shared_map_region_payload_t)
    @table{weak__apm__shared__map__region__payload__t}

    @return
    #GPR_IBASIC_RSP_RESULT

    @dependencies
    None. @newpage
*/
#define APM_CMD_SHARED_MEM_MAP_REGIONS_V2 0x01001061

/** @weakgroup weak_apm_cmd_shared_mem_map_regions_v2_t
@{ */
/** Immediately following this structure are num_regions of
    #apm_shared_map_region_payload_t.
*/
#include "spf_begin_pack.h"
struct apm_cmd_shared_mem_map_regions_v2_t
{
   uint32_t unique_shm_id;
   /**< Unique identifier set by the client for the current shared memory region being mapped.
     The client is responsible for setting a unique ID for each APM_CMD_SHARED_MEM_MAP_REGIONS_V2
     command that it sends to DSP. The same ID to be sent in the unmap command as well.

     @values 0x00000001 through 0x00FFFFFFFF

     The 8 MSB bits are reserved and must be set to 0. Hence the range of this field
     is 0 to 2^24 - 1.
     */

   uint32_t proc_domain_id;
   /**< Destination processor domain to which the current command is intended to.

       @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_ADSP
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2
        - #APM_PROC_DOMAIN_ID_APPS
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROC_DOMAIN_ID_GDSP_0
        - #APM_PROC_DOMAIN_ID_GDSP_1
        - #APM_PROC_DOMAIN_ID_APPS_2
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend

      If the client sends command to a different processor from what’s populated here,
      the receiving processor will route to the set proc domain.

      APM_PROP_ID_DONT_CARE – means don’t care. And the processor receiving the command will
      handle the mapping command.

      For example, if the field is set to APM_PROC_DOMAIN_ID_ADSP_2 but GPR command is sent
      to APM_PROC_DOMAIN_ID_ADSP. ADSP_0 routes the command to APM_PROC_DOMAIN_ID_ADSP_2.
   */

   uint16_t mem_pool_id;
   /**< Type of memory on which this memory region is mapped.

        @valuesbul
        - #APM_MEMORY_MAP_SHMEM8_4K_POOL
        - Other values are reserved

        The memory ID implicitly defines the characteristics of the memory.
        Characteristics can include alignment type, permissions, and so on.

        APM_MEMORY_MAP_SHMEM8_4K_POOL is shared memory, byte addressable, and
        4 KB aligned. */

   uint16_t num_regions;
   /**< Number of regions to map.

        @values > 0

        If the is_offset_mode bit in the property_flag is set, num_regions can
        only be set to 1.*/

   uint32_t property_flag;
   /**< Configures one common property for all regions in the payload. No
        two regions in the same command can have different properties.

        @values 0x00000000 through 0x00000007

        @contcell
        @subhead{Bit 0 -- IsVirtual flag}

        Indicates physical or virtual mapping:
          - 0 -- The shared memory address provided in
                 #apm_shared_map_region_payload_t is a physical address. The
                 shared memory must be mapped (a hardware TLB entry), and a
                 software entry must be added for internal bookkeeping.
          - 1 -- The shared memory address provided in the map
                 payload [usRegions] is a virtual address. The shared memory
                 must not be mapped (because the hardware TLB entry is already
                 available). But a software entry must be added for internal
                 bookkeeping. \n @vertspace{2}
                 This bit value (1) is useful if two services within the aDSP
                 are communicating via the APR. They can directly communicate
                 via the virtual address instead of the physical address. The
                 virtual regions must be contiguous.

        @subhead{Bit 1 -- is\_uncached flag}

        Indicates whether the memory is mapped as cached memory or uncached:
          - 0 -- Cached
          - 1 -- Uncached

        Most memories must be mapped as cached. Some special cases require
        uncached memory.

        @subhead{Bit 2 -- is\_offset\_mode flag}

        Indicates whether the memory will be subsequently referred to with
        offsets:
          - 0 -- Subsequent commands using mem_map_handle returned by
                 #APM_CMD_RSP_SHARED_MEM_MAP_REGIONS will use physical or
                 virtual addresses. \n @vertspace{2}
                 For example, buf_addr_lsw and buf_addr_msw in
                 data_cmd_wr_sh_mem_ep_data_buffer_t will be physical or
                 virtual addresses.
          - 1 -- Subsequent commands using mem_map_handle returned by
                 APM_CMD_RSP_SHARED_MEM_MAP_REGIONS will use offsets. \n
                 @vertspace{2}
                 For example, buf_addr_lsw and buf_addr_msw in
                 data_cmd_wr_sh_mem_ep_data_buffer_t will be offsets. The
                 offset is from the base address, which is defined as the
                 address given by apm_shared_map_region_payload_t.

        After mapping a physical region using this command, a mem_map_handle is
        returned. Subsequently, the client uses mem_map_handle in any command
        using addresses or offsets that belong to a memory region mapped in
        this command.

        @contcell
        @subhead{Bit 3 -- is\_client\_loaned\_memory flag}

        Specifies whether the client manages the memory or allows the APM to
        manage the memory:
          - 0 -- Client owned memory (the shared memory is managed by the
                 client). @vertspace{3}
          - 1 -- Client loaned memory (the shared memory is mapped by the
                 client and managed by the APM).

        For loaned memory, the client must relinquish the memory access
        permissions for multi-DSP framework (MDF) use cases.

        @contcell
        @subhead{Bit 4-5 -- loaned\_memory\_heap\_configuration}

        Specifies the heap configuration for the memory loaned by client and
        managed by APM ( .i.e., this configuration is valid only when Bit_3 is SET)
          - 0 -- Default Heap ( Regular heap, functionally backward compatible )
          - 1 -- Safe Heap    ( When the satellite is considered as un-secure
                                Process Domain, configuring as Safe Heap is
                                recommended )
          - 2/3 -- Values reserved for future use

         -It is recommended to use the same heap configuration for all the MDF memory maps
         for a pair for the master-satellite combination.
         -If multiple Satellite Process Domains (PD's) are present in a product
         with same loaned shared memory mapped to all the satellite PD's,
         and one of the satellite PD is termed as un-secure PD,
         configuring as Safe Heap is recommended.
         -In general, configuration as Default Heap is recommended.

        @contcell
        @subhead{Bit 6-8 -- memory\_address\_type}

         Specifies the memory address type. In the Default configuration (0), would ensure backward compatibility.
           -  0     � Physical/Virtual Address
           -  1     � File descriptor
           -  2-7   � Values reserved for Future

		 It is important to note, the Signal Processing Framework (a.k.a AudioReach SPF)  would not have any
		 mechanism to convert the memory address type or validate the same.
         This configuration would enable the HLOS system, specifically GSL in userspace and the audio packet
         driver with kernel space access, to determine if FD (file descriptor) to PA (physical address)
         conversion is required in the audio packet driver. This determination is based on the actual needs
         of the underlying process domain, which requires memory addresses accordingly.


        @subhead{Bits 31 to 9}

        Reserved and must be set to 0. */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_shared_mem_map_regions_t */
typedef struct apm_cmd_shared_mem_map_regions_v2_t apm_cmd_shared_mem_map_regions_v2_t;

/** Commands the APM to unmap multiple shared memory regions that were
    previously mapped via #APM_CMD_SHARED_MEM_MAP_REGIONS_V2.

    @gpr_hdr_fields
    Opcode -- APM_CMD_SHARED_MEM_UNMAP_REGIONS_V2

    @msgpayload{apm_cmd_shared_mem_unmap_regions_v2_t}
    @table{weak__apm__cmd__shared__mem__unmap__regions__v2__t}

    @return
    GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

    @dependencies
    An #APM_CMD_SHARED_MEM_MAP_REGIONS_V2 command must have been issued. @newpage
*/
#define APM_CMD_SHARED_MEM_UNMAP_REGIONS_V2 0x01001062

/** @weakgroup weak_apm_cmd_shared_mem_unmap_regions_v2_t
@{ */
#include "spf_begin_pack.h"
struct apm_cmd_shared_mem_unmap_regions_v2_t
{
   uint32_t unique_shm_id;
   /**< Unique identifier that was set by the client in the APM_CMD_SHARED_MEM_MAP_REGIONS_V2
     payload.

     @values 0x00000001 through 0x00FFFFFFFF

     The 8 MSB bits are reserved and must be set to 0. Hence the range of this field
     is 0 to 2^24 - 1. */

   uint32_t proc_domain_id;
   /**< Destination processor domain to which the current command is intended to.

       @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_ADSP
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2
        - #APM_PROC_DOMAIN_ID_APPS
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROC_DOMAIN_ID_GDSP_0
        - #APM_PROC_DOMAIN_ID_GDSP_1
        - #APM_PROC_DOMAIN_ID_APPS_2
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend

      If the client sends command to a different processor from what’s populated here,
      the receiving processor will route to the set proc domain.

      APM_PROP_ID_DONT_CARE – means don’t care. And the processor receiving the command will
      handle the mapping command.

      For example, if the field is set to APM_PROC_DOMAIN_ID_ADSP_2 but GPR command is sent
      to APM_PROC_DOMAIN_ID_ADSP. ADSP_0 routes the command to APM_PROC_DOMAIN_ID_ADSP_2.
   */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_shared_mem_unmap_regions_v2_t */
typedef struct apm_cmd_shared_mem_unmap_regions_v2_t apm_cmd_shared_mem_unmap_regions_v2_t;

/** APM command to set the peer DSP access info for a given shared memory region that
    was mapped earlier with APM_CMD_SHARED_MEM_UNMAP_REGIONS_V2.

    This command must be set to the processor domain, that was loaned a shared memory region
    with APM_CMD_SHARED_MEM_UNMAP_REGIONS_V2. It basically sets the list of processor domains
    that have mapping to a given shared memory region.

    @gpr_hdr_fields
    Opcode -- APM_CMD_SHARED_MEM_REGION_ACCESS_INFO

    @msgpayload{apm_cmd_shared_mem_region_access_info_t}
    @table{weak__apm__cmd__shared__mem__region__access__info__t}

    @return
    GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

    @dependencies
    An #APM_CMD_SHARED_MEM_MAP_REGIONS_V2 command must have been issued to all the processor domains
    listed in the payload.. @newpage
*/
#define APM_CMD_SHARED_MEM_REGION_ACCESS_INFO 0x01001063

/** @weakgroup weak_apm_cmd_shared_mem_region_access_info_t
@{ */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct apm_cmd_shared_mem_region_access_info_t
{
  uint32_t unique_shm_id;
  /**< Unique identifier that was set by the client in the APM_CMD_SHARED_MEM_MAP_REGIONS_V2
      payload.

      @values 0x00000001 through 0x00FFFFFFFF

      The 8 MSB bits are reserved and must be set to 0. Hence the range of this field
      is 0 to 2^24 - 1. */

  uint32_t proc_domain_id;
  /**< Destination processor domain to which the current command is intended to.

      @valuesbul
       - #APM_PROC_DOMAIN_ID_MDSP
       - #APM_PROC_DOMAIN_ID_ADSP
       - #APM_PROC_DOMAIN_ID_ADSP_1
       - #APM_PROC_DOMAIN_ID_ADSP_2
       - #APM_PROC_DOMAIN_ID_APPS
       - #APM_PROC_DOMAIN_ID_SDSP
       - #APM_PROC_DOMAIN_ID_CDSP
       - #APM_PROC_DOMAIN_ID_GDSP_0
       - #APM_PROC_DOMAIN_ID_GDSP_1
       - #APM_PROC_DOMAIN_ID_APPS_2
       - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend

     If the client sends command to a different processor from what’s populated here,
     the receiving processor will route to the set proc domain.

     APM_PROP_ID_DONT_CARE – means don’t care. And the processor receiving the command will
     handle the mapping command.

     For example, if the field is set to APM_PROC_DOMAIN_ID_ADSP_2 but GPR command is sent
     to APM_PROC_DOMAIN_ID_ADSP. ADSP_0 routes the command to APM_PROC_DOMAIN_ID_ADSP_2.
  */

  uint32_t num_peer_proc_domain_ids;
  /**< Number of processor domains to which the shared memory region has been mapped to.

    @values 0x1 through 0xF.
    */

  uint32_t peer_proc_domain_list[0];
  /**< List of Processor domain IDs to which the mapping has been done.

      @valuesbul
       - #APM_PROC_DOMAIN_ID_MDSP
       - #APM_PROC_DOMAIN_ID_ADSP
       - #APM_PROC_DOMAIN_ID_ADSP_1
       - #APM_PROC_DOMAIN_ID_ADSP_2
       - #APM_PROC_DOMAIN_ID_APPS
       - #APM_PROC_DOMAIN_ID_SDSP
       - #APM_PROC_DOMAIN_ID_CDSP
       - #APM_PROC_DOMAIN_ID_GDSP_0
       - #APM_PROC_DOMAIN_ID_GDSP_1
       - #APM_PROC_DOMAIN_ID_APPS_2
       - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend  */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_apm_cmd_shared_mem_unmap_regions_v2_t */
typedef struct apm_cmd_shared_mem_region_access_info_t apm_cmd_shared_mem_region_access_info_t;

/** @} */ /* end_addtogroup spf_apm_memory_map */

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* _APM_MEMMAP_API_H_ */
