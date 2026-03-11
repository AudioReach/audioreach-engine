/**
 *   \file offload_apm_api.h
 *   \brief
 *        This file contains Shared mem module APIs extension for Read shared memory Endpoint module
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef OFFLOAD_APM_API_H_
#define OFFLOAD_APM_API_H_

#include "spf_begin_pack.h"

/* Scenario ID reserved and mapped for SAT_VOICE_CALL scenario identification . */
#define APM_SUB_GRAPH_SID_SAT_VOICE_CALL 4 //APM_SUB_GRAPH_SID_RESERVED_1

struct apm_imcl_peer_domain_info_t
{
   uint32_t module_iid;
   /**< Module instance ID of the remote peer module */

   uint32_t domain_id;
   /**< Domain ID of remote peer module */
}

#include "spf_end_pack.h"
;
typedef struct apm_imcl_peer_domain_info_t apm_imcl_peer_domain_info_t;

/**
  @ingroup apm_param_id_imcl_peer_domain_info_t
  This API is sent in addition to the OPEN payload by the OLC
  to the SATELLITE APM. This gives the information about the
  domain IDs of the IMCL peers.

  @gpr_hdr_fields
  Opcode -- APM_PARAM_ID_IMCL_PEER_DOMAIN_INFO

  @msg_payload
  apm_param_id_imcl_peer_domain_info_t

  @return
  None

  @dependencies
  None
 */
#define APM_PARAM_ID_IMCL_PEER_DOMAIN_INFO 0x08001153

/**
  This structure is the payload structure used by APM_PARAM_ID_IMCL_PEER_DOMAIN_INFO param
  at the end of the container payload of the APM_CMD_GRAPH_OPEN. This is used to communicate
  to the APM on the remote processors about the domain IDs of the Inter-processor IMCL peers.
 */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"

struct apm_param_id_imcl_peer_domain_info_t
{
   uint32_t num_imcl_peer_cfg;
   /**< Number of IMCL peer configuration objects
        @values  */

   apm_imcl_peer_domain_info_t imcl_peer_cfg[0];
   /**< Number of these objects is determined
        by num_imcl_peer_cfg.
        @values  */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

typedef struct apm_param_id_imcl_peer_domain_info_t apm_param_id_imcl_peer_domain_info_t;

/**
  @ingroup apm_param_id_master_pd_info_t
  This API is sent to the satellite APM informing
  the proc domain id of the master apm.

  @gpr_hdr_fields
  Opcode -- APM_PARAM_ID_MASTER_PD_INFO

  @msg_payload
  apm_param_id_master_pd_info_t

  @return
  None

  @dependencies
  None
 */
#define APM_PARAM_ID_MASTER_PD_INFO 0x08001244

/**
  This structure is the payload structure used by APM_PARAM_ID_MASTER_PD_INFO param.
 */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"

struct apm_param_id_master_pd_info_t
{
   uint32_t proc_domain;
   /**< ID of the master proc domain
        @values { "APM_PROC_DOMAIN_ID_INVALID"=0x0,
         "MDSP"=#APM_PROC_DOMAIN_ID_MDSP,
         "ADSP"=#APM_PROC_DOMAIN_ID_ADSP,
         "APPS"=#APM_PROC_DOMAIN_ID_APPS,
         "SDSP"=#APM_PROC_DOMAIN_ID_SDSP,
         "CDSP"=#APM_PROC_DOMAIN_ID_CDSP,
         "GDSP_0"=#APM_PROC_DOMAIN_ID_GDSP_0,
         "GDSP_1"=#APM_PROC_DOMAIN_ID_GDSP_1,
         "APPS_2"=#APM_PROC_DOMAIN_ID_APPS_2 } */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct apm_param_id_master_pd_info_t apm_param_id_master_pd_info_t;
#endif // OFFLOAD_APM_API_H_
