#ifndef __SPF_CIRC_BUF_UTILS_I_H
#define __SPF_CIRC_BUF_UTILS_I_H

/**
 * \file spf_circular_buffer_i.h
 * \brief
 *    This file contains internal utility functions for handling fragmented circular
 *    buffer operation.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_circular_buffer.h"
#include "ar_defs.h"
#include "posal.h"
#include "spf_list_utils.h"
#include "capi_cmn.h"

// Use to override AR_MSG with AR_MSG_ISLAND. Always include this after ar_msg.h
#ifdef AR_MSG_IN_ISLAND
#include "ar_msg_island_override.h"
#endif

/*==============================================================================
   Macros
==============================================================================*/

//#define DEBUG_CIRC_BUF_UTILS
//#define DEBUG_CIRC_BUF_UTILS_VERBOSE
//#define DEBUG_CIRC_BUF_UTILS_METADATA

#define DEFAULT_CH_IDX 0

#define ALIGN_8_BYTES(a) (((a) & (~(uint32_t)0x7)) + 8)

#define GET_CHUNK_END_ADDRESS(chunk_ptr) ((int8_t *)chunk_ptr->buf_ptr[0] + chunk_ptr->size)

#define GET_CHUNK_FRAME_SIZE(frame_data_buf_size) (ALIGN_8_BYTES(frame_data_buf_size + sizeof(spf_circ_buf_frame_t)))

// Returns address of the possible next frame, the frame may not be within in the currenty chunk.
// Need to validate further if the frame is within the chunk.
#define GET_NEXT_FRAME_ADDRESS(chunk_ptr, cur_frame_ptr)                                                               \
   ((int8_t *)cur_frame_ptr + GET_CHUNK_FRAME_SIZE(chunk_ptr->frame_size))

#define CIRC_BUF_TS_TOLERANCE 1000

#define NUM_CH_PER_CIRC_BUF 1

#define _CEIL(x, y) (((x) + (y)-1) / (y))

/*==============================================================================
   Structure definitions
==============================================================================*/

/*==============================================================================
   Inline Functions
==============================================================================*/

static inline bool_t is_a_valid_frame_address(spf_circ_buf_t *      circ_buf_ptr,
                                              spf_circ_buf_chunk_t *chunk_ptr,
                                              spf_circ_buf_frame_t *frame_ptr)
{
   int8_t *chunk_end_addr = GET_CHUNK_END_ADDRESS(chunk_ptr);
   int8_t *frame_end_addr = (int8_t *)frame_ptr + GET_CHUNK_FRAME_SIZE(chunk_ptr->frame_size);

   // TODO: do we need to check if the base address is an indexed to a existing frame.
   if (frame_end_addr <= chunk_end_addr)
   {
      return TRUE;
   }

   return FALSE;
}

static inline spf_circ_buf_frame_t *get_frame_ptr(spf_circ_buf_position_t *pos_ptr, uint32_t ch_idx)
{
   spf_circ_buf_chunk_t *temp_chunk_ptr = (spf_circ_buf_chunk_t *)pos_ptr->chunk_node_ptr->obj_ptr;

   return (spf_circ_buf_frame_t *)&temp_chunk_ptr->buf_ptr[ch_idx][pos_ptr->frame_position];
}

/*==============================================================================
   Function declarations
==============================================================================*/

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

static inline void incr_mf_ref_count(spf_circ_buf_t *circ_buf_ptr, spf_circ_buf_mf_info_t *mf_ptr)
{
   if (mf_ptr)
   {
      mf_ptr->ref_count++;
   }
}

static inline void decr_mf_ref_count(spf_circ_buf_t *circ_buf_ptr, spf_circ_buf_mf_info_t **mf_ptr)
{
   if ((*mf_ptr)->ref_count > 0)
   {
      (*mf_ptr)->ref_count--;
   }

   // free the mf ptr from the list.
   if (0 == (*mf_ptr)->ref_count)
   {
      spf_list_find_delete_node(&circ_buf_ptr->mf_list_ptr, mf_ptr, TRUE);
   }
   *mf_ptr = NULL;
}

static inline uint32_t convert_us_to_bytes(uint32_t us, spf_circ_buf_mf_info_t *operating_mf)
{
   uint32_t bytes = capi_cmn_us_to_bytes(us,
                                         operating_mf->sampling_rate,
                                         operating_mf->bits_per_sample,
                                         NUM_CH_PER_CIRC_BUF /* Num channels per circular buffer */);
   return bytes;
}

static inline uint32_t convert_bytes_to_us(uint32_t bytes, spf_circ_buf_mf_info_t *operating_mf)
{
   uint32_t us = capi_cmn_bytes_to_us(bytes,
                                      operating_mf->sampling_rate,
                                      operating_mf->bits_per_sample,
                                      NUM_CH_PER_CIRC_BUF /* Num channels per circular buffer */,
                                      NULL);
   return us;
}

spf_circ_buf_result_t _circ_buf_read_advance(spf_circ_buf_client_t *rd_client_ptr, uint32_t bytes_to_advance);

spf_circ_buf_result_t _circ_buf_advance_to_next_frame(spf_circ_buf_t *circ_buf_ptr, spf_circ_buf_position_t *pos);

spf_circ_buf_result_t _move_metadata_from_input_sdata_to_cur_wr_frame(spf_circ_buf_client_t *wr_client_ptr,
                                                                      capi_buf_t *           in_buf_ptr,
                                                                      capi_stream_data_t *   in_sdata_ptr,
                                                                      uint32_t len_consumed_per_ch_in_bytes,
                                                                      uint32_t in_initial_len_per_ch_in_bytes);

spf_circ_buf_result_t _circ_buf_clone_sdata_from_cur_frame_to_reader_hdl(spf_circ_buf_frame_t * cur_rd_frame_ptr,
                                                                         spf_circ_buf_client_t *rd_client_ptr,
                                                                         bool_t                 is_output_sdata_v2);
spf_circ_buf_result_t _circ_buf_clone_or_move_metadata_list(spf_circ_buf_t *       circ_buf_ptr,
                                                            module_cmn_md_list_t **src_list_pptr,
                                                            module_cmn_md_list_t **dst_list_pptr,
                                                            bool_t                 is_clone);
bool_t                _circ_buf_can_read_data_further(spf_circ_buf_client_t *rd_client_ptr,
                                                      capi_stream_data_t *   out_sdata_ptr,
                                                      uint32_t               initial_bytes_in_output);

spf_circ_buf_result_t _circular_buffer_propagate_metadata_to_output_stream(spf_circ_buf_client_t *rd_client_ptr,
                                                                           capi_buf_t *           out_buf_ptr,
                                                                           capi_stream_data_t *   out_sdata_ptr,
                                                                           uint32_t initial_read_frame_offset,
                                                                           uint32_t initial_bytes_available_to_read,
                                                                           uint32_t len_consumed_from_frame,
                                                                           uint32_t initial_bytes_in_output);

spf_circ_buf_result_t _circ_buf_write_one_frame(spf_circ_buf_client_t *wr_client_ptr,
                                                capi_stream_data_t *   sdata_ptr,
                                                bool_t                 allow_overflow,
                                                uint32_t *             memset_value);

spf_circ_buf_result_t _circ_buf_read_one_frame(spf_circ_buf_client_t *rd_client_ptr, capi_stream_data_t *out_sdata_ptr);

spf_circ_buf_result_t _circ_buf_write_data(spf_circ_buf_client_t *wr_handle,
                                           capi_stream_data_t *   in_sdata_ptr,
                                           bool_t                 allow_overflow,
                                           uint32_t *             memset_value_ptr,
                                           uint32_t *             num_memset_bytes);

spf_circ_buf_result_t _circ_buf_detect_and_handle_overflow(spf_circ_buf_client_t *wr_client_ptr, bool_t allow_overflow);

// Function to allocate a chunk and returns chunk pointer.
// allocate chunk function
spf_circ_buf_chunk_t *_circ_buf_allocate_chunk(spf_circ_buf_t *        circ_buf_ptr,
                                               uint32_t                frame_data_size_in_bytes,
                                               uint32_t                chunk_size,
                                               spf_circ_buf_mf_info_t *mf_ptr);

// Frees all the metadata in the chunk, can be used during buffer reset or before freeing the chunk.
spf_circ_buf_result_t _circ_buf_free_chunk_metadata(spf_circ_buf_t *circ_buf_ptr, spf_circ_buf_chunk_t *chunk_ptr);

spf_circ_buf_result_t _circ_buf_free_frame_metadata(spf_circ_buf_t *       circ_buf_ptr,
                                                    spf_circ_buf_frame_t * frame_ptr,
                                                    bool_t                 force_free,
                                                    module_cmn_md_list_t **stream_associated_md_list_pptr);

/* Frees metadata from the given frame of a chunk. */
spf_circ_buf_result_t _circ_buf_free_sdata(spf_circ_buf_t *       circ_buf_ptr,
                                           capi_stream_data_v2_t *sdata_ptr,
                                           bool_t                 force_free,
                                           module_cmn_md_list_t **stream_associated_md_list_pptr);

bool_t _circ_buf_check_ts_disc(spf_circ_buf_client_t *rd_client_ptr,
                               capi_stream_data_v2_t *rd_frame_sdata_ptr,
                               uint32_t               initial_read_frame_offset,
                               capi_stream_data_t *   out_sdata_ptr,
                               uint32_t               initial_bytes_in_output);

// Function to allocate a chunk and returns chunk pointer.
spf_circ_buf_result_t _circ_buf_free_chunk(spf_circ_buf_t *circ_buf_ptr, spf_circ_buf_chunk_t *chunk_ptr);

void _circ_buf_destroy_client_hdl(spf_circ_buf_t *circ_buf_ptr, spf_circ_buf_client_t **client_pptr);

// Expands the size of the circular buffer by additional size.
// The circular buffer size will increased by adding new frames, additional_frames is computed by,
// additional_frames = ceil(additional_size, container frame size)
// Depending on additional frames required new chunks are added to the circular buffer.
spf_circ_buf_result_t _circ_buf_expand_buffer(spf_circ_buf_t *circ_buf_ptr, uint32_t additional_size);

/* Creates chunks based on the media format passed as argument and inserts the new chunks after the current writers
 * chunk */
spf_circ_buf_result_t _circ_buf_add_chunks(spf_circ_buf_t *        circ_buf_ptr,
                                           uint32_t                num_additional_chunks,
                                           uint32_t                num_additional_frames,
                                           uint32_t                frame_data_size_in_bytes,
                                           spf_circ_buf_mf_info_t *chunk_mf,
                                           uint32_t                last_chunk_size,
                                           uint32_t                actual_chunk_size);

// Utility to remove chunks based on the decreased buffer size.
spf_circ_buf_result_t _circ_buf_shrink_buffer(spf_circ_buf_t *circ_buf_ptr, uint32_t removable_size);

// Utility to resize the buffer based on the update read clients.
spf_circ_buf_result_t _circ_buf_client_resize(spf_circ_buf_t *circ_buf_ptr);

// Utility to advance the given chunk node to the next chunk in the list. It wraps around to the head
// chunk if the input chunk is the tail of the list.
spf_circ_buf_result_t _circ_buf_next_chunk_node(spf_circ_buf_t *circ_buf_ptr, spf_list_node_t **chunk_ptr_ptr);

// Utility to reset the client r/w chunk position based on current buffer state.
spf_circ_buf_result_t _circ_buf_read_client_reset(spf_circ_buf_client_t *rd_client_ptr);

// Utility to reset the client write chunk position based on current buffer state.
spf_circ_buf_result_t _circ_buf_write_client_reset(spf_circ_buf_client_t *wr_client_ptr);

// Helper function to advance clients read/write position forward by given bytes.
spf_circ_buf_result_t _circ_buf_position_shift_forward(spf_circ_buf_client_t *client_ptr,
                                                       uint32_t               bytes_to_advance,
                                                       bool_t                 need_to_handle_metadata);

// Helper function to advance clients read/write position forward by given bytes.
spf_circ_buf_result_t _circ_buf_inc_write_byte_counter(spf_circ_buf_t *circ_buf_ptr, uint32_t bytes);

// check validity of input media format
bool_t _circ_buf_is_valid_mf(spf_circ_buf_client_t *client_ptr, capi_media_fmt_v2_t *inp_mf);

bool_t _circ_buf_check_and_handle_change_in_mf(spf_circ_buf_client_t *rd_client_ptr, spf_circ_buf_mf_info_t *new_mf);

// Returns new chunk ID.
uint16_t _circ_buf_get_new_chunk_id(spf_circ_buf_t *circ_buf_ptr);

/* Media format utils */
bool_t _is_mf_unchanged(spf_circ_buf_client_t *wr_handle, capi_media_fmt_v2_t *inp_mf);

bool_t _circ_buf_is_mf_equal(spf_circ_buf_mf_info_t *mf1, spf_circ_buf_mf_info_t *mf2);

void _circ_buf_compute_chunk_info(spf_circ_buf_t *        circ_buf_ptr,
                                  spf_circ_buf_mf_info_t *mf_ptr,
                                  uint32_t                buffer_size_in_us,
                                  uint32_t *              total_num_frames,
                                  uint32_t *              frame_data_size_in_bytes,
                                  uint32_t *              total_num_chunks,
                                  uint32_t *              actual_chunk_size,
                                  uint32_t *              last_chunk_size);

spf_circ_buf_result_t _circ_buf_check_and_recreate(spf_circ_buf_client_t *wr_handle,
                                                   bool_t                 allow_overflow,
                                                   bool_t *               did_recreate_ptr);

/* Raw Compressed Circular Buffer */

/* Advances to next frame */
spf_circ_buf_result_t _circ_buf_raw_advance_to_next_frame(spf_circ_buf_raw_t *     circ_buf_raw_ptr,
                                                          spf_circ_buf_position_t *pos);

/* Metadata Move from input to buffer */
spf_circ_buf_result_t _move_metadata_from_input_sdata_to_cur_wr_raw_frame(spf_circ_buf_raw_client_t *wr_client_ptr,
                                                                          capi_buf_t *               in_buf_ptr,
                                                                          capi_stream_data_t *       in_sdata_ptr,
                                                                          uint32_t len_consumed_per_ch_in_bytes,
                                                                          uint32_t in_initial_len_per_ch_in_bytes);

/* Metadata Clone from buffer to output */
spf_circ_buf_result_t _circ_buf_raw_clone_sdata_from_cur_frame_to_reader_hdl(spf_circ_buf_frame_t *cur_rd_frame_ptr,
                                                                             spf_circ_buf_raw_client_t *rd_client_ptr,
                                                                             bool_t is_output_sdata_v2);

/* Move or clone metadata */
spf_circ_buf_result_t _circ_buf_raw_clone_or_move_metadata_list(spf_circ_buf_raw_t *   circ_buf_raw_ptr,
                                                                module_cmn_md_list_t **src_list_pptr,
                                                                module_cmn_md_list_t **dst_list_pptr,
                                                                bool_t                 is_clone);

/* check if the data can be further copied from buffer frame to output buffer */
bool_t _circ_buf_raw_can_read_data_further(spf_circ_buf_raw_client_t *rd_client_ptr,
                                           capi_stream_data_t *       out_sdata_ptr,
                                           uint32_t                   initial_bytes_in_output);

/* Propagate metadata to output */
spf_circ_buf_result_t _circular_buffer_raw_propagate_metadata_to_output_stream(spf_circ_buf_raw_client_t *rd_client_ptr,
                                                                               capi_buf_t *               out_buf_ptr,
                                                                               capi_stream_data_t *       out_sdata_ptr,
                                                                               uint32_t initial_read_frame_offset,
                                                                               uint32_t initial_bytes_available_to_read,
                                                                               uint32_t len_consumed_from_frame,
                                                                               uint32_t initial_bytes_in_output);

/* Write one MTU frame into the buffer */
spf_circ_buf_result_t _circ_buf_raw_write_one_frame(spf_circ_buf_raw_client_t *wr_raw_client_ptr,
                                                    capi_stream_data_t *       in_sdata_ptr,
                                                    bool_t                     allow_overflow,
                                                    uint32_t *                 memeset_value_ptr);

/* Check f next write can cause overflow. If yes, check if the buf is not at full capacity and expand. */
bool_t _circ_buf_raw_check_if_need_space(spf_circ_buf_raw_client_t *rd_raw_client_ptr,
                                         spf_circ_buf_raw_client_t *wr_raw_client_ptr,
                                         uint32_t                   actual_data_len_consumed);

/* Read one MTU frame into output buffer */
spf_circ_buf_result_t _circ_buf_raw_read_one_frame(spf_circ_buf_raw_client_t *rd_client_ptr,
                                                   capi_stream_data_t *       out_sdata_ptr);

/* Write specified data into buffer */
spf_circ_buf_result_t _circ_buf_raw_write_data(spf_circ_buf_raw_client_t *wr_handle,
                                               capi_stream_data_t *       in_sdata_ptr,
                                               bool_t                     allow_overflow,
                                               uint32_t *                 memset_value_ptr,
                                               uint32_t *                 num_memset_bytes);

/* Check if there is overflow in the buffer - read and write handles*/
spf_circ_buf_result_t _circ_buf_raw_detect_and_handle_overflow(spf_circ_buf_raw_client_t *wr_client_ptr,
                                                               bool_t                     allow_overflow);

/* Allocate one chunk based on chunk size mentioned */
spf_circ_buf_chunk_t *_circ_buf_raw_allocate_chunk(spf_circ_buf_raw_t *circ_buf_ptr,
                                                   uint32_t            frame_data_size_in_bytes,
                                                   uint32_t            chunk_size);

/* Free Chunk Metadata */
spf_circ_buf_result_t _circ_buf_raw_free_chunk_metadata(spf_circ_buf_raw_t *  circ_buf_raw_ptr,
                                                        spf_circ_buf_chunk_t *chunk_ptr);

/* Free frame memtadata */
spf_circ_buf_result_t _circ_buf_raw_free_frame_metadata(spf_circ_buf_raw_t *   circ_buf_raw_ptr,
                                                        spf_circ_buf_frame_t * frame_ptr,
                                                        bool_t                 force_free,
                                                        module_cmn_md_list_t **stream_associated_md_list_pptr);

/* Free sdata in the buffer */
spf_circ_buf_result_t _circ_buf_raw_free_sdata(spf_circ_buf_raw_t *   circ_buf_raw_ptr,
                                               capi_stream_data_v2_t *sdata_ptr,
                                               bool_t                 force_free,
                                               module_cmn_md_list_t **stream_associated_md_list_pptr);

/* Free the chunk */
spf_circ_buf_result_t _circ_buf_raw_free_chunk(spf_circ_buf_raw_t *  circ_buf_raw_ptr,
                                               spf_circ_buf_chunk_t *chunk_hdr_ptr);

/* Destroy read/write client */
void _circ_buf_raw_destroy_client_hdl(spf_circ_buf_raw_t *circ_buf_raw_ptr, spf_circ_buf_raw_client_t **client_pptr);

/* Add specified number of chunks of equal chunk_size */
spf_circ_buf_result_t _circ_buf_raw_add_chunks(spf_circ_buf_raw_t *circ_raw_buf_ptr,
                                               uint32_t            num_additional_chunks,
                                               uint32_t            num_additional_frames,
                                               uint32_t            frame_data_size_in_bytes,
                                               uint32_t            chunk_size);

/* Reset and create write client handle */
spf_circ_buf_result_t _circ_buf_raw_write_client_reset(spf_circ_buf_raw_client_t *client_ptr);

/* Resize the client */
spf_circ_buf_result_t _circ_buf_raw_client_resize(spf_circ_buf_raw_t *circ_buf_raw_ptr);

/* Get next node in the chunk */
spf_circ_buf_result_t _circ_buf_raw_next_chunk_node(spf_circ_buf_raw_t *circ_buf_raw_ptr,
                                                    spf_list_node_t **  chunk_ptr_ptr);

/* Reset and create read client handle */
spf_circ_buf_result_t _circ_buf_raw_read_client_reset(spf_circ_buf_raw_client_t *rd_client_ptr);

/* Validate if media format is raw compressed / deint raw compressed */
bool_t _circ_buf_raw_is_valid_mf(spf_circ_buf_raw_client_t *client_ptr, capi_cmn_raw_media_fmt_t *inp_mf);

/* Get chunk id for new chunk created */
uint16_t _circ_buf_raw_get_new_chunk_id(spf_circ_buf_raw_t *circ_buf_raw_ptr);

/* Compute size of one chunk and number of frames per chunk */
void _circ_buf_raw_compute_chunk_info(spf_circ_buf_raw_t *circ_buf_raw_ptr,
                                      uint32_t            buffer_size_bytes,
                                      uint32_t *          frame_data_size_bytes,
                                      uint32_t *          actual_chunk_size,
                                      uint32_t *          actual_num_frames_per_chunk);

/* Expand the buffer by one chunk at a time after first time creation only */
spf_circ_buf_result_t _circ_buf_raw_expand_buffer_by_chunk(spf_circ_buf_raw_client_t *wr_raw_handle);

/* Create/expand the buffer (for the first time) using the size mentioned in additional_size_bytes */
spf_circ_buf_result_t _circ_buf_raw_expand_buffer(spf_circ_buf_raw_t *circ_buf_raw_ptr, uint32_t additional_size_bytes);

#ifdef DEBUG_CIRC_BUF_UTILS_METADATA
void print_frame_metadata(spf_circ_buf_frame_t *frame_ptr);
void print_frame_metadata_list(capi_stream_data_v2_t *in_sdata_v2_ptr);
#endif

// Utility to check if the buffer is corrupted.
// spf_circ_buf_result_t _circ_buf_check_if_corrupted(spf_circ_buf_t *frag_circ_buf_ptr);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /*__CIRC_BUF_UTILS_I_H*/
