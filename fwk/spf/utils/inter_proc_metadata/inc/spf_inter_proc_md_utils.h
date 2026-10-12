#ifndef _SPF_IPCMD_UTILS_H_
#define _SPF_IPCMD_UTILS_H_

/**
 * \file spf_ref_counter.h
 * \brief
 *     This file defines the api for a reference counting server.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/*-------------------------------------------------------------------------
Include Files
-------------------------------------------------------------------------*/

/* System */
#include "posal.h"
#include "module_cmn_metadata.h"
#include "spf_utils.h"
#include "metadata_api.h"
#include "sh_mem_ep_metadata_api.h"
#include "gpr_api_inline.h"
#include "spf_thread_pool.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/*************************** MACROS ***************************/
#define SPF_MAGIC_MD_TRACKING_KEY 0xBABADEAD
#define MAX_PROC_DOMAIN_COUNT 5
#define MAX_SUPPORTED_CONCURRENT_JOBS 50

/** debug macro */
#define DEBUG_SPF_MD_TRACKING_UTILS

// inter proc MD propagation debug msg prefix
#define MIID_UNKNOWN 0
#define IPMD_MSG_PREFIX "MIID:[%lX] "
// Fix: use IPMD_MSG_PREFIX (not IPC_TX_MSG_PREFIX)
#define IPMD_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, IPMD_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

// even to increment and decrement refcount at the process boundary
#define EVENT_ID_IPC_METADATA_INCR_REF_COUNT 0x0300100F

#define EVENT_ID_IPC_METADATA_DECR_REF_COUNT 0x03001010

/*************************** Structures ***************************/
typedef struct spf_ipcmd_node_refcount_t
{
   uint16_t domain_id;
   uint8_t  ref_count;
   uint8_t  max_ref_count;
} spf_ipcmd_node_refcount_t;

// context pointer for the GPR command
typedef struct tp_job_info
{
   spf_thread_pool_job_t job;
   bool_t                in_use;
   gpr_packet_t         *gpr_pkt_ptr;
} tp_job_info_t;

typedef struct spf_ipcmd_node_info_t
{
   uint32_t total_ref_count;

   spf_ipcmd_node_refcount_t ref_count_list[MAX_PROC_DOMAIN_COUNT];

   /* token populated when sending MD to another proc domain. Note that this token will be different from the token
    * populated by the client in the actual tracking MD in the "md_ptr". This token is used only by fwk to handle
    * tracking MD events in the src domain context.*/
   uint32_t token_lsw;
   uint32_t token_msw;

   module_cmn_md_list_t *md_ptr;
   /**< list of tracking metadata*/

} spf_ipcmd_node_info_t;

typedef struct spf_ipcmd_tracker_util_t
{
   spf_list_node_t *tracking_md_list_ptr;

   spf_thread_pool_inst_t *thread_pool_ptr;

   posal_mutex_t mutex;

   tp_job_info_t job_list[MAX_SUPPORTED_CONCURRENT_JOBS];

} spf_ipcmd_tracker_util_t;

/*---------------------------------------------------------------------------
Function Declarations and Documentation
----------------------------------------------------------------------------*/

ar_result_t spf_ipcmd_init();

ar_result_t spf_ipcmd_deinit();

uint32_t spf_ipmd_gpr_callback_handler(gpr_packet_t *gpr_pkt_ptr);

bool_t spf_ipcmd_is_md_already_being_tracked(uint32_t log_id, module_cmn_md_t *md_ptr);

ar_result_t spf_add_md_to_inter_proc_md_tracker(
   uint32_t               log_id,
   void                  *md_extn_context_ptr, // this context ptr is same the one set by the INTF_EXTN_METADATA_HANDLER
   module_cmn_md_list_t  *md_node_ptr,
   module_cmn_md_list_t **src_md_list_pptr,
   metadata_header_t     *md_data_header_ptr,
   POSAL_HEAP_ID          heap_id);

ar_result_t spf_ipcmd_raise_event_to_update_ref_count(uint32_t                          log_id,
                                                      module_cmn_md_tracking_payload_t *md_tracking_ptr,
                                                      uint32_t                          metadata_id,
                                                      bool_t                            is_increment_ref_count);

// Kept signature consistent with request even if it doesn’t log internally
void spf_ipcmd_convert_client_md_flag_to_int_md_flags(uint32_t               log_id,
                                                      uint32_t               client_md_flags,
                                                      module_cmn_md_flags_t *int_md_flags);

/** need to call this is SSR/PDR context to destory all the nodes. if valid arguments are passed deletes the nodes
 which matches the arguments, else deletes all the nodes. */
ar_result_t spf_ipcmd_destroy_pending_tracked_md_info(uint32_t log_id,
                                                      uint32_t has_valid_args,
                                                      uint32_t remote_proc_domain_id,
                                                      bool_t   render_event);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef _SPF_IPCMD_UTILS_H_
