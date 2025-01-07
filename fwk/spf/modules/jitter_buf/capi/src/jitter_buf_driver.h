#ifndef _JITTER_BUF_DRIVER_LIB_H
#define _JITTER_BUF_DRIVER_LIB_H

/**
 *   \file jitter_buf_driver.h
 *   \brief
 *        This file contains utility functions for handling jitter buf buffering utils.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_defs.h"
#include "posal.h"
#include "capi_types.h"
#include "spf_list_utils.h"
#include "spf_circular_buffer.h"

// DEBUG macro
//#define DEBUG_JITTER_BUF_DRIVER
#define JITTER_BUF_TOLERANCE_FRAMES 2

/*==============================================================================
   Function declarations
==============================================================================*/

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

typedef struct capi_jitter_buf_t capi_jitter_buf_t;
/* Capi pointer structure */

typedef struct jitter_buf_driver_t jitter_buf_driver_t;
/* Jitter buffer driver structure */

struct jitter_buf_driver_t
{
   uint32_t circ_buf_size_in_us;
   /* Buffer size based on the decoder frame size and
    * the jitter buffer delay from the set param */

   uint32_t total_data_written_in_ms;
   uint32_t total_data_read_in_ms;
   /* Debug variables to track total data written/read from start of buffer
    * for finding out effective drift between read-write over a period of time */

   spf_circ_buf_t *stream_buf;
   /* Circular buffer used by the jitter buffer */

   spf_circ_buf_client_t *writer_handle;
   /* Writer client handle for jitter buffer */

   spf_circ_buf_client_t *reader_handle;
   /* Reader client handle for jitter buffer */
};

ar_result_t jitter_buf_driver_init(capi_jitter_buf_t *me_ptr);

ar_result_t jitter_buf_driver_deinit(capi_jitter_buf_t *me_ptr);

ar_result_t jitter_buf_stream_write(capi_jitter_buf_t *drv_ptr, capi_stream_data_t *in_stream);

ar_result_t jitter_buf_calibrate_driver(capi_jitter_buf_t *me_ptr);

ar_result_t jitter_buf_stream_read(capi_jitter_buf_t *drv_ptr, capi_stream_data_t *out_stream);

ar_result_t jitter_buf_update_drift_based_on_buffer_fullness(capi_jitter_buf_t *me_ptr, bool_t is_input_trig);

void jitter_buffer_set_steady_state_buffer_fullness(capi_jitter_buf_t *me_ptr);

capi_err_t jitter_buf_check_fill_zeros(capi_jitter_buf_t *me_ptr);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /*_JITTER_BUF_DRIVER_LIB_H*/
