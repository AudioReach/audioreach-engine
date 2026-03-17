/**
 * \file spdm_i.h
 * \brief
 * This file contains internal definitions and declarations for the OLC Satellite Graph Management.
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef OLC_SPDM_I_H
#define OLC_SPDM_I_H

#include "olc_cmn_utils.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#include "spf_svc_utils.h"
#include "graph_utils.h"
#include "posal_power_mgr.h"
#include "posal_memorymap.h"
#include "spf_macros.h"
// For eos.
#include "spf_ref_counter.h"
// for state propagation
#include "offload_sp_api.h"
#include "olc_driver.h"
#include "sdm.h"
#include "sprm.h"

/* =======================================================================
   OLC SGM Macros
   ======================================================================= */
// Size of Queues
#define SDM_MAX_DATA_Q_ELEMENTS 64
#define PORT_INDEX_SHIFT_FACTOR 6
#define DEFAULT_FRAME_SIZE_IN_UNITS 10 // in ms
// Read buffer frame-size for compressed data
#define DEFAULT_FRAME_SIZE_COMPRESSED_DATA 8192 // in bytes
// Number of event configuration for Read port
#define NUM_RD_PORT_EVENT_CONFIG 6
// Number of event configuration for Write port
#define NUM_WR_PORT_EVENT_CONFIG 2
#define GAURD_PROTECTION_BYTES 128

/* =======================================================================
   OLC SDM Structure Definitions
   ======================================================================= */

/* =======================================================================
   OLC SDM Function Declarations
   ======================================================================= */

/**--------------------------- sdm data handler utilities --------------------*/
/**
 * \brief Processes the data read completion.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] packet_ptr Pointer to the packet structure.
 * \param[in] port_index Index of the port.
 * \return Result of the operation.
 */
ar_result_t spdm_process_data_read_done(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr, uint32_t port_index);

/**
 * \brief Processes the data write completion.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] packet_ptr Pointer to the packet structure.
 * \param[in] port_index Index of the port.
 * \return Result of the operation.
 */
ar_result_t spdm_process_data_write_done(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr, uint32_t port_index);

/**
 * \brief Retrieves a data buffer from the read data pool.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] rd_ptr Pointer to the read data port object.
 * \return Result of the operation.
 */
ar_result_t spdm_get_databuf_from_rd_datapool(spgm_info_t *spgm_ptr, read_data_port_obj_t *rd_ptr);

/**
 * \brief Sends a read data buffer.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] data_buf_node_ptr Pointer to the data buffer node.
 * \param[in] rd_ptr Pointer to the read data port object.
 * \return Result of the operation.
 */
ar_result_t spdm_send_read_data_buffer(spgm_info_t * spgm_ptr,
                                       data_buf_pool_node_t *data_buf_node_ptr,
                                       read_data_port_obj_t *rd_ptr);

/**
 * \brief Allocates IPC data buffers.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] new_buf_size Size of the new buffer.
 * \param[in] port_index Index of the port.
 * \param[in] data_type Type of the data.
 * \return Result of the operation.
 */
ar_result_t spdm_alloc_ipc_data_buffers(spgm_info_t * spgm_ptr,
                                        uint32_t new_buf_size,
                                        uint32_t port_index,
                                        sdm_ipc_data_type_t data_type);

/**
 * \brief Retrieves a data object.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] port_index Index of the port.
 * \param[in] data_type Type of the data.
 * \param[out] data_obj_pptr Pointer to the data object.
 * \return Result of the operation.
 */
ar_result_t spdm_get_data_obj(spgm_info_t *spgm_ptr, uint32_t port_index, uint32_t data_type, void **data_obj_pptr);

/**
 * \brief Retrieves a data pool pointer.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] port_index Index of the port.
 * \param[in] data_type Type of the data.
 * \param[out] data_pool_pptr Pointer to the data pool.
 * \return Result of the operation.
 */
ar_result_t spdm_get_data_pool_ptr(spgm_info_t * spgm_ptr,
                                   uint32_t port_index,
                                   uint32_t data_type,
                                   shmem_data_buf_pool_t **data_pool_pptr);

/**
 * \brief Removes a node from the data pool.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] data_pool_ptr Pointer to the data pool.
 * \param[in] num_buf_nodes_to_remove Number of buffer nodes to remove.
 * \return Result of the operation.
 */
ar_result_t spdm_remove_node_from_data_pool(spgm_info_t * spgm_ptr,
                                            shmem_data_buf_pool_t *data_pool_ptr,
                                            uint32_t num_buf_nodes_to_remove);

/**
 * \brief Removes all nodes from the data pool.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] data_pool_ptr Pointer to the data pool.
 * \param[in] num_buf_nodes_to_remove Number of buffer nodes to remove.
 * \return Result of the operation.
 */
ar_result_t spdm_remove_all_node_from_data_pool(spgm_info_t * spgm_ptr,
                                                shmem_data_buf_pool_t *data_pool_ptr,
                                                uint32_t num_buf_nodes_to_remove);

/**
 * \brief Retrieves an available data buffer node.
 * \param[in] data_buf_list_ptr Pointer to the data buffer list.
 * \param[in] num_data_buf_list Number of data buffer list.
 * \param[out] data_buf_node_ptr Pointer to the data buffer node.
 * \return Boolean indicating if the node is available.
 */
bool_t spdm_get_available_data_buf_node(spf_list_node_t * data_buf_list_ptr,
                                        uint32_t num_data_buf_list,
                                        data_buf_pool_node_t **data_buf_node_ptr);

/**--------------------------- utils------------------------------------------*/
/**
 * \brief Retrieves the RW EP MIID given the RW client MIID.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] client_miid Client MIID.
 * \param[out] sat_ep_miid_ptr Pointer to the satellite EP MIID.
 * \param[in] data_type Type of the data.
 * \return Result of the operation.
 */
ar_result_t sgm_get_rw_ep_miid_given_rw_client_miid(spgm_info_t * spgm_ptr,
                                                    uint32_t client_miid,
                                                    uint32_t * sat_ep_miid_ptr,
                                                    sdm_ipc_data_type_t data_type);

/**
 * \brief Retrieves the data port index given the bit index.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] data_type Type of the data.
 * \param[in] channel_bit_index Channel bit index.
 * \param[out] port_index_ptr Pointer to the port index.
 * \return Result of the operation.
 */
ar_result_t sgm_get_data_port_index_given_bit_index(spgm_info_t * spgm_ptr,
                                                    sdm_ipc_data_type_t data_type,
                                                    uint32_t channel_bit_index,
                                                    uint32_t * port_index_ptr);

/**
 * \brief Retrieves a data buffer from the write data pool.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] wr_ptr Pointer to the write data port object.
 * \return Result of the operation.
 */
ar_result_t spdm_get_databuf_from_wr_datapool(spgm_info_t *spgm_ptr, write_data_port_obj_t *wr_ptr);

/**
 * \brief Retrieves a data buffer node.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] data_pool_ptr Pointer to the data pool.
 * \param[in] token Token value.
 * \param[out] data_buf_node_pptr Pointer to the data buffer node.
 * \return Result of the operation.
 */
ar_result_t spdm_get_data_buf_node(spgm_info_t * spgm_ptr,
                                   shmem_data_buf_pool_t *data_pool_ptr,
                                   uint32_t token,
                                   data_buf_pool_node_t **data_buf_node_pptr);

/**--------------------------- meta data handler------------------------------------------*/

/**
 * \brief Reads metadata from the data buffer node.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] data_buf_node_ptr Pointer to the data buffer node.
 * \param[out] md_list_pptr Pointer to the metadata list.
 * \param[in] rd_client_module_iid Read client module IID.
 * \param[in] port_index Index of the port.
 * \return Result of the operation.
 */
ar_result_t spdm_read_meta_data(spgm_info_t *          spgm_ptr,
                                data_buf_pool_node_t * data_buf_node_ptr,
                                module_cmn_md_list_t **md_list_pptr,
                                uint32_t               rd_client_module_iid,
                                uint32_t               port_index);

/**
 * \brief Writes metadata to the data buffer node.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] data_buf_node_ptr Pointer to the data buffer node.
 * \param[out] md_list_pptr Pointer to the metadata list.
 * \param[in] wr_client_module_iid Write client module IID.
 * \param[in] port_index Index of the port.
 * \return Result of the operation.
 */
ar_result_t spdm_write_meta_data(spgm_info_t *          spgm_ptr,
                                 data_buf_pool_node_t * data_buf_node_ptr,
                                 module_cmn_md_list_t **md_list_pptr,
                                 uint32_t               wr_client_module_iid,
                                 uint32_t               port_index);

/**
 * \brief Retrieves the size of the write metadata.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] port_index Index of the port.
 * \param[in] md_list_ptr Pointer to the metadata list.
 * \param[out] wr_md_size_ptr Pointer to the write metadata size.
 * \return Result of the operation.
 */
ar_result_t spdm_get_write_meta_data_size(spgm_info_t *         spgm_ptr,
                                          uint32_t              port_index,
                                          module_cmn_md_list_t *md_list_ptr,
                                          uint32_t *            wr_md_size_ptr);

/**--------------------Read Write data port configuration utilities------------------------*/
/**
 * \brief Sets the configuration for the read endpoint port.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] port_index Index of the port.
 * \return Result of the operation.
 */
ar_result_t spdm_set_rd_ep_port_config(spgm_info_t *spgm_ptr, uint32_t port_index);

/**
 * \brief Sets the configuration for the read endpoint client.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] port_index Index of the port.
 * \return Result of the operation.
 */
ar_result_t spdm_set_rd_ep_client_config(spgm_info_t *spgm_ptr, uint32_t port_index);

/**
 * \brief Sets the configuration for the read endpoint metadata rendered.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] port_index Index of the port.
 * \param[in] md_te_ptr Pointer to the metadata tracking event.
 * \param[in] is_last_instance Indicates if this is the last instance.
 * \return Result of the operation.
 */
ar_result_t spdm_set_rd_ep_md_rendered_config(spgm_info_t *              spgm_ptr,
                                              uint32_t                   port_index,
                                              metadata_tracking_event_t *md_te_ptr,
                                              uint32_t                   is_last_instance);

/**
 * \brief Sets the configuration for the write endpoint client.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] port_index Index of the port.
 * \return Result of the operation.
 */
ar_result_t spdm_set_wd_ep_client_config(spgm_info_t *spgm_ptr, uint32_t port_index);

/**
 * \brief Registers the operating frame size event for the endpoint.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] port_index Index of the port.
 * \param[in] data_type Type of the data.
 * \return Result of the operation.
 */
ar_result_t spdm_register_ep_operating_frame_size_event(spgm_info_t *spgm_ptr, uint32_t port_index, uint32_t data_type);

/**
 * \brief Registers the state propagation event for the endpoint.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] port_index Index of the port.
 * \param[in] data_type Type of the data.
 * \param[in] event_id Event ID.
 * \return Result of the operation.
 */
ar_result_t spdm_register_ep_state_propagation_event(spgm_info_t *spgm_ptr,
                                                     uint32_t     port_index,
                                                     uint32_t     data_type,
                                                     uint32_t     event_id);

/**
 * \brief Registers the read media format event for the endpoint.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] port_index Index of the port.
 * \return Result of the operation.
 */
ar_result_t spdm_register_read_media_fmt_event(spgm_info_t *spgm_ptr, uint32_t port_index);

/**--------------------Read Write data port event handler utilities------------------------*/
/**
 * \brief Processes the upstream port state change.
 * \param[in] spgm_ptr Pointer to the SPGM information structure.
 * \param[in] port_index Index of the port.
 * \return Result of the operation.
 */
ar_result_t spdm_process_us_port_state_change(spgm_info_t *spgm_ptr, uint32_t port_index);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef OLC_SPDM_I_H
