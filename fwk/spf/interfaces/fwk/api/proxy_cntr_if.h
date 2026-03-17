#ifndef _PROXY_CONTAINER_IF_H_
#define _PROXY_CONTAINER_IF_H_

/**
 * \file vcpm_cntr_if.h
 *  
 * \brief
 *     This file defines container APIs with proxy (VCPM).
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "posal_types.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus
/********************************************************************************************************/
/*                                          Parameters                                                  */
/********************************************************************************************************/

/**
 * This param ID is used as part of #SPF_MSG_CMD_SET_CFG.
 *
 * This parameter is used by container clients (E.g., VCPM) to
 * set the voice session parameters needed by the container.
 *
 * Payload: cntr_param_id_voice_session_info_t
 */
#define CNTR_PARAM_ID_VOICE_SESSION_INFO 0x080010D8

typedef struct cntr_param_id_voice_session_info_t cntr_param_id_voice_session_info_t;

/**
 * Payload for CNTR_PARAM_ID_VOICE_SESSION_INFO
 */
struct cntr_param_id_voice_session_info_t
{
   uint32_t vsid;
   /**< The voice system ID*/
   uint32_t vfr_mode;
   /**< The VFR mode - hard/soft*/
   uint32_t period_us;
   /**< The container processing period set by a client
        service (like VCPM - VFR cycle period)*/
};

/**
 * This param ID is used as part of #SPF_MSG_CMD_GET_CFG.
 *
 * This parameter is used by container clients (E.g., VCPM) to
 * set the proc duration to the containers.
 *
 * For instance, this may be used by the containers to
 * appropriately set their thread priorities.
 *
 * Payload: cntr_param_id_proc_duration_t
 */

#define CNTR_PARAM_ID_PROC_DURATION 0x080010BD

typedef struct cntr_param_id_proc_duration_t cntr_param_id_proc_duration_t;

/**
 * Payload for CNTR_PARAM_ID_PROC_DURATION
 */
struct cntr_param_id_proc_duration_t
{
   uint32_t proc_duration_us;
   /**< Processing duration of the container*/

   uint32_t safety_margin_us;
   /**< For voice containers, even though proc dur is < 1ms, there's leg room of 1.5 ms. used for thread priority.*/
};

/**
 * This param ID is used as part of #SPF_MSG_CMD_GET_CFG.
 *
 * This parameter is used by container clients (E.g., VCPM) to
 * query the container proc params..
 *
 * Payload: cntr_param_id_container_proc_params_info_t
 */
#define CNTR_PARAM_ID_CONTAINER_PROC_PARAMS_INFO 0x080010E0

typedef struct cntr_param_id_container_proc_params_info_t cntr_param_id_container_proc_params_info_t;

typedef struct cntr_proc_params_flags_t
{
   uint32_t kpps_query : 1;
   uint32_t frame_size_query : 1;
   uint32_t bw_query : 1;
   uint32_t hw_acc_proc_delay_query : 1;
} cntr_proc_params_flags_t;

/**
 * Payload for CNTR_PARAM_ID_CONTAINER_PROC_PARAMS_INFO
 */
struct cntr_param_id_container_proc_params_info_t
{
   cntr_proc_params_flags_t event_flags;
   /* flag that denotes the queried field among the following - Populated by the client
    * bitfields need to be set if the metric needs to be queried.
    *
    *   All other bits are reserved. */
   uint32_t kpps;
   /**< Aggregated KPPS vote of the container*/
   uint32_t frame_size_us;
   /*operating frame size (ICB) of the container*/
   uint32_t bw;
   /*Aggregated BW voted by the container in BPS*/
   uint32_t hw_acc_proc_delay_us;
   /*HW accelerator proc delay*/
};

#define CNTR_EVENT_ID_CONTAINER_PERF_PARAMS_UPDATE 0x08001122

typedef struct cntr_event_id_container_perf_params_update_t cntr_event_id_container_perf_params_update_t;

/**
 * Payload for CNTR_EVENT_ID_CONTAINER_PERF_PARAMS_UPDATE
 */
struct cntr_event_id_container_perf_params_update_t
{
   uint32_t kpps;
   /**< KPPS vote of the container*/
   uint32_t bw;
   /*BW voted by the container in BPS*/
   uint32_t frame_size_us;
   /*operating frame size (ICB) of the container*/
   bool_t did_algo_delay_change;
   /* bool indicating that the algo delay might've changed. Client may use this
   to re-query */
   bool_t did_hw_acc_proc_delay_change;
   /*bool indicating that the HW acc proc delay might have changed and the latest value 
     is stored in hw_acc_proc_delay_us*/
   uint32_t hw_acc_proc_delay_us;
   /*HW accelerator proc delay*/
};

/**
 * This event ID is used to indicate frame delivery done info by the containers to the client.
 *
 * This event is used by container clients (E.g., VCPM) to
 * keep track of Container sleep duration during steady state.
 *
 * Payload: None
 */

#define CNTR_EVENT_ID_CONTAINER_FRAME_DELIVERY_DONE 0x0800116F

/**
 * This param ID is used as part of #SPF_MSG_CMD_SET_CFG.
 *
 * This parameter is used by container clients (E.g., APM/VCPM) to
 * set the offload voice session parameters needed by the container.
 * Payload: cntr_param_id_offload_voice_session_info_t
 */
#define CNTR_PARAM_ID_OFFLOAD_VOICE_SESSION_INFO 0x08001B35

struct cntr_param_id_offload_voice_session_info_t
{
   uint32_t sg_id;
   /**< Subgraph ID.*/

   uint32_t kpps_sf;
   /**< kpps scale factor to be applied on offload path.*/

   uint32_t bw_sf;
   /**< bw scale factor to be applied on offload path.*/
};
typedef struct cntr_param_id_offload_voice_session_info_t cntr_param_id_offload_voice_session_info_t;

/**
 * This param ID is used as part of #SPF_MSG_CMD_GET_CFG.
 *
 * This parameter is used by container clients (E.g., VCPM) to
 * Get the satellite domain info mapped to the OLC.
 *
 * Payload: cntr_param_id_satellite_domain_info_t
 */
#define CNTR_PARAM_ID_SATELLITE_DOMAIN_INFO 0x08001B63

struct cntr_param_id_satellite_domain_info_t
{
   uint32_t proc_id;
   /**< The proc ID of satellite domain. Its an ouput field */
};
typedef struct cntr_param_id_satellite_domain_info_t cntr_param_id_satellite_domain_info_t;

/********************************************************************************************************/
/*                                          Messages                                                    */
/********************************************************************************************************/

typedef struct spf_event_info_t spf_event_info_t;
struct spf_event_info_t
{
   uint32_t event_id;
   /**< unique id of the event, SPF_EVT_ID* */

   uint32_t payload_size;
   /**< size of the event specific payload */

   void *event_payload_ptr;
   /**< pointer to event specific payload, if any. This must be set to NULL,
    * if no event specific payload is required*/
};

/**
 * Payload for SPF_EVT_ID_CLOCK_VOTE_CHANGE
 */
typedef struct spf_event_clock_vote_change_t spf_event_clock_vote_change_t;
struct spf_event_clock_vote_change_t
{
   uint32_t vsid;
   /**< VSID of the VCPM sesion to which this event is being sent. */
};

/**
 * This param ID is used as part of #SPF_MSG_CMD_SET_CFG.
 *
 * This parameter is used by container clients (e.g., VCPM) to notify  
 * that all the calibration has been applied for the container in question.
 * 
 * On receiving this param, the container can in turn notify all 
 * the modules subscribing to the INTF_EXTN_CALIBRATION_OPS
 * 
 * There is no payload for CNTR_PARAM_ID_CALIBRATION_OPS_DONE 
 * 
 * Payload: cntr_param_id_calibration_ops_t
 */
#define CNTR_PARAM_ID_CALIBRATION_OPS_DONE 0x0800FFFF                            /*TODO - Reserve PID */

/**
 * Payload for CNTR_PARAM_ID_CALIBRATION_OPS_DONE is not supported.
 */

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef _PROXY_CONTAINER_IF_H_
