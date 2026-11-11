/**
 * \file gen_topo_sync_fwk_ext.c
 *
 * \brief
 *
 *     Implementation of stub utilities for FWK_EXTN_SYNC in gen cntr
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */


#include "ar_osal_error.h"
#include "gen_topo.h"

ar_result_t gen_topo_get_ipc_port_callback_info(gen_topo_t                            *topo_ptr,
                                                gen_topo_module_t                     *module_ptr,
                                                fwk_extn_prop_ipc_msg_callback_info_t *cb_info,
                                                bool_t                                 is_ext_input,
                                                uint32_t                               port_index)
{
   return AR_EUNSUPPORTED;
}

bool_t gen_topo_is_ipc_module_trigger_satisfied(gen_topo_t        *topo_ptr,
                                                gen_topo_module_t *module_ptr,
                                                bool_t             inp_has_no_trigger,
                                                bool_t             out_has_no_trigger)
{
   return FALSE;
}

ar_result_t gen_topo_init_set_get_ipc_data_port_properties(gen_topo_module_t     *module_ptr,
                                                           gen_topo_t            *topo_ptr,
                                                           gen_topo_graph_init_t *graph_init_ptr)
{
   return AR_EOK;
}

void gen_topo_pure_st_check_ipc_input_port_triggers(gen_topo_t        *topo_ptr,
                                                    gen_topo_module_t *module_ptr,
                                                    bool_t            *atleast_one_input_has_data)
{
   return;
}

void gen_topo_pure_st_check_ipc_output_port_triggers(gen_topo_t        *topo_ptr,
                                                     gen_topo_module_t *module_ptr,
                                                     bool_t            *atleast_one_op_started,
                                                     bool_t            *all_started_out_ports_have_trigger)
{
   return;
}
