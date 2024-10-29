/**
 *   \file capi_jitter_buf_imcl_utils_island.c
 *   \brief
 *        This file contains CAPI implementation of Jitter Buf module.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_jitter_buf_i.h"

// Use to override AR_MSG with AR_MSG_ISLAND. Always include this after ar_msg.h
#ifdef AR_MSG_IN_ISLAND
#include "ar_msg_island_override.h"
#endif

/*==============================================================================
   Local Function forward declaration
==============================================================================*/

/*==============================================================================
   Public Function Implementation
==============================================================================*/

/* Client uses this to read drift from the BFBDE */
ar_result_t jitter_buf_imcl_read_acc_out_drift(imcl_tdi_hdl_t *      drift_info_hdl_ptr,
                                               imcl_tdi_acc_drift_t *acc_drift_out_ptr)
{
   ar_result_t result = AR_EOK;

#ifdef DEBUG_JITTER_BUF_DRIVER
   AR_MSG(DBG_HIGH_PRIO, "jitter_buf: Reading drift from jitter_buf");
#endif

   if (!drift_info_hdl_ptr || !acc_drift_out_ptr)
   {
      return AR_EFAILED;
   }

   jitter_buf_drift_info_t *shared_drift_ptr = (jitter_buf_drift_info_t *)drift_info_hdl_ptr;

   posal_mutex_lock(shared_drift_ptr->drift_info_mutex);

   /* Copy the accumulated drift info */
   memscpy(acc_drift_out_ptr, sizeof(imcl_tdi_acc_drift_t), &shared_drift_ptr->acc_drift, sizeof(imcl_tdi_acc_drift_t));

   posal_mutex_unlock(shared_drift_ptr->drift_info_mutex);

   return result;
}
