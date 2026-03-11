/**
 * \file spdm_utils.c
 * \brief
 *     This file contains Satellite Data Management utility functions.
 *  
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spdm_i.h"

/* =======================================================================
Static Function Definitions
========================================================================== */

/* Utility function to validate if the IPC data connection type is valid or not*/
ar_result_t sgm_is_valid_ipc_data_conn_type(spgm_info_t *spgm_ptr, sdm_ipc_data_type_t data_type)
{
   if ((IPC_WRITE_DATA == data_type) || (IPC_READ_DATA == data_type))
   {
      return AR_EOK;
   }
   else
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "sgm_util: invalid ipc data conn type (w/r: 0/1) %lu",
                  (uint32_t)data_type);
      return AR_EBADPARAM;
   }
}

/* function to get the associated/connected Read/Write EP module instance ID in the satellite graph,
 * given the Read/Write client module instance ID in the OLC core
 */
ar_result_t sgm_get_rw_ep_miid_given_rw_client_miid(spgm_info_t *       spgm_ptr,
                                                    uint32_t            rw_client_miid,
                                                    uint32_t *          sat_rw_ep_miid_ptr,
                                                    sdm_ipc_data_type_t data_type)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t          log_id         = 0;
   uint32_t          arr_indx       = 0;
   uint32_t          sat_rw_ep_miid = 0;
   conn_node_info_t *conn_node_ptr  = NULL;

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != sat_rw_ep_miid_ptr));
   *sat_rw_ep_miid_ptr = 0;

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_MED_PRIO,
               "sgm_util: get sat_rw_ep, given rw_cl_miid (0x%lX) ipc dct (w/r: 0/1) %lu",
               rw_client_miid,
               (uint32_t)data_type);

   TRY(result, sgm_is_valid_ipc_data_conn_type(spgm_ptr, data_type));

   if (IPC_WRITE_DATA == data_type) // for IPC Write path
   {
      VERIFY(result, (0 != spgm_ptr->process_info.data_conn_info.num_data_wr_conn));
      for (arr_indx = 0; arr_indx < spgm_ptr->process_info.data_conn_info.num_data_wr_conn; arr_indx++)
      {
         /** go through the list of connection node information to find the node corresponding
          * to specified wr_client and derived the wr_ep_miid
          **/
         conn_node_ptr = &spgm_ptr->process_info.data_conn_info.ipc_rw_conn_list[IPC_WRITE_DATA][arr_indx];
         if ((TRUE == conn_node_ptr->is_node_used) && (rw_client_miid == conn_node_ptr->src_mod_inst_id))
         {
            sat_rw_ep_miid = conn_node_ptr->dst_mod_inst_id;
            break;
         }
      }
      // if node is not found, error out
      if (arr_indx == spgm_ptr->process_info.data_conn_info.num_data_wr_conn)
      {
         THROW(result, AR_EFAILED);
      }
   }
   else if (IPC_READ_DATA == data_type) // for IPC read path
   {
      VERIFY(result, (0 != spgm_ptr->process_info.data_conn_info.num_data_rd_conn));
      for (arr_indx = 0; arr_indx < spgm_ptr->process_info.data_conn_info.num_data_rd_conn; arr_indx++)
      {
         /** go through the list of connection node information to find the node corresponding to specified rd_client
      and derived the rd_ep_miid */
         conn_node_ptr = &spgm_ptr->process_info.data_conn_info.ipc_rw_conn_list[IPC_READ_DATA][arr_indx];
         if ((TRUE == conn_node_ptr->is_node_used) && (rw_client_miid == conn_node_ptr->dst_mod_inst_id))
         {
            sat_rw_ep_miid = conn_node_ptr->src_mod_inst_id;
            break;
         }
      }
      // if node is not found, error out
      if (arr_indx == spgm_ptr->process_info.data_conn_info.num_data_rd_conn)
      {
         THROW(result, AR_EFAILED);
      }
   }

   *sat_rw_ep_miid_ptr = sat_rw_ep_miid;

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_HIGH_PRIO,
               "sgm_util: sat_rw_ep (0x%lX), given rw_cl_miid (0x%lX) "
               "ipc dct (w/r: 0/1) %lu conn_list index %lu result %lu",
               sat_rw_ep_miid,
               rw_client_miid,
               (uint32_t)data_type,
               arr_indx,
               result);

   return result;
}

/* function to get the associated Read/Write data port index in the SGM driver,
 * given the channel bit index of the associated queue.
 */
ar_result_t sgm_get_data_port_index_given_bit_index(spgm_info_t *       spgm_ptr,
                                                    sdm_ipc_data_type_t data_type,
                                                    uint32_t            channel_bit_index,
                                                    uint32_t *          port_index_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id     = 0;
   uint32_t port_index = 0;
   uint32_t bit_mask   = 1 << channel_bit_index;

   write_data_port_obj_t *wd_port_obj_ptr = NULL;
   read_data_port_obj_t * rd_port_obj_ptr = NULL;

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != port_index_ptr));

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_MED_PRIO,
               "sgm_util: get_data_port_index, given bit_index (0x%lX) ipc dct (w/r: 0/1) %lu",
               channel_bit_index,
               (uint32_t)data_type);
#endif

   TRY(result, sgm_is_valid_ipc_data_conn_type(spgm_ptr, data_type));

   for (port_index = 0; port_index < SPDM_MAX_IO_PORTS; port_index++)
   {
      /** go through the list of port data objects  to find the object corresponding
       * to specified bit_mask and derived the data port index
       **/
      if (IPC_WRITE_DATA == data_type)
      {
         wd_port_obj_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
         if ((NULL != wd_port_obj_ptr) && (bit_mask == wd_port_obj_ptr->port_info.ctrl_cfg.sat_rw_bit_mask))
         {
            *port_index_ptr = port_index;
            break;
         }
      }
      else if (IPC_READ_DATA == data_type)
      {
         rd_port_obj_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
         if ((NULL != rd_port_obj_ptr) && (bit_mask == rd_port_obj_ptr->port_info.ctrl_cfg.sat_rw_bit_mask))
         {
            *port_index_ptr = port_index;
            break;
         }
      }
   }

   if (SPDM_MAX_IO_PORTS == port_index)
   {
      THROW(result, AR_EFAILED);
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_HIGH_PRIO,
               "sgm_util: data_port_index %lu, given cbi (0x%lX) ipc_dct (w/r: 0/1) %lu, result %lu",
               port_index,
               channel_bit_index,
               (uint32_t)data_type,
               result);
#endif

   return AR_EOK;
}

/* function to get the associated data port index in the SGM driver,
 * given the satellite RW EP module instance ID in the satellite graph
 */
ar_result_t sgm_get_data_port_index_given_rw_ep_miid(spgm_info_t *       spgm_ptr,
                                                     sdm_ipc_data_type_t data_type,
                                                     uint32_t            sat_rw_ep_miid,
                                                     uint32_t *          port_index_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id     = 0;
   uint32_t port_index = 0;

   write_data_port_obj_t *wd_port_obj_ptr = NULL;
   read_data_port_obj_t * rd_port_obj_ptr = NULL;

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != port_index_ptr));

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_MED_PRIO,
               "sgm_util: get dpi, given rw_ep_miid (0x%lX) ipc_dct (w/r: 0/1) %lu",
               sat_rw_ep_miid,
               (uint32_t)data_type);

   TRY(result, sgm_is_valid_ipc_data_conn_type(spgm_ptr, data_type));

   for (port_index = 0; port_index < SPDM_MAX_IO_PORTS; port_index++)
   {
      /** go through the list of port data objects  to find the object corresponding
       * to specified satellite RW_EP_MIID and derived the data port index
       **/

      if (IPC_WRITE_DATA == data_type)
      {
         wd_port_obj_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
         if ((NULL != wd_port_obj_ptr) && (sat_rw_ep_miid == wd_port_obj_ptr->port_info.ctrl_cfg.rw_ep_miid))
         {
            *port_index_ptr = port_index;
            break;
         }
      }
      else if (IPC_READ_DATA == data_type)
      {
         rd_port_obj_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
         if ((NULL != rd_port_obj_ptr) && (sat_rw_ep_miid == rd_port_obj_ptr->port_info.ctrl_cfg.rw_ep_miid))
         {
            *port_index_ptr = port_index;
            break;
         }
      }
   }

   if (SPDM_MAX_IO_PORTS == port_index)
   {
      THROW(result, AR_EFAILED);
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_HIGH_PRIO,
               "sgm_util: sat_rw_dpi is %lu, given rw_ep_miid (0x%lX) ipc_dct (w/r: 0/1) %lu result %lu",
               port_index,
               sat_rw_ep_miid,
               (uint32_t)data_type,
               result);

   return result;
}

/* utility function to get the associated data port index in the SGM driver,
 * given the satellite RW client module instance ID in the OLC core
 */
ar_result_t sgm_get_data_port_index_given_rw_client_miid(spgm_info_t *       spgm_ptr,
                                                         sdm_ipc_data_type_t data_type,
                                                         uint32_t            rw_client_miid,
                                                         uint32_t *          port_index_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id     = 0;
   uint32_t port_index = 0;

   write_data_port_obj_t *wd_port_obj_ptr = NULL;
   read_data_port_obj_t * rd_port_obj_ptr = NULL;

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != port_index_ptr));

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_LOW_PRIO,
               "sgm_util: get dpi, given rw_client_miid (0x%lX) ipc dct (w/r: 0/1) %lu",
               rw_client_miid,
               (uint32_t)data_type);

   TRY(result, sgm_is_valid_ipc_data_conn_type(spgm_ptr, data_type));

   for (port_index = 0; port_index < SPDM_MAX_IO_PORTS; port_index++)
   {
      /** go through the list of port data objects  to find the object corresponding
       * to specified RW_CLIENT_MIID and derived the data port index
       **/
      if (IPC_WRITE_DATA == data_type)
      {
         wd_port_obj_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
         if ((NULL != wd_port_obj_ptr) && (rw_client_miid == wd_port_obj_ptr->port_info.ctrl_cfg.rw_client_miid))
         {
            *port_index_ptr = port_index;
            break;
         }
      }
      else if (IPC_READ_DATA == data_type)
      {
         rd_port_obj_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
         if ((NULL != rd_port_obj_ptr) && (rw_client_miid == rd_port_obj_ptr->port_info.ctrl_cfg.rw_client_miid))
         {
            *port_index_ptr = port_index;
            break;
         }
      }
   }

   if (SPDM_MAX_IO_PORTS == port_index)
   {
      THROW(result, AR_EFAILED);
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_HIGH_PRIO,
               "sgm_util: sat_rw_dpi is %lu, given rw_client_miid (0x%lX) ipc dct (w/r: 0/1) %lu result %lu",
               port_index,
               rw_client_miid,
               (uint32_t)data_type,
               result);

   return result;
}

/* utility function to get the associated data port index in the SGM driver,
 * given the satellite Write client module instance ID in the OLC core
 */
ar_result_t sgm_get_data_port_index_given_wr_client_miid(spgm_info_t *spgm_ptr,
                                                         uint32_t     write_client_miid,
                                                         uint32_t *   port_index_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id     = 0;
   uint32_t port_index = 0;

   write_data_port_obj_t *wd_port_obj_ptr = NULL;

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != port_index_ptr));

#ifdef SGM_ENABLE_WRITE_DATA_FLOW_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID, DBG_LOW_PRIO, "data_rsp : get_data_port_index  wr_client_miid (0x%lX)", write_client_miid);
#endif

   /** go through the list of port data objects  to find the object corresponding
    * to specified RW_CLIENT_MIID and derived the data port index
    **/
   for (port_index = 0; port_index < SPDM_MAX_IO_PORTS; port_index++)
   {
      wd_port_obj_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
      if ((NULL != wd_port_obj_ptr) && (write_client_miid == wd_port_obj_ptr->port_info.ctrl_cfg.rw_client_miid))
      {
         *port_index_ptr = port_index;
         break;
      }
   }

   if (SPDM_MAX_IO_PORTS == port_index)
   {
      THROW(result, AR_EFAILED);
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

#ifdef SGM_ENABLE_WRITE_DATA_FLOW_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_LOW_PRIO,
               "data_rsp :data_port_index is %lu, given rw_client_miid (0x%lX)  result %lu",
               port_index,
               write_client_miid,
               result);
#endif

   return result;
}

/* utility function to get the associated data port index in the SGM driver,
 * given the satellite RW client module instance ID in the OLC core
 */
ar_result_t sgm_get_data_port_index_given_rd_client_miid(spgm_info_t *spgm_ptr,
                                                         uint32_t     rd_client_miid,
                                                         uint32_t *   port_index_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id     = 0;
   uint32_t port_index = 0;

   read_data_port_obj_t *rd_port_obj_ptr = NULL;

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != port_index_ptr));

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID, DBG_LOW_PRIO, "data_rsp : get_data_port_index, rd_client_miid (0x%lX)", rd_client_miid);
#endif

   /** go through the list of port data objects  to find the object corresponding
    * to specified RW_CLIENT_MIID and derived the data port index
    **/
   for (port_index = 0; port_index < SPDM_MAX_IO_PORTS; port_index++)
   {
      rd_port_obj_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
      if ((NULL != rd_port_obj_ptr) && (rd_client_miid == rd_port_obj_ptr->port_info.ctrl_cfg.rw_client_miid))
      {
         *port_index_ptr = port_index;
         break;
      }
   }

   if (SPDM_MAX_IO_PORTS == port_index)
   {
      THROW(result, AR_EFAILED);
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_LOW_PRIO,
               "data_rsp :data_port_index is %lu, given rd_client_miid (0x%lX)  result %lu",
               port_index,
               rd_client_miid,
               result);
#endif

   return result;
}
