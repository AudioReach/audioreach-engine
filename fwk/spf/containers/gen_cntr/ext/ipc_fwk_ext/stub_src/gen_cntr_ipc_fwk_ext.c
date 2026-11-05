/**
 * \file gen_cntr_ipc_fwk_ext.c
 * \brief
 *
 *
 * \copyright
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ar_osal_error.h"
#include "gen_cntr_i.h"
#include "gen_cntr_utils.h"

ar_result_t gen_cntr_get_and_update_fwk_extn_ipc_port_msg_cb_info(cu_base_t   *base_ptr,
                                                                  gu_module_t *gu_module_ptr,
                                                                  bool_t       is_ext_input,
                                                                  uint32_t     port_index)
{
   return AR_EUNSUPPORTED;
}

/*** Error check function - end */
ar_result_t gen_cntr_handle_ipc_data_link_info_event(gen_topo_t        *topo_ptr,
                                                     gen_topo_module_t *mod_ptr,
                                                     capi_event_info_t *event_info_ptr)
{
   return AR_EUNSUPPORTED;
}

ar_result_t gen_cntr_handle_ipc_port_gpr_cmd(cu_base_t *base_ptr)
{
   return AR_EUNSUPPORTED;
}

bool_t gen_cntr_check_if_ipc_ext_out_needs_buffer(gen_cntr_t *me_ptr)
{
   return FALSE;
}

void gen_cntr_ipc_ports_update_wait_mask(gen_cntr_t *me_ptr,
                                         uint32_t   *in_wait_mask,
                                         uint32_t   *out_wait_mask,
                                         uint32_t   *stop_mask,
                                         uint32_t   *optional_wait_mask)
{
   return;
}