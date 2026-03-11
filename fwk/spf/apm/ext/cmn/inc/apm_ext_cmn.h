/**
 * \file apm_ext_cmn.h
 *  
 * \brief
 *  
 *     This file contains structure definition and structure declaration for APM
 *     extended functionalities.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _APM_EXT_CMN_H_
#define _APM_EXT_CMN_H_

#include "apm_data_path_utils.h"
#include "apm_close_all_utils.h"
#include "apm_shmem_util.h"
#include "apm_offload_utils.h"
#include "apm_err_hdlr_utils.h"
#include "apm_sys_util.h"
#include "apm_pwr_mgr_utils.h"
#include "apm_set_get_cfg_utils.h"
#include "apm_cntr_peer_heap_utils.h"
#include "apm_runtime_link_hdlr_utils.h"
#include "apm_db_query.h"
#include "apm_parallel_cmd_utils.h"
#include "apm_debug_info_cfg.h"
#include "apm_multi_client.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*==============================================================================
 Structure Definition
==============================================================================*/

struct apm_ext_utils_t
{
   apm_data_path_utils_vtable_t         *data_path_vtbl_ptr;
   /** Data path utils Vtable ptr */

   apm_close_all_utils_vtable_t         *close_all_vtbl_ptr;
   /** Close All Vtable ptr */

   apm_shmem_utils_vtable_t             *shmem_vtbl_ptr;
   /** Close All Vtable ptr */

   apm_offload_utils_vtable_t           *offload_vtbl_ptr;
   /** Offload utils vtable ptr */

   apm_err_hdlr_utils_vtable_t          *err_hdlr_vtbl_ptr;
   /** Offload utils vtable ptr */

   apm_sys_util_vtable_t                *sys_util_vtbl_ptr;
   /** Sys util Vtable ptr */

   apm_pwr_mgr_utils_vtable_t           *pwr_mgr_vtbl_ptr;
   /** Power Manger vtable ptr */

   apm_set_get_cfg_utils_vtable_t       *set_get_cfg_vtbl_ptr;
   /** Set Get Config vtable ptr */

   apm_cntr_peer_heap_utils_vtable_t    *cntr_peer_heap_utils_vtbl_ptr;
   /** Peer Heap Utils vtable ptr */

   apm_runtime_link_hdlr_utils_vtable_t *runtime_link_hdlr_vtbl_ptr;
   /** Runtime Link Hdlr vtble ptr */

   apm_parallel_cmd_utils_vtable_t      *parallel_cmd_utils_vtbl_ptr;
   /** Parallel cmd handler utils vtble ptr */
   
   apm_multi_client_vtable_t            *multi_client_vtbl_ptr;
   /** multi client vtble ptr */

   apm_db_query_utils_vtable_t          *db_query_vtbl_ptr;
   /** apm db query vtble  ptr */

   apm_debug_info_utils_vtable_t        *debug_info_utils_vtable_ptr;
   /** apm debug info vtable ptr */

};


/*==============================================================================
 Public Function definitions
==============================================================================*/

/** Inits the ext utils.

   return:  on success, or error code otherwise.
*/
ar_result_t apm_ext_utils_init(apm_t *apm_info_ptr);

/** Deinits the ext utils.

   return:  on success, or error code otherwise.
*/
ar_result_t apm_ext_utils_deinit(apm_t *apm_info_ptr);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* #ifdef _APM_EXT_CMN_H_ */
