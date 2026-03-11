#ifndef IMCL_SA_HDT_API_H
#define IMCL_SA_HDT_API_H

/**
  @file imcl_sa_hdt_api.h

  @brief defines the Intent IDs for communication over Inter-Module Control
  Links (IMCL) between Spatial audio head tracking and Spatial audio processing
  modules.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear

  Edit History

  when          who         what, where, why
  --------    -------      --------------------------------------------------
=============================================================================*/

#include "imcl_spm_intent_api.h"

#define MAX_NUM_STREAMS 2
#define MAX_STREAM_RAW_PAYLOAD_SIZE 256 // Assuming HDT payload from BT is not going to exceed 256 byte.

#ifdef INTENT_ID_SA_HDT_RAW_DATA

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/**< Header - Any IMCL message will have the following header followed by the actual payload.
 *  The peers have to parse the header accordingly*/

typedef struct sa_hdt_imcl_header_t
{
   // specific purpose understandable to the IMCL peers only
   uint32_t opcode;

   // Size (in bytes) for the payload specific to the intent.
   uint32_t actual_data_len;

} sa_hdt_imcl_header_t;

#define MIN_INCOMING_IMCL_PARAM_SIZE_SA_HDT (sizeof(intf_extn_param_id_imcl_incoming_data_t) + sizeof(sa_hdt_imcl_header_t))

/* ============================================================================
   Param ID
==============================================================================*/

#define PARAM_ID_SA_HDT_SHARED_IMC_PAYLOAD_RAW 0x08001B6F

/*==============================================================================
   Param structure defintions
==============================================================================*/

#include "spf_begin_pack.h"

struct sa_hdt_raw_stream_t
{
   // Stream ID
   uint32_t stream_id;

   // Stream size in bytes
   uint32_t stream_size;

   // Payload for stream
   uint8_t stream_payload[MAX_STREAM_RAW_PAYLOAD_SIZE];

}
#include "spf_end_pack.h"
;
typedef struct sa_hdt_raw_stream_t sa_hdt_raw_stream_t;

#include "spf_begin_pack.h"
struct sa_hdt_raw_imc_config_t
{
   // number of time configuration is updated by HDT module
   posal_atomic_word_t atomic_counter;

   // Number of streams in the payload
   uint32_t num_streams;

   struct sa_hdt_raw_stream_t sa_hdt_raw_stream[MAX_NUM_STREAMS];

}
#include "spf_end_pack.h"
;
typedef struct sa_hdt_raw_imc_config_t sa_hdt_raw_imc_config_t;

#include "spf_begin_pack.h"

struct sa_hdt_imc_ping_pong_raw_t
{
   // first configuration payload (part of the ping pong config payload)
   sa_hdt_raw_imc_config_t config1;

   // second configuration payload (part of the ping pong config payload)
   sa_hdt_raw_imc_config_t config2;
}
#include "spf_end_pack.h"
;

typedef struct sa_hdt_imc_ping_pong_raw_t sa_hdt_imc_ping_pong_raw_t;

/** @h2xmlp_parameter   {"PARAM_ID_SA_HDT_SHARED_IMC_PAYLOAD_RAW",
                         PARAM_ID_SA_HDT_SHARED_IMC_PAYLOAD_RAW}
    @h2xmlp_description {This parameter is used to send the HDT raw payload
                         to the spatializer process module in Rx path.}
    @h2xmlp_toolPolicy  {NO_SUPPORT} */
typedef struct sa_hdt_imc_ping_pong_raw_t *sa_hdt_shared_imc_payload_raw_t;

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif // INTENT_ID_SA_HDT_RAW_DATA

#endif /* IMCL_SA_HDT_API_H */