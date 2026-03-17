/**
@file irm_i.h

@brief Internal header for Integrated Resource Monitor (IRM).

================================================================================
Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
==============================================================================*/
// clang-format off
// clang-format on

#ifndef _IRM_I_H_
#define _IRM_I_H_

#include "ar_osal_types.h"
#include "irm_api.h"
#include "irm_pvt_api.h"
#include "irm_dev_cfg.h"
#include "spf_cmn_if.h"
#include "spf_list_utils.h"
#include "spf_utils.h"
#include "posal.h"
#include "gpr_api_inline.h"
#include "apm.h"
#include "apm_cntr_if.h"
#include "apm_offload_mem.h"
#include "irm.h"
#include "rtm_logging_api.h"
//#include "irm_offload_utils.h"
#include "spf_sys_util.h"
#include "posal_power_mgr.h"

#define IRM_DEBUG 1

#define ALIGN_8_BYTES(a) ((a + 7) & (0xFFFFFFF8))
#define ALIGN_4_BYTES(a) ((a + 3) & (0xFFFFFFFC))
#define IRM_MAX_CMD_Q_ELEMENTS 8
#define IRM_CMD_Q_BYTE_SIZE (POSAL_QUEUE_GET_REQUIRED_BYTES(PRM_MAX_CMD_Q_ELEMENTS))
#define IRM_MAX_RSP_Q_ELEMENTS 16
#define IRM_RSP_Q_BYTE_SIZE (POSAL_QUEUE_GET_REQUIRED_BYTES(PRM_MAX_RSP_Q_ELEMENTS))
#define IRM_MSG_PREFIX "IRM:"
#define IRM_MAX_NUM_HEAP_ID POSAL_MEM_TYPE_NUM_SUPPORTED
#define IRM_LOG_CODE (0x184B)
#define IRM_MIN_PROFILING_PERIOD_1MS (1000)
#define IRM_MIN_PROFILES_PER_REPORT_1 (1)
#define IRM_HEAP_ID_MASK (0xFFFFFFC0)

#define IRM_INTERNAL_STATIC_HEAP_ID                                                                                    \
   (MODIFY_STATIC_MODULE_HEAP_ID_FOR_MEM_TRACKING(IRM_MODULE_INSTANCE_ID, POSAL_HEAP_DEFAULT))

typedef struct irm_cmd_ctrl_t irm_cmd_ctrl_t;

/** IRM PM Information Struct */
typedef struct irm_pm_info_t
{
   posal_pm_handle_t pm_handle_ptr;
   /**< Pointer to PM handle */

   posal_pm_register_t register_info;
   /**< PM register information */

   posal_signal_t pm_signal;
   /**< Signal for IRM pm voting */
} irm_pm_info_t;

typedef struct irm_metric_info_t
{
   irm_report_metric_t *metric_payload_ptr;
   void *               prev_statistic_ptr;
   void *               current_mod_statistics_ptr;
} irm_metric_info_t;

typedef struct irm_static_module_info_t
{
   uint32_t static_miid; // instance ID of the static service
   uint32_t heap_id;     // heap id used by static service for tracking
   int64_t  tid;         // thread id of the service (to query for profiling metrics)
} irm_static_module_info_t;

// Common structure used to store information for block, instance and Metirc IDs
typedef struct irm_node_obj_t
{
   uint32_t id; // Block ID or instance ID
   union
   {
      spf_handle_t *            handle_ptr;             // Valid only if ID is an Instance ID
      irm_static_module_info_t *static_module_info_ptr; // Valid iff ID is a static module id
   };
   uint32_t       cntr_iid;      // Valid only if block id type is MODULE
   posal_mutex_t *mod_mutex_ptr; // Mutex to read module params from cntrs, valid only if block type is MODULE
   uint32_t       heap_id;       // Heap id of the module
   bool_t         is_first_time;
   // Union to either store the gk list node or metric payload pointer.
   union
   {
      spf_list_node_t * head_node_ptr;
      irm_metric_info_t metric_info; // Has info about ptr to report payload and ptr to prev statistics of the metric
   };
} irm_node_obj_t;

typedef struct irm_rtm_header_t
{
   uint16_t                 version;
   uint16_t                 header_size;
   rtm_logging_param_data_t rtm_header;
} irm_rtm_header_t;

/** IRM core structure */
typedef struct irm_core_t
{
   uint32_t         sessoin_counter;             // Counter for each IRM session;
   uint32_t         profiling_period_us;         // Period in which the profiler profiles data
   uint32_t         num_profiles_per_report;     // Number of profiling periods to be done before reporting
   uint32_t         timer_tick_counter;          // Counts the timer tick till num_profiles_per_report
   bool_t           is_profiling_started;        // Variable to to keep track if the timer started
   uint64_t         previous_timer_tick_time;    // time during previous timer tick and metric collection happened
   uint8_t *        report_payload_ptr;          // Ptr to report payload
   uint32_t         report_payload_size;         // size of the report payload
   spf_list_node_t *block_head_node_ptr;         // Head node ptr containing the block obj
   uint32_t         irm_bufpool_handle;          // buf pool handle.
   void *           profiler_handle_ptr;         // Ptr to the profiler handle
   void *           test_fwk_event_ptr;          // Ptr to test fwk event info
   posal_mutex_t    cntr_mod_prof_enable_mutex;  // mutex lock for is_cntr_or_mod_prof_enabled
   bool_t           is_cntr_or_mod_prof_enabled; // Flag to track if any container or module prof is enabled
} irm_core_t;

/** IRM Module Struct */
typedef struct irm_t
{
   spf_handle_t           irm_handle;          // IRM thread handle
   spf_cmd_handle_t       irm_cmd_handle;      // IRM thread command handle
   posal_channel_t        cmd_channel;         // Mask for Q's owned by this obj
   posal_signal_t         kill_signal;         // Signal to destroy IRM module thread
   posal_signal_t         timer_signal;        // Signal for IRM report timer ticks
   posal_queue_t *        irm_cmd_q_ptr;       // IRM Command queue
   posal_queue_t *        irm_rsp_q_ptr;       // IRM Command rsp queue
   posal_timer_t          irm_report_timer;    // IRM report timer
   irm_pm_info_t          pm_info;             // IRM pm info
   uint32_t               gpr_handle;          // GPR handle
   bool_t                 thread_launched;     // if the thread launched
   POSAL_HEAP_ID          heap_id;             // heap ID configured during init
   spf_sys_util_handle_t *sys_util_handle_ptr; // Sys util handle
   irm_core_t             core;                // Core structure containing some Core info
   bool_t                 enable_all;          // TRUE if IRM is listening to all metrics from APM
   uint32_t
      num_usecase_handles; // Total number of handles in the use case (used to query APM the proper # of instances)
   spf_list_node_t
      *tmp_metric_payload_list; // Caches the requested metrics in the enable all case (obj type param_id_enable_all_t)
   spf_list_node_t *cmd_ctrl_list; /**< List of commands under process (obj type: amdb_cmd_ctrl_t).
                                       The following is to store the cmd ctx based on token.
                                       This is only used on the Master DSP*/
} irm_t;

struct irm_cmd_ctrl_t
{
   uint16_t               token;              /**< Token used to identify the command ctrl obj */
   uint32_t               cmd_opcode;         /**< Command opcode under process */
   spf_msg_t              cmd_msg;            /**< Command payload GK msg */
   uint32_t               dst_domain_id;      /**< Destination proc domain id */
   bool_t                 is_out_of_band;     /**< Flag to indicate out of band */
   uint32_t               bytes_written;      /**< Number of bytes written to the original payload*/
   uint32_t               num_resp_pending;   /**< Indicates how many responses remain for the offloaded cmd */
   apm_offload_ret_info_t ret_info;           /**< offload shared memory info*/
   void *                 loaned_mem_ptr;     /**< Loaned mem ptr associated with the cmd payload. NULL if inband. */
   void *                 master_payload_ptr; /**< Loaned mem ptr associated with the cmd payload. NULL if inband. */
   spf_list_node_t *get_cfg_rsp_cmd_ctrl_list_ptr; /**< list of irm_get_cfg_resp_cmd_ctrl_t to get the location of the
                                                      responses for get cfg msgs */
};

typedef struct irm_cmd_ctrl_t irm_cmd_ctrl_t;

typedef struct
{
   uint16_t token; /**< Token used to identify the command ctrl obj */
   void
      *set_cfg_orig_resp_ptr; /**< points to where in the original msg the response for the current get cfg should go */
} irm_get_cfg_resp_cmd_ctrl_t;

typedef struct irm_capability_node_t
{
   uint32_t  block_id;
   uint32_t  num_metrics;
   uint32_t *capability_ptr;
} irm_capability_node_t;

// Extern values to expose the irm capabilities
extern irm_system_capabilities_t g_irm_cmn_capabilities;
extern irm_capability_node_t *   g_capability_list_ptr;
extern uint32_t                  g_num_capability_blocks;

uint32_t    irm_gpr_call_back_f(gpr_packet_t *gpr_pkt_ptr, void *cb_ctx_ptr);
ar_result_t irm_process_cmd_q(irm_t *irm_ptr);
ar_result_t irm_process_rsp_q(irm_t *irm_ptr);
ar_result_t irm_process_timer_tick(irm_t *irm_ptr);
ar_result_t irm_cmdq_apm_cmd_handler(irm_t *irm_ptr, spf_msg_t *msg_ptr);
ar_result_t irm_cmdq_gpr_cmd_handler(irm_t *irm_ptr, spf_msg_t *msg_ptr);
ar_result_t irm_rspq_gpr_rsp_handler(irm_t *irm_ptr, spf_msg_t *msg_ptr);
ar_result_t irm_rspq_spf_rsp_handler(irm_t *irm_ptr, spf_msg_t *msg_ptr);
ar_result_t irm_timer_tick_handler(irm_t *irm_ptr);
ar_result_t irm_set_get_cfg_handler(irm_t *    irm_ptr,
                                    uint8_t *  data_ptr,
                                    uint32_t   payload_size,
                                    bool_t     is_set,
                                    bool_t *   is_offload_needed_ptr,
                                    uint32_t * dest_domain_id_ptr,
                                    spf_msg_t *msg_ptr);
ar_result_t irm_set_cfg(irm_t *                  irm_ptr,
                        apm_module_param_data_t *module_param_data_ptr,
                        bool_t *                 is_offload_needed_ptr,
                        uint32_t *               dest_domain_id_ptr);
ar_result_t irm_get_cfg(irm_t *                  irm_ptr,
                        apm_module_param_data_t *module_param_data_ptr,
                        spf_msg_t *              msg_ptr,
                        irm_cmd_ctrl_t **        curr_cmd_ctrl_pptr);

ar_result_t irm_handle_set_enable(irm_t *irm_ptr, param_id_enable_disable_metrics_t *param_ptr);
ar_result_t irm_handle_set_disable(irm_t *irm_ptr, param_id_enable_disable_metrics_t *param_ptr);
ar_result_t irm_recreate_report_payload(irm_t *irm_ptr);
ar_result_t irm_populate_report_payload_info(irm_t *irm_ptr);
void        irm_clean_up_all_nodes(irm_t *irm_ptr);
void        irm_cleanup_stray_nodes(irm_t *irm_ptr);
void        irm_clean_up_enable_all(irm_t *irm_ptr);
uint32_t    irm_get_metric_payload_size(uint32_t metric_id);
uint32_t    irm_get_prev_metric_payload_size(uint32_t metric_id);
ar_result_t irm_handle_new_config(irm_t *irm_ptr);
void        irm_reset_report_payload(irm_t *irm_ptr);
ar_result_t irm_send_rtm_packet(irm_t *irm_ptr);
ar_result_t irm_send_get_cfg_gpr_resp(gpr_packet_t *gpr_rsp_pkt_ptr,
                                      gpr_packet_t *gpr_pkt_ptr,
                                      uint32_t      payload_size,
                                      uint8_t *     cmd_payload_ptr,
                                      bool_t        is_oob);

// irm_list_util functions
spf_list_node_t *irm_find_node_from_id(spf_list_node_t *head_ptr, uint32_t id);
void             irm_return_list_objs_to_pool(spf_list_node_t *head_ptr);
void             irm_delete_list(spf_list_node_t **node_pptr);
void             irm_delete_node(spf_list_node_t **head_pptr, spf_list_node_t **node_pptr);
void             irm_get_node_obj(irm_t *irm_ptr, irm_node_obj_t **node_obj_pptr);
irm_node_obj_t * irm_check_insert_node(irm_t *irm_ptr, spf_list_node_t **head_pptr, uint32_t id);

ar_result_t irm_handle_send_cntr_module_enable_disable_info(irm_t *irm_ptr, uint8_t *payload_ptr, bool_t is_enable);
void        irm_update_cntr_mod_prof_enable_flag(irm_t * irm_ptr,
                                                 bool_t *is_mem_prof_enabled_ptr,
                                                 bool_t *is_mod_process_prof_enabled_ptr);
ar_result_t irm_request_instance_handles_payload(irm_t *irm_ptr);
ar_result_t irm_request_all_instance_handles(irm_t *irm_ptr);
ar_result_t irm_parse_instance_handle_rsp(irm_t *irm_ptr, apm_module_param_data_t *param_data_ptr);
ar_result_t irm_parse_all_instance_handle_rsp(irm_t *irm_ptr, apm_module_param_data_t *param_data_ptr);

void        irm_update_cntr_handle_list(spf_handle_t **mod_cntr_handles_pptr,
                                        uint32_t *     num_cntrs_with_modules_ptr,
                                        spf_handle_t * handle_ptr);
void        irm_handle_send_cntr_module_enable_info(irm_t *        irm_ptr,
                                                    spf_handle_t **module_cntr_handle_set_pptr,
                                                    uint32_t       num_cntrs_with_modules,
                                                    uint32_t       is_enable);
ar_result_t irm_parse_cntr_rsp(irm_t *irm_ptr, apm_module_param_data_t *param_data_ptr);

ar_result_t irm_route_get_cfg_rsp_to_client(irm_t *irm_ptr, spf_msg_t *msg_ptr);

// Static module utils
ar_result_t irm_insert_all_static_modules(irm_t *irm_ptr);
ar_result_t irm_fill_static_instance_info(irm_t *irm_ptr);

typedef struct irm_t irm_t;
bool_t               irm_is_supported_metric(uint32_t block_id, uint32_t metric_id);
void                 irm_get_supported_metric_arr(uint32_t block_id, uint32_t **metric_id_list, uint32_t *num_elements);
ar_result_t          irm_profiler_init(irm_t *irm_ptr);
ar_result_t          irm_collect_and_fill_info(irm_t *irm_ptr, uint32_t frame_size_ms);
void                 irm_profiler_deinit(irm_t *irm_ptr);

#endif /* _IRM_I_H_ */
