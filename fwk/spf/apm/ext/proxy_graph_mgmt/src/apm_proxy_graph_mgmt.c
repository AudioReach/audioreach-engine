/**
 * \file apm_proxy_graph_mgmt.c
 * \brief
 *     This file contains APM proxy graph managment utility functions.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "apm_proxy_graph_mgmt_i.h"

/****************************************************************************
 * Object                                                                   *
 ****************************************************************************/

extern apm_proxy_graph_mgmt_t  g_apm_proxy_graph_mgmt;

/**********************************************************************
Proxy graph managment function, called from MicroApps
**********************************************************************/
ar_result_t apm_proxy_graph_mgmt_register(uint32_t usecase_id, void **client_handle_pptr)
{
    if (g_apm_proxy_graph_mgmt.thread_pool_ptr == NULL)
    {
        AR_MSG(DBG_ERROR_PRIO, "apm_proxy_graph_mgmt_register failed, thread pool not initialized.");
        return AR_EFAILED;
    }

    //create client handle
    proxy_client_handle_t *client_handle_ptr = (proxy_client_handle_t*)posal_memory_malloc(sizeof(proxy_client_handle_t), POSAL_HEAP_DEFAULT);
    if(NULL == client_handle_ptr)
    {
        AR_MSG(DBG_ERROR_PRIO, "apm_proxy_graph_mgmt_register failed, no memory");
        return AR_ENOMEMORY;
    }

    //assigne use case id and channel/signal ptr
    client_handle_ptr->usecase_id = usecase_id;
    
    posal_channel_create(&client_handle_ptr->wait_channel_ptr, POSAL_HEAP_DEFAULT);

    posal_signal_create(&client_handle_ptr->wait_signal_ptr, POSAL_HEAP_DEFAULT);

    posal_channel_add_signal(client_handle_ptr->wait_channel_ptr, client_handle_ptr->wait_signal_ptr, 0x1);

    spf_thread_pool_job_t job = {.job_func_ptr = apm_proxy_graph_mgmt_register_int, .job_context_ptr = &client_handle_ptr->usecase_id, .job_result = AR_EOK, .job_signal_ptr = client_handle_ptr->wait_signal_ptr};

    //push it to the thread where usecase is added into the data base synchronously.
    spf_thread_pool_push_job_with_wait(g_apm_proxy_graph_mgmt.thread_pool_ptr, &job, 0);

    if(job.job_result != AR_EOK)
    {
        posal_signal_destroy(&client_handle_ptr->wait_signal_ptr);
        posal_channel_destroy(&client_handle_ptr->wait_channel_ptr);
        posal_memory_free(client_handle_ptr);
        client_handle_ptr = NULL;
    }

    //return the handle
    *client_handle_pptr = client_handle_ptr;

    return job.job_result;
}

ar_result_t apm_proxy_graph_mgmt_stop(void* context_ptr)
{
    if (g_apm_proxy_graph_mgmt.thread_pool_ptr == NULL)
    {
        AR_MSG(DBG_ERROR_PRIO, "apm_proxy_graph_mgmt_stop failed, thread pool not initialized.");
        return AR_EFAILED;
    }

    proxy_client_handle_t *client_handle_ptr = (proxy_client_handle_t*)context_ptr;

    spf_thread_pool_job_t job = {.job_func_ptr = apm_proxy_graph_mgmt_stop_int, .job_context_ptr = &client_handle_ptr->usecase_id, .job_result = AR_EOK, .job_signal_ptr = client_handle_ptr->wait_signal_ptr};

    //push it to the thread where usecase is stopped synchronously.
    spf_thread_pool_push_job_with_wait(g_apm_proxy_graph_mgmt.thread_pool_ptr, &job, 0);

    return job.job_result;
}

ar_result_t apm_proxy_graph_mgmt_start(void* context_ptr)
{
    if (g_apm_proxy_graph_mgmt.thread_pool_ptr == NULL)
    {
        AR_MSG(DBG_ERROR_PRIO, "apm_proxy_graph_mgmt_start failed, thread pool not initialized.");
        return AR_EFAILED;
    }

    proxy_client_handle_t *client_handle_ptr = (proxy_client_handle_t*)context_ptr;

    spf_thread_pool_job_t job = {.job_func_ptr = apm_proxy_graph_mgmt_start_int, .job_context_ptr = &client_handle_ptr->usecase_id, .job_result = AR_EOK, .job_signal_ptr = client_handle_ptr->wait_signal_ptr};

    //push it to the thread where usecase is started synchronously.
    spf_thread_pool_push_job_with_wait(g_apm_proxy_graph_mgmt.thread_pool_ptr, &job, 0);

    return job.job_result;
}

bool_t apm_proxy_graph_mgmt_get_state(void *context_ptr)
{
    if (g_apm_proxy_graph_mgmt.thread_pool_ptr == NULL)
    {
        AR_MSG(DBG_ERROR_PRIO, "apm_proxy_graph_mgmt_get_status failed, thread pool not initialized.");
        return FALSE;
    }

    proxy_client_handle_t *client_handle_ptr = (proxy_client_handle_t*)context_ptr;

    spf_thread_pool_job_t job = {.job_func_ptr = apm_proxy_graph_mgmt_get_state_int, .job_context_ptr = &client_handle_ptr->usecase_id, .job_result = AR_EOK, .job_signal_ptr = client_handle_ptr->wait_signal_ptr};

    //push it to the thread where usecase is started synchronously.
    spf_thread_pool_push_job_with_wait(g_apm_proxy_graph_mgmt.thread_pool_ptr, &job, 0);

    return (job.job_result == AR_EOK)? TRUE: FALSE;
}

ar_result_t apm_proxy_graph_mgmt_deregister(void* context_ptr)
{
    ar_result_t result = apm_proxy_graph_mgmt_start(context_ptr);

    proxy_client_handle_t *client_handle_ptr = (proxy_client_handle_t*)context_ptr;

    //destroy the client handle.
    posal_signal_destroy(&client_handle_ptr->wait_signal_ptr);
    posal_channel_destroy(&client_handle_ptr->wait_channel_ptr);
    posal_memory_free(client_handle_ptr);

    return result;
}


/**********************************************************************
Proxy graph managment function, called from SPF framework
**********************************************************************/

ar_result_t apm_proxy_graph_mgmt_init()
{
    ar_result_t result = AR_EOK;
    memset(&g_apm_proxy_graph_mgmt, 0, sizeof(g_apm_proxy_graph_mgmt));
 
    //register with GPR
    apm_proxy_graph_mgmt_gpr_init();

    // initialize the  thread pool which will be reserved
    spf_thread_pool_get_instance(&g_apm_proxy_graph_mgmt.thread_pool_ptr,
                                 POSAL_HEAP_DEFAULT,
                                 THREAD_PRIO,                                 
                                 TRUE, /*is_dedicated_pool*/
                                 STACK_SIZE,
                                 1,
                                 0);

    return result;

}

ar_result_t apm_proxy_graph_mgmt_deinit()
{
   //destroying the thread first.
   spf_thread_pool_release_instance(&g_apm_proxy_graph_mgmt.thread_pool_ptr, 0);

    //move all the use cases to default START state.
   for(spf_list_node_t* node_ptr = g_apm_proxy_graph_mgmt.usecase_list_ptr; node_ptr; node_ptr = node_ptr->next_ptr)
   {
       apm_proxy_graph_mgmt_usecase_info_t* usecase_ptr = (apm_proxy_graph_mgmt_usecase_info_t*)node_ptr->obj_ptr;
       apm_proxy_graph_mgmt_start_int(&usecase_ptr->usecase_id);
   }

   spf_list_delete_list_and_free_objs(&g_apm_proxy_graph_mgmt.usecase_list_ptr, TRUE);

   //deregistering with GPR
   apm_proxy_graph_mgmt_gpr_deinit();

   return AR_EOK;
}