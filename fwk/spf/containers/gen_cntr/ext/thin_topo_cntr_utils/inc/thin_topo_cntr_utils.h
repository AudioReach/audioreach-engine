#ifndef THIN_TOPO_CNTR_UTILS_H
#define THIN_TOPO_CNTR_UTILS_H
/**
 * \file thin_topo_cntr_utils.h
 * \brief
 *     This file contains functions container utilties for thin topo.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "gen_cntr.h"
#include "thin_topo.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/** If GC switched to thin topo, signal trigger handler is called */
ar_result_t thin_topo_signal_trigger_handler(cu_base_t *cu_ptr, uint32_t channel_bit_index);

// thin topo
ar_result_t thin_topo_update_process_info(gen_cntr_t *me_ptr);

ar_result_t gen_cntr_switch_to_thin_topo_util_(gen_cntr_t *me_ptr);

ar_result_t thin_topo_handle_fwk_events(gen_cntr_t                 *me_ptr,
                                        gen_topo_capi_event_flag_t *capi_event_flag_ptr,
                                        cu_event_flags_t           *fwk_event_flag_ptr);


#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef THIN_TOPO_CNTR_UTILS_H
