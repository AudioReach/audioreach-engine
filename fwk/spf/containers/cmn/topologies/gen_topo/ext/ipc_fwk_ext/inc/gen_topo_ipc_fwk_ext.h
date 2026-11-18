#ifndef GEN_TOPO_IPC_FWK_EXT_H
#define GEN_TOPO_IPC_FWK_EXT_H
/**
 * // clang-format off
 * \file gen_topo_ipc_fwk_ext.h
 * \brief
 *  This file contains utility functions for FWK_EXTN_IPC_PORT_HANDLER
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
// clang-format on

#include "ar_defs.h"
#include "ar_error_codes.h"

#if defined(__cplugenus)
extern "C" {
#endif // __cplugenus

typedef struct gen_topo_t gen_topo_t;
typedef struct gen_topo_module_t gen_topo_module_t;
typedef struct gen_topo_graph_init_t gen_topo_graph_init_t;
typedef struct fwk_extn_prop_ipc_msg_callback_info_t fwk_extn_prop_ipc_msg_callback_info_t;

ar_result_t gen_topo_get_ipc_port_callback_info(gen_topo_t                            *topo_ptr,
                                                gen_topo_module_t                     *module_ptr,
                                                fwk_extn_prop_ipc_msg_callback_info_t *cb_info,
                                                bool_t                                 is_ext_input,
                                                uint32_t                               port_index);

bool_t gen_topo_is_ipc_module_trigger_satisfied(gen_topo_t        *topo_ptr,
                                                gen_topo_module_t *module_ptr,
                                                bool_t            inp_has_no_trigger,
                                                bool_t            out_has_no_trigger);

ar_result_t gen_topo_init_set_get_ipc_data_port_properties(gen_topo_module_t     *module_ptr,
                                                           gen_topo_t            *topo_ptr,
                                                           gen_topo_graph_init_t *graph_init_ptr);

void gen_topo_pure_st_check_ipc_input_port_triggers(gen_topo_t        *topo_ptr,
                                                    gen_topo_module_t *module_ptr,
                                                    bool_t            *atleast_one_input_has_data);

void gen_topo_pure_st_check_ipc_output_port_triggers(gen_topo_t        *topo_ptr,
                                                     gen_topo_module_t *module_ptr,
                                                     bool_t            *atleast_one_op_started,
                                                     bool_t            *all_started_out_ports_have_trigger);

#if defined(__cplugenus)
}
#endif // __cplugenus

#endif /* GEN_TOPO_IPC_FWK_EXT_H */
