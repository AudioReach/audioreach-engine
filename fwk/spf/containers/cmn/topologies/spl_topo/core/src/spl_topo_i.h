/**
 * \file spl_topo_i.h
 *
 * \brief
 *
 *     Internal header file for the advanced topo.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SPL_TOPO_I_H_
#define SPL_TOPO_I_H_

// clang-format off

#include "spl_topo.h"
#include "gen_topo_buf_mgr.h"
#include "irm_cntr_prof_util.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

typedef struct topo_process_context_t spl_topo_process_context_t;

typedef enum spl_topo_process_status_t {
   SPL_TOPO_PROCESS_UNEVALUATED = 0,   // Processing conditions are not evaluated yet.
   SPL_TOPO_PROCESS,           // Process this module.
   SPL_TOPO_PROCESS_SKIP,      // Skip this module since this subgraph is stopped or module is otherwise not to be processed.
                               // A skipped module in a SISO graph will cause a data stall.
} spl_topo_process_status_t;

/* =======================================================================
Public Function Declarations
========================================================================== */

/**---------------------------- spl_topo_buf_utils -----------------------------*/
ar_result_t spl_topo_get_output_port_buffer(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);
void spl_topo_return_output_buf(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);
ar_result_t spl_topo_flush_return_output_buf(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);
ar_result_t spl_topo_adjust_buf(spl_topo_t *                 topo_ptr,
                             topo_buf_t *           buf_ptr,
                             capi_stream_data_v2_t *sdata_ptr,
                             uint32_t                  consumed_data_per_ch,
                             topo_media_fmt_t *        media_fmt_ptr);

bool_t spl_topo_output_port_is_size_known(void *ctx_topo_ptr, void *ctx_out_port_ptr);

/**------------------------------ spl_topo_capi ------------------------------*/
ar_result_t spl_topo_capi_get_required_fmwk_extensions(void *           topo_ctx_ptr,
                                                       void *           module_ctx_ptr,
                                                       void *           amdb_handle,
                                                       capi_proplist_t *init_proplist_ptr);

/**------------------------- spl_topo_capi_cb_handler ------------------------*/
capi_err_t spl_topo_handle_output_media_format_event(void *                ctx_ptr,
                                                     void *                module_ctxt_ptr,
                                                     capi_event_info_t *event_info_ptr,
                                                     bool_t                is_std_fmt_v2,
                                                     bool_t                is_pending_data_valid);
/**--------------------------- spl_topo_data_process ----------------------------*/
spl_topo_process_status_t spl_topo_module_processing_decision(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr);
ar_result_t spl_topo_module_drop_all_data(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr, bool_t *data_was_dropped_ptr);

/**------------------------ spl_topo_media_format_utils -------------------------*/
bool_t spl_topo_is_med_fmt_prop_possible(spl_topo_t *           topo_ptr,
                                         spl_topo_module_t *    module_ptr,
                                         spl_topo_input_port_t *in_port_ptr,
                                         bool_t              is_data_path);
ar_result_t spl_topo_prop_med_fmt_from_prev_out_to_curr_in(spl_topo_t *           topo_ptr,
                                                           spl_topo_module_t *    module_ptr,
                                                           spl_topo_input_port_t *in_port_ptr,
                                                           bool_t              is_data_path);
ar_result_t spl_topo_propagate_media_fmt_from_module(void *            cxt_ptr,
                                                     bool_t            is_data_path,
                                                     gu_module_list_t *start_module_list_ptr);
ar_result_t spl_topo_propagate_media_fmt_single_module(spl_topo_t *       topo_ptr,
                                                       bool_t             is_data_path,
                                                       spl_topo_module_t *module_ptr,
                                                       bool_t *           mf_propped_ptr);
ar_result_t spl_topo_check_send_media_fmt_to_fwk_layer(spl_topo_t *topo_ptr,
                                                       bool_t * sent_to_fwk_layer);

/**-------------------------------- spl_topo_utils ------------------------------*/
uint32_t spl_topo_get_module_threshold_bytes_per_channel(gen_topo_common_port_t *port_common_ptr,
                                                         spl_topo_module_t *        module_ptr);
bool_t spl_topo_module_has_pending_in_media_fmt(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr);
bool_t spl_topo_input_port_has_pending_flushing_eos(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr);
ar_result_t spl_topo_get_ext_op_buf(spl_topo_t *            topo_ptr,
                                    spl_topo_output_port_t *out_port_ptr,
                                    spl_topo_ext_buf_t **   ext_out_buf_pptr,
                                    bool_t *             found_external_op_ptr);

bool_t spl_topo_ip_port_contains_unconsumed_data(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr);

bool_t spl_topo_out_port_has_pending_eof(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);
ar_result_t spl_topo_input_port_clear_pending_eof(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr);
ar_result_t spl_topo_ip_check_prop_eof(spl_topo_t *           topo_ptr,
                                       spl_topo_input_port_t *in_port_ptr,
                                       uint32_t            consumed_data_per_ch,
                                       uint32_t            prev_in_len_per_ch,
                                       bool_t              has_int_ts_disc,
                                       uint32_t            ts_disc_pos_bytes);

ar_result_t spl_topo_check_update_bypass_module(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr, bool_t is_disabled);
/**-------------------------------- spl_topo_metadata ------------------------------*/
ar_result_t spl_topo_transfer_md_between_ports(spl_topo_t *topo_ptr,
                                               void *   dst_port_ptr,
                                               bool_t   dst_is_input,
                                               void *   src_port_ptr,
                                               bool_t   src_is_input);

ar_result_t spl_topo_transfer_md_from_out_sdata_to_out_port(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr);
ar_result_t topo_2_append_eos_zeros(spl_topo_t *            topo_ptr,
                                    spl_topo_module_t *     module_ptr,
                                    spl_topo_input_port_t * in_port_ptr,
                                    spl_topo_output_port_t *connected_out_port_ptr,
                                    spl_topo_input_port_t * ext_in_port_ptr);

ar_result_t spl_topo_propagate_metadata(spl_topo_t *              topo_ptr,
                                        spl_topo_module_t *       module_ptr,
                                        bool_t                 input_has_metadata_or_eos,
                                        uint32_t               input_size_before_per_ch,
                                        spl_topo_process_status_t proc_status);

void spl_topo_handle_internal_timestamp_discontinuity(spl_topo_t *            topo_ptr,
						      spl_topo_output_port_t *out_port_ptr,
						      uint32_t                prev_actual_data_len_all_ch,
						      bool_t                  is_ext_op);

bool_t spl_topo_is_ipc_module_trigger_satisfied(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr);

/**
 * Check if there is any data in the output port's buffer and returns the actual data length */
static inline uint32_t spl_topo_int_out_port_actual_data_len(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   gen_topo_common_port_t *cmn_port_ptr = &out_port_ptr->t_base.common;
   return (((NULL != cmn_port_ptr->bufs_ptr) && (NULL != cmn_port_ptr->bufs_ptr[0].data_ptr)) ?
         (cmn_port_ptr->bufs_ptr[0].actual_data_len * cmn_port_ptr->sdata.bufs_num) : 0);
}

/**
 * Checks if the module is disabled. The module can be disabled by process
 * state or if setting the input media format failed.
 */
static inline bool_t spl_topo_is_module_disabled(spl_topo_module_t *module_ptr)
{
   return module_ptr->t_base.flags.disabled || module_ptr->in_media_fmt_failed;
}

/**
 * Check if there is any data in the output port's buffer. Checks if we are holding a buffer and, if so, if the
 * actual data length is not zero.
 */
static inline bool_t spl_topo_int_out_port_has_data(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   return (0 != spl_topo_int_out_port_actual_data_len(topo_ptr, out_port_ptr));
}

/**
 * Checks if the input port has a pending media format event set.
 */
static inline bool_t spl_topo_ip_port_has_pending_media_fmt(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   return in_port_ptr->t_base.common.flags.media_fmt_event;
}

/**
 * Checks if the output port has a pending media format event set.
 */
static inline bool_t spl_topo_op_port_has_pending_media_fmt(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   return out_port_ptr->t_base.common.flags.media_fmt_event;
}

/**
 * Check if a port has not yet recieved media format. If media format is not valid, that is also considered not received.
 */
static inline bool_t spl_topo_media_format_not_received_on_port(spl_topo_t *topo_ptr, gen_topo_common_port_t *port_cmn_ptr)
{
   return !port_cmn_ptr->flags.is_mf_valid;
}

/**
 * Checks if the framework should handle flushing eos (by pushing zeros and propagating flushing eos) or not.
 * The framework should push zeros if the module doesn't support metadata. Also the fwk can only handle for SISO
 * modules, however non-SISO modules MUST handle md so the SISO check is technically not required.
 */
static inline bool_t spl_topo_fwk_handles_flushing_eos(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr)
{
   return (!(module_ptr->t_base.flags.supports_metadata)) && (module_ptr->t_base.gu.flags.is_siso);
}

/**
 * Clears all associated fields in the output port's internal ts_disc structure.
 */
static inline void spl_topo_clear_output_timestamp_discontinuity(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   out_port_ptr->flags.ts_disc     = FALSE;
   out_port_ptr->ts_disc_pos_bytes = 0;
   out_port_ptr->disc_timestamp    = 0;
}

/**
 * Returns the output port connected to this input port. There are 2 cases:
 * 1. Input port is external. There is no connected port so return the external port.
 * 2. Input port is internal. Return the connected output port.
 *
 * We pass in two double pointers to distinguish whether we found a connected internal
 * output port or connected external input port.
 */
static inline void spl_topo_get_connected_int_or_ext_op_port(spl_topo_t *             topo_ptr,
                                               spl_topo_input_port_t *  in_port_ptr,
                                               spl_topo_output_port_t **connected_int_out_port_pptr,
                                               spl_topo_input_port_t ** ext_in_port_pptr)
{
   // Assign both output params to NULL, one will be overwritten by this function.
   *connected_int_out_port_pptr = NULL;
   *ext_in_port_pptr            = NULL;

   if (in_port_ptr->t_base.gu.ext_in_port_ptr)
   {
      // Input port is external, it has the external input buffer.
      *ext_in_port_pptr = in_port_ptr;
   }
   else
   {
      *connected_int_out_port_pptr = (spl_topo_output_port_t *)in_port_ptr->t_base.gu.conn_out_port_ptr;
   }
}

/**
 * Gets the downstream input port (or external output port) connected to this output port.
 */
static inline void spl_topo_get_connected_int_or_ext_ip_port(spl_topo_t *topo_ptr,
                                               spl_topo_output_port_t * out_port_ptr,
                                               spl_topo_input_port_t ** connected_int_in_port_pptr,
                                               spl_topo_output_port_t **ext_out_port_pptr)
{

   // Assign both output params to NULL, one will be overwritten by this function.
   *connected_int_in_port_pptr = NULL;
   *ext_out_port_pptr          = NULL;

   if (out_port_ptr->t_base.gu.ext_out_port_ptr)
   {
      // Input port is external, it has the external input buffer.
      *ext_out_port_pptr = out_port_ptr;
   }
   else
   {
      *connected_int_in_port_pptr = (spl_topo_input_port_t *)out_port_ptr->t_base.gu.conn_in_port_ptr;
   }
}
#ifdef __cplusplus
}
#endif //__cplusplus

// clang-format on

#endif // #ifndef SPL_TOPO_I_H_
