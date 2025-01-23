/**
 * \file sgm_cmd_handler.c
 * \brief
 *     This file contains Satellite Graph Management functions for command handling.
 *  
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "sgm_i.h"
#include "apm.h"
/* =======================================================================
Static Function Definitions
========================================================================== */

/* Function to handle the Satellite Graph Open command
 * - Analyze the GK MSG open payload to determine the size of OPEN payload
 * - and then create the OPEN payload.
 * - Send the Payload to the satellite processor.
 */
ar_result_t sgm_handle_open(spgm_info_t *             spgm_ptr,
                            spf_msg_cmd_graph_open_t *gmc_apm_open_cmd_ptr,
                            uint32_t                  apm_gmc_payload_size)
{
   ar_result_t result    = AR_EOK;
   uint32_t    opcode    = APM_CMD_GRAPH_OPEN;
   uint32_t    is_inband = TRUE;

   // Command pre-processing
   if (AR_EOK != (result = sgm_cmd_preprocessing(spgm_ptr, opcode, is_inband)))
   {
      return result;
   }

   /* Function to create the gmc_sat_open_payload from the gmc_apm_open_payload
    * the graph message payload for satellite is further analyzed to determine the
    * size of the client payload for the satellite graph open and further create
    * the payload to be send to the satellite process domain for graph open command.
    */
   if (AR_EOK != (result = sgm_create_graph_open_client_payload(spgm_ptr, gmc_apm_open_cmd_ptr, apm_gmc_payload_size)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_MGMT: processing cmd opcode [0x%lX], failed to create the client payload ",
                  opcode);
      // Failed to create the payload. Handling bailout
      sgm_cmd_handling_bail_out(spgm_ptr);
      return (result & (~AR_ECONTINUE));
   }

   /* Send the open command payload to the satellite process domain */
   if (AR_EOK != (result = sgm_ipc_send_command(spgm_ptr)))
   {
      // Failure Case. Handling Bailout
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   // Command post-processing
   sgm_cmd_postprocessing(spgm_ptr);

   return result;
}

/* Function to handle the Satellite Graph Management command
 * - Analyze the SPF_MSG_MGMT payload to determine the command payload size and create the payload
 * - Send the Payload to the satellite processor.
 * The same function is used for Prepare, Start, Stop, Flush, Close
 */
ar_result_t sgm_handle_graph_mgmt_cmd(spgm_info_t *             spgm_ptr,
                                      spf_msg_cmd_graph_mgmt_t *gmc_apm_gmgmt_cmd_ptr,
                                      uint32_t                  payload_size,
                                      uint32_t                  opcode)
{
   ar_result_t result    = AR_EOK;
   uint32_t    is_inband = TRUE;

   // Command pre-processing
   if (AR_EOK != (result = sgm_cmd_preprocessing(spgm_ptr, opcode, is_inband)))
   {
      return result;
   }

   /* Function to analyze the spf_mgmt_payload and create the command payload
    * for the graph management command. Further send to the command to
    * satellite process domain for graph management command.
    */
   if (AR_EOK != (result = sgm_create_graph_mgmt_client_payload(spgm_ptr, gmc_apm_gmgmt_cmd_ptr)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_MGMT: processing cmd opcode [0x%lX], failed to create the client payload ",
                  opcode);
      // Failed to create the payload. Handling bailout
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   /* Send the graph management command payload to the satellite process domain */
   if (AR_EOK != (result = sgm_ipc_send_command(spgm_ptr)))
   {
      // Failure Case. Handling Bailout
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   // Command post-processing
   sgm_cmd_postprocessing(spgm_ptr);

   return result;
}

/* Function to handle the Prepare command */
ar_result_t sgm_handle_prepare(spgm_info_t *             spgm_ptr,
                               spf_msg_cmd_graph_mgmt_t *gmc_apm_prepare_cmd_ptr,
                               uint32_t                  payload_size)
{
   ar_result_t result = AR_EOK;
   uint32_t    opcode = APM_CMD_GRAPH_PREPARE;
   uint32_t    log_id = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != gmc_apm_prepare_cmd_ptr));

   // handle Prepare graph management command
   TRY(result, sgm_handle_graph_mgmt_cmd(spgm_ptr, gmc_apm_prepare_cmd_ptr, payload_size, opcode));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the Start command */
ar_result_t sgm_handle_start(spgm_info_t *             spgm_ptr,
                             spf_msg_cmd_graph_mgmt_t *gmc_apm_start_cmd_ptr,
                             uint32_t                  payload_size)
{
   ar_result_t result = AR_EOK;
   uint32_t    opcode = APM_CMD_GRAPH_START;
   uint32_t    log_id = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != gmc_apm_start_cmd_ptr));

   // handle Start graph management command
   TRY(result, sgm_handle_graph_mgmt_cmd(spgm_ptr, gmc_apm_start_cmd_ptr, payload_size, opcode));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the Suspend command */
ar_result_t sgm_handle_suspend(spgm_info_t *             spgm_ptr,
                               spf_msg_cmd_graph_mgmt_t *gmc_apm_suspend_cmd_ptr,
                               uint32_t                  payload_size)
{
   ar_result_t result = AR_EOK;
   uint32_t    opcode = APM_CMD_GRAPH_SUSPEND;
   uint32_t    log_id = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != gmc_apm_suspend_cmd_ptr));

   // handle suspend graph management command
   TRY(result, sgm_handle_graph_mgmt_cmd(spgm_ptr, gmc_apm_suspend_cmd_ptr, payload_size, opcode));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the Stop command */
ar_result_t sgm_handle_stop(spgm_info_t *             spgm_ptr,
                            spf_msg_cmd_graph_mgmt_t *gmc_apm_stop_cmd_ptr,
                            uint32_t                  payload_size)
{
   ar_result_t result = AR_EOK;
   uint32_t    opcode = APM_CMD_GRAPH_STOP;
   uint32_t    log_id = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != gmc_apm_stop_cmd_ptr));

   // handle Stop graph management command
   TRY(result, sgm_handle_graph_mgmt_cmd(spgm_ptr, gmc_apm_stop_cmd_ptr, payload_size, opcode));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the Flush command */
ar_result_t sgm_handle_flush(spgm_info_t *             spgm_ptr,
                             spf_msg_cmd_graph_mgmt_t *gmc_apm_flush_cmd_ptr,
                             uint32_t                  payload_size)
{
   ar_result_t result = AR_EOK;
   uint32_t    opcode = APM_CMD_GRAPH_FLUSH;
   uint32_t    log_id = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != gmc_apm_flush_cmd_ptr));
   // handle Flush graph management command
   TRY(result, sgm_handle_graph_mgmt_cmd(spgm_ptr, gmc_apm_flush_cmd_ptr, payload_size, opcode));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the Close command */
ar_result_t sgm_handle_close(spgm_info_t *             spgm_ptr,
                             spf_msg_cmd_graph_mgmt_t *gmc_apm_close_cmd_ptr,
                             uint32_t                  payload_size)
{
   ar_result_t result    = AR_EOK;
   uint32_t    opcode    = APM_CMD_GRAPH_CLOSE;
   uint32_t    is_inband = TRUE;
   uint32_t    log_id    = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != gmc_apm_close_cmd_ptr));

   // Command pre-processing
   TRY(result, sgm_cmd_preprocessing(spgm_ptr, opcode, is_inband));

   /* Function to analyze the spf_mgmt_payload and create the close command
    * payload for the graph management command. Further send to the command to
    * satellite process domain for graph management command.
    */
   if (AR_EOK != (result = sgm_create_graph_close_client_payload(spgm_ptr, gmc_apm_close_cmd_ptr)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_MGMT: processing cmd opcode [0x%lX], failed to create the client payload ",
                  opcode);

      // Failed to create the payload. Handling bailout
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   /* Send the graph management command payload to the satellite process domain */
   if (AR_EOK != (result = sgm_ipc_send_command(spgm_ptr)))
   {
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   // Command post-processing
   sgm_cmd_postprocessing(spgm_ptr);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the Calibration command sent to APM
 * - Payload is sent by the client to APM and APM sends to OLC
 */
ar_result_t sgm_handle_set_get_cfg(spgm_info_t *                     spgm_ptr,
                                   spf_msg_cmd_param_data_cfg_t *    gmc_apm_param_data_cfg_ptr,
                                   uint32_t                          payload_size,
                                   bool_t                            is_set_cfg_msg,
                                   bool_t                            is_inband,
                                   spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr)
{
   ar_result_t result = AR_EOK;
   // check if the command is set or get configuration to determine the command opcode
   uint32_t opcode = (is_set_cfg_msg) ? APM_CMD_SET_CFG : APM_CMD_GET_CFG;
   uint32_t log_id = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != gmc_apm_param_data_cfg_ptr));

   // Command pre-processing
   TRY(result, sgm_cmd_preprocessing(spgm_ptr, opcode, is_inband));

   // Determine the size of the payload and create the payload to send to Satellite graph
   if (AR_EOK != (result = sgm_create_set_get_cfg_client_payload(spgm_ptr, gmc_apm_param_data_cfg_ptr, is_inband)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_MGMT: processing cmd opcode [0x%lX], "
                  "failed to create the client payload for the set_get_cfg command",
                  opcode);

      // Failed to create the payload. Handling bailout
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   /* Send the configuration payload to the satellite process domain */
   if (AR_EOK != (result = sgm_ipc_send_command(spgm_ptr)))
   {
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   // Command post-processing
   spgm_ptr->active_cmd_hndl_ptr->cmd_extn_info.extn_payload_ptr = (void *)cmd_extn_ptr;
   spgm_ptr->active_cmd_hndl_ptr->is_apm_cmd_rsp                 = TRUE;
   sgm_cmd_postprocessing(spgm_ptr);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the Calibration command send to module
 * - Payload is sent by the client to Module (i.e., OLC container)
 */
ar_result_t sgm_handle_set_get_cfg_packed(spgm_info_t *spgm_ptr,
                                          uint8_t *    set_cfg_payload_ptr,
                                          uint32_t     set_payload_size,
                                          uint32_t     dst_port,
                                          bool_t       is_inband,
                                          uint32_t     opcode)
{
   ar_result_t result = AR_EOK;
   uint32_t    log_id = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != set_cfg_payload_ptr));

   // Command pre-processing
   TRY(result, sgm_cmd_preprocessing(spgm_ptr, opcode, is_inband));

   // Determine the size of the payload and create the payload to send to Satellite graph
   if (AR_EOK !=
       (result =
           sgm_create_set_get_cfg_packed_client_payload(spgm_ptr, set_cfg_payload_ptr, set_payload_size, is_inband)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_MGMT: processing cmd opcode [0x%lX], "
                  "failed to create the client payload for the set_get_packed_cfg command",
                  opcode);

      // Failed to create the payload. Handling bailout
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   // TODO: VB : for OOB payload need to flush the memory

   /* Send the graph management command payload to the satellite process domain */
   if (AR_EOK != (result = sgm_ipc_send_command_to_dst(spgm_ptr, dst_port)))
   {
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   spgm_ptr->active_cmd_hndl_ptr->rsp_payload_ptr  = set_cfg_payload_ptr;
   spgm_ptr->active_cmd_hndl_ptr->rsp_payload_size = set_payload_size;

   // Command post-processing
   sgm_cmd_postprocessing(spgm_ptr);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the Persistent Calibration command sent to module
 * - Payload is sent by the client to APM  and APM sends to the Module (i.e., container)
 */
ar_result_t sgm_handle_persistent_cfg(spgm_info_t *                     spgm_ptr,
                                      void *                            param_data_ptr,
                                      uint32_t                          payload_size,
                                      bool_t                            is_inband,
                                      bool_t                            is_deregister,
                                      spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr)
{
   ar_result_t       result         = AR_EOK;
   apm_cmd_header_t *cmd_header_ptr = NULL;
   uint32_t          master_handle  = 0;
   uint32_t          offset         = 0;
   uint32_t          opcode         = is_deregister ? APM_CMD_DEREGISTER_CFG : APM_CMD_REGISTER_CFG;
   uint32_t          log_id         = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != param_data_ptr));

   // Command pre-processing
   TRY(result, sgm_cmd_preprocessing(spgm_ptr, opcode, is_inband));

   // allocates APM header size payload to send as gpr pkt
   if (AR_EOK != (result = sgm_alloc_cmd_hndl_resources(spgm_ptr, payload_size, is_inband, TRUE /*is_persistent*/)))
   {
      // Failed to create the payload. Handling bailout
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }
   cmd_header_ptr = (apm_cmd_header_t *)spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;

   result = posal_memorymap_get_shmm_handle_and_offset_from_va_offset_map(apm_get_mem_map_client(),
                                                                          (uint32_t)param_data_ptr,
                                                                          &master_handle,
                                                                          &offset);
   if (AR_EOK != result)
   {
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }
   cmd_header_ptr->mem_map_handle = apm_offload_get_persistent_sat_handle(spgm_ptr->sgm_id.sat_pd, master_handle);

   if (APM_OFFLOAD_INVALID_VAL == cmd_header_ptr->mem_map_handle)
   {
      sgm_cmd_handling_bail_out(spgm_ptr);
      return AR_EFAILED;
   }

   cmd_header_ptr->payload_address_lsw = offset;
   cmd_header_ptr->payload_address_msw = 0; // offset mode
   cmd_header_ptr->payload_size        = payload_size;

   /* Send the graph management command payload to the satellite process domain */
   if (AR_EOK != (result = sgm_ipc_send_command_to_dst(spgm_ptr, sgm_get_dst_port_id(spgm_ptr)))) // send to sat APM
   {
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   // Command post-processing
   spgm_ptr->active_cmd_hndl_ptr->cmd_extn_info.extn_payload_ptr = (void *)cmd_extn_ptr;
   spgm_ptr->active_cmd_hndl_ptr->is_apm_cmd_rsp                 = TRUE;
   sgm_cmd_postprocessing(spgm_ptr);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the Persistent Calibration command sent to module
 * - Payload is sent by the client to APM  and APM sends to the Module (i.e., container)
 */
ar_result_t sgm_handle_persistent_cfg_v2(spgm_info_t *                     spgm_ptr,
                                         void **                           param_data_pptr,
                                         uint32_t                          num_config,
                                         uint32_t                          payload_size,
                                         bool_t                            is_inband,
                                         bool_t                            is_deregister,
                                         spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr)
{
   ar_result_t              result                = AR_EOK;
   apm_cmd_header_t *       cmd_header_ptr        = NULL;
   uint32_t                 master_handle         = 0;
   uint32_t                 offset                = 0;
   uint32_t                 opcode                = is_deregister ? APM_CMD_DEREGISTER_CFG : APM_CMD_REGISTER_CFG;
   uint32_t                 log_id                = 0;
   void *                   param_data_ptr        = NULL;
   apm_module_param_data_t *module_param_data_ptr = NULL;

   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != param_data_pptr));

   // Command pre-processing
   TRY(result, sgm_cmd_preprocessing(spgm_ptr, opcode, is_inband));

   // allocates APM header size payload to send as gpr pkt
   if (AR_EOK != (result = sgm_alloc_cmd_hndl_resources(spgm_ptr, payload_size, is_inband, TRUE /*is_persistent*/)))
   {
      // Failed to create the payload. Handling bailout
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }
   cmd_header_ptr = (apm_cmd_header_t *)spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;
   uint32_t token = posal_atomic_increment(spgm_ptr->token_instance);
   spgm_ptr->active_cmd_hndl_ptr->multi_max_resp_cnt = num_config;

   uint32_t cmd_cnt = 0;
   for (cmd_cnt = 0; cmd_cnt < num_config; cmd_cnt++)
   {
      param_data_ptr = param_data_pptr[cmd_cnt];

      result = posal_memorymap_get_shmm_handle_and_offset_from_va_offset_map(apm_get_mem_map_client(),
                                                                             (uint32_t)param_data_ptr,
                                                                             &master_handle,
                                                                             &offset);
      if (AR_EOK != result)
      {
         break;
      }

      cmd_header_ptr->mem_map_handle = apm_offload_get_persistent_sat_handle(spgm_ptr->sgm_id.sat_pd, master_handle);

      if (APM_OFFLOAD_INVALID_VAL == cmd_header_ptr->mem_map_handle)
      {
         break;
      }

      module_param_data_ptr = (apm_module_param_data_t *)param_data_ptr;

      cmd_header_ptr->payload_address_lsw = offset;
      cmd_header_ptr->payload_address_msw = 0; // offset mode
      cmd_header_ptr->payload_size        = sizeof(apm_module_param_data_t) + module_param_data_ptr->param_size;

      /* Send the graph management command payload to the satellite process domain */
      if (AR_EOK != (result = sgm_ipc_send_command_to_dst_with_token(spgm_ptr,
                                                                     sgm_get_dst_port_id(spgm_ptr),
                                                                     token))) // send to sat APM
      {
         break;
      }
   }

   if ((AR_EOK != (result)) && (0 == cmd_cnt)) // send to sat APM
   {
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }
   else if (AR_EOK != (result))
   {
      spgm_ptr->active_cmd_hndl_ptr->multi_max_resp_cnt -= (num_config - cmd_cnt);
   }

   // Command post-processing
   spgm_ptr->active_cmd_hndl_ptr->cmd_extn_info.extn_payload_ptr = (void *)cmd_extn_ptr;
   spgm_ptr->active_cmd_hndl_ptr->is_apm_cmd_rsp                 = TRUE;
   sgm_cmd_postprocessing(spgm_ptr);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the Persistent Calibration command sent to module
 * - Payload is sent by the client to Module (i.e., OLC container)
 */
ar_result_t sgm_handle_persistent_set_get_cfg_packed(spgm_info_t *     spgm_ptr,
                                                     apm_cmd_header_t *in_apm_cmd_header,
                                                     uint32_t          dst_port,
                                                     uint32_t          opcode)
{
   ar_result_t       result         = AR_EOK;
   bool_t            is_inband      = FALSE;
   apm_cmd_header_t *cmd_header_ptr = NULL;
   uint32_t          log_id         = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != in_apm_cmd_header));

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_HIGH_PRIO,
               "GRAPH_MGMT: processing cmd opcode [0x%lX], "
               "Persistent set cfg command to satellite graph",
               opcode);

   // Command pre-processing
   TRY(result, sgm_cmd_preprocessing(spgm_ptr, opcode, is_inband));

   // allocates APM header size payload to send as gpr pkt
   if (AR_EOK !=
       (result =
           sgm_alloc_cmd_hndl_resources(spgm_ptr, in_apm_cmd_header->payload_size, is_inband, TRUE /*is_persistent*/)))
   {
      // Failed to create the payload. Handling bailout
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_MGMT: processing cmd opcode [0x%lX], Failed to create the client payload",
                  opcode);
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   cmd_header_ptr = (apm_cmd_header_t *)spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;
   memscpy((void *)cmd_header_ptr, sizeof(apm_cmd_header_t), (void *)in_apm_cmd_header, sizeof(apm_cmd_header_t));
   // replace the handle with the sat handle
   cmd_header_ptr->mem_map_handle =
      apm_offload_get_persistent_sat_handle(spgm_ptr->sgm_id.sat_pd, in_apm_cmd_header->mem_map_handle);

   if (APM_OFFLOAD_INVALID_VAL == cmd_header_ptr->mem_map_handle)
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_MGMT: processing cmd opcode [0x%lX], invalid satellite memory handle",
                  opcode);
      sgm_cmd_handling_bail_out(spgm_ptr);
      return AR_EFAILED;
   }

   /* Send the graph management command payload to the satellite process domain */
   if (AR_EOK != (result = sgm_ipc_send_command_to_dst(spgm_ptr, dst_port)))
   {
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   // Command post-processing
   sgm_cmd_postprocessing(spgm_ptr);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the event registration with the satellite modules
 */
ar_result_t sgm_handle_register_module_events(spgm_info_t *spgm_ptr,
                                              uint8_t *    reg_payload_ptr,
                                              uint32_t     reg_payload_size,
                                              uint32_t     client_token,
                                              uint32_t     miid,
                                              uint32_t     client_port_id,
                                              uint32_t     client_domain_id,
                                              bool_t       is_inband,
                                              uint32_t     opcode)
{
   ar_result_t result        = AR_EOK;
   uint32_t    token_to_send = 0;
   uint32_t    log_id        = 0;
   INIT_EXCEPTION_HANDLING

   apm_module_register_events_t *payload_ptr    = NULL;
   spgm_event_info_t *           event_node_ptr = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != reg_payload_ptr));

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_HIGH_PRIO,
               "GRAPH_MGMT: processing cmd opcode [0x%lX], handle module event registration",
               opcode);

   payload_ptr = (apm_module_register_events_t *)reg_payload_ptr;
   if (sizeof(apm_module_register_events_t) > reg_payload_size)
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "GRAPH_MGMT: processing cmd opcode [0x%lX], invalid payload size", opcode);
      return AR_EBADPARAM;
   }

   // Command pre-processing
   if (AR_EOK != (result = sgm_cmd_preprocessing(spgm_ptr, opcode, is_inband)))
   {
      return result;
   }

   event_node_ptr = (spgm_event_info_t *)posal_memory_malloc(sizeof(spgm_event_info_t), spgm_ptr->cu_ptr->heap_id);
   if (NULL == event_node_ptr)
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_MGMT: processing cmd opcode [0x%lX], failed to allocate node memory",
                  opcode);
      sgm_cmd_handling_bail_out(spgm_ptr);
      return AR_ENOMEMORY;
   }

   token_to_send = posal_atomic_increment(spgm_ptr->token_instance);

   // populate the event list node
   event_node_ptr->module_iid          = miid;
   event_node_ptr->client_port_id      = client_port_id;
   event_node_ptr->client_domain_id    = client_domain_id;
   event_node_ptr->client_token        = client_token;
   event_node_ptr->olc_event_reg_token = token_to_send;
   // token_to_send = (uint32_t)event_node_ptr;

   if (AR_EOK != (result = sgm_create_reg_event_payload(spgm_ptr, reg_payload_ptr, reg_payload_size, is_inband)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_MGMT: processing cmd opcode [0x%lX],"
                  "failed to create the client payload for the reg event ID 0x%lx to satellite graph",
                  opcode,
                  payload_ptr->event_id);

      // Failed to create the payload. Handling bailout
      posal_memory_free(event_node_ptr);
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   /* Send the graph management command payload to the satellite process domain */
   if (AR_EOK != (result = sgm_ipc_send_command_to_dst_with_token(spgm_ptr, miid, token_to_send)))
   {
      posal_memory_free(event_node_ptr);
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   // insert the event node in the event registration list
   TRY(result,spf_list_insert_tail(&spgm_ptr->event_reg_list_ptr, event_node_ptr, spgm_ptr->cu_ptr->heap_id, TRUE /* use_pool*/));
   // Command post-processing
   sgm_cmd_postprocessing(spgm_ptr);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the path delay API
 * - Payload is sent by the client to APM and APM sends to OLC
 */
ar_result_t sgm_handle_set_get_path_delay_cfg(spgm_info_t *                     spgm_ptr,
                                              uint8_t *                         apm_path_defn_for_delay_ptr,
                                              uint8_t *                         rsp_payload_ptr,
                                              uint32_t                          rsp_payload_size,
                                              uint32_t                          sec_op_code,
                                              spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr)
{
   ar_result_t result    = AR_EOK;
   uint32_t    opcode    = APM_CMD_GET_CFG;
   uint32_t    log_id    = 0;
   uint32_t    is_inband = TRUE;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != apm_path_defn_for_delay_ptr));

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_HIGH_PRIO,
               "GRAPH_MGMT: processing cmd opcode [0x%lX], sec_opcode [0x%lX] handle path delay configuration",
               opcode,
               sec_op_code);

   // Command pre-processing
   TRY(result, sgm_cmd_preprocessing(spgm_ptr, opcode, is_inband));

   // Determine the size of the payload and create the payload to send to Satellite graph
   if (AR_EOK != (result = sgm_create_get_path_delay_client_payload(spgm_ptr, apm_path_defn_for_delay_ptr, is_inband)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_HIGH_PRIO,
                  "GRAPH_MGMT: processing cmd opcode [0x%lX], sec_opcode [0x%lX] handling path delay configuration"
                  "failed to create the client payload for the set_get_cfg command ",
                  opcode,
                  sec_op_code);

      // Failed to create the payload. Handling bailout
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   /* Send the configuration payload to the satellite process domain */
   if (AR_EOK != (result = sgm_ipc_send_command(spgm_ptr)))
   {
      sgm_cmd_handling_bail_out(spgm_ptr);
      return result;
   }

   // update the active command handle
   spgm_ptr->active_cmd_hndl_ptr->rsp_payload_ptr                = rsp_payload_ptr;
   spgm_ptr->active_cmd_hndl_ptr->rsp_payload_size               = rsp_payload_size;
   spgm_ptr->active_cmd_hndl_ptr->sec_opcode                     = sec_op_code;
   spgm_ptr->active_cmd_hndl_ptr->is_sec_opcode_valid            = TRUE;
   spgm_ptr->active_cmd_hndl_ptr->cmd_extn_info.extn_payload_ptr = (void *)cmd_extn_ptr;
   spgm_ptr->active_cmd_hndl_ptr->is_apm_cmd_rsp                 = TRUE;

   // Command post-processing
   sgm_cmd_postprocessing(spgm_ptr);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}
