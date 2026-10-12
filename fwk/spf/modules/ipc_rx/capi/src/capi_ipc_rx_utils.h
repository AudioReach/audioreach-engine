/*==============================================================================
@file capi_ipc_rx_utils.h
@brief This file contains utility functions and structures for the IPC RX CAPI module.

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/
// clang-format off
// No Edit History for automated refactoring.
// clang-format on

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/

#ifndef CAPI_IPC_RX_UTILS_H
#define CAPI_IPC_RX_UTILS_H

#include "ipc_tx_rx_api.h"
#include "capi_ipc_rx.h"
#include "gpr_api.h"           /* For gpr_packet_t, gpr_cmd_alloc_send_t */
#include "gpr_api_inline.h"
#include "ar_defs.h"          /* For AR_EOK, AR_EBADPARAM, etc. */
#include "ar_msg.h"            /* For AR_MSG macro definitions */
#include "capi_cmn.h"
#include "graph_utils.h"
#include "spf_interleaver.h"
#include "apm_offload_mem.h"
#include "spf_sys_util.h"
#include "offload_metadata_api.h"
#include "module_cmn_metadata.h"
#include "topo_utils.h"
#include "wr_sh_mem_ep_api.h"
#include "apm.h"
#include "spf_inter_proc_md_utils.h"

// Enable macro for verbose logging
#define DEBUG_IPC_RX

/**<
 * \brief Bandwidth in bytes/second required for the IPC_RX module.
 * This value may need to be adjusted based on actual performance requirements.
 *
 *  ToDo: Decide based on IRM profiling and update with actual values.
 */
#define CAPI_IPC_RX_BW (1 * 1024 * 1024)

/**<
 * \brief Kilo-packets per second (KPPS) for the IPC_RX module.
 * This value represents the processing power required by the module.
 *
 *
 * ToDo: Decide based on IRM on SIM and update with actual values.
 */
#define CAPI_IPC_RX_KPPS (1000)

/**<
 * \brief Module Instance ID (MIID) used as a placeholder for debug messages
 * when the actual MIID is not yet available or is unknown.
 */
#define MIID_UNKNOWN 0

/**<
 * \brief Prefix string for all log messages originating from the IPC_RX CAPI module.
 * This helps in easily identifying IPC_RX logs in a system trace.
 */
#define IPC_RX_MSG_PREFIX "CAPI IPC_RX:[%lX] "

/**<
 * \brief Macro for logging debug messages specific to the IPC_RX module.
 * Uses `AR_MSG` for logging with the defined prefix and module instance ID.
 */
#define IPC_RX_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, IPC_RX_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

/**<
 * \brief Default size for internal metadata buffers in the IPC_RX module.
 * This size is typically chosen to accommodate common metadata structures.
 */
#define IPC_RX_MD_BUFFER_SIZE 2048

/**<
 * \brief Default buffer size for handling raw compressed data within the IPC_RX module.
 * This is used for scenarios where the module might directly process compressed streams.
 */
#define IPC_RX_RAW_COMP_BUFFER_SIZE 2048

/**<
 * \brief Number of microseconds in one millisecond (1000).
 * Used for time unit conversions within the module.
 *
 * ToDo: Let's make use of the macro defined in capi_cmn.h
 */
#define NUM_US_PER_MS 1000

/*------------------------------------------------------------------------
 * Type definitions
 * -----------------------------------------------------------------------*/

/** @ingroup ar_spf_mod_data_log_macros
    ID of the parameter used to set configuration for Data logging inside IPC modules. */
#define PARAM_ID_IPC_DATA_LOGGING_CONFIG      0x08001BEE

/*==============================================================================
   Param structure defintions
==============================================================================*/

/** @h2xmlp_parameter   {"PARAM_ID_IPC_DATA_LOGGING_CONFIG", PARAM_ID_IPC_DATA_LOGGING_CONFIG}
   @h2xmlp_description  {Configures the data logging module. \n}
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

typedef struct mem_map_handle_info_t
{
   uint32_t mem_map_handle;
   /**< Unique identifier for the shared memory that corresponds to the data
        buffer address

        The SPF returns this handle through APM_CMD_RSP_SHARED_MEM_MAP_REGIONS
        (see the AudioReach SPF CAPI API Reference (80-VN500-6)). */

   uint64_t mem_map_virtual_addr;
   /**< Virtual address of the mapped memory region. */
} mem_map_handle_info_t;

/**<
 * \brief Structure containing state and information for an IPC_RX input port.
 * This tracks the operational status and specific attributes of each input connection.
 */
typedef struct ipc_rx_input_port_info_t
{
   uint32_t                    port_id;
   /**< Unique identifier for the input port. */
   uint32_t                    port_index;
   /**< Index of the input port (0 for the first, 1 for the second, etc.). */
   intf_extn_data_port_state_t port_state;
   /**< Current operational state of the input port (e.g., open, closed, started). */
   bool_t                      is_media_fmt_received;
   /**< Flag indicating if a media format has been successfully received for this port. */
} ipc_rx_input_port_info_t;

/**<
 * \brief Structure containing state and information for an IPC_RX output port.
 * Similar to the input port structure, this tracks the status of output connections.
 */
typedef struct ipc_rx_output_port_info_t
{
   uint32_t                    port_id;
   /**< Unique identifier for the output port. */
   uint32_t                    port_index;
   /**< Index of the output port. */
   intf_extn_data_port_state_t port_state;
   /**< Current operational state of the output port. */
} ipc_rx_output_port_info_t;

/**<
 * \brief Enumeration defining the possible data flow states within the IPC_RX module.
 * This helps in managing data processing logic based on whether data is currently
 * flowing or if there's a gap (e.g., due to flush or end-of-stream).
 *
 * ToDo: Make these macros unique w.r.to IPC RX
 */
typedef enum ipc_rx_data_flow_state_t
{
   IPC_RX_DFS_AT_GAP  = 0,
   /**< No data is currently flowing through the module. */
   IPC_RX_DFS_FLOWING = 1,
   /**< Data is actively being processed and flowing through the module. */
} ipc_rx_data_flow_state_t;

typedef struct ipc_rx_md_buf_info_t
{
   int8_t *md_buf_virtual_addr;
   /**< Virtual address of the metadata buffer. */
   uint32_t md_mem_map_handle;
   /**< Shared memory handle for the metadata buffer. */
   uint32_t md_buf_size;
   /**< Size of the metadata buffer. */
   uint32_t md_status;
   /**< Status of the metadata buffer processing. */
   module_cmn_md_list_t *md_list_ptr;
} ipc_rx_md_buf_info_t;

typedef struct ipc_rx_logging_info_t
{
   uint32_t seq_number;
   uint32_t session_id;
   param_id_ipc_data_logging_config_t cfg;
} ipc_rx_logging_info_t;

/**<
 * \brief Main structure for the CAPI IPC_RX module instance.
 * This comprehensive structure holds all context, configuration, and state
 * variables necessary for the IPC_RX module's operation. It includes CAPI-specific
 * information, buffer pointers, media format details, IPC communication data,
 * and state flags.
 *
 * ToDo: Try to pack ipc related variables in different structure.
 */
typedef struct capi_ipc_rx_t
{
   capi_t vtbl;
   /**< CAPI function table, required for all CAPI modules. */
   capi_event_callback_info_t cb_info;
   /**< Callback function and context for raising events to the container. */
   capi_heap_id_t heap_info;
   /**< Heap ID used for memory allocations within this module instance. */
   capi_port_num_info_t num_port_info;
   /**< Information about the number of input and output ports. */
   capi_media_fmt_v2_t input_media_fmt;
   /**< Current input media format of the module.
    *
    * ToDo: No need to store this structure */
   capi_media_fmt_v2_t output_media_fmt;
   /**< Current output media format of the module. */
   int8_t *int_buf_ptr;
   /**< Pointer to the internal buffer used for data processing. */

   uint32_t out_buf_size;
   /**< Configured size of the output buffer.
    *
    * ToDo: This can be renamed as configured_frame_size_bytes */
   uint32_t int_buf_actual_data_len;
   /**< Actual amount of data currently present in the internal buffer. */
   uint32_t int_buf_max_data_len;
   uint32_t int_buf_max_data_len_per_buf;
   /**< Maximum capacity of the internal buffer.
    *
    * ToDo: this should be same as out_buf_max_length */
   module_cmn_md_list_t *int_buf_md_list_ptr;
   /**< internal buffer MD list ptr */

   int8_t *ipc_buf_virtual_addr;
   /**< Virtual address of the IPC (inter-processor communication) input buffer. */
   uint32_t ipc_buf_size;
   uint32_t ipc_buf_size_per_buf;
   /**< Total allocated size of the IPC input buffer. */
   uint32_t data_mem_map_handle;
   /**< Shared memory handle for the data buffer. */

   uint32_t data_status;
   /**< Status of the data buffer processing.
    *
    * ToDo: This gets updated when IPC RX could not consume
    * complete data.
    *
    * _status flags can be moved to debug  */

   ipc_rx_md_buf_info_t md_buf_info;
   /**< Metadata info associated with the currently popped IPC data buffer. */

   uint32_t cur_frame_data_len;
   /**< Length of the current frame data in bytes.
    *
    * ToDo: This should be renamed as received_frame_length_bytes */
   uint32_t mf_cmd_pending_data_len;
   /**< Length of the data pending for a media format command in bytes. */
   uint32_t mf_cmd_ack_data_len;
   /**< Length of the data to be acknowledged for a media format command in bytes. */
   uint32_t num_frames_in_pkt;
   /**< Number of frames present in the current IPC packet. */

   gpr_packet_t *ipc_buf_packet_ptr;
   /**< Pointer to the GPR packet associated with the current IPC data buffer. */
   gpr_packet_t *mf_packet_ptr;
   /**< Pointer to the GPR packet holding a pending media format command. */
//   ipc_frame_length_t peer_frame_len;
//   /**< Information about the peer module's frame length. */
   uint32_t ipc_buf_actual_data_len;
   uint32_t ipc_buf_actual_data_len_per_buf;
   /**< Actual amount of valid data in the IPC input buffer. */
   uint32_t ipc_rx_cntr_duration_us;
   /**< Container frame duration in microseconds, received from framework.
    *
    * ToDo: If there is change in frame duration, then drop pending
    * data and reallocate internal buffer and then update trigger
    * info properly.  Add messages for this kind of changes.*/
   bool_t is_pending_mf;
   /**< Flag indicating if a media format change is pending application. */
   bool_t need_to_underrun;
   /**< IPC input buffer is not present, need to underrun */
   ipc_data_link_info_per_port_t ipc_rx_link_info;
   /**< IPC data link information provided by a framework extension. */
   fwk_extn_ipc_port_trigger_t input_trigger_info;
   /**< Specifies when the input port needs to be triggered. */
   ipc_rx_data_flow_state_t dfs;
   /**< Current data flow state of the module. */
   intf_extn_param_id_metadata_handler_t metadata_handler;
   /**< Handler for metadata operations. */
   ipc_rx_input_port_info_t in_port_info[IPC_RX_MAX_INPUT_PORTS];
   /**< Array of input port information structures. */
   ipc_rx_output_port_info_t out_port_info[IPC_RX_MAX_OUTPUT_PORTS];
   /**< Array of output port information structures. */
   mem_map_handle_info_t data_mem_map_hdl_entry;
   /**< Memory map handle entry for data buffer. */
   mem_map_handle_info_t md_mem_map_hdl_entry;
   /**< Memory map handle entry for metadata buffer. */
   uint32_t miid;
   /**< Module instance ID, unique identifier for this module instance. */

   bool_t is_mod_buf_access_enabled;
   /** Indicates if the module buffer access extension is enabled */
   int8_t *curr_shared_buf_ptr;
   /** Currently shared buffer with the fwk*/

   ipc_rx_logging_info_t logging_info;
} capi_ipc_rx_t;

/*------------------------------------------------------------------------
 * Function declarations
 * -----------------------------------------------------------------------*/

/**
 * \brief Sets multiple properties for the IPC_RX CAPI module instance.
 * This is a generic function to set module-level properties based on a property list.
 *
 * \param[in,out] me_ptr      Pointer to the CAPI IPC_RX instance.
 * \param[in] proplist_ptr  Pointer to the property list structure containing properties to set.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_process_set_properties(capi_ipc_rx_t *me_ptr, capi_proplist_t *proplist_ptr);

/**
 * \brief Retrieves multiple properties from the IPC_RX CAPI module instance.
 * This is a generic function to get module-level properties into a property list.
 *
 * \param[in,out] me_ptr      Pointer to the CAPI IPC_RX instance.
 * \param[out] proplist_ptr Pointer to the property list structure to be filled with property values.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_process_get_properties(capi_ipc_rx_t *me_ptr, capi_proplist_t *proplist_ptr);

/**
 * \brief Initializes the configuration parameters for the IPC_RX CAPI module.
 * This function is typically called during module creation to set up initial state.
 *
 * \param[in,out] me_ptr Pointer to the CAPI IPC_RX instance.
 */
void capi_ipc_rx_init_config(capi_ipc_rx_t *me_ptr);


/**
 * \brief Raises a generic event from the IPC_RX CAPI module to its container.
 * This could be used for various module-specific notifications.
 *
 * \param[in,out] me_ptr Pointer to the CAPI IPC_RX instance.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_raise_event(capi_ipc_rx_t *me_ptr);

/**
 * \brief Handles operations related to interface extension for data ports.
 * This includes commands like port open, close, start, stop.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance.
 * \param[in] params_ptr Pointer to the CAPI buffer containing parameters for the data port operation.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_handle_intf_extn_data_port_operation(capi_ipc_rx_t *me_ptr, capi_buf_t *params_ptr);

/**
 * \brief Handles the opening of an input or output port for the IPC_RX module.
 * This function initializes the state for the specified port.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance.
 * \param[in] port_id    The unique ID of the port to open.
 * \param[in] port_index The index of the port (e.g., 0 for the first port).
 * \param[in] is_input   Boolean flag: TRUE if it's an input port, FALSE if an output port.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_handle_port_open(capi_ipc_rx_t *me_ptr, uint32_t port_id, uint32_t port_index, bool_t is_input);

/**
 * \brief Handles the starting of an input or output port for the IPC_RX module.
 * This prepares the port for active data transfer.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance.
 * \param[in] port_index The index of the port to start.
 * \param[in] is_input   Boolean flag: TRUE if it's an input port, FALSE if an output port.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_handle_port_start(capi_ipc_rx_t *me_ptr, uint32_t port_index, bool_t is_input);

/**
 * \brief Handles the stopping of an input or output port for the IPC_RX module.
 * This suspends data transfer on the specified port.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance.
 * \param[in] port_index The index of the port to stop.
 * \param[in] is_input   Boolean flag: TRUE if it's an input port, FALSE if an output port.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_handle_port_stop(capi_ipc_rx_t *me_ptr, uint32_t port_index, bool_t is_input);

/**
 * \brief Handles the closing of an input or output port for the IPC_RX module.
 * This releases resources associated with the specified port.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance.
 * \param[in] port_index The index of the port to close.
 * \param[in] is_input   Boolean flag: TRUE if it's an input port, FALSE if an output port.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_handle_port_close(capi_ipc_rx_t *me_ptr, uint32_t port_index, bool_t is_input);

/**
 * \brief Destroys and cleans up resources for a specific input port of the IPC_RX module.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance.
 * \param[in] port_index The index of the input port to destroy.
 */
void capi_ipc_rx_destroy_input_port(capi_ipc_rx_t *me_ptr, uint32_t port_index);

/**
 * \brief Destroys and cleans up resources for a specific output port of the IPC_RX module.
 *
 * \param[in,out] me_ptr    Pointer to the CAPI IPC_RX instance.
 * \param[in] port_idx The index of the output port to destroy.
 */
void capi_ipc_rx_destroy_output_port(capi_ipc_rx_t *me_ptr, uint32_t port_idx);

/**
 * \brief Processes a request to retrieve a specific parameter from the IPC_RX CAPI module.
 *
 * \param[in,out] _pif           Pointer to the base CAPI instance structure.
 * \param[in] param_id       The ID of the parameter to retrieve.
 * \param[in] port_info_ptr  Optional pointer to port-specific information if the parameter is port-dependent.
 * \param[out] params_ptr     Pointer to a CAPI buffer where the retrieved parameter data will be stored.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_process_get_param(capi_t                 *_pif,
                                         uint32_t                param_id,
                                         const capi_port_info_t *port_info_ptr,
                                         capi_buf_t             *params_ptr);

/**
 * \brief Processes a request to set a specific parameter for the IPC_RX CAPI module.
 *
 * \param[in,out] _pif           Pointer to the base CAPI instance structure.
 * \param[in] param_id       The ID of the parameter to set.
 * \param[in] port_info_ptr  Optional pointer to port-specific information if the parameter is port-dependent.
 * \param[in] params_ptr     Pointer to a CAPI buffer containing the parameter data to set.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_process_set_param(capi_t                 *_pif,
                                         uint32_t                param_id,
                                         const capi_port_info_t *port_info_ptr,
                                         capi_buf_t             *params_ptr);

/**
 * \brief Handles control messages received from the IPC port framework extension.
 * This is a callback function invoked by the framework for IPC control commands.
 *
 * \param[in,out] capi_ptr     Pointer to the CAPI module instance.
 * \param[in] is_ext_input True if the message is for an external input port, FALSE otherwise.
 * \param[in] port_index   The index of the port associated with the message.
 * \param[in] pkt_ptr      Pointer to the GPR packet containing the control message.
 *
 * \return AR_EOK on success, or an appropriate AR_result error code otherwise.
 */
ar_result_t capi_ipc_rx_fwk_extn_ipc_port_ctrl_msg_handler(void       *capi_ptr,
                                                           uint32_t      is_ext_input,
                                                           uint32_t      port_index,
                                                           gpr_packet_t *pkt_ptr);

/**
 * \brief Handles data messages received from the IPC port framework extension.
 * This is a callback function invoked by the framework for IPC data commands.
 *
 * \param[in,out] capi_ptr               Pointer to the CAPI module instance.
 * \param[in] is_ext_input           True if the message is for an external input port, FALSE otherwise.
 * \param[in] port_index           The index of the port associated with the message.
 * \param[in] pkt_ptr              Pointer to the GPR packet containing the data message.
 * \param[in] ipc_data_msg_flag_mask Mask of flags indicating specific properties of the data message.
 *
 * \return AR_EOK on success, or an appropriate AR_result error code otherwise.
 */
ar_result_t capi_ipc_rx_fwk_extn_ipc_port_data_msg_handler(void       *capi_ptr,
                                                           uint32_t      is_ext_input,
                                                           uint32_t      port_index,
                                                           gpr_packet_t *pkt_ptr,
                                                           uint32_t      ipc_data_msg_flag_mask);

/**
 * \brief Reads audio data (PCM or non-PCM) from the IPC_RX module's internal
 * or IPC buffers and populates the CAPI output stream data.
 *
 * \param[in,out] me_ptr  Pointer to the CAPI IPC_RX instance.
 * \param[out] output[] Array of output stream data buffers to be filled.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_read_data(capi_ipc_rx_t *me_ptr, capi_stream_data_t *output[]);

/**
 * \brief Processes input data and generates output data for the IPC_RX CAPI module.
 *
 * This function is the core data processing entry point for the module,
 * responsible for transferring data from input to output streams.
 *
 * \param[in] _pif      Pointer to the CAPI instance.
 * \param[in] input[]   Array of pointers to input stream data buffers.
 * \param[out] output[] Array of pointers to output stream data buffers.
 *
 * \return CAPI_EOK on success, or an error code if processing fails.
 */
capi_err_t capi_ipc_rx_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

/**
 * \brief Calculates and returns the required metadata size for the IPC_RX module
 * based on the provided metadata list.
 *
 * \param[in,out] me_ptr           Pointer to the CAPI IPC_RX instance.
 * \param[in] md_list_ptr      Pointer to the metadata list for which size is to be calculated.
 * \param[out] meta_data_size_ptr Pointer to a variable where the calculated metadata size will be stored.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t ipc_rx_get_meta_data_size(capi_ipc_rx_t        *me_ptr,
                                     module_cmn_md_list_t *md_list_ptr,
                                     uint32_t             *meta_data_size_ptr);


/**
 * \brief Reads metadata from the IPC_RX module's internal buffers and
 * prepares it for the CAPI output stream data.
 *
 * \param[in,out] me_ptr  Pointer to the CAPI IPC_RX instance.
 * \param[out] output[] Array of output stream data buffers, where metadata will be attached.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t capi_ipc_rx_read_metadata(capi_ipc_rx_t *me_ptr, capi_stream_data_t *output[]);

/**
 * \brief Sets up the GPR client for processing an incoming input data buffer.
 * This involves mapping shared memory and validating buffer properties.
 *
 * \param[in,out] me_ptr    Pointer to the CAPI IPC_RX instance.
 * \param[in] packet_ptr Pointer to the GPR packet containing the data buffer information.
 *
 * \return AR_EOK on success, or an appropriate AR_result error code otherwise.
 */
ar_result_t ipc_rx_input_data_buffer_set_up_gpr_client_v2(capi_ipc_rx_t *me_ptr, gpr_packet_t *packet_ptr);

/**
 * \brief Handles input media format control commands received from a GPR client.
 * This updates the module's media format and potentially triggers internal buffer reallocations.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance.
 * \param[in] pkt_ptr    Pointer to the GPR packet containing the media format command.
 * \param[in] is_data_path Boolean flag: TRUE if the command came over a data path, FALSE otherwise.
 *
 * \return CAPI_EOK on success, or an appropriate CAPI error code otherwise.
 */
capi_err_t ipc_rx_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client(capi_ipc_rx_t *me_ptr,
                                                                    gpr_packet_t *pkt_ptr,
                                                                    bool_t        is_data_path);

/**
 * \brief Frees resources associated with an input data command and sends an acknowledgment
 * back to the GPR client. This function also handles cache invalidation and shared
 * memory reference count decrements.
 *
 * \param[in,out] me_ptr    Pointer to the CAPI IPC_RX instance.
 * \param[in] packet_ptr Pointer to the GPR packet that needs to be freed and acknowledged.
 * \param[in] status     The result status (AR_EOK for success or an error code) to be sent in the acknowledgment.
 * \param[in] is_flush   Boolean flag: TRUE if this operation is part of a flush sequence.
 *
 * \return AR_EOK on success, or an appropriate AR_result error code otherwise.
 */
ar_result_t ipc_rx_free_input_data_cmd(capi_ipc_rx_t *me_ptr,
                                       gpr_packet_t *packet_ptr,
                                       ar_result_t   status,
                                       bool_t        is_flush,
                                       bool_t        is_ref_counted);

/**
 * \brief Handles flush operations for the IPC_RX module.
 * This clears internal buffers, pending IPC packets, and media format data.
 *
 * \param[in,out] me_ptr  Pointer to the CAPI IPC_RX instance.
 * \param[in] pkt_ptr Optional pointer to a GPR packet that needs to be flushed.
 *
 * \return AR_EOK on success, or an appropriate AR_result error code otherwise.
 */
ar_result_t ipc_rx_flush_handling(capi_ipc_rx_t *me_ptr, gpr_packet_t *pkt_ptr, bool_t is_ref_counted);

/**
 * \brief Copies data from the IPC (inter-processor communication) buffer
 * into the module's internal processing buffer.
 *
 * \param[in,out] me_ptr Pointer to the CAPI IPC_RX instance.
 *
 * \return AR_EOK on success, or an appropriate AR_result error code otherwise.
 */
ar_result_t ipc_rx_copy_ipc_buffer_to_int_buffer(capi_ipc_rx_t *me_ptr);

/**
 * \brief Destory all the MD provided in the list.
 * \param[in,out] me_ptr Pointer to the CAPI IPC_RX instance.
 *
 * \return AR_EOK on success, or an appropriate AR_result error code otherwise.
 */
ar_result_t ipc_rx_destroy_all_md(capi_ipc_rx_t *me_ptr, module_cmn_md_list_t **md_list_pptr);
/**
 * @brief This function propagates metadata from a source buffer to a destination buffer
 *        within the context of an IPC (Inter-Process Communication) receive operation.
 *        It handles the adjustment of metadata offsets and lengths based on the
 *        amount of data copied or consumed from the buffers.
 *
 * @param[in,out] me_ptr Pointer to the IPC receive structure. This structure
 *                       likely contains state information for the IPC receiver.
 * @param[in]     src_mf_ptr Pointer to the source media format (version 2).
 *                           This provides information about the media, such as
 *                           sample rate, number of channels, and data format,
 *                           which is crucial for calculating byte offsets.
 * @param[in]     src_buf_len_per_ch_before_copy The length of the source buffer
 *                                               (per channel) before any data
 *                                               was copied/consumed from it.
 *                                               This is typically in samples.
 * @param[in]     src_buf_len_per_ch_after_copy The length of the source buffer
 *                                              (per channel) after data was
 *                                              copied/consumed from it.
 *                                              This is typically in samples.
 * @param[in,out] src_md_list_pptr Pointer to a pointer to the head of the
 *                                 source metadata list. This list contains
 *                                 metadata associated with the source buffer.
 *                                 The function may modify this list (e.g.,
 *                                 adjust offsets, remove processed MD).
 * @param[in]     dst_buf_len_per_ch_before_copy The length of the destination buffer
 *                                               (per channel) before any data
 *                                               was copied into it.
 *                                               This is typically in samples.
 * @param[in]     dst_buf_len_per_ch_after_copy The length of the destination buffer
 *                                              (per channel) after data was
 *                                              copied into it.
 *                                              This is typically in samples.
 * @param[in,out] dst_md_list_pptr Pointer to a pointer to the head of the
 *                                 destination metadata list. This list will be
 *                                 populated with (or have existing) metadata
 *                                 relevant to the destination buffer.
 *                                 The function may add new metadata nodes or
 *                                 adjust existing ones.
 *
 * @return Returns CAPI_EOK on success, or an error code if the operation fails.
 */
capi_err_t ipc_rx_prop_md_from_src_buf_to_dst_buf(capi_ipc_rx_t         *me_ptr,
                                                  capi_media_fmt_v2_t   *src_mf_ptr,
                                                  uint32_t               src_buf_len_per_ch_before_copy,
                                                  uint32_t               src_buf_len_per_ch_after_copy,
                                                  capi_stream_data_v2_t *src_stream_ptr,
                                                  uint32_t               dst_buf_len_per_ch_before_copy,
                                                  uint32_t               dst_buf_len_per_ch_after_copy,
                                                  capi_stream_data_v2_t *dst_stream_ptr);

capi_err_t capi_ipc_rx_check_n_enable_buffer_extn(capi_ipc_rx_t *me_ptr);

capi_err_t capi_ipc_rx_intf_extn_get_mod_output_buf(uint32_t    handle,
                                                    uint32_t    port_index,
                                                    uint32_t   *num_bufs_ptr,
                                                    capi_buf_t *buffer_ptr);

capi_err_t capi_ipc_rx_intf_extn_return_mod_output_buf(uint32_t    handle,
                                                      uint32_t    port_index,
                                                      uint32_t   *num_bufs_ptr,
                                                      capi_buf_t *buffer_ptr);

static inline bool_t check_if_in_buffer_range(int8_t *check_addr,
                                              int8_t *buf_start_addr,
                                              uint32_t buf_length)
{
   int8_t *buf_end_addr = buf_start_addr + buf_length;
  return ((check_addr >= buf_start_addr) && (check_addr < buf_end_addr)) ? TRUE : FALSE;
}

static inline uint32_t ipc_rx_get_num_bufs_from_mf(capi_ipc_rx_t *me_ptr)
{
   if ( CAPI_CMN_IS_PCM_FORMAT(me_ptr->output_media_fmt.header.format_header.data_format) &&
        (CAPI_DEINTERLEAVED_UNPACKED_V2 == me_ptr->output_media_fmt.format.data_interleaving))
   {
      return me_ptr->output_media_fmt.format.num_channels;
   }
   return 1;
}

ar_result_t ipc_rx_modify_md_in_internal_buf_when_new_data_arrives(capi_ipc_rx_t         *me_ptr,
                                                                   module_cmn_md_list_t **md_list_pptr);

bool_t ipc_rx_check_if_there_is_a_flushing_eos(capi_ipc_rx_t *me_ptr, module_cmn_md_list_t *md_list_ptr);

ar_result_t ipc_rx_populate_metadata_from_ipc_buffer(capi_ipc_rx_t *me_ptr, gpr_packet_t *packet_ptr);

#endif // CAPI_IPC_RX_UTILS_H
