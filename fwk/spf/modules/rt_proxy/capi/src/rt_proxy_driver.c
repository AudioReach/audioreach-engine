/**
 *   \file rt_proxy_driver.c
 *   \brief
 *        This file contains implementation of Audio Dam buffer driver
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "rt_proxy_driver.h"
#include "spf_circular_buffer.h"
#include "ar_msg.h"
#include "spf_list_utils.h"
#include "capi_rt_proxy_i.h"

#ifdef DEBUG_CIRC_BUF_UTILS
#define DEBUG_RT_PROXY_DRIVER
#endif

#define INT_MAX_64 0x7FFFFFFFFFFFFFFF

#define MEM_ALLOC_8_BYTE_ALIGNED 8

#define ALIGN_8_BYTES(a) (((a) & (~(uint32_t)0x7)) + 8)

#define CIRCULAR_BUFFER_PREFERRED_CHUNK_SIZE 2048

#define RT_PROXY_SIZE_FRACT_FACTOR 3

#define RT_PROXY_SIZE_INT_FACTOR 2

#define RT_PROXY_DRIFT_ALLOWANCE_MS 5

// Drift correction value, correct 8us for 10ms.
// The amount of drift correction must be atleast as high as the maximum drift possible
// else the RAT correction can never catch up to the RT clients drift.
// Drift correction is done when the buf size crosses the threshold. As the correction
// continues the buf size will eventually falls back within the thresholds.
#define DRIFT_CORRECTION_CONSTANT 8

#define NUM_MS_PER_FRAME 1

/*==============================================================================
   Local Declarations
==============================================================================*/

/*==============================================================================
   Local Function Implementation
==============================================================================*/

ar_result_t rt_proxy_port_set_timer_adj_val(capi_rt_proxy_t *me_ptr)
{

   // TODO: NEed to profile and assign different values for fractional vs integral sampling rates
   me_ptr->driver_hdl.timer_adjust_constant_value = DRIFT_CORRECTION_CONSTANT;

   AR_MSG(DBG_MED_PRIO, "RT_PROXY_DRIVER: timer_adjust_value=%d", (int)me_ptr->driver_hdl.timer_adjust_constant_value);

   return AR_EOK;
}

ar_result_t rt_proxy_calibrate_driver(capi_rt_proxy_t *me_ptr)
{
   ar_result_t        result  = AR_EOK;
   rt_proxy_driver_t *drv_ptr = &me_ptr->driver_hdl;

   // Set buffer fullness thresholds to determine for drift correction.
   // update upper and lower drift threshold.
   // In TX, path if the buffer crosses upper threshold. it means client is running faster hence end point
   // need to run faster, so drift is set on the timer.
   // If it goes below lower threshold, it means end point needs to run slower, timer is corrected accordingly.
   // lower drift threshold is sum of client frame size and
   uint32_t lower_drift_threshold_in_ms = ((me_ptr->cfg.jitter_allowance_in_ms > me_ptr->cfg.client_frame_size_in_ms)
                                              ? me_ptr->cfg.jitter_allowance_in_ms
                                              : me_ptr->cfg.client_frame_size_in_ms);

   // lower_drift_threshold_in_ms += (me_ptr->frame_duration_in_us/1000);

   // convert ms to bytes
   drv_ptr->lower_drift_threshold = capi_cmn_us_to_bytes_per_ch(lower_drift_threshold_in_ms * 1000,
                                                                me_ptr->operating_mf.format.sampling_rate,
                                                                me_ptr->operating_mf.format.bits_per_sample);

   uint32_t circ_buf_size_in_bytes = capi_cmn_us_to_bytes_per_ch(drv_ptr->circ_buf_size_in_us,
                                                                 me_ptr->operating_mf.format.sampling_rate,
                                                                 me_ptr->operating_mf.format.bits_per_sample);

   if (circ_buf_size_in_bytes > drv_ptr->lower_drift_threshold)
   {
      drv_ptr->upper_drift_threshold = circ_buf_size_in_bytes - drv_ptr->lower_drift_threshold;
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: Cannot update drift thresholds ");
      drv_ptr->lower_drift_threshold = 0;
      drv_ptr->upper_drift_threshold = circ_buf_size_in_bytes;
   }

#ifdef DEBUG_RT_PROXY_WATERMARK
   // get watermark event levels in bytes.
   drv_ptr->high_watermark_level = capi_cmn_us_to_bytes_per_ch(me_ptr->cfg.high_watermark_in_ms * 1000,
                                                               me_ptr->operating_mf.format.sampling_rate,
                                                               me_ptr->operating_mf.format.bits_per_sample);

   drv_ptr->low_watermark_level = capi_cmn_us_to_bytes_per_ch(me_ptr->cfg.low_watermark_in_ms * 1000,
                                                              me_ptr->operating_mf.format.sampling_rate,
                                                              me_ptr->operating_mf.format.bits_per_sample);
#endif

   AR_MSG(DBG_HIGH_PRIO,
          "RT_PROXY_DRIVER: lower_drift_threshold = %lu, upper_drift_threshold = %lu, high_watermark_level = %lu, "
          "high_watermark_level = %lu, ",
          drv_ptr->lower_drift_threshold,
          drv_ptr->upper_drift_threshold,
          drv_ptr->high_watermark_level,
          drv_ptr->low_watermark_level);

   result = rt_proxy_port_set_timer_adj_val(me_ptr);

   return result;
}

ar_result_t rt_proxy_driver_get_required_circbuf_size_in_us(capi_rt_proxy_t *me_ptr, uint32_t *size_us_ptr)
{
   uint32_t buf_size_in_ms       = 0;
   uint32_t rtpp_buf_size_factor = 0;

   // get buffer size factor
   if (is_sample_rate_fractional(me_ptr->operating_mf.format.sampling_rate))
   {
#ifdef DEBUG_RT_PROXY_DRIVER
      AR_MSG(DBG_ERROR_PRIO,
             "RT_PROXY_DRIVER: Fractional sample rate usecase. sample_rate = %lu",
             me_ptr->operating_mf.format.sampling_rate);
#endif
      rtpp_buf_size_factor = RT_PROXY_SIZE_FRACT_FACTOR;
   }
   else
   {
      rtpp_buf_size_factor = RT_PROXY_SIZE_INT_FACTOR;
   }

   // get total buffer size required in ms,
   //   1. Ping pong buffer with Client frame size and hw ep frame size.
   //   2. JItter allowance
   //   3. Drift allowance.
   buf_size_in_ms = (rtpp_buf_size_factor * (me_ptr->cfg.client_frame_size_in_ms)) +
                    (2 * (me_ptr->cfg.jitter_allowance_in_ms)) + (2 * RT_PROXY_DRIFT_ALLOWANCE_MS);

   // return size in us.
   *size_us_ptr = (buf_size_in_ms * 1000);

#ifdef DEBUG_RT_PROXY_DRIVER
   AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: Required circular_buf_size_in_us = %lu", *size_us_ptr);
#endif

   return AR_EOK;
}

capi_err_t circular_buffer_event_cb(void *          context_ptr,
                                    spf_circ_buf_t *circ_buf_ptr,
                                    uint32_t        event_id,
                                    void *          event_info_ptr)
{
   capi_rt_proxy_t *me_ptr = (capi_rt_proxy_t *)context_ptr;

   if (event_id == SPF_CIRC_BUF_EVENT_ID_OUTPUT_MEDIA_FORMAT)
   {
      AR_MSG(DBG_HIGH_PRIO, "RT_PROXY_DRIVER: circular buf raised output media format");
      rt_proxy_raise_output_media_format_event(me_ptr, (capi_media_fmt_v2_t *)event_info_ptr);
   }

   return CAPI_EOK;
}

ar_result_t rt_proxy_driver_intialize_circular_buffers(capi_rt_proxy_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   if (!me_ptr)
   {
      return AR_EFAILED;
   }

   // All the input arguments are same for all channel buffers except buffer ID.
   spf_circ_buf_alloc_inp_args_t inp_args;
   memset(&inp_args, 0, sizeof(spf_circ_buf_alloc_inp_args_t));
   inp_args.preferred_chunk_size  = CIRCULAR_BUFFER_PREFERRED_CHUNK_SIZE;
   inp_args.heap_id               = me_ptr->heap_id;
   inp_args.metadata_handler      = &me_ptr->metadata_handler;
   inp_args.cb_info.event_cb      = circular_buffer_event_cb;
   inp_args.cb_info.event_context = me_ptr;

   // Creates circular buffers and returns handle
   if (AR_DID_FAIL(result = spf_circ_buf_init(&me_ptr->driver_hdl.stream_buf, &inp_args)))
   {
#ifdef DEBUG_RT_PROXY_DRIVER
      AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: buffer create failed");
#endif
      return AR_EFAILED;
   }

   // register the writer handle with the driver.
   if (AR_DID_FAIL(result = spf_circ_buf_register_writer_client(me_ptr->driver_hdl.stream_buf,
                                                                me_ptr->driver_hdl.circ_buf_size_in_us, /* buf size*/
                                                                &me_ptr->driver_hdl.writer_handle)))
   {
#ifdef DEBUG_RT_PROXY_DRIVER
      AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: writer registration failed.");
#endif
      return AR_EFAILED;
   }

   // register the reader with the driver handle.
   if (AR_DID_FAIL(result = spf_circ_buf_register_reader_client(me_ptr->driver_hdl.stream_buf,
                                                                0, // not requesting any resize
                                                                &me_ptr->driver_hdl.reader_handle)))
   {
#ifdef DEBUG_RT_PROXY_DRIVER
      AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: Reader registration failed.");
#endif
      return AR_EFAILED;
   }

   return result;
}

void rt_proxy_change_trigger_policy(capi_rt_proxy_t *me_ptr, fwk_extn_port_trigger_policy_t new_policy)
{
   if (NULL == me_ptr->policy_chg_cb.change_data_trigger_policy_cb_fn)
   {
      return;
   }

   // need to listen to either inputs and outputs.
   fwk_extn_port_nontrigger_group_t nontriggerable_ports = { 0 };
   fwk_extn_port_trigger_affinity_t input_group1         = { FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT };
   fwk_extn_port_trigger_affinity_t output_group1        = { FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT };

   fwk_extn_port_trigger_group_t triggerable_groups[1];
   triggerable_groups[0].in_port_grp_affinity_ptr  = &input_group1;
   triggerable_groups[0].out_port_grp_affinity_ptr = &output_group1;

   // By default set the mode to RT, when the write arrives then make it FTRT.
   me_ptr->policy_chg_cb.change_data_trigger_policy_cb_fn(me_ptr->policy_chg_cb.context_ptr,
                                                          &nontriggerable_ports,
                                                          new_policy,
                                                          1 /*num_groups*/,
                                                          triggerable_groups);

   me_ptr->trigger_policy = new_policy;

#ifdef DEBUG_RT_PROXY_DRIVER
   AR_MSG(DBG_HIGH_PRIO, "RT_PROXY_DRIVER: new trigger_policy=%d", new_policy);
#endif

   return;
}

ar_result_t rt_proxy_update_drift_based_on_buffer_fullness(capi_rt_proxy_t *me_ptr)
{
   ar_result_t        result  = AR_EOK;
   rt_proxy_driver_t *drv_ptr = &me_ptr->driver_hdl;

   // get un read bytes from the driver.
   uint32_t unread_bytes = 0;
   spf_circ_buf_get_unread_bytes(drv_ptr->reader_handle, &unread_bytes);

   // Change policy from RT <-> FTRT
   if (FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL != me_ptr->trigger_policy)
   {
      if (me_ptr->is_tx_module)
      {
         uint32_t reader_frame_size = capi_cmn_us_to_bytes_per_ch(me_ptr->frame_duration_in_us,
                                                                  me_ptr->operating_mf.format.sampling_rate,
                                                                  me_ptr->operating_mf.format.bits_per_sample);
         if (unread_bytes >= reader_frame_size)
         {
            rt_proxy_change_trigger_policy(me_ptr, FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL);
         }
      }
      else
      {
         uint32_t reader_frame_size = capi_cmn_us_to_bytes_per_ch(me_ptr->cfg.client_frame_size_in_ms * 1000,
                                                                  me_ptr->operating_mf.format.sampling_rate,
                                                                  me_ptr->operating_mf.format.bits_per_sample);
         if (unread_bytes >= reader_frame_size)
         {
            rt_proxy_change_trigger_policy(me_ptr, FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL);
         }
      }
   }

   int64_t drift_adjust_value = 0;
   if (unread_bytes > drv_ptr->upper_drift_threshold)
   {
      // update the drift value.
      // For TX path, if buffer cross upper threshold, the end point consume faster to catch up with the client
      // Hence drift is negative. Vice versa for the RX path.
      // modulo is for: adjusting drift only at the client frame read/write boundary
      if (me_ptr->is_tx_module)
      {
         drift_adjust_value = -drv_ptr->timer_adjust_constant_value;
      }
      else
      {
         drift_adjust_value = drv_ptr->timer_adjust_constant_value;
      }

#ifdef DEBUG_RT_PROXY_DRIVER_DRIFT_ADJ
      AR_MSG(DBG_LOW_PRIO, "RT_PROXY_DRIVER: Upper drift threshold met. unread_bytes=%lu", unread_bytes);
#endif // DEBUG_RT_PROXY_DRIVER_DRIFT_ADJ
   }
   else if (unread_bytes < drv_ptr->lower_drift_threshold)
   {
      // update the drift value.
      // For TX path, if buffer cross lower threshold, the end point needs to slower to match with the client.
      // Hence drift is positive. Vice versa for the RX path.
      // modulo is for: adjusting drift only at the client frame read/write boundary
      if (me_ptr->is_tx_module)
      {
         drift_adjust_value = drv_ptr->timer_adjust_constant_value;
      }
      else
      {
         drift_adjust_value = -drv_ptr->timer_adjust_constant_value;
      }

#ifdef DEBUG_RT_PROXY_DRIVER_DRIFT_ADJ
      AR_MSG(DBG_LOW_PRIO, "RT_PROXY_DRIVER: Lower drift threshold met. unread_bytes=%lu", unread_bytes);
#endif // DEBUG_RT_PROXY_DRIVER_DRIFT_ADJ
   }

   // If the peer control port is not connected we don't need to update the drift.
   if (me_ptr->ctrl_port_info.state != CTRL_PORT_PEER_CONNECTED)
   {
      return AR_EOK;
   }

   // Update the drift
   result |= rt_proxy_update_accumulated_drift(&me_ptr->drift_info, drift_adjust_value, posal_timer_get_time());
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: Failed updating accumulated drift.");
   }

   return result;
}

/*==============================================================================
   Public Function Implementation
==============================================================================*/
ar_result_t rt_proxy_driver_init(capi_rt_proxy_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   if (NULL == me_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: Init failed. bad input params ");
      return AR_EBADPARAM;
   }

   // if calibration or mf is not set. return not ready.
   if (!me_ptr->is_calib_set)
   {
      AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: Not ready to initalize. ");
      return AR_ENOTREADY;
   }

   // Get circular buffer size.
   uint32_t new_circbuf_size_in_us = 0;
   rt_proxy_driver_get_required_circbuf_size_in_us(me_ptr, &new_circbuf_size_in_us);

   AR_MSG(DBG_HIGH_PRIO, "RT_PROXY_DRIVER: Initializing the driver with buf size %lu", new_circbuf_size_in_us);

   me_ptr->driver_hdl.circ_buf_size_in_us = new_circbuf_size_in_us;

   // Create circular buffers for each channel, initialize read and write handles.
   if (AR_EOK != (result = rt_proxy_driver_intialize_circular_buffers(me_ptr)))
   {
      return result;
   }

   // Compute upped and lower drift thresholds. And water mark levels in bytes based on mf.
   if (AR_EOK != (result = rt_proxy_calibrate_driver(me_ptr)))
   {
      return result;
   }

   return result;
}

ar_result_t rt_proxy_driver_deinit(capi_rt_proxy_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   if (NULL == me_ptr || NULL == me_ptr->driver_hdl.stream_buf)
   {
#ifdef DEBUG_RT_PROXY_DRIVER
      AR_MSG(DBG_ERROR_PRIO, "rt_proxy_driver_deinit: Failed. bad input params.");
#endif
      return AR_EBADPARAM;
   }

   // Destory the driver.
   // Destroys circular buffers and reader/writer registers
   if (AR_DID_FAIL(result = spf_circ_buf_deinit(&me_ptr->driver_hdl.stream_buf)))
   {
      return AR_EFAILED;
   }

   return result;
}

ar_result_t rt_proxy_stream_write(capi_rt_proxy_t *me_ptr, capi_stream_data_t *in_stream)
{
   ar_result_t        result              = AR_EOK;
   uint32_t           FIRST_CH_INDEX      = 0;
   rt_proxy_driver_t *drv_ptr             = &me_ptr->driver_hdl;
   bool_t             ALLOW_OVERFLOW_TRUE = TRUE;

   if (NULL == in_stream->buf_ptr[0].data_ptr)
   {
#ifdef DEBUG_RT_PROXY_DRIVER
      AR_MSG(DBG_MED_PRIO, "RT_PROXY_DRIVER: Input buffer not preset, nothing to write.");
#endif
      return AR_EOK;
   }

   if (!me_ptr->driver_hdl.stream_buf && !me_ptr->is_input_mf_received)
   {
      AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: Write failed. Driver not intialized yet.");
      return AR_EFAILED;
   }

   // For the first frame adjust the buffer fullness.
   if (!me_ptr->first_write_frame_done)
   {
      me_ptr->first_write_frame_done = TRUE;

      uint32_t inital_fullness_in_bytes = 0;

      // wait until first write done to read data in TX path.
      // adjust read pointer to half way in RX path when the client starts reading for first time.
      // Adjust the ideal fullness to half the buffer size.
      uint32_t inital_fullness_in_us = me_ptr->driver_hdl.circ_buf_size_in_us / 2;

      // Allocate two additional buffers for ICB buffers.
      inital_fullness_in_us += 2 * me_ptr->frame_duration_in_us;

      inital_fullness_in_bytes += capi_cmn_us_to_bytes_per_ch(inital_fullness_in_us,
                                                              me_ptr->operating_mf.format.sampling_rate,
                                                              me_ptr->operating_mf.format.bits_per_sample);

      AR_MSG(DBG_HIGH_PRIO,
             "RT_PROXY_DRIVER: Received first write buffer. ch_id=%lu actual_data_len=%lu inital_fullness_in_us=%lu",
             FIRST_CH_INDEX,
             in_stream->buf_ptr[FIRST_CH_INDEX].actual_data_len,
             inital_fullness_in_us);

#ifdef DEBUG_RT_PROXY_DRIVER
      me_ptr->driver_hdl.total_data_written_in_ms = (capi_cmn_bytes_to_us(inital_fullness_in_bytes,
                                                                          me_ptr->operating_mf.format.sampling_rate,
                                                                          me_ptr->operating_mf.format.bits_per_sample,
                                                                          1,
                                                                          NULL)) /
                                                    1000;
#endif

      // Memset to initialize the buffer to its initial fullness.
      if (SPF_CIRCBUF_FAIL ==
          (result = spf_circ_buf_memset(drv_ptr->writer_handle, ALLOW_OVERFLOW_TRUE, inital_fullness_in_bytes, 0)))
      {
         AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: Failed intializing the buffer on first write.");
         return AR_EFAILED;
      }
   }

   // Write one container frame into the circular buffer.
   result = spf_circ_buf_write_one_frame(drv_ptr->writer_handle, in_stream, ALLOW_OVERFLOW_TRUE);
   if (SPF_CIRCBUF_FAIL == result)
   {
      AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: Failed writing the frame to circular buffer %lu", result);
      return AR_EFAILED;
   }
   else if (SPF_CIRCBUF_OVERRUN == result)
   {
      // overflow can happen if there is a momentary long jitter in HLOS when sending rd buffer
      // in that case glitch is observed and module dosent need to return EFAILED, since its not
      // a fatal error.
      AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: circular buffer overrun occured. %lu", result);
      result = AR_EOK;
   }

#ifdef DEBUG_RT_PROXY_DRIVER

   uint32_t unread_bytes = 0;
   spf_circ_buf_get_unread_bytes(drv_ptr->reader_handle, &unread_bytes);
   AR_MSG(DBG_HIGH_PRIO,
          "RT_PROXY_DRIVER: Stream writer done. actual_data_len = %d, circ buf filled size=%lu",
          in_stream->buf_ptr[0].actual_data_len,
          unread_bytes);

   AR_MSG(DBG_HIGH_PRIO,
          "RT_PROXY_DRIVER: Write done. total_data_written_in_ms = %lu, total_data_read_in_ms = %lu diff = %d",
          ++drv_ptr->total_data_written_in_ms,
          drv_ptr->total_data_read_in_ms,
          drv_ptr->total_data_written_in_ms - drv_ptr->total_data_read_in_ms);
#endif

#ifdef DEBUG_RT_PROXY_WATERMARK
   if (drv_ptr->ch_info_arr[0]->read_handle.unread_bytes >= drv_ptr->high_watermark_level)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "RT_PROXY_DRIVER: High water mark threshold met. unread_bytes=%lu, high_watermark_level=%lu",
             drv_ptr->ch_info_arr[0]->read_handle.unread_bytes,
             drv_ptr->high_watermark_level);

      // TODO: raise high water mark event.
   }
#endif

   // Apply drift correction if buffer size crosses upper/lower thresholds.
   rt_proxy_update_drift_based_on_buffer_fullness(me_ptr);

   return result;
}

ar_result_t rt_proxy_stream_read(capi_rt_proxy_t *me_ptr, capi_stream_data_t *out_stream)
{
   ar_result_t result = AR_EOK;

   // if output buffer is not present, no need to generate output
   if (NULL == out_stream->buf_ptr[0].data_ptr)
   {
#ifdef DEBUG_RT_PROXY_DRIVER
      AR_MSG(DBG_MED_PRIO, "RT_PROXY_DRIVER: Output buffer not preset, nothing to read.");
#endif
      return AR_EOK;
   }

   if (NULL == me_ptr->driver_hdl.stream_buf && !me_ptr->is_input_mf_received)
   {
      AR_MSG(DBG_ERROR_PRIO, "RT_PROXY_DRIVER: Read failed. Driver not intialized yet.");
      return AR_EFAILED;
   }

   rt_proxy_driver_t *drv_ptr = &me_ptr->driver_hdl;

   // underrun until the first write buffer arrives.
   if (!me_ptr->first_write_frame_done)
   {
#ifdef DEBUG_RT_PROXY_DRIVER
      AR_MSG(DBG_HIGH_PRIO, "RT_PROXY_DRIVER: Stream reader underrun.");
#endif
      return AR_ENEEDMORE;
   }

   // Read one container frame from the circular buffer.
   result = spf_circ_buf_read_one_frame(drv_ptr->reader_handle, out_stream);
   if (SPF_CIRCBUF_UNDERRUN == result)
   {
      return AR_ENEEDMORE;
   }
   else if (SPF_CIRCBUF_FAIL == result)
   {
      AR_MSG(DBG_HIGH_PRIO, "RT_PROXY_DRIVER: Failed reading data from the buffer.");
      return AR_EFAILED;
   }

#ifdef DEBUG_RT_PROXY_DRIVER
   uint32_t unread_bytes = 0;
   spf_circ_buf_get_unread_bytes(drv_ptr->reader_handle, &unread_bytes);
   AR_MSG(DBG_HIGH_PRIO,
          "RT_PROXY_DRIVER: Stream reader done. actual_data_len = %d, circ buf filled size=%lu",
          out_stream->buf_ptr[0].actual_data_len,
          unread_bytes);

   AR_MSG(DBG_HIGH_PRIO,
          "RT_PROXY_DRIVER: Read done. total_data_written_in_ms = %lu, total_data_read_in_ms = %lu diff = %d",
          drv_ptr->total_data_written_in_ms,
          ++drv_ptr->total_data_read_in_ms,
          drv_ptr->total_data_written_in_ms - drv_ptr->total_data_read_in_ms);
#endif

   // Apply drift correction if buffer size crosses upper/lower thresholds.
   rt_proxy_update_drift_based_on_buffer_fullness(me_ptr);

   return result;
}