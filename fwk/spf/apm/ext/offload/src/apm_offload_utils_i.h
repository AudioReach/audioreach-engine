#ifndef _APM_OFFLOAD_UTILS_I_H__
#define _APM_OFFLOAD_UTILS_I_H__

/**
 * \file apm_offload_utils_i.h
 *
 * \brief
 *     This file contains function declaration for APM utilities for offload handling
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "posal.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/
/*------------------------------------------------------------------------------
 *  Constants/Macros
 *----------------------------------------------------------------------------*/

/****************************************************************************
 * Structure Definitions                                                    *
 ****************************************************************************/

/** Satellite container information for offload container */
typedef struct apm_satellite_cont_node_info_t apm_satellite_cont_node_info_t;

struct apm_satellite_cont_node_info_t
{
   uint32_t satellite_cont_id;
   /**< Satellite Container ID */

   uint32_t parent_cont_id;
   /**< OLC Container ID in master process domain*/
};

/**------------------------------------------------------------------------------
 *  Function Declaration
 *----------------------------------------------------------------------------*/

/**
  Creates the Heap Manager and maintains a record of the
  Master Handle with the associated heap details.

  @datatypes

  @param[in] mem_map_client   Client who has registered to memorymap.
  @param[in] master_handle    Memory Map Handle of the Master DSP.
  @param[in] mem_size         Size of the memory loaned to the master.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the global must be initialized.
*/
ar_result_t apm_offload_master_memorymap_register(uint32_t mem_map_client,
                                                  uint32_t master_handle,
                                                  uint32_t mem_size,
                                                  uint32_t heap_mgr_type);

/**
  Deletes the Heap Manager and destroys the record of the
  Master Handle and the associated heap.

  @datatypes

  @param[in] master_handle    Memory Map Handle of the Master DSP.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the global must be initialized.
*/
ar_result_t apm_offload_master_memorymap_check_deregister(uint32_t master_handle);

#ifdef __cplusplus
}
#endif /*__cplusplus*/
#endif /* _APM_OFFLOAD_UTILS_I_H__ */
