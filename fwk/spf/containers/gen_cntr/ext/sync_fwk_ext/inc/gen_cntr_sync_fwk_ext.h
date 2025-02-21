#ifndef GEN_CNTR_SYNC_FWK_H
#define GEN_CNTR_SYNC_FWK_H
/**
 * // clang-format off
 * \file gen_cntr_sync_fwk_ext.h
 * \brief
 *  This file contains utility functions for FWK_EXTN_SYNC
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
// clang-format on

#include "gen_cntr.h"
#include "gen_cntr_cmn_utils.h"
#include "gen_topo.h"

#if defined(__cplugenus)
extern "C" {
#endif // __cplugenus

typedef struct gen_cntr_t gen_cntr_t;
typedef struct gen_topo_input_port_t gen_topo_input_port_t;

//to handle threshold enable/disable event from the module
ar_result_t gen_cntr_fwk_extn_sync_handle_toggle_threshold_buffering_event(
   gen_cntr_t *                      me_ptr,
   gen_topo_module_t *               module_ptr,
   capi_buf_t *                      payload_ptr,
   capi_event_data_to_dsp_service_t *dsp_event_ptr);

//update external input port trigger policy
void gen_cntr_fwk_ext_sync_update_ext_in_tgp(gen_cntr_t *me_ptr);

bool_t gen_cntr_fwk_ext_sync_requires_data(gen_cntr_t *me_ptr, gen_topo_input_port_t *in_port_ptr);

#if defined(__cplugenus)
}
#endif // __cplugenus

#endif /* GEN_CNTR_SYNC_FWK_H */
