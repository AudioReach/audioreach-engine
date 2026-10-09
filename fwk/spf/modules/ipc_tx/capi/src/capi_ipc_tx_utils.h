/* ======================================================================== */
/**
*\file capi_ipc_tx_utils.h

   Header file to implement the ipc_tx block
*/

/* =========================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
  ========================================================================== */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     -------------------------------------

   ========================================================================= */

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/

#ifndef CAPI_IPC_TX_UTILS_H
#define CAPI_IPC_TX_UTILS_H

#include "ipc_tx_rx_api.h"
#include "capi_ipc_tx.h"
#include "gpr_api_inline.h"
#include "capi_cmn.h"
#include "graph_utils.h"
#include "spf_interleaver.h"
#include "apm_offload_mem.h"
#include "spf_sys_util.h"
#include "offload_metadata_api.h"
#include "module_cmn_metadata.h"
#include "topo_utils.h"
#include "wr_sh_mem_ep_api.h"
#include "capi_intf_extn_module_buffer_access.h"
#include "spf_inter_proc_md_utils.h"

#define CAPI_IPC_TX_BW (1 * 1024);

#define CAPI_IPC_TX_KPPS (30)

/* debug message */
#define MIID_UNKNOWN 0
#define IPC_TX_MSG_PREFIX "CAPI IPC_TX:[%lX] "
#define IPC_TX_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, IPC_TX_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)
#ifndef ALIGN_128_BYTES
#define ALIGN_128_BYTES(a) ((a + 127) & (0xFFFFFF80))
#endif
#define IPC_TX_MD_BUFFER_SIZE 2048
#define IPC_TX_RAW_COMP_BUFFER_SIZE 2048

#define ALIGNMENT_64 64
/* Number of microseconds in a millisecond*/
#define NUM_US_PER_MS 1000

// Enable macros for verbose logging
#define DEBUG_IPC_TX
//#define DEBUG_IPC_TX_SAFE_MODE

/*------------------------------------------------------------------------
 * Function declarations
 * -----------------------------------------------------------------------*/


/** @ingroup ar_spf_mod_data_log_macros
    ID of the parameter used to set configuration for Data logging inside IPC modules. */
#define PARAM_ID_IPC_DATA_LOGGING_CONFIG             0x08001BEE

/*==============================================================================
   Param structure defintions
==============================================================================*/

/** @h2xmlp_parameter   {"PARAM_ID_IPC_DATA_LOGGING_CONFIG", PARAM_ID_IPC_DATA_LOGGING_CONFIG}
   @h2xmlp_description  {Configures the data logging module.\n}
   @h2xmlp_toolPolicy   {Calibration; RTC} */

#include "spf_begin_pack.h"

/** @ingroup ar_spf_mod_data_log_macros
    Configures the data logging module. */
struct param_id_ipc_data_logging_config_t
{
   uint32_t log_code;
   /**< Logging code for this module instance.

        @valuesbul
		- 0 -- Disabled (Default)
   		- 0x152E
		- 0x152F
		- 0x1531
		- 0x1534
		- 0x1532
		- 0x1530
		- 0x1533
        - 0x1535
        - 0x1536
		- 0x1586
        - 0x19AF
	    - 0x19B0
        - 0x19B1
        - 0x158A
		- 0x158B
		*/
   /**< @h2xmle_description {logging code}
        @h2xmle_default     {0}
         @h2xmle_rangeList    {"Default"=0;
                              "0x152E"=0x152E;
							  "0x152F"=0x152F;
							  "0x1531"=0x1531;
							  "0x1534"=0x1534;
							  "0x1532"=0x1532;
							  "0x1530"=0x1530;
							  "0x1533"=0x1533;
							  "0x1535"=0x1535;
							  "0x1536"=0x1536;
							  "0x1586"=0x1586;
							  "0x19AF"=0x19AF;
							  "0x19B0"=0x19B0;
							  "0x19B1"=0x19B1;
							  "0x158A"=0x158A;
							  "0x158B"=0x158B
							  }
        @h2xmle_policy      {Advanced} */
}
#include "spf_end_pack.h"
;
/* Structure type def for above payload. */
typedef struct param_id_ipc_data_logging_config_t param_id_ipc_data_logging_config_t;

typedef struct ipc_tx_md_tracking_list_t
{
   uint32_t num_tracking_md_elemenet;
   /**< Number of tracking metadata elements in the list*/

   spf_list_node_t *md_list_ptr;
   /**< list of tracking metadata of type sdm_tracking_md_node_t*/

} ipc_tx_md_tracking_list_t;

typedef struct client_info_t
{
   uint16_t src_domain_id;
   uint16_t client_domain_id;
   uint16_t client_port;
} client_info_t;

typedef struct frame_length_info_t
{
   bool is_frame_len_received;

   uint32_t frame_dur_us;
   /**< Frame duration in microseconds */

   uint32_t frame_dur_ms;
   /**< Frame duration in milliseconds */

   uint32_t frame_len_bytes;
   /**< Frame duration in bytes */
} frame_length_info_t;

typedef enum
{
   IPC_BUF_IN_USE,
   IPC_BUF_READY_TO_DESTROY
} ipc_buf_status_t;

// Structure for shared memory handle for every allocation from the mdf loaned memory pool
typedef struct shmem_handle_t
{
   ipc_buf_status_t status;
   /* Buffer status */
   apm_offload_ret_info_t mem_attr;
   /*  memory attributes : master & satellite mem handle, memory offset */
   uint32_t shm_alloc_size;
   /* shared memory size */
   void *shm_mem_ptr;
   /* virtual address pointer */
} shmem_handle_t;

/* Capi input port state structure */
typedef struct ipc_tx_input_port_info_t
{
   uint32_t port_id;
   /**< ID of the input port */

   uint32_t port_index;
   /**< Index of the input port */

   intf_extn_data_port_state_t port_state;
   /**< State of the input port */

   bool_t is_media_fmt_received;
   /**< Flag indicating whether media format has been received */

   module_cmn_md_list_t *md_list_ptr;
   /**< Pointer to the list of metadata for the input port */

} ipc_tx_input_port_info_t;

/* Capi output port state structure */
typedef struct ipc_tx_output_port_info_t
{
   uint32_t port_id;
   /**< ID of the output port */

   uint32_t port_index;
   /**< Index of the output port */

   intf_extn_data_port_state_t port_state;
   /**< State of the output port */

} ipc_tx_output_port_info_t;

// Structure to represent data buffer information
typedef struct data_buf_info_t
{
   uint32_t num_ipc_bufs_needed;
   /**< Number of ipc data buffer count which is some of regular and prebuffers. There are total numbers of buffers
    * created and pushed to the output queue*/

   uint32_t num_ipc_prebufs_needed_to_send;
   /**< Number of ipc data Pre buffers worth of zero buffers that needs to pushed at the time of data flow start.*/

   uint32_t num_ipc_bufs_created;
   /**< Number of ipc data buffers created at the moment */

   uint32_t num_valid_data_buf;
   /**< Number of port data buffer in the module with the required size */

   uint32_t num_bufs_to_destroy;
   /**< Number of ipc data buffers to be destroyed */

   uint32_t num_data_buf_ack_rcvd;
   /**< Number of ipc data buffers acknowledged */

   uint32_t shmem_alloc_size;
   /**< Size of the shared memory buffer including data and metadata*/

   uint16_t frame_size_in_bytes;
   uint16_t frame_size_per_ch_in_bytes;
   /**< Size of the data buffer */

   uint32_t data_buff_size;
   /**< Size of the data buffer */

   uint32_t metadata_buff_size;
   /**< Size of the metadata buffer */

   uint32_t actual_data_filled;
   /**< total data filled in the data buffer in bytes*/

   uint32_t actual_metadata_filled;
   /**< total metadata filled in the metadata buffer */

   uint32_t data_buff_offset;
   /**< Offset of the data buffer */

   uint32_t metadata_buff_offset;
   /**< Offset of the metadata buffer */

   shmem_handle_t ipc_sh_mem_info;
   /**< Shared memory handle for the data buffer */

   shmem_handle_t *shared_mem_buf_handle;
   /**< Shared memory handle for the data buffers */

   uint8_t *curr_buff;
   /**< Current buffer pointer */

   uint32_t curr_buff_index; // todo_mdf: remove this, '0' will be considered valid even though the buffer is not present
                             // with TX module. need to only use 'curr_buff' every where
   /**< Current buffer index that is received from the fwk*/

   data_cmd_rsp_wr_sh_mem_ep_data_buffer_done_v2_t write_done_payload;
   /**< Write done payload of the last returned buffer*/

   int8_t *overrun_buffer_ptr;
   uint32_t overrun_buffer_size;

   bool_t   is_prebuffers_sent;
   uint16_t num_pending_prebuffers;
   uint8_t *pending_prebuf_index_arr;

} data_buf_info_t;

typedef struct ipc_tx_media_fmt_t
{
   capi_media_fmt_v2_t inp_media_fmt;

   uint32_t cached_media_fmt_actual_size;
} ipc_tx_media_fmt_t;

typedef struct ipc_tx_logging_info_t
{
   uint32_t seq_number;
   uint32_t session_id;
   param_id_ipc_data_logging_config_t cfg;
} ipc_tx_logging_info_t;

// Structure to represent the CAPI IPC_TX object
typedef struct capi_ipc_tx_t
{
   capi_t                     vtbl;
   capi_event_callback_info_t cb_info;
   capi_heap_id_t             heap_info;
   capi_port_num_info_t       num_port_info;

   bool is_ds_frame_len_received;
   /**< Flag indicating whether downstream frame length is received */

   bool is_inp_media_fmt_received;
   /**< Flag indicating whether input media format is received */

   bool is_inp_media_fmt_pending;
   /**< Flag indicating whether input media format is pending */

   bool is_num_bufs_received;

   uint32_t base_token;

   fwk_extn_param_id_ipc_buffer_info_t ipc_buffer_info;
   /**< IPC buffer count information */

   ipc_frame_length_t downstream_fm_len;
   /**< Frame length information for downstream container */

   capi_media_fmt_v2_t inp_media_fmt;

   uint32_t inp_media_fmt_size;
   // ipc_tx_media_fmt_t pending_media_fmt;
   // /**< Pending input media format */

   // ipc_tx_media_fmt_t input_media_fmt;
   // /**< Input media format */

   // capi_media_fmt_v2_t output_media_fmt;
   // /**< Output media format */

   frame_length_info_t frame_length_info;
   /**< Frame length information of the self container*/

   client_info_t ipc_client_info;
   /**< Client information for IPC */

   data_buf_info_t sh_buf_info;
   /**< Shared ipc data buffer information */

   ipc_data_link_info_per_port_t ipc_tx_link_info;
   /**< IPC link information w.r.t. PARAM_ID_IPC_DATA_LINK_INFO*/

   fwk_extn_ipc_port_trigger_t output_trigger_info;
   /**< Output trigger information */

   intf_extn_param_id_metadata_handler_t metadata_handler;
   /**< Metadata handler object */

   ipc_tx_md_tracking_list_t tracking_md;
   /**< Tracking metadata list */

   bool_t need_to_overrun;

   /* Port info */
   ipc_tx_input_port_info_t  in_port_info[IPC_TX_MAX_INPUT_PORTS];
   ipc_tx_output_port_info_t out_port_info[IPC_TX_MAX_OUTPUT_PORTS];

   uint32_t miid;

   bool_t is_mod_buf_access_enabled;
   int8_t * curr_shared_buf_ptr;

   ipc_tx_logging_info_t logging_info;
} capi_ipc_tx_t;

capi_err_t capi_ipc_tx_process_set_properties(capi_ipc_tx_t *me_ptr, capi_proplist_t *proplist_ptr);

capi_err_t capi_ipc_tx_process_get_properties(capi_ipc_tx_t *me_ptr, capi_proplist_t *proplist_ptr);

capi_err_t capi_ipc_tx_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

void capi_ipc_tx_init_config(capi_ipc_tx_t *me_ptr);

capi_err_t capi_ipc_tx_raise_event(capi_ipc_tx_t *me_ptr);

// capi_err_t capi_ipc_tx_raise_process_event(capi_ipc_tx_t *me_ptr);

capi_err_t capi_ipc_tx_handle_intf_extn_data_port_operation(capi_ipc_tx_t *me_ptr, capi_buf_t *params_ptr);

capi_err_t capi_ipc_tx_handle_port_open(capi_ipc_tx_t *me_ptr, uint32_t port_id, uint32_t port_index, bool_t is_input);

capi_err_t capi_ipc_tx_handle_port_start(capi_ipc_tx_t *me_ptr, uint32_t port_index, bool_t is_input);

capi_err_t capi_ipc_tx_handle_port_stop(capi_ipc_tx_t *me_ptr, uint32_t port_index, bool_t is_input);

capi_err_t capi_ipc_tx_handle_port_close(capi_ipc_tx_t *me_ptr, uint32_t port_index, bool_t is_input);

void capi_ipc_tx_destroy_input_port(capi_ipc_tx_t *me_ptr, uint32_t port_index);

void capi_ipc_tx_destroy_output_port(capi_ipc_tx_t *me_ptr, uint32_t port_idx);

capi_err_t capi_ipc_tx_process_get_param(capi_t                 *_pif,
                                         uint32_t                param_id,
                                         const capi_port_info_t *port_info_ptr,
                                         capi_buf_t             *params_ptr);

capi_err_t capi_ipc_tx_process_set_param(capi_t                 *_pif,
                                         uint32_t                param_id,
                                         const capi_port_info_t *port_info_ptr,
                                         capi_buf_t             *params_ptr);

// capi_err_t capi_ipc_allocate_sh_buffer(capi_ipc_tx_t *me_ptr, uint32_t data_buffer_size, uint32_t md_buffer_size);

capi_err_t capi_ipc_tx_free_shmem(capi_ipc_tx_t *me_ptr, uint32_t buffer_index);

ar_result_t capi_ipc_tx_fwk_extn_ipc_port_ctrl_msg_handler(void         *capi_ptr,
                                                           uint32_t      is_ext_input,
                                                           uint32_t      port_index,
                                                           gpr_packet_t *pkt_ptr);

ar_result_t capi_ipc_tx_fwk_extn_ipc_port_data_msg_handler(void         *capi_ptr,
                                                           uint32_t      is_ext_input,
                                                           uint32_t      port_index,
                                                           gpr_packet_t *pkt_ptr,
                                                           uint32_t      ipc_data_msg_flag_mask);

capi_err_t capi_ipc_tx_check_recreate_buffer(capi_ipc_tx_t      *me_ptr,
                                             capi_stream_data_t *input[],
                                             uint32_t           *meta_data_size_ptr);

capi_err_t capi_ipc_tx_write_data(capi_ipc_tx_t *me_ptr, capi_stream_data_t *input[]);

ar_result_t ipc_tx_get_write_meta_data_size(capi_ipc_tx_t        *me_ptr,
                                            module_cmn_md_list_t *md_list_ptr,
                                            uint32_t             *req_md_buf_size_ptr);

capi_err_t capi_ipc_tx_drop_all_metadata(capi_ipc_tx_t *me_ptr, capi_stream_data_t *input);

void capi_ipc_tx_convert_int_md_flags_to_client_md_flag(module_cmn_md_flags_t int_md_flags, uint32_t *client_md_flags);

capi_err_t capi_ipc_tx_write_metadata(capi_ipc_tx_t *me_ptr, capi_stream_data_t *input[]);

capi_err_t capi_ipc_tx_create_send_media_fmt(capi_ipc_tx_t *me_ptr,
                                             uint8_t       *payload_ptr,
                                             uint32_t       is_data_path,
                                             uint32_t       write_payload_size);

capi_err_t capi_ipc_tx_update_media_fmt(capi_ipc_tx_t      *me_ptr,
                                        capi_media_fmt_v2_t inp_media_fmt,
                                        uint32_t            param_actual_data_len);

// capi_err_t capi_ipc_tx_update_bufs_num(capi_ipc_tx_t *me_ptr);

capi_err_t capi_ipc_tx_manage_buffer(capi_ipc_tx_t *me_ptr, uint32_t new_data_size, uint32_t new_md_size);

capi_err_t capi_ipc_tx_handle_buffer_reallocation(capi_ipc_tx_t *me_ptr, uint32_t new_shmem_size);
capi_err_t capi_ipc_tx_update_bufs_num(capi_ipc_tx_t *me_ptr);

capi_err_t capi_ipc_tx_check_n_enable_buffer_access_extn(capi_ipc_tx_t *me_ptr);

capi_err_t capi_ipc_tx_intf_extn_get_mod_input_buf(uint32_t    handle,
                                                    uint32_t    port_index,
                                                    uint32_t   *num_bufs,
                                                    capi_buf_t *buffer_ptr);

capi_err_t capi_ipc_tx_intf_extn_return_mod_input_buf(uint32_t    handle,
                                                      uint32_t    port_index,
                                                      uint32_t   *num_bufs_ptr,
                                                      capi_buf_t *buffer_ptr);

#endif // CAPI_IPC_TX_UTILS_H
