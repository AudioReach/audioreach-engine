#ifndef CU_IPC_FWK_EXT_H
#define CU_IPC_FWK_EXT_H

/**
 * \file cu_ipc_fwk_ext.h
 *
 * \brief
 *
 *     Common container IPC module extension related code.
 *
 *
 * \copyright
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "posal.h"
#include "topo_utils.h"
#include "spf_list_utils.h"
#include "capi_fwk_extns_ipc_port_handler.h"
#include "ipc_tx_rx_api.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

typedef struct cu_base_t         cu_base_t;
typedef struct cu_ext_out_port_t cu_ext_out_port_t;
typedef struct cu_ext_in_port_t  cu_ext_in_port_t;

#define ALLOW_UNDERRUN TRUE
#define DENY_UNDERRUN FALSE

#define ALLOW_OVERRUN TRUE
#define DENY_OVERRUN FALSE

// enable for verbose logging
// #define IPC_MODULE_VERBOSE_DEBUGGING

typedef struct cu_fwk_extn_ipc_port_info_t
{
   void *callback_handle_ptr;

   fwk_extn_ipc_port_trigger_t *ext_port_trigger_shared_ptr;

   fwk_extn_ipc_port_ctrl_msg_handler_fptr_t ctrl_msg_handler; // SPF_CMD_PRIVATE_CTRL_MSG
   /**< Module's ctrl cmds or ctrl cmd rsp opcode handlers. After handling the ipc msgs, module is expected to call
    * either free() or end() on the packets to return response for the . If the msg type is rsp cmd, module can only
    * call free() if it called end() error will be reported. */

   fwk_extn_ipc_port_data_msg_handler_fptr_t data_msg_handler; // SPF_CMD_PRIVATE_DATA_MSG
   /**< Module's data cmds or data cmd rsp opcode handlers. After handling the ipc msgs, module is expected to call
    * either free() or end() on the packets to return response for the . If the msg type is rsp cmd, module can only
    * call free() if it called end() error will be reported. */

   uint32_t peer_port_id;
   /**< Port ID of the destination external input/output port. If module is IPC Rx input is external input, peer is
    * considered external output. This is optional param, if the destination is a port less client this may not be set.
    */

   uint32_t peer_module_iid;
   /**< Peer module instance ID, this is the unique identifier or GPR destination address. */

   uint32_t peer_proc_domain_id;
   /**< Peer processor domain ID this it the proc domain of the destination module instance. */

} cu_fwk_extn_ipc_port_info_t;

/** CU IPC EXTERNAL PORT UTILITIES */

ar_result_t cu_ipc_rx_init_ext_in_port(cu_base_t        *base_ptr,
                                       gu_ext_in_port_t *gu_ext_port_ptr,
                                       uint32_t          ext_in_queue_offset);

ar_result_t cu_ipc_tx_init_ext_out_port(cu_base_t         *base_ptr,
                                        gu_ext_out_port_t *gu_ext_port_ptr,
                                        uint32_t           ext_out_queue_offset);

ar_result_t cu_destroy_ipc_ext_out_port_buffers(cu_base_t *base_ptr, gu_ext_out_port_t *ext_out_port_ptr);

ar_result_t cu_ipc_port_ext_out_on_data_trigger(cu_base_t         *base_ptr,
                                                gu_ext_out_port_t *gu_ext_out_port_ptr,
                                                uint32_t           trigger_flags);
ar_result_t cu_ipc_port_ext_in_on_data_trigger(cu_base_t        *base_ptr,
                                               gu_ext_in_port_t *gu_ext_in_port_ptr,
                                               uint32_t          trigger_flags);

ar_result_t cu_ipc_port_ctrl_msg_trigger(cu_base_t *base_ptr);

ar_result_t cu_poll_and_setup_ipc_input_port_buffer(cu_base_t                   *base_ptr,
                                                    gu_ext_in_port_t            *ipc_ext_in_port_ptr,
                                                    fwk_extn_ipc_port_trigger_t *in_trigger_ptr,
                                                    bool_t                       allow_underrun);

ar_result_t cu_poll_and_setup_ipc_output_port_buffer(cu_base_t                   *base_ptr,
                                                     gu_ext_out_port_t           *ipc_ext_out_port_ptr,
                                                     fwk_extn_ipc_port_trigger_t *out_trigger_ptr,
                                                     bool_t                       allow_overrun);

uint32_t cu_ipc_module_gpr_callback(gpr_packet_t *packet, void *callback_data);

void cu_deinit_ipc_rx_ext_in_port(cu_base_t        *base_ptr,
                                  gu_ext_in_port_t *ext_in_port_ptr,
                                  bool_t            b_ignore_ports_from_sg_close,
                                  bool_t            force_deinit_all_ports);
void cu_deinit_ipc_tx_ext_out_port(cu_base_t         *base_ptr,
                                   gu_ext_out_port_t *ext_out_port_ptr,
                                   bool_t             b_ignore_ports_from_sg_close,
                                   bool_t             force_deinit_all_ports);


ar_result_t cu_ipc_ext_input_flush_data_queue(cu_base_t        *base_ptr,
                                             gu_ext_in_port_t *gu_ext_in_port_ptr);

ar_result_t cu_handle_ipc_peer_port_property_update_gpr_cmd(cu_base_t *base_ptr);

ar_result_t cu_handle_upstream_ipc_tx_stopped_ack(cu_base_t *base_ptr);

ar_result_t cu_ipc_rx_create_send_ipc_info_to_upstreams(cu_base_t        *base_ptr,
                                                     cu_ext_in_port_t *ext_in_port_ptr,
                                                     gu_ext_in_port_t *gu_ext_in_port_ptr);

ar_result_t cu_ipc_tx_handle_icb_info_from_ds(cu_base_t         *base_ptr,
                                              gu_ext_out_port_t *gu_ext_out_port_ptr,
                                              cu_ext_out_port_t *ext_out_port_ptr);

ar_result_t cu_ipc_output_ports_update_sg_state(cu_base_t        *me_ptr,
                                                gu_module_t      *module_ptr,
                                                topo_port_state_t self_port_state);

ar_result_t cu_ipc_input_ports_update_sg_state(cu_base_t        *me_ptr,
                                               gu_module_t      *module_ptr,
                                               topo_port_state_t self_port_state);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef CU_IPC_FWK_EXT_H
