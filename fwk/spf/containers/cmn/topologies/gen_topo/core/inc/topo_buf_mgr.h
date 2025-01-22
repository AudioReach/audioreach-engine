/**
 * \file topo_buf_mgr.h
 *  
 * \brief
 *  
 *     Topology buffer manager
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_list_utils.h"

#ifndef TOPO_BUF_MGR_H_
#define TOPO_BUF_MGR_H_

//#define ENABLE_BUF_MANAGER_TEST

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

// #define TOPO_BUF_MGR_DEBUG

#define TBF_MSG_PREFIX "TBF :%08X: "
#define TBF_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, TBF_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)
#define TBF_MSG_ISLAND(ID, xx_ss_mask, xx_fmt, ...) AR_MSG_ISLAND(xx_ss_mask, TBF_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#define DIFF(x, y) ((x) < (y) ? ((y) - (x)) : ((x) - (y)))

#define MAX_BUF_UNUSED_COUNT 10

// 100 ms
#define TBF_UNUSED_BUFFER_CALL_INTERVAL_US 100000

typedef struct topo_buf_manager_element_t
{
   spf_list_node_t list_node; /* should be first element. */
   uint16_t        ref_count; /* used by topology; stored here for saving memory */
   uint16_t        unused_count;
   uint32_t        size;
} topo_buf_manager_element_t;

#define TBF_EXTRA_ALLOCATION (ALIGN_8_BYTES(sizeof(topo_buf_manager_element_t)))
#define TBF_BUF_PTR_OFFSET (ALIGN_8_BYTES(sizeof(topo_buf_manager_element_t)))

/** This enum used for aggregation in the container */
typedef enum topo_buf_manager_mode_t
{
   TOPO_BUF_MODE_INVALID = 0,
   TOPO_BUF_LOW_LATENCY = 1,
   TOPO_BUF_LOW_POWER = 2,
} topo_buf_manager_mode_t;

typedef struct topo_buf_manager_t
{
   uint32_t         max_memory_allocated;
   uint32_t         current_memory_allocated;
   uint16_t         total_num_bufs_allocated;
   uint16_t         num_used_buffers; /**< statistics for debugging/efficiency check. */
   topo_buf_manager_mode_t mode;
   spf_list_node_t *last_node_ptr;
   spf_list_node_t *head_node_ptr;
   uint64_t         prev_destroy_unused_call_ts_us; /* timestamp of the last unused buffer funtion call in micro seconds */
} topo_buf_manager_t;

typedef struct gen_topo_t gen_topo_t;

ar_result_t topo_buf_manager_init(gen_topo_t *topo_ptr);

void topo_buf_manager_deinit(gen_topo_t *topo_ptr);

ar_result_t topo_buf_manager_get_buf(gen_topo_t *topo_ptr, int8_t **buf_pptr, uint32_t buf_size);

void topo_buf_manager_return_buf(gen_topo_t *topo_ptr, int8_t *buf_ptr);

void topo_buf_manager_destroy_all_unused_buffers(gen_topo_t *topo_ptr, bool_t force_free);

#ifdef ENABLE_BUF_MANAGER_TEST
ar_result_t buf_mgr_test();
#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif /* AVS_SPF_CONTAINERS_CMN_TOPOLOGIES_ADVANCED_TOPO_INC_ADV_TOPO_BUF_MANAGER_H_ */
