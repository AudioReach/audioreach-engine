#ifndef CU_VOICE_H
#define CU_VOICE_H

/**
 * \file cu_voice.h
 *  
 * \brief
 *  
 *     Common container framework code.
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "posal.h"
#include "topo_utils.h"
#include "spf_list_utils.h"
#include "proxy_cntr_if.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

typedef struct cu_base_t         cu_base_t;
typedef struct cu_ext_out_port_t cu_ext_out_port_t;
typedef struct cu_ext_in_port_t  cu_ext_in_port_t;

// clang-format off
bool_t cu_has_voice_sid(cu_base_t *base_ptr);

ar_result_t cu_create_voice_info(cu_base_t *base_ptr,    spf_msg_cmd_graph_open_t *open_cmd_ptr);

void cu_destroy_voice_info(cu_base_t *base_ptr);

ar_result_t cu_voice_session_cfg(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr);
ar_result_t cu_set_calibration_ops_done(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr);

ar_result_t cu_offload_voice_session_cfg(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr);

ar_result_t cu_cntr_proc_params_query(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr);

ar_result_t cu_check_and_raise_voice_proc_param_update_event(cu_base_t *base_ptr, uint32_t log_id, uint32_t hw_acc_proc_delay, bool_t hw_acc_proc_delay_event);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef CU_VOICE_H
