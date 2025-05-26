#ifndef _APM_PRIVATE_API_H_
#define _APM_PRIVATE_API_H_
/**
 * \file apm_private_api.h
 * \brief
 *    This file contains the version information for Audio Procesing Manager Private API.
 *
 * \copyright
 *    Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *    SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/** @addtogroup aapm_private_api
@{ */

#include "ar_defs.h"

#ifdef __cplusplus
extern "C"
{
#endif /*__cplusplus*/

/** @ingroup APM_CMD_REGISTER_MODULE_EVENTS_V2
  Registers/de-registers one or more event ID's for one or more
  module instances present in the graph. This command can be only sent
  to modules present in an opened graph. This command
  cannot be sent to APM_MODULE_INSTANCE_ID.

  Compared to V1 command, the V2 command has additional fields to
  specify the GPR address of the destination. The domain id, destination,
  client token and heap id fields should be populated depending on the
  client which actually needs to recieve the module event.

  This command can be issued for all the module instance ID that
  have been configured at least once using the #APM_CMD_GRAPH_OPEN

  @Payload struct
      apm_cmd_header_t
      apm_module_register_events_v2_t
      <event config payload if any>
      <8 byte alignment if any>
      apm_module_register_events_v2_t
      <event config payload if any>
      <8 byte alignment if any, but not mandatory at the end>

  When there are multiple events different apm_module_register_events_v2_t
  must be 8 byte aligned.

  @gpr_hdr_fields
  Opcode -- APM_CMD_REGISTER_MODULE_EVENTS_V2

  @return
  Error code -- #GPR_BASIC_RSP_RESULT

  @dependencies
  Module ID's listed as part of this command must be
  configured using the #APM_CMD_GRAPH_OPEN command or else and
  error code is returned by APM.

  */
#define APM_CMD_REGISTER_MODULE_EVENTS_V2 0x01001047

/**
   Payload of the register events data structure. This
   structure is preceded by apm_cmd_header_t structure
*/
#include "spf_begin_pack.h"
struct apm_module_register_events_v2_t
{
   uint32_t module_instance_id;
   /**< Valid instance ID of module */

   uint32_t event_id;
   /**< Valid event ID of the module */

   uint32_t is_register;
   /**< 1 - to register the event
    *   0 - to de-register the event
    */

   uint32_t dst_domain_id;
   /**< Domain ID of the destination where the event is to be delivered.

        - Bits 0 to 7 (eight bits).
        - The domain ID refers to a process, a processor, or an off-target
      location that houses GPR.
        - All domain ID values are reserved by GPR. */

   uint32_t dst_port;
   /**< Unique ID of the service where the event is to be delivered.

       - Bits 31 to 0 (thirty-two bits).
       - The dst_port refers to the ID that the receiver service registers with
      the GPR.
       - This ID must be unique to the service within a given domain. */

   uint32_t client_token;
   /**< Client specific token that needs to be populated in the gpr
        packet header by the module when sending event to the client.*/

   uint32_t heap_id;
   /**< Supported values:
           APM_CONT_HEAP_DEFAULT (0x1)   - Default heap ID
           APM_CONT_HEAP_LOW_POWER (0x2) - Low power heap ID

         The heap id indicates the heap used for allocating the event's GPR packet.
         This depends upon the destination to which event is being sent to.

         1. If the event is being sent to Apps the default heap ID is populated.
         2. If the event is being sent to an island client. For example for the
            ACD event to ASPS static service, low power heap id should be populated.
            This allows sending the event within low power mode. */

   uint32_t error_code;
   /**< Error code populated by the entity hosting the  module.
     Applicable only for out-of-band command mode  */

   uint32_t event_config_payload_size;
   /**< Size of the event config data based upon the
        module_instance_id/event_id combination.
        @values > 0 bytes, in multiples of
        4 bytes at least */

   uint32_t reserved;
   /**< reserved must be set to zero.
   For 8 byte alignment */
}
#include "spf_end_pack.h"
;
typedef struct apm_module_register_events_v2_t apm_module_register_events_v2_t;

/** @ingroup spf_apm_graph_props
    Container type identifier for a Front end container. */
#define APM_CONTAINER_TYPE_ID_FRONT_END_FWK             0x0B00100E

/** @ingroup spf_apm_container_props
    In-Sample mode for the container frame size property. */
#define APM_CONTAINER_PROP_FRAME_SIZE_SAMPLES 2

#include "spf_begin_pack.h"
struct apm_cont_prop_id_frame_size_samples_t
{
   uint32_t frame_size_samples;
   /**< Processing frame size of the container in samples.*/

   /*#< @h2xmle_range       {8...100000}
        @h2xmle_default     {240}
        @h2xmle_description {Processing frame size of the container in samples.} */
}
#include "spf_end_pack.h"
;

typedef struct apm_cont_prop_id_frame_size_samples_t apm_cont_prop_id_frame_size_samples_t;
  
/**
 * This param ID is used as part of #APM_CMD_SET_CFG.
 *
 * This parameter is used by  clients (E.g., VCPM) to
 * set the offload session parameters needed by the subgraphs in satellite domain.
 *
 * Payload: apm_param_id_offload_voice_session_info_t
 */
#define APM_PARAM_ID_OFFLOAD_VOICE_SESSION_INFO 0x08001B62

#include "spf_begin_pack.h"
struct apm_param_id_offload_voice_session_info_payload_t
{
   uint32_t sg_id;
   /**< SubGraph ID to which this param payload needs to be applied. */
   uint32_t kpps_sf;
   /**< kpps scale factor to be applied on offload path.*/
   uint32_t bw_sf;
   /**< bw scale factor to be applied on offload path.*/
}
#include "spf_end_pack.h"
;

typedef struct apm_param_id_offload_voice_session_info_payload_t apm_param_id_offload_voice_session_info_payload_t;
  
/**
 * Payload for APM_PARAM_ID_OFFLOAD_VOICE_SESSION_INFO
 */
 #include "spf_begin_pack.h"
 struct apm_param_id_offload_voice_session_info_t
{
  uint32_t num_configs;
  /**< num of configs.*/
  /**< Number of %apm_param_id_offload_voice_session_info_payload_t structures  
        immediately follow this structure. */
}
#include "spf_end_pack.h"
;

typedef struct apm_param_id_offload_voice_session_info_t apm_param_id_offload_voice_session_info_t;

#ifdef __cplusplus
}
#endif /*__cplusplus*/

/** @} */ /* end_addtogroup aapm_private_api */

#endif /* _APM_PRIVATE_API_H_ */
