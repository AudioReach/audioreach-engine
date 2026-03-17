#ifndef _AUDION_DAM_DRIVER_LIB_H
#define _AUDION_DAM_DRIVER_LIB_H

/**
 *   \file audio_dam_driver.h
 *   \brief
 *        This file contains utility functions for handling circular buffering
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_defs.h"
#include "posal.h"
#include "capi_types.h"
#include "spf_list_utils.h"
#include "circular_buffer.h"
#include "imcl_dam_detection_api.h"

#define CAPI_AUDIO_DAM_MAX_READ_CLIENTS 8
#define CAPI_AUDIO_DAM_MAX_INPUT_CHANNELS 32

#define FEAT_RAW_PROCESSED_DATA_SYNC
#define INT_MAX_64 0x7FFFFFFFFFFFFFFF

// #define DEBUG_AUDIO_DAM_DRIVER

/* Detection module debug message */
#define DAM_MSG_PREFIX "[0x%X]: "
#define DAM_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, DAM_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)
#define DAM_MSG_ISLAND(ID, xx_ss_mask, xx_fmt, ...) AR_MSG_ISLAND(xx_ss_mask, DAM_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

/*==============================================================================
   Function declarations
==============================================================================*/

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

typedef struct audio_dam_driver_t audio_dam_driver_t;

struct audio_dam_driver_t
{
   uint32_t iid;
   /*Driver instance ID */

   uint32_t num_writers;
   /* Numbers of writers created */

   spf_list_node_t *stream_writer_list;
   /* List of all the stream writers. */

   uint32_t num_readers;
   /* Numbers of readers created */

   spf_list_node_t *stream_reader_list;
   /* List of all the stream writers. */

   POSAL_HEAP_ID heap_id;
   /* Heap ID for memory allocations, passed at init.*/

   uint32_t preferred_chunk_size;
   /* Preferred chunk size. */

   bool_t is_raw_compressed;
   /* Is buffering raw frames */

   int8_t *ch_frame_scratch_buf_ptr;
   /* Scratch frame used allocated and used only for raw compressed. For
      raw compressed data, read/writes are done using hte scratch frame. */

   uint32_t frame_max_data_len_in_us;
   /* Valid only for raw compressed. PCM frame duration of each encoder frame. */

   uint32_t frame_max_data_len_in_bytes;
   /* Valid only for raw compressed. Max encoded raw compressed bytes per frame.*/

   uint32_t bytes_per_one_ms;
   /* Valid only for fixed point data.*/

   uint32_t sampling_rate;
   /* Valid only for fixed point data.*/

   uint32_t bytes_per_sample;
   /* Valid only for fixed point data.*/
};

typedef struct audio_dam_stream_reader_virtual_buf_info_t
{
   param_id_audio_dam_imcl_virtual_writer_info_t *cfg_ptr;
   /** Virtual writer config pointer */

   int8_t *read_ptr;
   /** Virtual writer config pointer */

   bool_t is_reader_ts_valid;
   /** Indicates is the reader timestamp is valid or not*/

   int64_t reader_ts;
   /** TS of the sample assocaited with the current read pointer */

   // array of size num channels in virtual buffer
   capi_buf_t *out_scratch_bufs;
} audio_dam_stream_reader_virtual_buf_info_t;

typedef struct audio_dam_stream_reader_t
{
   uint16_t id;
   /* Identifier for the stream reader.*/

   uint16_t num_channels;
   /* Number of channels in the output reader stream*/

   uint32_t req_base_bufffer_size;
   /* This the additional base size requested by the reader client at the time of reader create.*/

   uint32_t req_buf_resize;
   /* This is the latest resize request by the reader client. [Usually a detection engine]*/

   uint32_t rel_sync_offset_in_bytes;
   /* Relative sync offset in bytes, used to synchronize the processed and raw channels data */

   circ_buf_client_t **rd_client_ptr_arr;
   /* Array of client handles for the channel's circular buffers. Arrays size is num_channels.*/

   audio_dam_driver_t *driver_ptr;
   /* Ptr to the driver structure */

   /* Storing batching information */
   bool_t is_batch_streaming;
   /* Indicates if audio driver is operating in batching mode */

   uint32_t data_batching_us;
   /* Batching interval in micro seconds */

   uint32_t pending_batch_bytes;
   /** Pending bytes in the ongoing batch read*/

   audio_dam_stream_reader_virtual_buf_info_t *virt_buf_ptr;

} audio_dam_stream_reader_t;

typedef struct audio_dam_stream_writer_t
{
   uint16_t id;
   /* Identifier for the stream writer.*/

   uint16_t num_channels;
   /* Number of channels in the input writer stream*/

   uint32_t base_buffer_size;
   /*Base buffer size as requested by the writer client at the time of buffer creation.*/

   circ_buf_t *circ_buf_arr_ptr;
   /* Array of handles to the channel circular buffers. Arrays size is num_channels.*/

   circ_buf_client_t *wr_client_arr_ptr;
   /* Array of circular buffer clients handles . Arrays size is num_channels.*/

   audio_dam_driver_t *driver_ptr;
   /* Ptr to the driver structure */

} audio_dam_stream_writer_t;

/*** Compressed frame realted info ***/

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct audio_dam_raw_comp_frame_header_t
{
   uint16_t magic_word;
   /* Magic word used for debugging purpose to check if the read frame is valid.
         - AUDIO_DAM_RAW_COMPRESSED_FRAME_MAGIC_WORD */

   uint16_t actual_data_len;
   /* Actual length of the compressed frame. */

   int8_t data[0];
   /* Pointert to begining of the payload */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct audio_dam_raw_comp_frame_header_t audio_dam_raw_comp_frame_header_t;

#define AUDIO_DAM_RAW_COMPRESSED_FRAME_MAGIC_WORD (0xA178)
#define AUDIO_DAM_GET_TOTAL_FRAME_LEN(frame_max_data_len_in_bytes)                                                     \
   (sizeof(audio_dam_raw_comp_frame_header_t) + frame_max_data_len_in_bytes)

typedef struct audio_dam_init_args_t
{
   POSAL_HEAP_ID                                  heap_id;
   uint32_t                                       iid;
   uint32_t                                       preferred_chunk_size;
} audio_dam_init_args_t;

/*
 * Initializes the Audio Dam driver.
 *
 * heap_id[in]                : audio_dam_init_args_t
 * drv_ptr[in/out]            : Pointer to the driver pointer. Handle is created and returned.
 *
 * functionality : Initializes the handles for the driver. Driver handles is
 * needed for subsequent stream writer/reader creation.
 * return : ar_result_t
 */
ar_result_t audio_dam_driver_init(audio_dam_init_args_t *init_args_ptr, audio_dam_driver_t *drv_ptr);

/*
 * De initializes the Audio Dam driver. If there are any stream/writers present
 * they will be destroyed.
 *
 * audio_dam_driver_t[in/out]   : Destroys handles and update the pointer to NULL.
 *
 * return : ar_result_t
 */
ar_result_t audio_dam_driver_deinit(audio_dam_driver_t *drv_ptr);

/*
 * Creates circular buffers for each channel in the input port stream.
 *
 * circ_buf_struct_ptr[in/out]  : pointer to a circular buffer structure
 * circ_buf_size[in]            : Total circular buffer size in bytes
 *
 * functionality : Allocates non-contiguous buffer and initializes the circular
 * buffer parameters for each channel of the input stream. The memory chunks are
 * multiple memory allocations, each smaller than the circular buffer size.
 * The sum of the memory chunk sizes equals the circular buffer size.
 *
 * return : circbuf_result
 */
ar_result_t audio_dam_stream_writer_create(audio_dam_driver_t *        drv_ptr,
                                           uint32_t                    base_buffer_size,
                                           uint32_t                    num_channels,
                                           uint32_t *                  ch_id_arr,
                                           audio_dam_stream_writer_t **writer_handle_pptr);

/*
 * Creates circular buffers for each channel in the input port stream.
 * when the media format is received.
 *
 * circ_buf_struct_ptr[in/out]  : pointer to a circular buffer structure
 * circ_buf_size[in]            : Total circular buffer size in bytes
 *
 * functionality : Allocates non-contiguous buffer and initializes the circular
 * buffer parameters for each channels. The memory chunks are multiple memory allocations, each
 * smaller than the circular buffer size. The sum of the memory chunk sizes
 * equals the circular buffer size.
 * return : circbuf_result
 */
ar_result_t audio_dam_stream_writer_destroy(audio_dam_driver_t *drv_ptr, audio_dam_stream_writer_t **writer_handle);

/**
 * Creates stream reader with the list of channels provided. If its a virtual writer mode, it initializes the reader
 * and caches the configuration pointer. Note that client is expected to allocate the cfg ptr, and make sure its valid
 * until reader destroy is called.
 *
 * drv_ptr[in]                  : Driver handle
 * peer_heap_id[in]             : pointer to a circular buffer structure
 * downstream_setup_duration_in_us[in]            : Total circular buffer size in micro seconds
 * num_channels[in]             : Number of channels mapped to the reader.
 * ch_id_arr[in]                : List of channel IDs mapped to the reader, size of array == num_channels
 * virt_wr_cfg_ptr[in]          : Virtual writer config ptr, if the reader is associated with a virtual writer
 * reader_handle_pptr[in/out]   : Handle pointer is intialized and returned to the client.
 */
ar_result_t audio_dam_stream_reader_create(audio_dam_driver_t *drv_ptr,
                                           POSAL_HEAP_ID       peer_heap_id,
                                           uint32_t            downstream_setup_duration_in_us,
                                           uint32_t            num_channels,
                                           uint32_t *          ch_id_arr,
                                           param_id_audio_dam_imcl_virtual_writer_info_t *virt_wr_cfg_ptr,
                                           audio_dam_stream_reader_t **                   reader_handle_pptr);

/*
 * Creates circular buffers for each channel in the input port stream.
 * when the media format is received.
 *
 * circ_buf_struct_ptr[in/out]  : pointer to a circular buffer structure
 * circ_buf_size[in]            : Total circular buffer size in bytes
 *
 * functionality : Allocates non-contiguous buffer and initializes the circular
 * buffer parameters for each channels. The memory chunks are multiple memory allocations, each
 * smaller than the circular buffer size. The sum of the memory chunk sizes
 * equals the circular buffer size.
 * return : circbuf_result
 */
ar_result_t audio_dam_stream_reader_destroy(audio_dam_driver_t *drv_ptr, audio_dam_stream_reader_t **reader_handle);

/*
 * Initializes the Circular buffer
 * circ_buf_struct_ptr[in/out]  : pointer to a circular buffer structure
 * circ_buf_size[in]            : Total circular buffer size in bytes
 * preferred_chunk_size[in]     : Preferred size of each memory chunk in bytes
 * heap_id[in]                  : heap_id for malloc
 * functionality : Allocates non-contiguous buffer and initializes the circular
 * buffer parameters. The memory chunks are multiple memory allocations, each
 * smaller than the circular buffer size. The sum of the memory chunk sizes
 * equals the circular buffer size.
 * return : circbuf_result
 */
ar_result_t audio_dam_stream_write(audio_dam_stream_writer_t *writer_handle,
                                   uint32_t                   input_bufs_num,
                                   capi_buf_t *               input_buf_arr,
                                   uint32_t                   is_valid_timestamp,
                                   int64_t                    timestamp);

/*
 * Read samples from the Circular buffer
 * circ_buf_struct_ptr[in/out]  : pointer to a circular buffer structure
 * out_ptr[out]                 : Pointer to where data has to be read from
 *                                circular buffer
 * num_chs_to_read[in]           : number of chs to be read
 * output_buf_ts_is_valid[out]   : If output buffer TS is valid.
 * output_buf_ts[out]            : Start TS of the currently read data.
 * output_buf_len_in_us[out]     : Currently read actual data len converted to PCM duration in us.
 * functionality : copies the frame from circular buffers to Capi output buffer
 * return : circbuf_result
 */

ar_result_t audio_dam_stream_read(audio_dam_stream_reader_t *reader_handle,
                                  uint32_t                   num_chs_to_read,
                                  capi_buf_t *               output_buf_arr,
                                  bool_t *                   output_buf_ts_is_valid,
                                  int64_t *                  output_buf_ts,
                                  uint32_t *                 output_buf_len_in_us);

/*
 * Adjusts the read pointer
 * circ_buf_struct_ptr[in/out]  : pointer to a circular buffer structure
 * read_offset[in]              : offset needed between new read_index and
 *                                current write_index
 * bytes_per_one_ms[in]        : based on input MF.
 * read_offset[in]             : actual offset done by circular buffer layer.
 * force_adjust[in]            : go back by read offset, even though enough data is not buffered.
 * functionality : Moves the read_index to a new position to start reading from
 * write_index - read_offset
 * return : circbuf_result
 */
ar_result_t audio_dam_stream_read_adjust(audio_dam_stream_reader_t *reader_handle,
                                         uint32_t                   requested_read_offset_in_us,
                                         uint32_t *                 actual_read_offset_in_us,
                                         bool_t                     force_adjust);

/*
 * Adjusts the read pointer posistion of all the channels
 * capi_audio_dam_buffer_t[in/out]  : pointer to a circular buffer structure
 * audio_dam_out_port_t[in]            : output port handle.
 * functionality : Moves the read_index to a new position to start reading from
 * write_index - read_offset
 * return : circbuf_result
 */

ar_result_t audio_dam_stream_reader_req_resize(audio_dam_stream_reader_t *reader_handle,
                                               uint32_t                   requested_resize_in_us,
                                               POSAL_HEAP_ID              heap_id);

/*
 * Rearranges the channel output order based on the list of ch IDS given.
 * When the channels are read from stream reader, they are read in this order.
 *
 * circ_buf_struct_ptr[in/out]  : pointer to a circular buffer structure
 * num_chs_to_output[in]        : Number of channels to sort.
 * ch_ids_to_output[in]         : List of channel IDs to sort.
 *
 * return : ar_result_t
 */
ar_result_t audio_dam_stream_reader_ch_order_sort(audio_dam_stream_reader_t *reader_handle,
                                                  uint32_t                   num_chs_to_output,
                                                  uint32_t *                 ch_ids_to_output);

/* Sets media format of the compressed raw data being buffered. */
ar_result_t audio_dam_set_raw_compressed_mf(audio_dam_driver_t *drv_ptr,
                                            uint32_t            frame_max_data_len_in_us,
                                            uint32_t            frame_max_data_len_in_bytes);

/* Sets media format of the fixed point PCM data being buffered. */
ar_result_t audio_dam_set_pcm_mf(audio_dam_driver_t *drv_ptr, uint32_t sampling_rate, uint32_t bytes_per_sample);

/*
 * Adjusts the read pointer posistion of all the channels
 * capi_audio_dam_buffer_t[in/out]  : pointer to a circular buffer structure
 * audio_dam_out_port_t[in]            : output port handle.
 * functionality : Moves the read_index to a new position to start reading from
 * write_index - read_offset
 * return : circbuf_result
 */
ar_result_t audio_dam_stream_reader_enable_batching_mode(audio_dam_stream_reader_t *reader_handle,
                                                         bool_t                     is_batch_streaming,
                                                         uint32_t                   data_batching_us);

/* Sets media format of the fixed point PCM data being buffered. */
ar_result_t audio_dam_get_stream_reader_unread_bytes(audio_dam_stream_reader_t *reader_handle, uint32_t *unread_bytes);


static inline bool_t audio_dam_driver_is_virtual_writer_mode(audio_dam_stream_reader_t *reader_handle)
{
   return reader_handle->virt_buf_ptr ? TRUE : FALSE;
}

ar_result_t audio_dam_get_stream_reader_pending_bytes(audio_dam_stream_reader_t *reader_handle, uint32_t *unread_bytes);
ar_result_t audio_dam_force_set_pending_bytes(audio_dam_stream_reader_t *reader_handle);

/* check if the batching requirement will be met in the current process, if yes trigger Island exit if duty_cycling is enabled */
bool_t audio_dam_if_batching_req_met(audio_dam_stream_reader_t *reader_handle);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /*_AUDION_DAM_DRIVER_LIB_H*/
