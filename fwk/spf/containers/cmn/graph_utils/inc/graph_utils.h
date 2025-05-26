#ifndef GRAPH_UTILS_H_
#define GRAPH_UTILS_H_

/**
 * \file graph_utils.h
 *
 * \brief
 *
 *     graph utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_utils.h"
#include "apm_sub_graph_api.h"
#include "ar_ids.h"
#include "gpr_api_inline.h"
#include "apm_cntr_if.h"
#include "capi_cmn.h"


#define UNITY_Q4 0x10
//#define ENABLE_GU_TEST

// Enable to add debug prints.
//#define GU_DEBUG

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/* =======================================================================
  Macros
========================================================================== */
#ifndef ALIGN_128_BYTES
#define ALIGN_128_BYTES(a)   ((a + 127) & (0xFFFFFF80))
#endif

#ifndef ALIGN_64_BYTES
#define ALIGN_64_BYTES(a)   ((a + 63) & (0xFFFFFFC0))
#endif

#ifndef ALIGN_8_BYTES
#define ALIGN_8_BYTES(a)    ((a + 7) & (0xFFFFFFF8))
#endif

#ifndef ALIGN_4_BYTES
#define ALIGN_4_BYTES(a)    ((a + 3) & (0xFFFFFFFC))
#endif

#ifndef ALIGNED_SIZE_W_QUEUES
#define ALIGNED_SIZE_W_QUEUES(t, n) (ALIGN_8_BYTES(sizeof(t)) + posal_queue_get_size() * n)
#endif

/* =======================================================================
  Struct Definitions
========================================================================== */

typedef struct gu_t                    gu_t;
typedef struct gu_sg_t                 gu_sg_t;
typedef struct gu_module_t             gu_module_t;
typedef struct gu_cmn_port_t           gu_cmn_port_t;
typedef struct gu_input_port_t         gu_input_port_t;
typedef struct gu_output_port_t        gu_output_port_t;
typedef struct gu_ext_in_port_t        gu_ext_in_port_t;
typedef struct gu_ext_out_port_t       gu_ext_out_port_t;
typedef struct gu_ctrl_port_t          gu_ctrl_port_t;
typedef struct gu_ext_ctrl_port_t      gu_ext_ctrl_port_t;
typedef struct gu_module_list_t        gu_module_list_t;
typedef struct gu_cmn_port_list_t      gu_cmn_port_list_t;
typedef struct gu_input_port_list_t    gu_input_port_list_t;
typedef struct gu_output_port_list_t   gu_output_port_list_t;
typedef struct gu_sg_list_t            gu_sg_list_t;
typedef struct gu_ext_in_port_list_t   gu_ext_in_port_list_t;
typedef struct gu_ext_out_port_list_t  gu_ext_out_port_list_t;
typedef struct gu_ctrl_port_list_t     gu_ctrl_port_list_t;
typedef struct gu_ext_ctrl_port_list_t gu_ext_ctrl_port_list_t;
typedef struct gu_async_graph_t        gu_async_graph_t;

typedef struct gu_ctrl_port_prop_intent_list_t gu_ctrl_port_prop_intent_list_t;

/* List structures. These should all mirror spf_list_node_t to typecast. */

/* Modules */
typedef struct gu_module_list_t
{
   gu_module_t *     module_ptr;
   gu_module_list_t *next_ptr;
   gu_module_list_t *prev_ptr;
} gu_module_list_t;

/* cmn data port list */
typedef struct gu_cmn_port_list_t
{
   gu_cmn_port_t *     cmn_port_ptr;
   gu_cmn_port_list_t *next_ptr;
   gu_cmn_port_list_t *prev_ptr;
} gu_cmn_port_list_t;

/* Module input ports */
typedef struct gu_input_port_list_t
{
   gu_input_port_t *     ip_port_ptr;
   gu_input_port_list_t *next_ptr;
   gu_input_port_list_t *prev_ptr;
} gu_input_port_list_t;

/* Module output ports */
typedef struct gu_output_port_list_t
{
   gu_output_port_t *     op_port_ptr;
   gu_output_port_list_t *next_ptr;
   gu_output_port_list_t *prev_ptr;
} gu_output_port_list_t;

/* Subgraphs */
typedef struct gu_sg_list_t
{
   gu_sg_t *     sg_ptr;
   gu_sg_list_t *next_ptr;
   gu_sg_list_t *prev_ptr;
} gu_sg_list_t;

/* Container input ports */
typedef struct gu_ext_in_port_list_t
{
   gu_ext_in_port_t *     ext_in_port_ptr;
   gu_ext_in_port_list_t *next_ptr;
   gu_ext_in_port_list_t *prev_ptr;
} gu_ext_in_port_list_t;

/* Container output ports */
typedef struct gu_ext_out_port_list_t
{
   gu_ext_out_port_t *     ext_out_port_ptr;
   gu_ext_out_port_list_t *next_ptr;
   gu_ext_out_port_list_t *prev_ptr;
} gu_ext_out_port_list_t;

/**
 * Graph states
 */
typedef enum gu_status_t
{
   GU_STATUS_DEFAULT = 0,
   GU_STATUS_NEW = 1,
   GU_STATUS_UPDATED = 2,
   GU_STATUS_CLOSING = 3 //used only for sg and external ports
} gu_status_t;

typedef struct gu_peer_handle_t
{
   spf_handle_t *spf_handle_ptr;     /**< gk handle of the peer */
   uint32_t      module_instance_id; /**< module-instance-id of the peer */
   uint32_t      port_id;            /**< port-id of the peer */
   POSAL_HEAP_ID heap_id;            /**< heap-id of the peer */
} gu_peer_handle_t;

/**
 * External input
 */
typedef struct gu_ext_in_port_t
{
   spf_handle_t     this_handle;     /**< This external port's handle. must be first element */
   gu_peer_handle_t upstream_handle; /**< gu_peer_handle_t::spf_handle = gu_ext_out_port_t */
   gu_input_port_t *int_in_port_ptr; /**< Reference to the internal port that's connected to this external port */
   gu_sg_t         *sg_ptr;          /*Reference to the SG pointer*/
   gu_status_t      gu_status;
} gu_ext_in_port_t;

/**
 * External output
 */
typedef struct gu_ext_out_port_t
{
   spf_handle_t      this_handle;       /**< This external port's handle. must be first element */
   gu_peer_handle_t  downstream_handle; /**<gu_peer_handle_t::spf_handle = gu_ext_in_port_t */
   gu_output_port_t *int_out_port_ptr;  /**< Reference to the internal port that's connected to this external port */
   gu_sg_t          *sg_ptr;            /*Reference to the SG pointer*/
   gu_status_t       gu_status;
} gu_ext_out_port_t;

typedef struct gu_cmn_port_flags_t
{
   uint8_t mark : 1;          /**< for graph sort usage */
   uint8_t elementary_pending_attachment : 1;     /**< used only for output ports - TRUE means downstream module is an elementary module and pending attachment. */
} gu_cmn_port_flags_t;

/**
 * Common struct for input and output port.
 */
typedef struct gu_cmn_port_t
{
   uint32_t            id;         /**< This is the ID of the port. Index of the port (CAPIv2 requirement) is different. Framework sets ID <->
                                          Index mapping to the module.*/
   gu_module_t *       module_ptr; /**< Ptr of the module to which this input port belongs */
   uint8_t             index;      /**< index (0 to num_ports) is determined by number of active ports */
   gu_cmn_port_flags_t flags;
   gu_status_t         gu_status;
} gu_cmn_port_t;

/**
 * input ports
 */
typedef struct gu_input_port_t
{
   gu_cmn_port_t     cmn;                /**< Must be first element */
   gu_output_port_t  *conn_out_port_ptr; /**< Upstream or source port connecting to this input port. If ext_in_port_ptr
                                           != NULL, then this is NULL.*/
   gu_ext_in_port_t  *ext_in_port_ptr;   /**< Non-NULL if connected to ports/modules external to this container.
                                           ext_in_port_list_ptr has the primary ref to this pointer. */
   uint16_t          depth;              /**< Depth is a measure of distance from this port to its upstream connected external input ports. It
                                           might be needed to compare depths of ports on different legs of the graph. */
} gu_input_port_t;

/**
 * output ports
 */
typedef struct gu_output_port_t
{
   gu_cmn_port_t     cmn;                /**< Must be first element */
   gu_input_port_t   *conn_in_port_ptr;  /**< Downstream or destination port connecting to this output port. If
                                           ext_out_port_ptr != NULL, then this is NULL.*/
   gu_ext_out_port_t *ext_out_port_ptr; /**< Non-NULL if connected to ports/modules external to this container.
                                           ext_out_port_list_ptr has the primary ref to this pointer.  */

   gu_module_t        *attached_module_ptr;  /**< The elementary module attached to this port.  */
} gu_output_port_t;

/**
 * Attached modules
 * If a module is an elementary module (capi property CAPI_IS_ELEMENTARY), it may be removed from the sorted list and "attached" to
 * the upstream connected output port. This attachment also modifies upstream and downstream module's connected input/output ports to
 * completely bypass the elementary module. The elementary module's input and output ports remain allocated but disconnected from the
 * graph, having NULL connected and ext port pointers. When looping through the sorted list, the "attached" module works directly in
 * the context of the "host" output port - the output port's buffer is passed to the module as both input and output data pointers.
 * This way, MPPS overhead of setting up/tearing down elementary modules can be avoided.
 */

/**
 * Control port
 */
typedef struct gu_ext_ctrl_port_t
{
   spf_handle_t      this_handle;
                                          /**< Incoming IMC messages are buffered in this queue. In case of control port, spf_handle.cmd_q
                                            corresponds to incoming intent msg_q and buffer q corresponds is for intent buffers allocated
                                            for the outgoing intents. */
   gu_peer_handle_t  peer_handle;         /**< Outgoing control messages are pushed using this handle. */
   uint32_t          peer_domain_id;      /**< Valid for offloaded cases - inter-proc control links. Will be host proc id if its same
                                           domain imcl*/
   gu_ctrl_port_t    *int_ctrl_port_ptr;  /**< pointer to internal control port*/
   gu_sg_t           *sg_ptr;             /**< Reference to SG ptr.*/
   gu_status_t       gu_status;
} gu_ext_ctrl_port_t;

/**
 * Control port
 */
typedef struct gu_ctrl_port_t
{
   uint32_t            id;                /**< Control port ID */
   POSAL_HEAP_ID       operating_heap_id; /**< heap id where this control port related resources are allocated. */
   gu_module_t *       module_ptr;        /**< Module to which the current control port is connected to.*/
   gu_ext_ctrl_port_t *ext_ctrl_port_ptr; /**< This is non NULL, if the peer module is in another container */
   gu_ctrl_port_t *    peer_ctrl_port_ptr; /**< Module to which the current control port is connected to.*/

   gu_ctrl_port_prop_intent_list_t *intent_info_ptr; /**< Control link intent list info ptr. */
   gu_status_t         gu_status;
} gu_ctrl_port_t;

/*
 * Control port intent list structure.
 * */
#include "spf_begin_pragma.h"
struct gu_ctrl_port_prop_intent_list_t
{
   uint32_t num_intents;       /**< Number of intents associated with a control link */
   uint32_t intent_id_list[0]; /**< List of intents associated with a control link */
}
#include "spf_end_pragma.h"
;

/*
 * Control port list pointer
 * */
typedef struct gu_ctrl_port_list_t
{
   gu_ctrl_port_t *     ctrl_port_ptr;
   gu_ctrl_port_list_t *next_ptr;
   gu_ctrl_port_list_t *prev_ptr;
} gu_ctrl_port_list_t;

/*
 * Control port list pointer
 * */
typedef struct gu_ext_ctrl_port_list_t
{
   gu_ext_ctrl_port_t *     ext_ctrl_port_ptr;
   gu_ext_ctrl_port_list_t *next_ptr;
   gu_ext_ctrl_port_list_t *prev_ptr;
} gu_ext_ctrl_port_list_t;

typedef struct gu_module_flags_t
{
   uint8_t is_source : 1;     /**< is source module */
   uint8_t is_sink : 1;       /**< is sink module */
   uint8_t is_siso : 1;       /**< is single-input-single-output module */
   uint8_t is_elementary         : 1;  /** TRUE if this module is an elementary module according to get static properties CAPI_IS_ELEMENTARY */
   uint8_t is_ds_at_sg_or_cntr_boundary : 1;  /** TRUE if this module's any output is at subgraph or container boundary */
   uint8_t is_us_at_sg_or_cntr_boundary : 1;  /** TRUE if this module's any input is at subgraph or container boundary */
} gu_module_flags_t;

/**
 * Module
 */
typedef struct gu_module_t
{
   uint32_t module_id;                  /**< Module ID */
   uint32_t module_instance_id;         /**< Module Instance ID */
   POSAL_HEAP_ID module_heap_id;        /** < heap ID of module */
   void *   amdb_handle;

   uint8_t  module_type; /**< AMDB_MODULE_TYPE_DECODER, AMDB_MODULE_TYPE_ENCODER, AMDB_MODULE_TYPE_PACKETIZER,
                              AMDB_MODULE_TYPE_DEPACKETIZER,
                              AMDB_MODULE_TYPE_CONVERTER, AMDB_MODULE_TYPE_GENERIC, AMDB_MODULE_TYPE_FRAMEWORK,
                              AMDB_MODULE_TYPE_DETECTOR, AMDB_MODULE_TYPE_GENERATOR, AMDB_MODULE_TYPE_PP, AMDB_MODULE_TYPE_END_POINT */
   uint8_t  itype;       /**< interface type of the module: CAPI or STUB */

   uint8_t  max_input_ports;
   uint8_t  max_output_ports;

   uint8_t  min_input_ports;
   uint8_t  min_output_ports;

   uint8_t  num_input_ports;
   uint8_t  num_output_ports;
   uint8_t  num_ctrl_ports;

   gu_status_t gu_status;

   uint8_t path_index;   /**< identifier in case there are multiple parallel paths in the topo. */
   gu_module_flags_t flags;
   gu_input_port_list_t * input_port_list_ptr;  /**< gu_input_port_t */
   gu_output_port_list_t *output_port_list_ptr; /**< gu_output_port_t */

   gu_ctrl_port_list_t *ctrl_port_list_ptr; /**< list of control ports connected to the module. */

   gu_sg_t *   sg_ptr; /**< every module belongs to a subgraph. not port */

   gu_output_port_t *host_output_port_ptr; /** NULL if no attached(elementary) module - points to host module's output port. */
} gu_module_t;


typedef enum gu_sg_mem_resource_type_t
{
  SG_MEM_RESOURCE_MODULE = 0,
  SG_MEM_RESOURCE_PORT = 1,
  SG_MEM_RESOURCE_MAX
} gu_sg_mem_resource_type_t;

/**
 * resource header structure for memories allocated for subgraph.
 * memory fragmentation should be avoided for improved cache performance.
 * a single memory chunk can be allocated for all the modules/ports in a subgraph.
 */
typedef struct gu_sg_mem_resource_t
{
  int8_t* curr_ptr; /**< pointer to the next available memory in this resource. */
  uint32_t available_mem_size; /**< size of the available memory in this resource. */
  gu_sg_mem_resource_type_t type; /**< resource type.*/
} gu_sg_mem_resource_t;

/**
 * subgraph
 */
typedef struct gu_sg_t
{
   uint32_t          id;
   uint8_t           sid;       /**< Scenario ID */
   uint8_t           perf_mode; /**< Performance mode */
   uint8_t           direction;
   gu_status_t       gu_status;
   bool_t            duty_cycling_mode_enabled;    /**< Duty Cycling mode */
   uint16_t          num_modules;
   uint16_t          duty_cycling_clock_scale_factor_q4; /**< Clock scale factor for Duty cycling subgraphs */
   uint16_t          clock_scale_factor_q4;     /**< Clock scale factor for other subgraphs */
   uint16_t          bus_scale_factor_q4;     /**< Bus clock scale factor for other subgraphs */
   uint32_t          kpps_scale_factor;     /**< Kpps scale factor for Voice Satellite subgraphs */
   uint32_t          bw_scale_factor;     /**< BW scale factor for Voice Satellite subgraphs */
   gu_module_list_t *module_list_ptr; /**< list of modules. Each node is a gu_module_t. this is primary list of module
                                         tied to num_modules. */
   spf_list_node_t  *resources_ptr;   /**< list of memory pointers used in this sg. these memories are used for modules and ports. */
} gu_sg_t;

typedef enum gu_sort_status_t
{
   GU_SORT_DEFAULT = 0,
   GU_SORT_UPDATED = 1,
} gu_sort_status_t;

/**
 * graph utility
 */
typedef struct gu_t
{
   uint32_t          log_id;        /**< logging id: see topo_utils.h LOG_ID_CNTR_TYPE_SHIFT etc */
   gu_sort_status_t  sort_status;   /**< indicates if sort was completed or not */
   uint8_t           num_subgraphs; /**< number of subgraphs */
   uint8_t           num_ext_in_ports;
   uint8_t           num_ext_out_ports;
   uint8_t           num_ext_ctrl_ports;
   uint8_t           num_parallel_paths; /**< number of parallel paths */
   gu_sg_list_t      *sg_list_ptr;   /**< list of subgraphs. Each node is a gu_sg_t.*/
   gu_module_list_t  *sorted_module_list_ptr; /**< sorted module list. Sorted in DAG order. Each node is gu_module_t. Modules could
                                                    belong to different subgraphs. sg_ptr->module_list_ptr is the primary list. */
   gu_ext_in_port_list_t *  ext_in_port_list_ptr;   /**< gu_ext_in_port_t */
   gu_ext_out_port_list_t * ext_out_port_list_ptr;  /**< gu_ext_out_port_t */
   gu_ext_ctrl_port_list_t *ext_ctrl_port_list_ptr; /**< List of control ports to the graph.*/
   posal_mutex_t            prof_mutex;             /**< Mutex used to access profiling shared resources */
   uint32_t container_instance_id;                   /**< instance id of container */

   gu_async_graph_t *async_gu_ptr; /**< graph info which is kept hidden from the main gu while data path is running in parallel. This is used in open and close context. Don't use this directly from container and topo layer. */

   int32_t          data_path_thread_id; /**< main thread id in which data-path processing is active */
#ifdef CONTAINER_ASYNC_CMD_HANDLING
   posal_mutex_t  critical_section_lock_; /**< Mutex Lock to protect the critical section to avoid race condition between command processing thread and data processing thread. */
   uint32_t       is_sync_cmd_context_; /**< if greater than zero then it means that the command handling is currently running synchronously with data-path processing. */
#endif
} gu_t;


/* structure which temporarily holds the graph information which is either not yet added into the primary gu (open) or just unlinked from the primary gu (close).
 * */
typedef struct gu_async_graph_t
{
    gu_t                   gu;            /**< During Open: this holds
                                                            1. SG list of new Subgraphs
                                                            2. New external ports
                                               During Close: this holds
                                                            1. SG list of closing subgraphs
                                                            2. list of closing external ports */
    gu_cmn_port_list_t    *port_list_ptr; /**< During Open: list of internal ports which are pending to get connected to the modules */
} gu_async_graph_t;

typedef struct gu_sizes_t
{
   uint16_t ext_ctrl_port_size;
   uint16_t ctrl_port_size;
   uint16_t ext_in_port_size;
   uint16_t ext_out_port_size;
   uint16_t in_port_size;
   uint16_t out_port_size;
   uint16_t sg_size;
   uint16_t module_size;
} gu_sizes_t;

ar_result_t gu_validate_graph_open_cmd(gu_t *gu_ptr, spf_msg_cmd_graph_open_t *open_cmd_ptr);

ar_result_t gu_create_graph(gu_t *                    gu_ptr,
                            spf_msg_cmd_graph_open_t *open_cmd_ptr,
                            gu_sizes_t *              sizes_ptr,
                            POSAL_HEAP_ID             heap_id);

//function to init (link) the external and internal input port
static inline ar_result_t gu_init_ext_in_port(gu_ext_in_port_t *ext_in_port_ptr, gu_input_port_t *int_in_port_ptr)
{
   if (ext_in_port_ptr && int_in_port_ptr)
   {
      ext_in_port_ptr->int_in_port_ptr = int_in_port_ptr;
      int_in_port_ptr->ext_in_port_ptr = ext_in_port_ptr;
      return AR_EOK;
   }
   return AR_EFAILED;
}

//function to init (link) the external and internal output port
static inline ar_result_t gu_init_ext_out_port(gu_ext_out_port_t *ext_out_port_ptr, gu_output_port_t *int_out_port_ptr)
{
   if (ext_out_port_ptr && int_out_port_ptr)
   {
      ext_out_port_ptr->int_out_port_ptr = int_out_port_ptr;
      int_out_port_ptr->ext_out_port_ptr = ext_out_port_ptr;
      return AR_EOK;
   }
   return AR_EFAILED;
}

//function to init (link) the external and internal control port
static inline ar_result_t gu_init_ext_ctrl_port(gu_ext_ctrl_port_t *ext_ctrl_port_ptr,
                                                gu_ctrl_port_t     *int_ctrl_port_ptr)
{
   if (ext_ctrl_port_ptr && int_ctrl_port_ptr)
   {
      ext_ctrl_port_ptr->int_ctrl_port_ptr = int_ctrl_port_ptr;
      int_ctrl_port_ptr->ext_ctrl_port_ptr = ext_ctrl_port_ptr;
      return AR_EOK;
   }
   return AR_EFAILED;
}

//function to deinit (unlink) the external and internal input port
static inline ar_result_t gu_deinit_ext_in_port(gu_ext_in_port_t *ext_in_port_ptr)
{
   if (ext_in_port_ptr && ext_in_port_ptr->int_in_port_ptr)
   {
      ext_in_port_ptr->int_in_port_ptr->ext_in_port_ptr = NULL;
      ext_in_port_ptr->int_in_port_ptr                  = NULL;
      return AR_EOK;
   }
   return AR_EFAILED;
}

//function to deinit (unlink) the external and internal output port
static inline ar_result_t gu_deinit_ext_out_port(gu_ext_out_port_t *ext_out_port_ptr)
{
   if (ext_out_port_ptr && ext_out_port_ptr->int_out_port_ptr)
   {
      ext_out_port_ptr->int_out_port_ptr->ext_out_port_ptr = NULL;
      ext_out_port_ptr->int_out_port_ptr                   = NULL;
      return AR_EOK;
   }
   return AR_EFAILED;
}

//function to deinit (unlink) the external and internal control port
static inline ar_result_t gu_deinit_ext_ctrl_port(gu_ext_ctrl_port_t *ext_ctrl_port_ptr)
{
   if (ext_ctrl_port_ptr && ext_ctrl_port_ptr->int_ctrl_port_ptr)
   {
      ext_ctrl_port_ptr->int_ctrl_port_ptr->ext_ctrl_port_ptr = NULL;
      ext_ctrl_port_ptr->int_ctrl_port_ptr                    = NULL;
      return AR_EOK;
   }
   return AR_EFAILED;
}

void gu_prepare_cleanup_for_graph_open_failure(gu_t *                     gu_ptr,
                                               spf_msg_cmd_graph_open_t * open_cmd_ptr);

ar_result_t gu_respond_to_graph_open(gu_t *gu_ptr, spf_msg_t *cmd_msg_ptr, POSAL_HEAP_ID heap_id);

ar_result_t gu_attach_pending_elementary_modules(gu_t *gu_ptr, POSAL_HEAP_ID heap_id);

ar_result_t gu_get_ext_out_port_for_last_module(gu_t *              gu_ptr,
                                                gu_cmn_port_t *     cmn_port_ptr,
                                                gu_ext_out_port_t **ext_out_port_pptr);

gu_module_t *gu_find_module(gu_t *gu_ptr, uint32_t module_instance_id);

ar_result_t gu_parse_data_link(gu_t *                 gu_ptr,
                               apm_module_conn_cfg_t *data_link_ptr,
                               gu_output_port_t **    src_mod_out_port_ptr,
                               gu_input_port_t **     dst_mod_in_port_ptr);

ar_result_t gu_parse_ctrl_link(gu_t *                      gu_ptr,
                               apm_module_ctrl_link_cfg_t *ctrl_link_ptr,
                               gu_ctrl_port_t **           peer_1_ctrl_port_pptr,
                               gu_ctrl_port_t **           peer_2_ctrl_port_pptr);

gu_ext_ctrl_port_t *gu_get_ext_ctrl_port_for_inter_proc_imcl(gu_t *   gu_ptr,
                                                             uint32_t local_miid,
                                                             uint32_t remote_miid,
                                                             uint32_t ctrl_port_id);

void gu_cleanup_dangling_data_ports(gu_t *gu_ptr);
void gu_cleanup_danling_control_ports(gu_t *gu_ptr);

ar_result_t gu_destroy_graph(gu_t *gu_ptr, bool_t b_destroy_everything);

bool_t gu_is_sg_id_found_in_spf_array(spf_cntr_sub_graph_list_t *spf_sg_array_ptr, uint32_t sg_id);
bool_t gu_is_port_handle_found_in_spf_array(uint32_t       num_handles,
                                            spf_handle_t **port_handles_array_pptr,
                                            spf_handle_t * given_handle);

gu_cmn_port_t *gu_find_cmn_port_by_id(gu_module_t *module_ptr, uint32_t id);
// gu_find_input_port_by_id
gu_input_port_t *gu_find_input_port(gu_module_t *module_ptr, uint32_t id);
// gu_find_output_port by id
gu_output_port_t *gu_find_output_port(gu_module_t *module_ptr, uint32_t id);
gu_output_port_t *gu_find_output_port_by_index(gu_module_t *module_ptr, uint32_t index);
gu_input_port_t *gu_find_input_port_by_index(gu_module_t *module_ptr, uint32_t index);
gu_cmn_port_t *gu_find_port_by_index(spf_list_node_t *list_ptr, uint32_t index);

gu_ctrl_port_t *gu_find_ctrl_port_by_id(gu_module_t *module_ptr, uint32_t id);

gu_sg_t *gu_find_subgraph(gu_t *gu_ptr, uint32_t id);

void gu_update_parallel_paths(gu_t *gu_ptr);

ar_result_t gu_update_sorted_list(gu_t *gu_ptr, POSAL_HEAP_ID heap_id);
ar_result_t gu_prepare_for_module_loading(gu_t *            gu_ptr,
                                          spf_list_node_t **amdb_h_list_pptr,
                                          gu_module_t *     module_ptr,
                                          bool_t            is_placeholder_replaced,
                                          uint32_t          real_id,
                                          POSAL_HEAP_ID     heap_id);
ar_result_t gu_handle_module_loading(spf_list_node_t **amdb_h_list_pptr, uint32_t log_id);
ar_result_t gu_reset_graph_port_markers(gu_t *gu_ptr);
void gu_print_graph(gu_t *gu_ptr);

ar_result_t gu_prepare_async_create(gu_t *gu_ptr, POSAL_HEAP_ID heap_id);
ar_result_t gu_finish_async_create(gu_t *gu_ptr, POSAL_HEAP_ID heap_id);

ar_result_t gu_prepare_async_destroy(gu_t *gu_ptr, POSAL_HEAP_ID heap_id);
ar_result_t gu_finish_async_destroy(gu_t *gu_ptr);

ar_result_t gu_detach_module(gu_t *gu_ptr, gu_module_t *attached_module_ptr, POSAL_HEAP_ID heap_id);

/**
 * returns NULL if cmn port doesn't correspond to an input port that's not connected to ext input
 * or if cmn port corresponds to an output port
 */
gu_ext_in_port_t *gu_get_ext_in_port_from_cmn_port(gu_cmn_port_t *cmn_port_ptr);

gu_ext_out_port_t *gu_get_ext_out_port_from_cmn_port(gu_cmn_port_t *cmn_port_ptr);

/**
 * if return value is TRUE, then given port-id is that of output
 * if not it's input.
 */
static inline bool_t gu_is_output_port_id(uint32_t port_id)
{
   bool_t is_output = (AR_PORT_DIR_TYPE_OUTPUT == spf_get_bits(port_id, AR_PORT_DIR_TYPE_MASK, AR_PORT_DIR_TYPE_SHIFT));
   return is_output;
}

/**
 * if return value is TRUE, then given domain-id is remote
 */
static inline bool_t gu_is_domain_id_remote(uint32_t domain_id)
{
   uint32_t host_domain_id;
   __gpr_cmd_get_host_domain_id(&host_domain_id);
   bool_t is_remote = (host_domain_id != domain_id) ? TRUE : FALSE;
   return is_remote;
}

static inline POSAL_HEAP_ID gu_get_heap_id_from_heap_prop(uint32_t heap_prop)
{
   POSAL_HEAP_ID heap_id = (APM_CONT_HEAP_LOW_POWER == heap_prop) ? posal_get_island_heap_id() : POSAL_HEAP_DEFAULT;
   return heap_id;
}

/* THis function returns the downgraded heap id. Heap ID is downgraded to use Low power if either
   self or peer heap is Low power, else it will fallback to default.
   Use downgraded heap ID when allocating resources for the external ports.*/
static inline POSAL_HEAP_ID gu_get_downgraded_heap_id(POSAL_HEAP_ID self_heap_id, POSAL_HEAP_ID peer_heap_id)
{
   // if self is default heap, return peer heap id. If self is low power, return low power heap id.
   return POSAL_IS_ISLAND_HEAP_ID(self_heap_id) ? self_heap_id : peer_heap_id;
}

/* Private function to acquire GU lock.
 * Don't use directly, Use SPF_CRITICAL_SECTION_START instead.
 * */
static inline void gu_critical_section_start_(gu_t *gu_ptr)
{
#ifdef CONTAINER_ASYNC_CMD_HANDLING
   if (gu_ptr->critical_section_lock_)
   {
      posal_mutex_lock(gu_ptr->critical_section_lock_);

      if (posal_thread_get_curr_tid() != gu_ptr->data_path_thread_id)
      {
         gu_ptr->is_sync_cmd_context_++;
      }
   }
#endif
   return;
}

/* Private function to release GU lock.
 * Don't use directly, Use SPF_CRITICAL_SECTION_END instead.
 * */
static inline void gu_critical_section_end_(gu_t *gu_ptr)
{
#ifdef CONTAINER_ASYNC_CMD_HANDLING
   if (gu_ptr->critical_section_lock_)
   {
      posal_mutex_unlock(gu_ptr->critical_section_lock_);

      if (posal_thread_get_curr_tid() != gu_ptr->data_path_thread_id && gu_ptr->is_sync_cmd_context_ > 0)
      {
            gu_ptr->is_sync_cmd_context_--;
      }
   }
#endif
   return;
}


/*
 * If async open/close handling is active then this returns async_gu_ptr.
 * If async open/close handling is not active then this returns the main gu_ptr.
 * */
static inline gu_t* get_gu_ptr_for_current_command_context(gu_t* gu_ptr)
{
	return (gu_ptr->async_gu_ptr)? &gu_ptr->async_gu_ptr->gu: gu_ptr;
}

#ifdef __cplusplus
}
#endif //__cplusplus

// clang-format on
#endif // #ifndef GRAPH_UTILS_H_
