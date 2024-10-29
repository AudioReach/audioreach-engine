/**
 *   \file jitter_buf_driver_island.c
 *   \brief
 *        This file contains implementation of Jitter buffer driver
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "jitter_buf_driver.h"
#include "spf_circular_buffer.h"
#include "ar_msg.h"
#include "spf_list_utils.h"
#include "capi_jitter_buf_i.h"

// Use to override AR_MSG with AR_MSG_ISLAND. Always include this after ar_msg.h
#ifdef AR_MSG_IN_ISLAND
#include "ar_msg_island_override.h"
#endif

#ifdef DEBUG_CIRC_BUF_UTILS
#define DEBUG_JITTER_BUF_DRIVER
#endif

#define CIRCULAR_BUFFER_PREFERRED_CHUNK_SIZE 2048

/*==============================================================================
   Local Function Implementation
==============================================================================*/

/* Calculate and store until drift based on ts_total_data_written_start, current time
 * and data till current time */
void jitter_buf_update_local_drift_based_on_buffer(capi_jitter_buf_t *me_ptr, uint64_t cur_ts_us)
{
   uint64_t time_gap = 0, data_gap = 0;

   time_gap = cur_ts_us - me_ptr->ts_total_data_written_start;

   // already checked is_input_mf_receieved at process start
   data_gap = capi_cmn_bytes_to_us(me_ptr->total_data_written,
                                   me_ptr->operating_mf.format.sampling_rate,
                                   me_ptr->operating_mf.format.bits_per_sample,
                                   1,
                                   NULL);

   me_ptr->total_drift_pending_update_us = time_gap - data_gap;

   AR_MSG(DBG_LOW_PRIO,
          "JITTER_BUF_DRIVER: Drift: Local accumulated drift %ld timegap %ld datagap %ld",
          (int32_t)me_ptr->total_drift_pending_update_us,
          (int32_t)time_gap,
          (int32_t)data_gap);
}

/* Update the drift accumulated with the current drift adjustment */
ar_result_t jitter_buf_update_accumulated_drift(jitter_buf_drift_info_t *shared_drift_ptr,
                                                int64_t                  current_drift_adjustment)
{
   ar_result_t result = AR_EOK;

   if (!shared_drift_ptr)
   {
      return AR_EFAILED;
   }

   posal_mutex_lock(shared_drift_ptr->drift_info_mutex);

   shared_drift_ptr->acc_drift.acc_drift_us = current_drift_adjustment;

   posal_mutex_unlock(shared_drift_ptr->drift_info_mutex);

   AR_MSG(DBG_HIGH_PRIO, "jitter_buf: Drift: Updating imcl drift from Jitter Buf with %lld", current_drift_adjustment);

   return result;
}

/* Check if the local drift is more than the tolerance value and report
 * it if the control link is connected */
ar_result_t jitter_buf_check_update_imcl_drift(capi_jitter_buf_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   // updating imcl drift
   uint32_t drift_local_samples =
      capi_cmn_us_to_samples(me_ptr->total_drift_pending_update_us, me_ptr->operating_mf.format.sampling_rate);

   if ((drift_local_samples > JITTER_BUF_DRIFT_TOLERANCE_SAMPLES) ||
       (drift_local_samples < -JITTER_BUF_DRIFT_TOLERANCE_SAMPLES))
   {
      result |= jitter_buf_update_accumulated_drift(&me_ptr->drift_info, me_ptr->total_drift_pending_update_us);
      if (AR_EOK != result)
      {
         AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: Failed updating accumulated drift.");
      }
   }

   return result;
}

/* Check if the settlement time has passed from ts_first to current time */
bool_t jitter_buf_settlement_time_reached(capi_jitter_buf_t *me_ptr, uint64_t cur_ts_us)
{
   bool_t reached = FALSE;

   if (cur_ts_us - me_ptr->ts_first_data >= (me_ptr->drift_settlement_in_ms * 1000))
   {
      reached = TRUE;
   }
   else
   {
      reached = FALSE;
   }
   return reached;
}

/* Calculate drift once settlement time is done and report it if required */
ar_result_t jitter_buf_update_drift_based_on_buffer_fullness(capi_jitter_buf_t *me_ptr, uint32_t data_len)
{
   ar_result_t result = AR_EOK;

   if (JBM_BUFFER_INPUT_AT_INPUT_TRIGGER == me_ptr->input_buffer_mode)
   {
      // during this mode (for ICMD), no need to calculate drift as it won't be correct.
      return result;
   }

   uint64_t cur_ts_us = posal_timer_get_time();

   /*once settlement time is reached we do not un-set it - it is set only per session. */
   if (!me_ptr->settlement_time_done)
   {
      if (!jitter_buf_settlement_time_reached(me_ptr, cur_ts_us))
      {
         /* Drift calculation need not be done yet */
         return AR_EOK;
      }
      else
      {
         AR_MSG(DBG_HIGH_PRIO, "JITTER_BUF_DRIVER: Drift: Setllement Time Done at %u", cur_ts_us);
         me_ptr->settlement_time_done        = TRUE;
         me_ptr->ts_total_data_written_start = cur_ts_us;
         me_ptr->total_data_written += data_len;
         return AR_EOK;
      }
   }

   /* Drift Calculation for local drift donr */
   jitter_buf_update_local_drift_based_on_buffer(me_ptr, cur_ts_us);

   /*Update imcl drift - if the peer control port is not connected
    * we don't need to update the imcl drift. */
   if (me_ptr->ctrl_port_info.state == CTRL_PORT_PEER_CONNECTED)
   {
      result = jitter_buf_check_update_imcl_drift(me_ptr);

      if (AR_EOK == result)
      {
         /* once result is updated reset drift info */
         me_ptr->ts_total_data_written_start = cur_ts_us;
         me_ptr->total_data_written          = 0;
      }
   }

   /* Update data len after updating drift */
   me_ptr->total_data_written += data_len;

   return result;
}

/* If jitter buffer is ever drained out - fill the buffer with jitter size 0s */
ar_result_t jitter_buf_check_fill_zeros(capi_jitter_buf_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   bool_t is_buf_empty = FALSE;
   spf_circ_buf_driver_is_buffer_empty(me_ptr->driver_hdl.reader_handle, &is_buf_empty);

   if (is_buf_empty)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "Jitter_Buf Process Read: Buffer was drained - filling buffer with zeros %d",
             me_ptr->jiter_bytes);
      result = spf_circ_buf_memset(me_ptr->driver_hdl.writer_handle, TRUE, me_ptr->jiter_bytes, 0);
   }

   return result;
}

/* Jitter buffer write input stream into the jitter buffer */
ar_result_t jitter_buf_stream_write(capi_jitter_buf_t *me_ptr, capi_stream_data_t *in_stream)
{
   ar_result_t          result              = AR_EOK;
   jitter_buf_driver_t *drv_ptr             = &me_ptr->driver_hdl;
   bool_t               ALLOW_OVERFLOW_TRUE = TRUE;

   /* No need to write data if input is not present */
   if (NULL == in_stream->buf_ptr[0].data_ptr)
   {
#ifdef DEBUG_JITTER_BUF_DRIVER
      AR_MSG(DBG_MED_PRIO, "JITTER_BUF_DRIVER: Input buffer not preset, nothing to write.");
#endif
      return AR_EOK;
   }

   /* Cannot write data if not configured and created  */
   if (!me_ptr->driver_hdl.stream_buf && !me_ptr->is_input_mf_received)
   {
      AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: Write failed. Driver not intialized yet.");
      return AR_EFAILED;
   }

   /* Write one container frame into the circular buffer. */
   result = spf_circ_buf_write_one_frame(drv_ptr->writer_handle, in_stream, ALLOW_OVERFLOW_TRUE);
   if (SPF_CIRCBUF_OVERRUN == result)
   {
      AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: circular buffer overrun occured. %lu", result);
      result = AR_EOK;
   }
   else if (SPF_CIRCBUF_SUCCESS != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: Failed writing the frame to circular buffer");
      return AR_EFAILED;
   }

   /* If first frame is written store the timestamp at which first data is written */
   if (!me_ptr->first_frame_written)
   {
      me_ptr->first_frame_written = TRUE;
      me_ptr->ts_first_data       = posal_timer_get_time();
      AR_MSG(DBG_HIGH_PRIO, "JITTER_BUF_DRIVER: Drift: First Frame Written at %u", me_ptr->ts_first_data);
   }

#ifdef DEBUG_JITTER_BUF_DRIVER
   uint32_t unread_bytes = 0;
   spf_circ_buf_get_unread_bytes(drv_ptr->reader_handle, &unread_bytes);
   AR_MSG(DBG_HIGH_PRIO,
          "JITTER_BUF_DRIVER: Stream writer done. actual_data_len = %d, circ buf filled size=%lu",
          in_stream->buf_ptr[0].actual_data_len,
          unread_bytes);
#endif

   /* Apply drift correction if buffer size crosses upper/lower thresholds. */
   jitter_buf_update_drift_based_on_buffer_fullness(me_ptr, in_stream->buf_ptr[0].actual_data_len);

   return result;
}

/* Read data from the jitter buffer */
ar_result_t jitter_buf_stream_read(capi_jitter_buf_t *me_ptr, capi_stream_data_t *out_stream)
{
   ar_result_t result = AR_EOK;

   /* If output buffer is not present, no need to generate output */
   if (NULL == out_stream->buf_ptr[0].data_ptr)
   {
#ifdef DEBUG_JITTER_BUF_DRIVER
      AR_MSG(DBG_MED_PRIO, "JITTER_BUF_DRIVER: Output buffer not preset, nothing to read.");
#endif
      return AR_EOK;
   }

   /* If buffer is not configured and created nothing to read from */
   if (NULL == me_ptr->driver_hdl.stream_buf && !me_ptr->is_input_mf_received)
   {
      AR_MSG(DBG_ERROR_PRIO, "JITTER_BUF_DRIVER: Read failed. Driver not intialized yet.");
      return AR_EFAILED;
   }

   jitter_buf_driver_t *drv_ptr = &me_ptr->driver_hdl;

   /* Read one container frame from the circular buffer. */
   result = spf_circ_buf_read_one_frame(drv_ptr->reader_handle, out_stream);
   if (SPF_CIRCBUF_UNDERRUN == result)
   {
      return AR_ENEEDMORE;
   }
   else if (SPF_CIRCBUF_FAIL == result)
   {
      AR_MSG(DBG_HIGH_PRIO, "JITTER_BUF_DRIVER: Failed reading data from the buffer.");
      return AR_EFAILED;
   }

#ifdef DEBUG_JITTER_BUF_DRIVER
   uint32_t unread_bytes = 0;
   spf_circ_buf_get_unread_bytes(drv_ptr->reader_handle, &unread_bytes);
   AR_MSG(DBG_HIGH_PRIO,
          "JITTER_BUF_DRIVER: Stream reader done. actual_data_len = %d, circ buf filled size=%lu",
          out_stream->buf_ptr[0].actual_data_len,
          unread_bytes);
#endif

   return result;
}
