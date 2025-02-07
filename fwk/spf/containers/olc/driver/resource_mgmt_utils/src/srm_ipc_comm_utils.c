/**
 * \file srm_ipc_comm_utils.c
 * \brief
 *     This file contains Satellite Graph Management utility functions for handling the inter-processor communication.
 *  (commands/data)
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "sprm_i.h"
#include "offload_sp_api.h"
/* =======================================================================
Static Function Definitions
========================================================================== */

uint32_t sgm_get_src_port_id(spgm_info_t *spgm_info)
{
   uint32_t src_port_id = (uint32_t)spgm_info->sgm_id.cont_id;
   return src_port_id;
}

ar_result_t sgm_ipc_send_command(spgm_info_t *spgm_ptr)
{
   ar_result_t result = AR_EOK;

   uint32_t token = 0;

   if (NULL == spgm_ptr->active_cmd_hndl_ptr)
   {
      return AR_EBADPARAM;
   }

   token                                = posal_atomic_increment(spgm_ptr->token_instance);
   spgm_ptr->active_cmd_hndl_ptr->token = token;

   gpr_cmd_alloc_send_t args;
   args.src_domain_id = spgm_ptr->sgm_id.master_pd;
   args.dst_domain_id = spgm_ptr->sgm_id.sat_pd;
   args.src_port      = sgm_get_src_port_id(spgm_ptr);
   args.dst_port      = sgm_get_dst_port_id(spgm_ptr);
   args.token         = token; // todo:VB:make generic
   args.opcode        = spgm_ptr->active_cmd_hndl_ptr->opcode;
   args.payload       = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;
   args.payload_size  = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_size;
   args.client_data   = 0;

   if (AR_EOK != (result = __gpr_cmd_alloc_send(&args)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "gpr_gmc: failed to send the command with opcode[0x%lX]",
                  spgm_ptr->active_cmd_hndl_ptr->opcode);
   }

   return result;
}

ar_result_t sgm_ipc_send_command_to_dst_with_token(spgm_info_t *spgm_ptr, uint32_t dst_port, uint32_t token)
{
   ar_result_t result = AR_EOK;

   if (NULL == spgm_ptr->active_cmd_hndl_ptr)
   {
      return AR_EBADPARAM;
   }

   spgm_ptr->active_cmd_hndl_ptr->token = token;

   gpr_cmd_alloc_send_t args;
   args.src_domain_id = spgm_ptr->sgm_id.master_pd;
   args.dst_domain_id = spgm_ptr->sgm_id.sat_pd;
   args.src_port      = sgm_get_src_port_id(spgm_ptr);
   args.dst_port      = dst_port;
   args.token         = token;
   args.opcode        = spgm_ptr->active_cmd_hndl_ptr->opcode;
   args.payload       = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;
   args.payload_size  = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_size;
   args.client_data   = 0;

   if (AR_EOK != (result = __gpr_cmd_alloc_send(&args)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "gpr_gmc: failed to send the command with opcode[0x%lX]",
                  spgm_ptr->active_cmd_hndl_ptr->opcode);
   }

   return result;
}

ar_result_t sgm_ipc_send_command_to_dst(spgm_info_t *spgm_ptr, uint32_t dst_port)
{
   ar_result_t result = AR_EOK;
   uint32_t    token  = 0;

   if (NULL == spgm_ptr->active_cmd_hndl_ptr)
   {
      return AR_EBADPARAM;
   }

   token                                = posal_atomic_increment(spgm_ptr->token_instance);
   spgm_ptr->active_cmd_hndl_ptr->token = token;

   gpr_cmd_alloc_send_t args;
   args.src_domain_id = spgm_ptr->sgm_id.master_pd;
   args.dst_domain_id = spgm_ptr->sgm_id.sat_pd;
   args.src_port      = sgm_get_src_port_id(spgm_ptr);
   args.dst_port      = dst_port;
   args.token         = token; // todo:VB:make generic
   args.opcode        = spgm_ptr->active_cmd_hndl_ptr->opcode;
   args.payload       = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;
   args.payload_size  = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_size;
   args.client_data   = 0;

   if (AR_EOK != (result = __gpr_cmd_alloc_send(&args)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "gpr_gmc: failed to send the command with opcode[0x%lX]",
                  spgm_ptr->active_cmd_hndl_ptr->opcode);
   }

   return result;
}

ar_result_t sgm_ipc_send_data_pkt(spgm_info_t *spgm_ptr)
{
   ar_result_t result = AR_EOK;

   gpr_cmd_alloc_send_t args;
   args.src_domain_id = spgm_ptr->sgm_id.master_pd;
   args.dst_domain_id = spgm_ptr->sgm_id.sat_pd;
   args.src_port      = spgm_ptr->process_info.active_data_hndl.src_port;
   args.dst_port      = spgm_ptr->process_info.active_data_hndl.dst_port;
   args.token         = spgm_ptr->process_info.active_data_hndl.token;
   args.opcode        = spgm_ptr->process_info.active_data_hndl.opcode;
   args.payload       = spgm_ptr->process_info.active_data_hndl.payload_ptr;
   args.payload_size  = spgm_ptr->process_info.active_data_hndl.payload_size;
   args.client_data   = 0;

   if (AR_EOK != (result = __gpr_cmd_alloc_send(&args)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "gpr_gmc: failed to send the data command with opcode[0x%lX]",
                  spgm_ptr->process_info.active_data_hndl.opcode);
   }

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_LOW_PRIO,
               "gpr_gmc: send the data command with opcode[0x%lX]",
               spgm_ptr->process_info.active_data_hndl.opcode);

   return result;
}

uint32_t sdm_gpr_callback(gpr_packet_t *packet_ptr, void *callback_data)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                 log_id     = 0;
   uint32_t                 port_index = 0;
   spf_msg_t                msg;
   gpr_ibasic_rsp_result_t *rsp_ptr = NULL;

   VERIFY(result, (NULL != packet_ptr));

   //#ifdef VERBOSE_DEBUGGING
   AR_MSG(DBG_LOW_PRIO, "GPR callback for dst port 0x%lx pkt_ptr 0x%lx", packet_ptr->dst_port, packet_ptr);
   //#endif

   msg.payload_ptr = packet_ptr;
   msg.msg_opcode  = SPF_MSG_CMD_GPR;

   spgm_info_t *spgm_ptr = (spgm_info_t *)callback_data;

   /*Validate handles and queue pointers */
   VERIFY(result, (spgm_ptr && spgm_ptr->rsp_q_ptr && spgm_ptr->evnt_q_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   switch (cu_get_bits(packet_ptr->opcode, AR_GUID_TYPE_MASK, AR_GUID_TYPE_SHIFT))
   {
      case AR_GUID_TYPE_CONTROL_CMD_RSP:
      {
         switch (packet_ptr->opcode)
         {
            case GPR_IBASIC_RSP_RESULT:
            {
               rsp_ptr = (gpr_ibasic_rsp_result_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
               VERIFY(result, (NULL != rsp_ptr));
               switch (rsp_ptr->opcode)
               {
                  case APM_CMD_SET_CFG:
                  {
                     if (NULL != packet_ptr)
                     {
                        __gpr_cmd_free(packet_ptr);
                     }
                     break;
                  }
                  default:
                  {
                     /** control commands */
                     TRY(result,
                         (ar_result_t)posal_queue_push_back(spgm_ptr->rsp_q_ptr, (posal_queue_element_t *)&msg));
                  }
               }
               break;
            }
            default:
            {
               /** control commands */
               TRY(result, (ar_result_t)posal_queue_push_back(spgm_ptr->rsp_q_ptr, (posal_queue_element_t *)&msg));
            }
         }
         break;
      }
      case AR_GUID_TYPE_CONTROL_EVENT:
      {
         switch (packet_ptr->opcode)
         {
            case OFFLOAD_EVENT_ID_UPSTREAM_STATE:
            case OFFLOAD_EVENT_ID_DOWNSTREAM_PEER_PORT_PROPERTY:
            case OFFLOAD_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY:
            {
               OLC_SGM_MSG(OLC_SGM_ID,
                           DBG_ERROR_PRIO,
                           "gpr: data callback handler, invalid command handling for opcode 0x%lX",
                           packet_ptr->opcode);
               THROW(result, AR_EFAILED); // packet would be freed in catch
               break;
            }
            default:
            {
               OLC_SGM_MSG(OLC_SGM_ID,
                           DBG_ERROR_PRIO,
                           "gpr: data callback handler, invalid guid 0x%lX",
                           packet_ptr->opcode);
               THROW(result, AR_EFAILED); // packet would be freed in catch
            }
         }
         break;
      }
      case AR_GUID_TYPE_DATA_CMD_RSP:
      {
         switch (packet_ptr->opcode)
         {
            case DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2:
            {

               TRY(result,
                   sgm_get_data_port_index_given_rw_client_miid(spgm_ptr,
                                                                 IPC_WRITE_DATA,
                                                                 packet_ptr->dst_port,
                                                                 &port_index));

               TRY(result,
                   (ar_result_t)
                      posal_queue_push_back(spgm_ptr->process_info.wdp_obj_ptr[port_index]->port_info.this_handle.q_ptr,
                                            (posal_queue_element_t *)&msg));
            }
            break;
            case DATA_CMD_RSP_RD_SH_MEM_EP_DATA_BUFFER_DONE_V2:
            {

               TRY(result,
                   sgm_get_data_port_index_given_rw_client_miid(spgm_ptr,
                                                                 IPC_READ_DATA,
                                                                 packet_ptr->dst_port,
                                                                 &port_index));
               TRY(result,
                   (ar_result_t)
                      posal_queue_push_back(spgm_ptr->process_info.rdp_obj_ptr[port_index]->port_info.this_handle.q_ptr,
                                            (posal_queue_element_t *)&msg));
            }
            break;

         }

         break;
      }
      case AR_GUID_TYPE_DATA_EVENT:
      {
         switch (packet_ptr->opcode)
         {
            case DATA_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT:
            case OFFLOAD_DATA_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY:
            {
               TRY(result,
                   sgm_get_data_port_index_given_rw_ep_miid(spgm_ptr,
                                                             IPC_READ_DATA,
                                                             packet_ptr->src_port,
                                                             &port_index));
               TRY(result,
                   (ar_result_t)
                      posal_queue_push_back(spgm_ptr->process_info.rdp_obj_ptr[port_index]->port_info.this_handle.q_ptr,
                                            (posal_queue_element_t *)&msg));
               break;
            }
            default:
            {
               THROW(result, AR_EFAILED); // packet would be freed in catch
            }
         }
         break;
      }
      default:
      {
         OLC_SGM_MSG(OLC_SGM_ID, DBG_ERROR_PRIO, "gpr: data callback handler, invalid guid 0x%lX", packet_ptr->opcode);
         THROW(result, AR_EFAILED);
      }
   }

   CATCH(result, "OLC CNTR ID DATA MGMT CMD FAILED: 0x%lx", log_id)
   {
      __gpr_cmd_end_command(packet_ptr, result);
   }

   return result;
}

uint32_t sgm_gpr_callback(gpr_packet_t *packet_ptr, void *callback_data)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;

   //#ifdef VERBOSE_DEBUGGING
   AR_MSG(DBG_LOW_PRIO,
          "GPR callback for src domain id %lu, dst domain id %lu "
          "src port 0x%lx dst port 0x%lx pkt_ptr 0x%lx",
          packet_ptr->src_domain_id,
          packet_ptr->dst_domain_id,
          packet_ptr->src_port,
          packet_ptr->dst_port,
          packet_ptr);
   //#endif

   spf_msg_t msg;
   msg.payload_ptr = packet_ptr;
   msg.msg_opcode  = SPF_MSG_CMD_SATELLITE_GPR;

   spgm_info_t *spgm_ptr = (spgm_info_t *)callback_data;

   /*Validate handles and queue pointers */
   VERIFY(result, (spgm_ptr && spgm_ptr->rsp_q_ptr && spgm_ptr->evnt_q_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   if (packet_ptr->src_domain_id != spgm_ptr->sgm_id.sat_pd)
   {
      msg.msg_opcode = SPF_MSG_CMD_GPR;
      TRY(result,
          (ar_result_t)posal_queue_push_back(spgm_ptr->cu_ptr->cmd_handle.cmd_q_ptr, (posal_queue_element_t *)&msg));
		   return result;
   }

   switch (cu_get_bits(packet_ptr->opcode, AR_GUID_TYPE_MASK, AR_GUID_TYPE_SHIFT))
   {
      case AR_GUID_TYPE_CONTROL_CMD_RSP:
      {
         /** control commands */
         TRY(result, (ar_result_t)posal_queue_push_back(spgm_ptr->rsp_q_ptr, (posal_queue_element_t *)&msg));
         break;
      }
      case AR_GUID_TYPE_CONTROL_EVENT:
      {
         /** control commands */
         switch (packet_ptr->opcode)
         {
            case OFFLOAD_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY:
            case OFFLOAD_EVENT_ID_DOWNSTREAM_PEER_PORT_PROPERTY:
            case OFFLOAD_EVENT_ID_UPSTREAM_STATE:
            {
               TRY(result, (ar_result_t)posal_queue_push_back(spgm_ptr->cu_ptr->cmd_handle.cmd_q_ptr, (posal_queue_element_t *)&msg));
            }
            break;
            case APM_EVENT_MODULE_TO_CLIENT:
            case OFFLOAD_EVENT_ID_SH_MEM_EP_OPERATING_FRAME_SIZE:
            case OFFLOAD_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT:
            case EVENT_ID_MODULE_CMN_METADATA_CLONE_MD:
            case EVENT_ID_MODULE_CMN_METADATA_TRACKING_EVENT:
            {
               TRY(result, (ar_result_t)posal_queue_push_back(spgm_ptr->evnt_q_ptr, (posal_queue_element_t *)&msg));
            }
            break;
            default:
            {
               OLC_SGM_MSG(OLC_SGM_ID,
                           DBG_ERROR_PRIO,
                           "gpr_gmc: command callback handler, invalid guid 0x%lX",
                           packet_ptr->opcode);
               THROW(result, AR_EFAILED);
            }
            break;
         }
         break;
      }
      case AR_GUID_TYPE_DATA_CMD_RSP:
      {
         switch (packet_ptr->opcode)
         {
            default:
            {
               OLC_SGM_MSG(OLC_SGM_ID,
                           DBG_ERROR_PRIO,
                           "gpr_gmc: command callback handler, invalid guid 0x%lX",
                           packet_ptr->opcode);
               THROW(result, AR_EFAILED);
            }
         }
         break;
      }
      default:
      {
         OLC_SGM_MSG(OLC_SGM_ID,
                     DBG_ERROR_PRIO,
                     "gpr_gmc: command callback handler, invalid guid 0x%lX",
                     packet_ptr->opcode);
         THROW(result, AR_EFAILED);
      }
   }

   CATCH(result, "OLC CNTR ID GRAPH MGMT CMD FAILED: 0x%lx", log_id)
   {
      __gpr_cmd_end_command(packet_ptr, result);
   }

   return result;
}

ar_result_t sgm_deregister_satellite_module_with_gpr(spgm_info_t *spgm_ptr, uint32_t sub_graph_id)
{
   ar_result_t        result = AR_EOK;
   spf_list_node_t *  curr_node_ptr;
   sgm_module_info_t *module_node_ptr;
   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = spgm_ptr->gu_graph_info.satellite_module_list_ptr;

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      module_node_ptr = (sgm_module_info_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == module_node_ptr)
      {
         break;
      }

      if ((sub_graph_id == module_node_ptr->sub_graph_id) && (TRUE == module_node_ptr->is_registered_with_gpr))
      {
         result = __gpr_cmd_deregister(module_node_ptr->instance_id);
         if (AR_EOK != result)
         {
            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_ERROR_PRIO,
                        "close:failed to deregister the module iid(0x%08lx) with GPR, result %lu",
                        module_node_ptr->instance_id,
                        result);
         }
         else
         {
            module_node_ptr->is_registered_with_gpr = FALSE;
         }
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return result;
}

ar_result_t sgm_register_satellite_module_with_gpr(spgm_info_t *spgm_ptr, spf_handle_t *spf_handle)
{
   ar_result_t        result          = AR_EOK;
   spf_list_node_t *  curr_node_ptr   = NULL;
   sgm_module_info_t *module_node_ptr = NULL;
   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = spgm_ptr->gu_graph_info.satellite_module_list_ptr;

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      module_node_ptr = (sgm_module_info_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == module_node_ptr)
      {
         break;
      }

      if (FALSE == module_node_ptr->is_registered_with_gpr)
      {
         result = __gpr_cmd_register(module_node_ptr->instance_id, cu_gpr_callback, spf_handle);
         if (AR_EOK != result)
         {
            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_ERROR_PRIO,
                        "open:failed to register the module iid(0x%08lx) with GPR, result %lu",
                        module_node_ptr->instance_id,
                        result);
         }
         else
         {
            module_node_ptr->is_registered_with_gpr = TRUE;
         }
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return result;
}

ar_result_t sgm_deregister_olc_module_with_gpr(spgm_info_t *spgm_ptr, uint32_t sub_graph_id)
{
   ar_result_t        result = AR_EOK;
   spf_list_node_t *  curr_node_ptr;
   sgm_module_info_t *module_node_ptr;
   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = spgm_ptr->gu_graph_info.olc_module_list_ptr;

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      module_node_ptr = (sgm_module_info_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == module_node_ptr)
      {
         break;
      }

      if ((sub_graph_id == module_node_ptr->sub_graph_id) && (TRUE == module_node_ptr->is_registered_with_gpr))
      {
         result = __gpr_cmd_deregister(module_node_ptr->instance_id);
         if (AR_EOK != result)
         {
            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_ERROR_PRIO,
                        "close:failed to deregister the module iid(0x%08lx) with GPR, result %lu",
                        module_node_ptr->instance_id,
                        result);
         }
         else
         {
            module_node_ptr->is_registered_with_gpr = FALSE;
         }
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return result;
}

ar_result_t sgm_register_olc_module_with_gpr(spgm_info_t *spgm_ptr)
{
   ar_result_t        result = AR_EOK;
   spf_list_node_t *  curr_node_ptr;
   sgm_module_info_t *module_node_ptr;
   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = spgm_ptr->gu_graph_info.olc_module_list_ptr;

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      module_node_ptr = (sgm_module_info_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == module_node_ptr)
      {
         break;
      }
      if (FALSE == module_node_ptr->is_registered_with_gpr)
      {
         result = __gpr_cmd_register(module_node_ptr->instance_id, sdm_gpr_callback, &spgm_ptr->spf_handle);
         if (AR_EOK != result)
         {
            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_ERROR_PRIO,
                        "open:failed to register the module iid(0x%08lx) with GPR, result %lu",
                        module_node_ptr->instance_id,
                        result);
         }
         else
         {
            module_node_ptr->is_registered_with_gpr = TRUE;
         }
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return result;
}
