/**
 * \file spdm_port_config_handler.c
 * \brief
 *     This file contains the functions for the port configuration
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spdm_i.h"
#include "media_fmt_extn_api.h"
#include "offload_sp_api.h"

sgm_port_reg_event_info_t rd_port_event_info[NUM_RD_PORT_EVENT_CONFIG] = {
   { OFFLOAD_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT, SGM_SOURCE_PORT_CONTAINER, SGM_SINK_PORT_READ_EP },
   { DATA_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT, SGM_SOURCE_PORT_READ_CLIENT, SGM_SINK_PORT_READ_EP },
   { OFFLOAD_EVENT_ID_SH_MEM_EP_OPERATING_FRAME_SIZE, SGM_SOURCE_PORT_READ_CLIENT, SGM_SINK_PORT_READ_EP },
   { OFFLOAD_DATA_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY, SGM_SOURCE_PORT_READ_CLIENT, SGM_SINK_PORT_READ_EP },
   { OFFLOAD_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY, SGM_SOURCE_PORT_CONTAINER, SGM_SINK_PORT_READ_EP },
   { OFFLOAD_EVENT_ID_UPSTREAM_STATE, SGM_SOURCE_PORT_CONTAINER, SGM_SINK_PORT_READ_EP }
};

sgm_port_reg_event_info_t wr_port_event_info[NUM_WR_PORT_EVENT_CONFIG] = {
   { OFFLOAD_EVENT_ID_SH_MEM_EP_OPERATING_FRAME_SIZE, SGM_SOURCE_PORT_WRITE_CLIENT, SGM_SINK_PORT_WRITE_EP },
   { OFFLOAD_EVENT_ID_DOWNSTREAM_PEER_PORT_PROPERTY, SGM_SOURCE_PORT_CONTAINER, SGM_SINK_PORT_READ_EP }
};

// clang-format on
/* =======================================================================
Static Function Definitions
========================================================================== */

/* function to register for the encoder metadata configuration from the
 * satellite graph. Every read client MIID (read port) would register with
 * the associated read EP MIID in the satellite graph.
 * The registration is done during the creation of the port create
 */
ar_result_t spdm_set_rd_ep_port_config(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id            = 0;
   uint32_t rd_ep_port_id     = 0;
   uint32_t rd_client_port_id = 0;
   uint32_t token             = 0;

   typedef struct rd_ep_md_calib_t
   {
      apm_cmd_header_t         cmd_header;
      apm_module_param_data_t  calib_header;
      param_id_rd_sh_mem_cfg_t calib_data;
   } rd_ep_md_calib_t;

   rd_ep_md_calib_t *rd_ep_md_calib_ptr = NULL;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "read data port encoder configuration");

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != spgm_ptr->process_info.rdp_obj_ptr[port_index]));

   // satellite RD EP module IID
   rd_ep_port_id = spgm_ptr->process_info.rdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_ep_miid;
   // read client module IID
   rd_client_port_id = spgm_ptr->process_info.rdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_client_miid;

   // Allocate the memory for the metadata calibration payload
   rd_ep_md_calib_ptr = (rd_ep_md_calib_t *)posal_memory_malloc(sizeof(rd_ep_md_calib_t), spgm_ptr->cu_ptr->heap_id);
   if (NULL == rd_ep_md_calib_ptr)
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "Failed to allocate memory to configure RD_EP with encoding metadata configuration");
      return AR_ENOMEMORY;
      // if the registration fails. We may not be able to process this graph further
   }

   memset((void *)rd_ep_md_calib_ptr, 0, sizeof(rd_ep_md_calib_ptr));

   (void)sgm_get_unique_token(spgm_ptr, &token);

   // fill the header with the size information
   rd_ep_md_calib_ptr->cmd_header.payload_size        = sizeof(rd_ep_md_calib_t) - sizeof(apm_cmd_header_t);
   rd_ep_md_calib_ptr->cmd_header.mem_map_handle      = 0;
   rd_ep_md_calib_ptr->cmd_header.payload_address_lsw = 0;
   rd_ep_md_calib_ptr->cmd_header.payload_address_msw = 0;

   // fill the calibration payload
   rd_ep_md_calib_ptr->calib_header.module_instance_id = rd_ep_port_id;
   rd_ep_md_calib_ptr->calib_header.param_id           = PARAM_ID_RD_SH_MEM_CFG;
   rd_ep_md_calib_ptr->calib_header.param_size         = sizeof(param_id_rd_sh_mem_cfg_t);
   rd_ep_md_calib_ptr->calib_header.error_code         = 0;

   uint32_t metadata_control_flags = 0;

   tu_set_bits(&metadata_control_flags,
               FALSE, // todo : might need more analysis on this
               RD_EP_CFG_MD_CNTRL_FLAGS_BIT_MASK_ENABLE_ENCODER_FRAME_MD,
               RD_EP_CFG_MD_CNTRL_FLAGS_SHIFT_ENABLE_ENCODER_FRAME_MD);

   tu_set_bits(&metadata_control_flags,
               FALSE,
               RD_EP_CFG_MD_CNTRL_FLAGS_BIT_MASK_ENABLE_MEDIA_FORMAT_MD,
               RD_EP_CFG_MD_CNTRL_FLAGS_SHIFT_ENABLE_MEDIA_FORMAT_MD);

   rd_ep_md_calib_ptr->calib_data.metadata_control_flags = metadata_control_flags;
   rd_ep_md_calib_ptr->calib_data.num_frames_per_buffer  = 1;

   // fill the details to be filled in the GPR packet
   spgm_ptr->process_info.active_data_hndl.payload_size = sizeof(rd_ep_md_calib_t);
   spgm_ptr->process_info.active_data_hndl.payload_ptr  = (uint8_t *)rd_ep_md_calib_ptr;
   spgm_ptr->process_info.active_data_hndl.src_port     = rd_client_port_id;
   spgm_ptr->process_info.active_data_hndl.dst_port     = rd_ep_port_id;
   spgm_ptr->process_info.active_data_hndl.opcode       = APM_CMD_SET_CFG;
   spgm_ptr->process_info.active_data_hndl.token        = token;

   // Send the meta data calibration command to RD EP module on the satellite graph
   TRY(result, sgm_ipc_send_data_pkt(spgm_ptr));

   // reset the active handle
   memset(&spgm_ptr->process_info.active_data_hndl, 0, sizeof(spgm_ipc_data_obj_t));

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "read data port encoder configuration, completed");

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   // Free the event payload memory
   if (NULL != rd_ep_md_calib_ptr)
   {
      posal_memory_free(rd_ep_md_calib_ptr);
   }

   return result;
}

/* function to register for the client configuration event from the satellite Graph
 * Every read client miid (read port) would register with the associated
 * Read EP miid in the satellite graph.
 * The registration is done during the creation of the PORT
 */
ar_result_t spdm_set_rd_ep_client_config(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id            = 0;
   uint32_t rd_ep_port_id     = 0;
   uint32_t rd_client_port_id = 0;
   uint32_t token             = 0;

   typedef struct rd_ep_md_calib_t
   {
      apm_cmd_header_t            cmd_header;
      apm_module_param_data_t     calib_header;
      param_id_rd_ep_client_cfg_t client_config;
   } rd_ep_md_calib_t;

   rd_ep_md_calib_t *rd_ep_md_calib_ptr = NULL;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "read data port client configuration");

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != spgm_ptr->process_info.rdp_obj_ptr[port_index]));

   // satellite RD EP module IID
   rd_ep_port_id = spgm_ptr->process_info.rdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_ep_miid;
   // read client module IID
   rd_client_port_id = spgm_ptr->process_info.rdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_client_miid;

   // Allocate the memory for the metadata calibration payload
   rd_ep_md_calib_ptr = (rd_ep_md_calib_t *)posal_memory_malloc(sizeof(rd_ep_md_calib_t), spgm_ptr->cu_ptr->heap_id);
   if (NULL == rd_ep_md_calib_ptr)
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "Failed to allocate memory to configure RD_EP with client configuration");
      return AR_ENOMEMORY;
      // if the registration fails. We may not be able to process this graph further
   }

   memset((void *)rd_ep_md_calib_ptr, 0, sizeof(rd_ep_md_calib_t));

   (void)sgm_get_unique_token(spgm_ptr, &token);

   // fill the header with the size information
   rd_ep_md_calib_ptr->cmd_header.payload_size        = sizeof(rd_ep_md_calib_t) - sizeof(apm_cmd_header_t);
   rd_ep_md_calib_ptr->cmd_header.mem_map_handle      = 0;
   rd_ep_md_calib_ptr->cmd_header.payload_address_lsw = 0;
   rd_ep_md_calib_ptr->cmd_header.payload_address_msw = 0;

   // fill the calibration payload
   rd_ep_md_calib_ptr->calib_header.module_instance_id = rd_ep_port_id;
   rd_ep_md_calib_ptr->calib_header.param_id           = PARAM_ID_RD_EP_CLIENT_CFG;
   rd_ep_md_calib_ptr->calib_header.param_size         = sizeof(param_id_rd_ep_client_cfg_t);
   rd_ep_md_calib_ptr->calib_header.error_code         = 0;

   rd_ep_md_calib_ptr->client_config.client_id   = CLIENT_ID_OLC;
   rd_ep_md_calib_ptr->client_config.gpr_port_id = spgm_ptr->sgm_id.cont_id;

   // fill the details to be filled in the GPR packet
   spgm_ptr->process_info.active_data_hndl.payload_size = sizeof(rd_ep_md_calib_t);
   spgm_ptr->process_info.active_data_hndl.payload_ptr  = (uint8_t *)rd_ep_md_calib_ptr;
   spgm_ptr->process_info.active_data_hndl.src_port     = rd_client_port_id;
   spgm_ptr->process_info.active_data_hndl.dst_port     = rd_ep_port_id;
   spgm_ptr->process_info.active_data_hndl.opcode       = APM_CMD_SET_CFG;
   spgm_ptr->process_info.active_data_hndl.token        = token;

   // Send the meta data calibration command to RD EP module on the satellite graph
   TRY(result, sgm_ipc_send_data_pkt(spgm_ptr));

   // reset the active handle
   memset(&spgm_ptr->process_info.active_data_hndl, 0, sizeof(spgm_ipc_data_obj_t));

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "read data port client configuration completed");

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   // Free the payload memory
   if (NULL != rd_ep_md_calib_ptr)
   {
      posal_memory_free(rd_ep_md_calib_ptr);
   }

   return result;
}

/* function to set the configuration to Satellite RD_EP and release the cached metadata
 */
ar_result_t spdm_set_rd_ep_md_rendered_config(spgm_info_t *              spgm_ptr,
                                              uint32_t                   port_index,
                                              metadata_tracking_event_t *md_te_ptr,
                                              uint32_t                   is_last_instance)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id            = 0;
   uint32_t rd_ep_port_id     = 0;
   uint32_t rd_client_port_id = 0;
   uint32_t token             = 0;

   typedef struct md_rendered_calib_t
   {
      apm_cmd_header_t                 cmd_header;
      apm_module_param_data_t          calib_header;
      param_id_rd_ep_md_rendered_cfg_t calib_data;
   } md_rendered_calib_t;

   md_rendered_calib_t *md_rendered_calib_ptr = NULL;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "MD_DBG: Read data port Release MD configuration");

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != spgm_ptr->process_info.rdp_obj_ptr[port_index]));

   // satellite RD EP module IID
   rd_ep_port_id = spgm_ptr->process_info.rdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_ep_miid;
   // read client module IID
   rd_client_port_id = spgm_ptr->process_info.rdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_client_miid;

   // Allocate the memory for the metadata calibration payload
   md_rendered_calib_ptr =
      (md_rendered_calib_t *)posal_memory_malloc(sizeof(md_rendered_calib_t), spgm_ptr->cu_ptr->heap_id);
   if (NULL == md_rendered_calib_ptr)
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "Failed to allocate memory to configure RD_EP with release MD configuration");
      return AR_ENOMEMORY;
      // if the registration fails. We may not be able to process this graph further
   }

   memset((void *)md_rendered_calib_ptr, 0, sizeof(md_rendered_calib_t));

   (void)sgm_get_unique_token(spgm_ptr, &token);

   // fill the header with the size information
   md_rendered_calib_ptr->cmd_header.payload_size        = sizeof(md_rendered_calib_t) - sizeof(apm_cmd_header_t);
   md_rendered_calib_ptr->cmd_header.mem_map_handle      = 0;
   md_rendered_calib_ptr->cmd_header.payload_address_lsw = 0;
   md_rendered_calib_ptr->cmd_header.payload_address_msw = 0;

   // fill the calibration payload
   md_rendered_calib_ptr->calib_header.module_instance_id = rd_ep_port_id;
   md_rendered_calib_ptr->calib_header.param_id           = PARAM_ID_RD_EP_MD_RENDERED_CFG;
   md_rendered_calib_ptr->calib_header.param_size         = sizeof(param_id_rd_ep_md_rendered_cfg_t);
   md_rendered_calib_ptr->calib_header.error_code         = 0;

   md_rendered_calib_ptr->calib_data.md_node_address_lsw   = md_te_ptr->token_lsw;
   md_rendered_calib_ptr->calib_data.md_node_address_msw   = md_te_ptr->token_msw;
   md_rendered_calib_ptr->calib_data.md_rendered_port_id   = md_te_ptr->module_instance_id;
   md_rendered_calib_ptr->calib_data.md_rendered_domain_id = spgm_ptr->sgm_id.master_pd;
   md_rendered_calib_ptr->calib_data.is_last_instance      = is_last_instance;
   md_rendered_calib_ptr->calib_data.is_md_dropped         = md_te_ptr->status;

   // fill the details to be filled in the GPR packet
   spgm_ptr->process_info.active_data_hndl.payload_size = sizeof(md_rendered_calib_t);
   spgm_ptr->process_info.active_data_hndl.payload_ptr  = (uint8_t *)md_rendered_calib_ptr;
   spgm_ptr->process_info.active_data_hndl.src_port     = rd_client_port_id;
   spgm_ptr->process_info.active_data_hndl.dst_port     = rd_ep_port_id;
   spgm_ptr->process_info.active_data_hndl.opcode       = APM_CMD_SET_CFG;
   spgm_ptr->process_info.active_data_hndl.token        = token;

   // Send the meta data calibration command to RD EP module on the satellite graph
   result = sgm_ipc_send_data_pkt(spgm_ptr);

   if (AR_EOK != result)
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_HIGH_PRIO,
                  "MD_DBG: read data port Release MD configuration "
                  "rd_client_miid 0x%lx sat rd_ep_miid 0x%lx, "
                  "token (msw, lsw) 0x%lx 0x%lx , "
                  "failed to send the configuration, "
                  "can happen when SAT RD_EP is getting closed",
                  rd_client_port_id,
                  rd_ep_port_id,
                  md_te_ptr->token_msw,
                  md_te_ptr->token_lsw);
   }
   else
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_HIGH_PRIO,
                  "MD_DBG: read data port Release MD configuration "
                  "rd_client_miid 0x%lx sat rd_ep_miid 0x%lx, "
                  "token (msw, lsw) 0x%lx 0x%lx completed",
                  rd_client_port_id,
                  rd_ep_port_id,
                  md_te_ptr->token_msw,
                  md_te_ptr->token_lsw);
   }

   // reset the active handle
   memset(&spgm_ptr->process_info.active_data_hndl, 0, sizeof(spgm_ipc_data_obj_t));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   // Free the event payload memory
   if (NULL != md_rendered_calib_ptr)
   {
      posal_memory_free(md_rendered_calib_ptr);
   }

   return result;
}

/* function to register for the client configuration event from the satellite Graph
 * Every Write client miid (read port) would register with the associated
 * Write EP miid in the satellite graph.
 * The registration is done during the creation of the PORT
 */
ar_result_t spdm_set_wd_ep_client_config(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id            = 0;
   uint32_t wd_ep_port_id     = 0;
   uint32_t wd_client_port_id = 0;
   uint32_t token             = 0;

   typedef struct wd_ep_md_calib_t
   {
      apm_cmd_header_t            cmd_header;
      apm_module_param_data_t     calib_header;
      param_id_wr_ep_client_cfg_t client_config;
   } wd_ep_md_calib_t;

   wd_ep_md_calib_t *wd_ep_md_calib_ptr = NULL;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "write data port client configuration");

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != spgm_ptr->process_info.wdp_obj_ptr[port_index]));

   // satellite write EP module IID
   wd_ep_port_id = spgm_ptr->process_info.wdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_ep_miid;
   // write client module IID
   wd_client_port_id = spgm_ptr->process_info.wdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_client_miid;

   // Allocate the memory for the metadata calibration payload
   wd_ep_md_calib_ptr = (wd_ep_md_calib_t *)posal_memory_malloc(sizeof(wd_ep_md_calib_t), spgm_ptr->cu_ptr->heap_id);
   if (NULL == wd_ep_md_calib_ptr)
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "Failed to allocate memory to configure WR_EP with client configuration");
      return AR_ENOMEMORY;
      // if the registration fails. We may not be able to process this graph further
   }

   memset((void *)wd_ep_md_calib_ptr, 0, sizeof(wd_ep_md_calib_t));

   (void)sgm_get_unique_token(spgm_ptr, &token);

   // fill the header with the size information
   wd_ep_md_calib_ptr->cmd_header.payload_size        = sizeof(wd_ep_md_calib_t) - sizeof(apm_cmd_header_t);
   wd_ep_md_calib_ptr->cmd_header.mem_map_handle      = 0;
   wd_ep_md_calib_ptr->cmd_header.payload_address_lsw = 0;
   wd_ep_md_calib_ptr->cmd_header.payload_address_msw = 0;

   // fill the calibration payload
   wd_ep_md_calib_ptr->calib_header.module_instance_id = wd_ep_port_id;
   wd_ep_md_calib_ptr->calib_header.param_id           = PARAM_ID_WR_EP_CLIENT_CFG;
   wd_ep_md_calib_ptr->calib_header.param_size         = sizeof(param_id_wr_ep_client_cfg_t);
   wd_ep_md_calib_ptr->calib_header.error_code         = 0;

   wd_ep_md_calib_ptr->client_config.client_id   = CLIENT_ID_OLC;
   wd_ep_md_calib_ptr->client_config.gpr_port_id = spgm_ptr->sgm_id.cont_id;

   // fill the details to be filled in the GPR packet
   spgm_ptr->process_info.active_data_hndl.payload_size = sizeof(wd_ep_md_calib_t);
   spgm_ptr->process_info.active_data_hndl.payload_ptr  = (uint8_t *)wd_ep_md_calib_ptr;
   spgm_ptr->process_info.active_data_hndl.src_port     = wd_client_port_id;
   spgm_ptr->process_info.active_data_hndl.dst_port     = wd_ep_port_id;
   spgm_ptr->process_info.active_data_hndl.opcode       = APM_CMD_SET_CFG;
   spgm_ptr->process_info.active_data_hndl.token        = token;

   // Send the meta data calibration command to RD EP module on the satellite graph
   TRY(result, sgm_ipc_send_data_pkt(spgm_ptr));

   // reset the active handle
   memset(&spgm_ptr->process_info.active_data_hndl, 0, sizeof(spgm_ipc_data_obj_t));

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "write data port client configuration completed");

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   // Free the payload memory
   if (NULL != wd_ep_md_calib_ptr)
   {
      posal_memory_free(wd_ep_md_calib_ptr);
   }

   return result;
}

/* function to register/de-register event to the the satellite Graph
 * Every read client miid (read port) would register with the associated
 * Read EP miid in the satellite graph.
 * Every Write Client miid (write port) would register with the associated
 * Write EP miid in the satellite graph
 * some events are registered with container as the source to handle the responses
 * in priority, specially the state propagation events
 * The registration is done during the creation of the PORT
 */
ar_result_t sgm_config_data_port_events(spgm_info_t *spgm_ptr,
                                        uint32_t     port_index,
                                        uint32_t     data_type,
                                        bool_t       is_register)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                   log_id             = 0;
   uint32_t                   rw_ep_port_id      = 0;
   uint32_t                   rw_client_port_id  = 0;
   uint32_t                   src_port           = 0;
   uint32_t                   num_event_config   = 0;
   uint32_t                   event_id           = 0;
   sgm_source_port_t          src_port_info      = 0;
   rw_data_port_event_cfg_t * event_cfg_ptr      = NULL;
   sgm_port_reg_event_info_t *event_reg_info_ptr = NULL;

   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "data port event configuration (wr/rd:0/1) %lu", (uint32_t)data_type);

   // Allocate the memory for the event registration payload
   event_cfg_ptr =
      (rw_data_port_event_cfg_t *)posal_memory_malloc(sizeof(rw_data_port_event_cfg_t), spgm_ptr->cu_ptr->heap_id);
   if (NULL == event_cfg_ptr)
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "Failed to allocate memory for data port event configuration (wr/rd:0/1) %lu",
                  (uint32_t)data_type);
      THROW(result, AR_ENOMEMORY);
      // if the registration fails. We may not be able to process this graph further
   }

   if (IPC_WRITE_DATA == data_type)
   {
      VERIFY(result, (NULL != spgm_ptr->process_info.wdp_obj_ptr[port_index]));

      // satellite WR EP module IID
      rw_ep_port_id = spgm_ptr->process_info.wdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_ep_miid;
      // write client module IID
      rw_client_port_id = spgm_ptr->process_info.wdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_client_miid;

      // number of events for Write port
      num_event_config = NUM_WR_PORT_EVENT_CONFIG;

      // pointer to the List of events
      event_reg_info_ptr = &wr_port_event_info[0];
   }
   else if (IPC_READ_DATA == data_type)
   {
      VERIFY(result, (NULL != spgm_ptr->process_info.rdp_obj_ptr[port_index]));

      // satellite rd EP module IID
      rw_ep_port_id = spgm_ptr->process_info.rdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_ep_miid;
      // read client module IID
      rw_client_port_id = spgm_ptr->process_info.rdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_client_miid;

      // number of events for Read port
      num_event_config = NUM_RD_PORT_EVENT_CONFIG;

      // pointer to the List of events
      event_reg_info_ptr = &rd_port_event_info[0];
   }

   for (uint32_t indx = 0; indx < num_event_config; indx++)
   {
      src_port_info = event_reg_info_ptr[indx].src_port;
      event_id      = event_reg_info_ptr[indx].event_id;

      // determine the source port, client or container
      if ((SGM_SOURCE_PORT_READ_CLIENT == src_port_info) || (SGM_SOURCE_PORT_WRITE_CLIENT == src_port_info))
      {
         src_port = rw_client_port_id;
      }
      else if (SGM_SOURCE_PORT_CONTAINER == src_port_info)
      {
         src_port = spgm_ptr->sgm_id.cont_id;
      }

      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_HIGH_PRIO,
                  "data port event register (wr/rd:0/1) %lu, event id (0x%lX) src port (0x%lx) dst port (0x%lx)",
                  (uint32_t)data_type,
                  event_id,
                  src_port,
                  rw_ep_port_id);

      memset((void *)event_cfg_ptr, 0, sizeof(rw_data_port_event_cfg_t));

      // fill the header with the size information
      event_cfg_ptr->header.payload_size = sizeof(rw_data_port_event_cfg_t) - sizeof(apm_cmd_header_t);

      // fill the event payload
      event_cfg_ptr->reg_evt.event_config_payload_size = 0;
      event_cfg_ptr->reg_evt.event_id                  = event_id;
      event_cfg_ptr->reg_evt.is_register               = is_register;
      event_cfg_ptr->reg_evt.module_instance_id        = rw_ep_port_id;

      // fill the details to be filled in the GPR packet
      spgm_ptr->process_info.active_data_hndl.payload_size = sizeof(rw_data_port_event_cfg_t);
      spgm_ptr->process_info.active_data_hndl.payload_ptr  = (uint8_t *)event_cfg_ptr;
      spgm_ptr->process_info.active_data_hndl.src_port     = src_port;
      spgm_ptr->process_info.active_data_hndl.dst_port     = rw_ep_port_id;
      spgm_ptr->process_info.active_data_hndl.opcode       = APM_CMD_REGISTER_MODULE_EVENTS;
      spgm_ptr->process_info.active_data_hndl.token        = 0;

      // Send the Registration command to the satellite graph
      TRY(result, sgm_ipc_send_data_pkt(spgm_ptr));

      // reset the active handle
      memset(&spgm_ptr->process_info.active_data_hndl, 0, sizeof(spgm_ipc_data_obj_t));
   }

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "data port event configuration completed");

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   // Free the event payload memory
   if (NULL != event_cfg_ptr)
   {
      posal_memory_free(event_cfg_ptr);
   }

   return result;
}

/* Function to configure the read IPC data port */
ar_result_t sdm_setup_rd_data_port(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   uint32_t    log_id = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "read data port init start");

   TRY(result, spdm_set_rd_ep_port_config(spgm_ptr, port_index));
   TRY(result, spdm_set_rd_ep_client_config(spgm_ptr, port_index));
   TRY(result, sgm_config_data_port_events(spgm_ptr, port_index, IPC_READ_DATA, TRUE));

   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "read data port init done");

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }
   return result;
}

/* Function to configure the write IPC data port */
ar_result_t sdm_setup_wr_data_port(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   uint32_t    log_id = 0;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "write data port init start");

   TRY(result, spdm_set_wd_ep_client_config(spgm_ptr, port_index));
   TRY(result, sgm_config_data_port_events(spgm_ptr, port_index, IPC_WRITE_DATA, TRUE));

   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "write data port init done");

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }
   return result;
}
