/**
 * \file cu_voice_util.c
 * \brief
 *  This file contains container utility functions for voice path handling in CU
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "cu_i.h"
#include "apm.h"

/**
 * Returns TRUE if at least one subgraph in the container has the SID APM_SUB_GRAPH_SID_VOICE_CALL.
 */
bool_t cu_has_voice_sid(cu_base_t *base_ptr)
{

   return FALSE;
}

void cu_destroy_voice_info(cu_base_t *base_ptr)
{
   MFREE_NULLIFY(base_ptr->voice_info_ptr);
}

ar_result_t cu_create_voice_info(cu_base_t *base_ptr, spf_msg_cmd_graph_open_t *open_cmd_ptr)
{
   ar_result_t result = AR_EOK;

   return result;
}

ar_result_t cu_voice_session_cfg(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr)
{
   ar_result_t result = AR_EOK;

   return result;
}

ar_result_t cu_set_calibration_ops_done(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr)
{
   ar_result_t result = AR_EOK;

   return result;
}

ar_result_t cu_cntr_proc_params_query(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr)
{
   ar_result_t result = AR_EOK;

   return result;
}

ar_result_t cu_check_and_raise_voice_proc_param_update_event(cu_base_t *base_ptr,
                                                             uint32_t   log_id,
                                                             uint32_t   hw_acc_proc_delay,
                                                             bool_t     hw_acc_proc_delay_event)
{
   ar_result_t result = AR_EOK;

   return result;
}
