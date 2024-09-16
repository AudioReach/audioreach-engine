#ifndef GEN_CNTR_RD_SH_MEM_EP_H
#define GEN_CNTR_RD_SH_MEM_EP_H
/**
 * \file gen_cntr_rd_sh_mem_ep.h
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

ar_result_t gen_cntr_flush_cache_and_release_out_buf(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t gen_cntr_create_rd_sh_mem_ep(gen_cntr_t *           me_ptr,
                                         gen_topo_module_t *    module_ptr,
                                         gen_topo_graph_init_t *graph_init_ptr);
ar_result_t gen_cntr_init_gpr_client_ext_out_port(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_port_ptr);

ar_result_t gen_cntr_release_gpr_client_buffer(gen_cntr_t *             me_ptr,
                                               gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                               ar_result_t              errCode);
ar_result_t gen_cntr_send_media_fmt_to_gpr_client(gen_cntr_t *             me_ptr,
                                                  gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                  uint32_t                 reg_mf_event_id,
                                                  bool_t                   raise_only_event);
ar_result_t gen_cntr_rd_ep_num_loops_err_check(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr);
ar_result_t gen_cntr_output_buf_set_up_gpr_client(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t gen_cntr_write_data_for_gpr_client(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t gen_cntr_fill_frame_metadata(gen_cntr_t *             me_ptr,
                                         gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                         uint32_t                 bytes_per_process,
                                         uint32_t                 frame_offset_in_ext_buf,
										 bool_t                  * release_out_buf_ptr);
uint32_t gen_cntr_get_amount_of_data_in_gpr_client_buf(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t gen_cntr_flush_output_data_queue_gpr_client(gen_cntr_t *             me_ptr,
                                                        gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                        bool_t                   is_client_cmd);
void gen_cntr_propagate_metadata_gpr_client(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);

ar_result_t gen_cntr_rd_sh_mem_setup_topo_buffer(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef GEN_CNTR_RD_SH_MEM_EP_H
