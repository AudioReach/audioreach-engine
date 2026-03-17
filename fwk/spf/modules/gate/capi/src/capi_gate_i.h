/**
 * \file capi_gate_i.h
 * \brief
 *        Header file to define types internal to the CAPI interface for the gate module
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef CAPI_GATE_I_H
#define CAPI_GATE_I_H
/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#include "capi_cmn.h"
#include "gate_api.h"
#include "capi_gate.h"
#include "capi_cmn_imcl_utils.h"
#include "capi_intf_extn_imcl.h"
#include "imcl_deadline_time_api.h"
#include "shared_lib_api.h"
#include "capi_fwk_extns_container_proc_duration.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*------------------------------------------------------------------------
 * Macros
 * -----------------------------------------------------------------------*/
#define GATE_MAX_INTENTS_PER_CTRL_PORT 1
#define GATE_MAX_CONTROL_PORTS 1
#define CAPI_GATE_KPPS 50
#define GATE_NUM_FRAMEWORK_EXTENSIONS 1
#define GATE_NUM_US_PER_MS 1000
#define GATE_SCHEDULING_JITTER_US 100
//#define GATE_DBG 1
/*------------------------------------------------------------------------
 * Type definitions
 * -----------------------------------------------------------------------*/
typedef struct gate_ctrl_port_info_t
{
   uint32_t port_id;
   /**< Control port ID */

   imcl_port_state_t state;
   /**< Control port state */
} gate_ctrl_port_info_t;

typedef struct capi_gate_t
{
   const capi_vtbl_t *vtbl_ptr;
   /* pointer to virtual table */

   capi_event_callback_info_t event_cb_info;
   /* Event Call back info received from Framework*/

   capi_heap_id_t heap_mem;
   /* Heap id received from framework*/

   capi_media_fmt_v2_t operating_mf;
   /* Operating Media Format of the Module*/

   gate_ctrl_port_info_t in_ctrl_port_info;
   /* Control port info of the Module*/

   uint32_t ep_transmit_delay_us;
   /* Transmission delay of ep*/

   uint64_t proc_dur_us;

   uint64_t deadline_time_us;
   /* Deadline time reported by cop depack module*/

   uint32_t frame_interval_us;
   /* Encoder frame size in us*/

   uint32_t frame_size_bytes;
   /* Encoder frame size in bytes, storing to prevent re-calc */

   bool_t inp_mf_received;
   /*Flag to indicate inp mf is received */

   bool_t proc_dur_received;
   /*Flag to indicate proc delay is received */

   bool_t deadline_time_intent_received;
   /*Flag to indicate deadline_time_intent is received */

   bool_t gate_opened;
   /*Flag to indicate gate is opened */

   int32_t deadline_offset_us;
   /* Deadline offset in micro seconds */
} capi_gate_t;

/* clang-format on */

/*------------------------------------------------------------------------
 * Function Declarations
 * -----------------------------------------------------------------------*/
capi_err_t capi_gate_until_deadline_process(capi_gate_t *       me_ptr,
                                            capi_stream_data_t *input[],
                                            capi_stream_data_t *output[]);

capi_err_t capi_gate_imcl_port_operation(capi_gate_t *           me_ptr,
                                         const capi_port_info_t *port_info_ptr,
                                         capi_buf_t *            params_ptr);

capi_err_t capi_gate_calc_acc_delay(capi_gate_t *me_ptr);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif // CAPI_GATE_I_H
