#ifndef CONTAINER_UTILS_H_
#define CONTAINER_UTILS_H_

/**
 * \file container_utils.h
 *
 * \brief
 *     Common container framework code.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_msg.h"
#include "shared_lib_api.h"
#include "spf_macros.h"
#include "apm_cntr_if.h"
#include "cntr_cntr_if.h"
#include "posal_power_mgr.h"
#include "platform_cfg.h"
#include "container_utils_tgt.h"

#include "gpr_packet.h"
#include "gpr_api_inline.h"
#include "ar_guids.h"
#include "ar_spf_cmd_api.h"
#include "spf_svc_calib.h"
#include "graph_utils.h"
#include "topo_interface.h"
#include "topo_utils.h"
#include "cu_events.h"
#include "icb.h"

#include "cu_offload.h"
#include "cu_voice.h"
#include "cu_path_delay.h"
#include "cu_ctrl_port.h"
#include "cu_soft_timer_fwk_ext.h"
#include "cu_prof.h"
#include "cu_exit_island.h"
#include "cu_duty_cycle.h"
#include "cu_global_shmem_msg.h"
#include "posal_internal_inline.h"
#ifdef CONTAINER_ASYNC_CMD_HANDLING
#include "cu_async_cmd_handle.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

// clang-format off
/* =======================================================================
Macros
========================================================================== */
#define PATH_DELAY_DEBUGGING
//#define BUF_CNT_DEBUGGING  //enabling this always causes overhead for HW-EP containers.

#define UNITY_Q4 0x10

#define CU_MSG_PREFIX "CNTR:%08lX: "

#define CU_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, CU_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

#define CU_MSG_ISLAND(ID, xx_ss_mask, xx_fmt, ...) AR_MSG_ISLAND(xx_ss_mask, CU_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

#define GET_BASE_PTR(base_type, member_type, member_ptr)  (base_type *)(((int8_t *)member_ptr - offsetof(base_type, member_type)))

#define FRAME_SIZE_DONT_CARE     -1
#define FRAME_SIZE_1_MS           1
#define FRAME_SIZE_5_MS           5
#define FRAME_SIZE_10_MS         10

#define FRAME_LEN_1000_US        (1000)
#define FRAME_LEN_5000_US        (5000)
#define FRAME_LEN_10000_US       (10000)
#define FRAME_LEN_20000_US       (20000)
#define FRAME_LEN_40000_US       (40000)


#define LATENCY_VOTE_MIN         (40) //ms
#define LATENCY_VOTE_MAX         (0xFFFFFFFF)
#define LATENCY_VOTE_RT_FACTOR   (3)  // 3% tolerance
#define LATENCY_VOTE_NRT_FACTOR  (70) // 70% tolerance

// max elements is used for error checks, not mallocs. pre_alloc -> used for malloc.
#define CU_MIN_QUEUE_ELEMENTS_TO_PRE_ALLOC      (2)
#define CU_MAX_INP_DATA_Q_ELEMENTS              (128)
#define CU_MAX_OUT_BUF_Q_ELEMENTS               (128)
#define CU_MAX_INTENT_Q_ELEMENTS                (16)

#define CU_PTR_PUT_OFFSET(ptr, offset) ((void*) ((uint8_t*)ptr + offset))

/* =======================================================================
Struct Definitions
========================================================================== */

typedef struct cu_base_t         cu_base_t;
typedef struct cu_ext_in_port_t  cu_ext_in_port_t;
typedef struct cu_ext_out_port_t cu_ext_out_port_t;
typedef struct cu_cntr_vtable_t  cu_cntr_vtable_t;

typedef ar_result_t (*cu_queue_handler_t)(cu_base_t *me_ptr, uint32_t ch_bit_index);
typedef ar_result_t (*cu_msg_handler_func_t)(cu_base_t *me_ptr);
typedef ar_result_t (*cu_handle_rest_of_fn_t)(cu_base_t *me_ptr, void *ctx_ptr);

typedef struct cu_msg_handler_t
{
      uint32_t                opcode;     /**< Opcode of the message */
      cu_msg_handler_func_t   fn;         /**< function pointer to the handler */
} cu_msg_handler_t;

typedef struct cu_pm_info_flags_t
{
   uint8_t             is_request_async :1;         /**< Determines if pm request/releases are done synchronously */
   uint8_t             register_with_dcm :1;
   uint8_t             cntr_duty_cycling_allowed_subgraphs :1;
   uint8_t             module_disallows_duty_cycling :1;    /*<Buffering modules fullness in Duty Cycling containers before island entry*/
}cu_pm_info_flags_t;

/** Container power manager information struct */
typedef struct cu_pm_info_t
{
   posal_pm_handle_t      pm_handle_ptr;           /**< Pointer to PM handle */
   posal_pm_register_t    register_info;           /**< PM register information */
   cu_pm_info_flags_t     flags;
   uint32_t               cntr_kpps;               /**< KPPS contribution of the container framework. */
   uint32_t               cntr_bw;                 /**< BW contribution of the container framework. Bytes per sec */
   uint32_t               prev_kpps_vote;          /**< total KPPS of the decoder. */
   uint64_t               prev_floor_clock_vote;   /**< previous floor clock vote in hz*/
   uint32_t               prev_bw_vote;            /**< previous bw vote in bytes per sec inclu capi vote */
   uint32_t               prev_latency_vote;       /**< previous latency vote in us */
   uint32_t               weighted_kpps_scale_factor_q4;/**< weighted KPPS scale factor in q4, non-unity (0x10) indicates faster or slower processing. */

} cu_pm_info_t;

typedef struct cu_delay_info_t
{
   uint32_t              path_id;                /**< ID of the path */
   volatile uint32_t     *delay_us_ptr;          /**< ptr to the delay variable created by APM for this external input*/
} cu_delay_info_t;

typedef struct cu_handle_rest_ctx_for_set_cfg_t
{
   int8_t *           param_payload_ptr;           /**< param_payload_ptr at which the set-cfg processing is swapped with thread re-launch (points to mem after apm_module_param_data_t) */
   void *             module_ptr;                  /**< module pointer (gen_topo) */
   ar_result_t        overall_result;              /**< aggregated result */
} cu_handle_rest_ctx_for_set_cfg_t;

typedef struct cu_voice_cntr_event_flags_t
{
   uint32_t did_kpps_change : 1;
   uint32_t did_bw_change : 1;
   uint32_t did_frame_size_change : 1;
   uint32_t did_algo_delay_change : 1;
   uint32_t did_hw_acc_proc_delay_change : 1;
} cu_voice_cntr_event_flags_t;

typedef struct cu_voice_info_t
{
    cu_voice_cntr_event_flags_t event_flags;            /**< Flag bitfields to indicate the change in cntr proc params
                                                            Used to raise events to vcpm */
    uint32_t      safety_margin_us;
    bool_t        is_satellite_voice_sid;               /** specifies the container is operating in satellite SPF */
} cu_voice_info_t;

typedef struct cu_offload_info_t
{
   bool_t   is_satellite_container;      /**< specifies if the container is operating in master or satellite domain*/
   bool_t   is_satellite_ep_container;   /** < specified if the container has RD/WR EP module */
   uint32_t client_id;                   /**< specifies the parent container id */
} cu_offload_info_t;

#define CU_FWK_EVENT_FLAG_TYPE uint32_t

/** container level flags */
typedef union cu_event_flags_t
{
   struct
   {
       CU_FWK_EVENT_FLAG_TYPE sg_state_change : 1;                /**< Indicates if any self or peer SG state has changed to started, suspended or stopped. */
       CU_FWK_EVENT_FLAG_TYPE cntr_run_state_change : 1;          /**< when first sg is started, last SG is stopped/suspended. */
       CU_FWK_EVENT_FLAG_TYPE kpps_bw_scale_factor_change : 1;    /**< KPPS, BW or KPPS scale factor have changed */
       CU_FWK_EVENT_FLAG_TYPE rt_ftrt_change : 1;                 /**< RT FTRT state of the ports has changed due to prop from DS/US/intra-cntr */
       CU_FWK_EVENT_FLAG_TYPE port_state_change : 1;              /**< port state change due to propagation from DS (if so kpps/bw needs to be reaggregated) */
       CU_FWK_EVENT_FLAG_TYPE dfs_change : 1;                     /**< Data flow state has changed */
       CU_FWK_EVENT_FLAG_TYPE proc_dur_change : 1;                /**< for handling processing duration change */
       CU_FWK_EVENT_FLAG_TYPE frame_len_change : 1;               /**< frame length of the container changed */
       CU_FWK_EVENT_FLAG_TYPE need_to_handle_dcm_req_for_unblock_island_entry:1;    /**< to handle duty cycling manager request for unblock island entry */
       CU_FWK_EVENT_FLAG_TYPE need_to_handle_dcm_req_for_island_exit:1;         /**< to handle duty cycling manager request for island exit */
       CU_FWK_EVENT_FLAG_TYPE reevaluate_island_vote :1;          /**< flag is marked if island vote needs to be reevaluated. */
       CU_FWK_EVENT_FLAG_TYPE upstream_frame_len_change : 1;               /**< Upstream containers frame length changed */
       CU_FWK_EVENT_FLAG_TYPE port_flushed :1;                    /**< flag is marked if flush is done on any external port of a container. */
   };
   CU_FWK_EVENT_FLAG_TYPE word;
} cu_event_flags_t;

typedef struct cu_timer_node_t
{
   uint32_t       module_iid;
   uint32_t       timer_id;
   uint32_t       channel_bit_mask;
   posal_signal_t timer_signal_ptr;
   posal_timer_t  one_shot_timer;
} cu_timer_node_t;

typedef struct cu_flags_t
{
   uint32_t        is_cntr_proc_dur_set_paramed : 1; /**< true when cntr_proc_duration is set-param'ed from external entity such as VCPM */
   uint32_t        is_cntr_period_set_paramed : 1;   /**< true when period_us is set-param'ed from external entity such as VCPM */
   uint32_t        is_cntr_started : 1;              /**< cntr is started if at least one SG is in start state */
   uint32_t        apm_cmd_context : 1;              /**< Indicates if in apm command handling context, this is to ensure that
                                                          any pm_server voting in this context is non-blocking. */
   uint32_t        is_real_time: 1;                  /**< flag to indicate if any external port of the container is real time or not. */
} cu_flags_t;

typedef enum
{
   CU_PM_REQ_KPPS_BW,
   CU_PM_REL_KPPS_BW,
   CU_PM_REL_KPPS_ONLY,
} cu_pm_vote_type_t;
typedef struct cu_module_t
{
   spf_list_node_t     *event_list_ptr;       /**< Module events list */
} cu_module_t;
/**
 * @brief The base structure of a container
 *
 * Only the container owning this memory must use the elements of this struct
 * Other containers or APM must not use.
 */
typedef struct cu_base_t
{
   spf_handle_t           spf_handle;              /**< Handle to this container. Must be the first element. q_ptr points to response queue (no longer supported).*/
   spf_cmd_handle_t       cmd_handle;             /**< Memory for the cmd_handle_ptr referred to in spf_handle. */
   gu_t *                 gu_ptr;
   const cu_cntr_vtable_t *cntr_vtbl_ptr;          /**< Pointer to a function table containing container public functions. */
   const topo_cu_vtable_t *topo_vtbl_ptr;          /**< Pointer to a function table containing topology public functions. */
   const topo_cu_island_vtable_t *topo_island_vtbl_ptr; /**< Pointer to a Island function table containing topology island public functions. */
   void *                 topo_ptr;               /**< Opaque pointer to the topology, to be passed into public topo functions. */
   uint16_t               ext_in_port_cu_offset;  /**< Offset of cu component from gu, used to derive cu from gu_ptr. */
   uint16_t               ext_out_port_cu_offset; /**< Offset of cu component from gu, used to derive cu from gu_ptr. */
   uint16_t               ext_ctrl_port_cu_offset;/**< Offset of cu component from gu, used to derive cu from gu_ptr. */
   uint16_t               int_ctrl_port_cu_offset;/**< Offset of cu component from gu, used to derive cu from gu_ptr. */
   uint16_t               module_cu_offset;       /**< Offset of cu component from gu, used to derive cu from gu_ptr. */
   uint32_t               cntr_type;
   uint32_t               position;
   uint32_t               proc_domain;
   uint32_t               actual_stack_size;       /**< Actual size of the thread stack */
   uint32_t               configured_stack_size;
   uint32_t               root_thread_stack_size;
   int32_t                configured_thread_prio;  /**< Thread priority configured by the client */
   uint32_t               configured_sched_policy; /**< scheduling policy. Mainly for Linux */
   uint32_t               configured_core_affinity;/**< CPU Core affinity policy. Mainly for Linux */
   posal_channel_t        channel_ptr;
   posal_channel_t        gp_channel_ptr;           /**< General purpose channel */
   posal_signal_t         gp_signal_ptr;            /**< General purpose signal */
   POSAL_HEAP_ID          heap_id;
   uint32_t               curr_chan_mask;          /**< Current channel mask */
   uint32_t               available_bit_mask;      /**< Available bits in the bit mask of the channel */
   uint32_t               all_ext_in_mask;         /**< All external inputs mask */
   uint32_t               all_ext_out_mask;        /**< All external inputs mask */
   cu_queue_handler_t     qftable[32];
   spf_msg_t              cmd_msg;                 /**< Command message */
   const cu_msg_handler_t *cmd_handler_table_ptr;  /**< Pointer to a function table for command handling */
   uint16_t               cmd_handler_table_size;
   posal_thread_t         thread_id_to_exit;       /**< ID of thread need to be destroyed. a thread exists if this matches its ID */
   cu_handle_rest_of_fn_t handle_rest_fn;          /**< Thread might have re-launched after handling a command partially.
                                                        If so, this function ptr can be set to handle the rest of the functionality.
                                                        this is set only when thread is re-launched.*/
   void                   *handle_rest_ctx_ptr;    /**< Any data needed to complete the incomplete handling. The interpreter can decide how to
                                                        handle this pointer. this is set only when thread is re-launched. */
   cu_pm_info_t           pm_info;                 /**< power manager information */
   posal_channel_t        ctrl_channel_ptr;        /**< Current control channel */
   uint32_t               curr_ext_ctrl_chan_mask; /**< Current control channel mask for external ports */
   uint32_t               island_ctrl_chan_mask;   /**< Channels masks for the external control ports which are allocated in island heap. */
   uint32_t               available_ctrl_chan_mask;/**< Available bits in the bit mask of the control commands channel */
   uint16_t               active_non_island_num_buffers_for_ctrl_port; /**< number of non-island control port buffers held by module.*/
   icb_frame_length_t     conf_frame_len;          /**< Frame length configured as container property. */
   icb_frame_length_t     cntr_frame_len;          /**< Final container frame length.
                                                        First decided based on the threshold module in the container if any.
                                                        If no threshold modules then decided based on the conf_frame_len (configured as container property).
                                                        And if neither of those then at last decided based on the any one Subgraph's perf-mode property. */
   uint32_t               period_us;               /**< us, period of the container, in microseconds. Usually same as cntr_frame_len except for voice which has 40 ms etc for CDRX40. */
   uint32_t               cntr_proc_duration;      /**< us, processing duration of the container per frame = scaled frame_size*/
   cu_flags_t             flags;                   /**< general flags */
   cu_voice_info_t        *voice_info_ptr;         /**< voice call information */
   cu_offload_info_t      *offload_info_ptr;       /**< offload specific information */
   spf_list_node_t        *delay_path_list_ptr;    /**< object = cu_delay_info_t */
   spf_list_node_t        *event_list_ptr;         /**< List of events registered with the container instance
                                                        object = tu_event_info_t */
   spf_list_node_t        *timer_list_ptr;         /**<Pointer to list of modules and timer info if container has module that supports SOFT_TIMER fwk extn*/

   spf_handle_t           cntr_cmn_imcl_handle;    /*< Queue which is common to all the internal control port messages within this container*/

   /*Following Event Flags are hidden to prevent the direct access. These can be accessed using utility functions and Macros. */
   CU_FWK_EVENT_FLAG_TYPE fwk_evt_flags_;          /**< Main Events flags: these are set synchronous to the data-path processing thread. Any event handling is also done based on this. */

#ifdef CONTAINER_ASYNC_CMD_HANDLING
   CU_FWK_EVENT_FLAG_TYPE async_fw_evt_flags_;     /**< Async Event flags: these are set asynchronous to the data-path processing thread temporarily. This must be synchronously reconciled with the Main Event before event-handling. */

   cu_async_cmd_handle_t *async_cmd_handle;        /*< Async command handler in background thread. */
#endif
} cu_base_t;

typedef struct cu_ext_port_prop_info_t
{
   struct
   {
      uint32_t    prop_enabled : 1;          /**< is propagation enabled. TRUE for peer containers. FALSE for GPR clients (except offload when event is registered) */
      uint32_t    is_us_rt : 1;              /**< value of upstream real time property.
                                                    for output port, it is the value which was last propagated to downstream.
                                                    for input port, it is the value which was last received from upstream.*/
      uint32_t    is_ds_rt : 1;             /**< value of downstream real time property.
                                                    for output port, it is the value which was last received from downstream.
                                                    for input port, it is the value which was last propagated to upstream.*/
      uint32_t    is_rt_informed : 1;        /**< whether real time property is propagated at least once
                                                    for output port, true if "is_us_rt" is propagated once to downstream.
                                                    for input port, true if "is_ds_rt" is propagated once to upstream.*/
      uint32_t    is_state_informed : 1;     /**< whether state info is propagated at least once */
      uint32_t    did_inform_us_of_frame_len_and_var_ip : 1;    /**< whether Upstream was informed about the frame length & change in variable input */
   };
   topo_port_state_t port_state;             /**< value of last propagated state */
   /** Upstream state is never propagated to downstream. When DS propagates stop to US, US acks back through this func. SPF_MSG_CMD_UPSTREAM_STOPPED_ACK. */
   ar_result_t (*prop_us_state_ack_to_ds_fn)(cu_base_t *base_ptr, gu_ext_out_port_t *ext_out_port_ptr, topo_port_state_t ds_state);
   ar_result_t (*prop_us_prop_to_ds_fn)(cu_base_t *base_ptr, gu_ext_out_port_t *ext_out_port_ptr, spf_msg_peer_port_property_update_t *prop_ptr);
   ar_result_t (*prop_ds_prop_to_us_fn)(cu_base_t *base_ptr, gu_ext_in_port_t *ext_in_port_ptr, spf_msg_peer_port_property_update_t *prop_ptr);
} cu_ext_port_prop_info_t;

typedef struct cu_ext_in_port_icb_info_t
{
   icb_flags_t        flags;
   /**< for input ports, is_real_time is about downstream. */
} cu_ext_in_port_icb_info_t;


typedef struct cu_ext_in_port_upstream_frame_length_t
{
    uint32_t sample_rate;
    /**< Sample rate in Hz. */

    uint32_t frame_len_samples;
    /**< Frame length in samples. */

    uint32_t frame_len_us;
    /**< Frame length in microseconds. */

    uint32_t frame_len_bytes;
    /**< Frame length in bytes.
     * For raw compr and deint raw compr data,
     * this represents the total size of the buffer- including all channels */
} cu_ext_in_port_upstream_frame_length_t;

/**
 * Container level ports are those that have external connection from the topo.
 * These have pointers to the internal module port
 *
 */
typedef struct cu_ext_in_port_t
{
   topo_port_state_t          connected_port_state;  /**< State of the connected port. connected SG maybe same as SG in which
                                                          module owning this port exists.
                                                          State is not propagated in control path from US to DS. Hence this cannot be used for down-grading.
                                                          This can be only used when state is backward propagated - to ensure no inf loop in flush.
                                                          see gen_cntr_apply_downgraded_state_on_input_port*/
   uint32_t                   bit_mask;              /**< bit mask in the channel - applicable for external ports only */
   spf_msg_t                  input_data_q_msg;

   topo_media_fmt_t           media_fmt;             /**< media format from upstream */
   cu_ext_in_port_upstream_frame_length_t  upstream_frame_len;  /**< frame len info from upstream informed via spf media fmt msg*/

   cu_ext_in_port_icb_info_t  icb_info;              /**< inter-container buffering info. is_real_time is about upstream */
   cu_ext_port_prop_info_t    prop_info;             /**< info about propagated properties. */
   uint32_t                   id;                    /**< Unique ID from cu_unique_id. Used to ignore render/drop eos callbacks whose
                                                          reference port was deleted. */
   spf_list_node_t*           prebuffer_queue_ptr;   /**< prebuffer Q to hold the pre-buffers during first frame processing / threshold disabled by sync.*/

   uint32_t                   preserve_prebuffer :1; /**< Flag to indicate if prebuffer should be preserved for this port. */
#ifdef BUF_CNT_DEBUGGING
   uint32_t                   buf_recv_cnt;          /**< data buffer recvd count */
   uint32_t                   buf_done_cnt;          /**< data buffer done count */
#endif
} cu_ext_in_port_t;

/** Output port ICB information struct */
typedef struct cu_ext_out_port_icb_info_t
{
   icb_calc_output_t    icb;
   /**< Inter-container buffering calculated output */

   icb_flags_t          flags;
   /**< upstream flags : is_real_time is about whether any upstream is RT*/

   bool_t               disable_one_time_pre_buf;
   /**< Flag to indicate if pre-buffer is disabled by this container (upstream) */

   bool_t               is_prebuffer_sent;
   /**< Flag to indicate if the prebuffer was sent or not. This flag should be reset only
        when there data flow becomes at-gap/port stopped. This flag should not be reset when ICB info is changed.

          TRUE  - Indicates 1) Buffer was sent atleast once since the data flow started.
                            2) Data flow started && num_reg_prebufs == 0  */

   /** below info comes from downstream message to upstream */
   uint32_t             ds_sid;
   /**< Downstream Scenario ID. */

   icb_frame_length_t   ds_frame_len;
   /**< Period at which the downstream triggers processing. if all are zero, then downstream didn't inform yet. */

   uint32_t             ds_period_us;
   /**< Period at which the downstream triggers processing. if all are zero, then downstream didn't inform yet. */

   icb_flags_t          ds_flags;
   /**< downstream flags */

} cu_ext_out_port_icb_info_t;

/**
 * Flags for the cu external output port.
 */
typedef struct cu_ext_out_port_flags_t
{
   uint32_t    upstream_frame_len_changed: 1; /**< Flag to indicate that the upstream max frame len changed
                                                   For pcm and packetized data this refers to change in container frame len
                                                   For raw compr/deint raw compr data this refers to change in output threshold */

   uint32_t    is_pcm_unpacked:2; /**< Flag added to optimizes the steady check i.e if the interanl output has PCM and unpacked v1/v2.
                                       The reason to have a flag in ext output similar to internal output is, if MF changes runtime, ext
                                       out may have unpacked data but the internal output could have changed to intereleaved. Hence we
                                       cannot rely on the internal outputs media format to check if the data is unpacked.

                                       0 - not unpacked, 1 - TOPO_DEINTERLEAVED_UNPACKED, 2 - TOPO_DEINTERLEAVED_UNPACKED_V2 */
} cu_ext_out_port_flags_t;


typedef struct cu_ext_out_port_t
{
   topo_port_state_t          connected_port_state;      /**< state of the connected subgraph. connected SG maybe same as SG in which
                                                          module owning this port exists. */
   topo_port_state_t          propagated_port_state;     /**< state of the output port's downstream. connected_port_state is the sate of
                                                         immediate downstream and can be different than propagated_port_state. */
   topo_port_state_t          downgraded_port_state;     /**< Downgraded state based on connected_port_state and propagated_port_state */
   uint32_t                   bit_mask;                  /**< bit mask in the channel - applicable for external ports only */
   uint32_t                   num_buf_allocated;
   uint32_t                   buf_max_size;              /**< The amount of space allocated for each of this port's output buffers. The max size
                                                              is the same for all of this port's buffers at any point in time. */
   posal_bufmgr_node_t        out_bufmgr_node;           /**< Buffers are owned by the upstream container. This will be sent downstream
                                                            and then returned to us when the downstream client has consumed it. */
   topo_media_fmt_t           media_fmt;                 /**< media format to downstream */

   cu_ext_out_port_icb_info_t icb_info;                  /**< inter-container buffering info */
   cu_ext_port_prop_info_t    prop_info;                 /**< info about propagated properties. */
   cu_ext_out_port_flags_t   flags;

#ifdef BUF_CNT_DEBUGGING
   uint32_t                   buf_recv_cnt;              /**< data buffer recvd count. may not be accurate when buf is recreated etc */
   uint32_t                   buf_done_cnt;              /**< data buffer done count or num buffers sent to peer svc */
#endif

} cu_ext_out_port_t;

typedef struct cu_int_ctrl_port_t
{
   /** Used for intra-container IMCL  */
   posal_queue_t         *buffer_q_ptr;       // Outgoing ctrl buffer queue

   posal_channel_t       buf_q_channel_ptr; // outgoing ctrl buffer queue's channel.

   uint32_t              num_bufs;           // Number of buffers requested by the module.

   uint32_t              buf_size;           // Current buffer size of the allocated outgoing buffers.

   uint32_t              new_event_num_bufs; // New event num_bufs from the module event.

   uint32_t              new_event_buf_size; // New recurring buffer size from the module event.

   uint32_t              outgoing_intent_cnt;// outgoing intent counter.
} cu_int_ctrl_port_t;

typedef struct cu_ext_ctrl_port_t
{
   topo_port_state_t     connected_port_state;      /**< state of the connected subgraph. connected SG maybe same as SG in which
                                                          module owning this port exists. */

} cu_ext_ctrl_port_t;

/* =======================================================================
CU Function Table
========================================================================== */
/**
 * Each container must provide an implementation of the functions in this table.
 * These functions are called from within cu code.
 */
typedef struct cu_cntr_vtable_t
{
   ar_result_t (*port_data_thresh_change)(void *me_ptr);
   ar_result_t (*aggregate_kpps_bw)(void *me_ptr, uint32_t *kpps_ptr, uint32_t *bw_ptr);

   ar_result_t (*operate_on_subgraph)(void *                    me_ptr,
                                      uint32_t                  sg_ops,
                                      topo_sg_state_t           sg_state,
                                      gu_sg_t *                 sg_ptr,
                                      spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

   ar_result_t (*post_operate_on_subgraph)(void *                    me_ptr,
                                            uint32_t                  sg_ops,
                                            topo_sg_state_t           sg_state,
                                            gu_sg_t *                 sg_ptr,
                                            spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

   ar_result_t (*operate_on_subgraph_async)(void *                    me_ptr,
                                            uint32_t                  sg_ops,
                                            topo_sg_state_t           sg_state,
                                            gu_sg_t *                 sg_ptr,
                                            spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

   ar_result_t (*set_get_cfg)(cu_base_t                         *base_ptr,
                              void                              *mod_ptr,
                              uint32_t                           pid,
                              int8_t                            *param_payload_ptr,
                              uint32_t                          *param_size_ptr,
                              uint32_t                          *error_code_ptr,
                              bool_t                             is_set_cfg,
                              bool_t                             is_deregister,
                              spf_cfg_data_type_t                cfg_type,
                              cu_handle_rest_ctx_for_set_cfg_t **pending_set_cfg_ctx_pptr);

   ar_result_t (*register_events)(cu_base_t *base_ptr,
                                  gu_module_t *module_ptr,
                                  topo_reg_event_t *reg_event_payload_ptr,
                                  bool_t is_register,
                                  bool_t *capi_supports_v1_event_ptr);

   ar_result_t (*init_ext_in_port)(void *me_ptr, gu_ext_in_port_t *ext_in_port_ptr);
   ar_result_t (*deinit_ext_in_port)(void *me_ptr, gu_ext_in_port_t *ext_in_port_ptr);
   ar_result_t (*operate_on_ext_in_port)(void *             me_ptr,
                                         uint32_t           sg_ops,
                                         gu_ext_in_port_t **ext_in_port_pptr,
                                         bool_t             is_self_sg);
   ar_result_t (*post_operate_on_ext_in_port)(void *             me_ptr,
                                               uint32_t           sg_ops,
                                               gu_ext_in_port_t **ext_in_port_pptr,
                                               spf_cntr_sub_graph_list_t *spf_sg_list_ptr);
   ar_result_t (*post_operate_on_ext_out_port)(void *             me_ptr,
                                               uint32_t           sg_ops,
                                               gu_ext_out_port_t **ext_in_port_pptr,
                                               spf_cntr_sub_graph_list_t *spf_sg_list_ptr);
   ar_result_t (*input_media_format_received)(void *            me_ptr,
                                              gu_ext_in_port_t *ext_in_port_ptr,
                                              topo_media_fmt_t *media_format_ptr,
                                              cu_ext_in_port_upstream_frame_length_t *upstream_frame_len_ptr,
                                              bool_t            is_data_path);

   ar_result_t (*init_ext_out_port)(void *me_ptr, gu_ext_out_port_t *ext_out_port_ptr);
   ar_result_t (*deinit_ext_out_port)(void *me_ptr, gu_ext_out_port_t *ext_out_port_ptr);
   ar_result_t (*operate_on_ext_out_port)(void *              me_ptr,
                                          uint32_t            sg_ops,
                                          gu_ext_out_port_t **ext_out_port_pptr,
                                          bool_t              is_self_sg);
   ar_result_t (*ext_out_port_apply_pending_media_fmt)(void* me_ptr, gu_ext_out_port_t *ext_out_port_ptr);
   ar_result_t (*ext_out_port_recreate_bufs)(void* me_ptr, gu_ext_out_port_t *ext_out_port_ptr);

   ar_result_t (*init_ext_ctrl_port)(cu_base_t *me_ptr, gu_ext_ctrl_port_t *ext_in_port_ptr, uint32_t offset);
   ar_result_t (*deinit_ext_ctrl_port)(cu_base_t *me_ptr, gu_ext_ctrl_port_t *ext_in_port_ptr);
   ar_result_t (*operate_on_ext_ctrl_port)(cu_base_t *             me_ptr,
                                         uint32_t           sg_ops,
                                         gu_ext_ctrl_port_t **ext_in_port_pptr,
                                         bool_t             is_self_sg);

   ar_result_t (*connect_ext_ctrl_port)(cu_base_t *me_ptr, gu_ext_ctrl_port_t *ext_in_port_ptr);

   // Apply downgraded state on output/input port handles container and topo specific port operations for the give downgraded state.
   // eg: if downgraded state is START, container can update the channel listen mask and setup the topo input port.
   ar_result_t (*apply_downgraded_state_on_output_port)(cu_base_t *me_ptr, gu_output_port_t *out_port_ptr, topo_port_state_t downgraded_state);
   ar_result_t (*apply_downgraded_state_on_input_port)(cu_base_t *me_ptr,  gu_input_port_t *in_port_ptr, topo_port_state_t downgraded_state);

   ar_result_t (*destroy_all_metadata)(uint32_t log_id, void *module_ctx_ptr, module_cmn_md_list_t **md_list_pptr, bool_t is_dropped);

   ar_result_t (*handle_proc_duration_change)(cu_base_t *me_ptr);

   ar_result_t (*update_path_delay)(cu_base_t *base_ptr, uint32_t path_id);

   ar_result_t (*aggregate_hw_acc_proc_delay)(void *me_ptr, uint32_t *hw_acc_proc_delay_ptr);

   ar_result_t (*vote_against_island)(void *me_ptr);

   void (*exit_island_temporarily)(void *me_ptr);

   uint32_t (*get_additional_ext_in_port_delay_cu_cb)(cu_base_t *me_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
   uint32_t (*get_additional_ext_out_port_delay_cu_cb)(cu_base_t *me_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr);

   bool_t (*check_bump_up_thread_priority)(cu_base_t * me_ptr, bool_t is_bump_up, posal_thread_prio_t original_prio); //used for bumping up or reverting

   ar_result_t (*dcm_topo_set_param)(void *me_ptr);
   ar_result_t (*initiate_duty_cycle_island_entry)(cu_base_t * me_ptr);
   ar_result_t (*initiate_duty_cycle_island_exit)(cu_base_t * me_ptr);

   ar_result_t (*handle_cntr_period_change)(cu_base_t *me_ptr);

   ar_result_t (*rtm_dump_data_port_media_fmt)(void *vtopo_ptr, uint32_t container_instance_id, uint32_t port_media_fmt_report_enable);

} cu_cntr_vtable_t;

/* =======================================================================
Public Function Declarations
========================================================================== */

/**---------------------------------- cu -----------------------------------*/
ar_result_t cu_init(cu_base_t *me_ptr);

ar_result_t cu_deinit(cu_base_t *me_ptr);

ar_result_t cu_check_launch_thread(cu_base_t *            me_ptr,
                                   uint32_t               new_stack_size,
                                   uint32_t               new_root_stack_size,
                                   posal_thread_prio_t    thread_priority,
                                   char *                 thread_name,
                                   bool_t *               thread_launched_ptr);

ar_result_t cu_parse_container_cfg(cu_base_t *me_ptr, apm_container_cfg_t *container_cfg_ptr);

ar_result_t cu_handle_cmd_queue(cu_base_t *me_ptr, uint32_t channel_bit_index);
ar_result_t cu_process_cmd_queue(cu_base_t *me_ptr);
ar_result_t cu_process_resp_queue(cu_base_t *me_ptr, uint32_t channel_bit_index);

ar_result_t cu_create_media_fmt_msg_for_downstream(cu_base_t *        base_ptr,
                                                   gu_ext_out_port_t *ext_out_port_ptr,
                                                   spf_msg_t *         msg,
                                                   uint32_t           media_fmt_opcode);

ar_result_t cu_send_media_fmt_update_to_downstream(cu_base_t *        base_ptr,
                                                   gu_ext_out_port_t *ext_out_port_ptr,
                                                   spf_msg_t *         msg,
                                                   posal_queue_t *    q_ptr);
ar_result_t cu_handle_frame_len_change(cu_base_t * base_ptr, icb_frame_length_t *fm_info_ptr, uint32_t period_us);
void cu_update_cntr_proc_duration(cu_base_t *base_ptr);
ar_result_t cu_parse_get_self_and_peer_heap_ids(apm_container_cfg_t *container_cfg_ptr,
                                                POSAL_HEAP_ID *      self_heap_id_ptr,
                                                POSAL_HEAP_ID *      peer_heap_id_ptr);
uint32_t cu_get_next_unique_id(cu_base_t *base_ptr);
ar_result_t cu_set_cntr_type_bits_in_log_id(uint32_t cntr_type, uint32_t *log_id_ptr);

static inline bool_t cu_check_thread_relaunch_required(cu_base_t *me_ptr,
                                                       uint32_t   new_stack_size,
                                                       uint32_t   new_root_stack_size)
{
   return ((me_ptr->actual_stack_size != new_stack_size) || (me_ptr->root_thread_stack_size != new_root_stack_size))
             ? TRUE
             : FALSE;
}

/**----------------------------- cu_buf_util -------------------------------*/

ar_result_t cu_init_queue(cu_base_t *        base_ptr,
                            char *             data_q_name,
                            uint32_t           num_elements,
                            uint32_t           bit_mask,
                            cu_queue_handler_t q_func_ptr,
                            posal_channel_t    channel_ptr,
                            posal_queue_t **   q_pptr,
                            void *      dest_ptr,
                            POSAL_HEAP_ID      heap_id);

ar_result_t cu_handle_prebuffer(cu_base_t *           base_ptr,
                                gu_ext_out_port_t *   ext_out_port_gu_ptr,
                                spf_msg_data_buffer_t *cur_out_buf_ptr,
                                uint32_t               nominal_frame_size);

static inline bool_t cu_check_if_port_requires_prebuffers(cu_ext_out_port_t *ext_out_port_ptr)
{
   // check if regular prebuffers are required.
   if(ext_out_port_ptr->icb_info.icb.num_reg_prebufs > 0)
   {
      return TRUE;
   }

   // check if OTP frame len is valid. Currently OTP is disabled so these fields are always zero.
#if 0
   if(ext_out_port_ptr->icb_info.icb.otp.frame_len_samples || ext_out_port_ptr->icb_info.icb.otp.frame_len_us)
   {
      return TRUE;
   }
#endif

   return FALSE;
}


/**---------------------------- cu_cmd_handler ----------------------------*/
ar_result_t cu_init_signal(cu_base_t         *cu_ptr,
                           uint32_t           bit_mask,
                           cu_queue_handler_t q_func_ptr,
                           posal_signal_t    *signal_ptr);

ar_result_t cu_deinit_signal(cu_base_t *cu_ptr, posal_signal_t *signal_ptr);

ar_result_t cu_set_get_cfgs_packed(cu_base_t *me_ptr, gpr_packet_t *packet_ptr, spf_cfg_data_type_t cfg_type);
ar_result_t cu_set_get_cfgs_fragmented(cu_base_t *               me_ptr,
                                       apm_module_param_data_t **param_data_pptr,
                                       uint32_t                  num_param_id_cfg,
                                       bool_t                    is_set_cfg,
                                       bool_t                    is_deregister,
                                       spf_cfg_data_type_t        data_type);

ar_result_t cu_set_get_cfg_wrapper(cu_base_t                         *base_ptr,
                                   uint32_t                           mid,
                                   uint32_t                           pid,
                                   int8_t                            *param_payload_ptr,
                                   uint32_t                          *param_size_ptr,
                                   uint32_t                          *error_code_ptr,
                                   bool_t                             is_set_cfg,
                                   bool_t                             is_deregister,
                                   spf_cfg_data_type_t                cfg_type,
                                   cu_handle_rest_ctx_for_set_cfg_t **pending_set_cfg_ctx_pptr);

ar_result_t cu_register_module_events(cu_base_t *me_ptr, gpr_packet_t *packet_ptr);
ar_result_t cu_register_module_events_v2(cu_base_t *me_ptr, gpr_packet_t *packet_ptr);
ar_result_t cu_handle_cntr_events_reg_dereg(cu_base_t *me_ptr, topo_reg_event_t *reg_event_payload_ptr, bool_t is_register);
ar_result_t cu_raise_container_events_to_clients(cu_base_t *me_ptr, uint32_t event_id, int8_t *payload_ptr, uint32_t payload_size);

ar_result_t cu_graph_connect(cu_base_t *me_ptr);
ar_result_t cu_graph_control_connect(cu_base_t *me_ptr);
ar_result_t cu_handle_prepare(cu_base_t *base_ptr, spf_msg_cmd_graph_mgmt_t *cmd_gmgmt_ptr);
ar_result_t cu_ctrl_path_media_fmt_cmd(cu_base_t *base_ptr);
ar_result_t cu_unsupported_resp(cu_base_t *me_ptr);
ar_result_t cu_unsupported_cmd(cu_base_t *me_ptr);
ar_result_t cu_cmd_icb_info_from_downstream(cu_base_t *base_ptr);
ar_result_t cu_gpr_cmd(cu_base_t *me_ptr);
bool_t cu_is_frame_done_event_registered(cu_base_t *me_ptr);
ar_result_t cu_handle_peer_port_property_update_cmd(cu_base_t *base_ptr);
ar_result_t cu_process_peer_port_property_payload(cu_base_t *base_ptr, int8_t *payload_ptr, spf_handle_t *dst_handle_ptr);
ar_result_t cu_is_in_port_closing(cu_base_t *  me_ptr, gu_input_port_t *in_port_ptr, bool_t *is_closing_ptr);
ar_result_t cu_is_module_closing(cu_base_t *  me_ptr, gu_module_t *module_ptr, bool_t *is_closing_ptr);

/**--------------------------- cu_data_handler ----------------------------*/
ar_result_t cu_get_input_data_msg(cu_base_t *me_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
void cu_return_out_buf(cu_base_t *me_ptr, gu_ext_out_port_t *gu_ext_port_ptr);
ar_result_t cu_free_input_data_cmd(cu_base_t *me_ptr, gu_ext_in_port_t *ext_in_port_ptr, ar_result_t status);

/**--------------------------- cu_data_msg_handler ----------------------------*/

/**--------------------------- cu_ext_port_util ---------------------------*/
ar_result_t cu_determine_ext_out_buffering(cu_base_t *base_ptr, gu_ext_out_port_t *ext_out_port_ptr);
ar_result_t cu_inform_downstream_about_upstream_property(cu_base_t *base_ptr);
ar_result_t cu_inform_upstream_about_downstream_property(cu_base_t *base_ptr);
ar_result_t cu_peer_cntr_ds_propagation_init(cu_base_t *base_ptr, cu_ext_out_port_t *cu_ext_out_port_ptr);
ar_result_t cu_peer_cntr_us_propagation_init(cu_base_t *base_ptr, cu_ext_in_port_t *cu_ext_in_port_ptr);
bool_t cu_has_upstream_frame_len_changed(cu_ext_in_port_upstream_frame_length_t *a1,
                                         cu_ext_in_port_upstream_frame_length_t *b1,
                                         topo_media_fmt_t *                      media_fmt_ptr);

ar_result_t cu_ext_in_handle_prebuffer(cu_base_t        *me_ptr,
                                       gu_ext_in_port_t *gu_ext_in_port_ptr,
                                       uint32_t          min_num_buffer_to_hold);
ar_result_t cu_ext_in_requeue_prebuffers(cu_base_t *me_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
ar_result_t cu_ext_in_release_prebuffers(cu_base_t *me_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
bool_t cu_is_realtime(cu_base_t *base_ptr);

/**-------------------------------- cu_pm ---------------------------------*/
ar_result_t cu_handle_island_vote(cu_base_t *me_ptr, posal_pm_island_vote_t island_vote);
ar_result_t cu_vote_latency(cu_base_t *me_ptr, bool_t is_release, bool_t is_realtime_usecase);
ar_result_t cu_handle_clk_vote_change(cu_base_t *me_ptr,
                                     cu_pm_vote_type_t  vote_type,
                                     bool_t     force_vote,
                                     uint32_t   total_kpps,
                                     uint32_t   total_bw,
                                     uint32_t   scaled_kpps_agg_q4,
                                     bool_t     nonblocking);
ar_result_t cu_register_with_pm(cu_base_t *me_ptr, bool_t is_duty_cycling_allowed);
ar_result_t cu_deregister_with_pm(cu_base_t *me_ptr);

/**--------------------------- cu_state_handler ---------------------------*/
topo_port_state_t topo_sg_state_to_port_state(topo_sg_state_t sg_state);

static inline topo_port_state_t cu_get_external_output_ds_downgraded_port_state(cu_ext_out_port_t *ext_out_port_ptr)
{
   return ext_out_port_ptr->downgraded_port_state;
}

static inline topo_port_state_t cu_get_external_output_ds_downgraded_port_state_v2(cu_ext_out_port_t *ext_out_port_ptr)
{
   return ext_out_port_ptr->downgraded_port_state;
}


topo_port_state_t cu_evaluate_n_update_ext_out_ds_downgraded_port_state(cu_base_t *me_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr);

ar_result_t cu_update_all_sg_port_states(cu_base_t *me_ptr, bool_t is_skip_ctrl_ports);
ar_result_t cu_handle_sg_mgmt_cmd(cu_base_t *me_ptr, uint32_t sg_ops, topo_sg_state_t sg_state);
ar_result_t cu_handle_sg_mgmt_cmd_async(cu_base_t *me_ptr, uint32_t sg_ops, topo_sg_state_t sg_state);
bool_t cu_is_disconnect_ext_in_port_needed(cu_base_t *base_ptr, gu_ext_in_port_t *ext_in_port_ptr);
bool_t cu_is_disconnect_ext_out_port_needed(cu_base_t *base_ptr, gu_ext_out_port_t *ext_out_port_ptr);
bool_t cu_is_disconnect_ext_ctrl_port_needed(cu_base_t *base_ptr, gu_ext_ctrl_port_t *ext_ctrl_port_ptr);
ar_result_t cu_init_external_ports(cu_base_t *base_ptr, uint32_t ctrl_port_queue_offet);
void cu_deinit_external_ports(cu_base_t *base_ptr, bool_t b_ignore_ports_from_sg_close, bool_t force_deinit_all_ports);
ar_result_t cu_deinit_internal_ctrl_port(cu_base_t *     me_ptr,
        gu_ctrl_port_t *gu_ctrl_port_ptr,
        bool_t          b_skip_q_flush);
void cu_deinit_internal_ctrl_ports(cu_base_t *base_ptr, bool_t b_destroy_all_ports);

/**--------------------------- cu_gpr_if ---------------------------*/

uint32_t cu_gpr_callback(gpr_packet_t *packet, void *callback_data);

ar_result_t cu_gpr_free_pkt(uint32_t gpr_handle, gpr_packet_t *pkt_ptr);

/** ACK function for call back */
ar_result_t cu_gpr_generate_ack(cu_base_t *   me_ptr,
                                gpr_packet_t *packet_ptr, /// This is the received packet that requires ACK.
                                ar_result_t   status,
                                void *        ack_payload_ptr, /// payload that is required in ACK, specified by caller
                                uint32_t      size,            /// payload size.
                                uint32_t      ack_opcode       /// Optonal The opcode for ACK.
                                );

ar_result_t cu_gpr_generate_client_event(uint8_t  src_domain_id,
                                         uint8_t  dst_domain_id,
                                         uint32_t src_port,
                                         uint32_t dst_port,
                                         uint32_t token,
                                         uint32_t event_opcode,
                                         void *   event_payload_ptr,
                                         uint32_t payload_size);


/**---------------------------  utilities ---------------------------*/
ar_result_t cu_raise_frame_done_event(cu_base_t *base_ptr, uint32_t log_id);

ar_result_t cu_handle_event_from_dsp_service_topo_cb(cu_base_t *        cu_ptr,
                                                     gu_module_t *      module_ptr,
                                                     capi_event_info_t *event_info_ptr);
ar_result_t cu_handle_event_to_dsp_service_topo_cb(cu_base_t *        cu_ptr,
                                                   gu_module_t *      module_ptr,
                                                   capi_event_info_t *event_info_ptr);
ar_result_t cu_handle_event_data_to_dsp_client_v2_topo_cb(cu_base_t *        cu_ptr,
                                                  gu_module_t *      module_ptr,
                                                  capi_event_info_t *event_info_ptr);

ar_result_t cu_handle_capi_event(cu_base_t *        cu_ptr,
                                      gu_module_t *      module_ptr,
                                      capi_event_id_t    event_id,
                                      capi_event_info_t *event_info_ptr);

bool_t cu_check_all_subgraphs_duty_cycling_allowed(cu_base_t *cu_ptr);

ar_result_t cu_process_peer_port_property(cu_base_t *   base_ptr,
                                          spf_handle_t *dst_handle_ptr,
                                          uint32_t      property_type,
                                          uint32_t      property_value,
                                          uint32_t *    need_to_update_states);


static inline void cu_set_handler_for_bit_mask(cu_base_t *me_ptr, uint32_t bit_mask, cu_queue_handler_t fn)
{
   uint32_t n         = cu_get_bit_index_from_mask(bit_mask);
   me_ptr->qftable[n] = fn;
}

static inline void cu_stop_listen_to_mask(cu_base_t *me_ptr, uint32_t mask)
{
   me_ptr->curr_chan_mask &= ~mask;
}

static inline void cu_start_listen_to_mask(cu_base_t *me_ptr, uint32_t mask)
{
   me_ptr->curr_chan_mask |= mask;
}

static inline void cu_set_bit_in_bit_mask(cu_base_t *me_ptr, uint32_t mask)
{
   me_ptr->available_bit_mask &= ~mask;
}

static inline void cu_release_bit_in_bit_mask(cu_base_t *me_ptr, uint32_t mask)
{
   // add it back to available mask
   me_ptr->available_bit_mask |= mask;
   cu_set_handler_for_bit_mask(me_ptr, mask, NULL);
}

static inline uint32_t cu_get_bits(uint32_t x, uint32_t mask, uint32_t shift)
{
   return (x & mask) >> shift;
}

static inline void cu_set_bits(uint32_t *x_ptr, uint32_t val, uint32_t mask, uint32_t shift)
{
   val    = (val << shift) & mask;
   *x_ptr = (*x_ptr & ~mask) | val;
}

static inline uint32_t cu_request_bit_in_bit_mask(uint32_t *available_bit_mask)
{
   uint32_t n    = cu_get_bit_index_from_mask(*available_bit_mask);
   uint32_t mask = 1 << n;
   *available_bit_mask &= ~mask;

   return mask;
}

static inline void cu_set_bits_in_x(uint32_t *x_ptr, uint32_t flags)
{
   *x_ptr |= flags;
}

static inline void cu_clear_bits_in_x(uint32_t *x_ptr, uint32_t flags)
{
   *x_ptr &= ~flags;
}

/**
 * Return the first external input port with the given bitmask.
 */
static inline gu_ext_in_port_t *cu_get_ext_in_port_for_bit_index(cu_base_t *base_ptr, uint32_t index)
{
   uint32_t mask = 1 << index;

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = base_ptr->gu_ptr->ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      uint8_t *         gu_ext_in_port_ptr = (uint8_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      cu_ext_in_port_t *ext_in_port_ptr    = (cu_ext_in_port_t *)(gu_ext_in_port_ptr + base_ptr->ext_in_port_cu_offset);
      if (ext_in_port_ptr->bit_mask & mask)
      {
         return (gu_ext_in_port_t *)gu_ext_in_port_ptr;
      }
   }

   return NULL;
}

/**
 * Return the first external output port with the given bitmask.
 */
static inline gu_ext_out_port_t *cu_get_ext_out_port_for_bit_index(cu_base_t *base_ptr, uint32_t index)
{
   uint32_t mask = 1 << index;

   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = base_ptr->gu_ptr->ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      uint8_t *          gu_ext_out_port_ptr = (uint8_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      cu_ext_out_port_t *ext_out_port_ptr =
         (cu_ext_out_port_t *)(gu_ext_out_port_ptr + base_ptr->ext_out_port_cu_offset);
      if (ext_out_port_ptr->bit_mask & mask)
      {
         return (gu_ext_out_port_t *)gu_ext_out_port_ptr;
      }
   }

   return NULL;
}

static inline void cu_reset_handle_rest(cu_base_t *base_ptr)
{
   MFREE_NULLIFY(base_ptr->handle_rest_ctx_ptr);
   base_ptr->handle_rest_fn = NULL;
}
/**
 * when thread relaunch happens handle rest fn and ctx are set.
 */
static inline bool_t cu_is_any_handle_rest_pending(cu_base_t *base_ptr)
{
   return (NULL != base_ptr->handle_rest_fn);
}

static inline bool_t cu_is_island_container(cu_base_t *base_ptr)
{
   return (PM_MODE_ISLAND == base_ptr->pm_info.register_info.mode);
}

static inline uint32_t cu_frames_per_period(cu_base_t *base_ptr)
{
    return base_ptr->period_us / base_ptr->cntr_frame_len.frame_len_us;
}

/*
Static inline functions
*/

/* Utility to poll and process ext & int ctrl ports */
static inline ar_result_t cu_poll_and_process_ctrl_msgs(cu_base_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t set_ch_bitmask;

   if(0 == (set_ch_bitmask = posal_channel_poll_inline(me_ptr->ctrl_channel_ptr, 0xFFFFFFFF)))
   {
      return AR_EOK;
   }

   // Poll for channel for ext ctrl port messages
   if (set_ch_bitmask & me_ptr->curr_ext_ctrl_chan_mask)
   {
      // Exit lpi if ctrl port lib is compiled in nlpi
      cu_vote_against_lpi_if_ctrl_port_lib_in_nlpi(me_ptr);
      result |= cu_poll_and_process_ext_ctrl_msgs_util_(me_ptr, set_ch_bitmask);
   }

   // Poll for channel for internal ctrl port messages
   if (set_ch_bitmask & posal_queue_get_channel_bit(me_ptr->cntr_cmn_imcl_handle.q_ptr))
   {
      // Exit lpi if ctrl port lib is compiled in nlpi
      cu_vote_against_lpi_if_ctrl_port_lib_in_nlpi(me_ptr);
      result |= cu_poll_and_process_int_ctrl_msgs_util_(me_ptr, set_ch_bitmask);
   }

   return result;
}


///////////////////////////////// FWK EVENT HANDLING RELATED - START ////////////////////////////////

/* Following macro should be used to set one fwk event flag.
 * This macros ensure that event is set in the correct event_flag based on the command or data path processing context.
 */
#define CU_SET_ONE_FWK_EVENT_FLAG(cu_ptr, flag)                                                                        \
   {                                                                                                                   \
      cu_event_flags_t *fwk_event_flag_ptr = cu_get_fwk_evt_flag_(cu_ptr);                                             \
      fwk_event_flag_ptr->flag             = TRUE;                                                                     \
   }

/* Following macro should be used to set more than one fwk event flags.
 * This macros ensure that event is set in the correct event_flag based on the command or data path processing context.
 */
#define CU_SET_FWK_EVENT_FLAGS(cu_ptr, fwk_event_flags)                                                                \
   {                                                                                                                   \
      cu_event_flags_t *fwk_event_flag_ptr = cu_get_fwk_evt_flag_(cu_ptr);                                             \
      fwk_event_flag_ptr->word |= fwk_event_flags.word;                                                                \
   }
/*
 * Following two macros should be used carefully in an event handling function.
 * If only one of them is used then it will cause compilation error.
 * This is designed to remind the developers that there are two separate event flags
 * one main event flag which must be used synchronously with main data-path processing thread.
 * and one async event flag which can be used asynchronously to cache the events during parallel command handling.
 * */

// this macro should be used at the top of the event handling function.
// if this macro is used alone in a function then will get an unused variable error.
// Function where this Macro is used, must be executing synchronously with the Main data path processing thread.
// (Critical Section lock is required)
#define CU_FWK_EVENT_HANDLER_CONTEXT bool_t use_the_macro_only_in_the_fwk_event_handling_context;

// this macro should be used to read/write the fwk_event_flag only in event handler context.
// it takes cu_ptr as its first argument and a boolean "do_reconcile" as its second argument.
// if do_reconcile is TRUE then any pending event from the command handling context is reconciled with the main fwk
// event flag. if this macro is used alone in a function then will get undefined symbol error set "do_reconcile:FALSE"
// if handling events in context to data-path processing. set "do_reconcile:TRUE" if handling event in context of
// command handling. set "do_reconcile:TRUE: if not sure of the context or a function can be called from any context.
#define CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(event_flag_ptr, cu_ptr, do_reconcile)                             \
   if (do_reconcile)                                                                                                   \
   {                                                                                                                   \
      cu_reconcile_event_flags_((cu_ptr));                                                                             \
   }                                                                                                                   \
   event_flag_ptr  = (cu_event_flags_t *)&((cu_ptr)->fwk_evt_flags_);                                                  \
   use_the_macro_only_in_the_fwk_event_handling_context = 0; /* for error checking    */

// This is a private function and shouldn't be used directly. Use the Macros defined above
static inline void cu_reconcile_event_flags_(cu_base_t *me_ptr)
{
#ifdef CONTAINER_ASYNC_CMD_HANDLING
   // if there are any events which are set during the command handling then transfer those events to the main event
   // flag
   me_ptr->fwk_evt_flags_ |= me_ptr->async_fw_evt_flags_;
   me_ptr->async_fw_evt_flags_ = 0;
#endif
}

// This is a private function and shouldn't be used directly. Use the Macros defined above
static inline cu_event_flags_t *cu_get_fwk_evt_flag_(cu_base_t *cu_ptr)
{
#ifdef CONTAINER_ASYNC_CMD_HANDLING
    /*
    * 1. Return Main Fwk Event flag if already executing in data-path processing thread.
    * 2. Return Main Fwk Event flag if synchronously executing in command processing thread context.
    * 3. Return Async Fwk Event flag if asynchronously executing in command processing thread context.
    * */
   bool_t is_in_data_path_or_sync_handling_context =
      ((cu_ptr->gu_ptr->is_sync_cmd_context_ > 0) ||
       posal_thread_get_curr_tid() == cu_ptr->gu_ptr->data_path_thread_id);
   return (is_in_data_path_or_sync_handling_context) ? (cu_event_flags_t *)&cu_ptr->fwk_evt_flags_
                                                      : (cu_event_flags_t *)&cu_ptr->async_fw_evt_flags_;
#else
   return (cu_event_flags_t *)&cu_ptr->fwk_evt_flags_;
#endif
}


///////////////////////////////// FWK EVENT HANDLING RELATED - END ////////////////////////////////

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef CONTAINER_UTILS_H_
