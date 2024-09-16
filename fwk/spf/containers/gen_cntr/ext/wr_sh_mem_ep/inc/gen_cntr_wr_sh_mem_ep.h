#ifndef GEN_CNTR_WR_SH_MEM_EP_H
#define GEN_CNTR_WR_SH_MEM_EP_H
/**
 * \file gen_cntr_wr_sh_mem_ep.h
 * \brief
 *     This file contains utility functions for offloading
 *  
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr.h"
#include "gen_cntr_cmn_utils.h"
#include "gen_topo.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

typedef struct gen_cntr_t               gen_cntr_t;
typedef struct gen_cntr_timestamp_t     gen_cntr_timestamp_t;
typedef struct gen_cntr_ext_out_port_t  gen_cntr_ext_out_port_t;
typedef struct gen_cntr_ext_in_port_t   gen_cntr_ext_in_port_t;
typedef struct gen_cntr_ext_ctrl_port_t gen_cntr_ext_ctrl_port_t;
typedef struct gen_cntr_circ_buf_list_t gen_cntr_circ_buf_list_t;
typedef struct gen_cntr_module_t        gen_cntr_module_t;

ar_result_t gen_cntr_create_wr_sh_mem_ep(gen_cntr_t *           me_ptr,
                                         gen_topo_module_t *    module_ptr,
                                         gen_topo_graph_init_t *graph_init_ptr);
ar_result_t gen_cntr_init_gpr_client_ext_in_port(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_port_ptr);
ar_result_t gen_cntr_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client(gen_cntr_t *            me_ptr,
                                                                       gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                       media_format_t *        media_fmt_ptr,
                                                                       bool_t                  is_data_path);
ar_result_t gen_cntr_input_dataQ_trigger_gpr_client(gen_cntr_t *            me_ptr,
                                                    gen_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t gen_cntr_copy_gpr_client_input_to_int_buf(gen_cntr_t *            me_ptr,
                                                      gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                      uint32_t *              bytes_copied_ptr);
bool_t gen_cntr_is_input_a_gpr_client_data_buffer(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t gen_cntr_process_pending_data_cmd_gpr_client(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t gen_cntr_free_input_data_cmd_gpr_client(gen_cntr_t             *me_ptr,
                                                    gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                    ar_result_t             status,
                                                    bool_t                  is_flush);
ar_result_t gen_cntr_wr_shm_setup_int_port_buf(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef GEN_CNTR_WR_SH_MEM_EP_H
