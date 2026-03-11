/**
 * \file gen_topo_metadata.h
 *
 * \brief
 *
 *     Metadata header.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef GEN_TOPO_METADATA_H_
#define GEN_TOPO_METADATA_H_

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

typedef struct gen_topo_t gen_topo_t;
typedef struct gen_topo_module_t gen_topo_module_t;
typedef struct gen_topo_common_port_t gen_topo_common_port_t;
typedef struct gen_topo_input_port_t gen_topo_input_port_t;
typedef struct gen_topo_output_port_t gen_topo_output_port_t;

/**
 * Container specific structure for EoS
 */
typedef struct gen_topo_eos_cargo_t
{
   bool_t                        did_eos_come_from_ext_in;  /**< if flushing EOS came from ext-in */
   void *                        inp_ref;                   /**< to be removed after SPL_CNTR gets rid of it */
   uint32_t                      inp_id;                    /**< Unique ID of the external input port. */
   uint32_t                      ref_count;                 /**< every time split happens (inside the container) this is incremented. */
} gen_topo_eos_cargo_t;


/**
 * EOS MD specific event payload
 */
typedef struct gen_topo_eos_event_payload_t
{
   uint32_t                      is_flushing_eos;        /**< Specifies if the EOS is flushing EOS */
} gen_topo_eos_event_payload_t;


typedef struct gen_topo_tracking_md_context_t
{
   module_cmn_md_tracking_payload_t *tracking_payload_ptr; /**< tracking payload ptr, in case payload has to be sent out */
   uint32_t                          metadata_id;          /**< Unique ID for the metadata */
   uint32_t                          module_instance_id;   /**< module instance id, needed for metadata tracking payload */
   uint32_t                          render_status;        /**< 0 - MD_TRACKING_STATUS_IS_RENDERED
                                                                1 - MD_TRACKING_STATUS_IS_DROPPED */
   module_cmn_md_flags_t             flags;                /**< metadata flags */
   uint32_t                          log_id;               /**< log_id of the service raising the event */
   void                              *md_payload_ptr;      /** < Metadata specific payload to provide any custom
                                                                 information while raising event*/
} gen_topo_tracking_md_context_t;

capi_err_t gen_topo_capi_metadata_create(void *                 context_ptr,
                                         module_cmn_md_list_t **md_list_pptr,
                                         uint32_t               size,
                                         capi_heap_id_t         c_heap_id,
                                         bool_t                 is_out_band,
                                         module_cmn_md_t **     md_pptr);

capi_err_t gen_topo_capi_metadata_propagate(void *                      context_ptr,
                                            capi_stream_data_v2_t *     input_stream_ptr,
                                            capi_stream_data_v2_t *     output_stream_ptr,
                                            module_cmn_md_list_t **     internal_md_list_pptr,
                                            uint32_t                    algo_delay_us,
                                            intf_extn_md_propagation_t *input_md_info_ptr,
                                            intf_extn_md_propagation_t *output_md_info_ptr);

capi_err_t gen_topo_capi_metadata_clone(void *                 context_ptr,
                                        module_cmn_md_t *      md_ptr,
                                        module_cmn_md_list_t **out_md_list_pptr,
                                        capi_heap_id_t         c_heap_id);

void gen_topo_free_eos_cargo(gen_topo_t *topo_ptr, module_cmn_md_t *md_ptr, module_cmn_md_eos_t *eos_metadata_ptr);

ar_result_t gen_topo_clone_eos(gen_topo_t *           topo_ptr,
                               module_cmn_md_t *      in_metadata_ptr,
                               module_cmn_md_list_t **out_md_list_pptr,
                               POSAL_HEAP_ID          heap_id);

ar_result_t gen_topo_metadata_prop_for_sink(gen_topo_module_t *module_ptr, capi_stream_data_v2_t *output_stream_ptr);

ar_result_t gen_topo_raise_md_cloning_event(gen_topo_t *                      topo_ptr,
                                            module_cmn_md_tracking_payload_t *md_tracking_ptr,
                                            uint32_t                          metadata_id);


ar_result_t gen_topo_create_eos_for_cntr(gen_topo_t *               topo_ptr,
                                         gen_topo_input_port_t *    input_port_ptr,
                                         uint32_t                   input_port_id,
                                         POSAL_HEAP_ID              heap_id,
                                         module_cmn_md_list_t **    eos_md_list_pptr,
                                         module_cmn_md_flags_t *    in_md_flags,
                                         module_cmn_md_tracking_t * eos_tracking_ptr,
                                         module_cmn_md_eos_flags_t *eos_flag_ptr,
                                         uint32_t                   bytes_across_ch,
                                         topo_media_fmt_t *         media_fmt_ptr);

ar_result_t gen_topo_create_eos_cntr_ref(gen_topo_t *           topo_ptr,
                                         POSAL_HEAP_ID          heap_id,
                                         gen_topo_input_port_t *input_port_ptr,
                                         uint32_t               input_id,
                                         gen_topo_eos_cargo_t **eos_cargo_pptr);

ar_result_t gen_topo_handle_eos_md_after_process_for_out_port(gen_topo_t *            topo_ptr,
                                                              gen_topo_module_t *     module_ptr,
                                                              gen_topo_output_port_t *out_port_ptr);
ar_result_t gen_topo_handle_eos_md_after_process_for_inp_port(gen_topo_t *           topo_ptr,
                                                              gen_topo_module_t *    module_ptr,
                                                              gen_topo_input_port_t *in_port_ptr);
ar_result_t gen_topo_destroy_all_metadata(uint32_t               log_id,
                                          void *                 module_ctx_ptr,
                                          module_cmn_md_list_t **md_list_pptr,
                                          bool_t                 is_dropped);
ar_result_t gen_topo_drop_all_metadata_within_range(uint32_t                log_id,
                                                    gen_topo_module_t *     module_ptr,
                                                    gen_topo_common_port_t *cmn_port_ptr,
                                                    uint32_t                data_dropped_bytes,
													             bool_t                  keep_eos_and_ba_md);

ar_result_t gen_topo_do_md_offset_math(uint32_t          log_id,
                                       uint32_t *        offset_ptr,
                                       uint32_t          bytes_per_buf,
                                       topo_media_fmt_t *med_fmt_ptr,
                                       bool_t            need_to_add);
ar_result_t gen_topo_respond_and_free_eos(gen_topo_t *           topo_ptr,
                                          uint32_t               sink_miid,
                                          module_cmn_md_list_t * md_list_ptr,
                                          bool_t                 is_eos_rendered,
                                          module_cmn_md_list_t **head_pptr,
                                          bool_t                 override_ctrl_to_disable_tracking_event);
bool_t      gen_topo_is_flushing_eos(module_cmn_md_t *md_ptr);
ar_result_t gen_topo_metadata_adj_offset(gen_topo_t *          topo_ptr,
                                         topo_media_fmt_t *    med_fmt_ptr,
                                         module_cmn_md_list_t *md_list_ptr,
                                         uint32_t              bytes_consumed,
                                         bool_t                true_add_false_sub);

void gen_topo_populate_metadata_extn_vtable(gen_topo_module_t *                    module_ptr,
                                            intf_extn_param_id_metadata_handler_t *handler_ptr);

ar_result_t gen_topo_md_list_modify_md_when_new_data_arrives(gen_topo_t *           topo_ptr,
                                                             gen_topo_module_t *    module_ptr,
                                                             module_cmn_md_list_t **md_list_pptr,
                                                             uint32_t               end_offset,
                                                             bool_t *               new_marker_eos_ptr,
                                                             bool_t *               has_new_dfg_ptr);

ar_result_t gen_topo_create_dfg_metadata(gen_topo_t            *topo_ptr,
                                         module_cmn_md_list_t **metadata_list_pptr,
                                         POSAL_HEAP_ID          heap_id,
                                         module_cmn_md_t      **dfg_md_pptr,
                                         uint32_t               bytes_in_buf,
                                         topo_media_fmt_t      *media_format_ptr);

ar_result_t gen_topo_clone_md(gen_topo_t *           topo_ptr,
                              module_cmn_md_t *      md_ptr,
                              module_cmn_md_list_t **out_md_list_pptr,
                              POSAL_HEAP_ID          heap_id,
							  bool_t     disable_tracking_in_cloned_md);

ar_result_t gen_topo_free_md(gen_topo_t *           topo_ptr,
                             module_cmn_md_list_t * md_list_ptr,
                             module_cmn_md_t *      md_ptr,
                             module_cmn_md_list_t **head_pptr);

bool_t gen_topo_md_list_has_dfg(module_cmn_md_list_t *list_ptr);
bool_t gen_topo_md_list_has_flushing_eos(module_cmn_md_list_t *list_ptr);
bool_t gen_topo_md_list_has_flushing_eos_or_dfg(module_cmn_md_list_t *list_ptr);
bool_t gen_topo_md_list_has_buffer_associated_md(module_cmn_md_list_t *list_ptr);

ar_result_t gen_topo_metadata_create(gen_topo_t            *topo_ptr,
                                     module_cmn_md_list_t **md_list_pptr,
                                     uint32_t               size,
                                     POSAL_HEAP_ID          heap_id,
                                     bool_t                 is_out_band,
                                     module_cmn_md_t      **md_pptr);

capi_err_t gen_topo_capi_metadata_destroy(void *                 context_ptr,
                                          module_cmn_md_list_t * md_list_ptr,
                                          bool_t                 is_dropped,
                                          module_cmn_md_list_t **head_pptr,
										  uint32_t               md_actual_sink_miid,
										  bool_t                 override_ctrl_to_disable_tracking_event);
capi_err_t gen_topo_capi_metadata_modify_at_data_flow_start(void *                 context_ptr,
                                                            module_cmn_md_list_t * md_node_pptr,
                                                            module_cmn_md_list_t **head_pptr);

capi_err_t gen_topo_capi_metadata_create_with_tracking(void *                    context_ptr, // context cannot be NULL
                                                       module_cmn_md_list_t **   md_list_pptr,
                                                       uint32_t                  size,
                                                       capi_heap_id_t            heap_id,
                                                       uint32_t                  metadata_id,
                                                       module_cmn_md_flags_t     flags,
                                                       module_cmn_md_tracking_t *tracking_info_ptr,
                                                       module_cmn_md_t **        md_pptr);


static inline bool_t gen_topo_check_move_metadata(uint32_t md_offset,
                                                  uint32_t in_bytes_before,
                                                  uint32_t in_bytes_consumed_per_ch)
{
   bool_t in_data_present = (0 != in_bytes_before);

   // If offset of metadata belongs to data that's consumed, then move MD also considered consumed by the module
   // If input is not present, we can propagate only if offset is zero.
   // When input consumed is zero, MD is moved only if input was zero to begin with; otherwise, even if offset is
   // zero, MD is not propagated.
   bool_t move_md = ((in_data_present && (md_offset <= in_bytes_consumed_per_ch) && (0 != in_bytes_consumed_per_ch)) ||
                     (!in_data_present && (0 == md_offset)));

   return move_md;
}

ar_result_t gen_topo_validate_metadata_eof(gen_topo_module_t *module_ptr);

void gen_topo_populate_metadata_for_peer_cntr(gen_topo_t *           gen_topo_ptr,
                                              gu_ext_out_port_t *    ext_out_port_ptr,
                                              module_cmn_md_list_t **md_list_pptr,
											  module_cmn_md_list_t **out_md_list_pptr,
                                              bool_t *               out_buf_has_flushing_eos_ptr);

ar_result_t gen_topo_check_realloc_md_list_in_peer_heap_id(uint32_t               log_id,
                                                         gu_ext_out_port_t *    ext_out_port_ptr,
                                                         module_cmn_md_list_t **md_list_pptr);

void gen_topo_convert_client_md_flag_to_int_md_flags(uint32_t client_md_flags, module_cmn_md_flags_t *int_md_flags);
void gen_topo_convert_int_md_flags_to_client_md_flag(module_cmn_md_flags_t int_md_flags, uint32_t *client_md_flags);

void gen_topo_drop_md(uint32_t                          log_id,
                      module_cmn_md_tracking_payload_t *tracking_ptr,
                      uint32_t                          metadata_id,
                      module_cmn_md_flags_t             flags,
                      uint32_t                          module_instance_id,
                      bool_t                            ref_counted_obj_created,
                      void *                            md_event_payload_ptr);

void gen_topo_check_free_md_ptr(void **ptr, bool_t pool_used);

ar_result_t gen_topo_raise_tracking_event(gen_topo_t *          topo_ptr,
                                          uint32_t              source_miid,
                                          module_cmn_md_list_t *md_list_ptr,
                                          bool_t                is_md_rendered,
                                          void *                md_payload_ptr,
                                          bool_t                override_ctrl_to_disable_tracking_event);

ar_result_t gen_topo_push_zeros_at_eos_util_(gen_topo_t *           topo_ptr,
                                             gen_topo_module_t *    module_ptr,
                                             gen_topo_input_port_t *in_port_ptr);

ar_result_t gen_topo_realloc_md_list_in_peer_heap_id(uint32_t               log_id,
                                                     module_cmn_md_list_t **md_list_pptr,
                                                     module_cmn_md_list_t * node_ptr,
                                                     POSAL_HEAP_ID          downstream_heap_id);

#if defined(__cplusplus)
}
#endif // __cplusplus

#endif /* GEN_TOPO_METADATA_H_ */
