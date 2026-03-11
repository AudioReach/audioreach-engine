#ifndef __CAPI_SPR_H_I__
#define __CAPI_SPR_H_I__

/**
 *   \file capi_spr_i.h
 *   \brief
 *        This file contains CAPI API's published by Splitter-Renderer module
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi.h"
#include "spr_lib.h"
#include "spr_api.h"
#include "capi_spr_avsync.h"
#include "capi_spr.h"
#include "capi_cmn.h"
#include "ar_defs.h"
#include "shared_lib_api.h"
#include "capi_cmn_imcl_utils.h"
#include "capi_intf_extn_data_port_operation.h"
#include "capi_fwk_extns_trigger_policy.h"
#include "capi_fwk_extns_signal_triggered_module.h"
#include "capi_fwk_extns_thresh_cfg.h"
#include "capi_intf_extn_path_delay.h"
#include "capi_intf_extn_data_port_operation.h"
#include "capi_fwk_extns_multi_port_buffering.h"
#include "imcl_timer_drift_info_api.h"
#include "posal_timer.h"
#include "other_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*==============================================================================
   Local Defines
==============================================================================*/

// Debug flags
//#define DEBUG_SPR_MODULE
//#define SPR_INT_BUF_DEBUG
//#define AVSYNC_DEBUG

/* Stack size requirement from the module */
#define CAPI_SPR_MODULE_STACK_SIZE 2048

/* Chunk size used for fragmented circular buffer */
#define DEFAULT_CIRC_BUF_CHUNK_SIZE (2 * 1024)

/*Minimum chunk size */
#define MIN_CIRC_BUF_CHUNK_SIZE 0x800

/*Maximum chunk size */
#define MAX_CIRC_BUF_CHUNK_SIZE 0x4000

#define UMAX_32 0xFFFFFFFFL

// Check if the port index is valid.
#define IS_INVALID_PORT_INDEX(port_id) (port_id == UMAX_32)

#define MAX_CHANNELS_PER_STREAM CAPI_MAX_CHANNELS_V2

/* Max number of intents per control port in SPR */
#define SPR_MAX_INTENTS_PER_CTRL_PORT 1

// TODO: Discuss these values with the team
#define SPR_MAX_NUM_RECURRING_BUFS 8

#define SPR_MAX_RECURRING_BUF_SIZE 64

#undef ALIGN_8_BYTES
#define ALIGN_8_BYTES(a) ((a + 7) & (~7))

/* Number of milliseconds in a second*/
#define NUM_MS_PER_SEC 1000

/* Number of microseconds in a millisecond*/
#define NUM_US_PER_MS 1000

/* Number of microseconds in a second*/
#define NUM_US_PER_SEC 1000000

/* SPR timer duration */
#define SPR_TIMER_DURATION_5_MS  5

static const uint32_t TSM_UNITY_SPEED_FACTOR = 1<<24; //Q24 format

/**
 * maximum number of corrections per a ms of Q timer interrupt, in us.
 * This constant is decided based on the constraint that if drift is such that it
 * decreases the time available for q timer interrupt processing (1ms), then the decrease
 * shouldn't be drastic.
 */
#define SPR_MAX_US_CORR_PER_MS_IN_QT_INT 50

#define SPR_MSG_PREFIX "capi_spr: %08lX: "
#define SPR_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, SPR_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)
#define SPR_MSG_ISLAND(ID, xx_ss_mask, xx_fmt, ...) AR_MSG_ISLAND(xx_ss_mask, SPR_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)


// forward declaration
typedef struct avsync_t avsync_t;

// Structure to cache the media format & associated data streams
typedef struct spr_mf_handler_t
{
   capi_media_fmt_v2_t media_fmt;
   spr_int_buffer_t    int_buf;
   bool_t              is_applied;

} spr_mf_handler_t;

// Run Time Media Format Handler List structure. Mirror spf_list_t to typecast
typedef struct spr_mf_handler_list_t
{
   spr_mf_handler_t *            mf_handler_ptr;
   struct spr_mf_handler_list_t *next_ptr;
   struct spr_mf_handler_list_t *prev_ptr;

} spr_mf_handler_list_t;

typedef struct spr_ctrl_port_t
{
   uint32_t          is_mapped_to_output_port;
   uint32_t          port_id;
   uint32_t          peer_module_instance_id;
   uint32_t          peer_port_id;
   imcl_port_state_t state;
   uint32_t          num_intents;
   uint32_t          intent_list_arr[SPR_MAX_INTENTS_PER_CTRL_PORT];
   imcl_tdi_hdl_t *  timer_drift_info_hdl_ptr;
   // Incoming drift handle

   imcl_tdi_acc_drift_t acc_drift;
   /* To store the acc_drift for each port */

} spr_ctrl_port_t;

// IMCL port info list structure. Mirror spf_list_t to typecast
typedef struct spr_ctrl_port_list_t
{
   spr_ctrl_port_t *            port_info_ptr;
   struct spr_ctrl_port_list_t *next_ptr;
   struct spr_ctrl_port_list_t *prev_ptr;

} spr_ctrl_port_list_t;

// Input Port information structure
typedef struct spr_input_port_info_t
{
   uint32_t port_id;
   // ID of the port

   uint32_t port_index;
   // Port index

   intf_extn_data_port_state_t port_state;
   // Is port opened or closed ?

   spr_stream_writer_t *strm_writer_ptr;
   // circular buffer to hold pcm data

   spr_mf_handler_list_t *mf_handler_list_ptr;
   /* Cached run time media format changes to be applied by the SPR */

   uint32_t ctrl_port_id;
   // control port ID to be used to update the s2d drift

   uint32_t is_port_at_gap;
   // indicates if the SPR input is at gap (due to EOS/DFG)

} spr_input_port_info_t;

// Path delay structure
typedef struct spr_path_delay_t
{
   uint32_t            path_id;            /**< ID assigned for the path by APM*/
   uint32_t            dst_module_iid;     /**< destination module instance ID of this path.*/
   uint32_t            dst_module_port_id; /**< destination module's port ID of this path.*/
   uint32_t            num_delay_ptrs;     /**< number of delay pointers */
   volatile uint32_t **delay_us_pptr;      /**< Array of ptrs to the delay variable created by APM */
} spr_path_delay_t;

// Output Port information structure
typedef struct spr_output_port_info_t
{
   uint32_t port_id;
   // ID the port

   uint32_t port_index;
   // Port index

   intf_extn_data_port_state_t port_state;
   // Is port opened or closed ?

   spr_stream_reader_t *strm_reader_ptr;
   // circular buffer to hold pcm data.

   spr_path_delay_t path_delay;

   uint32_t ctrl_port_id;
   // Control port ID which is binded to the given output port.

   int64_t prev_output_ts;
   // Last buffer timestamp value sent

   bool_t is_prev_ts_valid;
   // Last buffer timestamp validity flag

   bool_t is_down_stream_rt;
   // Down stream is RT or NRT

} spr_output_port_info_t;

typedef struct spr_data_trigger_t
{
   /** trigger policy:
    * DROP render decision is the need for trigger policy (possible only with valid TS)
    * When dropping we need to drop FTRT, so we need to listen to input buf (not only timer)
    * If we drop with timer trigger, we will never catch up. When dropping output must be nontriggered optional.
    * Trigger policy extn is not applicable for timer trigger.
    * When dropping, output trigger policy will be blocked, input will be MANDATORY.
    * Otherwise, both are nontrigger (input-blocked, output-optional), indicating absence of data trigger.
    *
    * if input port is stopped when dropping data, then is_dropping_data is cleared and trigger policy changes to
    * BLOCKED. Output port state is never changed. always blocked
    * */

   fwk_extn_param_id_trigger_policy_cb_fn_t policy_chg_cb;
   fwk_extn_port_nontrigger_group_t *       non_trigger_group_ptr;
   fwk_extn_port_trigger_group_t *          trigger_groups_ptr;

   bool_t is_dropping_data;
   bool_t tp_enabled;
   // only if a DROP happens at least once, trigger policy is enabled.
   // This helps with MIPS optimization. Trigger policy takes extra looping in container. Most use cases have TS disabled and DROP doesn't occur.
} spr_data_trigger_t;

/** Drift struct used by spr module to keep track of its current acc_drift
 *  and allow other modules to access this info through control links*/
typedef struct spr_drift_info_t
{
   imcl_tdi_hdl_t drift_info_hdl;
   /**< Shared drift info  handle */

   imcl_tdi_acc_drift_t spr_acc_drift;
   /**< Current accumulated drift info  */

   posal_mutex_t drift_info_mutex;
   /**< Mutex for the shared spr_acc_drift info with
    rate matching modules  */
} spr_drift_info_t;

// Structure used by SPR to handle registered events
typedef struct spr_reg_event_info_t
{
   uint32_t event_id;
   // Event ID registered for

   uint64_t dest_address;
   // Address to which the event must be raised

   uint32_t token;
   // Token to be used when raising the event
} spr_reg_event_info_t;

// Enum to denote the reason for underrun at SPR. The first 3 values should align with
// the definition of EVENT_ID_SPR_UNDERRUN
typedef enum underrun_status_t {
   DATA_NOT_AVAILABLE = 1,
   DATA_HELD          = 2,
   DATA_DROPPED       = 3,
   INPUT_AT_GAP       = 4
} underrun_status_t;

typedef struct spr_flags_t
{
   uint32_t is_input_media_fmt_set:1;
   // flag for media format set
   uint32_t timer_created_started:1;
   // indicates if module has created timer instance
   uint32_t stm_ctrl_enable:1;
   // container has enabled the timer (port started), Self sub graph started
   uint32_t signal_trigger_set_flag:1;
   // Flag which is set after the signal ptr is sent through fwk extn property
   uint32_t uses_simple_process:1;
   // Indicates if SPR can be processed optimally
   uint32_t is_inplace:1;
   // Indicates if the module is inplace
   uint32_t has_rcvd_first_buf:1;
   // Indicates if the module has received first sample
   uint32_t has_rendered_first_buf:1;
   // Indicates if the module has rendered first sample
   uint32_t is_timer_disabled:1;
   // Indicates whether SPR disabled the timer or not
   uint32_t has_flushing_eos:1;
   // Indicates whether EOS is flushing or Non-flushing. For SPR timer enable mode EOS is Non-flushing, timer disable mode Eos is Flushing
   uint32_t is_cntr_duty_cycling:1;
   // Info need to store weather container is duty cycling enabled or not from set param
   uint32_t insert_int_eos_for_dfg:1;
   // Insert internal flushing EOS for DFG when timer is disabled
}spr_flags_t;

/* CAPI structure  */
typedef struct capi_spr_t
{
   const capi_vtbl_t *vtbl;
   /* pointer to virtual table */

   uint32_t miid;
   /* module instance ID*/

   POSAL_HEAP_ID heap_id;
   /* Heap id received from framework*/

   capi_event_callback_info_t event_cb_info;
   /* Event Call back info received from Framework*/

   spr_driver_t *drv_ptr;
   /* Handle to the SPR driver library.*/

   avsync_t *avsync_ptr;
   /* Handle to the AV Sync Library */

   uint32_t num_started_input_ports;
   /* Number of input ports currently active */

   uint32_t max_input_ports;
   /*Maximum number of input ports */

   spr_input_port_info_t *in_port_info_arr;
   /* Pointers to input port info structures.  Size of the array is max_input_ports.*/

   uint32_t num_started_output_ports;
   /* Number of input ports currently active */

   uint32_t max_output_ports;
   /* Maximum number of output ports*/

   uint32_t max_ctrl_ports;
   /* Maximum number of control ports */

   uint32_t primary_output_arr_idx;
   /* array index of the primary output for SPR */

   spr_output_port_info_t *out_port_info_arr;
   /* Pointers to output port info structures.   Size of the array is max_output_ports. */

   spr_ctrl_port_list_t *ctrl_port_list_ptr;
   /* List of the active control port for SPR. Each element is of type imcl_port_info_t */

   spr_flags_t flags;
   /* List of flags maintained by the module */

   capi_media_fmt_v2_t operating_mf;
   // Media format of the module.

   uint32_t frame_dur_us;
   uint32_t frame_dur_bytes_per_ch;
   // saving nominal frame dur

   void *signal_ptr;
   posal_timer_t timer;
   // timer related

   intf_extn_param_id_metadata_handler_t metadata_handler;
   // metadata handler

   spr_data_trigger_t data_trigger;
   // SPR data trigger policy info

   /* Module output drift info*/
   spr_drift_info_t spr_out_drift_info;

   /* Pending drift to be corrected*/
   int64_t spr_pending_drift_us;

   /* Save the initial start time to be used in every process call*/
   int64_t absolute_start_time_us;

   /* save (sampling rate/NUM_MS_PER_SEC)* frame_dur * NUM_MS_PER_SEC to prevent recalc in process*/
   uint64_t integ_sr_us;

   // process counter
   uint64_t counter;

   // event registration information for underrun events
   spr_reg_event_info_t underrun_event_info;

   // Info needed to store the no underrun occurance and time of last printed.
   capi_cmn_underrun_info_t underrun_info;

   // reset session time info
   param_id_spr_session_time_reset_info_t reset_sess_time_info;

   // event registration information for underrun events
   spr_reg_event_info_t session_time_reset_event_info;

} capi_spr_t;

/*==============================================================================
   Function declarations
==============================================================================*/

#define BYTES_PER_SAMPLE(bits_per_sample) CAPI_CMN_BITS_TO_BYTES(bits_per_sample)
#define SPR_MIN(a, b) (((a) < (b))? (a):(b))

static inline uint32_t _spr_us_to_bytes(capi_spr_t *me_ptr, uint64_t time_us)
{
   return (me_ptr->operating_mf.format.sampling_rate / NUM_MS_PER_SEC) * (time_us / NUM_US_PER_MS) *
          (CAPI_CMN_BITS_TO_BYTES(me_ptr->operating_mf.format.bits_per_sample));
}

static inline uint32_t _spr_ms_to_bytes(capi_spr_t *me_ptr, uint64_t time_ms)
{
   return _spr_us_to_bytes(me_ptr, (time_ms * 1000));
}

static inline uint64_t _spr_bytes_to_us(capi_spr_t *me_ptr, uint32_t num_bytes_per_ch)
{
   return capi_cmn_bytes_to_us(num_bytes_per_ch,
                               me_ptr->operating_mf.format.sampling_rate,
                               me_ptr->operating_mf.format.bits_per_sample,
                               1,
                               NULL);
}

static inline bool_t spr_has_cached_mf(capi_spr_t *me_ptr)
{
   return (me_ptr && me_ptr->in_port_info_arr && me_ptr->in_port_info_arr->mf_handler_list_ptr);
}

static inline bool_t spr_int_buf_is_head_node(spf_list_node_t *list_ptr, void *obj_ptr)
{
   if (!list_ptr || !obj_ptr)
   {
      return FALSE;
   }

   return ((list_ptr->obj_ptr == obj_ptr) ? TRUE : FALSE);
}

static inline spr_mf_handler_list_t *spr_get_cached_mf_list_head_node(capi_spr_t *me_ptr)
{
   return ((spr_has_cached_mf(me_ptr)) ? (me_ptr->in_port_info_arr->mf_handler_list_ptr) : NULL);
}

static inline spr_mf_handler_t *spr_get_cached_mf_list_head_obj_ptr(capi_spr_t *me_ptr)
{
   spr_mf_handler_list_t *node_ptr = spr_get_cached_mf_list_head_node(me_ptr);

   return (node_ptr ? (node_ptr->mf_handler_ptr) : NULL);
}

static inline spf_list_node_t *spr_get_cached_mf_list_head_strm_node(capi_spr_t *me_ptr)
{
   spr_mf_handler_t *mf_handler_ptr = spr_get_cached_mf_list_head_obj_ptr(me_ptr);

   return (mf_handler_ptr ? (mf_handler_ptr->int_buf.buf_list_ptr) : NULL);
}

static inline capi_err_t capi_spr_set_is_input_at_gap(capi_spr_t *me_ptr, uint32_t is_port_at_gap)
{
   if (me_ptr->in_port_info_arr)
   {
      me_ptr->in_port_info_arr->is_port_at_gap = is_port_at_gap;
      return CAPI_EOK;
   }

   return CAPI_EBADPARAM;
}

static inline bool_t capi_spr_check_if_input_is_at_gap(capi_spr_t *me_ptr)
{
   if (me_ptr->in_port_info_arr)
   {
      bool_t is_at_gap = (me_ptr->in_port_info_arr->is_port_at_gap) ? TRUE : FALSE;
      return is_at_gap;
   }

   return FALSE;
}

/**------------------------------- capi_spr_imcl --------------------------------------------------------------------*/
capi_err_t capi_spr_imcl_handle_incoming_data(capi_spr_t *me_ptr, capi_buf_t *params_ptr);
capi_err_t capi_spr_imcl_port_operation(capi_spr_t *me_ptr, capi_buf_t *params_ptr);
void capi_spr_send_output_drift_info(capi_spr_t *me_ptr);
capi_err_t capi_spr_update_ctrl_data_port_map(capi_spr_t *me_ptr, capi_buf_t *params_ptr);

/**------------------------------- capi_spr_control_utils -----------------------------------------------------------*/

void capi_spr_imcl_get_drift(capi_spr_t *me_ptr, spr_ctrl_port_t *port_info_ptr);
spr_ctrl_port_t *spr_get_ctrl_port_instance(capi_spr_t *me_ptr, uint32_t ctrl_port_id);
ar_result_t spr_read_acc_out_drift(imcl_tdi_hdl_t *drift_info_hdl_ptr, imcl_tdi_acc_drift_t *acc_drift_out_ptr);

capi_err_t capi_spr_init_out_drift_info(spr_drift_info_t *drift_info_ptr, imcl_tdi_get_acc_drift_fn_t get_drift_fn_ptr);
capi_err_t capi_spr_deinit_out_drift_info(spr_drift_info_t *drift_info_ptr);
capi_err_t capi_spr_calc_set_timer(capi_spr_t *me_ptr);
capi_err_t spr_timer_enable(capi_spr_t *me_ptr);
capi_err_t capi_spr_create_trigger_policy_mem(capi_spr_t *me_ptr);
void capi_spr_raise_event_data_trigger_in_st_cntr(capi_spr_t *me_ptr);
capi_err_t capi_spr_data_port_op_handler(capi_spr_t *me_ptr, capi_buf_t *params_ptr);
capi_err_t capi_spr_check_and_raise_output_media_format_event(capi_spr_t *me_ptr, uint32_t arr_index);
capi_err_t capi_spr_set_up_output(capi_spr_t *me_ptr, uint32_t outport_index, bool_t need_to_reinitialize);
void capi_spr_update_frame_duration_in_bytes(capi_spr_t *me_ptr);
capi_err_t capi_spr_process_register_event_to_dsp_client(capi_spr_t *                            me_ptr,
                                                         capi_register_event_to_dsp_client_v2_t *reg_event_ptr);
void capi_spr_check_raise_underrun_event(capi_spr_t *me_ptr, underrun_status_t *status_ptr, uint32_t output_port_idx);
void capi_spr_check_raise_session_time_reset_event(capi_spr_t *me_ptr);
capi_err_t capi_spr_change_trigger_policy_util_(capi_spr_t *me_ptr, bool_t need_to_drop, bool_t force);
capi_err_t capi_spr_check_timer_disable_update_tp(capi_spr_t *me_ptr);

/**------------------------------- capi_spr_path_delay --------------------------------------------------------------*/
capi_err_t spr_set_response_to_path_delay_event(capi_spr_t *me_ptr, capi_buf_t *params_ptr);
capi_err_t spr_set_destroy_path_delay_cfg(capi_spr_t *me_ptr, capi_buf_t *params_ptr);
capi_err_t spr_reset_path_delay(capi_spr_t *me_ptr, spr_output_port_info_t *out_port_info_ptr);
uint32_t spr_aggregate_path_delay(capi_spr_t *me_ptr, spr_output_port_info_t *out_port_info_ptr);
capi_err_t spr_request_path_delay(capi_spr_t *me_ptr, uint32_t end_module_iid);

/**------------------------------- capi_spr_port_utils --------------------------------------------------------------*/
capi_err_t capi_check_and_init_input_port(capi_spr_t *me_ptr, uint32_t arr_index, bool_t need_to_reinitialize);
capi_err_t capi_spr_check_and_init_output_port(capi_spr_t *me_ptr, uint32_t arr_index, bool_t need_to_reinitialize);
capi_err_t capi_spr_destroy_input_port(capi_spr_t *me_ptr, uint32_t arr_index);
capi_err_t capi_spr_destroy_output_port(capi_spr_t *me_ptr, uint32_t arr_index, bool_t need_to_partial_destroy);
capi_err_t capi_spr_create_port_structures(capi_spr_t *me_ptr);
uint32_t   spr_get_arr_index_from_port_id(capi_spr_t *me_ptr, uint32_t port_id);
uint32_t   spr_get_arr_index_from_port_index(capi_spr_t *me_ptr, uint32_t port_index, bool_t is_input);
capi_err_t capi_spr_set_data_port_property(capi_spr_t *me_ptr, capi_buf_t *params_ptr);
capi_err_t capi_spr_check_reinit_ports(capi_spr_t *         me_ptr,
                                              capi_media_fmt_v2_t *media_fmt_ptr,
                                              bool_t               reinit_spr_ports,
                                              bool_t               check_cache_mf);


/**------------------------------- capi_spr_md ----------------------------------------------------------------------*/
bool_t spr_check_input_for_eos_md(capi_spr_t *me_ptr, capi_stream_data_t *input[]);
capi_err_t spr_handle_metadata_util_(capi_spr_t *        me_ptr,
                                    capi_stream_data_t *input[],
                                    capi_stream_data_t *output[],
                                    bool_t              is_drop_metadata);
bool_t spr_can_reinit_with_new_mf(capi_spr_t *me_ptr);

/**------------------------------- capi_spr_data_utils --------------------------------------------------------------*/
capi_err_t capi_spr_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);
void capi_spr_evaluate_simple_process_criteria(capi_spr_t *me_ptr, bool_t is_input_ts_valid);

/**-------------------------------- capi_spr_mf_utils ---------------------------------------------------------------*/
void capi_spr_destroy_cached_mf_list(capi_spr_t *me_ptr, bool_t apply_tail_mf);
capi_err_t capi_spr_check_apply_mf_change(capi_spr_t *me_ptr, bool_t *did_mf_change);
capi_err_t capi_spr_handle_media_fmt_change(capi_spr_t *         me_ptr,
                                            capi_media_fmt_v2_t *media_fmt_ptr,
                                            bool_t               check_cache_mf);
bool_t spr_cached_mf_is_input_strm_head(capi_spr_t *me_ptr, void *input_strm_ptr);
capi_err_t capi_spr_add_input_to_mf_list(capi_spr_t *me_ptr, capi_stream_data_v2_t *input_ptr);
#ifdef DEBUG_SPR_MODULE
void capi_spr_print_media_fmt(capi_spr_t *me_ptr, capi_media_fmt_v2_t *media_fmt_ptr);
#endif

bool_t spr_has_old_mf_data_pending(capi_spr_t *me_ptr);



/**-------------------------------- capi_int_buf_utils --------------------------------------------------------------*/
void *capi_spr_get_list_head_obj_ptr(void *list_ptr);
capi_err_t capi_spr_destroy_int_buf_list(capi_spr_t *         me_ptr,
                                         spr_int_buffer_t *   int_buf_ptr,
                                         capi_media_fmt_v2_t *media_fmt_ptr);
capi_err_t capi_spr_create_int_buf_node(capi_spr_t *           me_ptr,
                                        spr_int_buffer_t *     int_buf_ptr,
                                        capi_stream_data_v2_t *input_ptr,
                                        POSAL_HEAP_ID          heap_id,
                                        capi_media_fmt_v2_t *  media_fmt_ptr);
capi_err_t capi_spr_destroy_int_buf_node(capi_spr_t *         me_ptr,
                                         spr_int_buffer_t *   int_buf_ptr,
                                         void *               buf_node_ptr,
                                         bool_t               check_if_head_node,
                                         capi_media_fmt_v2_t *media_fmt_ptr,
                                         bool_t               is_data_drop);
capi_err_t capi_spr_move_int_buf_node(spf_list_node_t *    node_ptr,
                                      spr_int_buffer_t *   src_buf_ptr,
                                      spr_int_buffer_t *   dst_buf_ptr,
                                      capi_media_fmt_v2_t *media_fmt_ptr);
capi_err_t spr_does_strm_reader_have_data(capi_spr_t *me_ptr, bool_t *has_data_ptr);

/********************  Static inline function ********************************/

/*------------------------------------------------------------------------------
  Function name: spr_handle_metadata
  Utility function to propagate metadata from the input stream to the output
  stream during module process.
* ------------------------------------------------------------------------------*/
static inline capi_err_t spr_handle_metadata(capi_spr_t *        me_ptr,
                               capi_stream_data_t *input[],
                               capi_stream_data_t *output[],
                               bool_t              is_drop_metadata)
{
   capi_err_t result = CAPI_EOK;

   /*
    * Since SPR has no delay and doesn't prioritize the output ports in any order,
    * we need to set the bit corresponding to uniform order eos
    *
    * a) No algo delay in SPR, hence no need to update sample_offset
    * b) clone if it's not the first output port
    */
   if (CAPI_STREAM_V2 != input[0]->flags.stream_data_version)
   {
      SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "stream version must be 1");
      return CAPI_EFAILED;
   }

   capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input[0];
   if (!in_stream_ptr->metadata_list_ptr)
   {
      return result;
   }

   return spr_handle_metadata_util_(me_ptr, input, output, is_drop_metadata );
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_change_trigger_policy
   Changes the trigger policy of the module when transitioning to/from DROP
   scenarios or during input port start/stop

   see notes in spr_data_trigger_t struct documentation
* ------------------------------------------------------------------------------*/
static inline capi_err_t capi_spr_change_trigger_policy(capi_spr_t *me_ptr, bool_t need_to_drop, bool_t force)
{
   // forcing works only if TP was enabled at least once (at least one drop)
   force = force && me_ptr->data_trigger.tp_enabled;

   need_to_drop = force ? me_ptr->data_trigger.is_dropping_data : need_to_drop;

   // No need to change TP if,
   // 1. Force change is FALSE  &&
   // 2. No change in dropping data flag.
   if (!force && (me_ptr->data_trigger.is_dropping_data == need_to_drop))
   {
      return CAPI_EOK;
   }

   return capi_spr_change_trigger_policy_util_(me_ptr, need_to_drop, force);
}

static inline bool_t capi_spr_check_print_underrun(capi_cmn_underrun_info_t *underrun_info_ptr,
                                                   bool_t         is_steady_state)
{
   uint64_t curr_time = posal_timer_get_time();
   uint64_t diff      = curr_time - underrun_info_ptr->prev_time;
   uint64_t threshold = CAPI_CMN_UNDERRUN_TIME_THRESH_US;
   if (TRUE == is_steady_state)
   {
      threshold = CAPI_CMN_STEADY_STATE_UNDERRUN_TIME_THRESH_US;
   }

   if ((diff >= threshold) || (0 == underrun_info_ptr->prev_time))
   {
      underrun_info_ptr->prev_time = curr_time;
      return TRUE;
   }

   return FALSE;
}

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /*__CAPI_SPR_H_I*/
