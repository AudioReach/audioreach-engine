/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file capi_mfc_osrc_utils_island.cpp
 *
 */
/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------
 * 7/15/26   vasharma    Added file for opensource resampler based MFC
 * =========================================================================*/

#include "capi_mfc_osrc_utils.h"


/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   This function raises dm event to the fwk to inform the input samples required/output produced
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_raise_dm_event(capi_mfc_osrc_t *                 me_ptr,
                                          fwk_extn_dm_param_id_req_samples_t *req_samples_ptr,
                                          uint32_t                            event_id)
{
   capi_err_t capi_result = CAPI_EOK;

   capi_event_data_to_dsp_service_t event_payload;
   capi_event_info_t                event_info;
   event_payload.param_id                = event_id;
   event_payload.token                   = 0;
   event_payload.payload.data_ptr        = (int8_t *)req_samples_ptr;
   event_payload.payload.actual_data_len = sizeof(fwk_extn_dm_param_id_req_samples_t);
   event_payload.payload.max_data_len    = sizeof(fwk_extn_dm_param_id_req_samples_t);
   event_info.payload.data_ptr           = (int8_t *)&event_payload;
   event_info.payload.actual_data_len    = sizeof(event_payload);
   event_info.payload.max_data_len       = sizeof(event_payload);
   event_info.port_info.is_valid         = FALSE;
   capi_result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_DATA_TO_DSP_SERVICE, &event_info);
   if (CAPI_FAILED(capi_result))
   {
      CNV_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "Failed to send report samples event with result %d",
              capi_result);
   }

   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   This function is used to query the resampler of req input samples to produce fixed out samples
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_get_fixed_out_samples(capi_mfc_osrc_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   // OSResampler is always SW — query required input samples directly
   uint32_t expected_in_samples = mfc_osrc_get_fixed_out_samples(&me_ptr->pc[0], me_ptr->dm_info.req_out_samples);
   me_ptr->dm_info.expected_in_samples = expected_in_samples;

#ifdef MFC_OSRC_DEBUG
   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "Fixed out samples %lu, in req %lu",
           me_ptr->dm_info.req_out_samples,
           me_ptr->dm_info.expected_in_samples);
#endif

   if (me_ptr->dm_info.expected_in_samples)
   {
      // raise event
      fwk_extn_dm_param_id_req_samples_t req_samples;
      req_samples.is_input                           = TRUE;
      req_samples.num_ports                          = 1;
      req_samples.req_samples[0].port_index          = 0;
      req_samples.req_samples[0].samples_per_channel = me_ptr->dm_info.expected_in_samples;
      capi_result |= capi_mfc_osrc_raise_dm_event(me_ptr, &req_samples, FWK_EXTN_DM_EVENT_ID_REPORT_SAMPLES);
   }

   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Prints media fmt
  ____________________________________________________________________________________________________________________*/
void capi_mfc_osrc_print_media_fmt(capi_mfc_osrc_t *me_ptr, mfc_media_fmt_t *fmt_ptr, uint32_t port)
{
   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "bit_width : %u , word_size : %u, q_factor : %u, interleaving : %u, endianness : %u, "
           "alignment : %u, num_channels : %u sampling rate %d",
           fmt_ptr->bit_width,
           fmt_ptr->word_size,
           fmt_ptr->q_factor,
           fmt_ptr->interleaving,
           fmt_ptr->endianness,
           fmt_ptr->alignment,
           fmt_ptr->num_channels,
           fmt_ptr->sampling_rate);
}


