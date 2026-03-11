#ifndef PT_CNTR_H
#define PT_CNTR_H

/**
 * \file pt_cntr.h
 *
 * \brief
 *     Compression-Decompression Container
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_cmn_utils.h"
#include "spf_utils.h"
#include "container_utils.h"
#include "gen_cntr_i.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#ifdef SAFE_MODE

/** by default safe mode only prints errors */
#define PT_CNTR_SAFE_MODE

/** enable error recovery in safe mode */
// #define PT_CNTR_SAFE_MODE_ERR_RECOVERY

/** enable crash on error in safe mode */
// #define PT_CNTR_SAFE_MODE_ERR_RECOVERY

#endif

/* =======================================================================
Data structures
========================================================================== */

typedef union pt_cntr_flags_t
{
   struct
   {
      uint32_t num_subgraphs_not_satisfied : 1; /**< set if no SGs are not as expected*/
      uint32_t supports_md_propagation     : 1;
      uint32_t processing_data_path_mf : 1;
   };
   uint32_t word;
} pt_cntr_flags_t;


/** instance struct of GEN_CNTR */
typedef struct pt_cntr_t
{
   gen_cntr_t gc; /**< Container utility. Must be first element. */

   // currently only one module is supported
   gu_module_list_t *module_proc_list_ptr;
   gu_module_list_t *src_module_list_ptr;  /**< List of source modules */
   gu_module_list_t *sink_module_list_ptr; /**< List of sink modules */

   pt_cntr_flags_t flags; /**< Set if the container is disabled >*/
} pt_cntr_t;

typedef union pt_cntr_module_flags_t
{
   struct
   {
      uint32_t has_attached_module : 1;
      uint32_t has_stopped_port : 1;
   };
   uint32_t word;
} pt_cntr_module_flags_t;

typedef struct pt_cntr_module_t
{
   gen_cntr_module_t gc;

   // TODO: for memory opt storing per module, ideally should be stored per port, can change this if such requirement
   // comes in future.
   /**<  alloc/free fn valid only for sink eps which support extn INTF_EXTN_EVENT_ID_MODULE_BUFFER_ACCESS_ENABLE*/
   intf_extn_get_module_input_buf_func_t     get_input_buf_fn;
   intf_extn_return_module_output_buf_func_t return_output_buf_fn;
   uint32_t                                  buffer_mgr_cb_handle;
   capi_err_t (*process)(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

   // todo: skip adding bypass modules in the proc list.
   //          1) Challenge: currently relying on NBLC to propagate ext buffers, if module list is altered need to update
   //                       NBLC start/end to do buffer assignment.

   capi_stream_data_v2_t **in_port_sdata_pptr;
   capi_stream_data_v2_t **out_port_sdata_pptr;

   pt_cntr_module_flags_t flags;
} pt_cntr_module_t;

typedef struct pt_cntr_input_port_t
{
   // todo: make sdata a pointer gen_topo_input_port_t->common.sdata_ptr
   // and for gen topo sdata_ptr will be a continguous to gen_topo_input_port_t struct memory
   // having sdata_ptr will increase overheads for GC/SC, because of extra pointer
   gen_topo_input_port_t gc;

   // pt_cntr_module_t    *ext_in_host_module_ptr;
   // /**< Has a valid host module ptr if the current module is attached. It will happen if current module is elementary,
   //  * has ext input and attached.*/

   bool_t can_assign_ext_in_buffer;
   bool_t can_assign_ext_out_buffer;

   // todo: optimize buffer memory and sdata memory in the topology, by buffer manager

   // todo: sdata sizeof(capi_stream_data_v2_t) comes from the pervious/connected output &gen_topo_input_port_t->common.sdata
   // for external input, it will be self.
   capi_stream_data_v2_t *sdata_ptr;
} pt_cntr_input_port_t;

typedef struct pt_cntr_output_port_t
{
   gen_topo_output_port_t gc;

   bool_t can_assign_ext_in_buffer;
   bool_t can_assign_ext_out_buffer;

   // sdata sizeof(capi_stream_data_v2_t) comes from &gen_topo_input_port_t->common.sdata
   //
   capi_stream_data_v2_t *sdata_ptr;
} pt_cntr_output_port_t;

typedef struct pt_cntr_ext_in_port_t
{
   gen_cntr_ext_in_port_t gc;

   int8_t *topo_in_buf_ptr; // buffer used if US != DS frame size, and also for underrun scenarios

   bool_t pass_thru_upstream_buffer;
} pt_cntr_ext_in_port_t;

typedef struct pt_cntr_ext_out_port_t
{
   gen_cntr_ext_out_port_t gc;
} pt_cntr_ext_out_port_t;

/**
 * Defines for inserting posal Q's after the container structures
 * memory for the gc is organized as follows:

gen_cntr_t  posal_queue_t posal_queue_t
            cmdq          ctrl_port_q
to reduce malloc overheads we do one single allocation
*/
#define PT_CNTR_SIZE_W_2Q (ALIGNED_SIZE_W_QUEUES(pt_cntr_t, 2))

#define PT_CNTR_CMDQ_OFFSET (ALIGN_8_BYTES(sizeof(pt_cntr_t)))
#define PT_CNTR_CTRL_PORT_Q_OFFSET (ALIGNED_SIZE_W_QUEUES(pt_cntr_t, 1))

#define PT_CNTR_EXT_IN_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(pt_cntr_ext_in_port_t, 1))
#define PT_CNTR_EXT_OUT_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(pt_cntr_ext_out_port_t, 1))

#define PT_CNTR_EXT_IN_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(pt_cntr_ext_in_port_t, 1))
#define PT_CNTR_EXT_OUT_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(pt_cntr_ext_out_port_t, 1))

#define PT_CNTR_GET_EXT_IN_PORT_Q_ADDR(x) (CU_PTR_PUT_OFFSET(x, ALIGN_8_BYTES(sizeof(pt_cntr_ext_in_port_t))))
#define PT_CNTR_GET_EXT_OUT_PORT_Q_ADDR(x) (CU_PTR_PUT_OFFSET(x, ALIGN_8_BYTES(sizeof(pt_cntr_ext_out_port_t))))

#define PT_CNTR_EXT_CTRL_PORT_Q_OFFSET (ALIGN_8_BYTES(sizeof(pt_cntr_ext_ctrl_port_t)))
#define PT_CNTR_INT_CTRL_PORT_Q_OFFSET (ALIGN_8_BYTES(sizeof(pt_cntr_int_ctrl_port_t)))

#define PT_CNTR_PUT_CMDQ_OFFSET(x) (CU_PTR_PUT_OFFSET(x, PT_CNTR_CMDQ_OFFSET))

/* =======================================================================
Functions
========================================================================== */

bool_t is_pass_thru_container_supported();

/** ----------FRONT End control path utilities -------- **/
ar_result_t pt_cntr_stm_fwk_extn_handle_enable(pt_cntr_t *me_ptr, gu_module_list_t *stm_mod_list_ptr);
ar_result_t pt_cntr_stm_fwk_extn_handle_disable(pt_cntr_t *me_ptr, gu_module_list_t *stm_mod_list_ptr);
ar_result_t pt_cntr_update_module_process_list(pt_cntr_t *me_ptr);
ar_result_t pt_cntr_handle_module_buffer_access_event(gen_topo_t        *topo_ptr,
                                                       gen_topo_module_t *mod_ptr,
                                                       capi_event_info_t *event_info_ptr);
ar_result_t pt_cntr_destroy_modules_resources(pt_cntr_t *me_ptr, bool_t b_destroy_all_modules);
capi_err_t  pt_cntr_bypass_module_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);
ar_result_t pt_cntr_validate_topo_at_open(pt_cntr_t *me_ptr);
ar_result_t pt_cntr_validate_media_fmt_thresh(pt_cntr_t *me_ptr);
capi_err_t  pt_cntr_capi_event_callback(void *context_ptr, capi_event_id_t id, capi_event_info_t *event_info_ptr);
ar_result_t pt_cntr_realloc_scratch_sdata_arr(pt_cntr_t *me_ptr, pt_cntr_module_t *module_ptr);
ar_result_t pt_cntr_create_module(gen_topo_t            *topo_ptr,
                                  gen_topo_module_t     *module_ptr,
                                  gen_topo_graph_init_t *graph_init_data_ptr);
ar_result_t pt_cntr_init_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr);
ar_result_t pt_cntr_init_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr);

ar_result_t pt_cntr_deinit_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr);

ar_result_t pt_cntr_post_operate_on_ext_in_port(void *                     base_ptr,
                                                 uint32_t                   sg_ops,
                                                 gu_ext_in_port_t **        ext_in_port_pptr,
                                                 spf_cntr_sub_graph_list_t *spf_sg_list_ptr);
ar_result_t pt_cntr_post_operate_on_ext_out_port(void                      *base_ptr,
                                                 uint32_t                   sg_ops,
                                                 gu_ext_out_port_t        **ext_out_port_pptr,
                                                 spf_cntr_sub_graph_list_t *spf_sg_list_ptr);
ar_result_t pt_cntr_check_insert_missing_eos_on_next_module(gen_topo_t *topo_ptr, gen_topo_input_port_t *in_port_ptr);

ar_result_t pt_cntr_preprocess_setup_ext_output_non_static(pt_cntr_t              *me_ptr,
                                                           pt_cntr_module_t       *module_ptr,
                                                           pt_cntr_ext_out_port_t *ext_out_port_ptr);

void pt_cntr_propagate_ext_input_buffer_forwards_non_static(pt_cntr_t             *me_ptr,
                                                 pt_cntr_input_port_t  *start_in_port_ptr,
                                                 capi_stream_data_v2_t *sdata_ptr,
                                                 uint32_t               num_bufs_to_update);

void pt_cntr_propagate_ext_output_buffer_backwards_non_static(pt_cntr_t             *me_ptr,
                                                   pt_cntr_output_port_t *end_output_port_ptr,
                                                   capi_stream_data_v2_t *sdata_ptr,
                                                   uint32_t               num_bufs_to_update);

static inline pt_cntr_output_port_t *pt_cntr_get_connected_output_port(pt_cntr_input_port_t *in_port_ptr)
{
   return (pt_cntr_output_port_t *)in_port_ptr->gc.gu.conn_out_port_ptr;
}

static inline pt_cntr_input_port_t *pt_cntr_get_connected_input_port(pt_cntr_output_port_t *out_port_ptr)
{
   return (pt_cntr_input_port_t *)out_port_ptr->gc.gu.conn_in_port_ptr;
}

/** ----------FRONT End data path utilities -------- **/
ar_result_t pt_cntr_signal_trigger(cu_base_t *cu_ptr, uint32_t channel_bit_index);
ar_result_t pt_cntr_assign_port_buffers(pt_cntr_t *me_ptr);

static inline bool_t check_if_pass_thru_container(gen_cntr_t *me_ptr)
{
   return (APM_CONTAINER_TYPE_ID_PTC == me_ptr->cu.cntr_type) ? TRUE : FALSE;
}

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //PT_CNTR_H