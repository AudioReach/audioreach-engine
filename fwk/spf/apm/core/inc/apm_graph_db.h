#ifndef _APM_GRAPH_DB_H__
#define _APM_GRAPH_DB_H__

/**
 * \file apm_graph_db.h
 * \brief
 *     This file structure definition and function declaration for APM Graph
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_list_utils.h"
#include "apm_api.h"
#include "ar_ids.h"
#include "apm_cntr_if.h"
#include "apm_cntr_path_delay_if.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/
/*------------------------------------------------------------------------------
 *  Enumerations
 *----------------------------------------------------------------------------*/

/* clang-format off */

typedef uint32_t apm_sub_graph_state_t;

typedef enum apm_graph_obj_type_t
{
   APM_OBJ_TYPE_MODULE = 0,
   /** Graph object type Module */

   APM_OBJ_TYPE_CONTAINER = 1,
   /** Graph object type Container */

   APM_OBJ_TYPE_MAX = APM_OBJ_TYPE_CONTAINER

}apm_graph_obj_type_t;

/** This token is used to identify the context of a response message recceived by APM. */
typedef enum apm_cmd_token_t
{
   APM_CMD_TOKEN_INVALID = 0,

   APM_CMD_TOKEN_CONTAINER_CTRL_TYPE,

   APM_CMD_TOKEN_PROXY_CTRL_TYPE

} apm_cmd_token_t;

/** This is used for the query type. */
typedef enum apm_db_query_t
{
   APM_DB_OBJ_QUERY = FALSE,

   APM_DB_OBJ_CREATE_REQD = TRUE,

   APM_DB_OBJ_NOTEXIST_OK = TRUE

}apm_db_query_t;

enum
{
   APM_OBJ_TYPE_CYCLIC  = 0,
   APM_OBJ_TYPE_ACYCLIC = 1,
   APM_OBJ_CYCLE_TYPE_MAX
};

typedef enum apm_port_cycle_type_t
{
   APM_PORT_TYPE_CYCLIC = APM_OBJ_TYPE_CYCLIC,
   APM_PORT_TYPE_ACYCLIC = APM_OBJ_TYPE_ACYCLIC,
   APM_PORT_CYCLE_TYPE_MAX = APM_OBJ_CYCLE_TYPE_MAX

}apm_port_cycle_type_t;


/** Container info structure */
typedef struct apm_container_t apm_container_t;

/*------------------------------------------------------------------------------
 *  Constants/Macros
 *----------------------------------------------------------------------------*/

#if defined(CHIP_SPECIFIC) && defined(APM_NUM_MAX_PARALLEL_CMD)
    //APM_NUM_MAX_PARALLEL_CMD gets injected from chipspecific
#else
  #define APM_NUM_MAX_PARALLEL_CMD  (4)
#endif

#define APM_NUM_MAX_CAPABILITIES    (4)

/**< Hash tables size for module instance ID look up and Container ID look up */

#if defined(CHIP_SPECIFIC) && defined(APM_MODULE_HASH_TBL_SIZE)
    //APM_MODULE_HASH_TBL_SIZE gets injected from chipspecific
#else
  #define APM_MODULE_HASH_TBL_SIZE    (64)
#endif

#if defined(CHIP_SPECIFIC) && defined(APM_CONT_HASH_TBL_SIZE)
    //APM_CONT_HASH_TBL_SIZE gets injected from chipspecific
#else
  #define APM_CONT_HASH_TBL_SIZE      (16)
#endif



/**< Common Hash function for module IID & container ID */
#define APM_HASH_FUNC(obj_id, hash_tbl_size)         ((obj_id) & ((hash_tbl_size) - 1))

/** Number of data port types Input & output */
#define APM_NUM_DATA_PORT_TYPE        (2)

/*------------------------------------------------------------------------------
 *  Sub-Graph Structure Definition
 *----------------------------------------------------------------------------*/


/** Sub-graph property structure */
typedef struct apm_sub_graph_prop_t apm_sub_graph_prop_t;

struct apm_sub_graph_prop_t
{
   uint32_t          num_properties;
   /**< Number of properties configured
        for this sub-graph */

   uint32_t          perf_mode;
   /**< Perf mode for this sub-graph. */

   uint32_t          direction;
   /**< Data flow direction for this sub-graph. */

   uint32_t          scenario_id;
   /**< Scenario ID for this sub-graph. */

   uint32_t         vsid;
   /**< VSID for voice call use case. */

   uint16_t         enable_duty_cycling;
   uint16_t	        duty_cycling_clock_scale_factor_q4;
   uint16_t 	    clock_scale_factor_q4;
   uint16_t         bus_scale_factor_q4;

   /**< Duty cycling scale factor for this sub-graph. */

   uint32_t         payload_size;
   /**< Overall property payload size */
};

typedef struct apm_client_context_t
{
	uint8_t client_domain_id;
	/**< First client ID(domain id) for which SG is listed */

	uint32_t client_port_id;
	/**< First client ID(port id) for which SG is listed */

	apm_sub_graph_state_t   prev_state;
	/**< client specific previous Sub-graph state */

	apm_sub_graph_state_t   curr_state;
	   /**< client specific current Sub-graph state */

}apm_client_context_t;

/** Sub-graph info struture */
typedef struct apm_sub_graph_t apm_sub_graph_t;

struct apm_sub_graph_t
{
   uint32_t                sub_graph_id;
   /**< ID for this sub-graph */

   apm_sub_graph_prop_t    prop;
   /**< Sub-graph property structure */

   apm_sub_graph_state_t   state;
   /**< current actual Sub-graph state */

   uint32_t                num_containers;
   /**< Number of containers within this sub-graph. */

   spf_list_node_t         *container_list_ptr;
   /**< List of containers within this sub-graph
        Node Type: apm_container_t */

   apm_sub_graph_cfg_t     *cfg_cmd_payload;
   /**< Cache the payload pointer for command */

   spf_list_node_t         *client_list_ptr;
   /**< Cache the client context for a particular SG */
   /**node type apm_client_context_t*/

	uint32_t                num_client;
	/**< Number of client for which this SG is referred */

	bool_t                  is_calibrated;
	/**< Subgraph calibration state
	 * Used to check calibration state of subgraph
	 * during APM_CMD_DB_DEFAULT_SET_CFG
	 */
};

/*------------------------------------------------------------------------------
 *  Container Structure Definition
 *----------------------------------------------------------------------------*/
/*
 * This is a generic structure for APM lists.
 */

typedef struct apm_list_t apm_list_t;

struct apm_list_t
{
   uint32_t            num_nodes;
   /**< Number of list Node */

   spf_list_node_t     *list_ptr;
   /**< list pointer */
};

typedef struct apm_module_data_port_conn_t
{
	//TODO- Add src host/dst host at top and add the connections under that structure
	// Enhancement
	uint32_t src_mod_inst_id;
	/**< Instance identifier for the source module. */

	uint32_t src_mod_op_port_id;
	/**< Output port identifier for the source module. */

	uint32_t dst_mod_inst_id;
	/**< Instance identifier for the destination module. */

	uint32_t dst_mod_ip_port_id;
	/**< Input port identifier for the destination module. */

	uint32_t src_host_sub_graph_id;
	/**< host sub graph ID of the src module */

	uint32_t dst_host_sub_graph_id;
		/**< host sub graph ID of the dst module */

	spf_list_node_t  *client_list_ptr;
	/**< Cache the client context for a particular SG */
	/**node type apm_client_context_t*/

	uint32_t   num_client;
	/**< Number of client for which this SG is referred */
}apm_module_data_port_conn_t;

typedef struct apm_module_ctrl_port_conn_t
{
	//TODO- Add src host/dst host at top and add the connections under that structure
		// Enhancement
   uint32_t peer_1_mod_iid;
   /**< Instance identifier for the first peer module. */

   uint32_t peer_1_mod_ctrl_port_id;
   /**< Control port identifier for the first peer module. */

   uint32_t peer_2_mod_iid;
   /**< Instance identifier for the second peer module. */

   uint32_t peer_2_mod_ctrl_port_id;
   /**< Control port identifier for the second peer module. */

   uint32_t num_props;
   /**< Number of link properties (default = 0). */

   uint32_t peer_1_sub_graph_id;
   /**< host sub graph ID of the src module */

   uint32_t peer_2_sub_graph_id;
   /**< host sub graph ID of the dst module */

   spf_list_node_t  *client_list_ptr;
   /**< Cache the client context for a particular SG */
   /**node type apm_client_context_t*/

   uint32_t   num_client;
   /**< Number of client for which this SG is referred */
}apm_module_ctrl_port_conn_t;

typedef struct apm_cont_graph_t  apm_cont_graph_t;



/**
 * This structure is the header for all the cached configuration
 * paramters need to be set/get to/from against the container
 * specific param IDs
 */
typedef struct apm_cont_set_get_cfg_hdr_t apm_cont_set_get_cfg_hdr_t;

struct apm_cont_set_get_cfg_hdr_t
{
   uint32_t  param_id;
   /**< Header for caching the container param */
};

/** Structure for offload graph open payload configurations to be associated with
 *  offload container
 */
typedef struct apm_offload_cont_sat_graph_open_cfg_t apm_offload_cont_sat_graph_open_cfg_t;

struct apm_offload_cont_sat_graph_open_cfg_t
{
   uint32_t num_satellite_containers;
   /**< Number of parameters being configured */

   spf_list_node_t *sat_cnt_config_ptr; // apm_container_cfg_t
   /**< List of satellite container configuration payload
        Node Type: apm_container_cfg_t */
};

/** Structure for satellite container list associated with the OLC */
typedef struct apm_sat_cont_list_t apm_sat_cont_list_t;

struct apm_sat_cont_list_t
{
   uint32_t num_of_satellite_cnts;
   /**< Number of satellite containers associated with
    *   this container. This is non-zero only for off-load
    *   container  */

   spf_list_node_t *satellite_cnts_list_ptr;
   /**< List of satellite container associated with OLC
    * The object structure is of the type apm_cont_satellite_info_t */
};

typedef struct apm_cont_graph_open_params_t apm_cont_graph_open_params_t;

struct apm_cont_graph_open_params_t
{
   apm_container_cfg_t  *container_prop_ptr;
   /**< Container properties received
        from the client */

   uint32_t             num_sub_graphs;
   /**< Number of sub-graphs configured */

   spf_list_node_t      *sub_graph_cfg_list_ptr;
   /**< List of sub-graphs ID's configured
        Node Type: apm_sub_graph_t */

   uint32_t             num_module_list_cfg;
   /**< Number of module lists configured */

   spf_list_node_t      *module_cfg_list_ptr;
   /**< Pointer to list of modules configured
        Node Type: apm_modules_list_t */

   uint32_t             num_module_props;
   /**< Number of module configured */

   spf_list_node_t      *module_prop_list_ptr;
   /**< Pointer to list of modules configured
        Node Type: apm_module_prop_cfg_t */

   uint32_t             num_data_links;
   /**< Number of data links configured */

   spf_list_node_t      *mod_data_link_cfg_list_ptr;
   /**< List of module data link connections within this container
        Node Type: apm_module_conn_cfg_t */

   uint32_t            num_offloaded_imcl_peers;
   /**< Number of unique inter-proc ctrl connections */

   spf_list_node_t      *imcl_peer_domain_info_list_ptr;
    /**< Array of pointers for module control port connection objects.
        Gives the info about the domain IDs of the peers.
        Length of array is determined using the number
        of unique inter-proc ctrl connections being made.
        Node Type: apm_imcl_peer_domain_info_t */

   uint32_t             num_ctrl_links;
   /**< Number of control links configured */

   spf_list_node_t      *mod_ctrl_link_cfg_list_ptr;
   /**< List of module control link config within this container
        Node Type: apm_module_ctrl_link_cfg_t */

   uint32_t             num_mod_param_cfg;
   /**< Number of module param config */

   spf_list_node_t      *param_data_list_ptr;
   /**< List of module parameter configuration payloads
        Node Type: apm_module_param_data_t */

   apm_offload_cont_sat_graph_open_cfg_t sat_graph_open_cfg;
   /**< satellite container configuration payload for graph open */

   apm_list_t          mxd_heap_id_links_cfg[LINK_TYPE_MAX];
   /**< Array of the strcuture type apm_mxd_heap_id_link_cfg_t with one
        struct each for data and control link information
        across this container and a Non-default heap container.
        Node Type: apm_mxd_heap_id_link_cfg_t */
};

/** Structure for caching the data/control links  and
 *  data/control port handles to be closed as part of the
 *  data/control link param ID received in the graph close
 *  command */
typedef struct apm_cont_gm_link_close_params_t apm_cont_gm_link_close_params_t;

struct apm_cont_gm_link_close_params_t
{
   apm_list_t         cont_ports[APM_PORT_CYCLE_TYPE_MAX][PORT_TYPE_MAX];
   /**< Array of the structure apm_cont_port with 3 elements
        input output and control port respectively
        Node Type: spf_module_port_conn_t */

   uint32_t            num_data_links;
   /**< Number of data links configured */

   spf_list_node_t      *mod_data_link_cfg_list_ptr;
   /**< List of module data link connections within
        this container.
        Node Type: apm_module_conn_cfg_t */

   uint32_t             num_ctrl_links;
   /**< Number of control links configured */

   spf_list_node_t      *mod_ctrl_link_cfg_list_ptr;
   /**< List of module control link config within
        this container,
        Node Type: apm_module_ctrl_link_cfg_t */
};

/** Structure for caching the sub-graphs and data/control port
 *  handles to be closed as part of the sub-graph list param ID
 *  received in the graph close command */
typedef struct apm_cont_graph_mgmt_params_t apm_cont_graph_mgmt_params_t;

struct apm_cont_graph_mgmt_params_t
{
   uint32_t             num_sub_graphs;
   /**< Number of sub-graphs configured */

   spf_list_node_t      *sub_graph_cfg_list_ptr;
   /**< List of sub-graphs ID's configured
        Node Type: apm_sub_graph_t */

   apm_list_t           cont_ports[APM_PORT_CYCLE_TYPE_MAX][PORT_TYPE_MAX];
   /**< Array of the structure apm_cont_port with 3 elements
     input output and control port respectively
     Node Type: spf_module_port_conn_t* */

   uint32_t             num_data_links;
   /**< Number of data links configured */

   spf_list_node_t      *mod_data_link_cfg_list_ptr;
   /**< List of module data link connections within
        this container.
        Node Type: apm_module_conn_cfg_t */

   uint32_t             num_ctrl_links;
   /**< Number of control links configured */

   spf_list_node_t      *mod_ctrl_link_cfg_list_ptr;
   /**< List of module control link config within
        this container,
        Node Type: apm_module_ctrl_link_cfg_t */
};

typedef struct apm_cont_module_cfg_params_t apm_cont_module_cfg_params_t;

struct apm_cont_module_cfg_params_t
{
   uint32_t             num_mod_param_cfg;
   /**< Number of module param config */

   spf_list_node_t      *param_data_list_ptr;
   /**< List of module parameter configuration payloads
        Node Type: apm_module_param_data_t */
};

typedef struct apm_cont_cfg_params_t apm_cont_cfg_params_t;

struct apm_cont_cfg_params_t
{
   uint32_t         num_cfg_params;
   /**< Number of config paramaters */

   spf_list_node_t   *param_data_list_ptr;
   /**< List of config paramaters
         Obj type: apm_cont_set_get_cfg_hdr_t */
};

/** Cached configuration parameters from client */
typedef struct apm_cont_cached_cfg_t apm_cont_cached_cfg_t;

struct apm_cont_cached_cfg_t
{
   /** Using the union as only 1 type of command is handled per
    *  commond control instance. */
   union
   {
      apm_cont_graph_open_params_t graph_open_params;
      /**< Cached parameter for container related to
        GRAPH OPEN command */

      apm_cont_graph_mgmt_params_t graph_mgmt_params;
      /**< Cached parameter for container related to
        GRAPH MGMT commands */

      apm_cont_module_cfg_params_t module_cfg_params;
      /**< Cached parameter for container related to
		   module SET/GET CFG commands */
   };

   apm_cont_cfg_params_t        cont_cfg_params;
   /**< Cached parameter for container related to
        APM doing SET/GET CFG on the container.
        This SET/GET is typically triggered either
        as part of client command or container raising
        an event for requesting info from the APM
        spanning across multiple containers */

};

 /** Response message received from a container */
typedef struct apm_cont_cmd_rsp_ctrl_t apm_cont_cmd_rsp_ctrl_t;

struct apm_cont_cmd_rsp_ctrl_t
{
   bool_t      pending_msg_proc;
   /**< If the message processing is pending
        for this container */

   bool_t      rsp_pending;
   /**< If the response is pending from
        this container */

   spf_msg_t    rsp_msg;
   /**< Response message from container */

   ar_result_t rsp_result;
   /**< Response result */

   bool_t      reuse_rsp_msg_buf;
   /**< Reuse response msg buffer from container
        for subsequence message  */
};

/** List of upstream and down stream peer containers for a
 *  container */
typedef struct apm_cont_peer_list_t apm_cont_peer_list_t;

struct apm_cont_peer_list_t
{
   uint32_t         num_upstream_cont;
   /**< Number of upstream containers */

   spf_list_node_t  *upstream_cont_list_ptr;
   /**< List of containers upstream
        Node Type: apm_container_t */

   uint32_t         num_downstream_cont;
   /**< Number of downstream containers */

   spf_list_node_t  *downstream_cont_list_ptr;
   /**< List of containers downstream
        Node Type: apm_container_t */

};

typedef struct apm_cont_port_connect_info_t apm_cont_port_connect_info_t;

struct apm_cont_port_connect_info_t
{
   apm_sub_graph_t         *self_sg_obj_ptr;
   /**< Sub-graph obj of self port in the
        connection link */

   apm_sub_graph_t         *peer_sg_obj_ptr;
   /**< Sub-graph obj of peer port in the
        connection link */

   apm_list_t              peer_cont_list;
   /**< List of peer containers */

   apm_sub_graph_state_t   peer_sg_propagated_state;
   /**< Propagated state of the peer sub-graph */

   uint32_t                num_port_conn;
   /**< Number of  port connections */

   spf_list_node_t         *port_conn_list_ptr;
   /**< List of port connections for
     this container.
     Node Type: spf_module_port_conn_t */
};

typedef struct apm_cont_cmd_params_t apm_cont_cmd_params_t;

struct apm_cont_cmd_params_t
{
   apm_cont_gm_link_close_params_t  link_close_params;
   /**< Cached parameter for container related to
         data and control links close */
};

typedef struct apm_cont_cmd_ctrl_t  apm_cont_cmd_ctrl_t;

struct apm_cont_cmd_ctrl_t
{
   apm_cmd_token_t msg_token;
   /**< Token to identify the context from a
        response message received. */

   uint32_t                     list_idx;
   /**< Flag to indicate if this cmd control object is in unse  */

   void                         *apm_cmd_ctrl_ptr;
   /**< Pointer to APM cmd control  */

   void                         *host_container_ptr;
   /**< Pointer to host container for this control object  */

   apm_cont_cached_cfg_t         cached_cfg_params;
   /**< Cached configuration parameters to be sent to
        containers for a single SPF_MSG corresponding to
        current APM command under process */

   apm_cont_cmd_params_t         cmd_params;
   /**< APM command parameters to be used across
        multiple SPF_MSG's part of the single APM
        command under process */

   apm_cont_cmd_rsp_ctrl_t       rsp_ctrl;
   /**< Container response message info corresponding
        to this commands */
};

typedef struct apm_cont_cmd_list_t  apm_cont_cmd_list_t;

struct apm_cont_cmd_list_t
{
   uint32_t                   active_cmd_mask;
   /**< Bit mask for active commands under process */

   apm_cont_cmd_ctrl_t        cmd_ctrl_list[APM_NUM_MAX_PARALLEL_CMD];
   /**< Cached configuration parameters from client */
};


/** Sub-graph property structure */
typedef struct apm_container_prop_t apm_container_prop_t;

struct apm_container_prop_t
{
   uint32_t          num_properties;
   /**< Number of properties configured
        for this sub-graph */

   apm_container_type_t    cntr_type;
/*Type of the container*/

   uint32_t          graph_position;
   /**< Data flow direction for this sub-graph. */

   uint32_t          stack_size;
   /**< Container's stack size. */

   uint32_t         proc_domain;
   /**< Proc Domain that the container belongs to */

   uint32_t         heap_id;
   /**< Heap ID that the container uses*/

   uint32_t         peer_heap_id;
   /**< Heap ID that the container's peer uses'*/
};

typedef struct apm_cont_graph_utils_t apm_cont_graph_utils_t;

struct apm_cont_graph_utils_t
{
   bool_t   node_visted;
   /**< Container has been visted once as
        part topo sort   */

   bool_t   node_sorted;
   /**< Container has been visted once as
        part topo sort   */

   uint16_t out_degree;
   /**< Counter to keep track of current output
        ports during the topo sort process  */
};

struct apm_container_t
{
   uint32_t                      container_id;
   /**< Container ID */

   uint32_t                      prop_payload_size;
   /**< Property payload size */

   apm_container_prop_t prop;
   /**< Properties of the container.*/

   spf_handle_t                   *cont_hdl_ptr;
   /**< Handle returned by container as part of
        container creation */

   apm_cont_cmd_list_t           cmd_list;
   /**< List of commands under process for this container */

   uint32_t                      num_sub_graphs;
   /**< Number of sub-graphs within this container */

   spf_list_node_t               *sub_graph_list_ptr;
   /**< List of containers within this sub-graph
        Node Type: apm_sub_graph_t */

   uint32_t                      num_pspc_module_lists;
   /**< Number of pspc module lists within this container */

   spf_list_node_t               *pspc_module_list_node_ptr;
   /**< List of modules within this container
        on per sub-graph basis
        Node Type: apm_pspc_module_list_t */

   apm_list_t                    cont_ports_per_sg_pair[APM_PORT_CYCLE_TYPE_MAX][PORT_TYPE_MAX];
   /**< Array of the structure apm_cont_port with 3 elements
       input output and control port respectively
       Node Type: apm_cont_port_connect_info_t* */

   apm_cont_peer_list_t          peer_list;
   /**< List of upstream and downstream peer containers */

   apm_cont_graph_utils_t        graph_utils;
   /**< Graph sorted related book keeping */

   apm_cont_graph_t              *cont_graph_ptr;
   /**< Pointer to graph hosting this container  */

   apm_sat_cont_list_t           sat_cont_list;
   /**< List of satellite container associated with OLC */

   bool_t                        newly_created;
   /**< Flag to keep track if the container is newly created  */

};


/*------------------------------------------------------------------------------
 *  Module Structure Definition
 *----------------------------------------------------------------------------*/

/** Module data link struct */
typedef struct apm_module_data_link_t
{
   cntr_graph_vertex_t        src_port;
   /**< Source port in the data link */

   cntr_graph_vertex_t        dstn_port;
   /**< Dstn port in the data link */

   bool_t                     node_visted;
   /**< Flag used while performing DFS
        on the data link graph */
}apm_module_data_link_t;


/** Module info structure */
typedef struct apm_module_t apm_module_t;

struct apm_module_t
{
   uint32_t          instance_id;
   /**< Module Instance ID */

   uint32_t          module_id;
   /**< Module ID */

   apm_container_t   *host_cont_ptr;
   /**< Pointer to host container
     for this module */

   apm_sub_graph_t   *host_sub_graph_ptr;
   /**< Pointer to host sub_graph
     for this module */

   uint32_t           num_input_data_link;
   /**< Number of input data connections for
        this module */

   spf_list_node_t    *input_data_link_list_ptr;
   /**< Pointer to the list of input data link
        Obj type: apm_module_data_link_t */

   uint32_t           num_output_data_link;
   /**< Number of output data links for
        this module */

   spf_list_node_t    *output_data_link_list_ptr;
   /**< Pointer to the list of output data link
        Obj type: apm_module_data_link_t */
};

/** Per-container-per-subgraph module list structure */

typedef struct apm_pspc_module_list_t apm_pspc_module_list_t;

struct apm_pspc_module_list_t
{
   uint32_t        sub_graph_id;
   /**< Sub-graph ID for this module */

   uint32_t        container_id;
   /**< Container ID for this module */

   uint32_t        num_modules;

   spf_list_node_t  *module_list_ptr;
   /**< Module list pointer
        Node Type: apm_module_t */
};

/*------------------------------------------------------------------------------
 *  APM Graph Database Structure Definition
 *----------------------------------------------------------------------------*/

struct apm_cont_graph_t
{
   uint32_t        num_sub_graphs;
   /**< Number of sub-graphs in this graph */

   spf_list_node_t *sub_graph_list_ptr;
   /**< List of sub-graphs
        Node Type: apm_sub_graph_t */

   uint32_t        num_containers;
   /**< Number of containers in this graph */

   spf_list_node_t  *container_list_ptr;
   /**< Pointer to list of containers in this graph
        Node Type: apm_container_t */

   spf_list_node_t  *cont_list_tail_node_ptr;
   /**< Pointer to tail node in the list of containers
        in this graph
        Node Type: apm_container_t */

   bool_t          graph_is_sorted;
   /**< Flag to indicate if graph is already sorted */
};

/** APM graph info struture */
typedef struct apm_graph_info_t apm_graph_info_t;

struct apm_graph_info_t
{
   uint32_t          num_sub_graphs;
   /**< Number of sub-graphs */

   spf_list_node_t    *sub_graph_list_ptr;
   /**< List of sub-graph ID's
        Node Type: apm_sub_graph_t */

   uint32_t          num_sub_graph_conn;
   /**< List of sub-graph ID's */

   spf_list_node_t    *sub_graph_conn_list_ptr;
   /**< List of sub-graph ID's
        Node Type: apm_cont_port_connect_info_t */

   uint32_t             num_conn;
    /**< Number of connections at boundary */

   spf_list_node_t     *conn_cfg_list_ptr;
   /**< Pointer to the list of connections*/

   uint32_t             num_ctrl_link;
   /**< Number of connections at boundary */

   spf_list_node_t     *ctrl_link_cfg_list_ptr;
   /**< Pointer to the list of connections*/

   uint32_t          num_containers;
   /**< Number of containers */

   spf_list_node_t    *container_list_ptr[APM_CONT_HASH_TBL_SIZE];
   /**< List of containers.
        Node Type: apm_container_t */

   uint32_t       num_satellite_container;
   /**< Number of satellite containers */

   spf_list_node_t    *sat_container_list_ptr;
   /**< List of satellite cont node info.
        Node Type: apm_satellite_cont_node_info_t */

   uint32_t          num_modules;
   /**< Number of sub-graphs */

   spf_list_node_t    *module_list_ptr[APM_MODULE_HASH_TBL_SIZE];
   /**< List of module within this container.
        Node Type: apm_module_t */

   uint32_t          container_count;
   /**< Container counter, incremented every
        time a new container is created.
        Counter is reset during APM init only.
        Wrap around for this counter is allowed */

   uint32_t          num_cont_graphs;
   /**< Number of disjoint container graphs */

   spf_list_node_t    *cont_graph_list_ptr;
   /**< List of disjoint container graphs
        Node Type: apm_cont_graph_t */

   spf_list_node_t    *standalone_cont_list_ptr;
   /**< List of standalone containers not part of any graphs.
        This list is populated during GRAPH OPEN.
        Node Type: apm_container_t */

   uint32_t          num_proxy_managers;
   /**< Number of nodes in Proxy_Manager_list_ptr*/

   spf_list_node_t   *proxy_manager_list_ptr;
   /**< List of ProxyManagers node type: apm_proxy_manager_t.
        This list is populated during GRAPH OPEN. */

   uint32_t          num_data_paths;
   /**< Number of data paths */

   spf_list_node_t    *data_path_list_ptr;
   /**< List of data paths
        Obj Type: apm_data_path_delay_info_t */
};

/* clang-format on */

/*------------------------------------------------------------------------------
 *  Function Declaration
 *----------------------------------------------------------------------------*/
ar_result_t apm_db_add_node_to_list(spf_list_node_t **list_head_pptr, void *list_node_ptr, uint32_t *node_cntr_ptr);

ar_result_t apm_db_search_and_add_node_to_list(spf_list_node_t **list_head_pptr,
                                               void             *list_node_ptr,
                                               uint32_t         *node_cntr_ptr);

ar_result_t apm_db_remove_node_from_list(spf_list_node_t **list_head_pptr,
                                         void             *list_node_ptr,
                                         uint32_t         *node_cntr_ptr);

ar_result_t apm_db_add_obj_to_list(spf_list_node_t    **list_pptr,
                                   void                *obj_ptr,
                                   uint32_t             obj_id,
                                   apm_graph_obj_type_t obj_type,
                                   uint32_t            *obj_cntr_ptr);

ar_result_t apm_db_remove_obj_from_list(spf_list_node_t    **list_pptr,
                                        void                *obj_ptr,
                                        uint32_t             obj_id,
                                        apm_graph_obj_type_t obj_type,
                                        uint32_t            *obj_cntr_ptr);

ar_result_t apm_db_get_sub_graph_node(apm_graph_info_t *graph_info_ptr,
                                      uint32_t          sub_graph_id,
                                      apm_sub_graph_t **sub_graph_pptr,
                                      apm_db_query_t    query_type);

ar_result_t apm_db_get_mod_connection(apm_graph_info_t             *graph_info_ptr,
                                      apm_module_conn_cfg_t        *conn_cfg_ptr,
                                      apm_module_data_port_conn_t **conn_cfg_pptr,
                                      apm_db_query_t                query_type);

ar_result_t apm_db_get_mod_ctrl_link(apm_graph_info_t             *graph_info_ptr,
                                     apm_module_ctrl_link_cfg_t   *ctrl_link_cfg_ptr,
                                     apm_module_ctrl_port_conn_t **ctrl_link_cfg_pptr,
                                     apm_db_query_t                query_type);

ar_result_t apm_db_get_container_node(apm_graph_info_t *graph_info_ptr,
                                      uint32_t          container_id,
                                      apm_container_t **container_pptr,
                                      apm_db_query_t    query_type);

ar_result_t apm_db_get_module_node(apm_graph_info_t *graph_info_ptr,
                                   uint32_t          mod_instance_id,
                                   apm_module_t    **module_pptr,
                                   apm_db_query_t    query_type);

ar_result_t apm_db_get_cont_port_info_node(spf_list_node_t               *cont_port_info_list_ptr,
                                           uint32_t                       self_cont_port_sg_id,
                                           uint32_t                       peer_cont_port_sg_id,
                                           apm_cont_port_connect_info_t **port_connect_info_node_pptr);

ar_result_t apm_db_get_module_list_node(spf_list_node_t         *module_list_ptr,
                                        uint32_t                 sub_graph_id,
                                        uint32_t                 container_id,
                                        apm_pspc_module_list_t **mod_list_node_pptr);

ar_result_t apm_db_get_cont_port_conn(spf_list_node_t         *cont_port_info_list_ptr,
                                      uint32_t                 module_iid,
                                      uint32_t                 port_id,
                                      spf_module_port_conn_t **port_conn_info_pptr,
                                      apm_db_query_t           obj_query_type);

uint32_t apm_db_get_num_instances(apm_graph_info_t *graph_info_ptr);

#ifdef __cplusplus
}
#endif /*__cplusplus*/
#endif /* _APM_GRAPH_DB_H__ */
