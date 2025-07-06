/**
 * \file apm_ext_cmn.c
 *
 * \brief
 *
 *      This file contains function definition for APM extended
 *      functionalites.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**==============================================================================
   Global Defines
==============================================================================*/

#include "apm_close_all_utils.h"
#include "apm_data_path_utils.h"
#include "apm_offload_utils.h"
#include "apm_shmem_util.h"
#include "apm_err_hdlr_utils.h"
#include "apm_sys_util.h"
#include "apm_pwr_mgr_utils.h"
#include "apm_set_get_cfg_utils.h"
#include "apm_cntr_peer_heap_utils.h"
#include "apm_runtime_link_hdlr_utils.h"
#include "apm_parallel_cmd_utils.h"
#include "apm_db_query.h"
#include "apm_debug_info_cfg.h"
#include "apm_multi_client.h"

ar_result_t apm_ext_utils_init(apm_t *apm_info_ptr)
{
   ar_result_t result = AR_EOK;

   apm_data_path_utils_init(apm_info_ptr);

   apm_close_all_utils_init(apm_info_ptr);

   apm_offload_utils_init(apm_info_ptr);

   apm_shmem_utils_init(apm_info_ptr);

   apm_err_hdlr_utils_init(apm_info_ptr);

   apm_sys_util_init(apm_info_ptr);

   apm_pwr_mgr_utils_init(apm_info_ptr);

   apm_set_get_cfg_utils_init(apm_info_ptr);

   apm_cntr_peer_heap_utils_init(apm_info_ptr);

   apm_runtime_link_hdlr_utils_init(apm_info_ptr);

   apm_parallel_cmd_utils_init(apm_info_ptr);

   apm_multi_client_init(apm_info_ptr);

   apm_db_query_init(apm_info_ptr);
   
   apm_debug_info_init(apm_info_ptr);

   return result;
}

ar_result_t apm_ext_utils_deinit(apm_t *apm_info_ptr)
{
   ar_result_t result = AR_EOK;

   /** De-register offload utils   */
   result |= apm_offload_utils_deinit();

   /** De-init sys util   */
   result |= apm_sys_util_deinit();

   return result;
}
