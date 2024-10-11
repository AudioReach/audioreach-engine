#ifndef __CAPI_AUDIO_DAM_BUFFER_H_I__
#define __CAPI_AUDIO_DAM_BUFFER_H_I__

/**
 *   \file capi_audio_dam_buffer_i.h
 *   \brief
 *        This file contains CAPI API's published by Bridge Buffering module
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi.h"
#include "audio_dam_buffer_api.h"
#include "audio_dam_driver.h"
#include "capi_audio_dam_buffer.h"
#include "capi_cmn.h"
#include "ar_defs.h"
#include "shared_lib_api.h"
#include "capi_fwk_extns_multi_port_buffering.h"
#include "capi_fwk_extns_trigger_policy.h"
#include "capi_cmn_imcl_utils.h"
#include "imcl_dam_detection_api.h"
#include "capi_intf_extn_data_port_operation.h"
#include "capi_intf_extn_metadata.h"
#include "other_metadata.h"
#include "platform_internal_dcm_if.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*==============================================================================
   Local Defines
==============================================================================*/

// Debug flag
//#define DEBUG_AUDIO_DAM_BUFFER_MODULE

#define DAM_NUM_SUPPORTED_INTENTS_PER_PORT 1

/* Number of CAPI Framework extension needed */
#define CAPI_AUDIO_DAM_NUM_FRAMEWORK_EXTENSIONS 2

#define CAPI_AUDIO_DAM_MAX_INPUT_PORTS 32

#define CAPI_AUDIO_DAM_MAX_OUTPUT_PORTS CAPI_AUDIO_DAM_MAX_INPUT_PORTS

/* Chunk size used for fragmented circular buffer */
#define DEFAULT_CIRC_BUF_CHUNK_SIZE (4 * 1024)

#define UMAX_32 0xFFFFFFFFL

// Check if the port index is valid.
#define IS_INVALID_PORT_INDEX(port_id) (port_id == UMAX_32)

#define MAX_CHANNELS_PER_STREAM (AUDIO_DAM_MAX_CHANNELS_PER_STREAM)

#define AUDIO_DAM_MAX_INTENTS_PER_CTRL_PORT 1

#define DEFAULT_DOWNSTREAM_DELAY_IN_MS 250

#define CAPI_AUDIO_DAM_LOW_KPPS 0 // KPPS vote needed when gate is closed and ftrt data is done being drained

#define CAPI_AUDIO_DAM_HIGH_KPPS 500000 // KPPS vote needed when gate is opened and ftrt data is being drained

#define ALIGN_8_BYTES(a) ((a + 7) & (0xFFFFFFF8))

typedef enum eos_type_t{
   INVALID_EOS_TYPE = 0,
   FLUSHING_EOS,
   NON_FLUSHING_EOS
}eos_type_t;

typedef struct imcl_port_info_t
{
   uint32_t          port_id;
   imcl_port_state_t state;
   uint32_t          num_intents;
   uint32_t          intent_list_arr[AUDIO_DAM_MAX_INTENTS_PER_CTRL_PORT];
   param_id_audio_dam_imcl_virtual_writer_info_t *virt_wr_cfg_ptr;
} imcl_port_info_t;

/*Port information structure */
typedef struct
{
   uint32_t port_id;
   // ID of the port

   uint8_t port_index;
   // Port index

   bool_t is_open;
   // Is port opened or closed ?

   bool_t is_started;

   bool_t is_mf_set;
   // flag for media format set, for raw mf its set only after receiving raw media fmt metadata (G722)

   uint32_t num_channels;
   // Number of channels recieved in input media format.

   audio_dam_stream_writer_t *strm_writer_ptr;
   // circular buffer to hold pcm data

   audio_dam_input_port_cfg_t *cfg_ptr;

} _aud_dam_input_port_info;

/*Port information structure */
typedef struct
{
   uint32_t port_id;
   // ID the port

   uint8_t port_index;
   // Port index

   bool_t is_open;
   // Is port opened or closed ?

   bool_t is_started;

   audio_dam_stream_reader_t *strm_reader_ptr;
   // circular buffer to hold pcm data.

   audio_dam_output_port_cfg_t *cfg_ptr;
   // OUtput port configuration.

   uint32_t downstream_setup_duration_in_ms;
   /** This will be the base buffer size or the minimum size of the circular buffer required */

   uint32_t ctrl_port_id;
   /** Control port ID which is binded to the given output port. */

   uint32_t requested_resize_in_us;
   // Resize request from the output port's client

   uint32_t actual_output_num_chs;
   // If the best channel info is not received its same as the output port audio_dam_output_port_cfg_t.

   uint32_t actual_output_ch_ids[MAX_CHANNELS_PER_STREAM];
   // This is the actual output channel map, If the best channel info is not received its
   // same as the ouptut port audio_dam_output_port_cfg_t. valid values are first actual_output_num_chs elements.

   uint32_t is_peer_heap_id_valid;
   /* indicates if a valid heap ID is sent by a peer to this output, through IMCL */

   POSAL_HEAP_ID peer_heap_id;
   /* Heap id received from IMCL peer*/

   bool_t is_peer_aad;
   /** set to TRUE if IMCL peer is AAD module*/

   bool_t is_gate_opened;
   // Module generates output on this port, only when the detection engine
   // opens the gate through IMC link after detection.

   bool_t is_pending_gate_close;
   // Module gets gate close from Det Engine, and marks this as true.
   // In the next process call, it inserts EOS on this output port, handles gate close
   // and resets this flag.

   bool_t is_drain_history;
   // gate is opened to drain all the history data. gate will auto close when all history data is sent out.

   uint32_t ftrt_unread_data_len_in_us;
   // amount of KW data buffered at the time of gate open

   bool_t trigger_island_entry;
   /* flag to indicate downstream module is done processing and ready to enter island */

   uint32_t bytes_before_eos;
   /*bytes to drain before sending EOS in case of input port stop e.g., device switch in ASR case*/

   bool_t pending_eos;
   /* indicate if the EOS is yet to sent */
} _aud_dam_output_port_info;

typedef struct
{
   data_format_t fmt;

   uint32_t bitstream_format;

   uint32_t bits_per_sample; // valid only for Fixed point

   uint32_t bytes_per_sample; // valid only for Fixed point

   uint32_t q_factor; // valid only for Fixed point

   uint32_t sampling_rate; // valid only for Fixed point

   uint32_t data_is_signed; // valid only for Fixed point

} _aud_dam_media_fmt_t;

/*Trigger policy information structure */
typedef struct
{
   fwk_extn_port_nontrigger_group_t *non_trigger_group_ptr;
   fwk_extn_port_trigger_group_t *   trigger_groups_ptr;
} audio_dam_tp_info_t;

/* CAPI structure  */
typedef struct capi_audio_dam_t
{
   const capi_vtbl_t *vtbl;
   /* pointer to virtual table */

   POSAL_HEAP_ID heap_id;
   /* Heap id received from framework*/

   capi_event_callback_info_t event_cb_info;
   /* Event Call back info received from Framework*/

   uint32_t kpps_vote;
   /* current kpps vote */

   audio_dam_driver_t driver_handle;
   /* Handle to the audio Dam driver library.*/

   uint32_t max_input_ports;
   /*Maximum number of input ports */

   _aud_dam_input_port_info *in_port_info_arr;
   /* Pointers to input port info structures.  Size of the array is max_input_ports.*/

   uint32_t max_output_ports;
   /* Maximum number of output ports*/

   _aud_dam_output_port_info *out_port_info_arr;
   /* Pointers to output port info structures.   Size of the array is max_output_ports. */

   imcl_port_info_t *imcl_port_info_arr;
   /* Pointers to ctrl port info structures. Size of the array is max_output_ports. */

   bool_t is_input_media_fmt_set;
   // flag for media format set

   _aud_dam_media_fmt_t operating_mf;
   // Media format of the module.

   audio_dam_tp_info_t data_tp;   // Data trigger policy state
   audio_dam_tp_info_t signal_tp; // Signal trigger policy state
   /* Note: Key difference between Signal and data TP is the outputs with gate opened are set as,
      Optional non-triggerable for Signal trigger and optional triggerable for data triggers. This
      avoids unintended overruns during Signal triggers if output buffer is not available. */

   fwk_extn_param_id_trigger_policy_cb_fn_t policy_chg_cb;
   // Call back to trigger process scheduler policy change between FTRT and RT modes.

   uint32_t miid;

   intf_extn_param_id_metadata_handler_t metadata_handler;
   // metadata handler

   bool_t is_tp_enabled;                // TP can be disabled dynamicaly when all gates are closed
   bool_t cannot_toggle_to_default_tgp; // set to TRUE if one of the IMCL peer is Acoustic Activity Detection module

   bool_t is_island_duty_cycle_enabled;  // enable island enrty/exit based on this flag config
   dcm_island_control_payload_t payload; // Payload used for sending entry or exit to dcm

} capi_audio_dam_t;

#define INIT_EXCEPTION_HANDLING uint32_t exception_line_number = 0;

/** try to call a function and if it fails go to exception handling */
#define TRY(exception, func)                                                                                           \
   if (AR_EOK != (exception = func))                                                                                   \
   {                                                                                                                   \
      exception_line_number = __LINE__;                                                                                \
      goto exception##bail;                                                                                            \
   }

/** catching exceptions */
#define CATCH(exception, msg, ...)                                                                                     \
   exception##bail : if (exception != AR_EOK)                                                                          \
   {                                                                                                                   \
      AR_MSG(DBG_ERROR_PRIO,                                                                                           \
             msg ":Exception 0x%lx at line number %lu",                                                                \
             ##__VA_ARGS__,                                                                                            \
             exception,                                                                                                \
             exception_line_number);                                                                                   \
   }                                                                                                                   \
   if (exception != AR_EOK)

/*==============================================================================
   Function declarations
==============================================================================*/

uint32_t get_arr_index_from_output_port_id(capi_audio_dam_t *me_ptr, uint32_t output_port_id);

uint32_t get_arr_index_from_input_port_id(capi_audio_dam_t *me_ptr, uint32_t input_port_id);

uint32_t get_arr_index_from_port_index(capi_audio_dam_t *me_ptr, uint32_t port_index, bool_t is_input);

uint32_t capi_dam_get_ctrl_port_arr_idx_from_ctrl_port_id(capi_audio_dam_t *me_ptr, uint32_t ctrl_port_id);

capi_err_t get_output_arr_index_from_ctrl_port_id(capi_audio_dam_t *me_ptr,
                                                  uint32_t          ctrl_port_id,
                                                  uint32_t *        num_out_ports_ptr,
                                                  uint32_t          out_port_arr_idxs[]);

uint32_t get_op_arr_idx_from_ctrl_port_id(capi_audio_dam_t *me_ptr, uint32_t ctrl_port_id);

capi_err_t capi_audio_dam_imcl_data_send(capi_audio_dam_t *me_ptr,
                                         capi_buf_t *      buf_ptr,
                                         uint32_t          port_id,
                                         bool_t            send_to_peer,
                                         bool_t            is_trigger);

capi_err_t capi_audio_dam_imcl_return_to_fwk(capi_audio_dam_t *me_ptr, capi_buf_t *buf_ptr, uint32_t port_id);

capi_err_t capi_audio_dam_imcl_send_to_peer(capi_audio_dam_t *me_ptr, capi_buf_t *buf_ptr, uint32_t port_id);

capi_err_t capi_audio_dam_imcl_send_un_read_len(capi_audio_dam_t *me_ptr, uint32_t unread_len_in_us, uint32_t port_id);

capi_err_t capi_audio_dam_imc_set_param_handler(capi_audio_dam_t *me_ptr,
                                                uint32_t          ctrl_port_id,
                                                capi_buf_t *      intent_buf_ptr);

capi_err_t capi_audio_dam_imcl_send_unread_len(capi_audio_dam_t *me_ptr,
                                               uint32_t          unread_len_in_us,
                                               uint32_t          ctrl_port_id);

capi_err_t capi_audio_dam_imcl_set_hdlr_peer_info(capi_audio_dam_t *me_ptr,
                                                  uint32_t          op_arr_index,
                                                  vw_imcl_header_t *header_ptr);

capi_err_t capi_audio_dam_imcl_set_hdlr_resize(capi_audio_dam_t *me_ptr,
                                               uint32_t          op_arr_index,
                                               vw_imcl_header_t *header_ptr);

capi_err_t capi_audio_dam_imcl_set_hdlr_flow_ctrl_v2(capi_audio_dam_t *me_ptr,
                                                     uint32_t          op_arr_index,
                                                     vw_imcl_header_t *header_ptr,
                                                     uint32_t *        unread_bytes_ptr);

capi_err_t capi_audio_dam_imcl_set_hdlr_ouput_ch_cfg(capi_audio_dam_t *me_ptr,
                                                     uint32_t          op_arr_index,
                                                     vw_imcl_header_t *header_ptr);

capi_err_t capi_audio_dam_imcl_set_virt_writer_cfg(capi_audio_dam_t *me_ptr,
                                                   uint32_t          ctrl_arr_index,
                                                   uint32_t          op_arr_index,
                                                   vw_imcl_header_t *header_ptr);

capi_err_t capi_audio_dam_imcl_handle_gate_open(capi_audio_dam_t *                   me_ptr,
                                                uint32_t                             op_arr_index,
                                                param_id_audio_dam_data_flow_ctrl_t *cfg_ptr);

capi_err_t capi_audio_dam_imcl_handle_gate_close(capi_audio_dam_t *me_ptr, uint32_t op_arr_index);

capi_err_t capi_audio_dam_imcl_trigger_island_entry(capi_audio_dam_t *me_ptr,
                                                   uint32_t          op_arr_index,
                                                   vw_imcl_header_t *header_ptr);

/////////////////////////////////////  GENERIC UTILS ///////////////////////////////////////

capi_err_t capi_check_and_close_the_gate(capi_audio_dam_t *me_ptr, uint32_t op_arr_index, bool_t is_destroy);
capi_err_t capi_audio_dam_change_trigger_policy(capi_audio_dam_t *me_ptr);
capi_err_t capi_audio_dam_change_trigger_policy_proc_context(capi_audio_dam_t *me_ptr);

// raise capi events on receiving the input and output port configurations.
capi_err_t capi_check_and_raise_output_media_format_event(capi_audio_dam_t *me_ptr,
                                                          uint32_t          arr_index,
                                                          uint32_t          num_channels,
                                                          uint32_t *        out_ch_id_arr);

capi_err_t capi_check_and_init_output_port(capi_audio_dam_t *me_ptr, uint32_t arr_index);

// Check if the port config is received and then allocate stream reader for the particular index.
capi_err_t capi_destroy_output_port(capi_audio_dam_t *me_ptr, uint32_t op_arr_index);

// Check if the port config is received and then allocate stream reader for the particular index.
capi_err_t capi_check_and_reinit_output_port(capi_audio_dam_t *           me_ptr,
                                             uint32_t                     op_arr_index,
                                             audio_dam_output_port_cfg_t *cfg_ptr);

capi_err_t capi_create_trigger_policy_mem(capi_audio_dam_t *me_ptr);

capi_err_t capi_audio_dam_resize_buffers(capi_audio_dam_t *me_ptr, uint32_t op_arr_idx);

static inline bool_t is_dam_output_port_initialized(capi_audio_dam_t *me_ptr, uint32_t op_arr_index)
{
   return (NULL != me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr);
}

void capi_audio_dam_buffer_update_kpps_vote(capi_audio_dam_t *me_ptr);

capi_err_t capi_audio_dam_buffer_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

capi_err_t capi_audio_dam_buffer_end(capi_t *_pif);

capi_err_t capi_audio_dam_buffer_set_param(capi_t *                _pif,
                                           uint32_t                param_id,
                                           const capi_port_info_t *port_info_ptr,
                                           capi_buf_t *            params_ptr);

capi_err_t capi_audio_dam_buffer_set_param_non_island(capi_t *                _pif,
                                                      uint32_t                param_id,
                                                      const capi_port_info_t *port_info_ptr,
                                                      capi_buf_t *            params_ptr);

capi_err_t capi_audio_dam_buffer_get_param(capi_t *                _pif,
                                           uint32_t                param_id,
                                           const capi_port_info_t *port_info_ptr,
                                           capi_buf_t *            params_ptr);

capi_err_t capi_audio_dam_buffer_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

capi_err_t capi_audio_dam_buffer_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

capi_err_t capi_dam_insert_flushing_eos_at_out_port(capi_audio_dam_t   *me_ptr,
                                                   capi_stream_data_t *output,
                                                   bool_t              skip_voting_on_eos,
                                                   eos_type_t          eos_type);

capi_vtbl_t *capi_audio_dam_buffer_get_vtable();

static inline POSAL_HEAP_ID capi_audio_dam_get_peer_heap_id(capi_audio_dam_t *me_ptr, uint32_t op_arr_idx)
{
   POSAL_HEAP_ID peer_heap_id = me_ptr->heap_id;
   if (me_ptr->out_port_info_arr[op_arr_idx].is_peer_heap_id_valid)
   {
      peer_heap_id = me_ptr->out_port_info_arr[op_arr_idx].peer_heap_id;
   }
   return peer_heap_id;
}

void capi_audio_dam_reorder_chs_at_gate_open(capi_audio_dam_t *                   me_ptr,
                                             uint32_t                             op_arr_index,
                                             param_id_audio_dam_data_flow_ctrl_t *cfg_ptr);

void capi_audio_dam_reorder_chs_at_gate_close(capi_audio_dam_t *me_ptr, uint32_t op_arr_index, bool_t is_destroy);

capi_err_t capi_audio_dam_handle_pcm_frame_info_metadata(capi_audio_dam_t *             me_ptr,
                                                         capi_stream_data_t *           input,
                                                         uint32_t                       ip_port_index,
                                                         md_encoder_pcm_frame_length_t *info_ptr);

static inline bool_t capi_audio_dam_is_mf_valid_and_fixed_point(capi_audio_dam_t *me_ptr)
{
   return (me_ptr->is_input_media_fmt_set && (CAPI_FIXED_POINT == me_ptr->operating_mf.fmt));
}

capi_err_t capi_dam_duty_cycling_buf_send_message_to_dcm(capi_audio_dam_t *me_ptr, uint32_t spf_msg_cmd_dcm_req_code);

capi_err_t capi_audio_dam_handle_pending_eos(capi_audio_dam_t *me_ptr,
                                             capi_stream_data_t *output[],
                                             uint32_t arr_idx,
                                             uint32_t port_index);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /*__CAPI_AUDIO_DAM_BUFFER_H_I*/