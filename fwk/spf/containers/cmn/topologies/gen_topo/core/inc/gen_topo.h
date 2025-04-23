#ifndef GEN_TOPO_H_
#define GEN_TOPO_H_

/**
 * \file gen_topo.h
 *
 * \brief
 *
 *     Basic Topology header file.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_guids.h"

#include "spf_utils.h"
#include "spf_macros.h"
#include "spf_svc_utils.h"
#include "gpr_packet.h"
#include "gpr_api_inline.h"

#include "shared_lib_api.h"
#include "posal_internal_inline.h"
#include "posal_power_mgr.h"
#include "amdb_static.h"

#include "graph_utils.h"
#include "apm_cntr_if.h"
#include "cntr_cntr_if.h"
#include "topo_buf_mgr.h"
#include "topo_interface.h"
#include "gen_topo_module_bypass.h"
#include "apm_debug_info.h"

#include "wr_sh_mem_ep_api.h"
#include "rd_sh_mem_ep_api.h"
#include "wr_sh_mem_client_api.h"
#include "rd_sh_mem_client_api.h"
#include "sh_mem_ep_metadata_api.h"
#include "wr_sh_mem_ep_ext_api.h"

#include "capi_cmn.h"

#include "gen_topo_global_shmem_msg.h"
#include "gen_topo_path_delay.h"
#include "gen_topo_pcm_fwk_ext.h"
#include "gen_topo_data_port_ops_intf_ext.h"
#include "gen_topo_ctrl_port.h"
#include "gen_topo_dm_ext.h"
#include "gen_topo_prof.h"
#include "gen_topo_metadata.h"
#include "gen_topo_exit_island.h"
#include "gen_topo_sync_fwk_ext.h"
#include "topo_buf_mgr.h"
#include "gen_topo_pure_st.h"
#include "rtm_logging_api.h"
#include "thin_topo.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

// clang-format off

/** ------------- Begin debug macros */
// extra checks that can be enabled to debug memory corruption etc

#define DATA_FLOW_STATE_DEBUG
#define DEBUG_TOPO_PORT_PROP_TYPE
#define DEBUG_TOPO_BOUNDARY_STATE_PROP
//#define METADATA_DEBUGGING
#define PATH_DELAY_DEBUGGING
#define THRESH_PROP_DEBUG
//#define NBLC_DEBUG
#define ENABLE_SIGNAL_MISS_CRASH

#ifdef USES_DEBUG_DEV_ENV
/** Debug masks */


#if 0 //frequently needed ones
#define VERBOSE_DEBUGGING
#define TRIGGER_DEBUG
#define METADATA_DEBUGGING
#define SAFE_MODE 1
#define BUF_MGMT_DEBUG
//#define TRIGGER_DEBUG_DEEP
#endif

//#define BUF_MGMT_DEBUG

//#define CTRL_LINK_DEBUG

//#define TRIGGER_DEBUG_DEEP

//for debugging buffer overflows/underflows use TBF_BUF_OVERFLOW_DETECTION in topo_buf_mgr.h

#endif

#if defined(VERBOSE_DEBUGGING) || defined(SAFE_MODE)
#define ERROR_CHECK_MODULE_PROCESS
#endif

/** ------------- End debug macros */

// enable this only for SIM
#ifdef SIM
#define ST_TOPO_SAFE_MODE
#endif

#define GEN_TOPO_STATIC static
//#define GEN_TOPO_STATIC
#define UNITY_Q4 0x10
#define  GEN_TOPO_IS_NON_TRIGGERABLE_PORT(nontrigger_policy) (FWK_EXTN_PORT_NON_TRIGGER_INVALID != nontrigger_policy)

#define CAPI_IS_PCM_FORMAT(data_format) ((CAPI_FIXED_POINT == data_format) || (CAPI_FLOATING_POINT == data_format))

#define CAPI_IS_PCM_PACKETIZED(data_format) ((CAPI_RAW_COMPRESSED != data_format) && (CAPI_DEINTERLEAVED_RAW_COMPRESSED != data_format))

#ifdef _DIAG_MAX_TX_PKT_SIZE
#define MAX_LOG_PKT_SIZE_RTM (_DIAG_MAX_TX_PKT_SIZE - 200)
#else
//retaining 3360 for variants which do not diag macro defined
#define MAX_LOG_PKT_SIZE_RTM 3360
#endif
typedef struct gen_topo_t                    gen_topo_t;
typedef struct gen_topo_module_t             gen_topo_module_t;
typedef struct gen_topo_input_port_t         gen_topo_input_port_t;
typedef struct gen_topo_output_port_t        gen_topo_output_port_t;
typedef struct gen_topo_ctrl_port_t          gen_topo_ctrl_port_t;
typedef struct gen_topo_process_context_t      gen_topo_process_context_t;
typedef struct gen_topo_module_bypass_t      gen_topo_module_bypass_t;
typedef struct gen_topo_vtable_t             gen_topo_vtable_t;
typedef struct gen_topo_common_port_t        gen_topo_common_port_t;
typedef struct gen_topo_graph_init_t         gen_topo_graph_init_t;

#ifdef USES_THIN_TOPO
typedef struct thin_topo_t                    thin_topo_t;
#endif

/*
 * Table of functions that can be populated by different topo implementations
 * that use gen_topo as a base
 */
typedef struct gen_topo_vtable_t
{
   ar_result_t (*capi_get_required_fmwk_extensions)(void *topo_ptr, void *module_ptr, void *amdb_handle, capi_proplist_t *init_proplist_ptr);

   bool_t (*input_port_is_trigger_present)(void *topo_ptr,
                                                void *topo_in_port_ptr,
                                                bool_t *is_ext_trigger_not_satisfied_ptr);
   bool_t (*output_port_is_trigger_present)(void *topo_ptr,
                                                 void *topo_out_port_ptr,
                                                 bool_t *is_ext_trigger_not_satisfied_ptr);

   bool_t (*input_port_is_trigger_absent)(void *topo_ptr, void *topo_in_port_ptr);
   bool_t (*output_port_is_trigger_absent)(void *topo_ptr, void *topo_out_port_ptr);
   bool_t (*output_port_is_size_known)(void *topo_ptr, void *topo_out_port_ptr);

   /* Gets data len (actual or max based on is_max) of output port's internal buffer. For spl, for external output ports,
    * gets the external buffer len since there is no internal buffer. */
   uint32_t (*get_out_port_data_len)(void *topo_ptr, void *topo_out_port_ptr, bool_t is_max);

   /* Check if a module's port is at nblc end.*/
   bool_t (*is_port_at_nblc_end)(gu_module_t *module_ptr, gen_topo_common_port_t *cmn_port_ptr);

   /* Check if a module's port is at nblc end.*/
   void (*update_module_info)(gu_module_t *module_ptr);
} gen_topo_vtable_t;


typedef struct topo_to_cntr_vtable_t
{
   /* Topo callback to containers for clear EoS */
   ar_result_t (*clear_eos)(gen_topo_t *topo_ptr, void *ext_inp_ref, uint32_t ext_inp_id, module_cmn_md_eos_t *eos_metadata_ptr);

   /* Topo callback to containers to handle events to DSP service. */
   ar_result_t (*raise_data_to_dsp_service_event)(gen_topo_module_t *module_context_ptr,
                                                              capi_event_info_t *event_info_ptr);

   /* Topo callback to containers to handle events from DSP service. */
   ar_result_t (*raise_data_from_dsp_service_event)(gen_topo_module_t *module_context_ptr,
                                                  capi_event_info_t *event_info_ptr);

   ar_result_t (*handle_capi_event)(gen_topo_module_t *module_context_ptr,
                                              capi_event_id_t    id,
                                              capi_event_info_t *event_info_ptr);

   ar_result_t (*algo_delay_change_event)(gen_topo_module_t *module_context_ptr);

   ar_result_t (*set_pending_out_media_fmt)(gen_topo_t* topo_ptr, gen_topo_common_port_t *cmn_port_ptr, gu_ext_out_port_t *out_port_ptr);

   /* Set propagate property on ext output port */
   ar_result_t (*set_propagated_prop_on_ext_output)(gen_topo_t *topo_ptr, gu_ext_out_port_t* ext_out_port_ptr,
                                                    topo_port_property_type_t prop_type,  void* payload_ptr);

   /* Set propagate property  on ext input port */
   ar_result_t (*set_propagated_prop_on_ext_input)(gen_topo_t *topo_ptr, gu_ext_in_port_t* ext_in_port_ptr,
                                                    topo_port_property_type_t prop_type,  void* payload_ptr);

   ar_result_t (*handle_frame_done)(gen_topo_t *gen_topo_ptr, uint8_t path_index);

   ar_result_t (*create_module)(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr, gen_topo_graph_init_t *graph_init_data_ptr);
   ar_result_t (*destroy_module)(gen_topo_t *       topo_ptr,
                                 gen_topo_module_t *module_ptr,
                                 bool_t             reset_capi_dependent_dont_destroy);

   ar_result_t (*check_insert_missing_eos_on_next_module)(gen_topo_t *topo_ptr, gen_topo_input_port_t* inport_ptr);

   /* --- Below are currently only implemented by Topo2 ---  */

   /* Check and send media fmt message downstream. */
   ar_result_t (*check_apply_ext_out_ports_pending_media_fmt)(gen_topo_t *topo_ptr, bool_t is_data_path);

   /* Topo2 uses this to adjust the previous actual data length of the external input port when
      resizing the external input port when using it to push zeros on eos. */
   ar_result_t (*set_ext_in_port_prev_actual_data_len)(gen_topo_t *topo_ptr, gu_ext_in_port_t *in_port_ptr, uint32_t new_prev_actual_data_len);

   /* Indicate to the container that the maximum sample requirement on an input port has changed. This is primarily
    * needed for handling of modules running in fixed output mode */
   ar_result_t (*update_input_port_max_samples)(gen_topo_t *topo_ptr, gen_topo_input_port_t* inport_ptr);

   /* Gets the actual_data_len or max_data_len of the buffer corresponding to the external input port. */
   uint32_t (*ext_in_port_get_buf_len)(gu_ext_in_port_t* ext_in_port_ptr, bool_t is_max);

   /* Clears req samples state of all external input ports. */
   ar_result_t (*clear_topo_req_samples)(gen_topo_t *topo_ptr, bool_t is_max);

   bool_t (*ext_out_port_has_buffer)(gu_ext_out_port_t* ext_out_port_ptr);

   bool_t (*ext_in_port_has_data_buffer)(gu_ext_in_port_t *gu_ext_in_port_ptr);

   /* Update variable size flag in icb info of ext input/output ports*/
   ar_result_t (*update_icb_info)(gen_topo_t *topo_ptr);

   /* Topo2 only: checks if the external input port has enough data to process. */
   bool_t (*ext_in_port_has_enough_data)(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);

   /* Topo2 only: Gets the external buffer associated with this external output port.*/
   void* (*ext_out_port_get_ext_buf)(gu_ext_out_port_t *gu_ext_out_port_ptr);

   ar_result_t (*ext_in_port_dfg_eos_left_port)(gen_topo_t *topo_ptr, gu_ext_in_port_t* ext_in_port_ptr);

   ar_result_t (*ext_in_port_clear_timestamp_discontinuity)(gen_topo_t *topo_ptr, gu_ext_in_port_t* ext_in_port_ptr);

   /* Topo callback to handle module event to DSP client v2 */
   ar_result_t (*raise_data_to_dsp_client_v2)(gen_topo_module_t *module_context_ptr,
                                              capi_event_info_t *event_info_ptr);

   ar_result_t (*vote_against_island)(gen_topo_t *topo_ptr);

   uint32_t (*aggregate_ext_in_port_delay)(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
   uint32_t (*aggregate_ext_out_port_delay)(gen_topo_t *topo_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr);

   bool_t (*check_for_error_print)(gen_topo_t *topo_ptr);

   ar_result_t (*notify_ts_disc_evt)(gen_topo_t *topo_ptr, bool_t ts_valid, int64_t timestamp_disc_us, uint32_t path_index);

   ar_result_t (*module_buffer_access_event)(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr, capi_event_info_t *event_info_ptr);

   ar_result_t (*check_if_any_ext_in_has_to_preserve_prebuffer)(gen_topo_t *topo_ptr, bool_t *has_to_preserve_prebuffer);
} topo_to_cntr_vtable_t;


/** This enum used for aggregation in the container */
typedef enum gen_topo_data_need_t
{
   GEN_TOPO_DATA_NOT_NEEDED = 1<<0,       // data may not be needed because data is already there, but process can be called.
   GEN_TOPO_DATA_NEEDED = 1<<1,
   GEN_TOPO_DATA_NEEDED_OPTIONALLY = 1<<2,
   GEN_TOPO_DATA_BLOCKED = 1<<3,       // blocked is when data is not needed and process need not be called.
} gen_topo_data_need_t;

/** This enum used for external output port wait mask aggregation*/
typedef enum gen_topo_port_trigger_need_t
{
   GEN_TOPO_PORT_TRIGGER_NEEDED = 1<<0,
   GEN_TOPO_PORT_TRIGGER_NOT_NEEDED = 1<<1,
   GEN_TOPO_PORT_TRIGGER_NEEDED_OPTIONALLY = 1<<2,
} gen_topo_port_trigger_need_t;

#define GEN_TOPO_MAX_NUM_TRIGGERS 2

typedef enum gen_topo_trigger_t
{
   GEN_TOPO_INVALID_TRIGGER = 0,
   /**< when we come out of signal or buf trigger, invalid trigger is set.
    *  This helps if we check curr_trigger in some other context. */
   GEN_TOPO_DATA_TRIGGER=1,
   /**< data/buffer trigger caused current topo process */
   GEN_TOPO_SIGNAL_TRIGGER=2
   /**< signal/interrupt/timer trigger caused current topo process */
} gen_topo_trigger_t;

// check like this to avoid KW error
#define GEN_TOPO_INDEX_OF_TRIGGER(topo_trigger) (( (topo_trigger) == GEN_TOPO_DATA_TRIGGER) ? 0 : 1 )

/**
 *
 */
typedef struct gen_topo_process_info_t
{
   struct
   {
      uint8_t   is_in_mod_proc_context : 1;     /**< To know if modules raised event when process was called on them. */
      uint8_t   port_thresh_event : 1;          /**< if port-thresh event is raised during process, then come back to process without reading more input.
                                                   output buffer got recreated. Try to process before returning and waiting for input.
                                                   Decoder: read first frame, raise threshold. If we wait for next input, it will
                                                   overwrite timestamp of first buffer.
                                                   before waiting for next input, we need to finish processing first
                                                   frame using newly created output buffers.
                                                   Corner case: due to threshold propagation all buffers are dropped and there's no longer any input.
                                                   Due to bypassing of gen_cntr_setup_internal_input_port_and_preprocess,
                                                   container exists process as anything_changed would be false. If ext-in is already present, this will lead to
                                                   hang. To prevent the hang, anything_changed must be set to true to ensure we loop one more time after threshold change
                                                   a) first without reading more input b) second by reading some more input.
                                                   */
      uint8_t   anything_changed : 1;           /**< in process calls either inputs should get consumed, or output produced or EOF/metadata propagated.
                                                      If not we need to wait for ext trigger like cmd or data or buf or timer*/
      uint8_t   probing_for_tpm_activity : 1;   /**< Probing trigger policy modules for activity. If nothing changes when all trigger policy modules are done,
                                                      break processing. saves MIPS. Valid for signal trigger container, after signal trigger.*/
#ifdef USES_THIN_TOPO
      uint8_t   atleast_one_inp_holds_ext_buffer : 1;     /**< If set means, atleast one internal input is holding/propagating external input buffer to the NBLC */
#endif
   };
   uint8_t      num_data_tpm_done;              /**< number of data trigger policy modules done in one process call. used only under probing_tpm_for_activity.*/


} gen_topo_process_info_t;

/** This enum used for aggregation in the container */
typedef enum topo_proc_context_trigger_t
{
   TOPO_PROC_CONTEXT_TRIGGER_NOT_EVALUATED  = (0),
   TOPO_PROC_CONTEXT_TRIGGER_NOT_PRESENT    = (1),
   TOPO_PROC_CONTEXT_TRIGGER_IS_PRESENT     = (1 << 1)
}topo_proc_context_trigger_t;

typedef struct gen_topo_port_scratch_flags_t
{
   uint8_t            data_pending_in_prev : 1;              /**< prev out port data is copied to next in port before process. but if there's any pending data
                                                                   that must be copied after process, this flag indicates so.
                                                                   Calling gen_topo_check_copy_between_modules may cause MD/flags to go out of sync with data */
   uint8_t            prev_marker_eos : 1;                   /**< topo uses this to determine when eos has left the input or output port (prev_eos 1, cur_eos 0).
                                                                     need to remove this once spl_topo migrates to below flag */
   uint8_t            prev_eos_dfg : 1;                      /**< topo uses this to determine whether EOS/DFG left input or output port.
                                                                   Note that marker_eos is still set when EOS is stuck inside algos,
                                                                   but prev_eos_dfg is based on md list (which results in FALSE if EOS inside algo.
                                                                   keeping marker_eos set helps algos do zero pushing. marker_eos is also used here. */
   uint8_t           prev_eof : 1;                           /**< whether end of frame is set before process. Used in module process context to set
                                                                  anything_changed flag, if module consumes only EOF but not data.
                                                                  Note that ST topo doesnt require this flag, since anything_changed is always set at the end
                                                                  of ST topo processing. */
   uint8_t           is_timestamp_valid : 1;                 /**< Validity of gen_topo_port_scratch_data_t::timestamp */
   uint32_t          is_trigger_present:2;                   /**< Used to save the is trigger present as evaluated during module processing decision, must be used only
                                                                  in the topo process context and should be reset at the end of process */
} gen_topo_port_scratch_flags_t;

/**
 * a scratch memory for storing info such as previous actual len etc.
 *
 * This struct is per max-port, max-ch. be cautious about LPI mem.
 */
typedef struct gen_topo_port_scratch_data_t
{
   uint32_t                *prev_actual_data_len;              /** per buf (not necessarily per ch). similar to bufs_ptr, it's enough to check first buf to see if any buf has data.
                                                                      array of size max_num_channels.*/
   capi_buf_t              *bufs;                              /**< Used only for internal ports.
                                                                  used only when num_proc_loop > 1. for 1 loop, gen_topo_common_port_t.bufs_ptr is used.
                                                                  array of size max_num_channels.*/
   module_cmn_md_list_t    *md_list_ptr;                      /**< Metadata list ptr. When module is called in loop, only MD generated in a given call must be
                                                                   present in sdata as their offset is based on new data. Any old MD is kept here as old MD have
                                                                   offset based on beginning of the buffer. Used only for output ports*/
   gen_topo_port_scratch_flags_t flags;

   /**
    * Below fields are only needed in spl_topo.
    * To save memory in gen_topo, it's possible to separate these into a spl_topo_only structure. To do so, gen_topo_process_context_t needs
    * to use void *'s which have a different type for gen_topo/adv_topo. gen_topo_create_modules() would have to take sizes in to allocate
    * the correct sizes. Currently postponing this optimization since it becomes a hassle to reference scratch_data fields (needs a type cast).
    */

   int64_t                timestamp;                          /**< spl_topo uses this for storing the initial output port's timestamp before calling topo process.
                                                                   This is compared against output timestamp for checking timestamp discontinuity.
                                                                   gen_topo_port_scratch_flags_t::is_timestamp_valid*/

} gen_topo_port_scratch_data_t;

typedef struct gen_topo_ext_port_scratch_flags_t
{
   uint8_t   release_ext_out_buf : 1;   /**< for ext-out. when media fmt changes if old data existed in buf, that must be released even if it
                                             doesn't contain enough frames (same with timestamp discontinuity, when out buf is too small).
                                             input buf might still be present & we might have to continue processing after releasing out buf.*/
} gen_topo_ext_port_scratch_flags_t;

/**
 * a scratch memory for storing history info such as previous actual len etc.
 */
typedef struct gen_topo_ext_port_scratch_data_t
{
   uint32_t                            prev_actual_data_len;   /**< not used for ext-input in GC, used in SC*/
   gen_topo_ext_port_scratch_flags_t   flags;
} gen_topo_ext_port_scratch_data_t;

/**
 * this structure stores hist info only inside a process trigger. not across 2 process triggers.
 */
typedef struct gen_topo_process_context_t
{
   uint8_t                           num_in_ports;              /**< max of max input ports across any module */
   uint8_t                           num_out_ports;             /**< max of max output ports across any module */
   uint8_t                           num_ext_in_ports;          /**< max input ports across any module */
   uint8_t                           num_ext_out_ports;         /**< max output ports across any module */
   uint8_t                           max_num_channels;          /**< max num channels across any module's media fmt */

   gen_topo_port_scratch_data_t       *in_port_scratch_ptr;      /**< input port scratch data. size num_in_ports. Must index with in_port_ptr->gu.index */

   gen_topo_port_scratch_data_t       *out_port_scratch_ptr;     /**< output port scratch data. size num_out_ports. Must index with  out_port_ptr->gu.index */

   gen_topo_ext_port_scratch_data_t   *ext_in_port_scratch_ptr;       /**< input port history data. size num_ext_in_ports. Must index with 0 .. num_ext_in_ports-1 */

   gen_topo_ext_port_scratch_data_t   *ext_out_port_scratch_ptr;      /**< output port history data. size num_ext_out_ports.  Must index with 0 .. num_ext_in_ports-1 */

   gen_topo_process_info_t            process_info;           /**< */

   capi_stream_data_v2_t              **in_port_sdata_pptr;   /**< array of ptr to sdata for calling capi process. size num_in_ports. Must index with in_port_ptr->gu.index
                                                                   */

   capi_stream_data_v2_t              **out_port_sdata_pptr;  /**< array of ptr to sdata for calling capi process. size num_out_ports. Must index with  out_port_ptr->gu.index
                                                                   */

   gen_topo_trigger_t                 curr_trigger;           /**< current trigger that caused process frames */

   uint32_t                           err_print_time_in_this_process_ms;

   capi_err_t                         proc_result;
} gen_topo_process_context_t;


typedef struct gen_topo_init_data_t
{
   /** input */
   const topo_to_cntr_vtable_t            *topo_to_cntr_vtble_ptr;  /**< vtable for topo to call back containers */

   /** output */
   const topo_cu_vtable_t                 *topo_cu_vtbl_ptr;        /**< vtable for the CU */
   const topo_cu_island_vtable_t          *topo_cu_island_vtbl_ptr; /**< Island vtable for the CU */
} gen_topo_init_data_t;

typedef uint32_t (*gpr_callback_t)(gpr_packet_t *, void *) ;

typedef capi_err_t (*topo_capi_callback_f)(void *context_ptr, capi_event_id_t id, capi_event_info_t *event_info_ptr);

typedef struct gen_topo_timestamp_t
{
   int64_t value; // Microseconds.
   bool_t  valid; // TS validity
   bool_t  ts_continue; // TS continue flag
} gen_topo_timestamp_t;

/**
 * High precision.
 */
typedef struct gen_topo_hp_timestamp_t
{
   int64_t  ivalue;           // Microseconds (integer).
   uint64_t fvalue;           // Fractional, ns. +ve
   bool_t   ts_valid;         // TS validity
   bool_t   ts_continue;      // TS continue flag
} gen_topo_hp_timestamp_t;

/** Event flag is useful in 2  cases
 * 1. when event is not per port
 * 2. when event is per port, but the event handling is done for all ports at once.
 *
 * It's not useful when event is not per port and event cannot be handled at once, because
 * it's not known when to clear the mask*/

#define GEN_TOPO_CAPI_EVENT_FLAG_TYPE uint32_t

typedef union gen_topo_capi_event_flag_t
{
   struct
   {
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    kpps: 1;                /**< Set when kpps and kpss scale factor are changed by the module */
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    bw : 1;                 /**< Set when code or data BW are changed by the module */
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    port_thresh : 1;        /**< at least one threshold event is pending; thresh prop is pending */
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    process_state : 1;
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    media_fmt_event : 1;    /**< to track any change in output media fmt of a module and if so, to trigger media fmt dependent global
                                                operations such as threshold, kpps/bw. etc */
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    algo_delay_event : 1;   /**< algo delay change */
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    data_trigger_policy_change : 1;
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    signal_trigger_policy_change : 1;
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    port_prop_is_up_strm_rt_change: 1; /**< is real time port property change event */
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    port_prop_is_down_strm_rt_change: 1; /**< is real time port property change event */
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    dynamic_inplace_change   : 1;      /**< Set if a module raises dynamic inplace change event,
                                                          or if module changes from bypass to non bypass, viceversa. */
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    hw_acc_proc_delay_event : 1;  /**< hw accelerator proc delay change */
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    is_signal_triggered_active_change : 1;  /**< To indiacate signal trigger is changed from enable to disable or viceversa.
                                                                                    modules like spr can enable/disable the signal trigger */
      GEN_TOPO_CAPI_EVENT_FLAG_TYPE    realloc_scratch_mem : 1; /**< Checks and reallocates scratch memory if there is any Max num channels changed or num in/output ports changes */
   };
   GEN_TOPO_CAPI_EVENT_FLAG_TYPE word;
} gen_topo_capi_event_flag_t;

/** when graph is created, it results in output such as required stack size etc*/
typedef struct gen_topo_graph_init_t
{
   /** input */
   spf_handle_t *             spf_handle_ptr;
   gpr_callback_t             gpr_cb_fn;

   topo_capi_callback_f       capi_cb;          /**< CAPI callback function */

   /** output */
   uint32_t                   max_stack_size;   /**< max stack size required for the topo (max of all CAPIs) */

   bool_t                     input_bufs_ptr_arr_not_needed;
   bool_t                     skip_scratch_mem_reallocation;
   bool_t                     propagate_rdf;
} gen_topo_graph_init_t;


/** Returns gen_topo_capi_event_flag_t.port_thresh field BIT mask */
static inline uint32_t __gen_topo_capi_event_port_thresh_bitmask()
{
    gen_topo_capi_event_flag_t temp_flag;
    temp_flag.word = 0xFFFFFFFF;
    temp_flag.port_thresh = 0;
    return (~temp_flag.word);
}

/** Returns gen_topo_capi_event_flag_t.process_state field BIT mask */
static inline uint32_t __gen_topo_capi_event_process_state_bitmask()
{
    gen_topo_capi_event_flag_t temp_flag;
    temp_flag.word = 0xFFFFFFFF;
    temp_flag.process_state = 0;
    return (~temp_flag.word);
}

/** Returns gen_topo_capi_event_flag_t.media_fmt_event field BIT mask */
static inline uint32_t __gen_topo_capi_event_media_fmt_bitmask()
{
    gen_topo_capi_event_flag_t temp_flag;
    temp_flag.word = 0xFFFFFFFF;
    temp_flag.media_fmt_event = 0;
    return (~temp_flag.word);
}

static inline uint32_t __gen_topo_capi_event_signal_trigger_policy_change_bitmask()
{
    gen_topo_capi_event_flag_t temp_flag;
    temp_flag.word = 0xFFFFFFFF;
    temp_flag. signal_trigger_policy_change = 0;
    return (~temp_flag.word);
}

static inline uint32_t __gen_topo_capi_event_data_trigger_policy_change_bitmask()
{
    gen_topo_capi_event_flag_t temp_flag;
    temp_flag.word = 0xFFFFFFFF;
    temp_flag. data_trigger_policy_change = 0;
    return (~temp_flag.word);
}

#define  GT_CAPI_EVENT_PORT_THRESH_BIT_MASK     (__gen_topo_capi_event_port_thresh_bitmask())
#define  GT_CAPI_EVENT_PROCESS_STATE_BIT_MASK   (__gen_topo_capi_event_process_state_bitmask())
#define  GT_CAPI_EVENT_MEDIA_FMT_BIT_MASK       (__gen_topo_capi_event_media_fmt_bitmask())
#define  GT_CAPI_EVENT_SIGNAL_TRIGGER_POLICY_CHANGE_BIT_MASK     (__gen_topo_capi_event_signal_trigger_policy_change_bitmask())
#define  GT_CAPI_EVENT_DATA_TRIGGER_POLICY_CHANGE_BIT_MASK       (__gen_topo_capi_event_data_trigger_policy_change_bitmask())

typedef struct gen_topo_flags_t
{
   uint32_t any_data_trigger_policy: 1;   /**< is there any module which raised data trigger policy extn FWK_EXTN_TRIGGER_POLICY
                                                                  (note: currently if trigger policy extn is removed this is not reset.*/
   uint32_t cannot_be_pure_signal_triggered : 1; /**< Once this value is set it to TRUE it cannot be changed. It indicates if the signal triggered
                                                   container ever/never had a module which raises Data TP or Signal TP. */
   uint32_t is_signal_triggered : 1;      /**< whether container is signal triggered */
   uint32_t is_signal_triggered_active : 1;      /**< whether signal triggered module is signal triggered or not*/

   uint32_t need_to_handle_frame_done   : 1;    /**< Set if handle frame done event is set */
   uint32_t is_dm_enabled       : 1;      /**< Set if atleast one DM module is operating in a DM mode. */


   uint32_t aggregated_island_vote : 2;      /**< Flag used for bookkeeping of topo's island participation vote.
                                                * PM_ISLAND_VOTE_ENTRY = 0     -> Vote for Island entry Allowed state
                                                * PM_ISLAND_VOTE_EXIT = 1      -> Vote for Island entry restricted state
                                                * PM_ISLAND_VOTE_DONT_CARE = 2 -> Vote for Island Don't care */

   uint32_t aggregated_island_type: 2;      /**< Flag used for bookkeeping of topo's island participation vote type.
                                                * PM_ISLAND_TYPE_DEFAULT = 0     -> Vote for default (low power) Island
                                                * PM_ISLAND_TYPE_LOW_POWER_2 = 1 -> Vote for low power 2 Island */

   uint32_t need_to_ignore_signal_miss : 1;  /**<Flag used to indicates signal miss handling to avoid signal miss error recovery in certain case
                                                 for eg : after CMD handling*/

   uint32_t is_sync_module_present : 1; /**< whether container has sync module */

   uint32_t defer_voting_on_dfs_change : 1; /**< Indicates if fwk needs avoid updating kpps/bw votes on DFS change, if the system is in island.
                                                Its set if cntr gets EOS md with flag:defer_voting_on_dfs_change set. It can be reset by another
                                                EOS/cmd/events gets triggered. >*/

   uint32_t port_mf_rtm_dump_enable : 1; /**if this enable flag is true port mf can be wriiten into RTM palyload*/

   uint32_t is_src_module_present : 1; /**< Flag to check if there is any souce module (like DTMF-GEN) present in the topo. */

   uint32_t simple_threshold_propagation_enabled : 1; /**< Flag is set when all ports carry PCM media format.
                                                           If Flag is set then a simplified threshold propagation logic is used which is
                                                           fast compared to the generalized threshold propagation logic.*/

   /*** temporary flags */
   uint32_t process_us_gap : 1;           /**< flag indicates that EOS needs to be processed for upstream stop or flush*/
   uint32_t state_prop_going_on : 1;      /**< Set when state propagation is going on (to error out in capi event handling) */
   uint32_t process_pending :1;           /**< set to true if topo processing is exited in between to handle command Q.
                                               flag is used to re-enter the topo process as external trigger may have already been satisfied. */
} gen_topo_flags_t;


typedef struct gen_topo_port_mf_utils_t
{
  spf_list_node_t *mf_list_ptr;     /**< list of media format blocks assigned to different ports in this topo.*/
  uint32_t node_count;          /**< number of different media format blocks assigned to different ports in this topo. */
} gen_topo_port_mf_utils_t;

/**
 * Guideline to use capi event flags.
 * Since command handling and data-path handling can go in parallel therefore careful handling is needed when dealing with capi_event_flags.
 * 1. There are two different flags "capi_event_flag" and "async_capi_event_flag"
 * 2. "capi_event_flag" is the primary event flag which is used for calling the event-handler utility.
 * 3. "async_capi_event_flag" is used by the command handling thread to cache the intermediate events.
 * 4. "async_capi_event_flag" is used to ensure that data-path is not disturbed during parallel command handling.
 * 5. There are set of utility functions and Macro to access these event flags from different contexts.
 */

typedef struct gen_topo_t
{
   gu_t                          gu;                        /**< Graph utils. */
   gu_module_list_t             *started_sorted_module_list_ptr; /**< sub list from sorted_modue_list, but only includes modules from started-SG */
   gen_topo_process_context_t    proc_context;              /**< history data required for data processing */

   /*Following Capi Event Flags are hidden to prevent the direct access. These can be accessed using utility functions and Macros. */
   GEN_TOPO_CAPI_EVENT_FLAG_TYPE    capi_event_flag_;       /**< Main Capi Events: these are set synchronous to the data-path processing thread. Any event handling is also done based on this. */
#ifdef CONTAINER_ASYNC_CMD_HANDLING
   GEN_TOPO_CAPI_EVENT_FLAG_TYPE    async_capi_event_flag_; /**< Async Capi Events: these are set asynchronous to the data-path processing thread temporarily. This must be synchronously reconciled with the Main Capi Event before event-handling. */
#endif

   const gen_topo_vtable_t       *gen_topo_vtable_ptr;      /**< vtable for abstracting topo vs. spl_topo */
   const topo_to_cntr_vtable_t   *topo_to_cntr_vtable_ptr;  /**< vtable with topo to container functions*/
   topo_buf_manager_t            buf_mgr;
   uint8_t                       module_count;              /**< tracks modules created in this container for logging purposes (only increasing count).
                                                                 See LOG_ID_LOG_MODULE_INSTANCES_MASK)
                                                                 Every module create adds one count.
                                                                 This uniquely identifies a module in this container */
   uint8_t                       num_data_tpm;              /**< number of active data trigger policy modules */
   gen_topo_flags_t              flags;
   POSAL_HEAP_ID                 heap_id;                   /*Heap ID used for all memory allocations in the topology*/

   topo_mf_utils_t               mf_utils;                  /**< port media format utility.*/

   uint64_t                      wc_time_at_first_frame;    /**< wall clock time at first frame after island exit.*/

   uint32_t                      port_mf_rtm_dump_seq_num;  /**< sequence number used for dumping port MF to RTM */

   topo_capi_callback_f          capi_cb;          /**< CAPI callback function */

#ifdef USES_THIN_TOPO
   thin_topo_exit_flags_t        exit_flags;
   /** if any of the bit is set in the mask, cntr cannot process data with thin topo.
    This mask field needs to be tracked irrespective of whether cntr is signal triggered since
    container can be thin topo at any point. For example metadata and prebuffer related flags
    must be tracked always.  */

   spf_list_node_t*              list_of_active_md_node_ptr;
   /**<MD needs to be tracked always for GC since its required for thin topo. irrespective of
       whether container is currently signal triggered or not. Because it can become signal
       triggered later. > */

   thin_topo_t                   *thin_topo_ptr;
   /**This memory will be allocated by thin topo only if its GC & signal triggered container.
      Unlike the exit flags field, this struct is allocated only if thin cntr is signal triggered.
      it contains information related to thin topo process context.*/
#endif

} gen_topo_t;


/**
 * Container level subgraph - holds state information.
 */
typedef struct gen_topo_sg_t
{
   gu_sg_t           gu; /**<  Must be the first element. */
   topo_sg_state_t   state;
} gen_topo_sg_t;

/**
 * enum for need_data_trigger_in_st module's flag.
 */
enum
{
    BLOCKED_DATA_TRIGGER_IN_ST = 0, /**< Data trigger in Signal trigger container is not allowed for this module.*/
    ALLOW_DATA_TRIGGER_IN_ST_EVENT = 1, /**< Module has raised event to allow Data trigger in Signal trigger container.*/
    ALLOW_DATA_TRIGGER_IN_ST_PROPAGATION = 2 /**< Data trigger in Signal trigger container is allowed for this module via propagation of ALLOW_DATA_TRIGGER_IN_ST_EVENT event*/
};

/**
 * A bitfield for module flags
 */
typedef union gen_topo_module_flags_t
{
   struct
   {
      /***Static Flags***/
      uint64_t inplace             : 1;
      uint64_t dynamic_inplace     : 1;    /**< Indicates if module changed itself to inplace dynamically.
                                                This flag is intended to optimize buffer usage for MIMO modules
                                                which are momentarily operating in SISO mode like SAL/Splitter. Its used
                                                to assign output buffer same as input.

                                                Should not be used when traversing nblc inplace chains.

                                                Refer #CAPI_EVENT_DYNAMIC_INPLACE_CHANGE event. >*/
      uint64_t pending_dynamic_inplace   : 1; /**< If module raises inplace event in process context, need to cache it and handle at the end of process.
                                                   Else it will affect the buffer assignment during process context. > */

      uint64_t requires_data_buf   : 1;    /**< TRUE if CAPI requires framework to do buffering.
                                                If FALSE and module has threshold, then module will not be called unless input threshold is met
                                                (encoder, module like EC) & for output, output must have thresh amount of empty space.
                                                Enc may also use requires_data_buf = TRUE and do thresh check inside. */
      uint64_t disabled            : 1;         /**< by default modules are enabled.*/
      uint64_t active              : 1;         /**< caches module active flag: active = SG started && ports started (src,sink,siso cases) && (module bypassed||fwk module||!disabled) etc*/

      uint64_t need_data_trigger_in_st  : 2;    /**< 2 bit flag to indicate if data_trigger is needed in signal triggered container.
                                                          By default data triggers are not handled in signal triggered container unless this flag is set by the event or propagation.
                                                          #FWK_EXTN_EVENT_ID_DATA_TRIGGER_IN_ST_CNTR
                                                          This flag is used to evaluate the topo trigger policy of signal trigger container when a subgraph opens/closes.*/

      /** Flags for FWK_EXTN_EVENT_ID_DATA_TRIGGER_IN_ST_CNTR determing direction of propagation.
          Don't care value for all modules besides module raising this event. */
      uint64_t needs_input_data_trigger_in_st: 1;
      uint64_t needs_output_data_trigger_in_st: 1;

      uint64_t is_dm_disabled           : 1;    /**< Indicates if modules has enabled/disabled dm >*/
      uint64_t dm_mode                  : 2;    /**< Indicates operating dm mode [value range is enum gen_topo_dm_mode_t] */

      uint64_t is_nblc_boundary_module   : 1;    /**< Indicates if module is at the nblc boundary >*/

      uint64_t voted_island_exit : 1;            /**< For modules this flag indicates whether module voted for island exit>*/

      uint64_t is_signal_trigger_deactivated: 1;    /**< This flag is corresponds to is_signal_triggered_active in gen_topo_flags_t,
                                                   named as deactivated so that by default signal trigger deactivated will be false,
                                                   So by default signal trigger will be active*/
      uint64_t is_threshold_disabled: 1;        /**<Flag for threshold disabled state by SYNC module*/

      uint64_t supports_deintlvd_unpacked_v2 : 1; /**< Indicates if module supports CAPI_DEINTERLEAVED_UNPACKED_V2
                                                       interleaving format.*/

      /** Flags which indicate whether framework extension is required for the module */
      uint64_t need_stm_extn            : 1;    /**< FWK_EXTN_STM (SIGNAL TRIGGERED MODULE)*/
      uint64_t need_pcm_extn            : 1;    /**< FWK_EXTN_PCM */
      uint64_t need_mp_buf_extn         : 1;    /**< FWK_EXTN_MULTI_PORT_BUFFERING */
      uint64_t need_trigger_policy_extn : 1;    /**< FWK_EXTN_TRIGGER_POLICY */
      uint64_t need_cntr_frame_dur_extn : 1;    /**< FWK_EXTN_CONTAINER_FRAME_DURATION*/
      uint64_t need_thresh_cfg_extn     : 1;    /**< FWK_EXTN_THRESHOLD_CONFIGURATION*/
      uint64_t need_proc_dur_extn       : 1;    /**< FWK_EXTN_PARAM_ID_CONTAINER_PROC_DELAY */
      uint64_t need_dm_extn             : 1;    /**< FWK_EXTN_DM */
      uint64_t need_soft_timer_extn     : 1;    /**< FWK_EXTN_SOFT_TIMER */
      uint64_t need_island_extn         : 1;    /**< FWK_EXTN_ISLAND */
      uint64_t need_sync_extn           : 1;    /**< FWK_EXTN_SYNC */
      uint64_t need_async_st_extn       : 1;    /**< FWK_EXTN_ASYNC_SIGNAL_TRIGGER */
      uint64_t need_global_shmem_extn   : 1;    /**< FWK_EXTN_GLOBAL_SHMEM_MSG */

      /** Flags which record the interface extensions supported by the module
       * Other extensions such as INTF_EXTN_IMCL, INTF_EXTN_PATH_DELAY are not stored */
      uint64_t supports_metadata       : 1;       /**< INTF_EXTN_METADATA: this also means module propagates metadata */
      uint64_t supports_data_port_ops  : 1;       /**< INTF_EXTN_DATA_PORT_OPERATION: this also means module requires data port states. */
      uint64_t supports_prop_port_ds_state : 1;   /**< INTF_EXTN_PROP_PORT_DS_STATE */
      uint64_t supports_prop_is_rt_port_prop : 1; /**< INTF_EXTN_PROP_IS_RT_PORT_PROPERTY */
      uint64_t supports_module_allow_duty_cycling : 1;  /**< INTF_EXTN_DUTY_CYCLING_ISLAND: Module to raise allow/disallow duty cycling to container unblock island entry */
      uint64_t supports_period : 1; /** < INTF_EXTN_PERIOD */
      uint64_t supports_stm_ts : 1; /**< INTF_EXTN_STM_TS: Module requires the latest signal-triggered timestamp value*/
   };
   uint64_t word;
} gen_topo_module_flags_t;

typedef struct gen_topo_cached_param_node_t
{
   spf_cfg_data_type_t payload_type; /**< persistent, shared persistent and default*/
   uint32_t            param_id;     /**< cached Param ID*/
   uint32_t            param_size;   /**< size of the payload*/
   int8_t *            payload_ptr;  /**< pointer to the payload*/
} gen_topo_cached_param_node_t;

typedef struct gen_topo_cached_event_node_t
{
   capi_register_event_to_dsp_client_v2_t reg_event_payload;
} gen_topo_cached_event_node_t;

typedef struct gen_topo_trigger_policy_t
{
   fwk_extn_port_nontrigger_group_t          nontrigger_policy;   /**< ports belongs to non-trigger category? */

   fwk_extn_port_trigger_policy_t            port_trigger_policy; /**< port trigger policy */
   uint32_t                                  num_trigger_groups;  /**< groups of ports that can trigger */
   fwk_extn_port_trigger_group_t             *trigger_groups_ptr; /**< trigger groups */
} gen_topo_trigger_policy_t;

typedef struct gen_topo_module_t
{
   gu_module_t                               gu;                  /**< Must be the first element. */
   capi_t                                    *capi_ptr;
   gen_topo_t                                *topo_ptr;
   uint32_t                                  kpps;                /**< Total kpps of this capi for all its ports. needed to check for change. */
   uint32_t                                  kpps_scale_factor_q4;/**< Multiply by scale factor for running at higher speeds if module sets this flag, qfactor Q28.4 */
   uint32_t                                  algo_delay;          /**< us, for single port modules */
   uint32_t                                  hw_acc_proc_delay;   /**< us */
   uint32_t                                  code_bw;             /**< BW in Bytes per sec */
   uint32_t                                  data_bw;             /**< BW in Bytes per sec */
   gen_topo_module_flags_t                   flags;
   gen_topo_module_bypass_t *                bypass_ptr;          /**< if module is disabled & can be bypassed, then it will use this mem to park prev media fmt, kpps etc.
                                                                       note: most SISO modules are bypassed when disabled. MIMO modules cannot be bypassed.
                                                                       If bypass is not possible, then module proc will be called and module needs to take care of out media, kpps etc */

   uint32_t                                  pending_zeros_at_eos;            /**< pending amount of zeros to be pushed at flushing EoS in bytes (per channel), see func gen_topo_convert_len_per_buf_to_len_per_ch*/
   module_cmn_md_list_t                      *int_md_list_ptr;                /**< internal metadata list. for SISO modules that don't support metadata prop, MD stays here until algo delay elapses. */
   gen_topo_trigger_policy_t                 *tp_ptr[GEN_TOPO_MAX_NUM_TRIGGERS];            /**< trigger policy for each trigger type*/
   gen_topo_module_prof_info_t               *prof_info_ptr;                                /**< memory to store module profiling info */
   uint8_t                                   serial_num;                      /**< serial number assigned to the module, when it's first created (See LOG_ID_LOG_MODULE_INSTANCES_MASK) */
   uint8_t                                   err_msg_proc_failed_counter;     /**<Process failed since last failure printed.*/
   uint8_t                                   num_proc_loops;      /**< in case LCM thresh is used, then num process loop that need to be called per module */
} gen_topo_module_t;

/**
 * Every port same path-id belong to same path. hence delay can be added.
 */
typedef struct gen_topo_delay_info_t
{
   uint32_t                path_id;
   //for MIMO modules, per-in-per-out delay is determined by query (query doesn't exist as of May 2019)
} gen_topo_delay_info_t;

/** bit mask is used */
#define GEN_TOPO_BUF_ORIGIN_INVALID 0
/** buffer is from buf manager */
#define GEN_TOPO_BUF_ORIGIN_BUF_MGR 1
/** buffer is from ext-buf used in the internal port corresponding to the ext port */
#define GEN_TOPO_BUF_ORIGIN_EXT_BUF 2
/** buffer is borrowed from ext-port to an inplace nblc port */
#define GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED 4
/** buffer is borrowed from int-port, specifically, end of an inplace nblc */
#define GEN_TOPO_BUF_ORIGIN_BUF_MGR_BORROWED 8
/** buffer is provided by the module itself */
#define GEN_TOPO_BUF_ORIGIN_CAPI_MODULE 16
/** buffer is provided by the module itself */
#define GEN_TOPO_BUF_ORIGIN_CAPI_MODULE_BORROWED 32

/** bit mask is used */
#define GEN_TOPO_MODULE_BUF_ACCESS_INVALID 0
/** module can provide output buffer for framework */
#define GEN_TOPO_MODULE_OUTPUT_BUF_ACCESS 1
/** module can provide input buffer for framework */
#define GEN_TOPO_MODULE_INPUT_BUF_ACCESS 2

/** bit mask is used */
#define GEN_TOPO_MF_NON_PCM_UNPACKED    (0x0)
/** bit mask indicates that port media format is PCM unpacked V1 */
#define GEN_TOPO_MF_PCM_UNPACKED_V1     (0x1)
/** bit mask indicates that port media format is PCM unpacked V2 */
#define GEN_TOPO_MF_PCM_UNPACKED_V2     (0x2)

typedef union gen_topo_port_flags_t
{
   struct
   {
      /** Permanent    */
      uint32_t       port_has_threshold : 1;       /**< threshold is enforced on this port (either because module has raised or fwk has assigned) */
      uint32_t       buf_origin: 6;                /**< GEN_TOPO_BUF_ORIGIN_*. this is a bit mask. */
      uint32_t       is_upstream_realtime : 1;     /**< indicates if the upstream port produces RT data. Assigned only through propagation. */
      uint32_t       is_downstream_realtime : 1;   /**< indicates if the downstream is a RT consumer. Assigned only through propagation. */
      uint32_t       is_state_prop_blocked :1;     /**< indicates if the downstream state propagation is blocked on this port. This is only for output ports. */

      uint32_t       is_mf_valid : 1;              /**< cached value of topo_is_valid_media_fmt(gen_topo_common_port_t::media_fmt) for minimizing run time overhead.
                                                         CU ports dont have this flag as they are not frequently checked for validity.*/

      uint32_t       module_rejected_mf : 1;    /**< if set input media format fails, this flag is set to TRUE and if module rejected input
                                                     MF its threshold is ignored until valid MF is set.>*/
      uint32_t       port_is_not_reset : 1;     /**< if false then port is in algo-reset state.>*/

      /** temporary: cleared after event handling */
      uint32_t       media_fmt_event : 1;
      uint32_t       port_prop_is_rt_change: 1;    /**< is real time port property change event */
      uint32_t       port_prop_state_change: 1;    /**< state port property propagation event */

      uint32_t       force_return_buf : 1;   /**< Marked to force return the buffer, applicable only for low latency buffer manager. susbsequent call to
                                                  gen_topo_return_one_buf_mgr_buf() will return the buffer, if set. else it will be held in low latency usecases.

                                                  The flag is mainly used in dynamic inplace modules, ideally when module changes from inplace <-> non inplace
                                                  input/output buffers must be re assigned in the entire nblc chain. Ideally when module raises dynamic inplace,
                                                  buffers must be forced returned. But if data is present on the ports, buffers cannot be returend immediately,
                                                  so we mark the forced return flag. once the data is consument buffer will be returned and susbsequent get_buf()
                                                  will assign the input and output buffers correctly. */
      uint32_t       downstream_req_data_buffering : 1; /**< Flag is set if there is a module with requires data buffering in the downstream of the cur module's nblc.*/

      uint32_t       is_pcm_unpacked :  2; /**< GEN_TOPO_MF_PCM_UNPACKED_V1=0x1 indicates unpacked V1,
                                                GEN_TOPO_MF_PCM_UNPACKED_V2=0x2 indicates unpacked V2 */

      uint32_t       supports_buffer_resuse_extn: 2; /**< GEN_TOPO_MODULE_* bit mask */
#ifdef USES_THIN_TOPO
      uint32_t       thin_topo_can_assign_ext_in_buffer:1;
      uint32_t       thin_topo_can_assign_ext_out_buffer:1;
#endif
   };
   uint32_t          word;

} gen_topo_port_flags_t;

typedef struct gen_topo_common_port_t
{
   topo_port_state_t             state;                     /**< Downgraded state = downgrade of (self state, connected port state)*/
   topo_data_flow_state_t        data_flow_state;           /**< Data flow state. currently no requirement for output ports. applicable for input port only. */

   topo_media_fmt_t              *media_fmt_ptr;             /**< Port's media format.
                                                               media format pointers are shared with other ports as well, it should not be updated directly.
                                                               read can be done directly.
                                                               to write, first update the media format in a temporary variable (stack) and then update using
                                                               "tu_set_media_fmt" function.
                                                               */

   uint32_t                      max_buf_len;               /**< This is the max length of the buffer, assigned based on thresh propagation,
                                                                  not necessarily same as module raised threshold.
                                                                  max_data_len in buf struct is for current data_ptr.*/
   uint32_t                      max_buf_len_per_buf;       /**< deint-raw-compr and deint-unpacked have multiple buffers. max_buf_len_per_buf = max_buf_len/sdata.bufs_num*/
   topo_buf_t                    *bufs_ptr;                 /**< Array of capi_buf_t. array len = sdata.bufs_num.
                                                                  deinterleaved unpacked PCM->bufs_num=num_ch in med fmt.
                                                                  deinterleaved_raw_compr -> bufs_num = bufs_num in media fmt.
                                                                  Rules: to check if any data is present it's enough to check bufs_ptr[0] for data_ptr and actual_len.
                                                                     in deint-unpacked : total len = actual len * sdata.bufs_num
                                                                     in deint-raw-compr: total len = sum (actual_len) overall all bufs as diff buf can be of diff length.
                                                                        however, if one buf is empty all remaining bufs also must be empty.
                                                                      ** If bufs_ptr[0].actual_data_len is zero, rest are also assumed to be zero. But to reset, all must be set to zero. gen_topo_set_all_bufs_len_to_zero
                                                                      ** To check if a buf is assigned, check only for bufs_ptr[0].data_ptr, other pointers are not reset.
                                                                      ** To get per-ch length use gen_topo_convert_len_per_buf_to_len_per_ch. deint-pack,interleaved have all ch in one buf.
                                                                      ** For MD prop, total len is passed except for deint-raw-compr. gen_topo_get_actual_len_for_md_prop.*/
   capi_stream_data_v2_t         sdata;                     /**< buf in sdata points to a buf from process context if num_proc_loops > 1,
                                                                  otherwise, they point to bufs_ptr.
                                                                  sdata.bufs_ptr must not be used in container code except in module_process.
                                                                  Always use gen_topo_common_port_t.bufs_ptr */
   uint64_t                      fract_timestamp_pns;       /**< fractional ts stored in pns format.
                                                                  pns = "P nano seconds" is fractional timestamp represented in terms of 10 bits.
                                                                  to covert to nano seconds ns = (fract_timestamp * 1000) >> 10.

                                                                  You must use this utility tu_convert_pns_to_ns() to convert to nano seconds
                                                                  before doing any arthimetic op with it.*/
   gen_topo_port_flags_t         flags;

   uint32_t                      threshold_raised;          /**<threshold in bytes raised by the module*/
   /** Events update the below fields temporarily */
   uint32_t                      port_event_new_threshold;  /**< thresh from module is assigned here.
                                                                  thresh propagation also assigned here. 0 means no change in thresh.
                                                                  1 means no buffer needed.
                                                                  cleared after event is handled. */
   intf_extn_data_port_opcode_t  last_issued_opcode;        /**< Last issued port operation is stored to avoid consecutively
                                                              setting same operation.*/

   spf_list_node_t               *delay_list_ptr;           /**< list of delay objects gen_topo_delay_info_t */
} gen_topo_common_port_t;


typedef union gen_topo_input_port_flags_t
{
   struct
   {
      uint32_t       processing_began   : 1;          /**< indicates if the module processed from this input port at least once */
      uint32_t       need_more_input    : 1;          /**< indicates whether more input data is needed to continue processing. */
      uint32_t       media_fmt_received : 1;          /**< indicates if this input port has received media format at least once */
      uint32_t       disable_ts_disc_check : 1;       /**< If decoder drops input data, then input TS may no longer be used for
                                                            discontinuity check in gen_topo_check_copy_incoming_ts */
      uint32_t       was_eof_set: 1;                  /**< if eof was set on inport in the last process call, and another eof comes in the next process b4 data, we can simply ignore it.
                                                           When TS discont happens in first module, EOF goes thru all, but subsequent modules also detect EOF again, which can be dropped with this. */
      uint32_t       is_threshold_disabled_prop: 1;   /**< if set to TRUE then it means that there is a SYNC module connected
                                                           in the downstream of this input port and threshold is disabled by it. */
   };
   uint32_t          word;
} gen_topo_input_port_flags_t;

/**
 * checking one buffer is sufficient.
 * for deint unpacked, all channels must have eq num of samples.
 * for deint raw compr, checking fullness across all bufs is not possible as bufs can be of diff length (one buf can be full while other can be partially filled)
 */
#define GEN_TOPO_IS_IN_BUF_FULL(in_port_ptr)                                                                           \
   (in_port_ptr->common.bufs_ptr[0].data_ptr && in_port_ptr->common.bufs_ptr[0].max_data_len &&                                        \
    (in_port_ptr->common.bufs_ptr[0].max_data_len == in_port_ptr->common.bufs_ptr[0].actual_data_len))



typedef struct gen_topo_input_port_t
{
   gu_input_port_t               gu;                  /**< Must be the first element */
   gen_topo_common_port_t        common;
   gen_topo_input_port_flags_t   flags;
   gen_topo_input_port_t         *nblc_start_ptr;     /**< beginning of the non-buffering linear chain. similar to end. */
   gen_topo_input_port_t         *nblc_end_ptr;       /**< end port of non-buffering linear chain.
                                                         beginning port as well as the middle ports will have end ptr
                                                         this is required to find out if there's a downstream i/p trigger at every module o/p port.*/
   uint32_t                      bytes_from_prev_buf; /**< when module returns need more this many bytes are pending in input buffer.
                                                           this is decremented as and when module consumes these bytes. needed for syncing to TS coming from prev module or ext in.
                                                           decoder -> raw input => when bytes_from_prev_buf is nonzero, don't sync to input until decoder
                                                           stops returning need-more. pcm/61937 input => timestamp need to be adjusted for this
                                                           before assigning to input timestamp.
                                                           Only bufs_ptr[0] length considered in case there are multiple buffers .
                                                           For deint-raw-compr, if each buffers data is assumed to consume at the same rate so that if first buf is drained, other buf are also drained.*/
   gen_topo_hp_timestamp_t       ts_to_sync;          /**< timestamp that's yet to be synced to common.sdata*/
} gen_topo_input_port_t;

typedef struct gen_topo_output_port_t
{
   gu_output_port_t              gu;                      /**< Must be the first element */
   gen_topo_common_port_t        common;
   gen_topo_output_port_t        *nblc_start_ptr;         /**< beginning of the non-buffering linear chain. similar to end*/
   gen_topo_output_port_t        *nblc_end_ptr;           /**< end port of non-buffering linear chain.
                                                               beginning port as well as the middle ports will have end ptr
                                                               this is required to find out if there's ext o/p trigger at every module port.*/
   bool_t                        any_data_produced;       /**< Marked if any data/metadata is produced by the module in the previous process,
                                                               flag is checked from the next modules context in nblc chain. */
   uint8_t                       err_msg_dropped_data_counter;     /**< This counts number of dropped data instance since last printed */
   uint8_t                       err_msg_stale_data_drop_counter;  /**< This counts number of dropped stale data instance since last printed  */
} gen_topo_output_port_t;

typedef struct gen_topo_ctrl_port_t
{
   gu_ctrl_port_t               gu;                /**< Must be the first element */
   topo_port_state_t            state;

   intf_extn_imcl_port_opcode_t last_issued_opcode; /* Cache the last issued opcode to protect against repetitive issues to module.
                                                        For example, we cannot issue CLOSE twice to a module. */
} gen_topo_ctrl_port_t;



spf_data_format_t   gen_topo_convert_public_data_fmt_to_spf_data_format(uint32_t data_format);
uint32_t            gen_topo_convert_spf_data_fmt_public_data_format(spf_data_format_t spf_fmt);
topo_interleaving_t gen_topo_convert_public_interleaving_to_gen_topo_interleaving(uint16_t pcm_interleaving);
uint16_t          gen_topo_convert_gen_topo_interleaving_to_public_interleaving(topo_interleaving_t topo_interleaving);
uint16_t          gen_topo_convert_gen_topo_endianness_to_public_endianness(topo_endianness_t gen_topo_endianness);
topo_endianness_t gen_topo_convert_public_endianness_to_gen_topo_endianness(uint16_t pcm_endianness);
spf_data_format_t gen_topo_convert_capi_data_format_to_spf_data_format(data_format_t capi_data_format);
data_format_t     gen_topo_convert_spf_data_format_to_capi_data_format(spf_data_format_t spf_data_fmt);
capi_interleaving_t gen_topo_convert_gen_topo_interleaving_to_capi_interleaving(topo_interleaving_t gen_topo_int);
topo_interleaving_t gen_topo_convert_capi_interleaving_to_gen_topo_interleaving(capi_interleaving_t capi_int);

static inline bool_t gen_topo_fwk_extn_does_sync_module_exist_downstream(gen_topo_t *me_ptr, gen_topo_input_port_t *in_port_ptr)
{
   if (!me_ptr->flags.is_sync_module_present)
   {
      return FALSE;
   }

   return gen_topo_fwk_extn_does_sync_module_exist_downstream_util_(me_ptr, in_port_ptr);
}

static inline void gen_topo_set_bits(uint32_t *x_ptr, uint32_t val, uint32_t mask, uint32_t shift)
{
   val    = (val << shift) & mask;
   *x_ptr = (*x_ptr & ~mask) | val;
}

static inline void gen_topo_get_mod_heap_id_and_log_id(uint32_t *    log_id_ptr,
                                                       POSAL_HEAP_ID *heap_id_ptr,
                                                       uint8_t       serial_num,
                                                       POSAL_HEAP_ID topo_heap_id)
{
   gen_topo_set_bits(log_id_ptr,
                     (serial_num + 1),
                     LOG_ID_LOG_MODULE_INSTANCES_MASK,
                     LOG_ID_LOG_MODULE_INSTANCES_SHIFT);
   (*heap_id_ptr) = ((*log_id_ptr) | topo_heap_id);
   return;
}

// Checks if US or DS is realtime path.
static inline bool_t gen_topo_is_port_in_realtime_path(gen_topo_common_port_t *cmn_port_ptr)
{
   // create is US,DS RT bit mask
    gen_topo_port_flags_t temp_flag;
    temp_flag.word = 0xFFFFFFFF;
    temp_flag.is_downstream_realtime         =  0;
    temp_flag.is_upstream_realtime           =  0;
   uint32_t CMN_PORT_FLAG_RT_FLAGS_BIT_MASK = (~temp_flag.word);

   // return if port in rt path
   return (CMN_PORT_FLAG_RT_FLAGS_BIT_MASK & cmn_port_ptr->flags.word);
}

static inline uint32_t gen_topo_get_bits(uint32_t x, uint32_t mask, uint32_t shift)
{
   return (x & mask) >> shift;
}

static inline bool_t gen_topo_is_ext_peer_container_connected(gen_topo_output_port_t *out_port_ptr)
{
   return (out_port_ptr->gu.ext_out_port_ptr && out_port_ptr->gu.ext_out_port_ptr->downstream_handle.spf_handle_ptr);
}

static inline bool_t gen_topo_use_ext_out_buf_for_topo(gen_topo_output_port_t *out_port_ptr)
{
   // right now all ext out peer cases use ext out buf. hence it's sufficient condition
   return gen_topo_is_ext_peer_container_connected(out_port_ptr);
}

/**
 * timstamps a and b are in microseconds
 */
static inline bool_t gen_topo_is_timestamp_discontinuous1(bool_t valid_a, int64_t ts_a, bool_t valid_b, int64_t ts_b)
{
   // Max allowable difference between the actual and expected timestamps in microseconds
   static const int64_t TIMESTAMP_TOLERANCE = 1000;

   if (valid_a && valid_b)
   {
      int64_t timestamp_diff = ts_a - ts_b;
      if ((timestamp_diff < -TIMESTAMP_TOLERANCE) || (timestamp_diff > TIMESTAMP_TOLERANCE))
      {
         return TRUE;
      }
   }
   return FALSE;
}

static inline bool_t gen_topo_is_timestamp_discontinuous(bool_t valid_a, int64_t ts_a, bool_t valid_b, int64_t ts_b)
{
   // Max allowable difference between the actual and expected timestamps in microseconds
   static const int64_t TIMESTAMP_TOLERANCE = 1000;

   // validity difference => discontinuous
   if (valid_a ^ valid_b)
   {
      return TRUE;
   }

   if (valid_a && valid_b)
   {
      int64_t timestamp_diff = ts_a - ts_b;
      if ((timestamp_diff < -TIMESTAMP_TOLERANCE) || (timestamp_diff > TIMESTAMP_TOLERANCE))
      {
         return TRUE;
      }
   }
   return FALSE;
}

static inline uint32_t gen_topo_get_total_max_len(gen_topo_common_port_t *cmn_port_ptr)
{
   return (cmn_port_ptr->bufs_ptr[0].max_data_len * cmn_port_ptr->sdata.bufs_num);
}

static inline bool_t gen_topo_is_pcm_any_unpacked(gen_topo_common_port_t *cmn_port_ptr)
{
   // Optimize and not update actual data lengths for the DEINTERLEAVED_UNPACKED_V2 format.
   return (0 != cmn_port_ptr->flags.is_pcm_unpacked);
}

static inline bool_t gen_topo_is_pcm_unpacked_v2(gen_topo_common_port_t *cmn_port_ptr)
{
   // Optimize and not update actual data lengths for the DEINTERLEAVED_UNPACKED_V2 format.
   return (cmn_port_ptr->flags.is_pcm_unpacked == GEN_TOPO_MF_PCM_UNPACKED_V2);
}

static inline uint32_t gen_topo_get_num_sdata_bufs_to_update(gen_topo_common_port_t *cmn_port_ptr)
{
   // Optimize and not update actual data lengths for the DEINTERLEAVED_UNPACKED_V2 format.
   return gen_topo_is_pcm_any_unpacked(cmn_port_ptr) ? 1 : cmn_port_ptr->sdata.bufs_num;
}

/** this function must be called whenever a port's media format is updated, to update the mask that
    stores whether media format is unpacked v1/v2.*/
static inline void gen_topo_reset_pcm_unpacked_mask(gen_topo_common_port_t *cmn_port_ptr)
{
   cmn_port_ptr->flags.is_pcm_unpacked = 0;
   if (cmn_port_ptr->flags.is_mf_valid &&
       TU_IS_PCM_AND_ANY_DEINT_UNPACKED(cmn_port_ptr->media_fmt_ptr))
   {
      cmn_port_ptr->flags.is_pcm_unpacked =
         (TOPO_DEINTERLEAVED_UNPACKED_V2 == cmn_port_ptr->media_fmt_ptr->pcm.interleaving)
            ? GEN_TOPO_MF_PCM_UNPACKED_V2
            : GEN_TOPO_MF_PCM_UNPACKED_V1;
   }
}

/**
 * total actual len is sum of actual length across all buffers.
 *
 * see comments next to cmn_port_ptr->bufs_ptr
 */
static inline uint32_t gen_topo_get_total_actual_len(gen_topo_common_port_t *cmn_port_ptr)
{
   uint32_t total_actual_len;

   if (SPF_DEINTERLEAVED_RAW_COMPRESSED == cmn_port_ptr->media_fmt_ptr->data_format)
   {
      // each ch can be of diff length in deint-raw-compr
      total_actual_len = cmn_port_ptr->bufs_ptr[0].actual_data_len;
      for (uint32_t b = 1; b < cmn_port_ptr->sdata.bufs_num; b++)
      {
         total_actual_len += cmn_port_ptr->bufs_ptr[b].actual_data_len;
      }
   }
   else
   {
      total_actual_len = cmn_port_ptr->bufs_ptr[0].actual_data_len * cmn_port_ptr->sdata.bufs_num;
   }

   return total_actual_len;
}

static inline void gen_topo_set_all_bufs_len_to_zero(gen_topo_common_port_t *cmn_port_ptr)
{
   for (uint32_t b = 0; b < gen_topo_get_num_sdata_bufs_to_update(cmn_port_ptr); b++)
   {
      cmn_port_ptr->bufs_ptr[b].actual_data_len = 0;
   }
}

/**
 * This is mainly for MD propagation or timestamp.
 * For deint-raw compr, only first buf len is used.
 * for PCM/packetized: deint-packed & interleaved, division is needed.
 * For all others including raw compr, PCM unpacked, packetized, use first buf len
 */
static inline uint32_t gen_topo_convert_len_per_buf_to_len_per_ch(topo_media_fmt_t *med_fmt_ptr, uint32_t len_per_buf)
{
   if (SPF_IS_PACKETIZED_OR_PCM(med_fmt_ptr->data_format) && !TU_IS_ANY_DEINTERLEAVED_UNPACKED(med_fmt_ptr->pcm.interleaving))
   {
      return topo_div_num(len_per_buf, med_fmt_ptr->pcm.num_channels);
   }
   return len_per_buf;
}

static inline uint32_t gen_topo_convert_len_per_ch_to_len_per_buf(topo_media_fmt_t *med_fmt_ptr, uint32_t len_per_ch)
{
   if (SPF_IS_PACKETIZED_OR_PCM(med_fmt_ptr->data_format) && !TU_IS_ANY_DEINTERLEAVED_UNPACKED(med_fmt_ptr->pcm.interleaving))
   {
      return (len_per_ch * med_fmt_ptr->pcm.num_channels);
   }
   return len_per_ch;
}

static inline uint32_t gen_topo_get_out_port_data_len(void *topo_ctx_ptr, void *out_port_ctx_ptr, bool_t is_max)
{
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_ctx_ptr;
   gen_topo_common_port_t *cmn_port_ptr = &out_port_ptr->common;
   return is_max ? gen_topo_get_total_max_len(cmn_port_ptr) : gen_topo_get_total_actual_len(cmn_port_ptr);
}

/* Don't call this directly. call gen_topo_check_copy_incoming_ts */
bool_t gen_topo_check_copy_incoming_ts_util_(gen_topo_t *           topo_ptr,
                                             gen_topo_module_t *    next_module_ptr,
                                             gen_topo_input_port_t *next_in_port_ptr,
                                             int64_t                incoming_ts,
                                             bool_t                 incoming_ts_valid,
                                             bool_t                 continue_ts);

/* returns whether TS is discontinuous */
static inline bool_t gen_topo_check_copy_incoming_ts(gen_topo_t *           topo_ptr,
                                       gen_topo_module_t *    next_module_ptr,
                                       gen_topo_input_port_t *next_in_port_ptr,
                                       int64_t                incoming_ts,
                                       bool_t                 incoming_ts_valid,
                                       bool_t                 continue_ts)
{
   capi_stream_data_v2_t *next_sdata_ptr        = &next_in_port_ptr->common.sdata;
   int64_t                next_data_expected_ts = next_sdata_ptr->timestamp;

   if (continue_ts)
   {
      incoming_ts       = next_data_expected_ts;
      incoming_ts_valid = next_sdata_ptr->flags.is_timestamp_valid;
   }

   if (incoming_ts_valid || next_sdata_ptr->flags.is_timestamp_valid)
   {
      return gen_topo_check_copy_incoming_ts_util_(topo_ptr, next_module_ptr, next_in_port_ptr, incoming_ts, incoming_ts_valid, continue_ts);
   }
   return FALSE;
}

///* =======================================================================
// Public Function Declarations
//========================================================================== */
//


//////////////////////////////////////  container_data_port_media_fmt

ar_result_t gen_topo_rtm_dump_data_port_mf_for_all_ports(void *vtopo_ptr, uint32_t container_instance_id, uint32_t port_media_fmt_report_enable);
ar_result_t gen_topo_rtm_dump_change_in_port_mf(gen_topo_t *topo_ptr,
                                                gen_topo_module_t *     module_ptr,
                                                gen_topo_common_port_t *cmn_port_ptr,
                                                uint32_t port_id);

////////////////////////////////////////////////////////////////////////

ar_result_t gen_topo_init_topo(gen_topo_t *topo_ptr, gen_topo_init_data_t *topo_init_data_ptr, POSAL_HEAP_ID heap_id);
ar_result_t gen_topo_destroy_topo(gen_topo_t *topo_ptr);

ar_result_t gen_topo_operate_on_int_in_port(void *                     topo_ptr,
                                            gu_input_port_t *          in_port_ptr,
                                            spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                            uint32_t                   sg_ops,
                                            bool_t                     set_port_op);
ar_result_t gen_topo_operate_on_int_out_port(void *                     topo_ptr,
                                             gu_output_port_t *         out_port_ptr,
                                             spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                             uint32_t                   sg_ops,
                                             bool_t                     set_port_op);
ar_result_t gen_topo_operate_on_int_ctrl_port(void *                     topo_ptr,
                                              gu_ctrl_port_t *           ctrl_port_ptr,
                                              spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                              uint32_t                   sg_ops,
                                              bool_t                     set_port_op);

ar_result_t gen_topo_operate_on_modules(void *                     topo_ptr,
                                        uint32_t                   sg_ops,
                                        gu_module_list_t *         module_list_ptr,
                                        spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

ar_result_t gen_topo_set_port_property(void *                    vtopo_ptr,
                                       topo_port_type_t          port_type,
                                       topo_port_property_type_t prop_type,
                                       void *                    port_ptr,
                                       uint32_t                  val);
ar_result_t gen_topo_get_port_property(void *                    vtopo_ptr,
                                       topo_port_type_t          port_type,
                                       topo_port_property_type_t prop_type,
                                       void *                    port_ptr,
                                       uint32_t *                val_ptr);

ar_result_t gen_topo_set_param(void *ctx_ptr, apm_module_param_data_t *param_ptr);

ar_result_t gen_topo_set_input_port_media_format(gen_topo_t *topo_ptr, gen_topo_input_port_t* input_port_ptr, topo_media_fmt_t *media_fmt_ptr);

void        gen_topo_reset_top_level_flags(gen_topo_t *topo_ptr);
void        gen_topo_update_dm_enabled_flag(gen_topo_t *topo_ptr);
void        gen_topo_update_is_signal_triggered_active_flag(gen_topo_t *topo_ptr);
ar_result_t gen_topo_reset_module(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr);
void        gen_topo_reset_module_capi_dependent_portion(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr);
void        gen_topo_reset_input_port_capi_dependent_portion(gen_topo_t *           topo_ptr,
                                                             gen_topo_module_t *    module_ptr,
                                                             gen_topo_input_port_t *in_port_ptr);
void        gen_topo_reset_output_port_capi_dependent_portion(gen_topo_t *            topo_ptr,
                                                              gen_topo_module_t *     module_ptr,
                                                              gen_topo_output_port_t *out_port_ptr);
ar_result_t topo_basic_reset_input_port(gen_topo_t *me_ptr, gen_topo_input_port_t *in_port_ptr, bool_t use_bufmgr);
ar_result_t topo_shared_reset_input_port(void *topo_ptr, void *topo_out_port_ptr, bool_t use_bufmgr);
static inline ar_result_t gen_topo_reset_input_port(gen_topo_t *topo_ptr, gen_topo_input_port_t *topo_in_port_ptr)
{
   if(!topo_in_port_ptr->common.flags.port_is_not_reset)
   {
      return AR_EOK;
   }
   return topo_shared_reset_input_port(topo_ptr, topo_in_port_ptr, TRUE);
}
ar_result_t topo_basic_reset_output_port(gen_topo_t *me_ptr, gen_topo_output_port_t *out_port_ptr, bool_t use_bufmgr);
ar_result_t topo_shared_reset_output_port(void *topo_ptr, void *topo_out_port_ptr, bool_t use_bufmgr);
static inline ar_result_t gen_topo_reset_output_port(gen_topo_t *topo_ptr, gen_topo_output_port_t *topo_out_port_ptr)
{
   if(!topo_out_port_ptr->common.flags.port_is_not_reset)
   {
      return AR_EOK;
   }
   return topo_shared_reset_output_port(topo_ptr, topo_out_port_ptr, TRUE);
}

ar_result_t gen_topo_reset_all_out_ports(gen_topo_module_t *module_ptr);
ar_result_t gen_topo_reset_all_in_ports(gen_topo_module_t *module_ptr);


topo_sg_state_t gen_topo_get_sg_state(gu_sg_t *sg_ptr);
void            gen_topo_set_sg_state(gu_sg_t *sg_ptr, topo_sg_state_t state);

ar_result_t gen_topo_check_update_started_sorted_module_list(void *vtopo_ptr, bool_t b_force_update);

/* Process context sdata is common for all the module's capi process calls in the topo. Make sure to call reset
   in the begining of each module's process context. */
static inline void gen_topo_reset_process_context_sdata(gen_topo_process_context_t *pc, gen_topo_module_t *module_ptr)
{
   for (uint32_t ip_idx = 0; ip_idx < module_ptr->gu.max_input_ports; ip_idx++)
   {
      pc->in_port_sdata_pptr[ip_idx] = NULL;
   }

   for (uint32_t op_idx = 0; op_idx < module_ptr->gu.max_output_ports; op_idx++)
   {
      pc->out_port_sdata_pptr[op_idx] = NULL;
   }
}

/**-------------------------------- gen_topo_pm ---------------------------------*/
ar_result_t gen_topo_aggregate_kpps_bandwidth(gen_topo_t *topo_ptr,
                                              bool_t      only_aggregate,
                                              uint32_t *  aggregate_kpps_ptr,
                                              uint32_t *  aggregate_bw_ptr,
                                              uint32_t *  scaled_kpps_q10_agg_ptr,
                                              uint32_t *  scaled_bw_agg_ptr);

void gen_topo_exit_island_temporarily(gen_topo_t *topo_ptr);

/*
 * Basic topo process call that can be used from CdC and GEN_CNTR
 * */
ar_result_t gen_topo_topo_process(gen_topo_t *topo_ptr, gu_module_list_t **start_module_list_pptr, uint8_t *path_index_ptr);

//////////////////////////////////////  NBLC
bool_t      gen_topo_is_port_at_nblc_end(gu_module_t *gu_module_ptr, gen_topo_common_port_t *cmn_port_ptr);
ar_result_t gen_topo_assign_non_buf_lin_chains(gen_topo_t *topo_ptr);
ar_result_t gen_topo_propagate_requires_data_buffering_upstream(gen_topo_t *topo_ptr);

//////////////////////////////////////  GEN_TOPO_TOPO_HANDLER_H_
ar_result_t gen_topo_query_and_create_capi(gen_topo_t *           topo_ptr,
                                           gen_topo_graph_init_t *graph_init_ptr,
                                           gen_topo_module_t *    module_ptr);

ar_result_t gen_topo_init_set_get_data_port_properties(gen_topo_module_t *module_ptr,
                                                       gen_topo_t *topo_ptr,
                                                       bool_t is_placeholder_replaced,
                                                       gen_topo_graph_init_t* graph_init_ptr);

ar_result_t gen_topo_create_modules(gen_topo_t *topo_ptr, gen_topo_graph_init_t *graph_init_ptr);
ar_result_t gen_topo_check_n_realloc_scratch_memory(gen_topo_t *topo_ptr, bool_t is_open_context);

ar_result_t gen_topo_destroy_modules(gen_topo_t *topo_ptr, bool_t b_destroy_all_modules);
void gen_topo_destroy_module(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr, bool_t remove_capi_dependent_only);

void gen_topo_set_med_fmt_to_attached_module(gen_topo_t *topo_ptr, gen_topo_output_port_t *out_port_ptr);
ar_result_t gen_topo_propagate_media_fmt(void *topo_ptr, bool_t is_data_path);
ar_result_t gen_topo_propagate_media_fmt_from_module(void *            cxt_ptr,
                                                     bool_t            is_data_path,
                                                     gu_module_list_t *start_module_list_ptr);

bool_t gen_topo_is_module_sg_stopped(gen_topo_module_t *module_ptr);
bool_t gen_topo_is_module_sg_stopped_or_suspended(gen_topo_module_t *module_ptr);
bool_t gen_topo_is_module_sg_started(gen_topo_module_t *module_ptr);

bool_t gen_topo_is_module_data_trigger_policy_active(gen_topo_module_t *module_ptr);
/*
 * Returns True if module has an active signal trigger policy.
 */
static inline bool_t gen_topo_is_module_signal_trigger_policy_active(gen_topo_module_t *module_ptr)
{
   bool_t has_module_raised_trigger_policy =
      (NULL != module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(GEN_TOPO_SIGNAL_TRIGGER)]);

   return has_module_raised_trigger_policy;
}

bool_t gen_topo_is_module_active(gen_topo_module_t *module_ptr, bool_t need_to_ignore_state);
void   gen_topo_set_module_active_flag_for_all_modules(gen_topo_t *topo_ptr);

void gen_topo_find_cached_set_event_prop(uint32_t         log_id,
                                         spf_list_node_t *event_list_ptr,
                                         uint32_t         event_id,
                                         void **          cached_node);

uint32_t gen_topo_get_curr_port_threshold(gen_topo_common_port_t *port_ptr);

uint32_t gen_topo_get_curr_port_bufs_num_v2(gen_topo_common_port_t *port_ptr);

uint32_t gen_topo_get_port_threshold(void *port_ptr);

uint32_t gen_topo_get_bufs_num_from_med_fmt(topo_media_fmt_t *med_fmt_ptr);

ar_result_t gen_topo_check_and_set_default_port_threshold(gen_topo_module_t *     module_ptr,
                                                          gen_topo_common_port_t *cmn_port_ptr);

ar_result_t gen_topo_destroy_cmn_port(gen_topo_t *            me_ptr,
                                      gen_topo_common_port_t *cmn_port_ptr,
                                      gu_cmn_port_t *         gu_cmn_port_ptr,
                                      bool_t                  remove_capi_dependent_only);
ar_result_t gen_topo_destroy_input_port(gen_topo_t *me_ptr, gen_topo_input_port_t *in_port_ptr);

ar_result_t gen_topo_destroy_output_port(gen_topo_t *me_ptr, gen_topo_output_port_t *out_port_ptr);

///////////////////////////////////////// GEN_TOPO_TOPO_HANDLER_H_

//////////////////////////////////////////// GEN_TOPO_FWK_EXTN_UTILS_H

ar_result_t gen_topo_fmwk_extn_handle_at_init(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr);
ar_result_t gen_topo_fmwk_extn_handle_at_deinit(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr);
ar_result_t gen_topo_fwk_ext_set_cntr_frame_dur_per_module(gen_topo_t        *topo_ptr,
                                                           gen_topo_module_t *module_ptr,
                                                           uint32_t           frame_len_us);
ar_result_t gen_topo_fwk_ext_set_cntr_frame_dur(gen_topo_t *topo_ptr, uint32_t frame_len_us);

//////////////////////////////////////////////// GEN_TOPO_FWK_EXTN_UTILS_H

//////////////////////////////////////////////// GEN_TOPO_INTF_EXTN_UTILS_H

ar_result_t gen_topo_intf_extn_handle_at_init(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr);

/////////////////////////////////////////////// gen_topo_get_capi_callback_handler

capi_err_t gen_topo_handle_process_state_event(gen_topo_t                 *topo_ptr,
                                               gen_topo_module_t          *module_ptr,
                                               capi_event_info_t          *event_info_ptr);

// Handling of capi v2 event callbacks that is shared between topology implementations.
capi_err_t gen_topo_capi_callback_base(gen_topo_module_t *module_ptr,
                                       capi_event_id_t    id,
                                       capi_event_info_t *event_info_ptr);

// Topo_cmn capi v2 event callback handling.
capi_err_t gen_topo_capi_callback_non_island(void *context_ptr, capi_event_id_t id, capi_event_info_t *event_info_ptr);

capi_err_t gen_topo_capi_callback(void *context_ptr, capi_event_id_t id, capi_event_info_t *event_info_ptr);

/////////////////////////////////////////////// gen_topo_get_capi_callback_handler

ar_result_t gen_topo_set_event_reg_prop_to_capi_modules(uint32_t           log_id,
                                                        capi_t *           capi_ptr,
                                                        gen_topo_module_t *module_ptr,
                                                        topo_reg_event_t * payload_ptr,
                                                        bool_t             is_register,
                                                        bool_t *           store_client_info_ptr);

////////////////////////////////////////////// metadata
static inline bool_t gen_topo_fwk_owns_md_prop(gen_topo_module_t *module_ptr)
{
   if (module_ptr->bypass_ptr)
   {
      // Fwk will always propagate metadata for bypassed modules.
      return TRUE;
   }

   if (module_ptr->flags.supports_metadata)
   {
      return FALSE;
   }

   // if source module needs to introduce metadata, then will have  module_ptr->flags.supports_metadata=TRUE
   if (module_ptr->gu.flags.is_siso || module_ptr->gu.flags.is_sink /* || module_ptr->gu.flags.is_source */)
   {
      return TRUE;
   }

   return FALSE;
}

/**
 * Metadata propagation uses bytes for all channels for PCM.
 * for deint-raw-compr -> only first channel
 */
static inline uint32_t gen_topo_get_actual_len_for_md_prop(gen_topo_common_port_t *cmn_port_ptr)
{
   if (SPF_DEINTERLEAVED_RAW_COMPRESSED == cmn_port_ptr->media_fmt_ptr->data_format)
   {
      return cmn_port_ptr->bufs_ptr[0].actual_data_len;
   }
   else
   {
      return (cmn_port_ptr->bufs_ptr[0].actual_data_len * cmn_port_ptr->sdata.bufs_num);
   }
}

/**Return bytes required for MD propagation based on len per buffer.
 * Metadata propagation uses bytes for all channels for PCM.
 * for deint-raw-compr -> only first channel.
 */
static inline uint32_t gen_topo_get_bytes_for_md_prop_from_len_per_buf(gen_topo_common_port_t *cmn_port_ptr, uint32_t len_per_buf)
{
   if (SPF_DEINTERLEAVED_RAW_COMPRESSED == cmn_port_ptr->media_fmt_ptr->data_format)
   {
      return len_per_buf;
   }
   else
   {
      return (len_per_buf * cmn_port_ptr->sdata.bufs_num);
   }
}

// Note: never call this function directly, use only gen_topo_propagate_metadata()
ar_result_t gen_topo_propagate_metadata_util_(gen_topo_t *       topo_ptr,
                                              gen_topo_module_t *module_ptr,
                                              uint32_t           in_bytes_before,
                                              uint32_t           in_bytes_consumed,
                                              uint32_t           out_bytes_produced,
                                              bool_t             is_one_inp_sink_module);

static inline ar_result_t gen_topo_propagate_metadata(gen_topo_t *       topo_ptr,
                                                      gen_topo_module_t *module_ptr,
                                                      bool_t             input_has_metadata_or_eos,
                                                      uint32_t           in_bytes_before,
                                                      uint32_t           in_bytes_consumed,
                                                      uint32_t           out_bytes_produced)
{
   ar_result_t result = AR_EOK;
   if (!input_has_metadata_or_eos && (NULL == module_ptr->int_md_list_ptr))
   {
      return result;
   }

   // handle propagation for modules that don't support_metadata and not in bypass state.
   if ((module_ptr->flags.supports_metadata) && (NULL == module_ptr->bypass_ptr))
   {
      return result;
   }

   if (module_ptr->gu.flags.is_siso)
   {
      bool_t IS_ONE_INP_SINK_MODULE_FALSE = FALSE;

      //When md lib is compiled in nlpi, need to vote against island so that it can stay in nlpi for 2 frames.
      //This will help in avoiding multiple exit/entry in pi as we propagate metadata
      gen_topo_vote_against_lpi_if_md_lib_in_nlpi(topo_ptr);
      return gen_topo_propagate_metadata_util_(topo_ptr,
                                               module_ptr,
                                               in_bytes_before,
                                               in_bytes_consumed,
                                               out_bytes_produced,
                                               IS_ONE_INP_SINK_MODULE_FALSE);
   }
   else if (module_ptr->gu.flags.is_sink &&
            ((1 == module_ptr->gu.num_input_ports) && (1 == module_ptr->gu.max_input_ports))) // is_one_inp_sink_module
   {
      bool_t IS_ONE_INP_SINK_MODULE_TRUE = TRUE;
      //When md lib is compiled in nlpi, need to vote against island so that it can stay in nlpi for 2 frames.
      //This will help in avoiding multiple exit/entry in pi as we propagate metadata
      gen_topo_vote_against_lpi_if_md_lib_in_nlpi(topo_ptr);
      return gen_topo_propagate_metadata_util_(topo_ptr,
                                               module_ptr,
                                               in_bytes_before,
                                               in_bytes_consumed,
                                               out_bytes_produced,
                                               IS_ONE_INP_SINK_MODULE_TRUE);
   }

   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_ERROR_PRIO,
            " Metadata propagation for non-SISO, non-sink modules must be done inside the modules, module "
            "0x%lX",
            module_ptr->gu.module_instance_id);

   return result;
}

static inline ar_result_t gen_topo_metadata_adj_offset_after_process_for_inp(gen_topo_t *           topo_ptr,
                                                                             gen_topo_input_port_t *in_port_ptr,
                                                                             uint32_t total_bytes_consumed)
{
   if (!in_port_ptr->common.sdata.metadata_list_ptr)
   {
      return AR_EOK;
   }

   // Dont need to call island exit here even when md lib is compiled in nlpi, because we would have exited
   // island while copying md at container input or propagating md

   // no need to set marker EOS for flushing EOS as the
   // gen_topo_capi_metadata_propagate func already takes care of this.
   return gen_topo_metadata_adj_offset(topo_ptr,
                                       in_port_ptr->common.media_fmt_ptr,
                                       in_port_ptr->common.sdata.metadata_list_ptr,
                                       total_bytes_consumed,
                                       FALSE /* subtract */);
}

static inline ar_result_t gen_topo_metadata_adj_offset_after_process_for_out(gen_topo_t *            topo_ptr,
                                                                             gen_topo_module_t *     module_ptr,
                                                                             gen_topo_output_port_t *out_port_ptr,
                                                                             uint32_t                bytes_existing,
                                                                             capi_err_t              proc_result)
{
   if (!out_port_ptr->common.sdata.metadata_list_ptr)
   {
      return AR_EOK;
   }

   //Dont need to call island exit here even when md lib is compiled in nlpi, because we would have exited
   //island while copying md at container input or propagating md

   // update the offset of metadata based on the number of bytes that already exist in the output buffer
   // no need to set marker EOS for flushing EOS as the
   // gen_topo_capi_metadata_propagate func already takes care of this.
   return gen_topo_metadata_adj_offset(topo_ptr,
                                       out_port_ptr->common.media_fmt_ptr,
                                       out_port_ptr->common.sdata.metadata_list_ptr,
                                       bytes_existing,
                                       TRUE /* Add */);
}

/**
 * zero pushing is only for flushing EOS
 * when zeros are pushed the offset of the EOS has to be pushed to after the zeros.
 */
static inline ar_result_t gen_topo_push_zeros_at_eos(gen_topo_t *           topo_ptr,
                                                     gen_topo_module_t *    module_ptr,
                                                     gen_topo_input_port_t *in_port_ptr)
{
   ar_result_t result = AR_EOK;

   // When there's at least one flushing EOS, marker_eos is set.
   if (!in_port_ptr->common.sdata.flags.marker_eos)
   {
      return result;
   }
   if ((0 == module_ptr->pending_zeros_at_eos) || (NULL == in_port_ptr->common.bufs_ptr[0].data_ptr))
   {
      return result;
   }
   if (!SPF_IS_PACKETIZED_OR_PCM(in_port_ptr->common.media_fmt_ptr->data_format))
   {
      return result;
   }

   gen_topo_vote_against_lpi_if_md_lib_in_nlpi(module_ptr->topo_ptr);
   return gen_topo_push_zeros_at_eos_util_(topo_ptr, module_ptr, in_port_ptr);
}

ar_result_t gen_topo_move_md_from_ext_in_to_int_in_util_(gen_topo_input_port_t *in_port_ptr,
                                                         uint32_t               log_id,
                                                         module_cmn_md_list_t **src_md_lst_pptr,
                                                         uint32_t               bytes_already_in_dst,
                                                         uint32_t               bytes_already_in_src,
                                                         uint32_t               bytes_copied_from_src,
                                                         topo_media_fmt_t *     src_med_fmt_ptr);

ar_result_t gen_topo_move_md_from_prev_to_next_util_(gen_topo_module_t *     module_ptr,
                                                      gen_topo_input_port_t * next_in_port_ptr,
                                                      gen_topo_output_port_t *prev_out_port_ptr,
                                                      uint32_t                bytes_to_copy,
                                                      uint32_t                bytes_already_in_prev,
                                                      uint32_t                bytes_already_in_next);


static inline ar_result_t gen_topo_move_md_from_prev_to_next(gen_topo_module_t *     module_ptr,
                                                             gen_topo_input_port_t * next_in_port_ptr,
                                                             gen_topo_output_port_t *prev_out_port_ptr,
                                                             uint32_t                bytes_to_copy,
                                                             uint32_t                bytes_already_in_prev,
                                                             uint32_t                bytes_already_in_next)
{
   // if there is no metadata being moved from prev to next
   // && ( if there is not metadata offset adjustment in the next input due to incoming data we can return early
   if (!prev_out_port_ptr->common.sdata.metadata_list_ptr &&
       !((bytes_to_copy > 0) && (module_ptr->int_md_list_ptr || next_in_port_ptr->common.sdata.metadata_list_ptr)))
   {
      return AR_EOK;
   }

   //When md lib is compiled in nlpi, need to vote against island so that it can stay in nlpi for 2 frames.
   //This will help in avoiding multiple exit/entry in pi as we propagate metadata
   gen_topo_vote_against_lpi_if_md_lib_in_nlpi(module_ptr->topo_ptr);
   return gen_topo_move_md_from_prev_to_next_util_(module_ptr,
                                                   next_in_port_ptr,
                                                   prev_out_port_ptr,
                                                   bytes_to_copy,
                                                   bytes_already_in_prev,
                                                   bytes_already_in_next);
}


static inline ar_result_t gen_topo_move_md_from_ext_in_to_int_in(gen_topo_input_port_t *in_port_ptr,
                                                                 uint32_t               log_id,
                                                                 module_cmn_md_list_t **src_md_lst_pptr,
                                                                 uint32_t               bytes_already_in_dst,
                                                                 uint32_t               bytes_already_in_src,
                                                                 uint32_t               bytes_copied_from_src,
                                                                 topo_media_fmt_t *     src_med_fmt_ptr)
{
   // if there is not metadata being moved from ext in to next
   // && if there is not metadata offset adjustment in the next input due to bytes_copied_from_src then return
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;
   if (!*src_md_lst_pptr &&
       !((bytes_copied_from_src > 0) && (module_ptr->int_md_list_ptr || in_port_ptr->common.sdata.metadata_list_ptr)))
   {
      return AR_EOK;
   }

   //When md lib is compiled in nlpi, need to vote against island so that it can stay in nlpi for 2 frames.
   //Eventhough we already exited island from propagate md function this will help if ext input has
   //more than 2 frames worth of md
   gen_topo_vote_against_lpi_if_md_lib_in_nlpi(module_ptr->topo_ptr);
   return gen_topo_move_md_from_ext_in_to_int_in_util_(in_port_ptr,
                                                       log_id,
                                                       src_md_lst_pptr,
                                                       bytes_already_in_dst,
                                                       bytes_already_in_src,
                                                       bytes_copied_from_src,
                                                       src_med_fmt_ptr);
}

ar_result_t gen_topo_handle_data_flow_begin_util_(gen_topo_t *            topo_ptr,
                                                  gen_topo_common_port_t *cmn_port_ptr,
                                                  gu_cmn_port_t *         gu_cmn_port_ptr);

static inline ar_result_t gen_topo_handle_data_flow_begin(gen_topo_t *            topo_ptr,
                                                          gen_topo_common_port_t *cmn_port_ptr,
                                                          gu_cmn_port_t *         gu_cmn_port_ptr)
{
   ar_result_t result = AR_EOK;
   if (TOPO_DATA_FLOW_STATE_FLOWING == cmn_port_ptr->data_flow_state)
   {
      return result;
   }

   gen_topo_handle_data_flow_begin_util_(topo_ptr, cmn_port_ptr, gu_cmn_port_ptr);

   return result;
}

static inline void gen_topo_handle_eof_history(gen_topo_input_port_t *next_in_port_ptr, bool_t bytes_copied)
{
   capi_stream_data_v2_t *next_sdata_ptr = &next_in_port_ptr->common.sdata;
   /**
    * Set was_eof_set if sdata EOF of next is set.
    * Clear was_eof_set if any data is copied without end of frame.
    */
   if (next_sdata_ptr->flags.end_of_frame)
   {
      next_in_port_ptr->flags.was_eof_set = TRUE;
   }
   else
   {
      if (bytes_copied)
      {
         next_in_port_ptr->flags.was_eof_set = FALSE;
      }
   }
}

ar_result_t gen_topo_set_pending_zeros(gen_topo_module_t *module_ptr, gen_topo_input_port_t *in_port_ptr);



//////////////////////////////// gen_topo_data_process.cpp

ar_result_t gen_topo_populate_sdata_bufs(gen_topo_t *            topo_ptr,
                                         gen_topo_module_t *     module_ptr,
                                         gen_topo_output_port_t *out_port_ptr,
                                         bool_t                  err_check);

ar_result_t gen_topo_propagate_boundary_modules_port_state(void *base_ptr);

bool_t gen_topo_output_port_is_size_known(void *ctx_topo_ptr, void *ctx_out_port_ptr);

// TODO: check the MIPS impact of making these non-static
ar_result_t gen_topo_copy_data_from_prev_to_next(gen_topo_t *            topo_ptr,
                                                 gen_topo_module_t *     next_module_ptr,
                                                 gen_topo_input_port_t * next_in_port_ptr,
                                                 gen_topo_output_port_t *prev_out_port_ptr,
                                                 bool_t                  after);
capi_err_t gen_topo_copy_input_to_output(gen_topo_t *        topo_ptr,
                                         gen_topo_module_t * module_ptr,
                                         capi_stream_data_t *inputs[],
                                         capi_stream_data_t *outputs[]);
ar_result_t gen_topo_update_out_buf_len_after_process(gen_topo_t *            topo_ptr,
                                                      gen_topo_output_port_t *out_port_ptr,
                                                      bool_t                  err_check,
                                                      uint32_t *              prev_actual_data_len);
ar_result_t gen_topo_err_check_inp_buf_len_after_process(gen_topo_module_t *    module_ptr,
                                                                         gen_topo_input_port_t *in_port_ptr,
                                                                         uint32_t *             prev_actual_data_len);
ar_result_t gen_topo_handle_end_of_frame_after_process(gen_topo_t *           topo_ptr,
                                                       gen_topo_module_t *    module_ptr,
                                                       gen_topo_input_port_t *in_port_ptr,
                                                       uint32_t *             in_bytes_given_per_buf,
                                                       bool_t                 need_more);

ar_result_t gen_topo_move_data_to_beginning_after_process(gen_topo_t *           topo_ptr,
                                                          gen_topo_input_port_t *in_port_ptr,
                                                          uint32_t               remaining_size_after_per_buf,
                                                          uint32_t *             prev_actual_data_len);
ar_result_t gen_topo_sync_to_input_timestamp(gen_topo_t *           topo_ptr,
                                             gen_topo_input_port_t *in_port_ptr,
                                             uint32_t               data_consumed_in_process_per_buf);
void gen_topo_process_attached_elementary_modules(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr);
void gen_topo_process_attached_module_to_output(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr, gen_topo_output_port_t *out_port_ptr);

void gen_topo_drop_data_after_mod_process(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr, gen_topo_output_port_t *out_port_ptr);
void gen_topo_print_process_error(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr);


/////////////// TOPO propagation ////////////////////////////
ar_result_t gen_topo_propagate_port_props(void *base_ptr, topo_port_property_type_t prop_type);

ar_result_t gen_topo_handle_port_propagated_capi_event(gen_topo_t                 *topo_ptr,
                                                       gen_topo_module_t          *module_ptr,
                                                       capi_event_info_t          *event_info_ptr);

ar_result_t gen_topo_set_propagated_port_property_on_the_port(gen_topo_common_port_t *  dest_port_ptr,
                                                              topo_port_property_type_t prop_type,
                                                              void *                    payload_ptr);

ar_result_t gen_topo_propagate_port_property_forwards(void *                    vtopo_ptr,
                                                      void *                    vin_port_ptr,
                                                      topo_port_property_type_t prop_type,
                                                      uint32_t                  propagated_value,
                                                      uint32_t *                recurse_depth_ptr);

ar_result_t gen_topo_propagate_port_property_backwards(void *                    vtopo_ptr,
                                                       void *                    vout_port_ptr,
                                                       topo_port_property_type_t prop_type,
                                                       uint32_t                  propagated_value,
                                                       uint32_t *                recurse_depth_ptr);

ar_result_t gen_topo_set_get_propagated_property_on_the_output_port(gen_topo_t               *topo_ptr,
        gen_topo_output_port_t   *out_port_ptr,
        topo_port_property_type_t prop_type,
        void                     *payload_ptr,
        bool_t                   *continue_propagation_ptr);

ar_result_t gen_topo_set_get_propagated_property_on_the_input_port(gen_topo_t               *topo_ptr,
        gen_topo_input_port_t    *in_port_ptr,
        topo_port_property_type_t prop_type,
        void                     *propagated_payload_ptr,
        bool_t                   *continue_propagation_ptr);

static inline bool_t gen_topo_is_input_port_at_sg_boundary(gen_topo_input_port_t *in_port_ptr)
{

   gen_topo_output_port_t *prev_out_port_ptr = (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr;
   if(!prev_out_port_ptr)
   {
      return FALSE;
   }

   return (prev_out_port_ptr->gu.cmn.module_ptr->sg_ptr->id != in_port_ptr->gu.cmn.module_ptr->sg_ptr->id) ? TRUE
                                                                                                           : FALSE;
}

static inline bool_t gen_topo_is_output_port_at_sg_boundary(gen_topo_output_port_t *out_port_ptr)
{
   gen_topo_input_port_t *next_in_port_ptr = (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr;
   if(!next_in_port_ptr)
   {
      return FALSE;
   }

   return (out_port_ptr->gu.cmn.module_ptr->sg_ptr->id != next_in_port_ptr->gu.cmn.module_ptr->sg_ptr->id) ? TRUE
                                                                                                           : FALSE;
}

static inline bool_t gen_topo_is_stm_module(gen_topo_module_t *module_ptr)
{
   return ((module_ptr->flags.need_stm_extn > 0) ? TRUE : FALSE);
}

static inline bool_t gen_topo_is_dm_enabled(gen_topo_module_t *module_ptr)
{
   return (module_ptr->flags.is_dm_disabled == 0);
}

// If a module is inplace (or) dynamic inplace (or) disabled, && current no of in/out ports is '1'.
// then its considered inplace for topo buffer assignment.
static inline bool_t gen_topo_is_inplace_or_disabled_siso(gen_topo_module_t *module_ptr)
{
   return ((module_ptr->flags.inplace || module_ptr->flags.dynamic_inplace ||
            (module_ptr->bypass_ptr && (module_ptr->num_proc_loops == 1))) &&
           (module_ptr->gu.num_input_ports == 1) && (module_ptr->gu.num_output_ports == 1));
}

capi_err_t gen_topo_check_and_update_dynamic_inplace(gen_topo_module_t *module_ptr);

ar_result_t gen_topo_handle_pending_dynamic_inplace_change(gen_topo_t *topo_ptr, gen_topo_capi_event_flag_t* capi_event_flag_ptr);

ar_result_t gen_topo_aggregate_hw_acc_proc_delay(gen_topo_t *topo_ptr, bool_t only_aggregate, uint32_t *aggregate_hw_acc_proc_delay_ptr);

posal_pm_island_vote_t gen_topo_aggregate_island_vote(gen_topo_t *topo_ptr);

///////////// trigger policy
bool_t gen_topo_is_module_data_trigger_condition_satisfied(gen_topo_module_t *         module_ptr,
                                                           bool_t *                    is_ext_trigger_not_satisfied_ptr,
                                                           gen_topo_process_context_t *proc_ctxt_ptr);
void gen_topo_populate_trigger_policy_extn_vtable(gen_topo_module_t *                       module_ptr,
                                                  fwk_extn_param_id_trigger_policy_cb_fn_t *handler_ptr);
ar_result_t gen_topo_trigger_policy_event_data_trigger_in_st_cntr(gen_topo_module_t *module_ptr,
                                                                  capi_buf_t *       payload_ptr);
bool_t gen_topo_is_module_trigger_condition_satisfied(gen_topo_module_t *         module_ptr,
                                                      bool_t *                    is_ext_trigger_not_satisfied_ptr);
gen_topo_data_need_t gen_topo_in_port_needs_data(gen_topo_t *topo_ptr, gen_topo_input_port_t *in_port_ptr);
bool_t               gen_topo_out_port_has_trigger(gen_topo_t *topo_ptr, gen_topo_output_port_t *out_port_ptr);

bool_t                       gen_topo_input_port_is_trigger_present(void *  ctx_topo_ptr,
                                                                         void *  ctx_in_port_ptr,
                                                                         bool_t *is_ext_trigger_not_satisfied_ptr);
bool_t                       gen_topo_input_port_is_trigger_absent(void *  ctx_topo_ptr,
                                                                        void *  ctx_in_port_ptr);
bool_t                       gen_topo_output_port_is_trigger_present(void *  ctx_topo_ptr,
                                                                          void *  ctx_out_port_ptr,
                                                                          bool_t *is_ext_trigger_not_satisfied_ptr);
bool_t                       gen_topo_output_port_is_trigger_absent(void *  ctx_topo_ptr,
                                                                         void *  ctx_out_port_ptr);
fwk_extn_port_nontrigger_policy_t gen_topo_get_nontrigger_policy_for_output(gen_topo_output_port_t *out_port_ptr,
                                                                            gen_topo_trigger_t      curr_trigger);
fwk_extn_port_nontrigger_policy_t gen_topo_get_nontrigger_policy_for_input(gen_topo_input_port_t *in_port_ptr,
                                                                           gen_topo_trigger_t     curr_trigger);
static inline uint32_t gen_topo_get_num_trigger_groups(gen_topo_module_t *module_ptr, gen_topo_trigger_t curr_trigger)
{
   gen_topo_trigger_policy_t *tp_ptr = module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(curr_trigger)];
   if (tp_ptr)
   {
      return tp_ptr->num_trigger_groups;
   }
   return 1;
}

ar_result_t                    gen_topo_destroy_trigger_policy(gen_topo_module_t *module_ptr);
fwk_extn_port_trigger_policy_t gen_topo_get_port_trigger_policy(gen_topo_module_t *module_ptr,
                                                                gen_topo_trigger_t curr_trigger);

ar_result_t gen_topo_propagate_data_trigger_in_st_cntr_event(gen_topo_module_t *module_ptr);

static inline bool_t gen_topo_is_module_need_data_trigger_in_st_cntr(gen_topo_module_t *module_ptr)
{
   return (BLOCKED_DATA_TRIGGER_IN_ST != module_ptr->flags.need_data_trigger_in_st) ? TRUE : FALSE;
}

static inline bool_t gen_topo_has_module_allowed_data_trigger_in_st_cntr(gen_topo_module_t *module_ptr)
{
   return (ALLOW_DATA_TRIGGER_IN_ST_EVENT == module_ptr->flags.need_data_trigger_in_st) ? TRUE : FALSE;
}

/*
 *  Check if data triggers are allowed for a module
 *  Even if module has not raised trigger policy this can return TRUE (unlike above func)
 *
 *  if gen_topo_is_module_data_trigger_policy_active is active then no need to check
 * gen_topo_is_module_data_trigger_allowed
 */
static inline bool_t gen_topo_is_module_data_trigger_allowed(gen_topo_module_t *module_ptr)
{
   gen_topo_t *topo_ptr = module_ptr->topo_ptr;

   // If Container is not signal triggered then data triggers are allowed for all module.
   if (!topo_ptr->flags.is_signal_triggered)
   {
      return TRUE;
   }

   // Data triggers are not allowed in Signal Triggered container unless module raises
   // #FWK_EXTN_EVENT_ID_DATA_TRIGGER_IN_ST_CNTR event.
   bool_t b_need_data_trigger_in_st = gen_topo_is_module_need_data_trigger_in_st_cntr(module_ptr);
   return b_need_data_trigger_in_st;
}

static inline bool_t gen_topo_top_level_trigger_condition_satisfied(gen_topo_module_t *module_ptr)
{
   gen_topo_t *topo_ptr = module_ptr->topo_ptr;

   // if current trigger is Signal triggered top level trigger condition is satisfied
   if (GEN_TOPO_DATA_TRIGGER != topo_ptr->proc_context.curr_trigger)
   {
      return TRUE;
   }

   if (!gen_topo_is_module_data_trigger_allowed(module_ptr))
   {
#ifdef TRIGGER_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " Module 0x%lX: Data Trigger is not allowed.",
               module_ptr->gu.module_instance_id);
#endif
      return FALSE;
   }

   return TRUE;
}

capi_err_t gen_topo_change_trigger_policy_cb_fn(gen_topo_trigger_t                trigger_type,
                                                       void *                            context_ptr,
                                                       fwk_extn_port_nontrigger_group_t *nontriggerable_ports_ptr,
                                                       fwk_extn_port_trigger_policy_t    port_trigger_policy,
                                                       uint32_t                          num_groups,
                                                       fwk_extn_port_trigger_group_t *   triggerable_groups_ptr);

capi_err_t gen_topo_change_data_trigger_policy_cb_fn(void *                            context_ptr,
                                                            fwk_extn_port_nontrigger_group_t *nontriggerable_ports_ptr,
                                                            fwk_extn_port_trigger_policy_t    port_trigger_policy,
                                                            uint32_t                          num_groups,
                                                            fwk_extn_port_trigger_group_t *   triggerable_groups_ptr);

capi_err_t gen_topo_change_signal_trigger_policy_cb_fn( void *                            context_ptr,
                                                               fwk_extn_port_nontrigger_group_t *nontriggerable_ports_ptr,
                                                               fwk_extn_port_trigger_policy_t    port_trigger_policy,
                                                               uint32_t                          num_groups,
                                                               fwk_extn_port_trigger_group_t *   triggerable_groups_ptr);

//function to check if module requires fixed frame len for trigger.
static inline bool_t gen_topo_does_module_requires_fixed_frame_len(gen_topo_module_t *module_ptr)
{
   return (module_ptr->flags.need_sync_extn) ? !module_ptr->flags.is_threshold_disabled
                                             : !module_ptr->flags.requires_data_buf;
}

void gen_topo_update_data_tpm_count(gen_topo_t *topo_ptr);
/** ------------------------------------------- data flow state -----------------------------------------------------*/
ar_result_t gen_topo_handle_data_flow_preflow(gen_topo_t *            topo_ptr,
                                              gen_topo_common_port_t *cmn_port_ptr,
                                              gu_cmn_port_t *         gu_cmn_port_ptr);
ar_result_t gen_topo_handle_data_flow_end(gen_topo_t *            topo_ptr,
                                          gen_topo_common_port_t *cmn_port_ptr,
                                          gu_cmn_port_t *         gu_cmn_port_ptr);
bool_t gen_topo_check_if_all_src_are_ftrt_n_at_gap(gen_topo_t *topo_ptr, bool_t *is_ftrt_ptr);

void gen_topo_handle_fwk_ext_at_dfs_util_(gen_topo_t *topo_ptr, gu_cmn_port_t *cmn_port_ptr);
static inline void gen_topo_handle_fwk_ext_at_dfs(gen_topo_t *topo_ptr, gu_cmn_port_t *cmn_port_ptr)
{
   if(FALSE == topo_ptr->flags.is_sync_module_present)
   {
      return; // nothing to do
   }
   return gen_topo_handle_fwk_ext_at_dfs_util_(topo_ptr, cmn_port_ptr);
}

/** ------------------------------------------- mf_utils related -----------------------------------------------------*/
void gen_topo_set_default_media_fmt_at_open(gen_topo_t *topo_ptr);


///////////////////////////////// SIGNAL triggered topo function //////////////////////////////
ar_result_t st_topo_process(gen_topo_t *topo_ptr, gu_module_list_t **start_module_list_pptr);
void st_topo_drop_stale_data(gen_topo_t *topo_ptr, gen_topo_output_port_t *out_port_ptr);

static inline bool_t gen_topo_output_has_data(gen_topo_output_port_t *out_port_ptr)
{
   return (NULL != out_port_ptr->common.bufs_ptr[0].data_ptr) && (out_port_ptr->common.bufs_ptr[0].actual_data_len);
}

static inline bool_t gen_topo_input_has_data_or_md(gen_topo_input_port_t *in_port_ptr)
{
   return (NULL != in_port_ptr->common.bufs_ptr[0].data_ptr) &&
          (in_port_ptr->common.bufs_ptr[0].actual_data_len || in_port_ptr->common.sdata.metadata_list_ptr);
}

///////////////////////////////// CAPI EVENT HANDLING RELATED - START ////////////////////////////////

/* Following macro should be used to set one capi event flag.
 * This macros ensure that event is set in the correct event_flag based on the command or data path processing context.
 */
#define GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, flag)                                                               \
   {                                                                                                                   \
      gen_topo_capi_event_flag_t *capi_set_event_flag_ptr = gen_topo_get_capi_event_flag_(topo_ptr);                   \
      capi_set_event_flag_ptr->flag = TRUE;                                                                            \
   }

/* Following macro should be used to set more than one capi event flags.
 * This macros ensure that event is set in the correct event_flag based on the command or data path processing context.
 */
#define GEN_TOPO_SET_CAPI_EVENT_FLAGS(topo_ptr, capi_event_flags)                                                      \
   {                                                                                                                   \
      gen_topo_capi_event_flag_t *capi_set_event_flag_ptr = gen_topo_get_capi_event_flag_(topo_ptr);                   \
      capi_set_event_flag_ptr->word |= capi_event_flags.word;                                                          \
   }
/*
 * Following two macros should be used carefully in an event handling function.
 * If only one of them is used then it will cause compilation error.
 * This is designed to remind the developers that there are two separate event flags
 * one for command handling context and one for data-path processing/event_handling.
 * */

//this macro should be used at the top of the event handling function.
//if this macro is used alone in a function then will get an unused variable error.
//Function where this Macro is used, must be executing synchronously with the Main data path processing thread. (Critical Section lock is required)
#define GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT bool_t use_the_macro_only_in_the_capi_event_handling_context;

//this macro should be used to read/write the capi_event_flag only in event handler context.
//it takes topo_ptr as its first argument and a boolean "do_reconcile" as its second argument.
//if do_reconcile is TRUE then any pending event from the command handling context is reconciled with the main capi event flag.
//if this macro is used alone in a function then will get undefined symbol error
//set "do_reconcile:FALSE" if handling events in context to data-path processing.
//set "do_reconcile:TRUE" if handling event in context of command handling.
//set "do_reconcile:TRUE: if not sure of the context or a function can be called from any context.
#define GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(event_flag_ptr, topo_ptr, do_reconcile)                    \
   if (do_reconcile)                                                                                                   \
   {                                                                                                                   \
      gen_topo_reconcile_capi_event_flags_((topo_ptr));                                                                \
   }                                                                                                                   \
   event_flag_ptr = (gen_topo_capi_event_flag_t *)&((topo_ptr)->capi_event_flag_);                                     \
   use_the_macro_only_in_the_capi_event_handling_context = 0; /* for error checking    */


//This is a private function and shouldn't be used directly. Use the Macros defined above
static inline void gen_topo_reconcile_capi_event_flags_(gen_topo_t *topo_ptr)
{
#ifdef CONTAINER_ASYNC_CMD_HANDLING
   // if there are any events which are set during the command handling then transfer those events to the main event
   // flag
   topo_ptr->capi_event_flag_ |= topo_ptr->async_capi_event_flag_;
   topo_ptr->async_capi_event_flag_ = 0;
#endif
}

// This is a private function and shouldn't be used directly. Use the Macros defined above
static inline gen_topo_capi_event_flag_t *gen_topo_get_capi_event_flag_(gen_topo_t *topo_ptr)
{
#ifdef CONTAINER_ASYNC_CMD_HANDLING
    /*
     * 1. Return Main Capi Event flag if already executing in data-path processing thread.
    * 2. Return Main Capi Event flag if synchronously executing in command processing thread context.
    * 3. Return Async Capi Event flag if asynchronously executing in command processing thread context.
    * */
   bool_t is_in_data_path_or_sync_handling_context = ((topo_ptr->gu.is_sync_cmd_context_ > 0) ||
           posal_thread_get_curr_tid() == topo_ptr->gu.data_path_thread_id);

   return (is_in_data_path_or_sync_handling_context) ? (gen_topo_capi_event_flag_t *)&topo_ptr->capi_event_flag_
                                                      : (gen_topo_capi_event_flag_t *)&topo_ptr->async_capi_event_flag_;
#else
   return (gen_topo_capi_event_flag_t *)&topo_ptr->capi_event_flag_;
#endif
}
/**
 * events raised at process-call by the modules
 * framework addresses the events and calls the modules again.
 */
static inline bool_t gen_topo_any_process_call_events(gen_topo_t *topo_ptr)
{
   // kpps, bw, rt change can be handled at the end of topo processing. but media fmt, threshold events need
   // to be handled in b/w module processing also.
   // process state - we must break at this module and call the module again to avoid buffering in nblc.
   return (((gen_topo_capi_event_flag_t *)&topo_ptr->capi_event_flag_)->word & (GT_CAPI_EVENT_PORT_THRESH_BIT_MASK | GT_CAPI_EVENT_PROCESS_STATE_BIT_MASK |
                                             GT_CAPI_EVENT_MEDIA_FMT_BIT_MASK));
}

///////////////////////////////// CAPI EVENT HANDLING RELATED - END ////////////////////////////////

ar_result_t gen_topo_validate_port_sdata(uint32_t                log_id,
                                         gen_topo_common_port_t *cmn_port_ptr,
                                         bool_t                  is_input,
                                         uint32_t                port_index,
                                         gen_topo_module_t*      module_ptr,
                                         bool_t                  PROCESS_DONE);

/**
 * when inplace nblc end is assigned as pointer to ext-in port, it may not be 4 byte aligned.
 */
#define PRINT_PORT_INFO_AT_PROCESS(m_iid, port_id, cmn_port, result, str1, str2)                                       \
   do                                                                                                                  \
   {                                                                                                                   \
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,                                                                             \
                      DBG_LOW_PRIO,                                                                                    \
                      " Module 0x%lX: " str1 " port id 0x%lx, process " str2                                           \
                      ": length_per_buf %lu of %lu. buff addr: 0x%p, buf_origin%lu result 0x%lx",                      \
                      m_iid,                                                                                           \
                      port_id,                                                                                         \
                      cmn_port.bufs_ptr[0].actual_data_len,                                                            \
                      cmn_port.bufs_ptr[0].max_data_len,                                                               \
                      cmn_port.bufs_ptr[0].data_ptr,                                                                   \
                      cmn_port.flags.buf_origin,                                                                       \
                      result);                                                                                         \
      if (cmn_port.bufs_ptr[0].data_ptr)                                                                               \
      {                                                                                                                \
         uint64_t temp_num = (uint64_t)cmn_port.bufs_ptr[0].data_ptr;                                                  \
         if (!(temp_num & 0xF))                                                                                        \
         {                                                                                                             \
            uint32_t *data_ptr = (uint32_t *)cmn_port.bufs_ptr[0].data_ptr;                                            \
                                                                                                                       \
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,                                                                       \
                            DBG_LOW_PRIO,                                                                              \
                            " Module 0x%lX: " str1 " bytes (u32): %0lX %0lX %0lX %0lX  %0lX %0lX %0lX",                \
                            m_iid,                                                                                     \
                            *(data_ptr),                                                                               \
                            *(data_ptr + 1),                                                                           \
                            *(data_ptr + 2),                                                                           \
                            *(data_ptr + 3),                                                                           \
                            *(data_ptr + 4),                                                                           \
                            *(data_ptr + 5),                                                                           \
                            *(data_ptr + 6));                                                                          \
         }                                                                                                             \
         else                                                                                                          \
         {                                                                                                             \
            uint8_t *data_ptr = (uint8_t *)cmn_port.bufs_ptr[0].data_ptr;                                              \
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,                                                                       \
                            DBG_LOW_PRIO,                                                                              \
                            " Module 0x%lX: " str1 " bytes (u8): %0X %0X %0X %0X  %0X %0X %0X",                        \
                            m_iid,                                                                                     \
                            *(data_ptr),                                                                               \
                            *(data_ptr + 1),                                                                           \
                            *(data_ptr + 2),                                                                           \
                            *(data_ptr + 3),                                                                           \
                            *(data_ptr + 4),                                                                           \
                            *(data_ptr + 5),                                                                           \
                            *(data_ptr + 6));                                                                          \
         }                                                                                                             \
      }                                                                                                                \
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,                                                                             \
                      DBG_LOW_PRIO,                                                                                    \
                      " Module 0x%lX: " str1 " timestamp: %ld (0x%lx%lx), Flags0x%lX",                                 \
                      m_iid,                                                                                           \
                      (uint32_t)cmn_port.sdata.timestamp,                                                              \
                      (uint32_t)(cmn_port.sdata.timestamp >> 32),                                                      \
                      (uint32_t)cmn_port.sdata.timestamp,                                                              \
                      cmn_port.sdata.flags.word);                                                                      \
   } while (0)

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef GEN_TOPO_H_
