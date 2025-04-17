#ifndef _APM_PROXY_GRAPH_MGMT_API_H_
#define _APM_PROXY_GRAPH_MGMT_API_H_
/**
 * \file apm_proxy_graph_mgmt_api.h
 * \brief
 *    This file contains APM Proxy graph mgmt service api.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_defs.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*------------------------------------------------------------------------------
 *  Module Instance ID definitions
 *----------------------------------------------------------------------------*/

/* Static instance ID. */
#ifndef APM_PROXY_GRAPH_MGMT_MODULE_INSTANCE_ID
#define APM_PROXY_GRAPH_MGMT_MODULE_INSTANCE_ID  0x00000008
#endif

/*------------------------------------------------------------------------------
 *  API Definitions
 *----------------------------------------------------------------------------*/

/**
  The command is used to register the usecase with the APM Proxy graph mgmt service.
  The usecase is identified by the list sub-graph IDs which will be controlled 
  by an unique LPAI based client.

  @Payload struct for APM_PROXY_GRAPH_MGMT_CMD_USECASE_REGISTRATION
     apm_cmd_header_t (only inband payload supported)
     apm_proxy_graph_mgmt_cmd_usecase_registration_t (usecase id)
     apm_param_id_sub_graph_list_t  (Contains the number ofsub-graph IDs.) \n
     apm_sub_graph_id_t  (Array of sub-graph IDs.)

  @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

 */
#define APM_PROXY_GRAPH_MGMT_CMD_USECASE_REGISTRATION           0x0100105F

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct apm_proxy_graph_mgmt_cmd_usecase_registration_t
{
   uint32_t usecase_id;
   /**< Usecase ID for the use case of an unique LPAI based client. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

typedef struct apm_proxy_graph_mgmt_cmd_usecase_registration_t apm_proxy_graph_mgmt_cmd_usecase_registration_t;


/**
  The command is used to enable or disable the graph mgmt control for LPAI based client.
  -If control is disabled then the LPAI based client can not control the sub graph state.
  -If control is enabled then the LPAI based client can control the sub graph state.
  -HLOS enables the control during the HLOS sleep period and graphs has to be managed within LPAI.
  -HLOS disables the control during concurrency where HLOS is active.

  @Payload struct for APM_PROXY_GRAPH_MGMT_CMD_ENABLE_DISABLE_CONTROL
     apm_cmd_header_t (only inband payload supported)
     apm_proxy_graph_mgmt_cmd_enable_disable_control_t

  @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

 */
#define APM_PROXY_GRAPH_MGMT_CMD_ENABLE_DISABLE_CONTROL            0x01001060

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct apm_proxy_graph_mgmt_cmd_enable_disable_control_t
{
   uint32_t usecase_id;
   /**< Usecase ID of the usecases where LPAI based client control is enabled or disabled. */

   uint32_t disable_control;
   /*
      0: control is enabled for LPAI based client
      1: control is disabled for LPAI based client
   */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

typedef struct apm_proxy_graph_mgmt_cmd_enable_disable_control_t apm_proxy_graph_mgmt_cmd_enable_disable_control_t;

#ifdef __cplusplus
}
#endif /*__cplusplus*/
#endif //_APM_PROXY_GRAPH_MGMT_API_H_
