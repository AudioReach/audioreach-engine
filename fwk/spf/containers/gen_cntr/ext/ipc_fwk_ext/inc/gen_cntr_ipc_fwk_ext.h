#ifndef GEN_CNTR_IPC_FWK_EXT_H
#define GEN_CNTR_IPC_FWK_EXT_H
/**
 * \file gen_cntr_ipc_fwk_ext.h
 * \brief
 *     This file contains utility functions for IPC fwk extn handlers.
 *
 *
 *
 * \copyright
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gen_cntr.h"
#include "gen_cntr_cmn_utils.h"
#include "gen_topo.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

typedef struct gen_cntr_t               gen_cntr_t;
typedef struct gen_cntr_ext_out_port_t  gen_cntr_ext_out_port_t;

ar_result_t gen_cntr_ipc_tx_init_ext_out_port(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_port_ptr);

ar_result_t gen_cntr_ipc_port_ext_in_data_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index);
ar_result_t gen_cntr_ipc_port_ext_out_data_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index);
ar_result_t gen_cntr_ipc_port_cmd_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index);

ar_result_t gen_cntr_get_and_update_fwk_extn_ipc_port_msg_cb_info(cu_base_t   *base_ptr,
                                                                  gu_module_t *gu_module_ptr,
                                                                  bool_t       is_ext_input,
                                                                  uint32_t     port_index);

ar_result_t gen_cntr_handle_ipc_data_link_info_event(gen_topo_t        *topo_ptr,
                                                     gen_topo_module_t *mod_ptr,
                                                     capi_event_info_t *event_info_ptr);

ar_result_t gen_cntr_handle_ipc_port_gpr_cmd(cu_base_t *base_ptr);

bool_t gen_cntr_check_if_ipc_ext_out_needs_buffer(gen_cntr_t *me_ptr);

void gen_cntr_ipc_ports_update_wait_mask(gen_cntr_t *me_ptr,
                                         uint32_t   *in_wait_mask,
                                         uint32_t   *out_wait_mask,
                                         uint32_t   *stop_mask,
                                         uint32_t   *optional_wait_mask);
#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef GEN_CNTR_IPC_FWK_EXT_H
