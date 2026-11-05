/**
 * \file spl_cntr_ipc_fwk_ext.c
 *
 * \brief
 *     Implementation of IPC fwk extn in SPL container.
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause */

/* =======================================================================
Includes
========================================================================== */
#include "ar_osal_error.h"
#include "spl_cntr_i.h"

ar_result_t spl_cntr_ipc_input_data_q_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index)
{
   return AR_EUNSUPPORTED;
}

ar_result_t spl_cntr_ipc_output_data_q_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index)
{
   return AR_EUNSUPPORTED;
}

ar_result_t spl_cntr_update_ipc_ext_in_ports_gpd_mask(spl_cntr_t *me_ptr)
{
   return AR_EOK;
}

ar_result_t spl_cntr_update_ipc_ext_out_ports_gpd_mask(spl_cntr_t *me_ptr)
{
   return AR_EOK;
}

ar_result_t spl_cntr_get_and_update_fwk_extn_ipc_port_msg_cb_info(cu_base_t   *base_ptr,
                                                                  gu_module_t *gu_module_ptr,
                                                                  bool_t       is_ext_input,
                                                                  uint32_t     port_index)
{
   return AR_EUNSUPPORTED;
}

ar_result_t spl_cntr_handle_ipc_data_link_info_event(gen_topo_t        *topo_ptr,
                                                     gen_topo_module_t *mod_ptr,
                                                     capi_event_info_t *event_info_ptr)
{
   return AR_EUNSUPPORTED;
}

ar_result_t spl_cntr_handle_ipc_port_gpr_cmd(cu_base_t *base_ptr)
{
   return AR_EUNSUPPORTED;
}
