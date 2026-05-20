/**
 * \file cu_ipc_fwk_ext.c
 * \brief
 *     This file contains container utility functions for IPC Rx and Tx module.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "ar_osal_error.h"
#include "cu_i.h"
#include "rd_sh_mem_ep_api.h"
#include "wr_sh_mem_ep_api.h"
#include "ipc_module_cmn_api.h"

/* =======================================================================
Public Function Definitions
========================================================================== */

ar_result_t cu_ipc_tx_init_ext_out_port(cu_base_t         *base_ptr,
                                        gu_ext_out_port_t *gu_ext_port_ptr,
                                        uint32_t           ext_out_queue_offset)
{
   return AR_EUNSUPPORTED;
}

ar_result_t cu_destroy_ipc_ext_out_port_buffers(cu_base_t *base_ptr, gu_ext_out_port_t *ext_out_port_ptr)
{
   return AR_EUNSUPPORTED;
}

void cu_deinit_ipc_tx_ext_out_port(cu_base_t         *base_ptr,
                                   gu_ext_out_port_t *ext_out_port_ptr,
                                   bool_t             b_ignore_ports_from_sg_close,
                                   bool_t             force_deinit_all_ports)
{
   return;
}

ar_result_t cu_ipc_rx_init_ext_in_port(cu_base_t        *base_ptr,
                                       gu_ext_in_port_t *gu_ext_port_ptr,
                                       uint32_t          ext_in_queue_offset)
{
   return AR_EUNSUPPORTED;
}

void cu_deinit_ipc_rx_ext_in_port(cu_base_t        *base_ptr,
                                  gu_ext_in_port_t *ext_in_port_ptr,
                                  bool_t            b_ignore_ports_from_sg_close,
                                  bool_t            force_deinit_all_ports)
{
   return;
}


ar_result_t cu_ipc_port_ext_in_on_data_trigger(cu_base_t        *base_ptr,
                                               gu_ext_in_port_t *gu_ext_in_port_ptr,
                                               uint32_t          trigger_flags)
{
   return AR_EUNSUPPORTED;
}

/**
 * \brief Handles data trigger for an external output port.
 *
 * This function pops a buffer from the external output queue, checks the opcode,
 * and passes the buffer packet to the module for further processing.
 *
 * @param base_ptr Pointer to the base structure.
 * @param gu_ext_out_port_ptr Pointer to the external output port structure.
 *
 * @return AR_EOK on success, error code otherwise.
 */
ar_result_t cu_ipc_port_ext_out_on_data_trigger(cu_base_t         *base_ptr,
                                                gu_ext_out_port_t *gu_ext_out_port_ptr,
                                                uint32_t           trigger_flags)
{
   return AR_EUNSUPPORTED;
}

/**
 * \brief This function is used to trigger control messages for IPC ports.
 *
 * @param base_ptr A pointer to the base structure.
 *
 * @return ar_result_t The result of the operation.
 */
ar_result_t cu_ipc_port_ctrl_msg_trigger(cu_base_t *base_ptr)
{
   return AR_EUNSUPPORTED;
}

uint32_t cu_ipc_module_gpr_callback(gpr_packet_t *packet, void *callback_data)
{
   return AR_EUNSUPPORTED;
}

/** Polls IPC port data queue and sets on the module until module trigger is satisfied or if the queue is empty. */
ar_result_t cu_poll_and_setup_ipc_input_port_buffer(cu_base_t                   *base_ptr,
                                                    gu_ext_in_port_t            *ipc_ext_in_port_ptr,
                                                    fwk_extn_ipc_port_trigger_t *in_trigger_ptr,
                                                    bool_t                       should_underrun)
{
   return AR_EUNSUPPORTED;
}

ar_result_t cu_poll_and_setup_ipc_output_port_buffer(cu_base_t                   *base_ptr,
                                                     gu_ext_out_port_t           *ipc_ext_out_port_ptr,
                                                     fwk_extn_ipc_port_trigger_t *out_trigger_ptr,
                                                     bool_t                       allow_overrun)
{
   return AR_EUNSUPPORTED;
}

/** Module can return AR_ENOTREADY if it doesnt want to flush a given data msg.
    Then the container pushes back the msg to the queue. This is usually done for
    media format msgs. */
ar_result_t cu_ipc_ext_input_flush_data_queue(cu_base_t *base_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   return AR_EUNSUPPORTED;
}


/** Handle */
ar_result_t cu_handle_ipc_peer_port_property_update_gpr_cmd(cu_base_t *base_ptr)
{
   return AR_EUNSUPPORTED;
}


ar_result_t cu_handle_upstream_ipc_tx_stopped_ack(cu_base_t *base_ptr)
{
   return AR_EUNSUPPORTED;
}

// ipc modules
ar_result_t cu_ipc_rx_create_send_ipc_info_to_upstreams(cu_base_t        *base_ptr,
                                                        cu_ext_in_port_t *ext_in_port_ptr,
                                                        gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   return AR_EUNSUPPORTED;
}

ar_result_t cu_ipc_tx_handle_icb_info_from_ds(cu_base_t         *base_ptr,
                                              gu_ext_out_port_t *gu_ext_out_port_ptr,
                                              cu_ext_out_port_t *ext_out_port_ptr)
{
   return AR_EUNSUPPORTED;
}

ar_result_t cu_ipc_output_ports_update_sg_state(cu_base_t        *me_ptr,
                                                gu_module_t      *module_ptr,
                                                topo_port_state_t self_port_state)
{
   return AR_EUNSUPPORTED;
}

ar_result_t cu_ipc_input_ports_update_sg_state(cu_base_t        *me_ptr,
                                               gu_module_t      *module_ptr,
                                               topo_port_state_t self_port_state)
{
   return AR_EUNSUPPORTED;
}
