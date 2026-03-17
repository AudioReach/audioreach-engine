/**
 * \file spl_topo.h
 * \brief
 *     Advanced Topology header file.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef ADVANCED_TOPO_H_
#define ADVANCED_TOPO_H_

// clang-format off
#include "gen_topo.h"
#include "gen_topo_capi.h"
#include "spl_topo_trigger_policy_fwk_ext.h"
#include "spl_topo_dm_fwk_ext.h"
#include "spl_topo_sync_fwk_ext.h"
#include "spl_topo_mimo_proc_state_intf_extn.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

// Set SPL_TOPO_DEBUG_LEVEL to one of the below for different levels of debug logs.
// Note: These are only topo-layer debugs. For fwk-layer debugs, check container header files

// Guidelines for prints that should outside of debug macros:
// - Indicate errors that must be addressed (fatal, unrecoverable, breaks use case, calibration errors, etc)
// - Misleading error messages (not always errors) should be rewritten to avoid confusion/mis-triaging.
// - If not errors:
//    - Not printed during steady state (unless error), and otherwise does not clog up the logs
//    - Give information that is useful to know in all or almost all scenarios
//          (graph shape changes, indicator that commands or calibration was received or sent, etc)
// - Warnings should be clearly specified as non errors.
#define SPL_TOPO_DEBUG_LEVEL_NONE 0

// Lowest debug level for prints.
// Guidelines for prints that should be put in DEBUG_LEVEL_1
// - Not an error (errors go outside of debug macros)
// - Not printed during steady state but give information that might not always be essential for triaging.
#define SPL_TOPO_DEBUG_LEVEL_1 1
#define SPL_TOPO_DEBUG_LEVEL_1_FORCE 1 // Indicate we are breaking guidelines intentionally.

// Low debug level for prints.
// Guidelines for prints that should be put in DEBUG_LEVEL_2
// - Not an error (errors go outside of debug macros)
// - Printed at most once per input/output trigger.
#define SPL_TOPO_DEBUG_LEVEL_2 2
#define SPL_TOPO_DEBUG_LEVEL_2_FORCE 2 // Indicate we are breaking guidelines intentionally.

// Medium debug level for prints.
// Guidelines for prints that should be put in DEBUG_LEVEL_3
// - Not an error (errors go outside of debug macros)
// - Printed at most once per module each input/output trigger.
// - Printed at most once per external port each input/output trigger.
#define SPL_TOPO_DEBUG_LEVEL_3 3
#define SPL_TOPO_DEBUG_LEVEL_3_FORCE 3 // Indicate we are breaking guidelines intentionally.

// High debug level for prints.
// Guidelines for prints that should be put in DEBUG_LEVEL_4
// - Not an error (errors go outside of debug macros)
// - Printed at most once per module's internal port each input/output trigger.
#define SPL_TOPO_DEBUG_LEVEL_4 4
#define SPL_TOPO_DEBUG_LEVEL_4_FORCE 4 // Indicate we are breaking guidelines intentionally.

// Highest debug level for prints.
// Guidelines for prints that should be put in DEBUG_LEVEL_5
// - Not an error (errors go outside of debug macros)
// - Printed more than once per module's port each input/output trigger.
#define SPL_TOPO_DEBUG_LEVEL_5 5
#define SPL_TOPO_DEBUG_LEVEL_5_FORCE 5 // Indicate we are breaking guidelines intentionally.

// To get debug prints, you must compile the code with USES_DEBUG_DEV_ENV as a flag.
#ifdef USES_DEBUG_DEV_ENV
// Choose one of the above debug levels.
   //#define SPL_TOPO_DEBUG_LEVEL SPL_TOPO_DEBUG_LEVEL_5
   //#define TOPO_DM_DEBUG
#else
// Don't change this line.
//#define SPL_TOPO_DEBUG_LEVEL SPL_TOPO_DEBUG_LEVEL_NONE
#endif

//#define SPL_SIPT_DBG

#define SPL_TOPO_SG_ID_INVALID 0xFFFFFFFF
#define TU_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, "TU:%08X: " xx_fmt, ID, ##__VA_ARGS__)
#define TU_CATCH_PREFIX "TU:%08X:"

#define SPL_TOPO_INVALID_PATH_INDEX (0xFF)

typedef struct gen_topo_graph_init_t spl_topo_graph_init_t;
typedef struct spl_topo_t               spl_topo_t;
typedef struct spl_topo_module_t        spl_topo_module_t;
typedef struct spl_topo_input_port_t    spl_topo_input_port_t;
typedef struct spl_topo_output_port_t   spl_topo_output_port_t;
typedef struct spl_topo_dm_info_t       spl_topo_dm_info_t;

// Timestamp info attached to external input ports.
typedef struct spl_topo_timestamp_info_t
{
   int64_t timestamp;           /**< The timestamp itself (microseconds). */
   int32_t offset_bytes_per_ch; /**< The sample offset in this buffer which the timestamp refers to.*/
   bool_t  is_valid;            /**< Whether the timestamp is valid or not. */
} spl_topo_timestamp_info_t;

/* List structure. Mirrors spf_list_t to typecast. */
typedef struct spl_topo_timestamp_list_t spl_topo_timestamp_list_t;
struct spl_topo_timestamp_list_t
{
   spl_topo_timestamp_info_t *timestamp_ptr;
   spl_topo_timestamp_list_t *next_ptr;
   spl_topo_timestamp_list_t *prev_ptr;
};

/*
 * External buffer to topo, should be populated by container for any ports connected to external ports.
 * This field will only be populated during spl_topo_process().
 */
typedef struct spl_topo_ext_buf_t
{
   bool_t           send_to_topo;          /**< TRUE if this port should be sent to the topology when the topology is invoked. If FALSE,
                                                setup/adjust steps will be skipped. */
   spl_topo_timestamp_info_t buf_timestamp_info;     /** Timestamp info at the 0th position of the buffer. */
   spl_topo_timestamp_info_t newest_timestamp_info;  /** Most recent timestamp received from data messages. */

   int64_t        timestamp;             /**< The extrapolated timestamp (microseconds) that corresponds to the beginning of the data buffer. */
   bool_t         timestamp_is_valid;    /**< Whether the "timestamp" field is valid or not.*/
   bool_t         timestamp_discontinuity;  /**< Input side - TRUE when there's a timestamp discontinuity between the most recent
                                                 timestamp in the ts list and the incoming ts on the held input data msg. In this case we can't
                                                 buffer more data until the local buffer is emptied.
                                                 Output side - topo layer sends this when acting ext out port receives ts disc. */
   uint32_t       ts_disc_pos_bytes;        /**< Marks the position of the timestamp discontinuity in the output buffer. Used to partition the output
                                                 buffer into data before ts (sent immediately) and data after the ts (stored in "ts_temp" buf and delivered
                                                 immediately when next output buffer is popped from the buffer queue. */
   int64_t        disc_timestamp;           /**< The timestamp value at the timestamp discontinuity corresponding to the start of data after the
                                                 timestamp discontinuity. */

   uint32_t       num_bufs;              /**< one per channel for pcm deinterleaved unpacked */
   capi_buf_t *   buf_ptr;               /**< array of num_bufs */
   uint32_t       bytes_consumed_per_ch; /**< Bytes consumed per channel. We can't simply use
                                              actual_data_len because we need to distinguish
                                              on input side whether all data was consumed
                                              (actual_data_len unchanged) and external input
                                              didn't process case.

                                              Currently only used on external input side. */

   topo_data_flow_state_t data_flow_state;       /**< Data flow state of this buffer. */
   bool_t                 end_of_frame;          /**< Output side - copied over from the end_of_frame of the acting ext op's sdata flag. */
   bool_t                 first_frame_after_gap; /**< Used to help filter internal EOS sent while input port is at-gap */

} spl_topo_ext_buf_t;

typedef enum spl_topo_module_type_t {
   TOPO_MODULE_TYPE_SINGLE_PORT,
   TOPO_MODULE_TYPE_SOURCE,
   TOPO_MODULE_TYPE_SINK,
   TOPO_MODULE_TYPE_MULTIPORT
} spl_topo_module_type_t;

typedef ar_result_t (*fwk_event_cb_f)(void *             context_ptr,
                                      capi_event_id_t    id,
                                      capi_event_info_t *event_info_ptr,
                                      uint32_t           payload_size);

/**< Global state to determine which upper level called us. */
typedef enum spl_topo_cmd_state_t {
   TOPO_CMD_STATE_DEFAULT = 0,
   TOPO_CMD_STATE_SET_PARAM,
   TOPO_CMD_STATE_PROCESS
} spl_topo_cmd_state_t;

/*
 * Cached values for handling fractional resampling. This is currently used to resize buffers
 * to be large enough to hold a full frame of data in the case that the resampler generates
 * greater than the nominal output samples.
 *
 * Current assumptions:
 * 1. There is only one fractional resampling rate in the topology.
 * 2. All external input ports have the same sampling rate.
 *
 * All buffers are resized according to the ratio of their output port's sampling rate
 * to the external input port sampling rate.
 *
 * The framework is responsible for determining when it is handling a fractional resampling
 * use case, and setting these fields properly.
 */
typedef struct spl_topo_fract_resamp_info_t
{
   bool_t is_fractional_use_case; // If FALSE, no resizing is needed, just use nominal size.
   bool_t input_is_fractional;    // Output will only be greater than nominal if output has a
                                  // fractional sample rate. So we can also use the nominal size
                                  // if the input has a fractional sample rate.
   uint32_t input_sample_rate;    // Assumed to be same for all inputs/source modules.
} spl_topo_fract_resamp_info_t;

/* Flags that mark if anything happened during this topo invokation. */
typedef struct spl_topo_state_changed_flags_t
{
   // Becomes TRUE when any module consumed or generated any data during topo process. Applies to a single
   // topology invocation.
   uint32_t data_moved : 1;

   // Becomes TRUE if mf propagated at all during topo process. Applies to a single topology invocation.
   uint32_t mf_moved : 1;

   // Becomes TRUE if process state is raised during a module's process context.
   uint32_t event_raised : 1;
} spl_topo_state_changed_flags_t;

typedef struct spl_topo_process_info_t
{
   // Becomes TRUE when any module consumed or generated any data during topo process. Applies to a single
   // topology invocation.
   spl_topo_state_changed_flags_t state_changed_flags;
} spl_topo_process_info_t;


// Store the container frame length in duration and samples. Samples is used for buffer delivery/fullness criteria,
// duration is used for buffer sizing, rounded up to a half millisecond.
typedef struct spl_topo_frame_length_t
{
   uint32_t sample_rate;  // Hz
   uint32_t frame_len_samples;  // Samples per channel.
   uint32_t frame_len_us;

} spl_topo_frame_length_t;

// framework extension specific info in spl topo
typedef struct spl_topo_fwk_extn_info_t
{
   /* Contains dm informaton if any dm modules are present*/
   spl_topo_dm_info_t            dm_info;
   bool_t                        sync_extn_pending_inactive_handling;
}spl_topo_fwk_extn_info_t;

// Flags for checking if simplified topo processing is possible(for optimization).
// These are split into two structs: level 1 are easy to check, level 2 are harder to check.
// simp_topo_L1 flags: satisfied if all are FALSE and num_tpm == 0.
// The check for these are satisfied is simple and thus does not need an extra check at end of bckwd kick.
typedef union spl_topo_simp_topo_L1_flags_t
{
   struct
   {
      uint32_t backwards_kick           : 1;  // if data is stuck in the topo then we have to trigger backward kick to first drain the stuck data.
      uint32_t threshold_disabled       : 1;  // threshold disabled flag; SYNC extension. set to true if any sync module in topo has disabled threshold.
      uint32_t any_modules_skip_process : 1;  // set if any module in topo can't process.
                                              // module flag is updated and spl_topo_update_module_info function
                                              // topo level aggregation happens in spl_topo_update_simp_module_connections

      uint32_t any_state_not_started    : 1;  // set if any port's downgraded state is not STARTED.
                                              // aggregation happens in spl_topo_update_simp_module_connections

      uint32_t any_source_module        : 1;  // set if there is any source module in the topo.

      // uint32_t active_tp             : 1;  // flag to check if there is active data trigger policy in the topo
                                              // duplicate of topo.t_base.num_data_tpm
   };
   uint32_t word;
} spl_topo_simp_topo_L1_flags_t;

// simp_topo_L2 flags: satisfied if all are TRUE
// To update these requires a loop through all ports or modules in the topo and thus should only be done when required.
// Any time any of these states changes, we move to the spl_topo and set an event flag. At the end of the forward kick,
// we update check each flag only if the corresponding event bit is set.
typedef union spl_topo_simp_topo_L2_flags_t
{
   struct
   {
      uint32_t all_ports_data_flowing  : 1;  // marked when all the ports in topo are in data-flow state.
      // What changes data_flowing:
      // - data arrives at any internal input port
      // - data arrives at external input port

      uint32_t all_ports_valid_mf      : 1;  // marked when all the ports have valid media format
      // What changes valid_mf:
      // external input media format
      // output media format event
      // bypass disable
   };
   uint32_t word;
} spl_topo_simp_topo_L2_flags_t;

// Flags used to mark that we need to check to update simpt_flags at the end of the forward kick.
typedef union spl_topo_simp_topo_event_flags_t
{
   struct
   {
      uint32_t check_data_flow_state : 1;  // set when a ports goes to data-flow state.
                                           // if a port was at-gap and now becomes data-flow state then we have to re-evaluate all_ports_data_flowing

      uint32_t check_valid_mf        : 1;  // set when a port gets valid media format.
                                           // if a port had invalid mf now gets valid mf then we have to re-evaluate all_ports_valid_mf

      uint32_t check_pending_mf      : 1;  // set when media format event is set on external input or output port.
      uint32_t check_eof             : 1;  // set when eof is set on an output port or external input port.
   };
   uint32_t word;
} spl_topo_simp_topo_event_flags_t;


// flags used within spl topo process.
typedef struct spl_topo_flags_t
{
  uint32_t is_bypass_container :1; //set when all the non-elementary modules are disabled.
  uint32_t is_real_time_topo   :1; //true for device Tx PP and for voice call containers.
} spl_topo_flags_t;

typedef struct spl_topo_t
{
   gen_topo_t                    t_base;
   spl_topo_frame_length_t       cntr_frame_len;       /**< Frame length of the container */
   spl_topo_cmd_state_t          cmd_state;
   spl_topo_process_info_t       proc_info; /**< Topo2 info used during spl_topo_process(). */

   gu_module_list_t  *req_samp_query_start_list_ptr; /* list of module where required-sample queries should start from to calculate
                                                        the required number of input samples at external input port.
                                                        this could be threshold module/trigger policy module/boundary module*/

   bool_t                        during_max_traversal; /*set to true if we are in the midst of path traversal to get max samples*/
   spl_topo_fwk_extn_info_t      fwk_extn_info;

   spl_topo_simp_topo_L1_flags_t      simpt1_flags;
   spl_topo_simp_topo_L2_flags_t      simpt2_flags;
   spl_topo_simp_topo_event_flags_t   simpt_event_flags;

   spl_topo_flags_t  flags;
   gu_module_list_t  *simpt_sorted_module_list_ptr; /**< sorted module list for simplified topo.
                                                         excluded internal bypass modules. */

} spl_topo_t;

// Information about data duration modifying modules (DMM)


// Threshold information pertaining to a module.
typedef struct spl_topo_module_threshold_data_t
{
   uint32_t thresh_in_samples_per_channel; /* module threshold in samples per channel */
   uint32_t thresh_port_sample_rate;       /* sample rate of the threshold port */
   bool_t   is_threshold_module;           /* Indicates if the module has threshold or not */
} spl_topo_module_threshold_data_t;

/**
 * A bit field for module flags
 */
typedef struct spl_topo_module_flags_t
{
   /** Flags which indicate whether framework extension is required for the module */
   uint32_t need_voice_delivery_extn : 1;    /**< FWK_EXTN_VOICE_DELIVERY */
   uint32_t need_ecns_extn       : 1;    /**< FWK_EXTN_ECNS */
   uint32_t module_type : 2; /**< spl_topo_module_type_t */
   uint32_t is_skip_process :1; /**< Set to true if module's process should be skipped.*/
   uint32_t is_any_inp_port_at_gap :1; /**< Set to true if any input port is at gap.*/

   uint32_t is_mimo_module_disabled :1 ; /**<Flag to indicate if mimo module has suggested itself to be disabled.*/
} spl_topo_module_flags_t;

typedef struct spl_topo_module_t
{
   gen_topo_module_t t_base;

   // TRUE if most recent setting of input media format failed. When TRUE,
   // the module is considered disabled. FALSE by default.
   bool_t in_media_fmt_failed;

   // Bitfield for module flags related to extns
   spl_topo_module_flags_t flags;

   spl_topo_module_threshold_data_t threshold_data;
} spl_topo_module_t;

/**
 * A bitfield for input port flags used in the process context which should be remain throughout all
 * of the module's process iterations.
 */
typedef struct spl_topo_ip_fwd_kick_flags_t
{
   // Only allowed to append zeros to a module's input buffers once per topo invocation.
   uint32_t flushed_zeros : 1;
} spl_topo_ip_fwd_kick_flags_t;

typedef struct spl_topo_ip_flags_t
{
   // Gets set every process call - whether setting up of this input port happened or not.
   uint32_t short_circuited : 1;

   //gets set when a port moves from active list (GU) to inactive list in spl_module_t
   uint32_t port_inactive :1;
} spl_topo_ip_flags_t;

typedef struct spl_topo_req_samples_t
{
   bool_t is_updated;
   union
   {
      uint32_t samples_in;  // required input samples per channel, applicable to input port
      uint32_t expected_samples_out; // expected output samples per channel, applicable to output port
   };
} spl_topo_req_samples_t;


typedef struct spl_topo_input_port_t
{
   gen_topo_input_port_t          t_base;
   spl_topo_ext_buf_t *           ext_in_buf_ptr;

   // Metadata list for input ports is stored in sdata.
   // To check if there are any pending flushing eos's in this input port, check sdata.flags.marker_eos.

   spl_topo_ip_flags_t            flags;

   // handle to the connected internal output port, excluding bypass modules.
   spl_topo_output_port_t *simp_conn_out_port_t;

   // Fields for calculating the input required samples
   spl_topo_req_samples_t req_samples_info;
   spl_topo_req_samples_t req_samples_info_max;

   spl_topo_ip_fwd_kick_flags_t fwd_kick_flags;
} spl_topo_input_port_t;

typedef struct spl_topo_op_flags_t
{
   uint32_t ts_disc : 1;

   // Gets set every process call - whether setting up of this output port happened or not.
   uint32_t short_circuited : 1;

   //gets set when a module raises an event to move to inactive list from active list (GU) to inactive list in spl_module_t
   uint32_t pending_inactive :1;

   //gets set when a module raises an event to move to active list from inactive list (GU) to inactive list in spl_module_t
   uint32_t pending_active :1;

   //gets set when a port moves from active list (GU) to inactive list in spl_module_t
   uint32_t port_inactive :1;

} spl_topo_op_flags_t;

struct spl_topo_output_port_t
{
   gen_topo_output_port_t         t_base;
   uint32_t                       req_buf_size;

   // Metadata list for this output port. sdata is used only for metadata generated by current process
   // call and is later moved to this port. This lets us differentiate between new output md and already
   // existing output md. Only new output md needs offsets adjusted, in the case when there is already
   // partial output data in the buffer before calling prorcess.
   module_cmn_md_list_t          *md_list_ptr;

   spl_topo_op_flags_t            flags;

   // handle to the connected internal input port, excluding bypass modules.
   spl_topo_input_port_t *simp_conn_in_port_t;

   // list of elementary module hosted by this output port
   // and by modules which are disabled between this output port and st_conn_in_port_t
   gu_module_list_t *simp_attached_module_list_ptr;

   // Fields for calculating expected samples at output
   spl_topo_req_samples_t           req_samples_info;
   spl_topo_req_samples_t           req_samples_info_max;

   // If and where there is a timestamp discontinuity within the output buffer.
   // We check for a discontinuity between the extrapolated timestamp
   // from the beginning of the buffer and the timestamp in the sdata after calling spl_topo_process(). If there
   // is a discontinuity:
   // - We only pass on data before the discontinuity to the next module
   // - end_of_frame is set to TRUE
   // - We refrain from calling process on the previous module until all data from before the timestamp discontinuity
   //   is emptied from the buffer. At this point ts_disc becomes FALSE.
   // bool_t   ts_disc; <- This is part of spl_topo_op_flags_t
   uint32_t ts_disc_pos_bytes;
   int64_t  disc_timestamp;
};

typedef struct spl_topo_port_scratch_data_t
{
   gen_topo_port_scratch_data_t  base;
   /**
    * Below fields are only needed in spl_topo.
    * To save memory in gen_topo, it's possible to separate these into a spl_topo_only structure. To do so, gen_topo_process_context_t needs
    * to use void *'s which have a different type for gen_topo/adv_topo. gen_topo_create_modules() would have to take sizes in to allocate
    * the correct sizes. Currently postponing this optimization since it becomes a hassle to reference scratch_data fields (needs a type cast).
    */

   int64_t                timestamp;                          /**< spl_topo uses this for storing the initial output port's timestamp before calling topo process.
                                                                   This is compared against output timestamp for checking timestamp discontinuity.
                                                                   gen_topo_port_scratch_flags_t::is_timestamp_valid*/
} spl_topo_port_scratch_data_t;

/* =======================================================================
Public Function Declarations
========================================================================== */

/**------------------------------- adv_topo.cpp --------------------------------*/
ar_result_t spl_topo_init_topo(spl_topo_t *topo_ptr, gen_topo_init_data_t *init_data_ptr, POSAL_HEAP_ID heap_id);
ar_result_t spl_topo_deinit_ext_in_port(spl_topo_t *spl_topo_ptr, gu_ext_in_port_t *ext_in_port_ptr);
ar_result_t spl_topo_deinit_ext_out_port(spl_topo_t *spl_topo_ptr, gu_ext_out_port_t *ext_out_port_ptr);

ar_result_t spl_topo_destroy_topo(spl_topo_t *spl_topo_ptr);
ar_result_t spl_topo_destroy_modules(spl_topo_t *topo_ptr, bool_t b_destroy_all_modules);
ar_result_t spl_topo_set_param(spl_topo_t *topo_ptr, apm_module_param_data_t *param_ptr);

void spl_topo_set_param_begin(spl_topo_t *topo_ptr);
void spl_topo_set_param_end(spl_topo_t *topo_ptr);

/**------------------------------ spl_topo_buf_utils ----------------------------*/
uint32_t spl_topo_calc_buf_size(spl_topo_t *            topo_ptr,
                                spl_topo_frame_length_t frame_len,
                                topo_media_fmt_t    *media_format_ptr,
                                bool_t               for_delivery);

static inline uint32_t spl_topo_get_max_buf_len(spl_topo_t *topo_ptr, gen_topo_common_port_t *cmn_port_ptr)
{
   return cmn_port_ptr->max_buf_len;
}

ar_result_t spl_topo_update_max_buf_len_for_all_modules(spl_topo_t *topo_ptr);
ar_result_t spl_topo_update_max_buf_len_for_single_module(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr);
ar_result_t spl_topo_reset_output_port(spl_topo_t *topo_ptr, spl_topo_output_port_t *topo_out_port_ptr);

/**------------------------- spl_topo_capi_cb_handler ------------------------*/
capi_err_t spl_topo_capi_callback(void *context_ptr, capi_event_id_t id, capi_event_info_t *event_info_ptr);

/**--------------------------- spl_topo_data_process ----------------------------*/
ar_result_t spl_topo_get_required_input_samples(spl_topo_t *topo_ptr, bool_t is_max);
ar_result_t spl_topo_process(spl_topo_t *topo_ptr, uint8_t path_index);
ar_result_t simp_topo_process(spl_topo_t *topo_ptr, uint8_t path_index);
ar_result_t simp_topo_bypass(spl_topo_t *topo_ptr);

/**------------------------ spl_topo_media_format_utils -------------------------*/
ar_result_t spl_topo_propagate_media_fmt(void *cxt_ptr, bool_t is_data_path);

/**-------------------------------- spl_topo_metadata ----------------------------=*/
void spl_topo_set_eos_zeros_to_flush_bytes(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr);
void spl_topo_assign_marker_eos_on_port(spl_topo_t *topo_ptr, void *port_ptr, bool_t is_input, bool_t new_marker_eos);
ar_result_t spl_topo_ip_modify_md_when_new_data_arrives(spl_topo_t *           topo_ptr,
                                                        spl_topo_module_t *    module_ptr,
                                                        spl_topo_input_port_t *in_port_ptr,
                                                        uint32_t            new_data_amount,
                                                        bool_t              new_flushing_eos_arrived);

ar_result_t spl_topo_op_modify_md_when_new_data_arrives(spl_topo_t *            topo_ptr,
                                                        spl_topo_output_port_t *out_port_ptr,
                                                        uint32_t             new_data_amount,
                                                        bool_t               new_flushing_eos_arrived);

/**-------------------------------- spl_topo_pm ---------------------------------*/
ar_result_t spl_topo_aggregate_kpps_bandwidth(spl_topo_t * topo_ptr,
                                              bool_t    force_aggregate,
                                              uint32_t *aggregate_kpps_ptr,
                                              uint32_t *aggregate_bw_ptr,
                                              uint32_t *scaled_kpps_agg_q4_ptr,
                                              uint32_t *scaled_bw_agg_ptr);

/**-------------------------------- spl_topo_utils ------------------------------*/
void spl_topo_update_module_info(gu_module_t *module_ptr);
void spl_topo_update_all_modules_info(spl_topo_t *topo_ptr);
ar_result_t spl_topo_find_ext_inp_port(spl_topo_t *          topo_ptr,
                                       spl_topo_module_t *   start_module_ptr,
                                       gu_ext_in_port_t **ext_inp_port);
ar_result_t spl_topo_transfer_data_to_ts_temp_buf(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);
ar_result_t spl_topo_transfer_data_from_ts_temp_buf(spl_topo_t *            topo_ptr,
                                                    spl_topo_output_port_t *out_port_ptr,
                                                    spl_topo_ext_buf_t *    ext_buf_ptr);
uint32_t spl_topo_get_in_port_actual_data_len(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr);
uint32_t spl_topo_get_in_port_max_data_len(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr);

uint32_t spl_topo_get_out_port_actual_data_len(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);
uint32_t spl_topo_get_out_port_data_len(void *topo_ctx_ptr, void *out_port_ctx_ptr, bool_t is_max);
uint32_t spl_topo_get_in_port_required_data(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr);
uint32_t spl_topo_get_out_port_empty_space(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);
bool_t spl_topo_ip_contains_metadata(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr);

bool_t spl_topo_op_port_contains_unconsumed_data(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);

ar_result_t spl_topo_aggregate_hw_acc_proc_delay(spl_topo_t *topo_ptr, bool_t only_aggregate, uint32_t *aggregate_hw_acc_proc_delay_ptr);

ar_result_t spl_topo_update_simp_module_connections(spl_topo_t *topo_ptr);

ar_result_t spl_topo_operate_on_modules(void *                    topo_ptr,
                                     uint32_t                  sg_ops,
                                     gu_module_list_t *        module_list_ptr,
                                     spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

ar_result_t spl_topo_operate_on_int_in_port(void *                     topo_ptr,
                                            gu_input_port_t *          in_port_ptr,
                                            spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                            uint32_t                   sg_ops,
                                            bool_t                     set_port_op);

ar_result_t spl_topo_operate_on_int_out_port(void *                     topo_ptr,
                                             gu_output_port_t *         out_port_ptr,
                                             spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                             uint32_t                   sg_ops,
                                             bool_t                     set_port_op);

/**-------------------------- static inline functions ------------------------*/

/*
 * Returns scaled sample from one rate to another.
 */
static inline uint32_t spl_topo_get_scaled_samples(spl_topo_t *topo_ptr,
                           uint32_t    source_samples,
                           uint32_t    source_sample_rate,
                           uint32_t    dest_sample_rate)
{
  uint32_t dest_samples = 0;
  if (source_sample_rate == dest_sample_rate)
  {
    return source_samples;
  }
  dest_samples = TOPO_CEIL(((uint64_t)source_samples * dest_sample_rate), source_sample_rate);

  return dest_samples;
}

/**
 * Checks if this input port has EOF set or not.
 */
static inline bool_t spl_topo_input_port_has_pending_eof(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   return in_port_ptr->t_base.common.sdata.flags.end_of_frame;
}

static inline spl_topo_module_type_t spl_topo_get_module_port_type(spl_topo_module_t *module_ptr)
{
    return (spl_topo_module_type_t)module_ptr->flags.module_type;
}

/**
 * Checks if a module's port is at nblc end.
 */
static inline bool_t spl_topo_is_port_at_nblc_end(gu_module_t *gu_module_ptr,
                                                  gen_topo_common_port_t *cmn_port_ptr)
{
    spl_topo_module_t* module_ptr = (spl_topo_module_t*)gu_module_ptr;
    bool_t is_mimo_sink_source =
            ((module_ptr->t_base.gu.num_input_ports != 1) ||
                    (module_ptr->t_base.gu.num_output_ports != 1)) ? TRUE : FALSE;

    return (
            //mimo, sink and source module breaks the linearity.
            is_mimo_sink_source ||

            //module requires buffering
            module_ptr->t_base.flags.requires_data_buf ||
            module_ptr->t_base.flags.need_mp_buf_extn ||
            module_ptr->threshold_data.is_threshold_module ||

            //trigger policy module breaks the nblc because they may not need data on ports.
            module_ptr->t_base.flags.need_trigger_policy_extn);
}

/**
 * Checks if the eos is pending by checking if it is valid.
 */
static inline bool_t spl_topo_port_has_flushing_eos(spl_topo_t *topo_ptr, gen_topo_common_port_t *port_cmn_ptr)
{
   return port_cmn_ptr->sdata.flags.marker_eos;
}

/**
 * Check if a input port has DFG or Flusing EOS metadata.
 */
static inline bool_t spl_topo_input_port_has_dfg_or_flushing_eos(gen_topo_input_port_t *in_port_ptr)
{
   return (in_port_ptr->common.sdata.flags.marker_eos ||
           (in_port_ptr->common.sdata.metadata_list_ptr &&
                   gen_topo_md_list_has_flushing_eos_or_dfg(in_port_ptr->common.sdata.metadata_list_ptr)));
}

/**
 * True if an output port is not connected (has neither external output port structure or connected input port structre).
 */
static inline bool_t spl_topo_out_port_not_connected(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   return (out_port_ptr && !(out_port_ptr->t_base.gu.ext_out_port_ptr) && (!out_port_ptr->t_base.gu.conn_in_port_ptr));
}

static inline ar_result_t spl_topo_reset_input_port(spl_topo_t *topo_ptr, spl_topo_input_port_t *topo_in_port_ptr)
{
   return topo_shared_reset_input_port(&topo_ptr->t_base, &topo_in_port_ptr->t_base, FALSE);
}

static inline uint32_t spl_topo_simp_topo_get_all_L2_flag_set()
{
   spl_topo_simp_topo_L2_flags_t flag = {.word = 0};
   flag.all_ports_data_flowing  = 1;
   flag.all_ports_valid_mf      = 1;
   return flag.word;
}

static inline uint32_t spl_topo_simp_topo_get_all_event_flag_set()
{
   spl_topo_simp_topo_event_flags_t flag = { .word = 0 };
   flag.check_data_flow_state            = 1;
   flag.check_eof                        = 1;
   flag.check_pending_mf                 = 1;
   flag.check_valid_mf                   = 1;
   return flag.word;
}

// This function gets called after visiting each module in topo_process to determine if we need to switch out of the
// simplified processing logic.
// 1. If any simp_topo_L1_flags got set, we no longer can do simple topo processing
// 2. If any event flags got raised, it implies that one of the conditions for simple topo processing is not met.
//    - If event flags are set due to clearing of condition, then we wouldn't be in simple topo process anyways since
//    previously condition wasn't met.
//    - If event flgas are set due to adding a condition, then we need to move out of simple topo processing.
// 3. If any simp_topo_L2_flags got set, we no longer can do simple topo processing
static inline bool_t spl_topo_can_use_simp_topo(spl_topo_t *topo_ptr)
{
   return (0 == topo_ptr->simpt1_flags.word) && (0 == topo_ptr->t_base.num_data_tpm) &&
          (0 == topo_ptr->simpt_event_flags.word) && (spl_topo_simp_topo_get_all_L2_flag_set() == topo_ptr->simpt2_flags.word);
}

// We need to check simp_topo_L2_flags only if (there are any sipm topo events) and (easier checks are already satisfied)
// If level 1 flags say we can't use simp topo, then there's no reason to check other conditions. Only update these conditions once
// level-1 flags are satisfied.
static inline bool_t spl_topo_needs_update_simp_topo_L2_flags(spl_topo_t *topo_ptr)
{
  return ((0 != topo_ptr->simpt_event_flags.word) &&
          ((0 == topo_ptr->simpt1_flags.word) && (0 == topo_ptr->t_base.num_data_tpm) && (spl_topo_simp_topo_get_all_L2_flag_set() == topo_ptr->simpt2_flags.word)));
}

// to set all the event flag.
// to make sure that L2 flags are updated based on the current state.
static inline void spl_topo_simp_topo_set_all_event_flag(spl_topo_t *topo_ptr)
{
  topo_ptr->simpt2_flags.word = spl_topo_simp_topo_get_all_L2_flag_set();
  topo_ptr->simpt_event_flags.word = spl_topo_simp_topo_get_all_event_flag_set();
}

static inline void spl_topo_update_check_valid_mf_event_flag(spl_topo_t *   topo_ptr,
                                                              gu_cmn_port_t *port_ptr,
                                                              bool_t         is_valid_mf)
{
   if (!is_valid_mf)
   {
      // if media format is not valid then clear thsi flag.
      topo_ptr->simpt2_flags.all_ports_valid_mf = FALSE;

      // at least one port is invalid no need to check other ports.
      topo_ptr->simpt_event_flags.check_valid_mf = FALSE;
   }
   else if (!topo_ptr->simpt2_flags.all_ports_valid_mf)
   {
      // if all ports didn't have valid mf and this port got valid mf then
      // now we have to check all the ports to update the flag.
      // set the event flag here and all the ports will be checked in the event-handler.
      topo_ptr->simpt_event_flags.check_valid_mf = TRUE;

      //setting this flag so that event handler can be invoked.
      topo_ptr->simpt2_flags.all_ports_valid_mf = TRUE;
   }

#ifdef SPL_SIPT_DBG
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "port id = %ld, miid = 0x%lx is_mf_valid %ld, all_ports_valid_mf %ld, check_valid_mf %d.",
            port_ptr->id,
            port_ptr->module_ptr->module_instance_id,
            is_valid_mf,
            topo_ptr->simpt2_flags.all_ports_valid_mf,
        topo_ptr->simpt_event_flags.check_valid_mf);
#endif
}

static inline void spl_topo_update_check_data_flow_event_flag(spl_topo_t *   topo_ptr,
                                                              gu_cmn_port_t *port_ptr,
                                                              topo_data_flow_state_t dfs)
{
  if (TOPO_DATA_FLOW_STATE_AT_GAP == dfs)
  {
     // all ports are not in data flow state so clear this flag.
     topo_ptr->simpt2_flags.all_ports_data_flowing = FALSE;

     // at least one port is at gap so no need to check other ports
     topo_ptr->simpt_event_flags.check_data_flow_state = FALSE;
  }
  else if (!topo_ptr->simpt2_flags.all_ports_data_flowing)
  {
     // if all ports were not in data flow state and this port goes in the data-flow state
     // now we have to check all the ports to update the flag.
     // set the event flag here and all the ports will be checked in the event-handler.
     topo_ptr->simpt_event_flags.check_data_flow_state = TRUE;

     //setting this flag so that event handler can be invoked.
     topo_ptr->simpt2_flags.all_ports_data_flowing = TRUE;
  }

#ifdef SPL_SIPT_DBG
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "port id = %ld, miid = 0x%lx dfs %ld, all_ports_data_flowing %ld, check_data_flow_state %d.",
            port_ptr->id,
            port_ptr->module_ptr->module_instance_id,
        dfs,
            topo_ptr->simpt2_flags.all_ports_data_flowing,
        topo_ptr->simpt_event_flags.check_data_flow_state);
#endif
}

// Always wrap this function in an if of spl_topo_needs_update_sipt_2_flags().
void spl_topo_update_simp_topo_L2_flags(spl_topo_t *topo_ptr);


#ifdef __cplusplus
}
#endif //__cplusplus

// clang-format on

#endif // #ifndef ADVANCED_TOPO_H_
