/**
 * \file spf_ref_counter.c
 * \brief
 *     This file contains the implementation for spf message utility functions.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "ar_osal_error.h"
#include "capi.h"
#include "spf_inter_proc_md_utils.h"

ar_result_t spf_ipcmd_init()
{
   return AR_EUNSUPPORTED;
}

ar_result_t spf_ipcmd_deinit()
{
   return AR_EUNSUPPORTED;
}

uint32_t spf_ipmd_gpr_callback_handler(gpr_packet_t *gpr_pkt_ptr)
{
   return AR_EUNSUPPORTED;
}

bool_t spf_ipcmd_is_md_already_being_tracked(uint32_t log_id, module_cmn_md_t *md_ptr)
{
   return AR_EUNSUPPORTED;
}

ar_result_t spf_add_md_to_inter_proc_md_tracker(uint32_t               log_id,
                                                void                  *md_extn_context_ptr,
                                                module_cmn_md_list_t  *md_node_ptr,
                                                module_cmn_md_list_t **src_md_list_pptr,
                                                metadata_header_t     *md_data_header_ptr,
                                                POSAL_HEAP_ID          heap_id)
{
   return AR_EUNSUPPORTED;
}

ar_result_t spf_ipcmd_raise_event_to_update_ref_count(uint32_t                          log_id,
                                                      module_cmn_md_tracking_payload_t *md_tracking_ptr,
                                                      uint32_t                          metadata_id,
                                                      bool_t                            is_increment_ref_count)
{
   return AR_EUNSUPPORTED;
}

// Kept signature consistent with request even if it doesn’t log internally
void spf_ipcmd_convert_client_md_flag_to_int_md_flags(uint32_t               log_id,
                                                      uint32_t               client_md_flags,
                                                      module_cmn_md_flags_t *int_md_flags)
{
   return;
}

/** need to call this is SSR/PDR context to destory all the nodes. if valid arguments are passed deletes the nodes
 which matches the arguments, else deletes all the nodes. */
ar_result_t spf_ipcmd_destroy_pending_tracked_md_info(uint32_t log_id,
                                                      uint32_t has_valid_args,
                                                      uint32_t remote_proc_domain_id,
                                                      bool_t   render_event)
{
   return AR_EUNSUPPORTED;
}