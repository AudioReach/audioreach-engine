/**
 * \file capi_splitter_utils.h
 * \brief
 *        Header file to implement utilities for Simple Splitter Module (SPLITTER)
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef CAPI_SPLITTER_UTILS_H
#define CAPI_SPLITTER_UTILS_H

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#include "capi_cmn.h"
#include "ar_error_codes.h"
#include "capi_intf_extn_metadata.h"
#include "splitter_api.h"
#include "posal.h"
#include "capi_intf_extn_data_port_operation.h"
#include "capi_fwk_extns_trigger_policy.h"
#include "capi_intf_extn_prop_is_rt_port_property.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/* clang-format off */
//#define SPLITTER_DBG_LOW
/*------------------------------------------------------------------------
 * Macros
 * -----------------------------------------------------------------------*/
/*Q-Formats*/
#define SPLITTER_Q15 15
#define SPLITTER_Q27 27
#define SPLITTER_Q31 31
/*------------------------------------------------------------------------
 * Type definitions
 * -----------------------------------------------------------------------*/
typedef struct capi_splitter_events_config
{
   uint32_t        splitter_kpps;
   uint32_t        splitter_bw;
} capi_splitter_events_config_t;

typedef struct capi_splitter_out_port_flags_t
{
   uint8_t is_all_md_blocked:1;
   uint8_t is_eos_disable:1;
   uint8_t is_us_rt:1;
   uint8_t is_ds_rt:1;
   uint8_t ts_cfg:2;
} capi_splitter_out_port_flags_t;

typedef struct capi_splitter_out_port_state_t
{
   uint32_t                             port_id;
   intf_extn_data_port_state_t          state;  // Port state, based on port operation.
   capi_splitter_out_port_flags_t       flags;
} capi_splitter_out_port_state_t;

typedef struct capi_splitter_cached_out_port_cfg_t
{
   uint32_t        port_id;  /**< valid only for ports that receive set-param. */
   uint32_t        num_md;
   uint32_t        *md_whitelist_ptr;
} capi_splitter_cached_out_port_cfg_t;


typedef struct capi_splitter_tgp_t
{
    fwk_extn_param_id_trigger_policy_cb_fn_t  tg_policy_cb;
    fwk_extn_port_trigger_affinity_t          *out_port_triggerable_affinity_arr;
    fwk_extn_port_nontrigger_policy_t         *out_port_nontriggerable_policy_arr;
} capi_splitter_tgp_t;

typedef struct capi_splitter_flags_t
{
   uint8_t is_inplace:1;
   uint8_t is_in_media_fmt_set:1;
   uint8_t is_us_rt:1;
   uint8_t is_ds_rt:1;
} capi_splitter_flags_t;

typedef struct capi_splitter
{
   capi_t                              vtbl;
   /* v-table pointer */
   capi_heap_id_t                      heap_mem;
   /* Heap id, used to allocate memory */
   capi_event_callback_info_t          cb_info;
   /* Call back info for event raising */
   capi_splitter_events_config_t       events_config;
   /* Event information struct */
   capi_splitter_out_port_state_t      *out_port_state_arr;
   /*Array to maintain state information (active/inactive)*/
   capi_media_fmt_v2_t                 operating_mf;
   /* Operating Media Format of the SPLITTER Module*/
   intf_extn_param_id_metadata_handler_t  metadata_handler;
   /* Handler for multi-port metadata (EOS) prop */
   param_id_splitter_metadata_prop_cfg_t *out_port_md_prop_cfg_ptr;
    /*Array to cache port specific metadata configuration*/
   param_id_splitter_timestamp_prop_cfg_t *out_port_ts_prop_cfg_ptr;
    /*Array to cache port specific timestamp configuration*/
   intf_extn_param_id_stm_ts_t ts_payload;
   /*Handler to obtain the latest STM timestamp value*/
   capi_splitter_tgp_t                    tgp;
   /*Trigger policy callback handle.*/
   uint32_t                               miid;
   capi_splitter_flags_t                  flags;
   uint8_t                                num_out_ports;
   intf_extn_data_port_state_t            input_port_state;  //Input port state, based on port operation.
   /* Number of output ports on the SPLITTER Module*/
} capi_splitter_t;

/* clang-format on */
/*------------------------------------------------------------------------
 * Function Declarations
 * -----------------------------------------------------------------------*/
/* Utility functions not a part of the function table  (vtbl) */
/*Calculates and stores the current KPPS requirement of the SPLITTER module*/
capi_err_t capi_splitter_update_and_raise_kpps_bw_event(capi_splitter_t *me_ptr);

/*Checks if the Port MD cfg is received and gets the whitelist information including the ptr and the number of MD*/
bool_t capi_splitter_check_if_port_cache_cfg_rcvd_get_wl_info(capi_splitter_t *me_ptr,
                                                              uint32_t         port_id,
                                                              uint32_t **      wl_ptr,
                                                              uint32_t *       num_md_ptr);
/*check if md is blocked through white-list or not*/
bool_t capi_splitter_is_md_blcoked_wl(uint32_t *wl_arr_ptr, uint32_t num_metadata, uint32_t md_id);

/* eos flag for the port*/
void capi_splitter_update_port_md_flag(capi_splitter_t *me_ptr, uint32_t port_index);

/*Checks if any port is open and matches the configured port. If so, it updates the EOS flag*/
capi_err_t capi_splitter_check_if_any_port_is_open_and_update_eos_flag(capi_splitter_t *me_ptr);

void capi_splitter_update_port_ts_flag(capi_splitter_t *me_ptr, uint32_t out_port_index);
capi_err_t capi_splitter_update_all_opened_port_ts_config_flag(capi_splitter_t *me_ptr);

capi_err_t capi_splitter_update_is_rt_property(capi_splitter_t *me_ptr);
capi_err_t capi_splitter_set_data_port_property(capi_splitter_t *me_ptr, capi_buf_t *payload_ptr);

capi_err_t capi_splitter_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

capi_err_t capi_splitter_end(capi_t *_pif);

capi_err_t capi_splitter_set_param(capi_t *                _pif,
                                   uint32_t                param_id,
                                   const capi_port_info_t *port_info_ptr,
                                   capi_buf_t *            params_ptr);

capi_err_t capi_splitter_get_param(capi_t *                _pif,
                                   uint32_t                param_id,
                                   const capi_port_info_t *port_info_ptr,
                                   capi_buf_t *            params_ptr);

capi_err_t capi_splitter_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

capi_err_t capi_splitter_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

capi_vtbl_t *capi_splitter_get_vtbl();

capi_err_t capi_splitter_check_and_raise_dynamic_inplace(capi_splitter_t *me_ptr);

capi_err_t handle_metadata(capi_splitter_t *me_ptr, capi_stream_data_t *input[], capi_stream_data_t *output[]);

#ifdef __cplusplus
}
#endif /*__cplusplus*/
#endif // CAPI_SPLITTER_UTILS_H
