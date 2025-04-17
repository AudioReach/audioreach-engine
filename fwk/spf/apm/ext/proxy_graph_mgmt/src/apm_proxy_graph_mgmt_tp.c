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

apm_proxy_graph_mgmt_t  g_apm_proxy_graph_mgmt;

/**********************************************************************************
Proxy graph managment function, called in the context of thread pool backgroud thread.
***********************************************************************************/

static apm_proxy_graph_mgmt_usecase_info_t* get_usecase(uint32_t usecase_id)
{
    for(spf_list_node_t* node_ptr = g_apm_proxy_graph_mgmt.usecase_list_ptr; node_ptr; node_ptr = node_ptr->next_ptr)
    {
        apm_proxy_graph_mgmt_usecase_info_t* usecase_ptr = (apm_proxy_graph_mgmt_usecase_info_t*)node_ptr->obj_ptr;
        if(usecase_ptr->usecase_id == usecase_id)
        {
            return usecase_ptr;
        }
    }
    return NULL;
}

ar_result_t apm_proxy_graph_mgmt_register_int(void *context_ptr)
{
    ar_result_t result = AR_EOK;

    uint32_t usecase_id = *((uint32_t *)context_ptr);
    
    if(NULL == get_usecase(usecase_id))
    {
        //already registered
        AR_MSG(DBG_ERROR_PRIO, "USE CASE id 0x%x is not registered yet. Failing client registration.", usecase_id);
        return AR_EFAILED;
    }

    return result;
}

ar_result_t apm_proxy_graph_mgmt_stop_int(void *context_ptr)
{
    ar_result_t result = AR_EOK;

    uint32_t usecase_id = *((uint32_t *)context_ptr);
    
    apm_proxy_graph_mgmt_usecase_info_t *usecase_ptr = get_usecase(usecase_id);
    if(NULL == usecase_ptr)
    {
        AR_MSG(DBG_ERROR_PRIO, "USE CASE id 0x%x is not registered. Failing.", usecase_id);
        return AR_EFAILED;
    }

    if(usecase_ptr->b_disable_control)
    {
        AR_MSG(DBG_HIGH_PRIO, "USE CASE id 0x%x is being controlled by HLOS. Ignoring proxy command.", usecase_id);
        return AR_EOK;
    }

    apm_proxy_graph_mgmt_send_start_stop(usecase_ptr->num_subgraphs, usecase_ptr->sg_id_list, FALSE);

    usecase_ptr->b_client_stop = TRUE;

    AR_MSG(DBG_HIGH_PRIO, "USE CASE id 0x%x is STOPPED.", usecase_id);

    return result;
}

ar_result_t apm_proxy_graph_mgmt_start_int(void *context_ptr)
{
    ar_result_t result = AR_EOK;

    uint32_t usecase_id = *((uint32_t *)context_ptr);

    apm_proxy_graph_mgmt_usecase_info_t *usecase_ptr = get_usecase(usecase_id);
    if(NULL == usecase_ptr)
    {
        AR_MSG(DBG_ERROR_PRIO, "USE CASE id 0x%x is not registered. Failing.", usecase_id);
        return AR_EFAILED;
    }

    if(usecase_ptr->b_disable_control)
    {
        AR_MSG(DBG_HIGH_PRIO, "USE CASE id 0x%x is bing controlled by HLOS. Ignoring proxy command.", usecase_id);
        return AR_EOK;
    }

    apm_proxy_graph_mgmt_send_start_stop(usecase_ptr->num_subgraphs, usecase_ptr->sg_id_list, TRUE);

    usecase_ptr->b_client_stop = FALSE;

    AR_MSG(DBG_HIGH_PRIO, "USE CASE id 0x%x is STARTED.", usecase_id);

    return result;
}

ar_result_t apm_proxy_graph_mgmt_get_state_int(void *context_ptr)
{
    uint32_t usecase_id = *((uint32_t *)context_ptr);

    apm_proxy_graph_mgmt_usecase_info_t *usecase_ptr = get_usecase(usecase_id);
    if(NULL == usecase_ptr)
    {
        AR_MSG(DBG_ERROR_PRIO, "USE CASE id 0x%x is not registered. Failing.", usecase_id);
        return AR_EFAILED;
    }

    if(usecase_ptr->b_disable_control)
    {
        AR_MSG(DBG_HIGH_PRIO, "USE CASE id 0x%x is bing controlled by HLOS. Graph should be in Started state.", usecase_id);
        return AR_EOK;
    }
    else if (FALSE == usecase_ptr->b_client_stop)
    {
        AR_MSG(DBG_HIGH_PRIO, "USE CASE id 0x%x is started by the LPAI Client. Graph should be in Started state.", usecase_id);
        return AR_EOK;
    }

    return AR_EFAILED; //EFAILED means graph is in stop state.
}


/**********************************************************************
Proxy graph managment function, called from HLOS.
**********************************************************************/

ar_result_t apm_proxy_graph_mgmt_usecaes_register(void *context_ptr)
{
    tp_job_info_t *job_info_ptr = (tp_job_info_t*)context_ptr;
    gpr_packet_t *gpr_pkt_ptr = job_info_ptr->gpr_pkt_ptr;

    ar_result_t result = AR_EOK;

    apm_cmd_header_t *cmd_header_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, gpr_pkt_ptr);
    apm_proxy_graph_mgmt_cmd_usecase_registration_t *usecase_ptr = (apm_proxy_graph_mgmt_cmd_usecase_registration_t*)(cmd_header_ptr+1);
    apm_param_id_sub_graph_list_t *num_sg_ptr = (apm_param_id_sub_graph_list_t*)(usecase_ptr + 1);
    uint32_t *sg_id_array = (uint32_t*)(num_sg_ptr + 1);

    uint32_t req_payload = sizeof(apm_proxy_graph_mgmt_cmd_usecase_registration_t)+ sizeof(apm_param_id_sub_graph_list_t);

    if (0 == cmd_header_ptr->mem_map_handle && 
        cmd_header_ptr->payload_size > req_payload && 
        (cmd_header_ptr->payload_size >= (req_payload + (num_sg_ptr->num_sub_graphs * sizeof(uint32_t)))))
    {
        if(NULL != get_usecase(usecase_ptr->usecase_id))
        {
            //already registered
            AR_MSG(DBG_ERROR_PRIO, "USE CASE id 0x%x is already registered..", usecase_ptr->usecase_id);
            result = AR_EALREADY;
        }
        else
        {
            //allocate use case structure and allocate memory for subgraph array
            uint32_t size_req = sizeof(apm_proxy_graph_mgmt_usecase_info_t) + (sizeof(uint32_t) * (num_sg_ptr->num_sub_graphs-1));
            apm_proxy_graph_mgmt_usecase_info_t *usecase_ptr = (apm_proxy_graph_mgmt_usecase_info_t*)posal_memory_malloc(size_req, POSAL_HEAP_DEFAULT);
            if(usecase_ptr == NULL)
            {
                AR_MSG(DBG_ERROR_PRIO, "Failed to allocate memory for usecase");
                result = AR_ENOMEMORY;
            }
            else
            {
                memset(usecase_ptr, 0, sizeof(apm_proxy_graph_mgmt_usecase_info_t));
                usecase_ptr->usecase_id = usecase_ptr->usecase_id;
                usecase_ptr->num_subgraphs = num_sg_ptr->num_sub_graphs;
                memscpy(usecase_ptr->sg_id_list, sizeof(uint32_t) * num_sg_ptr->num_sub_graphs, sg_id_array, sizeof(uint32_t) * num_sg_ptr->num_sub_graphs);
            
                //add the use case into the use case list managed by the proxy graph mgmt
                spf_list_insert_head(&g_apm_proxy_graph_mgmt.usecase_list_ptr, usecase_ptr, POSAL_HEAP_DEFAULT, TRUE);
            
                AR_MSG(DBG_HIGH_PRIO, "USE CASE id 0x%x is REGISTERED.", usecase_ptr->usecase_id);
            }
        }
    }
    else
    {
        //out band not supported
        AR_MSG(DBG_ERROR_PRIO, "Invalid Payload received for opcode : 0x%x", gpr_pkt_ptr->opcode);
        result = AR_EBADPARAM;
    }

    __gpr_cmd_end_command(gpr_pkt_ptr, result);

    posal_memory_free(job_info_ptr);

    //returning ETERMINATED to avoid accessing job_ptr from thread pool.
    return AR_ETERMINATED;
}

ar_result_t apm_proxy_graph_mgmt_enable_disable_control(void* context_ptr)
{
    tp_job_info_t *job_info_ptr = (tp_job_info_t*)context_ptr;
    gpr_packet_t *gpr_pkt_ptr = job_info_ptr->gpr_pkt_ptr;

    ar_result_t result = AR_EOK;

    apm_cmd_header_t *cmd_header_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, gpr_pkt_ptr);

    if (0 == cmd_header_ptr->mem_map_handle && cmd_header_ptr->payload_size >= sizeof(apm_proxy_graph_mgmt_cmd_enable_disable_control_t))
    {
        apm_proxy_graph_mgmt_cmd_enable_disable_control_t* cmd_payload_ptr = (apm_proxy_graph_mgmt_cmd_enable_disable_control_t *)(cmd_header_ptr + 1);
        
        apm_proxy_graph_mgmt_usecase_info_t *usecase_ptr = get_usecase(cmd_payload_ptr->usecase_id);
        if(NULL == usecase_ptr)
        {
            AR_MSG(DBG_ERROR_PRIO, "USE CASE id 0x%x is not registered. Failing.", cmd_payload_ptr->usecase_id);
            result = AR_EFAILED;
        }
        else
        {
            if (cmd_payload_ptr->disable_control)
            {
                if(usecase_ptr->b_client_stop)
                {
                    //move the graph to the start state if it is currently in stopped state.
                    result = apm_proxy_graph_mgmt_start_int(&usecase_ptr->usecase_id);
                    
                    //restore the client stop flag
                    usecase_ptr->b_client_stop = TRUE;
                }
    
                usecase_ptr->b_disable_control = TRUE;
            }
            else
            {
                usecase_ptr->b_disable_control = FALSE;
    
                if(usecase_ptr->b_client_stop)
                {
                    //move the graph to the stop state if it is stopped from the client.
                    result = apm_proxy_graph_mgmt_stop_int(&usecase_ptr->usecase_id);
                }
            }
        }
    }
    else
    {
        //out band not supported
        AR_MSG(DBG_ERROR_PRIO, "Invalid Payload received for opcode : 0x%x", gpr_pkt_ptr->opcode);
        result = AR_EBADPARAM;
    }

    __gpr_cmd_end_command(gpr_pkt_ptr, result);

    posal_memory_free(job_info_ptr);

    //returning ETERMINATED to avoid accessing job_ptr from thread pool.
    return AR_ETERMINATED;
}
