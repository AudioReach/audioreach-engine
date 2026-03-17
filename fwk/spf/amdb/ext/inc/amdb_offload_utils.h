#ifndef _AMDB_MDF_UTILS_H_
#define _AMDB_MDF_UTILS_H_

/**
 * \file amdb_offload_utils.h
 * \brief
 *     This file contains utility functions for AMDB command handling
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/****************************************************************************
 * INCLUDE HEADER FILES                                                     *
 ****************************************************************************/
#include "spf_cmn_if.h"
#include "apm_api.h"
#include "apm_offload_mem.h"

#include "gpr_api_inline.h"
#include "amdb_thread_i.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/****************************************************************************
 * Structure Definitions                                                    *
 ****************************************************************************/
struct amdb_cmd_ctrl_t
{
   uint32_t               token;              /**< Token used to identify the command ctrl obj */
   uint32_t               cmd_opcode;         /**< Command opcode under process */
   spf_msg_t              cmd_msg;            /**< Command payload GK msg */
   uint32_t               dst_domain_id;      /**< Destination proc domain id */
   bool_t                 is_out_of_band;     /**< Flag to indicate out of band */
   apm_offload_ret_info_t ret_info;           /**< offload shared memory info*/
   void *                 loaned_mem_ptr;     /**< Loaned mem ptr associated with the cmd payload. NULL if inband. */
   void *                 master_payload_ptr; /**< Loaned mem ptr associated with the cmd payload. NULL if inband. */
};

typedef struct amdb_cmd_ctrl_t amdb_cmd_ctrl_t;

ar_result_t amdb_clean_up_proc_id_cmd_ctrl(amdb_thread_t *amdb_info_ptr, uint32_t proc_domain_id);

ar_result_t amdb_set_cmd_ctrl(amdb_thread_t *   amdb_info_ptr,
                              spf_msg_t *       msg_ptr,
                              void *            master_payload_ptr,
                              bool_t            is_out_of_band,
                              amdb_cmd_ctrl_t **curr_cmd_ctrl_pptr,
                              uint32_t          dst_domain_id);

ar_result_t amdb_clear_cmd_ctrl(amdb_thread_t *prm_info_ptr, amdb_cmd_ctrl_t *amdb_cmd_ctrl_ptr);

ar_result_t amdb_route_cmd_to_satellite(amdb_thread_t *prm_info_ptr,
                                        spf_msg_t *    msg_ptr,
                                        uint32_t       dst_domain_id,
                                        uint8_t *      amdb_payload_ptr);

ar_result_t amdb_route_basic_rsp_to_client(amdb_thread_t *prm_info_ptr, spf_msg_t *msg_ptr);

ar_result_t amdb_route_get_cfg_rsp_to_client(amdb_thread_t *amdb_info_ptr, spf_msg_t *msg_ptr);

ar_result_t amdb_route_load_rsp_to_client(amdb_thread_t *prm_info_ptr, spf_msg_t *msg_ptr);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _AMDB_MDF_UTILS_H_ */
