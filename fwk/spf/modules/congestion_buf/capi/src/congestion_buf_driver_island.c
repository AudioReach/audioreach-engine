/**
 *   \file congestion_buf_driver.c
 *   \brief
 *        This file contains implementation of Congestion Buffer driver
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "congestion_buf_driver.h"
#include "spf_circular_buffer.h"
#include "ar_msg.h"
#include "spf_list_utils.h"
#include "capi_congestion_buf_i.h"

// Use to override AR_MSG with AR_MSG_ISLAND. Always include this after ar_msg.h
#ifdef AR_MSG_IN_ISLAND
#include "ar_msg_island_override.h"
#endif

#ifdef DEBUG_CIRC_BUF_UTILS
#define DEBUG_CONGESTION_BUF_DRIVER
#endif

/*==============================================================================
   Local Declarations
==============================================================================*/

/*==============================================================================
   Local Function Implementation
==============================================================================*/

/*==============================================================================
   Public Function Implementation
==============================================================================*/

/* Write input data into the congestion buffer*/
ar_result_t congestion_buf_stream_write(capi_congestion_buf_t *me_ptr, capi_stream_data_t *in_stream)
{
   ar_result_t result              = AR_EOK;
   bool_t      ALLOW_OVERFLOW_TRUE = TRUE;

   congestion_buf_driver_t *drv_ptr = &me_ptr->driver_hdl;

   if (NULL == in_stream->buf_ptr[0].data_ptr)
   {
#ifdef DEBUG_CONGESTION_BUF_DRIVER
      AR_MSG(DBG_MED_PRIO, "CONGESTION_BUF_DRIVER: Input buffer not preset, nothing to write.");
#endif
      return AR_EOK;
   }

   if (!me_ptr->driver_hdl.stream_buf)
   {
      AR_MSG(DBG_ERROR_PRIO, "CONGESTION_BUF_DRIVER: Write failed. Driver not intialized yet.");
      return AR_EFAILED;
   }

   /* If amount of data is more than MTU amount of size then
    * mark all data as consumed and return error */
   if (in_stream->buf_ptr->actual_data_len > me_ptr->cfg_ptr.mtu_size)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "congestion_buf: Encoded data more than negotiated MTU %u - dropping %u",
             me_ptr->cfg_ptr.mtu_size,
             in_stream->buf_ptr->actual_data_len);

      /* TODO: Pending - Multiple buffers */
      in_stream->buf_ptr->actual_data_len = 0;

      return AR_EFAILED;
   }

   /* We do not write empty frames into congestion buffer. */
   if (0 == in_stream->buf_ptr->actual_data_len)
   {
      return AR_EOK;
   }

   /* Write one container frame into the circular buffer. */
   if (AR_DID_FAIL(result = spf_circ_buf_raw_write_one_frame(drv_ptr->writer_handle, in_stream, ALLOW_OVERFLOW_TRUE)))
   {
      AR_MSG(DBG_ERROR_PRIO, "CONGESTION_BUF_DRIVER: Failed writing the frame to circular buffer");
      return AR_EFAILED;
   }

#ifdef DEBUG_CONGESTION_BUF_DRIVER
   uint32_t unread_bytes = 0, unread_bytes_max = 0;
   spf_circ_buf_raw_get_unread_bytes(drv_ptr->reader_handle, &unread_bytes, &unread_bytes_max);
   AR_MSG(DBG_HIGH_PRIO,
          "CONGESTION_BUF_DRIVER: Stream writer done. actual_data_len = %d, circ buf filled size=%lu",
          in_stream->buf_ptr[0].actual_data_len,
          unread_bytes);

   AR_MSG(DBG_HIGH_PRIO,
          "CONGESTION_BUF_DRIVER: Write done. total_data_written_in_ms = %lu, total_data_read_in_ms = %lu diff = %d",
          ++drv_ptr->total_data_written_in_ms,
          drv_ptr->total_data_read_in_ms,
          drv_ptr->total_data_written_in_ms - drv_ptr->total_data_read_in_ms);
#endif

   return result;
}

/* Read one frame out of the congestion buffer */
ar_result_t congestion_buf_stream_read(capi_congestion_buf_t *me_ptr, capi_stream_data_t *out_stream)
{

   ar_result_t result = AR_EOK;

   if (NULL == out_stream->buf_ptr[0].data_ptr)
   {
#ifdef DEBUG_CONGESTION_BUF_DRIVER
      AR_MSG(DBG_MED_PRIO, "CONGESTION_BUF_DRIVER: Output buffer not preset, nothing to read.");
#endif
      return AR_EOK;
   }

   if (NULL == me_ptr->driver_hdl.stream_buf && !me_ptr->raw_data_len)
   {
      AR_MSG(DBG_ERROR_PRIO, "CONGESTION_BUF_DRIVER: Read failed. Driver not intialized yet.");
      return AR_EFAILED;
   }

   congestion_buf_driver_t *drv_ptr = &me_ptr->driver_hdl;

   /* If reader is empty then no need to read data */
   bool_t is_empty = FALSE;
   result          = spf_circ_buf_raw_driver_is_buffer_empty(drv_ptr->reader_handle, &is_empty);
   if (is_empty)
   {
      return AR_EOK;
   }

   /* Read one container frame from the circular buffer. */
   result = spf_circ_buf_raw_read_one_frame(drv_ptr->reader_handle, out_stream);
   if (SPF_CIRCBUF_UNDERRUN == result)
   {
      return AR_ENEEDMORE;
   }
   else if (SPF_CIRCBUF_FAIL == result)
   {
      AR_MSG(DBG_HIGH_PRIO, "CONGESTION_BUF_DRIVER: Failed reading data from the buffer.");
      return AR_EFAILED;
   }

#ifdef DEBUG_CONGESTION_BUF_DRIVER
   uint32_t unread_bytes = 0, unread_bytes_max = 0;
   spf_circ_buf_raw_get_unread_bytes(drv_ptr->reader_handle, &unread_bytes, &unread_bytes_max);
   AR_MSG(DBG_HIGH_PRIO,
          "CONGESTION_BUF_DRIVER: Stream reader done. actual_data_len = %d, circ buf filled size=%lu",
          out_stream->buf_ptr[0].actual_data_len,
          unread_bytes);

   AR_MSG(DBG_HIGH_PRIO,
          "CONGESTION_BUF_DRIVER: Read done. total_data_written_bytes = %lu, total_data_read_bytes = %lu diff = %d",
          drv_ptr->total_data_written_bytes,
          ++drv_ptr->total_data_read_bytes,
          drv_ptr->total_data_written_bytes - drv_ptr->total_data_read_bytes);
#endif

   return result;
}
