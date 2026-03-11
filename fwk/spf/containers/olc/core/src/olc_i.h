/**
 * \file olc_i.h
 * \brief
 *     This file contains internal definitions and declarations for the OLC.
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef OLC_I_H
#define OLC_I_H

#include "olc_cmn_utils.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus//

// VB:TODO :: includes are taken from PP. We can check if everything is needed

#include "apm_api.h"
#include "apm_sub_graph_api.h"
#include "apm_container_api.h"
#include "apm_module_api.h"
#include "apm_container_api.h"

#include "capi_mm_error_code_converter.h"
#include "container_utils.h"
#include "posal_thread.h"
#include "posal_thread_prio.h"
#include "spf_svc_utils.h"
#include "graph_utils.h"
#include "posal_power_mgr.h"

#include "gen_topo.h"
#include "gen_topo_capi.h"

// For eos.
#include "spf_ref_counter.h"
#include "wr_sh_mem_ep_api.h"

#include "olc_driver.h"
#include "offload_path_delay_api.h"

//#define VERBOSE_DEBUGGING 1
/* =======================================================================
OLC Macros
========================================================================== */

// static reserved bit mask for the system Queues
#define OLC_SYSTEM_Q_BIT_MASK 0x80000000

// static reserved bit mask for the command and response Queues
#define OLC_CMD_BIT_MASK 0x40000000

// size of command and response Queues
#define OLC_MAX_INP_DATA_Q_ELEMENTS 128
#define OLC_MAX_OUT_BUF_Q_ELEMENTS 128


/**
 * Defines for inserting posal Q's after the container structures
 * memory for the olc is organized as follows:

spl_cntr_t  posal_queue_t posal_queue_t posal_queue_t
            cmdq          sgm evt q     sgm respq
to reduce malloc overheads we do one single allocation
*/
#define OLC_SIZE (ALIGNED_SIZE_W_QUEUES(olc_t, 3))

/** memory for the ports are organized as follows:

port_type_t  posal_queue_t
To reduce malloc overheads we do one single allocation and put the queue right after the struct.
The following macros are to be used to get the size and position of the queue fields for the struct where needed for
resource allocation and initialization.

External ports for olc have 2 queues
*/
#define OLC_EXT_CTRL_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(olc_ext_ctrl_port_t, 1))
#define OLC_EXT_IN_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(olc_ext_in_port_t, 2))
#define OLC_EXT_OUT_PORT_SIZE_W_QS (ALIGNED_SIZE_W_QUEUES(olc_ext_out_port_t, 2))

#define OLC_CMDQ_OFFSET (ALIGN_8_BYTES(sizeof(olc_t)))
#define OLC_SGM_Q_OFFSET (ALIGNED_SIZE_W_QUEUES(olc_t, 1))

#define OLC_GET_EXT_IN_Q_ADDR(x) (CU_PTR_PUT_OFFSET(x, ALIGN_8_BYTES(sizeof(olc_ext_in_port_t))))
#define OLC_GET_EXT_IN_SGM_Q_ADDR(x) (CU_PTR_PUT_OFFSET(x, ALIGNED_SIZE_W_QUEUES(olc_ext_in_port_t, 1)))

#define OLC_GET_EXT_CTRL_PORT_Q_ADDR(x) (CU_PTR_PUT_OFFSET(x, ALIGN_8_BYTES(sizeof(olc_ext_out_port_t))))
#define OLC_GET_EXT_OUT_SGM_Q_ADDR(x) (CU_PTR_PUT_OFFSET(x, ALIGNED_SIZE_W_QUEUES(olc_ext_out_port_t, 1)))

/* =======================================================================
OLC Structure Definitions
========================================================================== */

/*
 * Container specific path delay information structure
 * */
typedef struct olc_cu_delay_info_t
{
   uint32_t           path_id;              /**< ID of the path */
   volatile uint32_t *delay_us_ptr;         /**< ptr to the delay variable created by APM for this external input*/
   uint32_t           module_delay;         /** < delay associated with the modules */
   uint32_t           ext_buf_delay;        /**< delay associated with the external buffers */
   uint32_t           write_ipc_delay;      /**< specifies the path delay on write path (due to IPC write buffers) */
   uint32_t           read_ipc_delay;       /**< specifies the path delay on read path (due to IPC read buffers) */
   uint32_t           satellite_path_delay; /**< specifies the path delay of the satellite graph */
} olc_cu_delay_info_t;

/*
 * Container specific external control port structure.
 * */
typedef struct olc_ext_ctrl_port_t
{
   gu_ext_ctrl_port_t gu; /** Must be the first element */
   cu_ext_ctrl_port_t cu;
} olc_ext_ctrl_port_t;

typedef struct olc_ext_in_port_flags_t
{
   uint32_t eof : 1;                 /**< end of frame coming from DSP client. */
   uint32_t flushing_eos : 1;        /**< flushing EoS : cleared only after eos comes out of all connected out ports.
                                                      nonflushing EOS is not stored as it doesn't block input. */
   uint32_t input_discontinuity : 1; /**< When there's timestamp discontinuity or discont due to other data messages,
                                        EOF is called to force-process, and then output is
                                        released when EOF comes out thru ext out ports.
                                        cleared after EOF is set on input port. */
   uint32_t pending_mf: 1;                  /**< when data path media format comes during process then it is marked pending
                                                and handled when processing is done. */
} olc_ext_in_port_flags_t;

/**
 * Container level ports are those that have external connection from the topo.
 * These have pointers to the internal module port .
 */
typedef struct olc_ext_in_port_t
{
   gu_ext_in_port_t gu;
   cu_ext_in_port_t cu;                  /**< Common container component. */
   bool_t           is_realtime_usecase; /**< Indicates if the use case is RT or FTRT. */
   uint32_t bytes_from_prev_buf; /**< multiple input buffers may be concatenated to form input to module. decoder -> raw
                            input => when bytes_from_prev_buf is nonzero, don't sync to input until decoder
                            stops returning need-more. pcm/61937 input => timestamp need to be adjusted for this
                            before assigning to input timestamp */
   module_cmn_md_list_t *  md_list_ptr; /**< all other metadata but EOS */
   olc_ext_in_port_flags_t flags;
   olc_buf_t               buf; /**< Data held by the external input port. Depending on the container,
                                     data could be buffered, or upstream clients may pass over their data_ptr. */
   gen_topo_timestamp_t  inbuf_ts;
   sdm_data_port_info_t *wdp_ctrl_cfg_ptr;
   bool_t                set_eos_to_sat_wr_ep_pending;
   bool_t                input_has_md;

   sdm_cnt_ext_data_buf_t sdm_wdp_input_data;
} olc_ext_in_port_t;

/**
 * Container level ports are those that have external connection from the topo.
 * These have pointers to the internal module port.
 */
typedef struct olc_ext_out_port_t
{
   gu_ext_out_port_t gu;                  /**< Topo component. */
   cu_ext_out_port_t cu;                  /**< Common container component. */
   bool_t            is_realtime_usecase; /**< Indicates if the use case is RT or FTRT. */
   uint32_t          bytes_from_prev_buf;
   /**< multiple input buffers may be concatenated to form input to module. decoder -> raw
input => when bytes_from_prev_buf is nonzero, don't sync to input until decoder
stops returning need-more. pcm/61937 input => timestamp need to be adjusted for this
before assigning to input timestamp */
   topo_media_fmt_t pending_media_fmt; /**< When a media format propagates to an external port, we set the pending
                                          media format based on the internal port. We then deliver any data we have
                                          downstream, and then apply the media format and send the media format
                                          downstream */
   olc_buf_t            buf;           /**< This points to the out_bufmgr_node. */
   gen_topo_timestamp_t out_buf_ts;
   uint32_t             num_frames_in_buf; /**< current number of audio frames. once this reaches max_frames_per_buffer,
                                                buffer is released*/
   bool_t                out_media_fmt_changed;
   bool_t                is_first_media_format;
   sdm_data_port_info_t *rdp_ctrl_cfg_ptr;
   module_cmn_md_list_t *md_list_ptr; /**< all other metadata but EOS */

   bool_t                 is_empty_buffer_available;
   sdm_cnt_ext_data_buf_t sdm_rdp_input_data;
   uint32_t               sat_graph_mf_changed;
   uint8_t *              sat_graph_mf_payload_ptr;
   uint32_t               required_buf_size;

} olc_ext_out_port_t;

typedef struct olc_module_t
{
   gen_topo_module_t topo;

   cu_module_t cu;

} olc_module_t;

typedef struct olc_serv_reg_event_handle_t
{
   uint32_t sysq_handler_table_size;

   spf_sys_util_handle_t *sys_util_ptr;  /**< Sys util handle */

} olc_serv_reg_event_handle_t;

/**
 * OLC container state structure.
 */
typedef struct olc_t
{
   cu_base_t cu;
   /**< Base container. Must be first element. */

   spgm_info_t spgm_info;
   /**<satellite processor graph management info */

   gen_topo_t topo;

   uint32_t nom_frame_duration_us;
   /**< frame duration on the container */

   uint32_t configured_frame_size_us; /**< Configured frame size in microseconds, derived from performance
                                           mode of sgs sent in graph_open. */
   void *olc_core_graph_open_cmd_ptr;

   uint32_t host_domain_id;

   spgm_cmd_rsp_node_t cmd_rsp_node;

   olc_serv_reg_event_handle_t serv_reg_handle;

   uint32_t satellite_up_down_status;
   /**< 0 specifies satellite is DOWN. 1 specifies satellite is UP (default) */

   uint32_t total_flush_eos_stuck;
   /**< total flushing EOSes stuck in the container. needed for voting */

} olc_t;

/* =======================================================================
OLC Function Declarations
========================================================================== */

/**--------------------------------- olc ----------------------------------*/
ar_result_t olc_parse_container_cfg(olc_t *me_ptr, apm_container_cfg_t *container_cfg_ptr);
ar_result_t olc_prepare_to_launch_thread(olc_t *              me_ptr,
                                         uint32_t *           stack_size,
                                         posal_thread_prio_t *priority,
                                         char *               thread_name,
                                         uint32_t             name_length);
void olc_handle_frame_length_n_state_change(olc_t *me_ptr, uint32_t period_in_us);

/**--------------------------------- utils ----------------------------------*/
ar_result_t olc_get_thread_stack_size(olc_t *me_ptr, uint32_t *stack_size);
ar_result_t olc_get_set_thread_priority(olc_t *me_ptr, int32_t *priority_ptr, bool_t should_set);
void olc_set_thread_priority(olc_t *me_ptr, uint32_t period_in_us);
ar_result_t olc_ext_in_port_reset(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr);
bool_t olc_is_stm(cu_base_t *base_ptr);
ar_result_t olc_ext_out_port_reset(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr);
ar_result_t olc_ext_out_port_basic_reset(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr);
ar_result_t olc_handle_ext_in_data_flow_begin(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr);
ar_result_t olc_handle_fwk_events(olc_t *me_ptr);

/** ------------------ **/
uint32_t olc_aggregate_ext_in_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
uint32_t olc_aggregate_ext_out_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr);
uint32_t olc_get_additional_ext_in_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
uint32_t olc_get_additional_ext_out_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr);


/**--------------------------- olc_cmd_handler ----------------------------*/
ar_result_t olc_gpr_cmd(cu_base_t *base_ptr);
ar_result_t olc_sgm_gpr_cmd(cu_base_t *base_ptr);
ar_result_t olc_graph_open(cu_base_t *base_ptr);
ar_result_t olc_graph_close(cu_base_t *base_ptr);
ar_result_t olc_graph_connect(cu_base_t *base_ptr);
ar_result_t olc_graph_disconnect(cu_base_t *base_ptr);
ar_result_t olc_destroy(olc_t *me_ptr);
ar_result_t olc_set_get_cfg(cu_base_t *base_ptr);
ar_result_t olc_graph_prepare(cu_base_t *base_ptr);
ar_result_t olc_graph_start(cu_base_t *base_ptr);
ar_result_t olc_graph_suspend(cu_base_t *me_ptr);
ar_result_t olc_graph_stop(cu_base_t *base_ptr);
ar_result_t olc_graph_flush(cu_base_t *base_ptr);
ar_result_t olc_destroy_container(cu_base_t *base_ptr);
ar_result_t olc_ctrl_path_media_fmt(cu_base_t *base_ptr);
ar_result_t olc_icb_info_from_downstream(cu_base_t *base_ptr);
ar_result_t olc_handle_peer_port_property_update_cmd(cu_base_t *me_ptr);
ar_result_t olc_handle_upstream_stop_cmd(cu_base_t *base_ptr);
/**--------------------------- olc_buf_utilities ----------------------------*/

ar_result_t olc_init_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr);
ar_result_t olc_deinit_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr);
ar_result_t olc_init_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr);
ar_result_t olc_deinit_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr);
ar_result_t olc_set_propagated_prop_on_ext_output(gen_topo_t *              topo_ptr,
                                                  gu_ext_out_port_t *       gu_out_port_ptr,
                                                  topo_port_property_type_t prop_type,
                                                  void *                    payload_ptr);
ar_result_t olc_set_propagated_prop_on_ext_input(gen_topo_t *              topo_ptr,
                                                 gu_ext_in_port_t *        gu_in_port_ptr,
                                                 topo_port_property_type_t prop_type,
                                                 void *                    payload_ptr);
ar_result_t olc_check_sg_cfg_for_frame_size(olc_t *me_ptr, spf_msg_cmd_graph_open_t *open_cmd_ptr);
void olc_destroy_ext_buffers(olc_t *me_ptr, olc_ext_out_port_t *ext_port_ptr, uint32_t num_bufs_to_keep);
void olc_return_back_out_buf(olc_t *me_ptr, olc_ext_out_port_t *ext_port_ptr);
ar_result_t olc_post_operate_flush(cu_base_t *base_ptr, spf_msg_t *cmd_msg);

/**--------------------------- olc_pm_handler ----------------------------*/
ar_result_t olc_handle_clk_vote_change(olc_t *           me_ptr,
                                       cu_pm_vote_type_t vote_type,
                                       bool_t            force_aggregate,
                                       uint32_t *        kpps_ptr,
                                       uint32_t *        bw_ptr);
ar_result_t olc_update_cntr_kpps_bw(olc_t *me_ptr, bool_t force_aggregate);
void olc_vote_pm_conditionally(olc_t *me_ptr, uint32_t period_us, bool_t is_at_least_one_sg_started);
ar_result_t olc_aggregate_kpps_bw(void *cu_ptr, uint32_t *kpps_ptr, uint32_t *bw_ptr);
ar_result_t olc_perf_vote(olc_t                      *me_ptr,
                          gen_topo_capi_event_flag_t *capi_event_flag_ptr,
                          cu_event_flags_t           *fwk_event_flag_ptr);
/**--------------------------- olc_rsp_handler ----------------------------*/

ar_result_t olc_graph_open_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
ar_result_t olc_graph_prepare_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
ar_result_t olc_graph_start_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
ar_result_t olc_graph_suspend_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
ar_result_t olc_graph_stop_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
ar_result_t olc_graph_flush_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
ar_result_t olc_graph_close_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
ar_result_t olc_graph_set_get_packed_cfg_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info_ptr, void * sat_rsp_pkt_ptr);
ar_result_t olc_graph_set_get_cfg_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
ar_result_t olc_graph_set_persistent_cfg_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
ar_result_t olc_graph_set_persistent_packed_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
ar_result_t olc_graph_event_reg_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);

ar_result_t olc_handle_port_data_thresh_change_event(void *ctx_ptr);

ar_result_t olc_input_media_format_received(void *            ctx_ptr,
                                            gu_ext_in_port_t *gu_ext_in_port_ptr,
                                            topo_media_fmt_t *media_fmt_ptr,
                                            cu_ext_in_port_upstream_frame_length_t *upstream_frame_len_ptr,
                                            bool_t            is_data_path);

ar_result_t olc_operate_on_ext_in_port(void *             base_ptr,
                                       uint32_t           sg_ops,
                                       gu_ext_in_port_t **ext_in_port_pptr,
                                       bool_t             is_self_sg);

ar_result_t olc_operate_on_ext_out_port(void *              base_ptr,
                                        uint32_t            sg_ops,
                                        gu_ext_out_port_t **ext_out_port_pptr,
                                        bool_t              is_self_sg);

ar_result_t olc_operate_on_subgraph(void *                     base_ptr,
                                    uint32_t                   sg_ops,
                                    topo_sg_state_t            sg_state,
                                    gu_sg_t *                  gu_sg_ptr,
                                    spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

ar_result_t olc_post_operate_on_subgraph(void *                     base_ptr,
                                         uint32_t                   sg_ops,
                                         topo_sg_state_t            sg_state,
                                         gu_sg_t *                  gu_sg_ptr,
                                         spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

ar_result_t olc_post_operate_on_ext_in_port(void *                     base_ptr,
                                            uint32_t                   sg_ops,
                                            gu_ext_in_port_t **        ext_in_port_pptr,
                                            spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

ar_result_t olc_ext_out_port_apply_pending_media_fmt_cmd_path(void *base_ptr, gu_ext_out_port_t *ext_out_port_ptr);

ar_result_t olc_output_bufQ_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index);
ar_result_t olc_input_dataQ_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index);
ar_result_t olc_recreate_ext_out_buffers(void *ctx_ptr, gu_ext_out_port_t *gu_out_port_ptr);
ar_result_t olc_apply_downgraded_state_on_output_port(cu_base_t *       cu_ptr,
                                                      gu_output_port_t *out_port_ptr,
                                                      topo_port_state_t downgraded_state);

ar_result_t olc_apply_downgraded_state_on_input_port(cu_base_t *       cu_ptr,
                                                     gu_input_port_t * in_port_ptr,
                                                     topo_port_state_t downgraded_state);

ar_result_t olc_flush_input_data_queue(olc_t *            me_ptr,
                                       olc_ext_in_port_t *ext_in_port_ptr,
                                       bool_t             keep_data_msg,
                                       bool_t             is_flush,
                                       bool_t             is_post_processing);
ar_result_t olc_create_ext_out_bufs(olc_t *             me_ptr,
                                    olc_ext_out_port_t *ext_port_ptr,
                                    uint32_t            num_out_bufs,
                                    uint32_t            new_buf_size);
ar_result_t olc_flush_output_data_queue(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr, bool_t is_flush);
ar_result_t olc_flush_cnt_output_data_queue(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr);

ar_result_t olc_copy_timestamp_from_input(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr);
ar_result_t olc_timestamp_set_next_out_buf_ts(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr, void *ts);

void olc_handle_failure_at_graph_open(olc_t *                   me_ptr,
                                      spf_msg_cmd_graph_open_t *open_cmd_ptr,
                                      ar_result_t               result,
                                      bool_t                    sg_open_state);

/**--------------------------- olc eos utils ----------------------------*/
ar_result_t olc_clear_eos(gen_topo_t *         topo_ptr,
                               void *               ext_inp_ref,
                               uint32_t             ext_inp_id,
                               module_cmn_md_eos_t *eos_metadata_ptr);

ar_result_t olc_process_eos_md_from_peer_cntr(olc_t *                me_ptr,
                                              olc_ext_in_port_t *    ext_in_port_ptr,
                                              module_cmn_md_list_t **md_list_head_pptr);


ar_result_t olc_cu_update_path_delay(cu_base_t *base_ptr, uint32_t path_id);

ar_result_t olc_create_module(gen_topo_t *           topo_ptr,
                              gen_topo_module_t *    module_ptr,
                              gen_topo_graph_init_t *graph_init_ptr);
ar_result_t olc_destroy_module(gen_topo_t *       topo_ptr,
                               gen_topo_module_t *module_ptr,
                               bool_t             reset_capi_dependent_dont_destroy);

ar_result_t olc_create_send_eos_md(olc_t *me_ptr, olc_ext_out_port_t *ext_out_port_ptr);

ar_result_t olc_aggregate_hw_acc_proc_delay(void *cu_ptr, uint32_t *hw_acc_proc_delay_ptr);

/**--------------------------- olc calib utils ----------------------------*/
ar_result_t olc_process_satellite_set_get_cfg(cu_base_t *                       base_ptr,
                                              spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr,
                                              uint32_t                          payload_size,
                                              bool_t                            is_set_cfg_msg);

ar_result_t olc_preprocess_set_get_cfg(cu_base_t *                       base_ptr,
                                       spf_msg_cmd_param_data_cfg_t *    cfg_cmd_ptr,
                                       spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr);

ar_result_t olc_process_container_set_get_cfg(cu_base_t *                       base_ptr,
                                              spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr,
                                              uint32_t                          msg_opcode);

/**--------------------------- olc service registry utils ----------------------------*/
ar_result_t olc_graph_open_error_rsp_h(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
ar_result_t olc_serv_reg_notify_init(olc_t *me_ptr);
ar_result_t olc_serv_reg_notify_deinit(olc_t *me_ptr);
ar_result_t olc_serv_reg_notify_register(olc_t *me_ptr, uint32_t satellite_proc_domain_id);

void olc_set_input_discontinuity_flag(olc_t *me_ptr, olc_ext_in_port_t *ext_in_port_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef OLC_I_H
