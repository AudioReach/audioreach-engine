/**
 * \file sdm.h
 *  
 * \brief
 *  
 *     header file for driver functionality of OLC
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef SDM_H
#define SDM_H

#include "olc_cmn_utils.h"
#include "container_utils.h"
#include "wr_sh_mem_ep_api.h"
#include "rd_sh_mem_ep_api.h"
#include "wr_sh_mem_ep_ext_api.h"
#include "rd_sh_mem_ep_ext_api.h"
#include "media_fmt_extn_api.h"
#include "sprm.h"
#include "gen_topo.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/* =======================================================================
OLC Structure Definitions
========================================================================== */

/* SDM data type */
typedef enum {
   IPC_WRITE_DATA = 0, // Indicates write data port : sends data to Satellite DSP
   IPC_READ_DATA       // Indicates read data port : Receives data from Satellite DSP
} sdm_ipc_data_type_t;

/* SGM source */
typedef enum {
   SGM_SOURCE_PORT_WRITE_CLIENT = 0, // write IPC source
   SGM_SOURCE_PORT_READ_CLIENT  = 1, // Read IPC source
   SGM_SOURCE_PORT_CONTAINER    = 2, // Container IPC source
   SGM_SOURCE_PORT_TYPE_MAX     = 3
} sgm_source_port_t;

/* SGM sink */
typedef enum {
   SGM_SINK_PORT_WRITE_EP  = 0, // write IPC source
   SGM_SINK_PORT_READ_EP   = 1, // Read IPC source
   SGM_SINK_PORT_CONTAINER = 2, // Container IPC source
   SGM_SINK_PORT_TYPE_MAX  = 3
} sgm_sink_port_t;

/* Write data wait states, help to indicate the mask that needs to be enabled */
typedef enum
{
   wait_for_ext_in_port_data         = 0,
   wait_for_ipc_write_data_done_evnt = 1,
   hold_processing_input_queues      = 2
} write_ipc_data_link_process_state_t;

/* Read data wait states, help to indicate the mask that needs to be enabled */
typedef enum
{
   wait_for_ext_out_port_buf        = 0,
   wait_for_ipc_read_data_done_evnt = 1,
   hold_processing_output_queues    = 2
} read_ipc_data_link_process_state_t;

/* SDM connection type */
typedef enum {
   OLC_IPC_WRITE_CLIENT_CONN = 0, // src in OLC, dst in satellite
   OLC_IPC_READ_CLIENT_CONN  = 1, // src in satellite, dst in OLC
   OLC_IPC_MAX_TYPE_CONN     = 2
} spdm_ipc_conn_type_t;

typedef struct sgm_port_reg_event_info_t
{
   uint32_t          event_id;
   sgm_source_port_t src_port;
   sgm_sink_port_t   sink_port;
} sgm_port_reg_event_info_t;

typedef struct rw_data_port_event_cfg_t
{
   apm_cmd_header_t             header;
   apm_module_register_events_t reg_evt;
} rw_data_port_event_cfg_t;

typedef struct conn_node_info_t
{
   bool_t               is_node_used;
   spdm_ipc_conn_type_t conn_type;
   uint32_t             dynamic_conn_id;
   uint32_t             src_mod_inst_id;
   uint32_t             dst_mod_inst_id;
} conn_node_info_t;

typedef struct spgm_ipc_conn_info_t
{
   uint16_t         num_data_wr_conn;
   uint16_t         num_data_rd_conn;
   conn_node_info_t ipc_rw_conn_list[OLC_IPC_MAX_TYPE_CONN][SPDM_MAX_IO_PORTS];
} spgm_ipc_conn_info_t;

/* common object to to be filled before calling the GPR send functions
 * Read and Write process would fill the details and call the GPR data send funtion */
typedef struct spgm_ipc_data_obj_t
{
   uint32_t opcode;
   /**< opcode of the data packet */

   uint32_t token;
   /**< unique token of the data packet */

   uint32_t src_port;
   /**< source port of the data packet */

   uint32_t dst_port;
   /**< Destination port of the data packet */

   uint32_t payload_size;
   /**< Payload size of the data message */

   uint8_t *payload_ptr;
   /**< pointer to the data message */

} spgm_ipc_data_obj_t;

/* Structure for the Read EP buffer payload structure  */
typedef struct rd_ep_data_header_t
{
   data_cmd_rd_sh_mem_ep_data_buffer_v2_t read_data;
   /**< Read EP buffer payload */
} rd_ep_data_header_t;

/* Structure for the Write EP buffer payload structure  */
typedef struct wr_ep_data_header_t
{
   data_cmd_wr_sh_mem_ep_data_buffer_v2_t write_data;
   /**< Write EP buffer payload */

} wr_ep_data_header_t;

/* Union for the Read Write EP buffer payload structure  */
typedef union rw_ep_data_header_t
{
   wr_ep_data_header_t write_data;
   /**< Write EP buffer payload */

   rd_ep_data_header_t read_data;
   /**< Read EP buffer payload */

} rw_ep_data_header_t;

/* Union for the Read Write EP buffer done payload structure  */
typedef union rw_ep_data_done_header_t
{
   data_cmd_rsp_wr_sh_mem_ep_data_buffer_done_v2_t write_data_done;
   /**< EP Write done buffer payload */

   data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2_t read_data_done;
   /**< EP Read done buffer payload */

} rw_ep_data_done_header_t;

typedef struct rw_meta_data_info_t
{
   uint32_t num_md_elements;
   /**< number of metadata elements */

   uint32_t metadata_buf_size;
   /**< Metadata buffer size. The metadata would follow the data buffer */
} rw_meta_data_info_t;

/* Local olc buffer */
typedef struct olc_buf_t
{
   uint8_t *data_ptr;
   uint32_t actual_data_len;
   uint32_t max_data_len;
   uint32_t mem_map_handle;
} olc_buf_t;

/* Data buffer structure to store the
 * buffer details of external input or output buffer*/
typedef struct sdm_cnt_ext_data_buf_t
{
   olc_buf_t data_buf;

   uint32_t offset;
   /**< points to memory offset to read or write data */

   gen_topo_timestamp_t *buf_ts;

   module_cmn_md_list_t **md_list_pptr;

   module_cmn_md_eos_flags_t eos_flags;

} sdm_cnt_ext_data_buf_t;

/* Structure for the guard header */
typedef struct sdm_data_port_guard_header_info_t
{
   uint32_t log_id;
   /** < specifies the container log_id */

   uint32_t sdm_port_index;
   /**< port index*/

   uint32_t rw_client_miid;
   /**< module instance of the rw client module in OLC core*/

   uint32_t rw_ep_miid;
   /**< module instance of the rw EP module in the satellite graph*/

} sdm_data_port_guard_header_info_t;

/* Data buffer pool node structure */
typedef struct data_buf_pool_node_t
{
   uint32_t data_buf_size;
   /**< Total buffer size for all channel*/

   uint32_t offset;
   /**< Size of the buffer filled for Write buffer.
    * Size of the buffer to consume for the read buffer */

   uint32_t token;
   /**< token to communicate with satellite graph service */

   bool_t buf_in_use;
   /**< Flag set when buffer in use*/

   // bool_t is_deprecated;
   /**< flag set if buffer needs to be deprecated */

   bool_t pending_alloc;
   /**< flag set if buffer was in use and would need to marked for reallocation. */

   uint32_t meta_data_buf_size;
   /** < shared memory size allocated to handle the incoming meta data */

   rw_meta_data_info_t rw_md_data_info;

   rw_ep_data_done_header_t data_done_header;
   /** < read write data done header */

   gen_topo_timestamp_t inbuf_ts;
   /** < Timestamp of the data buffer */

   sgm_shmem_handle_t ipc_data_buf;
   /**< shared memory buffer */

} data_buf_pool_node_t;

/* Structure for the Shmem Data buffer pool used
 *  for Read Write processing*/
typedef struct shmem_data_buf_pool_t
{
   uint32_t port_index;
   /** < port index for which the buf pool is assigned */

   sdm_ipc_data_type_t data_type;
   /** < indicate whether the pool is allocated for read or write */

   uint32_t buf_size;
   /** < size of the buffer, if the pool buffer size mismatches, it needs to be deprecated */

   uint32_t req_num_data_buf;
   /**< Number of port data buffer */

   uint32_t num_data_buf_in_list;
   /**< Number of port data buffer */

   uint32_t num_valid_data_buf_in_list;
   /**< Number of port data buffer in the list with the required size */

   spf_list_node_t *port_db_list_ptr;
   /**< SDM port data buffer list of type data_buf_pool_node_t*/

} shmem_data_buf_pool_t;

/*  base database structure for read/write data ports */
typedef struct data_port_obj_t
{
   shmem_data_buf_pool_t buf_pool;
   /** < shared memory data pool */

   sdm_cnt_ext_data_buf_t data_buf;
   /** < external container port buffer information */

   rw_ep_data_header_t data_header;
   /** < read write data cmd header */

   data_buf_pool_node_t *active_buf_node_ptr;
   /** < pointer to buffer being processing in the data pool */

   sdm_data_port_guard_header_info_t mem_gaurd_header;

} data_port_obj_t;

/* Union for the Read Write EP buffer payload structure  */
typedef union ipc_data_link_process_state_t
{
   write_ipc_data_link_process_state_t wr_state;
   /**< Write data link process state */

   read_ipc_data_link_process_state_t rd_state;
   /**< Read data link process state */

} ipc_data_link_process_state_t;

/* Structure for the Write Data Port */
typedef struct sdm_data_port_info_t
{
   uint32_t sat_rw_bit_mask;
   /** < Bit mask of the read/write client data queue for this port */

   uint32_t cnt_ext_port_bit_mask;
   /**< Bitmask of the associated external port in OLC Core */

   uint32_t sdm_port_index;
   /**< port index*/

   uint32_t rw_client_miid; // todo : redundant // miid
   /**< module instance of the rw client module in OLC core*/

   uint32_t rw_ep_miid;
   /**< module instance of the rw EP module in the satellite graph*/

   uint32_t sat_rd_ep_opfs_bytes;
   /**< operating frame-size of the rd_ep in the satellite graph */

   uint32_t buf_cnt;

   ipc_data_link_process_state_t data_link_ps;

} sdm_data_port_info_t;


/* Structure for the Write Data Port */
typedef struct sdm_write_port_t
{
   spf_handle_t this_handle;
   /**< Write data port's handle. must be first element */

   spf_msg_t input_buf_q_msg;

   sdm_data_port_info_t ctrl_cfg;

} sdm_write_port_t;

/* Structure for the Read Data Port */
typedef struct sdm_read_port_t
{
   spf_handle_t this_handle;
   /**< This external port's handle. must be first element */

   spf_msg_t output_data_q_msg;

   sdm_data_port_info_t ctrl_cfg;

} sdm_read_port_t;

/* Structure for the write data port*/
typedef struct write_data_port_obj_t
{
   data_port_obj_t db_obj;
   /** < data port object */

   sdm_write_port_t port_info;
   /** < write port information */

} write_data_port_obj_t;

/* Structure for the read data port*/
typedef struct read_data_port_obj_t
{
   data_port_obj_t db_obj;
   /** < data port object */

   sdm_read_port_t port_info;
   /** < Read port information */

   uint8_t *output_media_fmt_ptr;

   bool_t media_format_changed;

} read_data_port_obj_t;

typedef struct sdm_tracking_md_node_t
{
   uint32_t num_ref_count;
   /**< number of clone for the metadata created in the */

   uint32_t max_ref_count;
      /**< number of clone for the metadata created in the */

   module_cmn_md_list_t *md_ptr;
   /**< list of tracking metadata*/

} sdm_tracking_md_node_t;

typedef struct sdm_md_tracking_list_t
{
   uint32_t num_tracking_md_elemenet;
   /**< Number of tracking metadata elements in the list*/

   spf_list_node_t *md_list_ptr;
   /**< list of tracking metadata of type sdm_tracking_md_node_t*/

} sdm_md_tracking_list_t;

// OLC_CA : generalize max links
typedef struct sdm_process_info_t
{
   write_data_port_obj_t *wdp_obj_ptr[SPDM_MAX_IO_PORTS];
   /** < array of write port objects */

   read_data_port_obj_t *rdp_obj_ptr[SPDM_MAX_IO_PORTS];
   /** < array of read port objects */

   spgm_ipc_data_obj_t active_data_hndl;

   /**< Data pool for read and write shared memory */
   spgm_ipc_conn_info_t data_conn_info;

   /** < tracking metadata list */
   sdm_md_tracking_list_t tr_md;

} sdm_process_info_t;

ar_result_t sgm_get_data_port_index_given_rw_ep_miid(spgm_info_t *       spgm_ptr,
                                                     sdm_ipc_data_type_t data_type,
                                                     uint32_t            rw_ep_miid,
                                                     uint32_t *          port_index_ptr);

ar_result_t sgm_get_data_port_index_given_rw_client_miid(spgm_info_t *       spgm_ptr,
                                                         sdm_ipc_data_type_t data_type,
                                                         uint32_t            rw_client_miid,
                                                         uint32_t *          port_index_ptr);

ar_result_t spdm_write_dl_pcd(spgm_info_t *spgm_ptr, uint32_t port_index);
ar_result_t spdm_read_dl_pcd(spgm_info_t *spgm_ptr, uint32_t port_index);
ar_result_t spdm_process_media_format_event(spgm_info_t * spgm_ptr,
                                            gpr_packet_t *packet_ptr,
                                            uint32_t      port_index,
                                            bool_t        is_data_path);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef SDM_H
