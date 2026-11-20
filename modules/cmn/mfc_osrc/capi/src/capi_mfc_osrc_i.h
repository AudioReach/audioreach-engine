/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file capi_MFC_OSRC_i.h
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------
 * 7/15/26   vasharma    Added file for opensource resampler based MFC
 * =========================================================================*/

#ifndef CAPI_mfc_osrc_I_H
#define CAPI_mfc_osrc_I_H

#include "capi_mfc_osrc_utils.h"

capi_vtbl_t *capi_mfc_osrc_get_vtable();

capi_err_t capi_mfc_osrc_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

capi_err_t capi_mfc_osrc_end(capi_t *_pif);

capi_err_t capi_mfc_osrc_set_param(capi_t *                _pif,
                                     uint32_t                param_id,
                                     const capi_port_info_t *port_info_ptr,
                                     capi_buf_t *            params_ptr);

capi_err_t capi_mfc_osrc_get_param(capi_t *                _pif,
                                     uint32_t                param_id,
                                     const capi_port_info_t *port_info_ptr,
                                     capi_buf_t *            params_ptr);

capi_err_t capi_mfc_osrc_set_properties(capi_t *me_ptr, capi_proplist_t *props_ptr);

capi_err_t capi_mfc_osrc_get_properties(capi_t *me_ptr, capi_proplist_t *props_ptr);

capi_err_t capi_mfc_osrc_check_buffers(capi_mfc_osrc_t *me_ptr,
                                         capi_buf_t *       src_buf_ptr,
                                         capi_buf_t *       dest_buf_ptr,
                                         uint32_t           port,
                                         uint32_t           eos_flag);

capi_err_t capi_mfc_osrc_check_scratch_buffer(capi_mfc_osrc_t *me_ptr,
                                                capi_buf_t *       src_buf_ptr,
                                                capi_buf_t *       dest_buf_ptr,
                                                uint32_t           port);

capi_err_t capi_mfc_osrc_set_param_non_island(capi_mfc_osrc_t *     me_ptr,
                                                uint32_t                param_id,
                                                const capi_port_info_t *port_info_ptr,
                                                capi_buf_t *            params_ptr);

static inline void pcm_to_capi_interleaved_with_native(capi_interleaving_t *capi_value,
                                                       int16_t              cfg_value,
                                                       capi_interleaving_t  inp_value)
{
   switch (cfg_value)
   {
      case PCM_INTERLEAVED:
      {
         *capi_value = CAPI_INTERLEAVED;
         break;
      }
      case PCM_DEINTERLEAVED_PACKED:
      {
         *capi_value = CAPI_DEINTERLEAVED_PACKED;
         break;
      }
      case PCM_DEINTERLEAVED_UNPACKED:
      {
         *capi_value = CAPI_DEINTERLEAVED_UNPACKED_V2;
         break;
      }
      case PARAM_VAL_NATIVE:
      {
         *capi_value = inp_value;
         break;
      }
      default:
         break;
   }
}

#endif /* CAPI_mfc_osrc_I_H */
