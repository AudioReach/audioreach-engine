#ifndef __CIRC_BUFFER_I_H
#define __CIRC_BUFFER_I_H

/**
 *   \file circular_buffer_i.h
 *   \brief
 *        This file contains utility functions for handling fragmented circularbuffer operation.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "circular_buffer.h"

circbuf_result_t add_circ_buf_read_advance(circ_buf_client_t *rd_client_ptr, uint32_t bytes_to_advance);

circbuf_result_t add_circ_buf_write_util(circ_buf_client_t *wr_client_ptr,
                                         int8_t *           inp_ptr,
                                         uint8_t            memset_value,
                                         uint32_t           bytes_to_write,
                                         uint32_t           is_valid_timestamp,
                                         int64_t            timestamp);

// Helper function to advance clients read/write position forward by given bytes.
circbuf_result_t add_circ_buf_position_shift_forward(circ_buf_t *      circ_buf_ptr,
                                                     spf_list_node_t **rd_chunk_list_pptr,
                                                     uint32_t *        rw_chunk_offset_ptr,
                                                     uint32_t          bytes_to_advance);

// Utility to advance the given chunk node to the next chunk in the list. It wraps around to the head
// chunk if the input chunk is the tail of the list.
circbuf_result_t add_circ_buf_next_chunk_node(circ_buf_t *circ_buf_ptr, spf_list_node_t **chunk_ptr_ptr);

circbuf_result_t add_circ_buf_data_copy_util(circ_buf_t *      circ_buf_ptr,
                                             uint32_t          bytes_to_copy,
                                             int8_t *          src_buf_ptr,
                                             uint8_t           memset_value, // will be used only if src_buf_ptr is NULL
                                             spf_list_node_t **wr_chunk_node_pptr,
                                             uint32_t *        wr_chunk_offset_ptr);

/*
 * Resets the client read position to the oldest buffered sample.
 * force adjust unread length to the full size of the buffer.
 */
circbuf_result_t add_circ_buf_read_client_reset(circ_buf_client_t *client_ptr, bool_t force_reset);

#ifdef DEBUG_CIRC_BUF_UTILS
void print_chunk_list(circ_buf_t *circ_buf_ptr);
void print_client_chunk_positions(circ_buf_t *circ_buf_ptr);
#endif

#endif // __CIRC_BUFFER_I_H
