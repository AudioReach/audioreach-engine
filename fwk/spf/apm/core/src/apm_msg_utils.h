#ifndef _APM_MSG_UTILS_H_
#define _APM_MSG_UTILS_H_

/**
 * \file apm_msg_utils.h
 *
 * \brief
 *     This file contains APM messaging utility functions.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "apm_i.h"
#include "apm_graph_db.h"
#include "spf_cmn_if.h"
#include "apm_cntr_if.h"
#include "apm_proxy_if.h"
#include "apm_cntr_path_delay_if.h"
#include "spf_msg_util.h"
#include "gpr_api_inline.h"
#include "posal_intrinsics.h"
#include "apm_cmd_sequencer.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/****************************************************************************
 * Macro Definition                                                         *
 ****************************************************************************/

/* clang-format off */
#define APM_CLEAR_PENDING_CONT_LIST       (TRUE)
#define APM_RETAIN_PENDING_CONT_LIST      (FALSE)

#define CMD_RSP_PENDING                   (TRUE)
#define CMD_RSP_DONE                      (FALSE)

#define APM_CONT_CACHED_CFG_RELEASE       (TRUE)
#define APM_CONT_CACHED_CFG_RETAIN        (FALSE)

#define APM_CONT_CMD_PARAMS_RELEASE       (TRUE)
#define APM_CONT_CMD_PARAMS_RETAIN        (FALSE)

#define APM_SG_ID_DONT_CARE               (0)
#define APM_SG_ID_INVALID                 (0)

#define APM_NUM_MAX_CONT_MSG              (3)

#define APM_CMD_LIST_FULL_MASK            ((1UL << APM_NUM_MAX_PARALLEL_CMD) - 1)

#define SIZE_OF_PTR()                     (sizeof(void *))

typedef enum apm_cont_list_type_t
{
   APM_PENDING_CONT_LIST = 0,
   APM_CACHED_CONT_LIST = 1,

}apm_cont_list_type_t;

typedef enum apm_cached_cont_list_t
{
   APM_CONT_LIST_WITH_CYCLIC_LINK = APM_OBJ_TYPE_CYCLIC,
   APM_CONT_LIST_WITH_ACYCLIC_LINK = APM_OBJ_TYPE_ACYCLIC,
   APM_CONT_LIST_END = APM_CONT_LIST_WITH_ACYCLIC_LINK,
   APM_CONT_LIST_MAX

}apm_cached_cont_list_t;

/****************************************************************************
 * Structure Definitions                                                    *
 ****************************************************************************/

typedef struct apm_proxy_msg_opcode_t apm_proxy_msg_opcode_t;

struct apm_proxy_msg_opcode_t
{
   uint32_t num_proxy_msg_opcode;
   /**< Number of container message opcode */

   uint32_t proxy_opcode_list[APM_NUM_MAX_CONT_MSG];
   /**< List of container message opcodes */

   uint32_t proxy_opcode_idx;
   /**< Current opcode index being processed */
 };


/** APM command and response control structure. This structure
 *  MUST NOT declare any variable which remains static across
 *  more than 1 iteration of the multi-step command
 *  processing. Once all the response have been receieved for
 *  particular command step, this structure is cleared */

typedef struct apm_cmd_rsp_ctrl_t apm_cmd_rsp_ctrl_t;

struct apm_cmd_rsp_ctrl_t
{
   bool_t            cmd_rsp_pending;
   /**< Overall command response in pending status */

   ar_result_t       rsp_status;
   /**< Overall response status */

   uint32_t          num_cmd_issued;
   /**< Number of commands issues to containers */

   uint32_t          num_rsp_rcvd;
   /**< Number of responses received from containers */

   uint32_t          num_rsp_failed;
   /**< Number of failed responses */

   uint32_t          num_pending_container;
   /**< Number of containers pending to
        be sent message by APM */

   spf_list_node_t    *pending_cont_list_ptr;
   /**< List of all the containers to which the message
        is pending to be sent */

   spf_list_node_t    *failed_cont_list_ptr;
   /**< List of all containers which returned failure
        code for an APM command */

   bool_t            proxy_resp_pending;
   /**< Proxy Manager response pending status. */

   uint32_t          num_proxy_cmd_issued;
   /** Number of cmds issued to Proxy Mangers.. */

   uint32_t          num_proxy_resp_rcvd;
   /** NUmber of responses received from Proxy Managers. */

   uint32_t          num_proxy_rsp_failed;
   /** Number of Proxy Manager responses failed. */

   uint32_t          num_inactive_proxy_mgrs;
   /**< Number of Proxy Managers pending to be sent message by APM but not currently active */

   spf_list_node_t    *inactive_proxy_mgr_list_ptr;
   /**< List of all the Proxy Managers to which the message
        is pending to be sent but not currently active */

   uint32_t          num_active_proxy_mgrs;
   /**< Number of Proxy Managers ready to be sent message by APM */

   spf_list_node_t    *active_proxy_mgr_list_ptr;
   /**< List of all the Proxy Managers to which the message
        is ready to be sent */

   bool_t             permitted_proxy_sgs_pending;
   /** Flag to indicate that there are Proxy subgraphs
       approved by Proxy Managers and waiting to be processed*/

   uint32_t           num_permitted_proxy_mgrs;
   /** Number of Proxy managers that sent permission
       for graph mgmt command processing. */

   spf_list_node_t*    permitted_proxy_mgr_list_ptr;
   /** List of Proxy Managers that sent permission
       for graph mgmt command processing.*/
};

typedef struct apm_cont_msg_opcode_t apm_cont_msg_opcode_t;

struct apm_cont_msg_opcode_t
{
   uint32_t num_msg_opcode;
   /**< Number of container message opcode */

   uint32_t opcode_list[APM_NUM_MAX_CONT_MSG];
   /**< List of container message opcodes */

   uint32_t curr_opcode_idx;
   /**< Current opcode index being processed */
};

typedef struct apm_gm_cmd_sub_graph_list_t apm_gm_cmd_sub_graph_list_t;

struct apm_gm_cmd_sub_graph_list_t
{
   uint32_t              num_cmd_sg_id;
   /**< Number of sub-graph ID's received as part of
        graph management command*/

   apm_sub_graph_id_t   *cmd_sg_id_list_ptr;
   /**< List of sub-graph ID's received as part of
        graph management command*/

   uint32_t              num_reg_sub_graphs;
   /**< Number of regular (non-proxy) sub-graphs
        being processed */

   spf_list_node_t       *reg_sg_list_ptr;
   /**< Pointer to list of regular (non-proxy) sub-graph
        objects being processed
        Node Type: apm_sub_graph_t */

   uint32_t              num_proxy_sub_graphs;
   /**< Number of sub-graphs which have proxy managers.*/

   apm_sub_graph_id_t    *proxy_sg_id_list_ptr;
   /**< Pointer to the list of sub graph IDs
        that have proxy managers.*/

   bool_t                proxy_sub_graph_pending;
   /**< Flag to indicate if proxy sub-graphs pending
        */

   apm_sub_graph_state_t proxy_sg_list_state;
     /**< Current state of list of sub-graph ID's */

   uint32_t              num_cont_port_hdl_sg;
   /**< Number of sub-graphs being processed as
        part of data / control link info received */

   spf_list_node_t        *cont_port_hdl_sg_list_ptr;
   /**< Pointer to the list of sub-graphs being processed
        as part of data / control link info received
        Obj Type: apm_sub_graph_t */
};

typedef enum apm_cont_list_trav_state_t
{
   APM_CONT_LIST_TRAV_STOPPED = 0,
   /**< List traversal is stopped/not started */

   APM_CONT_LIST_TRAV_STARTED = 1,
   /**< List traversal is stopped/not started */

   APM_CONT_LIST_TRAV_DONE = 2
   /**< List traversal is done */

}apm_cont_list_trav_state_t;

typedef struct apm_gm_cmd_cont_proc_info_t apm_gm_cmd_cont_proc_info_t;

struct apm_gm_cmd_cont_proc_info_t
{
   spf_list_node_t            *curr_cont_graph_ptr;
   /**< Current container graph node under
        cmd process */

   spf_list_node_t             *curr_cont_node_ptr;
   /**< Current container node under cmd process */

   spf_list_node_t             *next_cont_node_ptr;
   /**< Next container node under cmd process */

   apm_list_t                   cached_cont_list[APM_CONT_LIST_MAX];
   /**< List of all the containers which are
        done processing graph mgmt command at
       least once in case of multi-step command */

   apm_cached_cont_list_t       curr_cont_list_type;
   /**< Current cached container list under process */

   apm_cont_list_trav_state_t   cont_list_trav_state[APM_CONT_LIST_MAX];
   /**< Container list traversal state */

};

typedef struct apm_gm_cmd_sg_proc_info_t apm_gm_cmd_sg_proc_info_t;

struct apm_gm_cmd_sg_proc_info_t
{
   uint32_t                      num_processed_sg;
   /**< Number of sub-graphs processed for
        one iteration of GM command */

   spf_list_node_t                *processed_sg_list_ptr;
   /**< Pointer to list of sub-graphs processed
        for one iteration of GM command  */

   apm_sub_graph_state_t         curr_sg_list_state;
   /**< Current state of list of sub-graph ID's */

   bool_t                        sg_list_mixed_state;
   /**< Flag to indicate if all the sub-graphs
        part of the current GM command has same
        or difference states */
};


typedef struct apm_graph_mgmt_cmd_ctrl_t apm_graph_mgmt_cmd_ctrl_t;

struct apm_graph_mgmt_cmd_ctrl_t
{
   apm_gm_cmd_sub_graph_list_t  sg_list;
   /**< List of sub-graphs being processed
        under different context, e.g. regular
        proxy, via data/control link close */

   apm_gm_cmd_cont_proc_info_t  cont_proc_info;
   /**< Container info corresponding to list of
        sub-graphs being processed */

   apm_gm_cmd_sg_proc_info_t    sg_proc_info;
   /**< Processing info for the list of
        sub-graphs*/
};

typedef enum apm_graph_open_cmd_status_t
{
   APM_OPEN_CMD_STATE_NORMAL = 0,
   /**< Normal status, no errors */

   APM_OPEN_CMD_STATE_CONT_CREATE_FAIL,
   /**< Container creation failed */

   APM_OPEN_CMD_STATE_OPEN_FAIL,
   /**< Failure during graph open, complete or
         partial */

   APM_OPEN_CMD_STATE_CONNECT_FAIL
   /**< Failure during container port
        connections, complete or partial */

}apm_graph_open_cmd_status_t;

typedef struct apm_graph_open_cmd_ctrl_t apm_graph_open_cmd_ctrl_t;

struct apm_graph_open_cmd_ctrl_t
{
   uint32_t                     num_sub_graphs;
   /**< Number of sub-graphs being processed */

   spf_list_node_t              *sg_id_list_ptr;
   /**< Pointer to list of sub-graph ID's being
        processed */

   uint32_t                     num_skipped_sg;
   /**< Number of sub-graphs being processed */

   spf_list_node_t              *skip_sg_id_list_ptr;
   /**< Pointer to list of sub-graph ID's being
        skipped */

   uint8_t                      *sat_prv_cfg_ptr;
   /**< Start of the Client mod param payload (apm_param_id_imcl_peer_domain_info_t)
        - Memory is from the client*/

   apm_list_t                   data_ctrl_link_list[LINK_TYPE_MAX];
   /**< List of control and data links being opened across
        started sub-graphs */

   apm_graph_open_cmd_status_t  cmd_status;
   /** Current status of graph open command */
};

typedef struct apm_get_cfg_cmd_ctrl_t apm_get_cfg_cmd_ctrl_t;

struct apm_get_cfg_cmd_ctrl_t
{
    uint32_t          num_pid_payloads;
   /**< Number of PID payloads in the overall
        GET CFG payload addressed to APM module
        instance ID */

   spf_list_node_t    *pid_payload_list_ptr;
   /**< List of PID payloads in the overall
        GET CFG payload addressed to APM module
        instance ID
        Obj Type: apm_module_param_data_t */
};

typedef struct apm_db_default_set_cfg_cmd_ctrl_t apm_db_default_set_cfg_cmd_ctrl_t;

struct apm_db_default_set_cfg_cmd_ctrl_t
{
    uint32_t          num_subgraph_node;
   /**< Number of subgraph in parsed subgraph list*/

   spf_list_node_t    *parsed_sg_list_ptr;
   /**<Pointer to parsed subgraph list */
};

typedef struct apm_debug_info_cfg_t
{
    bool_t is_port_media_fmt_enable;
   /**< Flag to indicate if port media info enable has to sent to satellite cntr  as
        part of SET_CFG cmd*/

    bool_t is_sattelite_debug_info_send_pending;
    /**< Flag to indicate if port media info enable has to sent to satellite cntr  as
        part of SET_CFG cmd*/
}apm_debug_info_cfg_t;

typedef struct apm_set_cfg_cmd_ctrl_t apm_set_cfg_cmd_ctrl_t;

struct apm_set_cfg_cmd_ctrl_t
{
    bool_t is_close_all_needed;
   /**< Flag to indicate if close_all handling required as
        part of SET_CFG cmd*/

    apm_debug_info_cfg_t debug_info;
    /**< struct contains debug_info information*/

};

struct apm_deferred_cmd_list_t
{
   bool_t          close_all_deferred;
   /**< Flag to track if a close all command
        is deferred   */

    apm_list_t     deferred_cmd_list;
   /**< Commands for which the processing
        is deferred due to conflict with
        List node ptr type: apm_cmd_ctrl_t */
};

struct apm_cmd_ctrl_t
{
   uint32_t                      list_idx;
   /**< Index of this cmd obj in the APM command list */

   uint32_t                      cmd_opcode;
   /**< Command opcode under process */

   spf_msg_t                      cmd_msg;
   /**< Command payload GK msg */

   void                          *cmd_payload_ptr;
   /**< Command payload pointer. For OOB command,
        this will be the virtual address corresponding
        to cmd physical address */

   uint32_t                       cmd_payload_size;
   /**< Command payload size */

   ar_result_t                   cmd_status;
   /**< Current status of command. This flag keep tracks
        of any intermediate failure during multi step
        command handling */

   bool_t                        cmd_pending;
   /**< Overall command in pending status */

   ar_result_t                   agg_rsp_status;
   /**< Aggregate response status of one step of a command */

   apm_cmd_seq_info_t            cmd_seq;
   /**< Command sequencer object */

   spf_msg_t                      cmd_rsp_payload;
   /**< Command response payload, if required
        for a given command opcode, e.g. in-band GET_CFG */

   apm_cont_msg_opcode_t         cont_msg_opcode;
   /**< List of container message opcode to be
        processed as part of this command */

   apm_proxy_msg_opcode_t        proxy_msg_opcode;
   /**< Proxy message opcode to be
        processed as part of this command */

   union
   {
      struct
      {
         apm_graph_open_cmd_ctrl_t  graph_open_cmd_ctrl;
         /**< Graph open command control */

         apm_graph_mgmt_cmd_ctrl_t  graph_mgmt_cmd_ctrl;
         /**< Graph management command control */

         apm_db_default_set_cfg_cmd_ctrl_t    db_default_set_cfg_cmd_ctrl;
            /**<Db_Default_Set config command control */
      };

      apm_set_cfg_cmd_ctrl_t     set_cfg_cmd_ctrl;
      /**< Set Config command control */

      apm_get_cfg_cmd_ctrl_t     get_cfg_cmd_ctrl;
      /**< Get Config command control */

   };

   apm_cmd_rsp_ctrl_t            rsp_ctrl;
   /**< Response Control for this command */

   uint64_t                      cmd_start_ts_us;
   /**< Command execution start timestamp
        in microseconds */
};

/* clang-format on */

/****************************************************************************
 *  Function Declarations
 ****************************************************************************/

ar_result_t apm_send_msg_to_containers(spf_handle_t *apm_handle_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr);

ar_result_t apm_release_cont_msg_cached_cfg(apm_cont_cached_cfg_t *cached_cfg_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr);

ar_result_t apm_cont_release_cmd_params(apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr);

// ar_result_t apm_set_up_cont_msg_proc(spf_handle_t *     apm_handle_ptr,
//                                     apm_cmd_ctrl_t *  apm_cmd_ctrl_ptr,
//                                     apm_graph_info_t *graph_info_ptr);

ar_result_t apm_graph_mgmt_cmd_get_next_cont(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr, apm_container_t **container_node_pptr);

ar_result_t apm_cache_cont_sg_and_port_list(apm_graph_info_t *graph_info_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr);

ar_result_t apm_send_connect_msg_to_container(spf_handle_t *       apm_handle_ptr,
                                              apm_cmd_ctrl_t *     apm_cmd_ctrl_ptr,
                                              apm_container_t *    container_node_ptr,
                                              apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr);

ar_result_t apm_get_cont_cmd_ctrl_obj(apm_container_t *     container_node_ptr,
                                      apm_cmd_ctrl_t *      apm_cmd_ctrl_ptr,
                                      apm_cont_cmd_ctrl_t **cont_cmd_ctrl_pptr);

ar_result_t apm_release_cont_cmd_ctrl_obj(apm_container_t *container_node_ptr, apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr);

ar_result_t apm_get_allocated_cont_cmd_ctrl_obj(apm_container_t *     container_node_ptr,
                                                apm_cmd_ctrl_t *      apm_cmd_ctrl_ptr,
                                                apm_cont_cmd_ctrl_t **cont_cmd_ctrl_pptr);

ar_result_t apm_add_cont_to_pending_msg_send_list(apm_cmd_ctrl_t *     apm_cmd_ctrl_ptr,
                                                  apm_container_t *    container_node_ptr,
                                                  apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr);

ar_result_t apm_clear_container_list(apm_cmd_ctrl_t *     apm_cmd_ctrl_ptr,
                                     spf_list_node_t **   cached_cont_list_pptr,
                                     uint32_t *           num_container_ptr,
                                     bool_t               clear_cont_cached_cfg,
                                     bool_t               clear_cont_cmd_params,
                                     apm_cont_list_type_t list_type);

ar_result_t apm_graph_mgmt_aggregate_cont_port_all(apm_cmd_ctrl_t *  apm_cmd_ctrl_ptr,
                                                   apm_container_t * container_node_ptr,
                                                   apm_graph_info_t *graph_info_ptr,
                                                   uint32_t          gm_cmd_curr_sg_id);

ar_result_t apm_set_gm_cmd_cont_proc_info(apm_graph_info_t *graph_info_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr);

ar_result_t apm_gm_cmd_populate_cont_list_to_process(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr);

ar_result_t apm_gm_cmd_prepare_for_next_cont_msg(apm_graph_info_t *graph_info_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr);

ar_result_t apm_set_get_cfg_msg_rsp_clear_cont_list(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr);

ar_result_t apm_gm_cmd_restart_cont_msg_proc(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr, apm_graph_info_t *graph_info_ptr);

ar_result_t apm_clear_set_get_cfg_cmd_info(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr);

ar_result_t apm_cont_msg_one_iter_completed(apm_cmd_ctrl_t *cmd_ctrl_ptr);

ar_result_t apm_gm_cmd_pre_processing(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr, apm_graph_info_t *graph_info_ptr);

ar_result_t apm_gm_cmd_reset_sg_cont_list_proc_info(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr);

ar_result_t apm_connect_peer_containers_ext_ports(apm_t *apm_info_ptr);

ar_result_t apm_gm_cmd_update_cont_cached_port_hdl_state(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr, uint32_t msg_opcode);

ar_result_t apm_clear_cont_cmd_rsp_ctrl(apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr, bool_t release_rsp_msg_buf);

ar_result_t apm_graph_open_handle_link_start(apm_t *apm_info_ptr);

ar_result_t apm_release_cont_cached_graph_open_cfg(apm_cont_cached_cfg_t *cached_cfg_ptr);

ar_result_t apm_release_cont_cached_graph_mgmt_cfg(apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr,
                                                   apm_cont_cached_cfg_t *cached_cfg_ptr);

ar_result_t apm_search_and_cache_cont_port_hdl(apm_container_t *      container_obj_ptr,
                                               apm_cont_cmd_ctrl_t *  cont_cmd_ctrl_ptr,
                                               uint32_t               module_iid,
                                               uint32_t               port_id,
                                               spf_module_port_type_t port_type,
                                               uint32_t               cmd_opcode);

/****************************************************************************
 *  Inline Function Definitions                                             *
 ****************************************************************************/

/**
  Utility to set a particular bit in an integer
*/
static inline void APM_SET_BIT(uint32_t *value, uint32_t bit_pos)
{
   *value = *value | (1UL << bit_pos);
}

/**
  Utility to clear a particular bit in an integer
*/
static inline void APM_CLR_BIT(uint32_t *value, uint32_t bit_pos)
{
   *value = *value & ~(1UL << bit_pos);
}

static inline void apm_clear_cmd_rsp_ctrl(apm_cmd_rsp_ctrl_t *apm_rsp_ctrl_ptr)
{
   /** Init the response control params */
   apm_rsp_ctrl_ptr->cmd_rsp_pending = CMD_RSP_DONE;
   apm_rsp_ctrl_ptr->rsp_status      = AR_EOK;
   apm_rsp_ctrl_ptr->num_cmd_issued  = 0;
   apm_rsp_ctrl_ptr->num_rsp_rcvd    = 0;
   apm_rsp_ctrl_ptr->num_rsp_failed  = 0;

   return;
}

static inline void apm_set_cont_cmd_rsp_pending(apm_cont_cmd_rsp_ctrl_t *cmd_rsp_ctrl_ptr, uint32_t msg_opcode)
{
   /** Set the send message pending flag. This flag does not
    *  serve any functional purpose. Helpful for debugging to
    *  check if response is pending from a container for a given
    *  APM commmand */
   cmd_rsp_ctrl_ptr->rsp_pending = TRUE;
}

static inline uint32_t apm_get_curr_cont_msg_opcode(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   return (apm_cmd_ctrl_ptr->cont_msg_opcode.opcode_list[apm_cmd_ctrl_ptr->cont_msg_opcode.curr_opcode_idx]);
}

static inline bool_t apm_check_cont_msg_seq_done(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   return (apm_cmd_ctrl_ptr->cont_msg_opcode.curr_opcode_idx == apm_cmd_ctrl_ptr->cont_msg_opcode.num_msg_opcode);
}

static inline bool_t apm_graph_mgmt_cmd_iter_is_broadcast(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   uint32_t cont_msg_opcode =
      apm_cmd_ctrl_ptr->cont_msg_opcode.opcode_list[apm_cmd_ctrl_ptr->cont_msg_opcode.curr_opcode_idx];

   if ((SPF_MSG_CMD_GRAPH_DISCONNECT == cont_msg_opcode) || (SPF_MSG_CMD_GRAPH_CLOSE == cont_msg_opcode))
   {
      return TRUE;
   }
   else
   {
      return FALSE;
   }
}

/* This function returns true when grph mgmt seq
 * is used in overall cmd opcode handling */
static inline bool_t apm_is_graph_mgmt_cmd_opcode(uint32_t cmd_opcode)
{
   switch (cmd_opcode)
   {
      case APM_CMD_GRAPH_OPEN:
      case APM_CMD_GRAPH_PREPARE:
      case APM_CMD_GRAPH_START:
      case APM_CMD_GRAPH_STOP:
      case APM_CMD_GRAPH_FLUSH:
      case APM_CMD_GRAPH_CLOSE:
      case APM_CMD_CLOSE_ALL:
      case APM_CMD_GRAPH_SUSPEND:
      case SPF_MSG_CMD_PROXY_GRAPH_START:
      case SPF_MSG_CMD_PROXY_GRAPH_STOP:
      case SPF_MSG_CMD_PROXY_GRAPH_PREPARE:
      case SPF_MSG_CMD_SET_CFG: // Close all can be called during this, which uses graph mgnt cmd
      {
         return TRUE;
      }
      default:
      {
         return FALSE;
      }
   }
}

static inline bool_t apm_is_proxy_graph_mgmt_cmd_opcode(uint32_t cmd_opcode)
{
   switch (cmd_opcode)
   {
      case SPF_MSG_CMD_PROXY_GRAPH_START:
      case SPF_MSG_CMD_PROXY_GRAPH_STOP:
      case SPF_MSG_CMD_PROXY_GRAPH_PREPARE:
      {
         return TRUE;
      }
      default:
      {
         return FALSE;
      }
   }
}

static inline bool_t apm_is_sg_state_changing_msg_opcode(uint32_t msg_opcode)
{
   switch (msg_opcode)
   {
      case SPF_MSG_CMD_GRAPH_CONNECT:
      case SPF_MSG_CMD_GRAPH_PREPARE:
      case SPF_MSG_CMD_GRAPH_START:
      case SPF_MSG_CMD_GRAPH_STOP:
      case SPF_MSG_CMD_GRAPH_SUSPEND:
      {
         return TRUE;
      }
      default:
      {
         return FALSE;
      }
   }
}

static inline bool_t apm_cont_has_port_hdl(apm_container_t *container_node_ptr, apm_cached_cont_list_t cont_list_type)
{
   if (container_node_ptr->cont_ports_per_sg_pair[cont_list_type][PORT_TYPE_DATA_IP].list_ptr ||
       container_node_ptr->cont_ports_per_sg_pair[cont_list_type][PORT_TYPE_DATA_OP].list_ptr ||
       container_node_ptr->cont_ports_per_sg_pair[cont_list_type][PORT_TYPE_CTRL_IO].list_ptr)
   {
      return TRUE;
   }
   else
   {
      return FALSE;
   }
}

static inline bool_t apm_is_cont_first_msg_opcode(apm_cmd_ctrl_t *cmd_ctrl_ptr)
{
   return (!cmd_ctrl_ptr->cont_msg_opcode.curr_opcode_idx);
}

static inline bool_t apm_is_cont_last_msg_opcode(apm_cmd_ctrl_t *cmd_ctrl_ptr)
{
   return ((cmd_ctrl_ptr->cont_msg_opcode.curr_opcode_idx + 1) == cmd_ctrl_ptr->cont_msg_opcode.num_msg_opcode);
}

static inline bool_t apm_gm_cmd_cache_sg_id(apm_cmd_ctrl_t *cmd_ctrl_ptr)
{
   bool_t cache_sg = TRUE;

   /** For the prepare/start command handling, suspended peer state
    *  may need to be propagated to newly opened sub-graphs. So
    *  suspend handling is inserted in prepare-start sequence.
    *  However, suspend message must not be sent to the newly
    *  opened sub-graphs because its own state is not changing to
    *  suspend. For the same reason Also in case of peer handles,
    *  suspend should not be sent to the containers where graph
    *  management command sub-graph ID is present as peer. */
   if (((APM_CMD_GRAPH_PREPARE == cmd_ctrl_ptr->cmd_opcode) ||
        (SPF_MSG_CMD_PROXY_GRAPH_PREPARE == cmd_ctrl_ptr->cmd_opcode) ||
        (APM_CMD_GRAPH_START == cmd_ctrl_ptr->cmd_opcode) ||
        (SPF_MSG_CMD_PROXY_GRAPH_START == cmd_ctrl_ptr->cmd_opcode)) &&
       (SPF_MSG_CMD_GRAPH_SUSPEND == apm_get_curr_cont_msg_opcode(cmd_ctrl_ptr)))
   {
      cache_sg = FALSE;

      /** For prepare / start command, Suspend is only done for peer
       *  state propagation. Once the suspend message is done,
       *  container sub-graph and port handle need to be recached
       *  for pepare/start handling. */
      cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.sg_list_mixed_state = TRUE;
   }

   return cache_sg;
}

static inline bool_t apm_check_if_multiple_cmds_executing(uint32_t active_cmd_mask)
{
   return ((active_cmd_mask > 1) && (active_cmd_mask & (active_cmd_mask - 1)));
}

static inline apm_cached_cont_list_t apm_get_curr_active_cont_list_type(apm_cmd_ctrl_t *cmd_ctrl_ptr)
{
   return cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_list_type;
}

/****************************************************************************
 * Static Inline Function Declarations
 ****************************************************************************/

static inline void apm_init_next_cmd_op_seq_idx(apm_op_seq_t *op_seq_ptr, apm_cmd_seq_idx_t next_seq_idx)
{
   if (APM_CMD_SEQ_IDX_INVALID == op_seq_ptr->curr_seq_idx)
   {
      op_seq_ptr->curr_seq_idx = next_seq_idx;

      /** Set the current command op pending status   */
      op_seq_ptr->curr_cmd_op_pending = TRUE;
   }
}

static inline void apm_incr_cmd_op_idx(apm_op_seq_t *op_seq_ptr)
{
   /** If all the command operations are not finished, and
    *  current successful operation does not result in sending any
    *  message to containers and/or proxy managers, then increment
    *  the op index and reset the op sequencer index for next
    *  operation */
   if ((APM_CMN_CMD_OP_COMPLETED != op_seq_ptr->op_idx) && (!op_seq_ptr->curr_cmd_op_pending))
   {
      /** Increment the operation index */
      op_seq_ptr->op_idx++;

      /** Reset the sequencer index for next operation */
      op_seq_ptr->curr_seq_idx = APM_CMD_SEQ_IDX_INVALID;
   }
}

static inline bool_t apm_is_open_cmd_err_seq(apm_cmd_seq_info_t *cmd_seq_ptr)
{
   return (APM_OPEN_CMD_OP_ERR_HDLR == cmd_seq_ptr->graph_open_seq.op_idx);
}

#ifdef __cplusplus
}
#endif /*__cplusplus*/
#endif /* _APM_MSG_UTILS_H_ */
