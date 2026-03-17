#ifndef _APM_OFFLOAD_MEMMAP_HANDLER_H_
#define _APM_OFFLOAD_MEMMAP_HANDLER_H_

/**
 * \file apm_offload_memmap_handler.h
 *
 * \brief
 *     This file declares utility functions to manage shared memory between the processors in the Multi DSP Framework,
 *  including physical to virtual address mapping, etc.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/

#include "spf_utils.h"
#include "apm_internal.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#define APM_OFFLOAD_MEM_MAP_DBG

/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Class Definitions
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Function Declarations and Documentation
 * -------------------------------------------------------------------------*/

ar_result_t apm_offload_shmem_cmd_handler(apm_t *apm_info_ptr, spf_msg_t *msg_ptr);

ar_result_t apm_offload_mem_basic_rsp_handler(apm_t *         apm_info_ptr,
                                              apm_cmd_ctrl_t *apm_cmd_ctrl_ptr,
                                              gpr_packet_t *  gpr_pkt_ptr);

ar_result_t apm_offload_mem_basic_rsp_handler_v2(apm_t          *apm_info_ptr,
                                                 apm_cmd_ctrl_t *apm_cmd_ctrl_ptr,
                                                 gpr_packet_t   *gpr_pkt_ptr);
#ifdef __cplusplus
}
#endif //__cplusplus

#endif // _APM_OFFLOAD_MEMMAP_HANDLER_H_
