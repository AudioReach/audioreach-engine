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

   spf_list_node_t *opt_static_buf_assign_temp_list_ptr;
   /**
    * This is temp list which contains list of buffers that can be used for optimized static buffer assignment.
    * This vairable is used just in the ctrl context where we are reassigning static buffers to topology.
    *
    * This list must be freed at end of static buffer assignment. Currently this list is used in the context of thin
    * topo only.
    *
    * Algorithm for static buffer assignment:
    * For first module in the sorted order buffers are assigned normally from the buf manager list, after all inputs and
    * outputs are assigned, iterate through all the input ports and push the input buffers to the temp free list
    * "opt_static_buf_assign_temp_list_ptr"
    *
    * For the next module, input buffers will be assigned same as the previous module's output buffer. But when
    * assigning output buffer for the next module, first buffer is qeuried from the temp free list. only if temp list is
    * empty it will a allocate a buffer from the buffer manager list i.e "topo_buf_manager_t->head_node_ptr"
    *
    * Example: static buffer assignment for the following topo looks like this SAL has 3 inputs, Splitter has 3 outputs.
    *  through this optimized static buffer assignment we use only 4 buffers for the 10 ports in the graph.
    *
    *   --(i1=b1)-->| [SAL]--(o1=b4)-> --(i1=b4)--> [MFC] --(o1=b1)-> -(i1=b1)--> [SPITTER]|--(o1=b2)-->
    *   --(i2=b2)-->|                                                                      |--(o2=b3)-->
    *   --(i3=b3)-->|                                                                      |--(o2=b4)-->
    */
} topo_buf_manager_t;

typedef struct gen_topo_t gen_topo_t;

ar_result_t topo_buf_manager_init(gen_topo_t *topo_ptr);

void topo_buf_manager_deinit(gen_topo_t *topo_ptr);

ar_result_t topo_buf_manager_get_buf(gen_topo_t *topo_ptr, int8_t **buf_pptr, uint32_t buf_size);

void topo_buf_manager_return_buf(gen_topo_t *topo_ptr, int8_t *buf_ptr);

void topo_buf_manager_destroy_all_unused_buffers(gen_topo_t *topo_ptr, bool_t force_free);

/** Topo buf utils for static buffer assignment

    * Algorithm for static buffer assignment:
    * For the first module in the sorted order buffers are assigned normally from the buf manager list, after all inputs
   and
    * outputs are assigned, iterate through all the input ports and add the input buffers to the temp free list
    * "opt_static_buf_assign_temp_list_ptr"
    *
    * For the next module, input buffers will be assigned same as the previous module's output buffer. But when
    * assigning output buffer for the next module, firstly temp free list is checked if any buffer is available. Only if
    * temp list is empty it will a allocate a buffer from the buffer manager list i.e "topo_buf_manager_t->head_node_ptr"
    *
    * Example: static buffer assignment for the following topo looks like this SAL has 3 inputs, Splitter has 3 outputs.
    * through this optimized static buffer assignment we use only 4 buffers for the 10 ports in the graph.
    *
    *   --(i1=b1)-->| [SAL]--(o1=b4)-> --(i1=b4)--> [MFC] --(o1=b1)-> -(i1=b1)--> [SPITTER]|--(o1=b2)-->
    *   --(i2=b2)-->|                                                                      |--(o2=b3)-->
    *   --(i3=b3)-->|

*/

// Resets the list containing free buffers during static buffer assigment process.
void topo_buf_manager_reset_static_assignment_list(gen_topo_t *topo_ptr);

// during static buffer assignment, fwk adds the assigned topo input buffers to this list,
// which can be potentially be assigned for the next modules input/output ports as required.
//
// Note that ref count is not deremented when adding to this list, because the port adding
// to this buffer is not clearing the references because it continues to use it.
void topo_buf_manager_static_buf_assign_add_buf_to_temp_free_list(gen_topo_t *topo_ptr, int8_t *buf_ptr);

// Gets the free buffer from the temp list during static buf assignment. Ref count is incremented when a buffer
// is popped from the list
void topo_buf_manager_check_static_buf_assign_temp_list(gen_topo_t *topo_ptr, int8_t **buf_pptr, uint32_t buf_size);

#ifdef ENABLE_BUF_MANAGER_TEST
ar_result_t buf_mgr_test();
#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif /* AVS_SPF_CONTAINERS_CMN_TOPOLOGIES_ADVANCED_TOPO_INC_ADV_TOPO_BUF_MANAGER_H_ */
