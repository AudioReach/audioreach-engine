/**
 * \file spdm_data_port_handler.c
 * \brief
 *     This file contains Satellite Graph Management functions for buffer handling for data path
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spdm_i.h"
#include "sprm.h"

/* =======================================================================
Static Function Definitions
========================================================================== */

/* Function to handle the read data response messages from the Read EP MIID in the Satellite Graph commands*/
static ar_result_t spgm_rd_data_port_msg_handler(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;

   posal_queue_t *       data_q_ptr    = NULL;
   spf_msg_t *           data_q_msg    = NULL;
   read_data_port_obj_t *read_data_ptr = NULL;
   gpr_packet_t *        packet_ptr    = NULL;

   // Validate the input arguments
   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (SPDM_MAX_IO_PORTS > port_index));

#ifdef SGM_ENABLE_DATA_RSP_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "rd_port_msg_h : processing read queue element");
#endif

   // Get the pointer to the queue and the msg

   read_data_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
   VERIFY(result, (NULL != read_data_ptr));
   data_q_ptr = read_data_ptr->port_info.this_handle.q_ptr;
   data_q_msg = &read_data_ptr->port_info.output_data_q_msg;

   VERIFY(result, (NULL != data_q_ptr));
   // POP the message from the queue.
   TRY(result, posal_queue_pop_front(data_q_ptr, (posal_queue_element_t *)(data_q_msg)));

   packet_ptr = (gpr_packet_t *)data_q_msg->payload_ptr;
   VERIFY(result, (NULL != packet_ptr));

#ifdef SGM_ENABLE_DATA_RSP_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_HIGH_PRIO,
               "rd_port_msg_h: processing cmd opcode (%lX) token (%lx) , pkt_ptr 0x%lx",
               packet_ptr->opcode,
               packet_ptr->token,
               packet_ptr);
#endif

   switch (packet_ptr->opcode)
   {
      // Read Done
      case DATA_CMD_RSP_RD_SH_MEM_EP_DATA_BUFFER_DONE_V2:
      {
         TRY(result, spdm_process_data_read_done(spgm_ptr, packet_ptr, port_index));
         break;
      }
      // Response media format
      case DATA_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT:
      {
         TRY(result, spdm_process_media_format_event(spgm_ptr, packet_ptr, port_index, TRUE));
         break;
      }

      case OFFLOAD_DATA_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY:
      {
         TRY(result, spgm_handle_event_upstream_peer_port_property(spgm_ptr, packet_ptr));
         break;
      }

      default:
      {
         OLC_SDM_MSG(OLC_SDM_ID,
                     DBG_ERROR_PRIO,
                     "read msg handler, Unsupported response with pkt opcode (%lX) token(%lx)",
                     packet_ptr->opcode,
                     packet_ptr->token);

         THROW(result, AR_EUNSUPPORTED);
      }
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   if (NULL != packet_ptr)
   {
      __gpr_cmd_free(packet_ptr);
   }
   return result;
}

/* Function to handle the read responses sent from the satellite graph
 */
static ar_result_t spdm_rd_dataQ_handler(cu_base_t *cu_ptr, uint32_t channel_bit_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id     = 0;
   uint32_t port_index = 0;

   uint8_t *    base_ptr = NULL;
   spgm_info_t *spgm_ptr = NULL;

   VERIFY(result, (NULL != cu_ptr));
   base_ptr = (uint8_t *)cu_ptr;

   spgm_ptr = (spgm_info_t *)(base_ptr + sizeof(cu_base_t));
   log_id   = spgm_ptr->sgm_id.log_id;

   // get the read data port index given the channel_bit_index of the corresponding data Queue
   TRY(result, sgm_get_data_port_index_given_bit_index(spgm_ptr, IPC_READ_DATA, channel_bit_index, &port_index))

   // Function to handle the message from the satellite Graph
   TRY(result, spgm_rd_data_port_msg_handler(spgm_ptr, port_index));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/*
 * function to create an instance of the read data port. Read data port is created
 * to handle processed data from the satellite graph and and send the data to the
 * downstream. The Port handle store would store all the information to associate
 * the read client miid, read EP miid and the corresponding port index.
 */
static ar_result_t sgm_create_read_data_port(spgm_info_t *spgm_ptr, uint32_t *data_port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t              log_id          = 0;
   uint32_t              port_index      = 0;
   read_data_port_obj_t *rd_port_obj_ptr = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "rd_port_init: port create, (ignore port index)");

   // find the available port index. Check for port object which is NULL
   for (port_index = 0; port_index < SPDM_MAX_IO_PORTS; port_index++)
   {
      if (NULL == spgm_ptr->process_info.rdp_obj_ptr[port_index])
      {
         break;
      }
   }

   if (SPDM_MAX_IO_PORTS == port_index)
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "read_port_init, Number of output port connections reached max ");
      THROW(result, AR_ENORESOURCE);
   }

   // allocate the memory for the read port object
   rd_port_obj_ptr = (read_data_port_obj_t *)posal_memory_malloc((sizeof(read_data_port_obj_t)),
                                                                 (POSAL_HEAP_ID)spgm_ptr->cu_ptr->heap_id);

   if (NULL == rd_port_obj_ptr)
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "read_port_init, failed to allocate memory for the port object ");
      THROW(result, AR_ENOMEMORY);
      // Failed to create the read data port, as memory is not available
   }

   memset(rd_port_obj_ptr, 0, sizeof(read_data_port_obj_t));
   spgm_ptr->process_info.rdp_obj_ptr[port_index] = rd_port_obj_ptr;
   *data_port_index                               = port_index;

#ifdef SGM_ENABLE_DATA_PORT_INIT_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "read_port_init, create done ");
#endif

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/*
 * function to create the read data queue.
 * All the  read done packet will be received by this queue
 * The queue is created per read data port
 */
ar_result_t sgm_create_rd_data_queue(cu_base_t *            cu_ptr,
                                     spgm_info_t *          spgm_ptr,
                                     uint32_t               ext_outport_bitmask,
                                     uint32_t               rd_client_miid,
                                     void *                 dest,
                                     sdm_data_port_info_t **rd_ctrl_cfg_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t              log_id           = 0;
   uint32_t              data_queue_index = 0;
   uint32_t              num_elements     = 0;
   uint32_t              bit_mask         = 0;
   uint32_t              port_index       = 0;
   uint32_t              sat_rd_ep_miid   = 0;
   char                  sgm_rd_data_q_name[POSAL_DEFAULT_NAME_LEN]; // data queue name
   read_data_port_obj_t *read_data_ptr = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "rd_port_init: create queue, (ignore port index)");

   // Create the read data port
   TRY(result, sgm_create_read_data_port(spgm_ptr, &port_index));
   // Get the satellite Read EP MIID for this port, given the Read Client MIID
   TRY(result, sgm_get_rw_ep_miid_given_rw_client_miid(spgm_ptr, rd_client_miid, &sat_rd_ep_miid, IPC_READ_DATA));

   read_data_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];

   // determine the queue index
   data_queue_index = ((spgm_ptr->sgm_id.log_id) | ((port_index) << PORT_INDEX_SHIFT_FACTOR));

   snprintf(sgm_rd_data_q_name, POSAL_DEFAULT_NAME_LEN, "%s%s%8lX", "RD", "SGM", data_queue_index);

   num_elements = SDM_MAX_DATA_Q_ELEMENTS;
   bit_mask     = cu_request_bit_in_bit_mask(&cu_ptr->available_bit_mask);

   // Failed to get valid bit mask for the read port. return fail code
   if (0 == bit_mask)
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "read_port_init, creating read data queue, bit mask has no bits available, "
                  " value of available_bit_mask 0x%08lx ",
                  cu_ptr->available_bit_mask);
      return AR_ENORESOURCE;
   }
   else
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_HIGH_PRIO,
                  "read_port_init, creating read data queue, with bit mask input 0x%08lx ",
                  bit_mask);
   }

   // creating the read data port queue
   if (AR_EOK != (result = cu_init_queue(cu_ptr,
                                         sgm_rd_data_q_name,
                                         num_elements,
                                         bit_mask,
                                         spdm_rd_dataQ_handler,
                                         cu_ptr->channel_ptr,
                                         &read_data_ptr->port_info.this_handle.q_ptr,
                                         dest,
                                         cu_ptr->heap_id)))
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "read_port_init, failed to init data queue with bit mask input 0x%08lx  ",
                  bit_mask);
      return result;
   }

   // remove the bit field from the available bit mask
   cu_ptr->available_bit_mask &= (~(bit_mask));
   cu_ptr->curr_chan_mask |= ((bit_mask));

   // populate the port control configuration
   read_data_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask = ext_outport_bitmask;
   read_data_ptr->port_info.ctrl_cfg.sat_rw_bit_mask       = bit_mask;
   read_data_ptr->port_info.ctrl_cfg.sdm_port_index        = port_index;
   read_data_ptr->port_info.ctrl_cfg.rw_client_miid        = rd_client_miid;
   read_data_ptr->port_info.ctrl_cfg.rw_ep_miid            = sat_rd_ep_miid;
   read_data_ptr->port_info.ctrl_cfg.data_link_ps.rd_state = wait_for_ipc_read_data_done_evnt;

   read_data_ptr->db_obj.mem_gaurd_header.rw_client_miid = rd_client_miid;
   read_data_ptr->db_obj.mem_gaurd_header.rw_ep_miid     = sat_rd_ep_miid;
   read_data_ptr->db_obj.mem_gaurd_header.sdm_port_index = port_index;
   read_data_ptr->db_obj.mem_gaurd_header.log_id         = spgm_ptr->sgm_id.log_id;

   *rd_ctrl_cfg_ptr = &read_data_ptr->port_info.ctrl_cfg;

   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_HIGH_PRIO,
               "rd_port_init: create done with ext_outport_bitmask (0x%lx) sat_rw_bit_mask (0x%lx) "
               "rw_client_miid (0x%lx) rw_ep_miid (0x%lx) ",
               ext_outport_bitmask,
               bit_mask,
               rd_client_miid,
               sat_rd_ep_miid);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to destroy the read data port */
ar_result_t sgm_destroy_rd_data_port(spgm_info_t *spgm_ptr, uint32_t port_index)

{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t              log_id          = 0;
   uint32_t              bit_mask        = 0;
   read_data_port_obj_t *rd_port_obj_ptr = NULL;
   spf_handle_t *        handle_ptr      = NULL;

   // Validate the input arguments
   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "rd_port_deinit: destroy port start");

   VERIFY(result, (SPDM_MAX_IO_PORTS > port_index));

   rd_port_obj_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
   VERIFY(result, (NULL != rd_port_obj_ptr));

   // port handle and bit mask
   handle_ptr = &rd_port_obj_ptr->port_info.this_handle;
   bit_mask   = rd_port_obj_ptr->port_info.ctrl_cfg.sat_rw_bit_mask;

   __gpr_cmd_deregister(rd_port_obj_ptr->port_info.ctrl_cfg.rw_client_miid);

   // release the bit mask and destroy the queue
   if (handle_ptr->q_ptr)
   {
      {
         /*Release mask only in Buffer driven mode*/
         cu_release_bit_in_bit_mask(spgm_ptr->cu_ptr, bit_mask);
      }

      /*deinit the queue */
      posal_queue_deinit(handle_ptr->q_ptr);
      handle_ptr->q_ptr = NULL;
   }

   // remove any pending data nodes from the shared data pool
   result |= spdm_remove_all_node_from_data_pool(spgm_ptr,
                                                 &rd_port_obj_ptr->db_obj.buf_pool,
                                                 rd_port_obj_ptr->db_obj.buf_pool.num_data_buf_in_list);

   // Free the read data port object
   posal_memory_free(rd_port_obj_ptr);
   spgm_ptr->process_info.rdp_obj_ptr[port_index] = NULL;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "rd_port_deinit: destroy port done, result %lu", result);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to handle the write data port response messages from the Write EP MIID in the Satellite Graph commands*/
static ar_result_t spdm_wr_data_port_msg_handler(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;

   posal_queue_t *        data_q_ptr     = NULL;
   spf_msg_t *            data_q_msg     = NULL;
   write_data_port_obj_t *write_data_ptr = NULL;
   gpr_packet_t *         packet_ptr     = NULL;

   // Validate the input arguments
   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (SPDM_MAX_IO_PORTS > port_index));

#if SGM_ENABLE_WRITE_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_LOW_PRIO, "wd_port_msg_h : processing write queue element");
#endif

   // Get the pointer to the queue and the msg

   write_data_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
   VERIFY(result, (NULL != write_data_ptr));
   data_q_ptr = write_data_ptr->port_info.this_handle.q_ptr;
   data_q_msg = &write_data_ptr->port_info.input_buf_q_msg;

   VERIFY(result, (NULL != data_q_ptr));
   // POP the message from the queue.
   TRY(result, posal_queue_pop_front(data_q_ptr, (posal_queue_element_t *)(data_q_msg)));

   packet_ptr = (gpr_packet_t *)data_q_msg->payload_ptr;
   VERIFY(result, (NULL != packet_ptr));

   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_MED_PRIO,
               "wd_port_msg_h : processing cmd opcode (%lX) token (%lx), pkt_ptr 0x%lx",
               packet_ptr->opcode,
               packet_ptr->token,
               packet_ptr);

   switch (packet_ptr->opcode)
   {
      // Write DONE
      case DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2:
      {
         TRY(result, spdm_process_data_write_done(spgm_ptr, packet_ptr, port_index));
         break;
      }
      default:
      {
         OLC_SDM_MSG(OLC_SDM_ID,
                     DBG_ERROR_PRIO,
                     "wd_port_msg_h: unsupported cmd_rsp with cmd opcode (%lX) token (%lx)",
                     packet_ptr->opcode,
                     packet_ptr->token);

         THROW(result, AR_EUNSUPPORTED);
      }
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   if (NULL != packet_ptr)
   {
      __gpr_cmd_free(packet_ptr);
   }
   return result;
}

/* Function to handle the write data responses from the Satellite graph
 */
static ar_result_t spdm_wr_dataQ_handler(cu_base_t *cu_ptr, uint32_t channel_bit_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id     = 0;
   uint32_t port_index = 0;

   spgm_info_t *spgm_ptr = NULL;
   uint8_t *    base_ptr = NULL;

   VERIFY(result, (NULL != cu_ptr));

   base_ptr = (uint8_t *)cu_ptr;
   spgm_ptr = (spgm_info_t *)(base_ptr + sizeof(cu_base_t));
   log_id   = spgm_ptr->sgm_id.log_id;

   // get the write data port index given the channel_bit_index of the corresponding data Queue
   TRY(result, sgm_get_data_port_index_given_bit_index(spgm_ptr, IPC_WRITE_DATA, channel_bit_index, &port_index))

   // Function to handle the message from the satellite Graph
   TRY(result, spdm_wr_data_port_msg_handler(spgm_ptr, port_index));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/*
 * function to create an instance of the write data port. Write data port is created
 * to send the data from the upstream container in master PD to the satellite graph.
 * The Port handle  would store all the information to associate
 * the write client miid, write EP miid and the corresponding port index.
 */
static ar_result_t sgm_create_write_data_port(spgm_info_t *spgm_ptr, uint32_t *data_port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t               log_id          = 0;
   uint32_t               port_index      = 0;
   write_data_port_obj_t *wd_port_obj_ptr = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "wr_port_init: port create, (ignore port index)");

   // find the available port index. Check for port object which is NULL
   for (port_index = 0; port_index < SPDM_MAX_IO_PORTS; port_index++)
   {
      if (NULL == spgm_ptr->process_info.wdp_obj_ptr[port_index])
      {
         break;
      }
   }

   if (SPDM_MAX_IO_PORTS == port_index)
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "wr_port_init: number of input port connections reached max");
      THROW(result, AR_ENORESOURCE);
   }

   // allocate the memory for the write port object
   wd_port_obj_ptr = (write_data_port_obj_t *)posal_memory_malloc((sizeof(write_data_port_obj_t)),
                                                                  (POSAL_HEAP_ID)spgm_ptr->cu_ptr->heap_id);

   if (NULL == wd_port_obj_ptr)
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "wr_port_init: creating write data port, failed to allocate memory for the port object");
      THROW(result, AR_ENOMEMORY);
      // Failed to create the write data port, as memory is not available
   }
   memset(wd_port_obj_ptr, 0, sizeof(write_data_port_obj_t));
   spgm_ptr->process_info.wdp_obj_ptr[port_index] = wd_port_obj_ptr;
   *data_port_index                               = port_index;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "wr_port_init: port create done");

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/*
 * function to create the write data queue.
 * All the  write done packet will be received by this queue
 * The queue is created per write data port
 */
ar_result_t sgm_create_wr_data_queue(cu_base_t *            cu_ptr,
                                     spgm_info_t *          spgm_ptr,
                                     uint32_t               ext_inport_bitmask,
                                     uint32_t               rw_client_miid,
                                     void *                 dest,
                                     sdm_data_port_info_t **rw_ctrl_cfg_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t               log_id           = 0;
   uint32_t               data_queue_index = 0;
   uint32_t               bit_mask         = 0;
   uint32_t               num_elements     = 0;
   uint32_t               port_index       = 0;
   uint32_t               sat_rw_ep_miid   = 0;
   write_data_port_obj_t *wd_port_obj_ptr  = NULL;
   char                   sdm_wr_data_q_name[POSAL_DEFAULT_NAME_LEN]; // data queue name

   VERIFY(result, (NULL != cu_ptr));
   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "wr_port_init: create queue, (ignore port index)");

   // Create the read data port
   TRY(result, sgm_create_write_data_port(spgm_ptr, &port_index));
   // Get the satellite Read EP MIID for this port, given the Read Client MIID
   TRY(result, sgm_get_rw_ep_miid_given_rw_client_miid(spgm_ptr, rw_client_miid, &sat_rw_ep_miid, IPC_WRITE_DATA));

   wd_port_obj_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
   VERIFY(result, (NULL != wd_port_obj_ptr));

   // determine the queue index
   data_queue_index = ((spgm_ptr->sgm_id.log_id) | ((port_index) << PORT_INDEX_SHIFT_FACTOR));
   snprintf(sdm_wr_data_q_name, POSAL_DEFAULT_NAME_LEN, "%s%s%8lX", "WD", "SGM", data_queue_index);

   num_elements = SDM_MAX_DATA_Q_ELEMENTS;
   bit_mask     = cu_request_bit_in_bit_mask(&cu_ptr->available_bit_mask);

   // Failed to get valid bit mask for the read port. return fail code
   if (0 == bit_mask)
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "wr_port_init: creating write data queue, bit mask has no bits available 0x%lx",
                  cu_ptr->available_bit_mask);

      return AR_ENORESOURCE;
   }
   else
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_HIGH_PRIO,
                  "wr_port_init: creating write data queue, with bit mask input 0x%08lx",
                  bit_mask);
   }

   // init the write data port queue
   if (AR_EOK != (result = cu_init_queue(cu_ptr,
                                         sdm_wr_data_q_name,
                                         num_elements,
                                         bit_mask,
                                         spdm_wr_dataQ_handler,
                                         cu_ptr->channel_ptr,
                                         &wd_port_obj_ptr->port_info.this_handle.q_ptr,
                                         dest,
                                         cu_ptr->heap_id)))
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "wr_port_init: failed to init write data queue with bit mask input 0x%08lx ",
                  bit_mask);

      return result;
   }

   // remove the bit field from the available bit mask
   cu_ptr->available_bit_mask &= (~(bit_mask));
   cu_ptr->curr_chan_mask |= ((bit_mask));

   // populate the port control configuration
   wd_port_obj_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask = ext_inport_bitmask;
   wd_port_obj_ptr->port_info.ctrl_cfg.sat_rw_bit_mask       = bit_mask;
   wd_port_obj_ptr->port_info.ctrl_cfg.sdm_port_index        = port_index;
   wd_port_obj_ptr->port_info.ctrl_cfg.rw_client_miid        = rw_client_miid;
   wd_port_obj_ptr->port_info.ctrl_cfg.rw_ep_miid            = sat_rw_ep_miid;
   wd_port_obj_ptr->port_info.ctrl_cfg.data_link_ps.wr_state = wait_for_ext_in_port_data;

   wd_port_obj_ptr->db_obj.mem_gaurd_header.rw_client_miid = rw_client_miid;
   wd_port_obj_ptr->db_obj.mem_gaurd_header.rw_ep_miid     = sat_rw_ep_miid;
   wd_port_obj_ptr->db_obj.mem_gaurd_header.sdm_port_index = port_index;
   wd_port_obj_ptr->db_obj.mem_gaurd_header.log_id         = spgm_ptr->sgm_id.log_id;

   *rw_ctrl_cfg_ptr = &wd_port_obj_ptr->port_info.ctrl_cfg;

   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_HIGH_PRIO,
               "wr_port_init: create done with ext_inport_bitmask (0x%lx) sat_rw_bit_mask (0x%lx) "
               "rw_client_miid (0x%lx) rw_ep_miid (0x%lx) ",
               ext_inport_bitmask,
               bit_mask,
               rw_client_miid,
               sat_rw_ep_miid);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to destroy the write data port */
ar_result_t sgm_destroy_wr_data_port(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t               log_id          = 0;
   uint32_t               bit_mask        = 0;
   write_data_port_obj_t *wd_port_obj_ptr = NULL;
   spf_handle_t *         handle_ptr      = NULL;

   // Validate the input arguments
   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "wr_port_deinit: destroy port start");

   VERIFY(result, (SPDM_MAX_IO_PORTS > port_index));

   wd_port_obj_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
   VERIFY(result, (NULL != wd_port_obj_ptr));

   handle_ptr = &wd_port_obj_ptr->port_info.this_handle;
   bit_mask   = wd_port_obj_ptr->port_info.ctrl_cfg.sat_rw_bit_mask;

   __gpr_cmd_deregister(wd_port_obj_ptr->port_info.ctrl_cfg.rw_client_miid);

   // release the bit mask and destroy the queue
   if (handle_ptr->q_ptr)
   {
      {
         /*Release mask only in Buffer driven mode*/
         cu_release_bit_in_bit_mask(spgm_ptr->cu_ptr, bit_mask);
      }

      /*Deinit the queue */
      posal_queue_deinit(handle_ptr->q_ptr);
      handle_ptr->q_ptr = NULL;
   }

   // remove any pending data nodes from the shared data pool
   result |= spdm_remove_all_node_from_data_pool(spgm_ptr,
                                                 &wd_port_obj_ptr->db_obj.buf_pool,
                                                 wd_port_obj_ptr->db_obj.buf_pool.num_data_buf_in_list);

   // Free the write data port object
   posal_memory_free(wd_port_obj_ptr);
   spgm_ptr->process_info.wdp_obj_ptr[port_index] = NULL;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "wr_port_deinit: destroy port done, result %lu", result);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}
