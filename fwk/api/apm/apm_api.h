#ifndef _APM_API_H_
#define _APM_API_H_
/**
 * \file apm_api.h
 * \brief
 *   This file contains Audio Processing Manager Commands Data Structures
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_defs.h"

#ifdef __cplusplus
extern "C"
{
#endif /*__cplusplus*/

/*# h2xml_title1          {Audio Processing Manager (APM) Module API}
    h2xml_title_agile_rev {Audio Processing Manager (APM) Module API}
    h2xml_title_date      {June 27, 2019} */

/*------------------------------------------------------------------------------
 *  Module ID Definitions
 *----------------------------------------------------------------------------*/

/** @ingroup spf_apm_commands
    Common payload header structure used by the following commands:
    - #APM_CMD_GRAPH_OPEN
    - #APM_CMD_GRAPH_PREPARE
    - #APM_CMD_GRAPH_START
    - #APM_CMD_GRAPH_STOP
    - #APM_CMD_GRAPH_CLOSE
    - #APM_CMD_GRAPH_FLUSH
    - #APM_CMD_SET_CFG
    - #APM_CMD_GET_CFG
    - #APM_CMD_REGISTER_CFG
    - #APM_CMD_DEREGISTER_SHARED_CFG
    - #APM_CMD_REGISTER_MODULE_EVENTS
    - #APM_CMD_GRAPH_SUSPEND

    For in-band payloads:
     - This structure is followed by an in-band Payload for type
       apm_module_param_data_t.

    For out-of-band payloads:
     - An out-of-band payload can be extracted from the mem_map_handle,
       payload_address_lsw, and payload_address_msw using
       posal_memorymap_get_virtual_addr_from_shmm_handle.
     - An out-of-band Payload for %apm_cmd_header_t can be of two types:
       @vertspace{3}
        - apm_module_param_data_t @vertspace{-2}
        - apm_module_param_shared_data_t -- To be used only for the following
          APM commands: @vertspace{2}
            - #APM_CMD_REGISTER_SHARED_CFG @vertspace{-2}
            - #APM_CMD_DEREGISTER_SHARED_CFG @vertspace{9}
 */
#include "spf_begin_pack.h"
struct apm_cmd_header_t
{
   uint32_t payload_address_lsw;
   /**< Lower 32 bits of the payload address. */

   uint32_t payload_address_msw;
   /**< Upper 32 bits of the payload address.

        The 64-bit number formed by payload_address_lsw and
        payload_address_msw must be aligned to a 32-byte boundary and be in
        contiguous memory.

        @valuesbul
        - For a 32-bit shared memory address, this field must be set to 0.
        - For a 36-bit shared memory address, bits 31 to 4 of this field must
          be set to 0. @tablebulletend */

   uint32_t mem_map_handle;
   /**< Unique identifier for a shared memory address.

        @valuesbul
        - NULL -- The message is in the payload (in-band).
        - Non-NULL -- The parameter data payload begins at the address
          specified by a pointer to the physical address of the payload in
          shared memory (out-of-band).

        The APM returns this memory map handle through
        #APM_CMD_SHARED_MEM_MAP_REGIONS.

        An optional field is available if parameter data is in-band:
        %afe_port_param_data_v2_t param_data[...]. */

   uint32_t payload_size;
   /**< Actual size of the variable payload accompanying the message or in
        shared memory. This field is used for parsing both in-band and
        out-of-band data.

        @values > 0 bytes, in multiples of 4 bytes */
}
#include "spf_end_pack.h"
;
typedef struct apm_cmd_header_t apm_cmd_header_t;


/** @ingroup spf_apm_commands
    Contains the module parameter data.

    Immediately following this structure are param_size bytes of calibration
    data. The structure and size depend on the  module_instance_id/param_id
    combination.
*/
#include "spf_begin_pack.h"
struct apm_module_param_data_t
{
   uint32_t module_instance_id;
   /**< Unique identifier for the instance of the APM module. */

   uint32_t param_id;
   /**< Unique identifier for the parameter. */

   uint32_t param_size;
   /**< Size of the parameter data based on the module_instance_id/param_id
        combination.

        @values > 0 bytes, in multiples of at least 4 bytes */

   uint32_t error_code;
   /**< Error code populated by the entity that is hosting the module.

        This field is applicable only for the out-of-band command mode. */
}
#include "spf_end_pack.h"
;
typedef struct apm_module_param_data_t apm_module_param_data_t;


/** @ingroup spf_apm_commands
    Contains the shared persistent module parameter data.

    Immediately following this structure are param_size bytes of
    calibration data. The structure and size depend on the param_id.
*/
#include "spf_begin_pack.h"
struct apm_module_param_shared_data_t
{
   uint32_t param_id;
   /**< Unique identifier for the parameter. */

   uint32_t param_size;
   /**< Size of the parameter data based on the module_instance_id/param_id
        combination.

        @values > 0 bytes, in multiples of at least 4 bytes @newpagetable */
}
#include "spf_end_pack.h"
;
typedef struct apm_module_param_shared_data_t apm_module_param_shared_data_t;


/** @ingroup spf_apm_commands
    Contains the property data. This structure applies to
    container, sub-graph, and module properties.

    Immediately following this structure are prop_size bytes of property data.
    The payload size must be at least 4 bytes.
*/
#include "spf_begin_pack.h"
struct apm_prop_data_t
{
   uint32_t prop_id;
   /**< Unique identifier  for the property. */

   uint32_t prop_size;
   /**< Size of the property ID structure. */
}
#include "spf_end_pack.h"
;
typedef struct apm_prop_data_t apm_prop_data_t;


/** @ingroup spf_apm_commands
    Contains the register events data. This structure is preceded by
    apm_cmd_header_t.
*/
#include "spf_begin_pack.h"
struct apm_module_register_events_t
{
   uint32_t module_instance_id;
   /**< Unique identifier  for the instance of the module. */

   uint32_t event_id;
   /**< Unique event identifier for the module. */

   uint32_t is_register;
   /**< Specifies whether the event is registered.

        @valuesbul
        - 1 -- Register the event
        - 0 -- De-register the event @tablebulletend */

   uint32_t error_code;
   /**< Error code populated by the entity that is hosting the module.

        This field is applicable only for the out-of-band command mode. */

   uint32_t event_config_payload_size;
   /**< Size of the event configuration data based on the
        module_instance_id/event_id combination.

        @values > 0 bytes, in multiples of at least 4 bytes */

   uint32_t reserved;
   /**< This field must be set to 0. */
   /* For 8 byte alignment */
}
#include "spf_end_pack.h"
;
typedef struct apm_module_register_events_t apm_module_register_events_t;


/** @ingroup spf_apm_commands
    Contains the module event data. This structure is preceded by
    apm_cmd_header_t and followed by the event payload.
*/
#include "spf_begin_pack.h"
struct apm_module_event_t
{
   uint32_t event_id;
   /**< Unique event identifier for the module. */

   uint32_t event_payload_size;
   /**< Size of the event data based on the module_instance_id/event_id
        combination.

        @values > 0 bytes, in multiples of at least 4 bytes. */
}
#include "spf_end_pack.h"
;
typedef struct apm_module_event_t apm_module_event_t;


/** @ingroup spf_apm_commands
    Identifier for an instance of the APM module.

    This module instance ID supports the following parameter IDs:
    - #APM_PARAM_ID_SUB_GRAPH_CONFIG
    - #APM_PARAM_ID_CONTAINER_CONFIG
    - #APM_PARAM_ID_MODULES_LIST
    - #APM_PARAM_ID_MODULE_PROP
    - #APM_PARAM_ID_MODULE_CONN
    - #APM_PARAM_ID_MODULE_CTRL_LINK_CFG
    - #APM_PARAM_ID_PATH_DELAY
    - #APM_PARAM_ID_SUB_GRAPH_LIST
*/
#define APM_MODULE_INSTANCE_ID                    0x00000001

/*# h2xmlm_module             {"APM_MODULE_INSTANCE_ID",
                                 APM_MODULE_INSTANCE_ID}
    h2xmlm_displayName        {"Audio Processing Manager"}
    h2xmlm_description        {Identifier for an APM module instance.}
    h2xmlm_dataMaxInputPorts  {0}
    h2xmlm_dataMaxOutputPorts {0}
    h2xmlm_stackSize          {0}
    h2xmlm_ToolPolicy         {Calibration}

    {                   <-- Start of the Module -->
    }                   <-- End of the Module   --> */


/** @ingroup spf_apm_commands
    Opens a list of one or more sub-graphs.

    @gpr_hdr_fields
    Opcode -- APM_CMD_GRAPH_OPEN

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_sub_graph_cfg_t  (Contains the number of
                                                   sub-graphs.) \n
          @indent{24pt} (The following list of structures is repeated for each
                         sub-graph ID being configured.) \n
          @indent{24pt} apm_sub_graph_cfg_t \n
          @indent{24pt} apm_prop_data_t \n
          @indent{24pt} apm_sg_prop_id_perf_mode_t \n
          @indent{24pt} apm_prop_data_t \n
          @indent{24pt} apm_sg_prop_id_direction_t \n
          @indent{24pt} apm_prop_data_t \n
          @indent{24pt} apm_sg_prop_id_scenario_id_t \n
          @indent{24pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_container_cfg_t  (Contains the number of
                                                   containers.) \n
          @indent{24pt} (The following list of structures is repeated for each
                         container ID being configured.) \n
          @indent{24pt} apm_container_cfg_t \n
          @indent{24pt} apm_prop_data_t \n
          @indent{24pt} apm_cont_prop_id_type_t \n
          @indent{24pt} apm_container_type_t \n
          @indent{24pt} apm_prop_data_t \n
          @indent{24pt} apm_cont_prop_id_graph_pos_t \n
          @indent{24pt} apm_prop_data_t \n
          @indent{24pt} apm_cont_prop_id_stack_size_t \n
          @indent{24pt} apm_prop_data_t \n
          @indent{24pt} apm_cont_prop_id_proc_domain_t \n
          @indent{24pt} apm_prop_data_t \n
          @indent{24pt} apm_cont_prop_id_parent_container_t \n
          @indent{24pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_modules_list_t  (Contains the number of
                                                  module lists.) \n
          @indent{24pt} (The following structure is repeated for each module
                         list per sub-graph/container ID pair being
                         configured.) \n
          @indent{24pt} apm_modules_list_t  (Number of modules in the list) \n
          @indent{24pt} (The following structure is repeated for each module
                         instance being configured.) \n
          @indent{24pt} apm_module_cfg_t \n
          @indent{24pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_module_prop_t  (Contains the number of module
                                                 property objects) \n
          @indent{24pt} (The following structure is repeated for each module
                         property configuration object. \n
          @indent{24pt} apm_module_prop_cfg_t  (Contains the number of
                                             properties per module instance) \n
          @indent{24pt} (The following list of structures is repeated for each
                         module instance property being configured.) \n
          @indent{24pt} apm_prop_data_t \n
          @indent{24pt} apm_module_prop_id_port_info_t \n
          @indent{24pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_module_conn_t  (Contains the number of data
                                                 connections.) \n
          @indent{24pt} (The following structure is repeated for each data link
                         being configured.) \n
          @indent{24pt} apm_module_conn_cfg_t \n
          @indent{24pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_module_ctrl_link_cfg_t  (Contains the number
                                                          of control links.) \n
          @indent{24pt} (The following structure is repeated for each control link being configured.) \n
          @indent{24pt} apm_module_ctrl_link_cfg_t  (Contains the number of
                                                     control link intents.) \n
          @indent{24pt} (The following list of structures is repeated for each
                         control link property.) \n
          @indent{24pt} apm_prop_data_t \n
          @indent{24pt} apm_module_ctrl_link_prop_id_intent_list_t \n
      @indent{12pt} (Any non-APM module calibration payloads follow.) \n
      @indent{12pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any, but not mandatory at the
                      end> \n
    @newpage

    @detdesc
    This command is issued against an APM module instance ID.
    The following parameter IDs are sent as part of the command:
    - #APM_PARAM_ID_SUB_GRAPH_CONFIG
    - #APM_PARAM_ID_CONTAINER_CONFIG
    - #APM_PARAM_ID_MODULES_LIST
    - #APM_PARAM_ID_MODULE_PROP
    - #APM_PARAM_ID_MODULE_CONN
    - #APM_PARAM_ID_MODULE_CTRL_LINK_CFG
    @par
    In the command payload, the APM_PARAM_ID_SUB_GRAPH_CONFIG and
    APM_PARAM_ID_CONTAINER_CONFIG parameter IDs are populated before any other
    parameter IDs are populated. Thus, the sub-graph and container IDs must be
    configured before configuring the rest of the parameter IDs. Configure the
    following in this sequence:
    -# APM_PARAM_ID_SUB_GRAPH_CONFIG and APM_PARAM_ID_CONTAINER_CONFIG
    -# APM_PARAM_ID_MODULES_LIST
    -# APM_PARAM_ID_MODULE_PROP
    -# APM_PARAM_ID_MODULE_CONN
    -# APM_PARAM_ID_MODULE_CTRL_LINK_CFG
    @par
    The entire payload is of variable length. The length of the payload depends
    upon the number of sub-graphs, containers, and modules being configured as
    part of this command.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    None. @newpage
 */
#define APM_CMD_GRAPH_OPEN                        0x01001000

/** @ingroup spf_apm_commands
    Prepares one or more sub-graph IDs that were configured using
    #APM_CMD_GRAPH_OPEN.

    @gpr_hdr_fields
    Opcode -- APM_CMD_GRAPH_PREPARE

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_sub_graph_list_t  (Contains the number of
                                                    sub-graph IDs.) \n
      @indent{12pt} apm_sub_graph_id_t  (Array of sub-graph IDs.)

    @detdesc
    This command is issued against an APM module instance ID. The command
    consists of the #APM_PARAM_ID_SUB_GRAPH_LIST parameter ID,and it can be
    issued after the APM_CMD_GRAPH_OPEN command.
    @par
    If this command is issued to more than one sub-graph ID, it is executed in
    a sequential manner for each sub-graph.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Sub-graph IDs listed as part of this command must be configured using
    APM_CMD_GRAPH_OPEN or the APM returns an error code. @newpage
 */
#define APM_CMD_GRAPH_PREPARE                     0x01001001

/** @ingroup spf_apm_commands
    Starts one or more sub-graph IDs that were configured using
    #APM_CMD_GRAPH_OPEN.

    @gpr_hdr_fields
    Opcode -- APM_CMD_GRAPH_START

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_sub_graph_list_t  (Contains the number of
                                                    sub-graph IDs.) \n
      @indent{12pt} apm_sub_graph_id_t  (Array of sub-graph IDs.

    @detdesc
    This command is issued against APM module instance ID. The command
    consists of the #APM_PARAM_ID_SUB_GRAPH_LIST parameter ID, and it can be
    issued after the following commands:
    - #APM_CMD_GRAPH_OPEN
    - #APM_CMD_GRAPH_PREPARE
    - #APM_CMD_GRAPH_STOP
    - #APM_CMD_GRAPH_SUSPEND
    @par
    If this command is issued to more than one sub-graph ID, it is executed in
    a sequential manner for each sub-graph.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Sub-graph IDs listed as part of this command must be configured using
    #APM_CMD_GRAPH_OPEN or the APM returns an error code. @newpage
 */
#define APM_CMD_GRAPH_START                       0x01001002

/** @ingroup spf_apm_commands
    Stops one or more sub-graph IDs that were configured using
    #APM_CMD_GRAPH_OPEN.

    @gpr_hdr_fields
    Opcode -- APM_CMD_GRAPH_STOP

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_sub_graph_list_t  (Contains the number of
                                                    sub-graph IDs.) \n
      @indent{12pt} apm_sub_graph_id_t  (Array of sub-graph IDs.)

    @detdesc
    This command is issued against an APM module instance ID. The command
    consists of the #APM_PARAM_ID_SUB_GRAPH_LIST parameter ID, and it can be
    issued after the #APM_CMD_GRAPH_START command.
    @par
    If this command is issued to more than one sub-graph ID, it is executed in
    a sequential manner for each sub-graph.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Sub-graph IDs listed as part of this command must be configured using
    APM_CMD_GRAPH_OPEN or the APM returns an error code. @newpage
 */
#define APM_CMD_GRAPH_STOP                        0x01001003

/** @ingroup spf_apm_commands
    Closes one or more sub-graph IDs, data, and control links that were
    configured using #APM_CMD_GRAPH_OPEN.

    @gpr_hdr_fields
    Opcode -- APM_CMD_GRAPH_CLOSE

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_sub_graph_list_t  (Contains the number of
                                                    sub-graph IDs.) \n
      @indent{12pt} apm_sub_graph_id_t             (Array of sub-graph IDs.) \n
      @indent{12pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_module_conn_t  (Contains the number of data
                                                 connections.) \n
          @indent{24pt} (The following structure is repeated for each data link
                         being configured.) \n
          @indent{24pt} apm_module_conn_cfg_t \n
          @indent{24pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_module_ctrl_link_cfg_t  (Contains the number
                                                          of control links.) \n
          @indent{24pt} (The following structure is repeated for each control
                         link being configured.) \n
          @indent{24pt} apm_module_ctrl_link_cfg_t  (Contains the number of
                                                     control link intents.) \n
          @indent{24pt} (The following list of structures is repeated for each
                         control link property.) \n
          @indent{24pt} apm_prop_data_t \n
          @indent{24pt} apm_module_ctrl_link_prop_id_intent_list_t \n
          @indent{24pt} \<8-byte alignment, if any, but not mandatory at the
                          end>

    @detdesc
    This command is issued against an APM module instance ID. Any combination
    of the following parameter IDs can be sent as part of the command:
    - #APM_PARAM_ID_SUB_GRAPH_LIST
    - #APM_PARAM_ID_MODULE_CONN
    - #APM_PARAM_ID_MODULE_CTRL_LINK_CFG
    @par
    This command can be issued after the following commands:
    - #APM_CMD_GRAPH_OPEN
    - #APM_CMD_GRAPH_PREPARE
    - #APM_CMD_GRAPH_START
    - #APM_CMD_GRAPH_STOP
    - #APM_CMD_GRAPH_FLUSH
    - #APM_CMD_GRAPH_SUSPEND @newpage
    @par
    If this command is issued to more than one sub-graph ID, the command is
    executed in a sequential manner for each sub-graph.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Sub-graph IDs listed as part of this command must be configured using
    APM_CMD_GRAPH_OPEN or the APM returns an error code. @newpage
 */
#define APM_CMD_GRAPH_CLOSE                       0x01001004

/** @ingroup spf_apm_commands
    Used by the client to clean up all the allocated resources within the
    framework.

    @gpr_hdr_fields
    Opcode -- APM_CMD_CLOSE_ALL

    @detdesc
    Clean up involves the following:
    - Closes all opened sub-graphs, containers, and modules. Sub-graphs are
      closed regardless of their states.
    - For multi DSP framework, closes all opened sub-graphs/containers/modules
      on all satellite processors.
    - Unmaps all mapped memories for the master and satellite processors.
    - Unloads and de-registers all modules that were registered and loaded via
      AMDB APIs.
    @par
    One or more resources listed above will be de-allocated upon receiving this
    command. The client is not required to send the list of all those resources
    explicitly.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    None. @newpage
 */
#define APM_CMD_CLOSE_ALL                       0x01001013

/** @ingroup spf_apm_commands
    Flushes one or more sub-graph IDs that were configured

  @gpr_hdr_fields
    Opcode -- APM_CMD_GRAPH_FLUSH

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_sub_graph_list_t  (Contains the number of
                                                    sub-graph IDs.) \n
      @indent{12pt} apm_sub_graph_id_t  (Array of sub-graph IDs.)

    @detdesc
    This command is issued against an APM module instance ID. The command is
    sent with the #APM_PARAM_ID_SUB_GRAPH_LIST parameter ID.
    @par
    This command can be issued for the sub-graphs that were started at least
    once and are in the Pause state. The Pause state is configured using
    #APM_CMD_SET_CFG on a specific module within a sub-graph.
    @par
    If this command is issued to more than one sub-graph ID, it is executed in
    a sequential manner for each sub-graph.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Sub-graph IDs listed as part of this command must be configured using
    #APM_CMD_GRAPH_OPEN or the APM returns an error code. @newpage
 */
#define APM_CMD_GRAPH_FLUSH                       0x01001005

/** @ingroup spf_apm_commands
    Configures one or more parameter IDs for one or more module instances that
    are present in the graph.

    @gpr_hdr_fields
    Opcode -- APM_CMD_SET_CFG

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} (The following list of structures is repeated for each
                     module instance parameter ID.) \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any>

    @detdesc
    Module instances can include a APM module instance or any other data and
    control processing module.
    @par
    This command can be issued for all the module instance IDs that were
    configured at least once using #APM_CMD_GRAPH_OPEN.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Module IDs listed as part of this command must be configured using
    APM_CMD_GRAPH_OPEN or the APM returns an error code. @newpage
 */
#define APM_CMD_SET_CFG                           0x01001006

/** @ingroup spf_apm_commands
    Used for querying one or more configuration/calibration parameter IDs that
    correspond to any module instance ID. This command is currently not
    supported for any APM module instance parameter IDs.

    @gpr_hdr_fields
    Opcode -- APM_CMD_GET_CFG

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} (The following list of structures is repeated for each
                     module instance parameter ID.) \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} \<Parameter ID empty payload> \n
      @indent{12pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} \<Parameter ID empty payload> \n
      @indent{12pt} \<8-byte alignment, if any, but not mandatory at the end>

    @detdesc
    Module instances can any control processing modules that are present in the
    graph.
    @par
    This command can be issued for all the module instance IDs that were
    configured at least once using the #APM_CMD_GRAPH_OPEN.

    @return
    Response code -- APM_CMD_RSP_GET_CFG

    @dependencies
    Module IDs listed as part of this command must be configured using
    APM_CMD_GRAPH_OPEN or the APM returns an error code. @newpage
 */
#define APM_CMD_GET_CFG                           0x01001007

/** @ingroup spf_apm_commands
    Returns a response code as an acknowledgment to APM_CMD_GET_CFG.

    @gpr_hdr_fields
    Opcode -- APM_CMD_RSP_GET_CFG

    @msgpayload
    apm_cmd_rsp_get_cfg_t
    (The following list of structures is repeated for each module instance
    parameter ID.)
    @indent{12pt} apm_module_param_data_t \n
    @indent{12pt} \<Parameter ID payload> \n
    @indent{12pt} \<8-byte alignment, if any> \n
    @indent{12pt} apm_module_param_data_t \n
    @indent{12pt} \<Parameter ID payload> \n
    @indent{12pt} \<8-byte alignment, if any, but not mandatory at the end>

    @detdesc
    The client is responsible for specifying a sufficiently large payload size
    to accommodate the configuration/calibration data that corresponds to each
    parameter ID data being queried.

    @return
    None.

    @dependencies
    None. @newpage
 */
#define APM_CMD_RSP_GET_CFG                       0x02001000


/** @ingroup spf_apm_commands
    Payload for #APM_CMD_RSP_GET_CFG.

    Immediately following the status field is a variable length payload that
    consists of the parameter data for the module IDs that are queried as part
    of APM_CMD_GET_CFG. Each PID data uses the header structure,
    apm_module_param_data_t, followed by the actual parameter ID data.
*/

#define APM_CMD_DB_DEFAULT_SET_CFG 0x0100105E
/** @ingroup spf_apm_commands
    Payload for #APM_CMD_DB_DEFAULT_SET_CFG.

    In a multi-client scenario, different clients can override the default/tools
    calibration/configuration, causing undesired outputs. To avoid this,
    subgraphs should be marked as calibrated to prevent redundant recalibration.
    The APM_CMD_DB_DEFAULT_SET_CFG is used for new configurations in
    multi-client scenarios.
*/

#include "spf_begin_pack.h"
struct apm_cmd_rsp_get_cfg_t
{
   uint32_t    status;
   /**< Indicates the status of the APM_CMD_GET_CFG command.

        @valuesbul
        - AR_EOK -- All the parameter IDs being queried return success codes.
        - AR_EFAILED -- At least one parameter ID results in a failure error
          code.
        - For details on the status values, see GPR_IBASIC_RSP_RESULT in
          the AudioReach SPF Generic Packet Router API Reference
          (80-VN500-10). @tablebulletend */
}
#include "spf_end_pack.h"
;
typedef struct apm_cmd_rsp_get_cfg_t apm_cmd_rsp_get_cfg_t;


/** @ingroup spf_apm_commands
    Registers persistent calibration/configuration data that corresponds to one
    or more parameter IDs for one or more data/control processing modules.

    @gpr_hdr_fields
    Opcode -- APM_CMD_REGISTER_CFG

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} (The following list of structures is repeated for each
                     module instance parameter ID.) \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any, but not mandatory at the end>

    @detdesc
    This command is applicable for the module instances that support persistent
    calibration/configuration data. It can be issued for all module instance
    IDs that were configured at least once using #APM_CMD_GRAPH_OPEN.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Module IDs listed as part of this command must be configured using
    APM_CMD_GRAPH_OPEN or the APM returns an error code. @newpage
 */
#define APM_CMD_REGISTER_CFG                      0x01001008

/** @ingroup spf_apm_commands
    De-registers persistent calibration/configuration data corresponding from
    one or more parameter IDs for one or more data/control processing modules.

    @gpr_hdr_fields
    Opcode -- APM_CMD_DEREGISTER_CFG

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} (The following list of structures is repeated for each
                     module instance parameter ID.) \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any, but not mandatory at the end>

    @detdesc
    This command is applicable for the module instances that support persistent
    calibration/configuration data. It can be issued after
    #APM_CMD_REGISTER_CFG for all module instance IDs that were configured at
    least once using #APM_CMD_GRAPH_OPEN.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Module IDs listed as part of this command must be configured using
    APM_CMD_GRAPH_OPEN or the APM returns an error code.
    @par
    This command can be sent for the parameter IDs only after a successful
    register command is issued. @newpage
 */
#define APM_CMD_DEREGISTER_CFG                    0x01001009

/** @ingroup spf_apm_commands
    Registers shared persistent calibration/configuration data that corresponds
    to one parameter ID for a data/control processing module.

    @gpr_hdr_fields
    Opcode -- APM_CMD_REGISTER_SHARED_CFG

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} (The following list of structures is repeated for each
                     module instance parameter ID.) \n
      @indent{12pt} apm_module_param_shared_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_shared_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any, but not mandatory at the end>

    @detdesc
    This command is applicable for module instances that support shared
    persistent calibration/configuration data. It can be issued for all the
    module instance IDs that were configured at least once using
    #APM_CMD_GRAPH_OPEN, whose payload structure (apm_cmd_header_t) includes
    apm_module_param_shared_data_t as the out-of-band payload.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Module IDs listed as part of this command must be configured using
    APM_CMD_GRAPH_OPEN or the APM returns an error code. @newpage
 */
#define APM_CMD_REGISTER_SHARED_CFG               0x0100100A

/** @ingroup spf_apm_commands
    Registers shared persistent calibration/configuration data that corresponds
    to one parameter ID for a data/control processing module.

    @gpr_hdr_fields
    Opcode -- APM_CMD_DEREGISTER_SHARED_CFG

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} (The following list of structures is repeated for each
                     module instance parameter ID.) \n
      @indent{12pt} apm_module_param_shared_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_param_shared_data_t \n
      @indent{12pt} \<Parameter ID payload> \n
      @indent{12pt} \<8-byte alignment, if any, but not mandatory at the end>

    @detdesc
    This command is applicable for the module instances that support shared
    persistent calibration/configuration data. It can be issued after
    #APM_CMD_REGISTER_SHARED_CFG.
    @par
    Also, it can be issued for all module instance IDs that were configured at
    least once using #APM_CMD_GRAPH_OPEN, whose payload structure
    (apm_cmd_header_t) includes apm_module_param_shared_data_t as the
    out-of-band payload.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Module IDs listed as part of this command must be configured using
    APM_CMD_GRAPH_OPEN or the APM returns an error code.
    @par
    The de-register command must be sent for the parameter IDs only after a
    successful register command is issued. @newpage
 */
#define APM_CMD_DEREGISTER_SHARED_CFG             0x0100100B

/** @ingroup spf_apm_commands
    Registers or de-registers one or more event IDs for one or more module
    instances present in the graph.

    @gpr_hdr_fields
    Opcode -- APM_CMD_REGISTER_MODULE_EVENTS

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} apm_module_register_events_t \n
      @indent{12pt} <Event configuration payload, if any> \n
      @indent{12pt} \<8-byte alignment, if any> \n
      @indent{12pt} apm_module_register_events_t \n
      @indent{12pt} <Event configuration payload, if any> \n
      @indent{12pt} \<8-byte alignment, if any, but not mandatory at the end>
    @par
    When there are multiple events, different apm_module_register_events_t
    structures must be 8 byte-aligned.

    @detdesc
    This command can be only sent to modules present in an opened graph. It
    cannot be sent to #APM_MODULE_INSTANCE_ID.
    @par
    This command can be issued for all the module instance IDs that were
    configured at least once using #APM_CMD_GRAPH_OPEN.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Module IDs listed as part of this command must be configured using
    APM_CMD_GRAPH_OPEN or the APM returns an error code. @newpage
 */
#define APM_CMD_REGISTER_MODULE_EVENTS            0x0100100E

/** @ingroup spf_apm_commands
    Queries for the SPF status. If the command can reach the APM, the SPF is
    ready.

    @gpr_hdr_fields
    Opcode -- APM_CMD_GET_SPF_STATE

    @return
    Response code -- APM_CMD_RSP_GET_SPF_STATE

    @dependencies
    None.
 */
#define APM_CMD_GET_SPF_STATE                   0x01001021


/** @ingroup spf_apm_commands
    Definition for the Not Ready state. */
#define APM_SPF_STATE_NOT_READY                 0

/** @ingroup spf_apm_commands
    Definition for the Ready state. */
#define APM_SPF_STATE_READY                     1


/** @ingroup spf_apm_commands
    Returns a response code as an acknowledgment for #APM_CMD_GET_SPF_STATE.

    @gpr_hdr_fields
    Opcode -- APM_CMD_RSP_GET_SPF_STATE

    @msgpayload
    apm_cmd_rsp_get_spf_status_t

    @return
    None.

    @dependencies
    None. @newpage
 */
#define APM_CMD_RSP_GET_SPF_STATE                0x02001007

/** @ingroup spf_apm_commands
    Payload for #APM_CMD_RSP_GET_SPF_STATE.
*/
#include "spf_begin_pack.h"
struct apm_cmd_rsp_get_spf_status_t
{
   uint32_t    status;
   /**< Indicates the status of the APM_CMD_GET_SPF_STATE command.

        @valuesbul
        - APM_SPF_STATE_NOT_READY
        - APM_SPF_STATE_READY @tablebulletend @newpagetable */
}
#include "spf_end_pack.h"
;
typedef struct apm_cmd_rsp_get_spf_status_t apm_cmd_rsp_get_spf_status_t;


/** @ingroup spf_apm_commands
    Event raised by a module to the client if the event is registered by the
    client.

    @gpr_hdr_fields
    Opcode -- APM_EVENT_MODULE_TO_CLIENT

    @msgpayload
    apm_module_event_t

    @detdesc
    This event can be raised by all module instances that were configured at
    least once using #APM_CMD_GRAPH_OPEN.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    None. @newpage
 */
#define APM_EVENT_MODULE_TO_CLIENT                0x03001000


/** @ingroup spf_apm_commands
    Parameter ID used to get the path delay from the APM.

    The graphs that correspond to the modules mentioned in the payload must be
    opened using #APM_CMD_GRAPH_OPEN.

    This parameter is used with #APM_CMD_GET_CFG, which typically does not
    carry a parameter-specific payload. However, the APM_CMD_GET_CFG command
    contains a payload to specify the path.

    The path delay is @ge 0. For the delay to be accurate:
    - Calibration must already be done for the path.
    - The media format must have been propagated.
    - Typically, all sub-graphs must be in the Prepare state.

    The maximum possible delay from source to destination is returned.
    In some cases, the delay might not be exactly the same as the returned
    delay, for example, in FTRT paths, or when processing time is smaller than
    expected. Also, the delay can be obtained only for PCM or packetized data
    paths. If raw compressed data is involved, the delay cannot be known
    correctly for the portion that is compressed. However, a query will not
    fail because the delay of the rest of the path (if any) can be determined.

    Under split-merge scenarios, if there are multiple paths from the source to
    the destination, the first path is chosen arbitrarily. If the path is not
    complete, an error is returned.

    The client can leave the port IDs as zeros to be autofilled by the SPF.
    When the port IDs are zero, the delay of the source and destination modules
    might not be included, as explained below:
     - If the source has only outputs and the maximum number of input ports =
       0, the source's delay is included. Because the source has no input
       ports, this is the only way to account for its delay.
     - If the destination has only input ports and the maximum number of output
       ports = 0, the destination's delay is included. Because the destination
       has no output ports, this is the only way to account for its delay.
     - For all other cases, delay is from the output port of the source to the
       output port of the destination.

    This method of defining helps with concatenation. For example, A -> B -> C
    can be obtained as A to B + B to C, and a delay of B is not considered
    twice. When the destination has multiple outputs, an output is selected
    arbitrarily.

    @msgpayload
    apm_param_id_path_delay_t \n
    @indent{12pt} apm_path_defn_for_delay_t
*/
#define APM_PARAM_ID_PATH_DELAY                 0x08001119

/** @ingroup spf_apm_commands
    Payload for #APM_PARAM_ID_PATH_DELAY. This structure defines the start and
    end of the path for which the delay is queried.
 */
#include "spf_begin_pack.h"
struct apm_path_defn_for_delay_t
{
   uint32_t    src_module_instance_id;
   /**< Identifier for the instance for the source module.

        This field is an input parameter. */

   uint32_t    src_port_id;
   /**< Unique identifier for the source port. If the port ID is not known, set
        this field to 0.

        Knowing the port ID helps reduce the complexity of a graph search.
        It also helps if specific path delays are required in split-merge
        scenarios. For example, ports 1 and 2 of module A end up in ports 1 and
        2 of endpoint B. Because there are two paths from A to B, knowing the
        port ID helps.

        If the port ID is zero at the time of the query, it is populated with
        the correct port ID when the APM responds.

        This field is an input and output parameter. */

   uint32_t    dst_module_instance_id;
   /**< Identifier for the instance of the destination module.

        This field is an input parameter. */

   uint32_t    dst_port_id;
   /**< Identifier for the destination port. If the port ID is not known, set
        this field to 0.

        If the port ID is zero at the time of the query, it is populated with
        the correct port ID when the APM responds.

        This field is an input and output parameter. */

   uint32_t    delay_us;
   /**< Delay in microseconds. This field is an output parameter. */
}
#include "spf_end_pack.h"
;
typedef struct apm_path_defn_for_delay_t apm_path_defn_for_delay_t;


/** @ingroup spf_apm_commands
    Payload for #APM_PARAM_ID_PATH_DELAY.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct apm_param_id_path_delay_t
{
   uint32_t                   num_paths;
   /**< Number of paths to be queried. */

   apm_path_defn_for_delay_t  paths[0];
   /**< Definition of each path. @newpagetable */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct apm_param_id_path_delay_t apm_param_id_path_delay_t;


/** @ingroup spf_apm_commands
    Suspends one or more sub-graph IDs that were configured using
    #APM_CMD_GRAPH_START.

    @gpr_hdr_fields
    Opcode -- APM_CMD_GRAPH_SUSPEND

    @msgpayload
    apm_cmd_header_t \n
      @indent{12pt} apm_module_param_data_t \n
      @indent{12pt} apm_param_id_sub_graph_list_t  (Contains the number of
                                                    sub-graph IDs.) \n
      @indent{12pt} apm_sub_graph_id_t  (Array of sub-graph IDs.)

    @detdesc
    This command is issued against an APM module instance ID. The command
    consists of the #APM_PARAM_ID_SUB_GRAPH_LIST parameter ID, and it can be
    issued for the sub-graphs that were started at least once.
    @par
    If this command is issued to more than one sub-graph ID, the command is
    executed in a sequential manner for each sub-graph.

    @return
    GPR_IBASIC_RSP_RESULT (see @xhyperref{80VN50010,80-VN500-10}).

    @dependencies
    Sub-graph IDs listed as part of this command must be configured using
    #APM_CMD_GRAPH_OPEN and started using #APM_CMD_GRAPH_START.
    @par
    For sub-graphs in the Stopped or Prepared state, this suspend command is
    not applied.
 */
#define APM_CMD_GRAPH_SUSPEND                       0x01001043


/** @ingroup spf_apm_commands
    Used with APM_CMD_SET_CFG to set the satellite processor domain
    information to the master APM.

    @msgpayload
    apm_param_id_satellite_pd_info_t @newpage
*/
#define APM_PARAM_ID_SATELLITE_PD_INFO                 0x08001251

/** @ingroup spf_apm_commands
    Payload for #APM_PARAM_ID_SATELLITE_PD_INFO.
*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct apm_param_id_satellite_pd_info_t
{
   uint32_t num_proc_domain_ids;
   /**< Number of satellite processor domain IDs. */

   uint32_t proc_domain_id_list[0];
   /**< List of satellite processor domain IDs.

        @values
        - APM_PROC_DOMAIN_ID_MDSP
        - APM_PROC_DOMAIN_ID_ADSP
        - APM_PROC_DOMAIN_ID_ADSP_1
        - APM_PROC_DOMAIN_ID_ADSP_2
        - APM_PROC_DOMAIN_ID_APPS
        - APM_PROC_DOMAIN_ID_SDSP
        - APM_PROC_DOMAIN_ID_CDSP
        - APM_PROC_DOMAIN_ID_GDSP_0
        - APM_PROC_DOMAIN_ID_GDSP_1
        - APM_PROC_DOMAIN_ID_APPS_2
        - Number of elements (num_proc_domain_ids) @tablebulletend*/
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct apm_param_id_satellite_pd_info_t apm_param_id_satellite_pd_info_t;


#ifdef __cplusplus
}
#endif /*__cplusplus*/


#endif /* _APM_API_H_ */
