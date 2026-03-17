/**
 * \file spl_cntr_i.h
 * \brief
 *     This file contains internal definitions and declarations for the spl_cntr.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef SPL_CNTR_I_H
#define SPL_CNTR_I_H

// clang-format off

#include "spl_cntr_cmn_utils.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#include "apm_api.h"
#include "apm_sub_graph_api.h"
#include "apm_container_api.h"
#include "apm_module_api.h"

#include "apm_container_api.h"
#include "capi_mm_error_code_converter.h"
#include "container_utils.h"
#include "spf_svc_utils.h"
#include "posal_err_fatal.h"
#include "graph_utils.h"
#include "posal_power_mgr.h"
#include "posal_internal_inline.h"
#include "spl_topo.h"

// For eos.
#include "spf_ref_counter.h"
#include "wr_sh_mem_ep_api.h"

#include "gen_topo.h"
#include "gen_topo_buf_mgr.h"

/* =======================================================================
SPL_CNTR Macros
========================================================================== */

// Set SPL_CNTR_DEBUG_LEVEL to one of the below for different levels of debug logs.
// Note: These are only fwk-layer debugs. For topo-layer debugs, check topology header files

// Guidelines for prints that should outside of debug macros:
// - Indicate errors that must be addressed (fatal, unrecoverable, breaks use case, calibration errors, etc)
// - Misleading error messages (not always errors) should be rewritten to avoid confusion/mis-triaging.
// - If not errors:
//    - Not printed during steady state (unless error), and otherwise does not clog up the logs
//    - Give information that is useful to know in all or almost all scenarios
//          (graph shape changes, indicator that commands or calibration was received or sent, etc)
// - Warnings should be clearly specified as non errors.
#define SPL_CNTR_DEBUG_LEVEL_NONE 0

// Lowest debug level for prints.
// Guidelines for prints that should be put in DEBUG_LEVEL_1
// - Not an error (errors go outside of debug macros)
// - Not printed during steady state (unless error), but give information that might not always be
//   essential for triaging.
#define SPL_CNTR_DEBUG_LEVEL_1       1
#define SPL_CNTR_DEBUG_LEVEL_1_FORCE 1  // Indicate we are breaking guidelines intentionally.

// Low debug level for prints.
// Guidelines for prints that should be put in DEBUG_LEVEL_2
// - Not an error (errors go outside of debug macros)
// - Printed at most once per input/output trigger.
#define SPL_CNTR_DEBUG_LEVEL_2       2
#define SPL_CNTR_DEBUG_LEVEL_2_FORCE 2  // Indicate we are breaking guidelines intentionally.

// Medium debug level for prints.
// Guidelines for prints that should be put in DEBUG_LEVEL_3
// - Not an error (errors go outside of debug macros)
// - Printed at most once per module each input/output trigger.
// - Printed at most once per external port each input/output trigger.
#define SPL_CNTR_DEBUG_LEVEL_3       3
#define SPL_CNTR_DEBUG_LEVEL_3_FORCE 3  // Indicate we are breaking guidelines intentionally.

// High debug level for prints.
// Guidelines for prints that should be put in DEBUG_LEVEL_4
// - Not an error (errors go outside of debug macros)
// - Printed at most once per module's internal port each input/output trigger.
#define SPL_CNTR_DEBUG_LEVEL_4       4
#define SPL_CNTR_DEBUG_LEVEL_4_FORCE 4  // Indicate we are breaking guidelines intentionally.

// Highest debug level for prints.
// Guidelines for prints that should be put in DEBUG_LEVEL_5
// - Not an error (errors go outside of debug macros)
// - Printed more than once per module's port each input/output trigger.
#define SPL_CNTR_DEBUG_LEVEL_5       5
#define SPL_CNTR_DEBUG_LEVEL_5_FORCE 5  // Indicate we are breaking guidelines intentionally.

// To get debug prints, you must compile the code with USES_DEBUG_DEV_ENV as a flag.
#ifdef USES_DEBUG_DEV_ENV
   // Choose one of the above debug levels.
   //#define SPL_CNTR_DEBUG_LEVEL SPL_CNTR_DEBUG_LEVEL_NONE
   //#define SPL_CNTR_DEBUG_DM

   // Log (write to file using FILE utilities) data at input, while buffering to local buffer.
   // #define SPL_CNTR_LOG_AT_INPUT TRUE

   // Log (write to file using FILE utilities) data at output, while delivering an output buffer but before packing.
   // #define SPL_CNTR_LOG_AT_OUTPUT TRUE
#else
   // Don't change this line.
   #define SPL_CNTR_DEBUG_LEVEL SPL_CNTR_DEBUG_LEVEL_NONE
#endif

// #define SPL_CNTR_RESYNC_MASK            0x80000000
// #define SPL_CNTR_REC_BUF_MASK           0x40000000
// #define SPL_CNTR_TICK_MASK              0x20000000
// #define SPL_CNTR_VDS_RESPONSE_MASK      0x08000000

#define SPL_CNTR_VOICE_RESYNC_BIT_MASK 0x80000000
#define SPL_CNTR_VOICE_TIMER_TICK_BIT_MASK 0x40000000
#define SPL_CNTR_CMD_BIT_MASK 0x20000000


/**
 * Defines for inserting posal Q's after the container structures
 * memory for the sc is organized as follows:

spl_cntr_t  posal_queue_t posal_queue_t
            cmdq          ctrl_port_q
to reduce malloc overheads we do one single allocation
*/
#define SPL_CNTR_SIZE_W_2Q (ALIGNED_SIZE_W_QUEUES(spl_cntr_t, 2))

#define SPL_CNTR_CMDQ_OFFSET (ALIGN_8_BYTES(sizeof(spl_cntr_t)))
#define SPL_CNTR_CTRL_PORT_OFFSET (ALIGNED_SIZE_W_QUEUES(spl_cntr_t, 1))

#define SPL_CNTR_GET_CMDQ_ADDR(x) CU_PTR_PUT_OFFSET(x, SPL_CNTR_CMDQ_OFFSET)


/** memory for the ports are organized as follows:

port_type_t  posal_queue_t
To reduce malloc overheads we do one single allocation and put the queue right after the struct.
The following macros are to be used to get the size and position of the queue fields for the struct where needed for
resource allocation and initialization.

*/
#define SPL_CNTR_EXT_CTRL_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(spl_cntr_ext_ctrl_port_t, 1))
#define SPL_CNTR_EXT_IN_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(spl_cntr_ext_in_port_t, 1))
#define SPL_CNTR_EXT_OUT_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(spl_cntr_ext_out_port_t, 1))
#define SPL_CNTR_INT_CTRL_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(spl_cntr_int_ctrl_port_t, 0))

#define SPL_CNTR_EXT_CTRL_PORT_Q_OFFSET (ALIGN_8_BYTES(sizeof(spl_cntr_ext_ctrl_port_t)))
#define SPL_CNTR_INT_CTRL_PORT_Q_OFFSET (ALIGN_8_BYTES(sizeof(spl_cntr_int_ctrl_port_t)))

/* =======================================================================
SPL_CNTR Structure Definitions
========================================================================== */

typedef struct spl_cntr_input_port_t
{
   spl_topo_input_port_t topo;
   // If cu_input_port_t existed, it would go here.
} spl_cntr_input_port_t;

typedef struct spl_cntr_output_port_t
{
   spl_topo_output_port_t topo;
   // If cu_output_port_t existed, it would go here.
} spl_cntr_output_port_t;

/**
 * Container level ports are those that have external connection from the topo.
 * These have pointers to the internal module port .
 *
 * Please note that a posal_queue_t immediately follows this structure
 */
typedef struct spl_cntr_ext_in_port_t
{
   gu_ext_in_port_t gu;
   cu_ext_in_port_t cu;       /**< Common container component. */
   spl_topo_ext_buf_t  topo_buf; /**< Buffer used by topology during topo_process as external buffer. */
   uint32_t         held_data_msg_consumed_bytes_per_ch;  /**< How much data in the held data msg has already been copied over to the topo_buf. */
   bool_t           is_realtime_usecase;   /**< Indicates if the use case is RT or FTRT. */
   topo_media_fmt_t pending_media_fmt; /**< When we receive a media format on the data path, we first store it here. We
                                            can only apply it once all data at the old format has been consumed at the
                                            input port. */
   //Buffer sizing parameters
   uint32_t         nominal_samples;       /**< Nominal case samples per channel. Determines input fullness criteria for GPD. */
   uint32_t         next_process_samples;  /**< Samples per channel to be sent in next process call */
   bool_t           next_process_samples_valid; /**< Indicates if value of next_process_samples is valid and should be used */
   uint32_t         max_process_samples;   /**< Max samples per channel that need to be held at this port,
                                                determines local buffer size */
   int64_t          vptx_next_expected_ts;
   uint32_t         vptx_ts_zeros_to_push_us;
   bool_t           vptx_ts_valid;
} spl_cntr_ext_in_port_t;

/**
 * Container level ports are those that have external connection from the topo.
 * These have pointers to the internal module port.
 * Please note that a posal_queue_t immediately follows this structure
 */
typedef struct spl_cntr_ext_out_port_t
{
   gu_ext_out_port_t gu;                   /**< Topo component. */
   cu_ext_out_port_t cu;                   /**< Common container component. */
   spl_topo_ext_buf_t   topo_buf;             /**< Buffer used by topology during topo_process as external buffer. */
   int8_t *          temp_out_buf_ptr;     /**< pointer to temporary output buffer allocated in case of
                                                   unavailability of output buffer node */
   uint32_t         temp_out_buf_size;     /* size of memory allocated to temporary buffer */
   bool_t           is_realtime_usecase;   /**< indicates if the use case is RT or FTRT */
   topo_media_fmt_t pending_media_fmt;     /**< When a media format propagates to an external port, we set the pending
                                                media format based on the internal port. We then deliver any data we have
                                                downstream, and then apply the media format and send the media format
                                                downstream */
   bool_t sent_media_fmt; /**< We need to make sure we send the media format downstream at least once. If we don't send
                               during prepare, we should send on the data path even if media format is the same. */

   uint32_t delivery_buf_size; /* This is the minimum buffer size required to be filled, inorder to deliver the buffer. In case of
                                  fraction resampling if output is 44.1 samples, required delivery samples will be truncated to lower
                                  bound i.e 44. Its stored here inorder to avoid calculating buf size, every time we deliver. */
} spl_cntr_ext_out_port_t;

/*
 * Container specific external control port structure.
 * Please note that 2 posal_queue_t's immediately follow this structure
 * */
typedef struct spl_cntr_ext_ctrl_port_t
{
   gu_ext_ctrl_port_t      gu;   /** Must be the first element */

   cu_ext_ctrl_port_t      cu;

} spl_cntr_ext_ctrl_port_t;

/* Please note that a posal_queue_t immediately follows this structure */
typedef struct spl_cntr_int_ctrl_port_t
{
   gen_topo_ctrl_port_t      topo;        /** Must be the first element */

   cu_int_ctrl_port_t        cu;

} spl_cntr_int_ctrl_port_t;

// framework extension specific info in spl cntr
typedef struct cntr_fwk_extn_info_t
{
	posal_signal_t   proc_tick_signal_ptr;
	/**<Signal passed to voice delivery module to trigger processing if module supports VOICE DELIVERY fwk extn*/

	posal_signal_t   resync_signal_ptr;
	/**<Signal passed to voice delivery module to trigger resync handling if module supports VOICE DELIVERY fwk extn*/
}spl_cntr_fwk_extn_info_t;

/**
 * Information about the container's threshold. The container's threshold is the least common multiple of all
 * thresholds raised by modules, such that all threshold modules are given an exact multiple of their threshold.
 * This value must also be greater than or equal to the configured frame size. If it is smaller than the
 * configured frame size, we choose a larger multiple so that the configured frame size is met.
 */
typedef struct spl_cntr_threshold_data_t
{
   uint32_t configured_frame_size_us;          /**< Configured frame size in microseconds, derived from container property or from performance
                                                    mode of sgs sent in graph_open. */
   uint32_t configured_frame_size_samples;     /**< Configured frame size in samples, derived from container property sent in graph_open. */
   uint32_t aggregated_threshold_us;           /**< The aggregated framework threshold in microseconds. This is the
                                                    largest of the thresholds reported by ports. */
   uint32_t threshold_port_sample_rate;        /**< Sampling rate of the port which determined the fwk (aggregated)
                                                    threshold. */
   uint32_t threshold_in_samples_per_channel;  /**< Aggregated threshold in samples, stored to avoid rounding by division. */
} spl_cntr_threshold_data_t;


typedef enum cont_trigger_policy_t
{
	DEFAULT_TRIGGER_MODE = 0,
   VOICE_PROC_TRIGGER_MODE,
	OUTPUT_BUFFER_TRIGGER_MODE
} cont_trigger_policy_t;

typedef struct spl_cntr_module_t
{
   spl_topo_module_t        topo;
   cu_module_t              cu;

} spl_cntr_module_t;

typedef struct spl_cntr_t
{
   cu_base_t  cu;           /**< Base container. Must be first element. */
   spl_topo_t topo;         /**< Topology layer. */

   uint32_t *gpd_mask_arr;  /**< gpd mask for each parallel path. (me_ptr->cu.gu_ptr->num_parallel_paths)
                                 GPD mask is bitmask where each bit corresponds to an external port GPD.*/

   uint32_t gpd_mask; /**< Bitmask where each bit corresponds to a different sub-decision in the gpd. 1 means decision
                           is false, 0 means decision is true. */
   uint32_t gpd_optional_mask; /**< Both required (gpd_mask) and optional (gpd_optional_mask) bits will be used to check
                                    which triggers should be added back in the postprocessing decision. However, only
                                    required bits will be checked in the gpd.

                                    Optional input: Input is optional while ports are started but data is not yet flowing
                                                    (time from APM START or EOS -> first media format or data buffer recieved).
                                                    If all inputs are optional, the GPD should return FALSE.
                                    Optional output: Output ports are optional for RT output use cases. If all outputs
                                                     are optional, the GPD should return TRUE. We will drop data for that
                                                     frame in order to catch up on timing deadlines.
                                                     Optional output port buffer queues must be polled when the GPD returns
                                                     TRUE in case the GPD was triggered by another port but there is a buffer
                                                     in the optional output port. Otherwise we would unnecessarily drop data
                                                     on that port.
                                    */

   spl_cntr_threshold_data_t threshold_data;  /**< threshold related data */

   spl_cntr_fwk_extn_info_t fwk_extn_info; /**<Contains all framework extn related info for container*/

   uint32_t        total_flush_eos_stuck;     /**< total flushing EOSes stuck in the container. needed for voting */

   cont_trigger_policy_t          trigger_policy;
   bool_t is_voice_delivery_cntr;
} spl_cntr_t;

/**
 * Information needed for handling of the frame duration framework extension. Currently unsued. The only
 * handling for frame duration is to send the frame duration set param to these modules when the container
 * frame length changes. It's easy enough to loop through all modules and check the needs_frame_duration
 * flag without separately storing/maintaining that information. That's better than having to maintain a list
 * of all modules which raise this extension (graph_close handling, etc).
 *
 * typedef struct spl_cntr_fwk_frame_duration_info_t
 * {
 *   gu_module_list_t *module_list_ptr; // List of modules which raise the frame duration framework extension.
 * } spl_cntr_fwk_frame_duration_info_t;
 */


/* =======================================================================
SPL_CNTR Function Declarations
========================================================================== */

/**--------------------------------- spl_cntr ----------------------------------*/
ar_result_t spl_cntr_create(cntr_cmn_init_params_t *init_params_ptr, spf_handle_t **handle, uint32_t cntr_type);
ar_result_t spl_cntr_destroy(cu_base_t *base_ptr, void *temp);
ar_result_t spl_cntr_get_thread_stack_size(spl_cntr_t *me_ptr, uint32_t *stack_size);
ar_result_t spl_cntr_prepare_to_launch_thread(spl_cntr_t      *me_ptr,
                                         posal_thread_prio_t  *priority,
                                         char *                thread_name,
                                         uint32_t              name_length);
ar_result_t spl_cntr_parse_container_cfg(spl_cntr_t *me_ptr, apm_container_cfg_t *container_cfg_ptr);
ar_result_t spl_cntr_handle_proc_duration_change(cu_base_t *base_ptr);
ar_result_t spl_cntr_handle_cntr_set_calibration_ops_done(cu_base_t *base_ptr);
ar_result_t spl_cntr_handle_cntr_period_change(cu_base_t *base_ptr);
ar_result_t spl_cntr_handle_offload_voice_session_info(cu_base_t *base_ptr,  cntr_param_id_offload_voice_session_info_t* voice_session_info_ptr);

void spl_cntr_set_thread_priority(spl_cntr_t *me_ptr);
ar_result_t spl_cntr_set_cntr_frame_len_us(spl_cntr_t *me_ptr, icb_frame_length_t *fm_ptr);
ar_result_t spl_cntr_create_module(gen_topo_t *           topo_ptr,
                              gen_topo_module_t *    module_ptr,
                              gen_topo_graph_init_t *graph_init_ptr);
ar_result_t spl_cntr_destroy_module(gen_topo_t *       topo_ptr,
                               gen_topo_module_t *module_ptr,
                               bool_t             reset_capi_dependent_dont_destroy);
void spl_cntr_update_all_modules_info(spl_cntr_t *me_ptr);

/**---------------------------- spl_cntr_buf_util ------------------------------*/
ar_result_t spl_cntr_clear_output_buffer(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr);
uint32_t spl_cntr_calc_required_ext_in_buf_size(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr);
uint32_t spl_cntr_calc_required_ext_out_buf_size(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr);

static inline uint32_t spl_cntr_get_ext_out_buf_deliver_size(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   return ext_out_port_ptr->delivery_buf_size;
}

ar_result_t spl_cntr_check_resize_ext_in_buffer(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr, uint32_t required_size_bytes);
uint32_t spl_cntr_ext_in_get_free_space(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t spl_cntr_buffer_held_input_data(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr, uint32_t *data_needed_bytes_per_ch_ptr);
ar_result_t spl_cntr_alloc_temp_out_buf(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t spl_cntr_create_ext_out_bufs(spl_cntr_t *             me_ptr,
                                    spl_cntr_ext_out_port_t *ext_port_ptr,
                                    uint32_t            buf_size,
                                    uint32_t            num_out_bufs);
ar_result_t spl_cntr_get_output_buffer(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t spl_cntr_deliver_output_buffer(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t spl_cntr_handle_ext_buffer_size_change(void *ctx_ptr);
ar_result_t spl_cntr_handle_int_port_data_thresh_change_event(void *ctx_ptr);
ar_result_t spl_cntr_determine_update_cntr_frame_len_us_from_cfg_and_thresh(spl_cntr_t *me_ptr);
ar_result_t spl_cntr_init_after_getting_out_buf(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t spl_cntr_return_held_out_buf(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr);
bool_t spl_cntr_is_output_buffer_empty(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t spl_cntr_recreate_ext_out_buffers(void *ctx_ptr, gu_ext_out_port_t *gu_out_port_ptr);
ar_result_t spl_cntr_update_input_port_max_samples(gen_topo_t *topo_ptr, gen_topo_input_port_t* inport_ptr);
ar_result_t spl_cntr_initiate_duty_cycle_island_entry(cu_base_t *base_ptr);
ar_result_t spl_cntr_initiate_duty_cycle_island_exit(cu_base_t *base_ptr);
ar_result_t spl_cntr_dcm_topo_set_param(void *cu_ptr);

/**--------------------------- spl_cntr_cmd_handler ----------------------------*/
ar_result_t spl_cntr_set_get_cfg_util(cu_base_t                         *base_ptr,
                                      void                              *mod_ptr,
                                      uint32_t                           pid,
                                      int8_t                            *param_payload_ptr,
                                      uint32_t                          *param_size_ptr,
                                      uint32_t                          *error_code_ptr,
                                      bool_t                             is_set_cfg,
                                      bool_t                             is_register,
                                      spf_cfg_data_type_t                cfg_type,
                                      cu_handle_rest_ctx_for_set_cfg_t **pending_set_cfg_ctx_pptr);

ar_result_t spl_cntr_register_events_utils(cu_base_t *     base_ptr,
                                      gu_module_t *   gu_module_ptr,
                                      topo_reg_event_t *reg_event_payload_ptr,
                                      bool_t          is_register,
                                      bool_t *        capi_supports_v1_event_ptr);

ar_result_t spl_cntr_operate_on_subgraph(void *                    base_ptr,
                                    uint32_t                  sg_ops,
                                    topo_sg_state_t           sg_state,
                                    gu_sg_t *                 sg_ptr,
                                    spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

ar_result_t spl_cntr_operate_on_subgraph_async(void                      *base_ptr,
                                               uint32_t                   sg_ops,
                                               topo_sg_state_t            sg_state,
                                               gu_sg_t                   *gu_sg_ptr,
                                               spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

ar_result_t spl_cntr_post_operate_on_subgraph(void *                    base_ptr,
                                         uint32_t                  sg_ops,
                                         topo_sg_state_t           sg_state,
                                         gu_sg_t *                 gu_sg_ptr,
                                         spf_cntr_sub_graph_list_t *spf_sg_list_ptr);
ar_result_t spl_cntr_post_operate_on_connected_input(spl_cntr_t *                   me_ptr,
                                                gen_topo_output_port_t *  out_port_ptr,
                                                spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                                uint32_t                  sg_ops);
ar_result_t spl_cntr_operate_on_ext_in_port(void *             me_ptr,
                                       uint32_t           sg_ops,
                                       gu_ext_in_port_t **ext_in_port_pptr,
                                       bool_t             is_self_sg);
ar_result_t spl_cntr_post_operate_on_ext_in_port(void *                    base_ptr,
                                            uint32_t                  sg_ops,
                                            gu_ext_in_port_t **       ext_in_port_pptr,
                                            spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

ar_result_t spl_cntr_operate_on_ext_out_port(void *              me_ptr,
                                        uint32_t            sg_ops,
                                        gu_ext_out_port_t **ext_out_port_pptr,
                                        bool_t              is_self_sg);
ar_result_t spl_cntr_gpr_cmd(cu_base_t *base_ptr);
ar_result_t spl_cntr_graph_open(cu_base_t *base_ptr);
ar_result_t spl_cntr_set_get_cfg(cu_base_t *base_ptr);
ar_result_t spl_cntr_graph_prepare(cu_base_t *base_ptr);
ar_result_t spl_cntr_graph_start(cu_base_t *base_ptr);
ar_result_t spl_cntr_graph_stop(cu_base_t *base_ptr);
ar_result_t spl_cntr_graph_suspend(cu_base_t *base_ptr);
ar_result_t spl_cntr_graph_flush(cu_base_t *base_ptr);
ar_result_t spl_cntr_graph_close(cu_base_t *base_ptr);
ar_result_t spl_cntr_graph_connect(cu_base_t *base_ptr);
ar_result_t spl_cntr_graph_disconnect(cu_base_t *base_ptr);
ar_result_t spl_cntr_destroy_container(cu_base_t *base_ptr);
ar_result_t spl_cntr_ctrl_path_media_fmt(cu_base_t *base_ptr);
ar_result_t spl_cntr_icb_info_from_downstream(cu_base_t *base_ptr);
ar_result_t spl_cntr_apply_downgraded_state_on_output_port(cu_base_t *       cu_ptr,
                                      gu_output_port_t  *out_port_ptr,
                                      topo_port_state_t downgraded_state);
ar_result_t spl_cntr_apply_downgraded_state_on_input_port(cu_base_t *       cu_ptr,
                                      gu_input_port_t * in_port_ptr,
                                      topo_port_state_t downgraded_state);
ar_result_t spl_cntr_handle_ctrl_port_trigger_cmd(cu_base_t *base_ptr);
ar_result_t spl_cntr_handle_peer_port_property_update_cmd(cu_base_t *base_ptr);
ar_result_t spl_cntr_handle_upstream_stop_cmd(cu_base_t *base_ptr);

/**--------------------------- spl_cntr_data_handler ---------------------------*/
void spl_cntr_drop_input_data(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t spl_cntr_input_data_q_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index);
ar_result_t spl_cntr_output_buf_q_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index);
ar_result_t spl_cntr_handle_data_path_media_fmt(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t spl_cntr_check_and_process_audio(spl_cntr_t *me_ptr, uint32_t gpd_check_mask);
/**------------------------------ spl_cntr_eos_util ----------------------------*/
ar_result_t spl_cntr_handle_clear_eos_topo_cb(gen_topo_t *         topo_ptr,
                                         void *               ext_inp_ref,
                                         uint32_t             ext_inp_id,
                                         module_cmn_md_eos_t *eos_metadata_ptr);
ar_result_t spl_cntr_ext_in_port_set_eos(spl_cntr_t *                me_ptr,
                                    spl_cntr_ext_in_port_t *    ext_in_port_ptr,
                                    module_cmn_md_list_t * node_ptr,
                                    module_cmn_md_list_t **list_head_pptr,
                                    bool_t *               new_flushing_eos_arrived_ptr);
ar_result_t spl_cntr_ext_in_port_handle_data_flow_end(spl_cntr_t *            me_ptr,
                                                      spl_cntr_ext_in_port_t *ext_in_port_ptr,
                                                      bool_t                  create_eos_md,
                                                      bool_t                  is_flusing);
ar_result_t spl_cntr_ext_in_port_dfg_eos_left_port(gen_topo_t *topo_ptr, gu_ext_in_port_t* ext_in_port_ptr);


/**----------------------------- spl_cntr_event_util ---------------------------*/
ar_result_t spl_cntr_handle_algo_delay_change_event(gen_topo_module_t *module_ptr);
ar_result_t spl_cntr_handle_event_get_data_from_dsp_service(gen_topo_module_t *module_ptr, capi_event_info_t *event_info_ptr);
ar_result_t spl_cntr_handle_capi_event(gen_topo_module_t *module_ptr,
                                  capi_event_id_t    event_id,
                                  capi_event_info_t *event_info_ptr);
ar_result_t spl_cntr_handle_event_data_to_dsp_service(gen_topo_module_t *context_ptr, capi_event_info_t *event_info_ptr);
ar_result_t spl_cntr_handle_frame_done(spl_cntr_t *me_ptr, uint8_t path_index);
ar_result_t spl_cntr_handle_process_events_and_flags(spl_cntr_t *me_ptr);

ar_result_t spl_cntr_handle_fwk_events(spl_cntr_t *me_ptr, bool_t is_data_path);
/**--------------------------- spl_cntr_ext_port_util --------------------------*/
ar_result_t spl_cntr_ext_in_port_handle_data_flow_begin(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t spl_cntr_invalidate_pending_media_fmt(topo_media_fmt_t *media_fmt_ptr);
ar_result_t spl_cntr_init_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr);
uint32_t spl_cntr_get_in_queue_num_elements(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_port_ptr);
ar_result_t spl_cntr_init_ext_in_queue(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr);
ar_result_t spl_cntr_deinit_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr);
uint32_t spl_cntr_ext_in_port_get_buf_len(gu_ext_in_port_t *gu_ext_port_ptr, bool_t is_max);
bool_t spl_cntr_ext_in_port_has_unconsumed_data(gu_ext_in_port_t *gu_ext_port_ptr);
ar_result_t spl_cntr_input_media_format_received(void *            me_ptr,
                                            gu_ext_in_port_t *ext_in_port_ptr,
                                            topo_media_fmt_t *media_fmt_ptr,
											cu_ext_in_port_upstream_frame_length_t *upstream_frame_len_ptr,
                                            bool_t            is_data_path);
ar_result_t spl_cntr_init_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr);
void *spl_cntr_ext_out_port_get_ext_buf(gu_ext_out_port_t *gu_ext_out_port_ptr);
ar_result_t spl_cntr_init_ext_out_queue(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr);
ar_result_t spl_cntr_deinit_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr);
ar_result_t spl_cntr_ext_in_port_flush_local_buffer(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t spl_cntr_flush_input_data_queue(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr, bool_t keep_data_msg, bool_t buffer_data);

bool_t spl_cntr_ext_out_port_has_buffer(gu_ext_out_port_t* gu_ext_out_port_ptr);
ar_result_t spl_cntr_ext_in_port_apply_pending_media_fmt(spl_cntr_t *            me_ptr,
                                                    spl_cntr_ext_in_port_t *ext_port_ptr,
                                                    bool_t             is_data_path);
ar_result_t spl_cntr_ext_out_port_apply_pending_media_fmt(spl_cntr_t *             me_ptr,
                                                     spl_cntr_ext_out_port_t *ext_port_ptr,
                                                     bool_t              is_data_path);
ar_result_t spl_cntr_check_apply_ext_out_ports_pending_media_fmt(gen_topo_t *gen_topo_ptr, bool_t is_data_path);
ar_result_t spl_cntr_set_pending_out_media_fmt(gen_topo_t* gen_topo_ptr, gen_topo_common_port_t *cmn_port_ptr, gu_ext_out_port_t *ext_out_port_ptr);
ar_result_t spl_cntr_ext_out_port_apply_pending_media_fmt_cmd_path(void *base_ptr, gu_ext_out_port_t *ext_out_port_ptr);
ar_result_t spl_cntr_ext_in_port_push_timestamp_to_local_buf(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t spl_cntr_ext_in_port_re_push_data_msg_ts_to_local_buf(spl_cntr_t *            me_ptr,
                                                                  spl_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t spl_cntr_ext_in_port_adjust_timestamps(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t spl_cntr_ext_in_port_assign_timestamp(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr);

ar_result_t spl_cntr_set_ext_in_port_prev_actual_data_len(gen_topo_t *      topo_ptr,
                                                     gu_ext_in_port_t *ext_in_port_ptr,
                                                     uint32_t          new_prev_actual_data_len);

ar_result_t spl_cntr_ext_in_port_clear_timestamp_discontinuity(gen_topo_t *topo_ptr, gu_ext_in_port_t* ext_in_port_ptr);

ar_result_t spl_cntr_check_insert_missing_eos_on_next_module(gen_topo_t *           gc_topo_ptr,
                                                             gen_topo_input_port_t *gc_in_port_ptr);

ar_result_t spl_cntr_update_icb_info(gen_topo_t *topo_ptr);

/**------------------------------- spl_cntr_gpd --------------------------------*/
ar_result_t spl_cntr_allocate_gpd_mask_arr(spl_cntr_t *me_ptr);
bool_t spl_cntr_ext_in_port_has_enough_data(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
ar_result_t spl_cntr_update_cu_bit_mask(spl_cntr_t *me_ptr);
ar_result_t spl_cntr_query_topo_req_samples(spl_cntr_t* me_ptr);
ar_result_t spl_cntr_clear_topo_req_samples(gen_topo_t *topo_ptr, bool_t is_max);
ar_result_t spl_cntr_aggregate_handle_threshold_disabled(spl_cntr_t *me_ptr);
void spl_cntr_ext_in_port_update_gpd_bit(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t spl_cntr_update_gpd_and_cu_bit_mask(spl_cntr_t *me_ptr);
ar_result_t spl_cntr_update_gpd_and_process(spl_cntr_t *me_ptr);
void spl_cntr_update_gpd_mask_for_parallel_paths(spl_cntr_t *me_ptr);
bool_t spl_cntr_is_any_parallel_path_gpd_satisfied(spl_cntr_t *me_ptr);
/**-------------------------------- spl_cntr_pm --------------------------------*/
ar_result_t spl_cntr_handle_clk_vote_change(spl_cntr_t *   me_ptr,
									   cu_pm_vote_type_t vote_type,
                                       bool_t    force_vote,
                                       bool_t    force_aggregate,
                                       uint32_t *kpps_ptr,
                                       uint32_t *bw_ptr);
ar_result_t spl_cntr_update_cntr_kpps_bw(spl_cntr_t *me_ptr, bool_t force_aggregate);
ar_result_t spl_cntr_aggregate_kpps_bw(void *cu_ptr, uint32_t *kpps_ptr, uint32_t *bw_ptr);

/**---------------------------- spl_cntr_prop_utils ----------------------------*/
ar_result_t spl_cntr_set_propagated_prop_on_ext_output(gen_topo_t *            topo_ptr,
                                                  gu_ext_out_port_t *     gu_out_port_ptr,
                                                  topo_port_property_type_t prop_type,
                                                  void *                  payload_ptr);

ar_result_t spl_cntr_set_propagated_prop_on_ext_input(gen_topo_t *            topo_ptr,
                                                  gu_ext_in_port_t *     gu_in_port_ptr,
                                                  topo_port_property_type_t prop_type,
                                                  void *                  payload_ptr);

/**---------------------------- Fwk extns ----------------------------*/
ar_result_t spl_cntr_handle_fwk_extn_at_init(gen_topo_t *topo_ptr, gen_topo_module_t *module_context_ptr);
ar_result_t spl_cntr_handle_fwk_extn_at_deinit(gen_topo_t *topo_ptr, gen_topo_module_t *module_context_ptr);
bool_t is_module_with_ecns_extn_found(spl_cntr_t *me_ptr);


ar_result_t spl_cntr_aggregate_hw_acc_proc_delay(void *cu_ptr, uint32_t *hw_acc_proc_delay_ptr);
/* =======================================================================
Static Inline Functions
========================================================================== */

static inline uint32_t spl_cntr_num_ext_in_ports(spl_cntr_t *me_ptr)
{
   return me_ptr->topo.t_base.gu.num_ext_in_ports;
}

static inline uint32_t spl_cntr_num_ext_out_ports(spl_cntr_t *me_ptr)
{
   return me_ptr->topo.t_base.gu.num_ext_out_ports;
}

static inline bool_t spl_cntr_ext_in_port_local_buf_exists(spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
  return NULL != ext_in_port_ptr->topo_buf.buf_ptr;
}

static inline uint32_t spl_cntr_ext_in_port_bytes_per_sample(spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   return ext_in_port_ptr->cu.media_fmt.pcm.bits_per_sample >> 3;
}

static inline uint32_t spl_cntr_ext_in_port_sample_rate(spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   return ext_in_port_ptr->cu.media_fmt.pcm.sample_rate;
}


static inline void spl_cntr_remove_ext_in_port_from_gpd_mask(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
  cu_clear_bits_in_x(&me_ptr->gpd_mask, ext_in_port_ptr->cu.bit_mask);
  cu_clear_bits_in_x(&me_ptr->gpd_mask_arr[ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->path_index],
                     ext_in_port_ptr->cu.bit_mask);
}

static inline void spl_cntr_add_ext_in_port_from_gpd_mask(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
  cu_set_bits_in_x(&me_ptr->gpd_mask, ext_in_port_ptr->cu.bit_mask);
  cu_set_bits_in_x(&me_ptr->gpd_mask_arr[ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->path_index],
                     ext_in_port_ptr->cu.bit_mask);
}

static inline void spl_cntr_remove_ext_out_port_from_gpd_mask(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
  cu_clear_bits_in_x(&me_ptr->gpd_mask, ext_out_port_ptr->cu.bit_mask);
  cu_clear_bits_in_x(&me_ptr->gpd_mask_arr[ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->path_index],
                     ext_out_port_ptr->cu.bit_mask);
}

static inline void spl_cntr_add_ext_out_port_from_gpd_mask(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
  cu_set_bits_in_x(&me_ptr->gpd_mask, ext_out_port_ptr->cu.bit_mask);
  cu_set_bits_in_x(&me_ptr->gpd_mask_arr[ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->path_index],
                     ext_out_port_ptr->cu.bit_mask);
}

/*
 * Check if we have a pending media format. Using data format unknown to check
 * this.
 */
static inline bool_t spl_cntr_media_fmt_is_pending(topo_media_fmt_t *media_fmt_ptr)
{
   return SPF_UNKNOWN_DATA_FORMAT != media_fmt_ptr->data_format;
}

/**
 * Check if there is any data buffered in the external output port.
 */
static inline bool_t spl_cntr_ext_out_port_has_unconsumed_data(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_port_ptr)
{
   bool_t has_unconsumed_data = FALSE;
   spl_topo_output_port_t* int_out_port_ptr = (spl_topo_output_port_t*)ext_port_ptr->gu.int_out_port_ptr;
   if (ext_port_ptr->topo_buf.buf_ptr)
   {
      has_unconsumed_data = ext_port_ptr->topo_buf.buf_ptr[0].actual_data_len;
   }
   return has_unconsumed_data || (int_out_port_ptr->md_list_ptr);
}

static inline void spl_cntr_listen_to_controls(spl_cntr_t *me_ptr)
{
   me_ptr->cu.curr_chan_mask = SPL_CNTR_CMD_BIT_MASK;
}

uint32_t spl_cntr_aggregate_ext_in_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
uint32_t spl_cntr_aggregate_ext_out_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr);
uint32_t spl_cntr_get_additional_ext_in_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
uint32_t spl_cntr_get_additional_ext_out_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

// clang-format on
#endif // #ifndef SPL_CNTR_I_H
