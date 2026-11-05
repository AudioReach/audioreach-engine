/**
 * \file capi_data_marker_i.h
 * \brief
 *  
 *     Header file to define types internal to the CAPI interface for the SAL module
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CAPI_DATA_MARKER_I_H
#define CAPI_DATA_MARKER_I_H
/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#include "capi_cmn.h"
#include "posal.h"
#include "data_marker_api.h"
#include "module_cmn_metadata.h"
#include "other_metadata.h"
#include "spf_list_utils.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*------------------------------------------------------------------------
 * Macros
 * -----------------------------------------------------------------------*/

#define CAPI_DATA_MARKER_KPPS 40 // AKR: Not profiled
/**< Flag bits (me_ptr->intercept_flag)
     First bit  - Delay Marker type
*/
#define DATA_MARKER_DELAY_MASK 0x0001
#define DATA_MARKER_DELAY_SHIFT 0
#define DATA_MARKER_DELAY_EVENT 1

/*------------------------------------------------------------------------
 * Type definitions
 * -----------------------------------------------------------------------*/

typedef struct event_reg_client_info_t
{
   uint64_t address;
   uint32_t token;
   uint32_t event_id;
} event_reg_client_info_t;

typedef struct cfg_md_info_t
{
   uint32_t token;
   uint32_t md_id;
   uint32_t frame_dur_ms;
} cfg_md_info_t;

typedef struct capi_data_marker_t
{
   const capi_vtbl_t *vtbl_ptr;
   /* pointer to virtual table */

   capi_event_callback_info_t event_cb_info;
   /* Event Call back info received from Framework*/

   capi_heap_id_t heap_mem;
   /* Heap id received from framework*/

   capi_media_fmt_v2_t operating_mf;
   /* Operating Media Format of the Module*/

   bool_t is_in_media_fmt_set;
   /* Flag indicating whether the OMF has been received*/

   bool_t is_md_inserter;
   /* Flag indicating whether the module needs to insert MD marker*/

   uint32_t intercept_flag;
   /* Flag where bits indicate what md the module should look for in process*/

   intf_extn_param_id_metadata_handler_t metadata_handler;
   /* Metadata Handler Object*/

   spf_list_node_t *client_info_list_ptr;
   /* List of Client info nodes for every client that has registered for an event
      Object Type: event_reg_client_info_t*/

   spf_list_node_t *insert_md_cfg_list_ptr;
   /* List of info nodes for every configuration that the module receives for MD insertion
      Object Type: cfg_md_info_t*/

   module_cmn_md_list_t *md_list_ptr;
   /* internal metadata list. will be used if output buf is larger than algo delay*/

   uint32_t miid;

   uint32_t cntr_frame_dur_us;

   uint32_t frame_counter;

} capi_data_marker_t;

/* clang-format on */

/*------------------------------------------------------------------------
 * Function Declarations
 * -----------------------------------------------------------------------*/
ar_result_t capi_data_marker_intercept_delay_marker_and_check_raise_events(capi_data_marker_t *  me_ptr,
                                                                           module_cmn_md_list_t *md_list_ptr,
                                                                           bool_t                is_delay_event_reg);

ar_result_t capi_data_marker_insert_marker(capi_data_marker_t *me_ptr, module_cmn_md_list_t **md_list_pptr);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif // CAPI_DATA_MARKER_I_H
