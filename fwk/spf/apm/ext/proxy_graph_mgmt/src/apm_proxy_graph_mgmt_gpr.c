/**
 * \file apm_proxy_graph_mgmt_gpr_utils.c
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

/***********************************************************************************
 * Global Defines
 ***********************************************************************************/
 
/* Define the GPR service name and instance ID.
  */
#define GPR_SERVICE_NAME "apm_proxy_graph_mgmt"
#define GPR_INSTANCE_ID APM_PROXY_GRAPH_MGMT_MODULE_INSTANCE_ID

/**
 * Define the start and stop tokens.
 */
static const uint32_t start_token = 0x98765432;
static const uint32_t stop_token = 0x87654321;

/* Signal for synchronizing the command handling */
static posal_signal_t apm_proxy_gpr_signal_ptr = NULL;
static posal_channel_t apm_proxy_gpr_channel_ptr = NULL;

/***********************************************************************************
 * Functions called by the SPF framework
 ***********************************************************************************/

static uint32_t gpr_callback(gpr_packet_t *gpr_pkt_ptr, void *ctxt_ptr);
 
ar_result_t apm_proxy_graph_mgmt_gpr_init() 
{
    ar_result_t      rc             = AR_EOK;
    gpr_heap_index_t gpr_heap_index = GPR_HEAP_INDEX_DEFAULT;

    // Register with GPR
    if (AR_EOK != (rc = __gpr_cmd_register_v2(GPR_INSTANCE_ID, gpr_callback, NULL, gpr_heap_index)))
    {
        // Error registering with GPR
        AR_MSG(DBG_ERROR_PRIO, "Failed to register with GPR, result: 0x%8x", rc);
    }

    //create channel and signal
    posal_channel_create(&apm_proxy_gpr_channel_ptr, POSAL_HEAP_DEFAULT);
    posal_signal_create(&apm_proxy_gpr_signal_ptr, POSAL_HEAP_DEFAULT);
    posal_channel_add_signal(apm_proxy_gpr_channel_ptr, apm_proxy_gpr_signal_ptr, 0x1);

    return rc;
}

ar_result_t apm_proxy_graph_mgmt_gpr_deinit() 
{
    ar_result_t      rc             = AR_EOK;
    gpr_heap_index_t gpr_heap_index = GPR_HEAP_INDEX_DEFAULT;

    // De-register with GPR
    if (AR_EOK != (rc = __gpr_cmd_deregister_v2(GPR_INSTANCE_ID, gpr_heap_index)))
    {
        // Error de-registering with GPR
        AR_MSG(DBG_ERROR_PRIO, "Failed to de-register with GPR, result: %lu", rc);
    }

   // destroy signal and channel
   posal_signal_destroy(&apm_proxy_gpr_signal_ptr);
   posal_channel_destroy(&apm_proxy_gpr_channel_ptr);

    return rc;
}

/***********************************************************************************
 * Functions invoked by the HLOS and Ack from APM
 ***********************************************************************************/
static uint32_t gpr_callback(gpr_packet_t *gpr_pkt_ptr, void *ctxt_ptr)
{
   if (NULL == gpr_pkt_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "received NULL gpr packet");
      return AR_EBADPARAM;
   }

   AR_MSG_ISLAND(DBG_LOW_PRIO,
                 "GPR received opcode (0x%x) token (0x%x) from src_port (0x%x)",
                 gpr_pkt_ptr->opcode,
                 gpr_pkt_ptr->token,
                 gpr_pkt_ptr->src_port);

   switch(gpr_pkt_ptr->opcode)
   {
        case APM_PROXY_GRAPH_MGMT_CMD_USECASE_REGISTRATION:
        {
            tp_job_info_t* job_ptr = (tp_job_info_t*)posal_memory_malloc(sizeof(tp_job_info_t), POSAL_HEAP_DEFAULT);
            if(job_ptr)
            {
                job_ptr->job.job_context_ptr = job_ptr;
                job_ptr->job.job_func_ptr = apm_proxy_graph_mgmt_usecaes_register;
                job_ptr->gpr_pkt_ptr = gpr_pkt_ptr;
                spf_thread_pool_push_job(g_apm_proxy_graph_mgmt.thread_pool_ptr, &job_ptr->job, 0);
            }
            break;
        }
        case APM_PROXY_GRAPH_MGMT_CMD_ENABLE_DISABLE_CONTROL:
        {
            tp_job_info_t* job_ptr = (tp_job_info_t*)posal_memory_malloc(sizeof(tp_job_info_t), POSAL_HEAP_DEFAULT);
            if(job_ptr)
            {
                job_ptr->job.job_context_ptr = job_ptr;
                job_ptr->job.job_func_ptr = apm_proxy_graph_mgmt_enable_disable_control;
                job_ptr->gpr_pkt_ptr = gpr_pkt_ptr;
                spf_thread_pool_push_job(g_apm_proxy_graph_mgmt.thread_pool_ptr, &job_ptr->job, 0);
            }
            break;
        }
        default:
        {
            switch (gpr_pkt_ptr->token)
            {
                case start_token:
                {
                    AR_MSG(DBG_LOW_PRIO, "start ack received");
                    posal_signal_send(apm_proxy_gpr_signal_ptr);
                    break;
                }
                case stop_token:
                {
                    AR_MSG(DBG_LOW_PRIO, "stop ack received");
                    posal_signal_send(apm_proxy_gpr_signal_ptr);
                    break;
                }
                default:
                {
                    AR_MSG(DBG_LOW_PRIO, "unexpected packet received. opcode: 0x%x, token: 0x%x", gpr_pkt_ptr->opcode, gpr_pkt_ptr->token);
                    break;
                }
            }
            // Free the GPR packet
            __gpr_cmd_free(gpr_pkt_ptr);
            break;
        }
   }

   return AR_EOK;
}

/***********************************************************************************
 * Functions invoked by the Proxy Client
 ***********************************************************************************/

static gpr_packet_t *alloc_gpr_pkt(uint32_t dest_domain_id,
                                         uint32_t dest_port,
                                         uint32_t token,
                                         uint32_t opcode,
                                         uint32_t module_iid,
                                         uint32_t param_id,
                                         uint32_t payload_size)
{
    gpr_packet_t *         pkt_ptr = NULL;
    gpr_cmd_alloc_ext_v2_t args;

   payload_size += sizeof(apm_proxy_graph_mgmt_cmd_payload_t);

    // Initialize the GPR command allocation arguments
    args.heap_index    = GPR_HEAP_INDEX_DEFAULT;
    args.src_domain_id = GPR_IDS_DOMAIN_ID_ADSP_V;
    args.src_port      = GPR_INSTANCE_ID;
    args.dst_domain_id = dest_domain_id;
    args.dst_port      = dest_port;
    args.token         = token;
    args.opcode        = opcode;
    args.payload_size  = payload_size;
    args.client_data   = 0;
    args.ret_packet    = &pkt_ptr;
    // Allocate the GPR packet
    ar_result_t result = __gpr_cmd_alloc_ext_v2(&args);
    if (NULL == pkt_ptr)
    {
        // Error allocating packet
        AR_MSG(DBG_ERROR_PRIO,
               "Failed to allocate gpr packet opcode 0x%lX payload_size %lu result 0x%lx",
               opcode,
               args.payload_size,
               result);
    }

   apm_proxy_graph_mgmt_cmd_payload_t *payload_ptr = GPR_PKT_GET_PAYLOAD(apm_proxy_graph_mgmt_cmd_payload_t, pkt_ptr);
   payload_ptr->header.payload_address_lsw = 0;
   payload_ptr->header.payload_address_msw = 0;
   payload_ptr->header.mem_map_handle = 0;
   payload_ptr->header.payload_size = payload_size - sizeof(payload_ptr->header);

   payload_ptr->param.module_instance_id = module_iid;;
   payload_ptr->param.param_id = param_id;;
   payload_ptr->param.param_size = payload_size - sizeof(apm_proxy_graph_mgmt_cmd_payload_t);
   payload_ptr->param.error_code = 0;

   return pkt_ptr;
}


ar_result_t apm_proxy_graph_mgmt_send_start_stop(uint32_t num_subgraphs, uint32_t* sg_id_array, bool_t is_start) 
{
    ar_result_t rc = AR_EOK;

    // Allocate a GPR packet
    gpr_packet_t *gpr_packet_ptr = alloc_gpr_pkt(GPR_IDS_DOMAIN_ID_ADSP_V,
                                                  APM_MODULE_INSTANCE_ID,
                                                  (is_start)? start_token: stop_token, // client token
                                                  (is_start)? APM_CMD_GRAPH_START: APM_CMD_GRAPH_STOP,
                                                  1,
                                                  APM_PARAM_ID_SUB_GRAPH_LIST, 
                                                  (sizeof(uint32_t) * (num_subgraphs-1)) + sizeof(apm_param_id_sub_graph_list_t));
    if (NULL == gpr_packet_ptr)
    {
        // Error allocating packet
        return AR_EFAILED;
    }

   apm_proxy_graph_mgmt_cmd_payload_t *payload_header_ptr = 
               GPR_PKT_GET_PAYLOAD(apm_proxy_graph_mgmt_cmd_payload_t, gpr_packet_ptr);
   apm_proxy_graph_mgmt_state_payload_t *payload_ptr = (apm_proxy_graph_mgmt_state_payload_t*)(payload_header_ptr + 1);

   payload_ptr->sg_list.num_sub_graphs = num_subgraphs;
   for(int i = 0; i < num_subgraphs; i++)
   {
      payload_ptr->sg_id_array[i] = sg_id_array[i];
   }

    // Send the GPR packet
    rc = __gpr_cmd_async_send(gpr_packet_ptr);
    if(rc)
    {
        // Error sending packet
        __gpr_cmd_free(gpr_packet_ptr);
        AR_MSG(DBG_ERROR_PRIO, "Failed to send START/STOP, result: %lu", rc);
    }

    posal_channel_wait(apm_proxy_gpr_channel_ptr, 0x1);

    posal_signal_clear(apm_proxy_gpr_signal_ptr);

    return AR_EOK;
}