/**
 * \file sgm_rsp_handler.c
 * \brief
 *     This file contains command response handling functions for Satellite Graph Management
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "sgm_i.h"

/* =======================================================================
Static Function Definitions
========================================================================== */

/* Function to handle the responses for the Graph commands sent to Satellite Graphs*/
static ar_result_t spgm_cmd_rsp_handler(cu_base_t *cu_ptr, spgm_info_t *spgm_ptr)
{

   ar_result_t          result          = AR_EOK;
   ar_result_t          cmd_resp_result = AR_EOK;
   uint32_t             free_cmd_handle = TRUE;
   uint32_t             log_id          = 0;
   spgm_cmd_rsp_node_t *rsp_info        = NULL;
   gpr_packet_t *       packet_ptr      = NULL;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   rsp_info   = &spgm_ptr->rsp_info;

   VERIFY(result, (NULL != cu_ptr));
   VERIFY(result, (NULL != spgm_ptr->cmd_rsp_vtbl));

   // POP the message from the queue.
   TRY(result, posal_queue_pop_front(spgm_ptr->rsp_q_ptr, (posal_queue_element_t *)&(spgm_ptr->rsp_msg)));

   packet_ptr = (gpr_packet_t *)spgm_ptr->rsp_msg.payload_ptr;
   VERIFY(result, (NULL != packet_ptr));

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_HIGH_PRIO,
               "cmd_rsp: rcvd from satellite with opcode(0x%lX) token(0x%lX) pkt_ptr(0x%lX)",
               packet_ptr->opcode,
               packet_ptr->token,
               packet_ptr);

   switch (packet_ptr->opcode)
   {
      case GPR_IBASIC_RSP_RESULT: //basic response
      {
         gpr_ibasic_rsp_result_t *rsp_ptr = (gpr_ibasic_rsp_result_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
         VERIFY(result, (NULL != rsp_ptr));

         rsp_info->rsp_result = rsp_ptr->status;
         rsp_info->opcode     = rsp_ptr->opcode;
         rsp_info->token      = packet_ptr->token;
         sgm_get_cache_cmd_msg(spgm_ptr, rsp_info->opcode, rsp_info->token, &rsp_info->cmd_msg);

         OLC_SGM_MSG(OLC_SGM_ID,
                     DBG_HIGH_PRIO,
                     "cmd_rsp: processing ibasic response with cmd_opcode(0x%lX) "
                     "token(0x%lX) ptime %lu",
                     rsp_ptr->opcode,
                     packet_ptr->token,
                     spgm_ptr->p_cmd_exec_ts);

         switch (rsp_ptr->opcode)
         {
            case APM_CMD_GRAPH_OPEN:
            {
               TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_open_rsp_h(cu_ptr, rsp_info));
               break;
            }
            case APM_CMD_GRAPH_CLOSE:
            {
               // if you TRY, AR_ETERMINATED will raise an exception.
               result = spgm_ptr->cmd_rsp_vtbl->graph_close_rsp_h(cu_ptr, rsp_info);

               // Destroy the command handle for the specified token as we return from here
               if ((TRUE == free_cmd_handle) && (NULL != spgm_ptr))
               {
                  sgm_destroy_cmd_handle(spgm_ptr, rsp_info->opcode, rsp_info->token);
               }
               // free the packet
               __gpr_cmd_free(packet_ptr);

               return result;
            }
            case APM_CMD_GRAPH_PREPARE:
            {
               TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_prepare_rsp_h(cu_ptr, rsp_info));
               break;
            }
            case APM_CMD_GRAPH_START:
            {
               TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_start_rsp_h(cu_ptr, rsp_info));
               break;
            }
            case APM_CMD_GRAPH_FLUSH:
            {
               TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_flush_rsp_h(cu_ptr, rsp_info));
               break;
            }
            case APM_CMD_GRAPH_SUSPEND:
            {
               TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_suspend_rsp_h(cu_ptr, rsp_info));
               break;
            }
            case APM_CMD_GRAPH_STOP:
            {
               TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_stop_rsp_h(cu_ptr, rsp_info));
               break;
            }
            case APM_CMD_GET_CFG:
            {
               // If the code is APM_CMD_GET_CFG, it indicates the failure of get param.
               // The response result is marked as failure
               if (0 != rsp_info->sec_opcode)
               {
                  rsp_info->rsp_result = AR_EFAILED;
               }
               if (APM_MODULE_INSTANCE_ID != packet_ptr->src_port) // response came from module
               {
                  // Handling the response to the case where get configuration is sent to the module instance.
                  TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_set_get_cfg_packed_rsp_h(cu_ptr, rsp_info, NULL));
               }
               else
               {
                  // Handling the response to the case where get configuration is sent to the APM module instance.
                  spgm_cmd_hndl_node_t *cmd_hndl_node_ptr = NULL;
                  result = sgm_get_active_cmd_hndl(spgm_ptr, rsp_info->opcode, rsp_info->token, &cmd_hndl_node_ptr);
                  rsp_info->sec_opcode    = cmd_hndl_node_ptr->sec_opcode;
                  rsp_info->cmd_extn_info = cmd_hndl_node_ptr->cmd_extn_info;
                  TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_set_get_cfg_rsp_h(cu_ptr, rsp_info));
               }
               break;
            }

            case APM_CMD_SET_CFG:
            {
               if (APM_MODULE_INSTANCE_ID != packet_ptr->src_port)
               {
                  // Handling the response to the case where Set configuration is sent to the module instance.
                  TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_set_get_cfg_packed_rsp_h(cu_ptr, rsp_info, NULL));
               }
               else
               {
                  // Handling the response to the case where Set configuration is sent to the APM module instance.
                  spgm_cmd_hndl_node_t *cmd_hndl_node_ptr = NULL;
                  result = sgm_get_active_cmd_hndl(spgm_ptr, rsp_info->opcode, rsp_info->token, &cmd_hndl_node_ptr);
                  rsp_info->sec_opcode    = cmd_hndl_node_ptr->sec_opcode;
                  rsp_info->cmd_extn_info = cmd_hndl_node_ptr->cmd_extn_info;
                  TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_set_get_cfg_rsp_h(cu_ptr, rsp_info));
               }
               break;
            }

            case APM_CMD_REGISTER_CFG:
            case APM_CMD_REGISTER_SHARED_CFG:
            case APM_CMD_DEREGISTER_CFG:
            case APM_CMD_DEREGISTER_SHARED_CFG: //OLC_CA : check the packed
            {
               spgm_cmd_hndl_node_t *cmd_hndl_node_ptr = NULL;
               bool_t                send_response     = TRUE;
               result = sgm_get_active_cmd_hndl(spgm_ptr, rsp_info->opcode, rsp_info->token, &cmd_hndl_node_ptr);
               if (cmd_hndl_node_ptr->multi_max_resp_cnt)
               {
                  cmd_hndl_node_ptr->multi_rsp_cnt++;
                  if (cmd_hndl_node_ptr->multi_rsp_cnt < cmd_hndl_node_ptr->multi_max_resp_cnt)
                  {
                     send_response   = FALSE;
                     free_cmd_handle = FALSE;
                  }
				  else
				  {
					rsp_info->sec_opcode    = cmd_hndl_node_ptr->sec_opcode;
					rsp_info->cmd_extn_info = cmd_hndl_node_ptr->cmd_extn_info;					  
				  }
               }

               if (TRUE == send_response)
               {
                  if (APM_MODULE_INSTANCE_ID != packet_ptr->src_port)
                  {
                     // Handling the response to the case where register/de-register
                     // configuration is sent to the module instance.
                     TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_set_persistent_rsp_h(cu_ptr, rsp_info));
                  }
                  else
                  {
                     // Handling the response to the case where register/de-register
                     // configuration is sent to the APM module instance.
                     TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_set_persistent_packed_rsp_h(cu_ptr, rsp_info));
                  }
               }
               break;
            }

            case APM_CMD_REGISTER_MODULE_EVENTS:
            {
               free_cmd_handle = FALSE;
               // The token is zero for event sent from OLC.
               // There is no better way to identify if the event is sent by HLOS or by the OLC.
               // assumption : If the command is sent by HLOS, the token is always non-zero.
               if (0 != packet_ptr->token)
               {
                  free_cmd_handle = TRUE;
                  TRY(result, spgm_ptr->cmd_rsp_vtbl->graph_event_reg_rsp_h(cu_ptr, rsp_info));
               }
               break;
            }

            default:
            {
               free_cmd_handle = FALSE;
               break;
            }
         }
         __gpr_cmd_free(packet_ptr); // free packet for basic response
      }
      break;

      case APM_CMD_RSP_GET_CFG:
      {
         rsp_info->rsp_result = AR_EOK;
         rsp_info->opcode     = packet_ptr->opcode;
         rsp_info->token      = packet_ptr->token;
         sgm_get_cache_cmd_msg(spgm_ptr, rsp_info->opcode, rsp_info->token, &rsp_info->cmd_msg);

         OLC_SGM_MSG(OLC_SGM_ID,
                     DBG_HIGH_PRIO,
                     "cmd_rsp: processing GET CFG response with "
                     "cmd_opcode(0x%lX) token(0x%lX) ptime %lu",
                     rsp_info->opcode,
                     packet_ptr->token,
					 spgm_ptr->p_cmd_exec_ts);

         if (APM_MODULE_INSTANCE_ID != packet_ptr->src_port)
         {
            // Handling the response to the case where get configuration is sent to the module instance.
            result = spgm_ptr->cmd_rsp_vtbl->graph_set_get_cfg_packed_rsp_h(cu_ptr, rsp_info, packet_ptr);
         }
         else
         {
            // Handling the response to the case where get configuration is sent to the APM module instance.
            spgm_cmd_hndl_node_t *cmd_hndl_node_ptr = NULL;
            result = sgm_get_active_cmd_hndl(spgm_ptr, rsp_info->opcode, rsp_info->token, &cmd_hndl_node_ptr);
            rsp_info->sec_opcode    = cmd_hndl_node_ptr->sec_opcode;
            rsp_info->cmd_extn_info = cmd_hndl_node_ptr->cmd_extn_info;
            result                  = spgm_ptr->cmd_rsp_vtbl->graph_set_get_cfg_rsp_h(cu_ptr, rsp_info);
         }
         __gpr_cmd_free(packet_ptr); // free packet
         break;
      }

      default:
      {
         OLC_SGM_MSG(OLC_SGM_ID,
                     DBG_ERROR_PRIO,
                     "cmd_rsp: unsupported response with pkt opcode(%lX) token(%lx) ",
                     packet_ptr->opcode,
                     packet_ptr->token);

         cmd_resp_result = AR_EUNSUPPORTED;
         __gpr_cmd_end_command(packet_ptr, cmd_resp_result);
      }
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
	   __gpr_cmd_free(packet_ptr); // free packet
   }

   // Destroy the command handle for the specified token
   if ((TRUE == free_cmd_handle) && (NULL != spgm_ptr))
   {
      sgm_destroy_cmd_handle(spgm_ptr, rsp_info->opcode, rsp_info->token);
   }
   return result;
}

/* Function to handle the responses for the Graph commands sent to Satellite PD
 * The response handler functions are defined in the OLC core and
 * function handler is shared to the SGM
 */
ar_result_t sgm_cmd_rsp_handler(cu_base_t *cu_ptr, uint32_t channel_bit_index)
{
   ar_result_t result = AR_EOK;
   if (NULL == cu_ptr)
   {
      return AR_EUNEXPECTED;
   }
   uint8_t *    base_ptr = (uint8_t *)cu_ptr;
   spgm_info_t *spgm_ptr = (spgm_info_t *)(base_ptr + sizeof(cu_base_t));

   if (AR_EOK != (result = spgm_cmd_rsp_handler(cu_ptr, spgm_ptr)))
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_ERROR_PRIO, "cmd_rsp: response handling failed, ignore for Close command");
   }

   return result;
}

/* Function to fill the set-get configuration command response from the
 * satellite graph in the client payload.
 * This function is used when calibration command is sent by Client to APM and
 * APM sends to the containers
 */

ar_result_t sgm_set_get_cfg_rsp_update(spgm_info_t *                 spgm_ptr,
                                       spf_msg_cmd_param_data_cfg_t *set_get_cfg_payload_ptr,
                                       uint32_t                      token)
{
   ar_result_t result                  = AR_EOK;
   uint32_t    arr_indx                = 0;
   uint32_t    size_per_param_data_cfg = 0;
   uint32_t    offset                  = 0;
   uint32_t    num_param_id_cfg        = 0;
   uint32_t    log_id                  = 0;
   uint32_t    dst_orig_param_size     = 0;
   bool_t      get_cmd_node            = FALSE;
   INIT_EXCEPTION_HANDLING

   uint8_t *                set_get_rsp_data_ptr = NULL;
   spgm_cmd_hndl_node_t *   active_cmd_hndl_ptr  = NULL;
   apm_module_param_data_t *param_data_ptr       = NULL;
   apm_module_param_data_t *dest_param_data_ptr  = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != set_get_cfg_payload_ptr));

   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "cmd_rsp: set_get cfg rsph, start parsing response payload");

   // get the active command handle given the token. Token is supposed to be unique
   get_cmd_node = olc_get_cmd_hndl_node(spgm_ptr->cmd_hndl_list.cmd_hndl_list_ptr,
                                        spgm_ptr->cmd_hndl_list.num_active_cmd_hndls,
                                        &active_cmd_hndl_ptr,
                                        token);

   if ((TRUE == get_cmd_node) && (NULL != active_cmd_hndl_ptr))
   {
      if (NULL != active_cmd_hndl_ptr->shm_info.shm_mem_ptr) // OOB handling
      {
         set_get_rsp_data_ptr = (uint8_t *)active_cmd_hndl_ptr->shm_info.shm_mem_ptr;

         // cache invalidate the response from the satellite graph
         TRY(result,
             posal_cache_invalidate((uint32_t)set_get_rsp_data_ptr, active_cmd_hndl_ptr->shm_info.shm_alloc_size));

         // copy the payload from mdf shared memory to the client payload
         num_param_id_cfg = set_get_cfg_payload_ptr->num_param_id_cfg;

         OLC_SGM_MSG(OLC_SGM_ID, DBG_MED_PRIO, "cmd_rsp: set_get cfg rsph, num_param_id = %lu", num_param_id_cfg);

         for (arr_indx = 0; arr_indx < num_param_id_cfg; arr_indx++)
         {
            if (NULL != set_get_cfg_payload_ptr->param_data_pptr[arr_indx])
            {
               param_data_ptr      = (apm_module_param_data_t *)(set_get_rsp_data_ptr + offset);
               dest_param_data_ptr = (apm_module_param_data_t *)(set_get_cfg_payload_ptr->param_data_pptr[arr_indx]);
               size_per_param_data_cfg = sizeof(apm_module_param_data_t);
               size_per_param_data_cfg += param_data_ptr->param_size;
               dst_orig_param_size = sizeof(apm_module_param_data_t);
               dst_orig_param_size += dest_param_data_ptr->param_size;
               // if the size is wrong or parsing is wrong, we might start to corrupt the memory.
               // below meessage would give some details in such case
               memscpy(set_get_cfg_payload_ptr->param_data_pptr[arr_indx],
                       dst_orig_param_size,
                       param_data_ptr,
                       size_per_param_data_cfg);
               offset += ALIGN_8_BYTES(dst_orig_param_size);

#if 0
               uint32_t *temp_print_ptr = (uint32_t *)(param_data_ptr);
               uint32_t *temp_dst_print_ptr = (uint32_t *)(dest_param_data_ptr);
               for (uint32_t print_cnt = 0; print_cnt < size_per_param_data_cfg/4; print_cnt++)
               {
                  OLC_SGM_MSG(OLC_SGM_ID,
                              DBG_MED_PRIO,
                              "cmd_rsp: set_get cfg rsph, cnt %lu value 0x%lx value_in_dst 0x%lx",
                              print_cnt,
                              temp_print_ptr[print_cnt],
                              temp_dst_print_ptr[print_cnt]);
               }

#endif

               OLC_SGM_MSG(OLC_SGM_ID,
                           DBG_MED_PRIO,
                           "cmd_rsp: set_get cfg rsph, param_index %lu, param size to copy %lu, src_mem 0x%lX dst mem "
                           "0x%lX",
                           arr_indx,
                           size_per_param_data_cfg,
                           param_data_ptr,
                           set_get_cfg_payload_ptr->param_data_pptr[arr_indx]);
            }
            else
            {
               OLC_SGM_MSG(OLC_SGM_ID, DBG_ERROR_PRIO, "cmd_rsp: set_get cfg rsph, invalid destination memory pointer");
               return AR_EBADPARAM;
            }
         }
         // there is a need to flush the client memory here. APM will do if needed
      }
      else
      {
         // Configuration from APM is always supposed to OOB. (i.e., processed as OOB)
         // The OLC container is not aware if the original calibration command was in-band or out-band
         OLC_SGM_MSG(OLC_SGM_ID,
                     DBG_ERROR_PRIO,
                     "cmd_rsp: set_get cfg rsph, active command handler has no valid shared memory pointer, "
                     "unexpected");
         return AR_EUNEXPECTED;
      }
   }
   else
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "cmd_rsp: set_get cfg rsph, failed to find active command handler for the given token");
      result = AR_EFAILED;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to fill the set-get configuration command response from the
 * satellite graph in the client payload.
 * This function is used when calibration command is sent by Client
 * to the module through GPR.
 */
ar_result_t sgm_set_get_packed_cfg_rsp_update(spgm_info_t *spgm_ptr, uint32_t token, uint32_t opcode, void *sat_rsp_packet_ptr)
{
   ar_result_t result       = AR_EOK;
   uint32_t    log_id       = 0;
   uint32_t    copy_size    = 0;
   bool_t      get_cmd_node = FALSE;
   INIT_EXCEPTION_HANDLING

   uint8_t *             set_packed_rsp_data_ptr = NULL;
   spgm_cmd_hndl_node_t *active_cmd_hndl_ptr     = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "cmd_rsp: set_get_packed cfg rsph, start parsing response payload");

   // get the active command handle given the token. Token is supposed to be unique
   get_cmd_node = olc_get_cmd_hndl_node(spgm_ptr->cmd_hndl_list.cmd_hndl_list_ptr,
                                        spgm_ptr->cmd_hndl_list.num_active_cmd_hndls,
                                        &active_cmd_hndl_ptr,
                                        token);

   if ((TRUE == get_cmd_node) && (NULL != active_cmd_hndl_ptr))
   {
      // if the shared memory is valid, then it indicates that the payload was sent as OOB
      if (NULL != active_cmd_hndl_ptr->shm_info.shm_mem_ptr)
      {
         set_packed_rsp_data_ptr = (uint8_t *)active_cmd_hndl_ptr->shm_info.shm_mem_ptr;
         VERIFY(result, (NULL != active_cmd_hndl_ptr->rsp_payload_ptr));

         OLC_SGM_MSG(OLC_SGM_ID, DBG_MED_PRIO, "cmd_rsp: set_get_packed cfg rsph, payload is OOB");

         // invalidate the response from the satellite graph
         if (AR_EOK != (result = posal_cache_invalidate((uint32_t)set_packed_rsp_data_ptr,
                                                        active_cmd_hndl_ptr->shm_info.shm_alloc_size)))
         {
            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_ERROR_PRIO,
                        "cmd_rsp: failed to cache invalidate the set-get config cmd response data");
            return (AR_EPANIC | result);
         }

         // copy the payload from mdf shared memory to the client payload
         copy_size = memscpy(active_cmd_hndl_ptr->rsp_payload_ptr,
                             active_cmd_hndl_ptr->rsp_payload_size,
                             set_packed_rsp_data_ptr,
                             active_cmd_hndl_ptr->shm_info.shm_alloc_size);

         // Flush the client payload memory
         if (AR_EOK != (result = posal_cache_flush((uint32_t)active_cmd_hndl_ptr->rsp_payload_ptr, copy_size)))
         {
            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_ERROR_PRIO,
                        "cmd_rsp: failed to cache flush the client memory during set-get packed");
            return (AR_EPANIC | result);
         }

         if (opcode == APM_CMD_RSP_GET_CFG)
         {
            apm_cmd_rsp_get_cfg_t cmd_get_cfg_rsp = { 0 };
            cmd_get_cfg_rsp.status                = result;
            gpr_packet_t *client_cmd_packet_ptr   = (gpr_packet_t *)active_cmd_hndl_ptr->cmd_msg.payload_ptr;

            gpr_cmd_alloc_send_t args;
            args.src_domain_id = client_cmd_packet_ptr->dst_domain_id;
            args.dst_domain_id = client_cmd_packet_ptr->src_domain_id;
            args.src_port      = client_cmd_packet_ptr->dst_port;
            args.dst_port      = client_cmd_packet_ptr->src_port;
            args.token         = client_cmd_packet_ptr->token;
            args.opcode        = APM_CMD_RSP_GET_CFG;
            args.payload       = &cmd_get_cfg_rsp;
            args.payload_size  = sizeof(apm_cmd_rsp_get_cfg_t);
            args.client_data   = 0;
            TRY(result, __gpr_cmd_alloc_send(&args));
         }
      }
      else // inband
      {
         if (opcode == APM_CMD_RSP_GET_CFG)
         {
            if ((NULL != sat_rsp_packet_ptr) && (NULL != active_cmd_hndl_ptr))
            {
               gpr_packet_t *sat_rsp_pkt_ptr       = (gpr_packet_t *)sat_rsp_packet_ptr;
               gpr_packet_t *client_cmd_packet_ptr = (gpr_packet_t *)active_cmd_hndl_ptr->cmd_msg.payload_ptr;
               uint32_t      payload_size          = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(sat_rsp_pkt_ptr->header);
               void *        payload_ptr           = GPR_PKT_GET_PAYLOAD(void, sat_rsp_pkt_ptr);

               OLC_SGM_MSG(OLC_SGM_ID,
                           DBG_ERROR_PRIO,
                           "cmd_rsp: processing the inband getcfg size 0x%lx token %lu",
                           payload_size,
                           client_cmd_packet_ptr->token);

               gpr_cmd_alloc_send_t args;
               args.src_domain_id = client_cmd_packet_ptr->dst_domain_id;
               args.dst_domain_id = client_cmd_packet_ptr->src_domain_id;
               args.src_port      = client_cmd_packet_ptr->dst_port;
               args.dst_port      = client_cmd_packet_ptr->src_port;
               args.token         = client_cmd_packet_ptr->token;
               args.payload_size  = payload_size;
               args.opcode        = APM_CMD_RSP_GET_CFG;
               args.client_data   = 0;
               args.payload       = payload_ptr;

               if (AR_EOK != (result = __gpr_cmd_alloc_send(&args)))
               {
                  OLC_SGM_MSG(OLC_SGM_ID,
                              DBG_ERROR_PRIO,
                              "gpr_gmc: failed to send the command the GetCfg response opcode 0x%lx token %lu",
                              active_cmd_hndl_ptr->opcode,
                              active_cmd_hndl_ptr->token);
                  return AR_EUNEXPECTED;
               }

               return AR_EOK;
            }
            else
            {
               OLC_SGM_MSG(OLC_SGM_ID,
                           DBG_ERROR_PRIO,
                           "gpr_gmc: failed to send GetCfg response opcode 0x%lx token %lu, sat_rsp pkt is NULL",
                           opcode,
                           token);
               return AR_EUNEXPECTED;
            }
         }
         else if (opcode == APM_CMD_SET_CFG)
         {
            // if the payload is inband for SET_CFG and if we are coming here, The response is EOK
            return AR_EOK;
         }
         else if (opcode == APM_CMD_GET_CFG)
         {
            // if the payload is inband for GET_CFG and if we are coming here, The response is AR_EFAILED
            return AR_EFAILED;
         }
         else
         {
            OLC_SGM_MSG(OLC_SGM_ID, DBG_ERROR_PRIO, "cmd_rsp: set_get_packed cfg rsph, invalid opcode %lu", opcode);
            return AR_EUNEXPECTED;
         }
      }
   }
   else
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "cmd_rsp: set_get_packed cfg rsph, failed to get command node %lu",
                  opcode);
      result = AR_EFAILED;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to fill the set-get configuration command response from the
 * satellite graph in the client payload.
 * This function is used when calibration command is sent to the specifically to
 *  the container (not modules) from APM. (like path delay)
 */

ar_result_t sgm_cntr_set_cfg_rsp_update(spgm_info_t *spgm_ptr, uint32_t token)
{
   ar_result_t result       = AR_EOK;
   uint32_t    log_id       = 0;
   bool_t      get_cmd_node = FALSE;
   INIT_EXCEPTION_HANDLING

   uint8_t *                set_get_rsp_data_ptr = NULL;
   spgm_cmd_hndl_node_t *   active_cmd_hndl_ptr  = NULL;
   apm_module_param_data_t *param_data_ptr       = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "cmd_rsp: cntr_set_get_cfg rsph, start parsing response payload");

   // get the active command handle given the token. Token is supposed to be unique
   get_cmd_node = olc_get_cmd_hndl_node(spgm_ptr->cmd_hndl_list.cmd_hndl_list_ptr,
                                        spgm_ptr->cmd_hndl_list.num_active_cmd_hndls,
                                        &active_cmd_hndl_ptr,
                                        token);

   if ((TRUE == get_cmd_node) && (NULL != active_cmd_hndl_ptr))
   {
      if (NULL != active_cmd_hndl_ptr->shm_info.shm_mem_ptr)
      {
         set_get_rsp_data_ptr = (uint8_t *)active_cmd_hndl_ptr->shm_info.shm_mem_ptr;

         // cache invalidate the response from the satellite graph
         TRY(result,
             posal_cache_invalidate((uint32_t)set_get_rsp_data_ptr, active_cmd_hndl_ptr->shm_info.shm_alloc_size));
      }
      else if (active_cmd_hndl_ptr->is_inband)
      {
         gpr_packet_t *packet_ptr       = NULL;
         packet_ptr                     = (gpr_packet_t *)spgm_ptr->rsp_msg.payload_ptr;
         apm_cmd_rsp_get_cfg_t *rsp_ptr = (apm_cmd_rsp_get_cfg_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
         set_get_rsp_data_ptr           = (uint8_t *)(rsp_ptr + 1);
      }
      else
      {
         // we should not hit this case
         OLC_SGM_MSG(OLC_SGM_ID,
                     DBG_ERROR_PRIO,
                     "cmd_rsp: cntr_set_get_cfg rsph, unexpected payoad neither Outband or inband");
         return AR_EUNEXPECTED;
      }

      switch (active_cmd_hndl_ptr->sec_opcode)
      {
         case CNTR_PARAM_ID_PATH_DELAY_CFG:
         {
            param_data_ptr = (apm_module_param_data_t *)(set_get_rsp_data_ptr);
            VERIFY(result, (APM_PARAM_ID_OFFLOAD_GRAPH_PATH_DELAY == param_data_ptr->param_id));
            VERIFY(result, (sizeof(sgm_param_id_offload_graph_path_delay_t) == param_data_ptr->param_size));

            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_HIGH_PRIO,
                        "cmd_rsp: cntr_set_get_cfg rsph, processing path delay command response");

            sgm_param_id_offload_graph_path_delay_t *pd_ptr =
               (sgm_param_id_offload_graph_path_delay_t *)(param_data_ptr + 1);

            VERIFY(result, (1 == pd_ptr->num_paths));

            cntr_param_id_path_delay_cfg_t *cmd_rsp_ptr =
               (cntr_param_id_path_delay_cfg_t *)active_cmd_hndl_ptr->rsp_payload_ptr;
            *cmd_rsp_ptr->delay_us_ptr += pd_ptr->paths.delay_us;

            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_HIGH_PRIO,
                        "GET_PATH_DELAY: path id %lu satellite graph delay %lu, total path delay %lu ",
                        cmd_rsp_ptr->path_id,
                        pd_ptr->paths.delay_us,
                        *cmd_rsp_ptr->delay_us_ptr);

            if (FALSE == cmd_rsp_ptr->is_one_time_query)
            {
               get_container_delay_event_t sat_delay_event;
               sat_delay_event.prev_delay_in_us = 0;
               sat_delay_event.new_delay_in_us  = pd_ptr->paths.delay_us;
               sat_delay_event.path_id          = cmd_rsp_ptr->path_id;

               olc_update_path_delay(spgm_ptr->cu_ptr, cmd_rsp_ptr->path_id, (void *)&sat_delay_event);
               sgm_update_path_delay_list(spgm_ptr, cmd_rsp_ptr->path_id, pd_ptr->paths.get_sat_path_id, TRUE);
               sgm_register_path_delay_event(spgm_ptr, TRUE); // register
            }

            break;
         }
         default:
         {
            OLC_SGM_MSG(OLC_SGM_ID, DBG_ERROR_PRIO, "cmd_rsp: cntr_set_get_cfg rsph, unsupported PID");
            result = AR_EUNSUPPORTED;
            break;
         }
      }
   }
   else
   {
      result = AR_EFAILED;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "cmd_rsp: cntr_set_get_cfg rsph done, result %lu", result);

   return result;
}
