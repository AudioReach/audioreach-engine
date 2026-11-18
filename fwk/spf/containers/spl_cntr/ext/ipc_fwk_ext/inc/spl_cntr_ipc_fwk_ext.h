#ifndef SPL_CNTR_IPC_FWK_EXT_H
#define SPL_CNTR_IPC_FWK_EXT_H
/**
 * \file spl_cntr_ipc_fwk_ext.h
 *
 * \brief
 *     Implementation of IPC fwk extn in SPL container.
 *
 *  This file contains utility functions for FWK_EXTN_IPC_PORT_HANDLER.
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
// clang-format on

#include "ar_defs.h"
#include "ar_error_codes.h"

#if defined(__cplusplus)
extern "C" {
#endif // __cplusplus

typedef struct spl_cntr_t        spl_cntr_t;
typedef struct cu_base_t         cu_base_t;
typedef struct gu_module_t       gu_module_t;
typedef struct gen_topo_t        gen_topo_t;
typedef struct gen_topo_module_t gen_topo_module_t;
typedef struct capi_event_info_t capi_event_info_t;

ar_result_t spl_cntr_ipc_input_data_q_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index);
ar_result_t spl_cntr_ipc_output_data_q_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index);

ar_result_t spl_cntr_update_ipc_ext_in_ports_gpd_mask(spl_cntr_t *me_ptr);

ar_result_t spl_cntr_update_ipc_ext_out_ports_gpd_mask(spl_cntr_t *me_ptr);

ar_result_t spl_cntr_get_and_update_fwk_extn_ipc_port_msg_cb_info(cu_base_t   *base_ptr,
                                                                  gu_module_t *gu_module_ptr,
                                                                  bool_t       is_ext_input,
                                                                  uint32_t     port_index);

ar_result_t spl_cntr_handle_ipc_port_gpr_cmd(cu_base_t *base_ptr);

ar_result_t spl_cntr_handle_ipc_data_link_info_event(gen_topo_t        *topo_ptr,
                                                     gen_topo_module_t *mod_ptr,
                                                     capi_event_info_t *event_info_ptr);

#if defined(__cplusplus)
}
#endif // __cplusplus

#endif /* SPL_CNTR_IPC_FWK_EXT_H */
