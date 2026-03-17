#ifndef GEN_CNTR_PEER_CNTR_H
#define GEN_CNTR_PEER_CNTR_H
/**
 * \file gen_cntr_peer_cntr.h
 * \brief
 *     This file contains functions for peer container handling.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr.h"

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

/** ------------------------------------------- Peer Container Output----------------------------------------------*/
ar_result_t gen_cntr_process_pop_out_buf(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_port_ptr);
ar_result_t gen_cntr_init_after_popping_peer_cntr_out_buf(gen_cntr_t *             me_ptr,
                                                          gen_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t gen_cntr_return_back_out_buf(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_port_ptr);
ar_result_t gen_cntr_ext_out_port_apply_pending_media_fmt(void *me_ptr, gu_ext_out_port_t *ext_out_port_ptr);
ar_result_t gen_cntr_create_send_media_fmt_to_peer_cntr(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t gen_cntr_flush_output_data_queue_peer_cntr(gen_cntr_t *             me_ptr,
                                                       gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                       bool_t                   is_client_cmd);
ar_result_t gen_cntr_write_data_for_peer_cntr(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t gen_cntr_init_peer_cntr_ext_out_port(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_port_ptr);
ar_result_t gen_cntr_recreate_out_buf_peer_cntr(gen_cntr_t *             me_ptr,
                                                gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                uint32_t                 req_out_buf_size,
                                                uint32_t                 num_data_msg,
                                                uint32_t                 num_bufs_per_data_msg_v2);
ar_result_t gen_cntr_get_output_port_media_format_from_topo(gen_cntr_ext_out_port_t *            ext_out_port_ptr,
                                                            bool_t                               update_to_unchanged);
/** ------------------------------------------- Peer Container Input ----------------------------------------------*/
ar_result_t gen_cntr_process_pending_data_cmd_peer_cntr(gen_cntr_t *            me_ptr,
                                                               gen_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t gen_cntr_data_ctrl_cmd_handle_inp_media_fmt_from_upstream_cntr(gen_cntr_t *            me_ptr,
                                                                           gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                           spf_msg_header_t *      msg_header,
                                                                           bool_t                  is_data_path);
ar_result_t gen_cntr_copy_peer_or_olc_client_input(gen_cntr_t *            me_ptr,
                                                   gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                   uint32_t *              bytes_copied_ptr);
ar_result_t gen_cntr_init_peer_cntr_ext_in_port(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_port_ptr);
ar_result_t gen_cntr_peer_output_setup_topo_buffer(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);

ar_result_t gen_cntr_copy_peer_cntr_input_to_int_buf(gen_cntr_t *            me_ptr,
                                                            gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                            uint32_t *              bytes_copied_ptr);
ar_result_t gen_cntr_input_dataQ_trigger_peer_cntr(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);

ar_result_t gen_cntr_check_if_any_ext_in_has_to_preserve_prebuffer(gen_topo_t *topo_ptr,
                                                                   bool_t     *has_to_preserve_prebuffer);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef GEN_CNTR_PEER_CNTR_H
