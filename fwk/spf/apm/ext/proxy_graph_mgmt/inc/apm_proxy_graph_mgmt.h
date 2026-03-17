#ifndef _APM_PROXY_GRAPH_MGMT_H_
#define _APM_PROXY_GRAPH_MGMT_H_

/**
 * \file apm_proxy_graph_mgmt.h
 * \brief
 *     This file contains APM proxy graph managment utility functions.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_defs.h"
#include "posal.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/**********************************************************************
Proxy graph managment function, called from SPF framework
**********************************************************************/

ar_result_t apm_proxy_graph_mgmt_init();

ar_result_t apm_proxy_graph_mgmt_deinit();

/**********************************************************************
Proxy graph managment function, called from MicroApps (LPAI based client)
**********************************************************************/
ar_result_t apm_proxy_graph_mgmt_register(uint32_t usecase_id, void **client_handle_pptr);

ar_result_t apm_proxy_graph_mgmt_stop(void* client_handle_ptr);

ar_result_t apm_proxy_graph_mgmt_start(void *client_handle_ptr);

//TRUE: graph is started, FALSE: graph is stopped or not setup
bool_t apm_proxy_graph_mgmt_get_state(void *client_handle_ptr);

ar_result_t apm_proxy_graph_mgmt_deregister(void *client_handle_ptr);

#ifdef __cplusplus
}
#endif /*__cplusplus*/
#endif /* _APM_PROXY_GRAPH_MGMT_H_ */
