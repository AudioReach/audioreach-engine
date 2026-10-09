#ifndef GEN_TOPO_MOD_BUF_ACC_EXT_H
#define GEN_TOPO_MOD_BUF_ACC_EXT_H
/**
 * // clang-format off
 * \file gen_topo_mod_buf_access_ext.h
 * \brief
 *  This file contains utility functions for INTF_EXTN_MODULE_BUFFER_ACCESS
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
// clang-format on

#include "ar_defs.h"
#include "ar_error_codes.h"
#include "gen_topo.h"

#if defined(__cplugenus)
extern "C" {
#endif // __cplugenus

typedef struct gen_topo_t                            gen_topo_t;
typedef struct gen_topo_module_t                     gen_topo_module_t;
typedef struct gen_topo_output_port_t                gen_topo_output_port_t;
typedef struct gen_topo_input_port_t                 gen_topo_input_port_t;
typedef struct gen_topo_graph_init_t                 gen_topo_graph_init_t;
typedef struct fwk_extn_prop_ipc_msg_callback_info_t fwk_extn_prop_ipc_msg_callback_info_t;

ar_result_t gen_topo_clear_and_replace_capi_input_buffers(gen_topo_t *topo_ptr, bool_t force_drop_data);
ar_result_t gen_topo_clear_and_replace_capi_output_buffers(gen_topo_t *topo_ptr, bool_t force_drop_data);
bool_t      gen_topo_cur_output_has_capi_op_buf_extn_port_us(uint32_t log_id, gen_topo_output_port_t *out_port_ptr);
bool_t      gen_topo_cur_input_has_capi_inp_buf_extn_port_ds(uint32_t log_id, gen_topo_input_port_t *in_port_ptr);
ar_result_t gen_topo_handle_assign_buffers_capi_input_buffer_extn_module(gen_topo_t *topo_ptr);

ar_result_t gen_topo_clear_and_replace_capi_input_buffers_util(gen_topo_t            *topo_ptr,
                                                               gen_topo_input_port_t *in_port_ptr,
                                                               bool_t                 force_drop_data);

ar_result_t gen_topo_clear_and_replace_capi_output_buffers_util(gen_topo_t             *topo_ptr,
                                                                gen_topo_output_port_t *out_port_ptr,
                                                                bool_t                  force_drop_data);
ar_result_t gen_topo_query_n_assign_capi_input_port_util(gen_topo_t *topo_ptr, gen_topo_input_port_t *in_port_ptr);

// ar_result_t gen_topo_propagate_capi_module_buffer_backwards(gen_topo_t *topo_ptr);

#if defined(__cplugenus)
}
#endif // __cplugenus

#endif /* GEN_TOPO_MOD_BUF_ACC_EXT_H */
