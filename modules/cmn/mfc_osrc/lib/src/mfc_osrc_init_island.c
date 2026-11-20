/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "mfc_osrc_converter.h"

// Use to override AR_MSG with AR_MSG_ISLAND. Always include this after ar_msg.h
// #ifdef AR_MSG_IN_ISLAND
// #include "ar_msg_island_override.h"
// #endif

uint32_t mfc_osrc_get_fixed_out_samples(mfc_osrc_lib_t *pc_ptr, uint32_t req_out_samples)
{
   uint32_t    exp_in_samples = 0;
   ar_result_t result         = osrc_rs_lib_get_required_input_samples(pc_ptr->osrc_rs_lib_ptr,
                                                                        req_out_samples,
                                                                        &exp_in_samples);
   if (AR_EOK != result)
   {
      CNV_MSG(pc_ptr->miid, DBG_ERROR_PRIO,
              "mfc_osrc_get_fixed_out_samples: failed to get required input samples, result=0x%lx",
              result);
      return 0;
   }
   return exp_in_samples;
}

