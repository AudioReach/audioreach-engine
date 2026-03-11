/**
 * \file capi_history_buffer_imcl_utils.h
 *
 * \brief
 *
 *     This file contains CAPI APIs for History Buffer Module
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _CAPI_HISTORY_BUFFER_IMCL_UTILS_H_
#define _CAPI_HISTORY_BUFFER_IMCL_UTILS_H_

/**------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/

#include "capi_history_buffer_i.h"
#include "capi_intf_extn_imcl.h"
#ifdef __cplusplus
extern "C" {
#endif
/**------------------------------------------------------------------------
 * Type Definitions
 * -----------------------------------------------------------------------*/

typedef struct capi_history_buffer_t capi_history_buffer_t;

typedef enum imcl_port_state_t
{
   CTRL_PORT_CLOSED = 0,
   /**< Indicates that the control port is closed. And module can de-allocate
        the resources for this port. */

   CTRL_PORT_OPENED = 1,
   /**< Indicates that the control port is open.
        Set to this when capi receives the port open.
        Doesn't necessarily mean that the connected port is opened. */

   CTRL_PORT_PEER_CONNECTED = 2,
   /**< Indicates that the peer is ready to receive the messages.
        Module can send messages only in this state.  */

   CTRL_PORT_PEER_DISCONNECTED = 3,
   /**< Indicates that peer module cannot handle any incoming messages on
        this control link. When the module get this state, it must stop
        sending the control messages further on this ctrl port. */

   PORT_STATE_INVALID = 0xFFFFFFFF
   /**< Port state is not valid. */

}imcl_port_state_t;

/* Control port handle structure */
typedef struct history_buffer_ctrl_port_info_t
{
   uint32_t          port_id;
   imcl_port_state_t state;
   uint32_t          peer_miid;
   uint32_t          peer_port_id;
   uint32_t          num_intents;
   uint32_t          intent_list_arr[HISTORY_BUFFER_MAX_INTENTS_PER_CTRL_PORT];
} history_buffer_ctrl_port_info_t;


/**------------------------------------------------------------------------
 * Function Declarations
 * -----------------------------------------------------------------------*/

/* =========================================================================
 * FUNCTION : history_buffer_get_ctrl_port_index
 *
 * DESCRIPTION:
 * Each module maintains a control port info array.
 *    1. If a new control port ID is passed to this function, it will find an unused
 *       entry in the array and assigns that index to the given control port ID.
 *    2. If there is an existing entry with the given input control port ID. It returns
 *       that index.
 *    3. If a new control port ID is passed and there is no unused entry available,
 *       it return a UMAX_32 as error.
 * ========================================================================= */
ar_result_t history_buffer_get_ctrl_port_index(capi_history_buffer_t *me_ptr,
                                          uint32_t                 ctrl_port_id,
                                          uint32_t *               ctrl_port_idx_ptr);

/* =========================================================================
 * FUNCTION : capi_history_buffer_imcl_register_for_recurring_bufs
 *
 * This function is for registering for recurring buffers to be used for
 * sending control data by a module
 *
 * @param[in] event_cb_info_ptr: Module's CAPI Event CB info ptr
 * @param[in] ctrl_port_id:  Module control port ID
 * @param[in] buf_size:  Buffer size in bytes
 * @param[in] num_bufs:  Number of buffers requested
 *
 * @return    CAPI error code
 * =========================================================================
 */
capi_err_t capi_history_buffer_imcl_register_for_recurring_bufs(capi_event_callback_info_t *event_cb_info_ptr,
                                                           uint32_t                    ctrl_port_id,
                                                           uint32_t                    buf_size,
                                                           uint32_t                    num_bufs);

/* =========================================================================
 * FUNCTION : capi_history_buffer_imcl_get_recurring_buf
 *
 * This function is for requesting a recurring buffer from the framework.
 * This util returns a pointer to the capi_buf_t whose data_ptr will point
 * to the memory to be used by the module.
 *
 * @param[in] event_cb_info_ptr: Module's CAPI Event CB info ptr
 * @param[in] ctrl_port_id:  Module control port ID
 * @param[out] rec_buf_ptr:  Pointer to returned recurring
 *       buffer
 *
 * @return    CAPI error code
 * =========================================================================
 */
capi_err_t capi_history_buffer_imcl_get_recurring_buf(capi_event_callback_info_t *event_cb_info_ptr,
                                                 uint32_t                    ctrl_port_id,
                                                 capi_buf_t *                rec_buf_ptr);

/* =========================================================================
 * FUNCTION : capi_history_buffer_imcl_get_one_time_buf
 *
 * This function is for requesting a one time buffer from the
 *  fwk. This util returns a pointer to the capi_buf_t whose
 *  data_ptr will point to the memory to be used by the module.
 *
 * @param[in] event_cb_info_ptr: Module's CAPI Event CB info ptr
 * @param[in] ctrl_port_id:  Module control port ID
 * @param[in] req_buf_size:  Required buffer size in bytes
 * @param[out] ot_buf_ptr:  Pointer to returned one-time
 *       buffer
 *
 * @return    CAPI error code
 * =========================================================================
 */
capi_err_t capi_history_buffer_imcl_get_one_time_buf(capi_event_callback_info_t *event_cb_info_ptr,
                                                uint32_t                    ctrl_port_id,
                                                uint32_t                    req_buf_size,
                                                capi_buf_t *                ot_buf_ptr);


/* =========================================================================
 * FUNCTION : capi_history_buffer_imcl_send_to_peer
 *
 * This function is for sending the intent buffer to the
 * connected peer module.
 *
 * The flags must be set to send the message in trigger mode or polling mode.
 * refer interface extension for information on trigger and polling modes.
 *
 * @param[in] event_cb_info_ptr: Module's CAPI Event CB info ptr
 * @param[in] ctrl_data_buf_ptr:  Pointer to control data CAPI
 *       buffer
 * @param[in] ctrl_port_id:  Module control port ID
 *
 * @return    CAPI error code
 * =========================================================================
 */
capi_err_t capi_history_buffer_imcl_send_to_peer(capi_event_callback_info_t *event_cb_info_ptr,
                                            capi_buf_t *                ctrl_data_buf_ptr,
                                            uint32_t                    ctrl_port_id,
                                            imcl_outgoing_data_flag_t   flags);

/* =========================================================================
 * FUNCTION : capi_history_buffer_handle_intf_extn_ctrl_port_operation
 *
 * DESCRIPTION:
 *    This function handles control port operation. The following operations
 * are handled,
 *   1. INTF_EXTN_IMCL_PORT_OPEN -
 *         Indicates a new control port is being opened. Control port id,
 *      port index and list of intents being opened for the control port are
 *      passed. Intent ids help the module to understand the purpose. M
 *
 *      Module must cache port_id. AFter open, framework is expected used port id
 *      for sending/receiving intent message buffers.
 *
 *      If module intends to use recurring buffers, module must request the number
 *      of buffers it will be needing runtime. Recurring buffers are allocated at
 *      init time, so there is no delay for allocation. Onetime buffers are allocated
 *      runtime.Refer interface extension header for infomation on recurring
 *      messages vs ontime buffers.
 *
 *      Module cannot send intent messages in open state.
 *
 *   2. INTF_EXTN_IMCL_PORT_CLOSE -
 *         Closes a given port index. Module can free any resources allocated
 *      for the given port. Module cannot send messages in CLOSED state.
 *
 *   3. INTF_EXTN_IMCL_PORT_PEER_CONNECTED -
 *         This operation indicates history buffer module that the peer module is
 *      connected and control link is ready for communication. Module can send
 *      messages only in this state.
 *
 *   4. INTF_EXTN_IMCL_PORT_PEER_DISCONNECTED -
 *         This operation indicates history buffer module that the peer module is
 *      disconnected and control link is not active to send any messages.
 * ========================================================================= */
capi_err_t capi_history_buffer_handle_intf_extn_ctrl_port_operation(capi_history_buffer_t *me_ptr, capi_buf_t *params_ptr);


/* =========================================================================
 * FUNCTION : capi_history_buffer_handle_incoming_imc_message
 *
 * DESCRIPTION:
 *    This function handles any incoming intent messages to history buffer module.
 * This helper funtion is called from the set_param() handling of the param id
 * INTF_EXTN_PARAM_ID_IMCL_INCOMING_DATA.
 * ========================================================================= */
capi_err_t capi_history_buffer_handle_incoming_imc_message(capi_history_buffer_t *me_ptr, capi_buf_t *params_ptr);

/* =========================================================================
 * FUNCTION : capi_history_buffer_send_flow_ctrl_msg_to_dam
 * DESCRIPTION: Sends flow control IMC message to DAM.
 *   is_gate_open = TRUE : sends gate open, DAM will start draining the
 *                          buffered data
 *   is_gate_open = FALSE: sends gate close, DAM will stop draining data and
 *                          goes back to buffering mode.
 *
 * Sending a intent to the peer module involves two steps,
 *   STEP-A) Get recurring buffer from framework, for this module must have registered for
 *           recurring buffers. If not module can request for onetime buffers.
 *   STEP-B) Populate the payload of intent buffer and raise an event to send IMC message
 *           to peer module.
 * ========================================================================= */
capi_err_t capi_history_buffer_send_flow_ctrl_msg_to_dam(capi_history_buffer_t *            me_ptr,
                                                    param_id_audio_dam_data_flow_ctrl_t *param_ptr);

/* =========================================================================
 * FUNCTION : capi_history_buffer_imcl_send_resize_to_dam
 *
 * Utility to send resize command to audio dam module
 * ========================================================================= */
capi_err_t capi_history_buffer_imcl_send_resize_to_dam(capi_history_buffer_t *me_ptr);

/* =========================================================================
 * FUNCTION : capi_history_buffer_validate_intent_id
 *
 * Utility to check if the given control port supports the intent id.
 * ========================================================================= */
bool_t capi_history_buffer_validate_intent_id(capi_history_buffer_t *me_ptr, uint32_t intent_id);

/* =========================================================================
 * FUNCTION : capi_history_buffer_check_intent_id_support
 *
 * Checks if a given opened control port supports the intent id.
 * ========================================================================= */
bool_t capi_history_buffer_check_intent_id_support(capi_history_buffer_t *me_ptr, uint32_t ctrl_port_idx, uint32_t intent_id);

/* =========================================================================
 * FUNCTION : capi_history_buffer_destroy_control_port
 *
 * Destroys
 * ========================================================================= */
void capi_history_buffer_destroy_control_port(capi_history_buffer_t *me_ptr, uint32_t ctrl_port_idx);

#ifdef __cplusplus
}
#endif
#endif /** _CAPI_HISTORY_BUFFER_IMCL_UTILS_H_ */
