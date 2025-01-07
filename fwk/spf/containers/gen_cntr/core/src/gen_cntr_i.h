#ifndef GEN_CNTR_I_H
#define GEN_CNTR_I_H
/**
 * \file gen_cntr_i.h
 * \brief
 *   This file contains structure definitions for cd
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_cmn_utils.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */

#include "apm_container_api.h"
#include "apm_api.h"
#include "apm_sub_graph_api.h"
#include "apm_container_api.h"
#include "apm_module_api.h"
#include "common_enc_dec_api.h"
#include "module_cmn_api.h"
#include "wr_sh_mem_ep_ext_api.h"
#include "rd_sh_mem_ep_ext_api.h"
#include "offload_sp_api.h"

#include "capi_mm_error_code_converter.h"

#include "container_utils.h"
#include "graph_utils.h"

#include "spf_ref_counter.h"
#include "spf_svc_utils.h"

#include "posal_power_mgr.h"
#include "posal_intrinsics.h"
#include "posal_internal_inline.h"
#include "posal_err_fatal.h"

#include "gen_topo.h"
#include "gen_topo_capi.h"
#include "gen_topo_buf_mgr.h"
#include "gen_cntr_utils.h"
#include "gen_cntr_err_check.h"
#include "gen_cntr_tgt_specific.h"
#define GEN_CNTR_STATIC static

/**
 * Defines for inserting posal Q's after the container structures
 * memory for the gc is organized as follows:

gen_cntr_t  posal_queue_t posal_queue_t
            cmdq          ctrl_port_q
to reduce malloc overheads we do one single allocation
*/
#define GEN_CNTR_SIZE_W_2Q (ALIGNED_SIZE_W_QUEUES(gen_cntr_t, 2))

#define GEN_CNTR_CMDQ_OFFSET (ALIGN_8_BYTES(sizeof(gen_cntr_t)))
#define GEN_CNTR_CTRL_PORT_Q_OFFSET (ALIGNED_SIZE_W_QUEUES(gen_cntr_t, 1))

/** memory for the ports are organized as follows:

port_type_t  posal_queue_t
To reduce malloc overheads we do one single allocation and put the queue right after the struct.
The following macros are to be used to get the size and position of the queue fields for the struct where needed for
resource allocation and initialization.

*/
#define GEN_CNTR_EXT_CTRL_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(gen_cntr_ext_ctrl_port_t, 1))
#define GEN_CNTR_EXT_IN_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(gen_cntr_ext_in_port_t, 1))
#define GEN_CNTR_EXT_OUT_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(gen_cntr_ext_out_port_t, 1))
#define GEN_CNTR_INT_CTRL_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(gen_cntr_int_ctrl_port_t, 0))

#define GEN_CNTR_GET_EXT_IN_PORT_Q_ADDR(x) (CU_PTR_PUT_OFFSET(x, ALIGN_8_BYTES(sizeof(gen_cntr_ext_in_port_t))))
#define GEN_CNTR_GET_EXT_OUT_PORT_Q_ADDR(x) (CU_PTR_PUT_OFFSET(x, ALIGN_8_BYTES(sizeof(gen_cntr_ext_out_port_t))))

#define GEN_CNTR_EXT_CTRL_PORT_Q_OFFSET (ALIGN_8_BYTES(sizeof(gen_cntr_ext_ctrl_port_t)))
#define GEN_CNTR_INT_CTRL_PORT_Q_OFFSET (ALIGN_8_BYTES(sizeof(gen_cntr_int_ctrl_port_t)))

#define GEN_CNTR_PUT_CMDQ_OFFSET(x) (CU_PTR_PUT_OFFSET(x, GEN_CNTR_CMDQ_OFFSET))

/*This macro controls the number of frames an "Island Container"
Processes after START, before voting for island participation. */
#define GEN_CNTR_PRE_ISLAND_FRAMES_TO_PROCESS 2

#define GEN_CNTR_ERR_PRINT_INTERVAL_MS 10
// clang-format off

// command & event processing during container execution need to be done at high prio in case proc dur is less than this.
// microseconds
#define GEN_CNTR_PROC_DUR_THRESH_FOR_PRIO_BUMP_UP 2500

// A scale factor for decreasing the thread processing time can be specified.
// Suppose nominal frame duration is 1ms. For command handling available time may reduce to 0.5 ms.
// The scale factor in this case is 2
// This is used only for started signal triggered containers which have frame duration less than GEN_CNTR_PROC_DUR_THRESH_FOR_PRIO_BUMP_UP
#define GEN_CNTR_PROC_DUR_SCALE_FACTOR_FOR_CMD_PROC 2

/* In GC spf msg token upper nibble of MSB is  used to indicate if the data msg created was according to
 * deinterleaved (SPF_MSG_DATA_BUFFER_V2) OR (SPF_MSG_DATA_BUFFER) definitions. The token_data is populated when created
 * and read when popped in re-alloc context. SPF_MSG_DATA_BUFFER_V2 is currently only supported in GCs hence the handling
 * of this message need not be a defined flag but a token instead. */
#define GEN_CNTR_DATA_MSG_OUT_BUF_TOKEN_MASK 0xF0000000
#define GEN_CNTR_DATA_MSG_OUT_BUF_TOKEN_SHIFT 28

/* Local gen_cntr meta_data buffer */
typedef struct gen_cntr_md_buf_t
{
   bool_t   is_md_buffer_api_supported; // todo : we can remove in future
   bool_t   is_md_parsed_from_input_buf;
   int8_t * data_ptr;
   uint32_t max_data_len;
   uint32_t actual_data_len;
   uint32_t mem_map_handle;
   uint32_t status;
   module_cmn_md_list_t      *md_list_ptr;        /**< metadata */
   int8_t *inband_buf_ptr;
   uint32_t inband_md_buf_size;
   uint32_t min_md_size_in_next_buffer;
   uint32_t max_md_size_per_frame;
} gen_cntr_md_buf_t;


/* Local gen_cntr buffer */
typedef struct gen_cntr_buf_t
{
   int8_t * data_ptr;
   uint32_t actual_data_len;
   uint32_t max_data_len;
   uint32_t mem_map_handle;
   uint32_t status;

   gen_cntr_md_buf_t    *md_buf_ptr;

} gen_cntr_buf_t;

typedef struct gen_cntr_ext_in_port_flags_t
{
   uint32_t eof : 1;                         /**< end of frame coming from DSP client.
                                                  Important note: Not applicable to Signal triggered containers. */
   uint32_t input_discontinuity: 1;          /**< When there's timestamp discontinuity or discont due to other data messages,
                                                EOF is called to force-process, and then output is
                                                released when EOF comes out thru ext out ports.
                                                cleared after EOF is set on input port.

                                                Important note: TS disc is ignored for signal triggered containers.
                                                So input_discontinuity can never happen to Signal triggerd containers.  */
   uint32_t pending_mf: 1;                        /**< when data path media format comes during process then it is marked pending
                                                       and handled when processing is done.

                                                       Not applicable to Signal triggerd containers. MF is handled immediately by dropping data and aslo
                                                       input_discontinuity is not applicable for ST containers. */

   uint32_t ready_to_go : 1;                  /**< TRUE indicates data is filled and there's no need to call gen_cntr_setup_internal_input_port_and_preprocess again*/

   uint32_t is_not_reset : 1;                 /**< TRUE indicates port is not in reset state, FALSE indicates port is in reset state*/

} gen_cntr_ext_in_port_flags_t;

/**
 * External input port information specific for off-load requirement.
 *
 */
typedef struct ext_in_port_client_cfg_t
{
    param_id_wr_ep_client_cfg_t cid;  /**< specifies the type and GPR port ID
                                                      of the Client sharing the data buffers to the WR EP module */

} ext_in_port_client_cfg_t;


/**
 * External output port information specific for off-load requirement.
 *
 */
typedef struct ext_out_port_client_cfg_t
{
    param_id_rd_ep_client_cfg_t cid;     /** specifies the type and GPR port ID sharing the data buffers to the RD EP module */
    uint32_t                    olc_client_md_buf_size;     /** specifies the metadata buffer size in the client buffer for OLC*/
    uint32_t                    read_data_buf_size;
    uint32_t                    pending_internal_eos_event;
} ext_out_port_client_cfg_t;

typedef struct gen_cntr_fwk_module_vtable_t
{
   ar_result_t (*set_cfg)(gen_cntr_t *me_ptr, gen_cntr_module_t *module_ptr, uint32_t param_id, int8_t *param_data_ptr, uint32_t param_size, spf_cfg_data_type_t cfg_type, cu_handle_rest_ctx_for_set_cfg_t **pending_set_cfg_ctx_pptr);
   ar_result_t (*reg_evt)(gen_cntr_t *me_ptr, gen_cntr_module_t *  module_ptr, topo_reg_event_t *event_cfg_payload_ptr, bool_t  is_register);
   ar_result_t (*raise_evt)(gen_cntr_t *me_ptr, gen_cntr_module_t *module_ptr);
   ar_result_t (*raise_ts_disc_event)(gen_cntr_t *me_ptr, gen_cntr_module_t *module_ptr, bool_t ts_valid, int64_t timestamp_disc_us);

} gen_cntr_fwk_module_vtable_t;

typedef struct gen_cntr_ext_in_vtable_t
{
   ar_result_t (*on_trigger)(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);
   ar_result_t (*read_data)(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr, uint32_t *bytes_copied_per_buf_ptr);
   /**
    * whether the pending buffer is a data buffer
    */
   bool_t (*is_data_buf)(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);
   ar_result_t (*free_input_buf)(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr, ar_result_t status, bool_t is_flush);
   ar_result_t (*process_pending_data_cmd)(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);
   ar_result_t (*frame_len_change_notif)(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);
   ar_result_t (*setup_topo_buf)(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_out_port_ptr);
} gen_cntr_ext_in_vtable_t;

typedef struct gen_cntr_ext_out_vtable_t
{
   // set up bufs on trigger.
   ar_result_t (*setup_bufs)(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
   ar_result_t (*write_data)(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);

   // below is only for RD SH MEM EP
   ar_result_t (*fill_frame_md)(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr, uint32_t bytes_per_process, uint32_t frame_offset_in_ext_buf, bool_t * release_out_buf_ptr);

   // gives amount of data filled in output buffer
   ar_result_t (*get_filled_size)(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);

   ar_result_t (*flush)(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr, bool_t is_client_cmd);

   ar_result_t (*recreate_out_buf)(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr, uint32_t req_out_buf_size, uint32_t num_data_msg, uint32_t num_bufs_per_data_msg_v2);

   ar_result_t (*prop_media_fmt)(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);

   void  (*write_metadata)(gen_cntr_t * me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr, uint32_t frame_offset_in_ext_buf, bool_t * release_out_buf_ptr);

   ar_result_t (*setup_topo_buf)(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);

} gen_cntr_ext_out_vtable_t;

/**
 * container level ports are those that have external connection from the topo
 * these have pointers to the internal module port
 *
 * media fmt is stored in module-port.
 * Please note that in memory a posal queue will immediatly follow this structure.
 */
typedef struct gen_cntr_ext_in_port_t
{
   gu_ext_in_port_t           gu;                 /**< Must be the first element */

   cu_ext_in_port_t           cu;

   module_cmn_md_list_t      *md_list_ptr;        /**< metadata */

   uint32_t                   flush_eos_cnt;      /**< ref count of flushing EOS that entered the container. cleared when the EOS cntr ref is destroyed. */

   gen_cntr_buf_t                  buf;                /**< Data held by the external input port. Depening on the container, data could be buffered,
                                                          or upstream clients may pass over their data_ptr. */

   ext_in_port_client_cfg_t        client_config;      /** < Configuration specific to offload use-cases */

   gen_cntr_ext_in_port_flags_t    flags;

   uint8_t                         err_msg_underrun_err_count;  /*< Underrun count after the last printed for this port */

   uint8_t                        bufs_num;                     /**< number of buffers that are valid in bufs_ptr - serves to check if data buf is v1 or v2 also.
                                                                     Either bufs.data_ptr or bufs_num should be set if there is data buffer */
   gen_cntr_buf_t *                bufs_ptr;                     /**< buffers that are available at ext out port */

   const gen_cntr_ext_in_vtable_t  *vtbl_ptr;

} gen_cntr_ext_in_port_t;

/**
 * Flags for the external output port.
 */
typedef struct gen_cntr_ext_out_port_flags_t
{
   uint32_t    frame_metadata_enable : 1; /**< Flag to enable adding frame metadata into the output buffer given by the
                                             client. */
   uint32_t    fill_ext_buf : 1;          /**< fill external buffer as much as possible. set to TRUE if max_frames_per_buffer is set as
                                             RD_SH_MEM_NUM_FRAMES_IN_BUF_AS_MUCH_AS_POSSIBLE */

   uint32_t    md_mf_enable : 1 ;         /**< Flag to enable media format as metadata to the client */

   uint32_t    is_not_reset : 1;          /**< TRUE indicates port is not in reset state, FALSE indicates port is in reset state*/
   /** temporary flags */
   uint32_t    out_media_fmt_changed: 1;  /**< Flag to indicate that the output media fmt changed*/

} gen_cntr_ext_out_port_flags_t;

/**
 * container level ports are those that have external connection from the topo
 * these have pointers to the internal module port
 *
 * In memory, a posal_queue_t always immediately follows this struct
 */
typedef struct gen_cntr_ext_out_port_t
{
   gu_ext_out_port_t          gu;                     /** Must be the first element */

   cu_ext_out_port_t          cu;

   module_cmn_md_list_t       *md_list_ptr;           /** metadata list ptr */
   module_cmn_md_list_t       *cached_md_list_ptr;    /** metadata is cached in this list when it is waiting to be rendered on master SPF domain */
   uint32_t                   num_frames_in_buf;      /**< current number of audio frames. once this reaches max_frames_per_buffer, buffer is
                                                           released*/
   uint32_t                   max_frames_per_buffer;  /**< max number of frames required in output buffer (Special case:
                                                           RD_SH_MEM_NUM_FRAMES_IN_BUF_AS_MUCH_AS_POSSIBLE)*/

   spf_msg_t                       out_buf_gpr_client;

   gen_cntr_buf_t                  buf;                    /**< This points to the out_bufmgr_node. */

   gen_topo_timestamp_t            next_out_buf_ts;
   ext_out_port_client_cfg_t       client_config;         /** < Configuration specific to offload use-cases */

   gen_cntr_ext_out_port_flags_t   flags;                  /**< Flags for the ext out port.*/

   uint8_t                         err_msg_overrun_err_count;     /*< Overrun count after the last printed for this port */
   uint8_t                         err_msg_overrun_prepare_buf_err_count;     /*< Overrun count after the last printed for this port */

   uint8_t                        bufs_num;                     /**< number of buffers that are valid in bufs_ptr - serves to check if data buf is v1 or v2 also */
   gen_cntr_buf_t *               bufs_ptr;                     /**< buffers that are available at ext out port */

   const gen_cntr_ext_out_vtable_t *vtbl_ptr;
} gen_cntr_ext_out_port_t;

/*
 * Container specific external control port structure.
 * In memory, 2 posal_queue_t's will immediately follow this structure.
 */
typedef struct gen_cntr_ext_ctrl_port_t
{
   gu_ext_ctrl_port_t      gu;        /** Must be the first element */

   cu_ext_ctrl_port_t      cu;

} gen_cntr_ext_ctrl_port_t;

/**
 * A posal_queue_t will always immediately follow this structure
 * */
typedef struct gen_cntr_int_ctrl_port_t
{
   gen_topo_ctrl_port_t      topo;        /** Must be the first element */

   cu_int_ctrl_port_t        cu;

} gen_cntr_int_ctrl_port_t;

typedef struct gen_cntr_fwk_module_t
{
   uint32_t                              module_id;              /**< real module ID of the placeholder module*/
   spf_list_node_t                       *cached_param_list_ptr; /**< gen_topo_cached_param_node_t*/
   const gen_cntr_fwk_module_vtable_t    *vtbl_ptr;              /**< vtable pointer */
} gen_cntr_fwk_module_t;

typedef struct gen_cntr_module_t
{
   gen_topo_module_t             topo;
   cu_module_t                   cu;
   gen_cntr_fwk_module_t   *     fwk_module_ptr;      /**< ptr to the fwk module struct*/
} gen_cntr_module_t;

typedef struct gen_cntr_stm_t
{
   posal_signal_t                     trigger_signal_ptr;        /**< non-NULL if signal triggered (after start)  */
   int32_t                            raised_interrupt_counter;  /**< Counter shared with module, incremented in module fast intr context  */
   int32_t                            processed_interrupt_counter;  /**< Counter incremented in process context of container  */
   stm_latest_trigger_ts_t            *st_module_ts_ptr;         /**< ptr from which we can read timestamp */
   uint32_t                           signal_miss_counter;  /**< Counter to keep track of number of signal miss encountered */
   void                               *stm_ts_ctxt_ptr; /**< ptr to the dev handle of different ep-modules */
   stm_get_ts_fn_ptr_t                update_stm_ts_fptr;   /**< function pointer to get the STM timestamp */
#ifdef ENABLE_SIGNAL_MISS_CRASH
   int32_t                            steady_state_interrupt_counter; /**< Counter incremented in process context similar to
                                                                           processed_interrupt_counter but reset after any command handling.
                                                                           used for detecting when signal miss occurs after counter is greater
                                                                           than 1000 */
#endif
} gen_cntr_stm_t;

/** This object is created for each async signal added to the container channel mask.
 *  This object is created and cached in container only if the ASYNC signal is enabled, which happens at graph start.
 *  Object is destroyed when the async signal is disabled, i.e once the module is stopped its destroyed.
*/
typedef struct gen_cntr_async_signal_t
{
   posal_signal_t             signal_ptr;           /**< async signal pointer  */
   uint32_t                   bit_index;            /**< Container's channel bit index thats assigned for this async signal   */
   gen_topo_module_t *        module_ptr;           /**< pointer to module that registered for this async signal  */

   fwk_extn_async_signal_callback_fn_ptr_t cb_fn_ptr;       /**< module callback function that container calls when async signal is received  */
   void                          *cb_context_ptr;           /**< context pointer passed as an argument to the callback function  */

} gen_cntr_async_signal_t;

typedef struct gen_cntr_flags_t
{
   uint32_t is_any_ext_in_mf_pending : 1; /**< TRUE only if any external input port MF is pending.
                                               Not applicable for Signal triggered containers.
                                               MF is handled immediately by dropping left prev data. */
   uint32_t is_thread_prio_bumped_up : 1; /**< temp flag which avoids bumping up priority if already done. */
} gen_cntr_flags_t;

/** instance struct of GEN_CNTR */
typedef struct gen_cntr_t
{
   cu_base_t         cu;                        /**< Container utility. Must be first element. */
   gen_topo_t        topo;
   uint32_t          total_flush_eos_stuck;     /**< total flushing EOSes stuck in the container. needed for voting */
   gen_cntr_stm_t    st_module;                 /**< Signal triggered module struct */
   gen_cntr_flags_t  flags;                     /**< Flags */
   uint32_t          prev_err_print_time_ms;
   uint32_t         *wait_mask_arr;             /**< wait mask for each parallel path. (me_ptr->cu.gu_ptr->num_parallel_paths)
                                                     this is bitmask where each bit corresponds to an external port.*/
   spf_list_node_t  *async_signal_list_ptr;     /**< list of async signals created for the container, node type is gen_cntr_async_signal_t */
} gen_cntr_t;

typedef struct gen_cntr_render_eos_cb_context_t
{
   gen_cntr_t *                         me_ptr;               /**< gen_cntr_t */
   module_cmn_md_tracking_payload_t    *eos_core_payload_ptr; /**< eos payload ptr, in case payload has to be sent out */
   uint32_t                             module_instance_id;   /**< module instance id, needed for eos rendered payload */
   uint32_t                             render_status;        /**< 1 - WR_SH_MEM_EP_EOS_RENDER_STATUS_RENDERED
                                                                   2 - WR_SH_MEM_EP_EOS_RENDER_STATUS_DROPPED */
   uint32_t                             is_flushing_eos;      /**< Specifies if the EOS is flushing or non flushing EOS
                                                                   0 -  non flushing EOS
                                                                   1 -  flushing eos */
} gen_cntr_render_eos_cb_context_t;

typedef struct gen_cntr_thread_relaunch_rest_handle_ctx_t
{
    uint32_t               stack_size;              /**< New stack size required for the thread relaunch.*/
    uint32_t               root_stack_size;         /**< New root stack size required for the thread relaunch.*/
    void *                 handle_rest_ctx_ptr;     /**< context pointer for further handling after thread relaunch. */
} gen_cntr_thread_relaunch_rest_handle_ctx_t;

// clang-format on

static inline uint32_t gen_cntr_get_all_input_port_mask(gen_cntr_t *me_ptr)
{
   return me_ptr->cu.all_ext_in_mask;
}

static inline uint32_t gen_cntr_get_all_output_port_mask(gen_cntr_t *me_ptr)
{
   return me_ptr->cu.all_ext_out_mask;
}

static inline void gen_cntr_listen_to_inputs(gen_cntr_t *me_ptr)
{
   uint32_t omask = gen_cntr_get_all_output_port_mask(me_ptr);
   uint32_t imask = gen_cntr_get_all_input_port_mask(me_ptr);
   cu_stop_listen_to_mask(&me_ptr->cu, omask);  // stop listening to output
   cu_start_listen_to_mask(&me_ptr->cu, imask); // start listening to input
}

static inline void gen_cntr_listen_to_outputs(gen_cntr_t *me_ptr)
{
   uint32_t imask = gen_cntr_get_all_input_port_mask(me_ptr);
   uint32_t omask = gen_cntr_get_all_output_port_mask(me_ptr);
   cu_stop_listen_to_mask(&me_ptr->cu, imask);  // stop listening to input
   cu_start_listen_to_mask(&me_ptr->cu, omask); // start listening to output
}

static inline void gen_cntr_listen_to_controls(gen_cntr_t *me_ptr)
{
   me_ptr->cu.curr_chan_mask = GEN_CNTR_CMD_BIT_MASK;
}

/**
 * given variable is masked and compared with value
 *
 * returns 1 if test succeeds
 */
static inline bool_t gen_cntr_test_bit_mask(uint32_t var, uint32_t mask, uint32_t val)
{
   return ((var & mask) == val);
}

/**
 * discontinuity  matters if we concatenate old and new data at the output buf.
 * In interrupt driven (GEN_CNTR), this doesn't matter
 */
static inline void gen_cntr_check_set_input_discontinuity_flag(gen_cntr_t *            me_ptr,
                                                               gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                               bool_t                  is_mf_pending)
{
   if (/*me_ptr->topo.flags.is_signal_triggered &&*/ me_ptr->topo.flags.is_signal_triggered_active)
   {
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                          DBG_HIGH_PRIO,
                          "Input discontinuity ignored for interrupt triggerred mode");
#endif
      return;
   }

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "Input discontinuity set for (0x%lX, 0x%lX). is discontinuity due to media format %d",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
                is_mf_pending);
#endif
   ext_in_port_ptr->flags.input_discontinuity = TRUE;

   if (is_mf_pending)
   {
      ext_in_port_ptr->flags.pending_mf      = TRUE;
      me_ptr->flags.is_any_ext_in_mf_pending = TRUE;
   }
}

void gen_cntr_st_fill_null_bursts(uint32_t num_zeros_to_fill, int8_t *dst_ptr);

/*do not call this util_ function directly, call only the wrapper gen_cntr_handle_fwk_events_in_data_path() or
 * gen_cntr_reconcile_and_handle_fwk_events or gen_cntr_handle_events_after_cmds*/
ar_result_t gen_cntr_handle_fwk_events_util_(gen_cntr_t                 *me_ptr,
                                             gen_topo_capi_event_flag_t *capi_event_flag_ptr,
                                             cu_event_flags_t           *fwk_event_flag_ptr);

/* Wrapper function to call the event-handler utility.
 * This function does not reconcile the event flags, which means if any event flag is asynchronously set in the parallel
 * command handling context then those events won't be handled here.
 * This function must only be called from the data-path processing context.
 * Since this function is meant for the data-path therefore it exits island only if event needs to be handled.
 * */
static inline ar_result_t gen_cntr_handle_fwk_events_in_data_path(gen_cntr_t *me_ptr)
{
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;
   cu_event_flags_t           *fwk_event_flag_ptr  = NULL;
   // In data-path event handling, we need to exclude any events set in the command context.
   // Therefore avoid reconciliation of the event flags.
   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
   CU_FWK_EVENT_HANDLER_CONTEXT

   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, &me_ptr->topo, FALSE /*do reconcile*/)
   CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(fwk_event_flag_ptr, &me_ptr->cu, FALSE /*do reconcile*/);

   if ((0 == fwk_event_flag_ptr->word) && (0 == capi_event_flag_ptr->word))
   {
      return AR_EOK;
   }

   // exit island before handling events
   gen_topo_exit_island_temporarily(&me_ptr->topo);

   return gen_cntr_handle_fwk_events_util_(me_ptr, capi_event_flag_ptr, fwk_event_flag_ptr);

   return AR_EOK;
}

/* Wrapper function to call the event-handler utility.
 * This function reconcile the event flags first, which means it also takes care of any events which are set
 * asynchronously from the command handling context.
 * This function can be called when event handling is required and you are not sure about the execution context.
 * This function first exits the island and then checks if there are any events to handle, therefore it must be avoided
 * from the steady state data-path processing context.
 * */
static inline ar_result_t gen_cntr_reconcile_and_handle_fwk_events(gen_cntr_t *me_ptr)
{
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;
   cu_event_flags_t           *fwk_event_flag_ptr  = NULL;

   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
   CU_FWK_EVENT_HANDLER_CONTEXT

   gen_topo_exit_island_temporarily(&me_ptr->topo);

   CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(fwk_event_flag_ptr, &me_ptr->cu, TRUE /*do reconcile*/)
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, &me_ptr->topo, TRUE /*do reconcile*/)

   if ((0 == fwk_event_flag_ptr->word) && (0 == capi_event_flag_ptr->word))
   {
      return AR_EOK;
   }
   return gen_cntr_handle_fwk_events_util_(me_ptr, capi_event_flag_ptr, fwk_event_flag_ptr);
}

/************* data utils *********************/
// empty buffer

static inline bool_t gen_cntr_is_ext_out_v2(gen_cntr_ext_out_port_t *ext_port_ptr)
{
   bool_t is_v2 = FALSE;
   if (ext_port_ptr->bufs_num)

   {
      is_v2 = TRUE;
   }

   return is_v2;
}

static inline bool_t gen_cntr_is_ext_in_v2(gen_cntr_ext_in_port_t *ext_port_ptr)
{
   bool_t is_v2 = FALSE;
   if ((SPF_DEINTERLEAVED_RAW_COMPRESSED == ext_port_ptr->cu.media_fmt.data_format) && ext_port_ptr->bufs_num)

   {
      is_v2 = TRUE;
   }

   return is_v2;
}

static inline bool_t gen_cntr_ext_out_port_has_buffer(gu_ext_out_port_t *gu_ext_out_port_ptr)
{
   // ext_in_port_ptr NULLness check has to be made outside.
   gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)gu_ext_out_port_ptr;

   bool_t has_data_buffer = FALSE;

   if (!gen_cntr_is_ext_out_v2(ext_out_port_ptr))
   {
      has_data_buffer = (NULL != ext_out_port_ptr->buf.data_ptr);
   }
   else
   {
      has_data_buffer = TRUE;
      for (uint32_t b = 0; b < ext_out_port_ptr->bufs_num; b++)
      {
         if (NULL == ext_out_port_ptr->bufs_ptr[b].data_ptr)
         {
            has_data_buffer = FALSE;
            break;
         }
      }
   }

   return has_data_buffer;
}

// data buffer: note MF is not populated in data_ptr
static inline bool_t gen_cntr_ext_in_port_has_data_buffer(gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   // ext_in_port_ptr NULLness check has to be made outside.
   gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)gu_ext_in_port_ptr;
   bool_t                  has_data_buffer = FALSE;

   if (!gen_cntr_is_ext_in_v2(ext_in_port_ptr))
   {
      has_data_buffer = (NULL != ext_in_port_ptr->buf.data_ptr);
   }
   else
   {
      has_data_buffer = TRUE;
      for (uint32_t b = 0; b < ext_in_port_ptr->bufs_num; b++)
      {
         if (NULL == ext_in_port_ptr->bufs_ptr[b].data_ptr)
         {
            has_data_buffer = FALSE;
            break;
         }
      }
   }

   return has_data_buffer;
}

static inline bool_t gen_cntr_is_pure_signal_triggered(gen_cntr_t *me_ptr)
{
   return !me_ptr->topo.flags.cannot_be_pure_signal_triggered;
}

static inline ar_result_t gen_cntr_check_and_vote_for_island_in_data_path(gen_cntr_t *me_ptr)
{
   // if it is island container and currently not is island then check if can go in island
   if (cu_is_island_container(&me_ptr->cu) && (PM_ISLAND_VOTE_EXIT == me_ptr->topo.flags.aggregated_island_vote) &&
       (0 == me_ptr->cu.active_non_island_num_buffers_for_ctrl_port)) // if non-island buffers are active then avoid
                                                                      // island entry
   {

      return gen_cntr_check_and_vote_for_island_in_data_path_(me_ptr);
   }
   return AR_EOK;
}

bool_t               gen_cntr_is_any_path_ready_to_process_util_(gen_cntr_t *me_ptr);
static inline bool_t gen_cntr_is_any_path_ready_to_process(gen_cntr_t *me_ptr)
{
   if (1 == me_ptr->cu.gu_ptr->num_parallel_paths)
   {
      return (0 == me_ptr->wait_mask_arr[0]) ? TRUE : FALSE;
   }
   else
   {
      return gen_cntr_is_any_path_ready_to_process_util_(me_ptr);
   }
}

static inline void gen_cntr_set_data_msg_out_buf_token(spf_msg_token_t *token)
{
    uint32_t token_data = (1 << GEN_CNTR_DATA_MSG_OUT_BUF_TOKEN_SHIFT) & GEN_CNTR_DATA_MSG_OUT_BUF_TOKEN_MASK;
    token->token_data = (token->token_data & ~GEN_CNTR_DATA_MSG_OUT_BUF_TOKEN_MASK) | token_data;
    return;
}

static inline bool_t gen_cntr_check_data_msg_out_buf_token_is_v2(spf_msg_token_t token)
{
    bool_t is_v2_buf = (0 != (token.token_data & GEN_CNTR_DATA_MSG_OUT_BUF_TOKEN_MASK));
    return is_v2_buf;
}

static inline ar_result_t gen_cntr_ext_input_setup_int_port_buf(gen_cntr_t             *me_ptr,
                                                                   gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                   gen_topo_input_port_t  *in_port_ptr)
{
   if (ext_in_port_ptr->vtbl_ptr->setup_topo_buf)
   {
      return ext_in_port_ptr->vtbl_ptr->setup_topo_buf(me_ptr, ext_in_port_ptr);
   }

   // note this call can return a fresh buf or a buf already at inplace-nblc-end.
   return gen_topo_check_get_in_buf_from_buf_mgr(&me_ptr->topo, in_port_ptr, NULL);
}

void gen_cntr_handle_st_overrun_at_post_process(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);

ar_result_t gen_cntr_mv_data_from_topo_to_ext_out_buf_npli_(gen_cntr_t *             me_ptr,
                                                            gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                            uint32_t                 total_len,
                                                            uint32_t                 max_empty_space);

ar_result_t gen_cntr_st_check_input_and_underrun(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);

ar_result_t gen_cntr_handle_process_events_and_flags(gen_cntr_t *             me_ptr,
                                                     gen_topo_process_info_t *process_info_ptr,
                                                     bool_t *                 mf_th_event_ptr,
                                                     gu_module_list_t *       mf_start_module_list_ptr);

ar_result_t gen_cntr_post_process_ext_output_port(gen_cntr_t                 *me_ptr,
                                                  gen_topo_process_info_t    *process_info_ptr,
                                                  gen_topo_process_context_t *pc_ptr,
                                                  gen_cntr_ext_out_port_t    *ext_out_port_ptr,
                                                  uint32_t                    out_port_index);

ar_result_t gen_cntr_timestamp_set_next_out_buf_ts(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
void        gen_cntr_reset_process_info(gen_cntr_t *me_ptr);

ar_result_t gen_cntr_ext_out_port_int_reset(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);

void     gen_cntr_clear_ext_out_bufs(gen_cntr_ext_out_port_t *ext_port_ptr, bool_t clear_max);
bool_t   gen_cntr_is_data_present_in_ext_in_bufs(gen_cntr_ext_in_port_t *ext_in_port_ptr);
bool_t   gen_cntr_is_data_present_in_ext_out_bufs(gen_cntr_ext_out_port_t *ext_out_port_ptr);
void     gen_cntr_get_ext_out_total_actual_max_data_len(gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                        uint32_t *               actual_data_len,
                                                        uint32_t *               max_data_len);
uint32_t gen_cntr_get_ext_in_total_actual_data_len(gen_cntr_ext_in_port_t *ext_in_port_ptr);
uint32_t gen_cntr_get_bytes_in_ext_in_for_md(gen_cntr_ext_in_port_t *ext_in_port_ptr);
uint32_t gen_cntr_get_bytes_in_ext_out_for_md(gen_cntr_ext_out_port_t *ext_out_port_ptr);

void gen_cntr_check_and_send_prebuffers_util_(gen_cntr_t *             me_ptr,
                                              gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                              spf_msg_data_buffer_t *  out_buf_ptr);

static inline void gen_cntr_check_and_send_prebuffers(gen_cntr_t *             me_ptr,
                                                      gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                      spf_msg_data_buffer_t *  out_buf_ptr)
{
   if (ext_out_port_ptr->cu.icb_info.is_prebuffer_sent)
   {
      return;
   }

   return gen_cntr_check_and_send_prebuffers_util_(me_ptr, ext_out_port_ptr, out_buf_ptr);
}

ar_result_t gen_cntr_workloop_async_signal_trigger_handler(cu_base_t *base_ptr, uint32_t bit_mask);
ar_result_t gen_cntr_fwk_extn_async_signal_enable(gen_cntr_t *me_ptr, gen_topo_module_t *module_ptr);
ar_result_t gen_cntr_fwk_extn_async_signal_disable(gen_cntr_t *me_ptr, gen_topo_module_t *module_ptr);

void gen_cntr_set_stm_ts_to_module(gen_cntr_t *me_ptr);

ar_result_t gen_cntr_clear_borrowed_ext_in_buffer_from_int_ports(gen_cntr_t             *me_ptr,
                                                                 gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                 gen_topo_input_port_t  *in_port_ptr,
                                                                 bool_t                  IS_FORCE_CLEAR);

ar_result_t gen_cntr_clear_borrowed_ext_out_buffer_from_int_ports(gen_cntr_t              *me_ptr,
                                                                  gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                                  gen_topo_output_port_t  *out_port_ptr);
#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef GEN_CNTR_I_H
