#ifndef _CAPI_FWK_EXTNS_IPC_PORT_HANDLER_H_
#define _CAPI_FWK_EXTNS_IPC_PORT_HANDLER_H_

/**
 *   \file capi_fwk_extns_ipc_port_handler.h
 *   \brief
 *        This file contains External Input/Output IPC communication handler APIs.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include "capi_types.h"
#include "gpr_api_inline.h"
#include "ar_error_codes.h"

/** @addtogroup capi_fwk_extns_ipc_port_handler
@{ */

/*==============================================================================
     Constants
==============================================================================*/

/** Unique identifier of the framework extension for the IPC port related handlers.

    This extension supports the following property and parameter IDs:
*/
#define FWK_EXTN_IPC_PORT_HANDLER      0x0A00105D

/*==============================================================================
   Constants
==============================================================================*/
/** ID of the custom property used to share graph link info corresponding to a given
    external input/outut port.

    This property must be set at the time of module creation or graph open. If this info
    is not received by PREPARE/START module must return an error.

    If this info is not set module may return error even in the process context.

    Module uses the destination module ID and proc ID information to initiate IPC.
    Module uses the destination info to send both control and data cmds. This is a new kind
    of mechaism where IPC modules are using the data link information to communicate
    both ctrl and data cmds.

    Though module is using data link information to send the ctrl info to the peer, on reaching
    desitnation the ctrl packets are actually pushed to container's control queue instead of
    data queue. Only data cmds are pushed to external port data queue. Based on module data trigger
    requirements the data cmds are popped and passed to the module for processing.

    @msgpayload{fwk_extn_event_ipc_data_link_info_t}
    @table{weak__fwk__extn__event__ipc__data__link__info__t}
*/
#define FWK_EXTN_EVENT_ID_IPC_DATA_LINK_INFO    0x0A00105E

/*==============================================================================
   Type definitions
==============================================================================*/

/** unique port handle set by the fwk for each port ID along with link info. */

typedef struct fwk_extn_event_ipc_data_link_info_t fwk_extn_event_ipc_data_link_info_t;

/** @weakgroup fwk_extn_prop_ipc_link_info_t
@{ */
struct fwk_extn_event_ipc_data_link_info_t
{
   bool_t is_ext_input;
   /**< Specifies whether the ipc link is connected to current module's external input/output.

        @valuesbul
        - 0 -- External Output
        - 1 -- External Input  @tablebulletend */

   uint32_t self_port_id;
   /**< Port ID of the external input/output port. */

   uint32_t self_module_iid;
   /**< Self module instance ID. */

   uint32_t self_proc_domain_id;
   /**< Self processor domain ID */

   uint32_t peer_port_id;
   /**< Port ID of the destination external input/output port. If current input is external input, destination is
    * considered external output. This is optional param, if the destination is a port less client this may not be set.
    */

   uint32_t peer_module_iid;
   /**< Self module instance ID, this is the unique identifier or GPR destination address. */

   uint32_t peer_proc_domain_id;
   /**< Self processor domain ID this it the proc domain of the destination module instance. */
};
/** @} */ /* end_weakgroup fwk_extn_prop_ipc_link_info_t */

/*==============================================================================
   Constants
==============================================================================*/

/**
    Like other modules, IPC modules rely on the container to register with GPR, as they lack their own thread context to
handle GPR packets. Therefore, GPR packets are received by the container, which is responsible for routing them to
either the control command queue or the external port data queue—depending on the command type.

To enable this, the container must distinguish whether an IPC GPR packet is a control or data command. To acheive this
behavior, we are introducing two new container command opcodes:

 The actual memory layout of the GPR packet will be,

                   < GPR packet: gpr_packet_t > // opcode => primary opcode
                   < msg payload memory >

    @msgpayload{fwk_extn_prop_ipc_msg_callback_info_t}
    @table{weak__fwk__extn__prop__ext__io__callback__hanlders__t}
*/

typedef struct fwk_extn_ipc_msg_header_t
{
   uint32_t msg_opcode;
   /** this opcode is internal to the IPC modules. This msg opcode and payload formats are abstracted from the container
    * fwk code. */

   uint32_t msg_payload_size;
   /** Size of the message payload size following this header. */

}fwk_extn_ipc_msg_header_t;

/*==============================================================================
   Constants
==============================================================================*/
/** ID of the parameter used to get the callback handlers related to the ipc msg handling
    from the module.The callback handlers are same for all the external ports for a given module.

   The callback must be island safe if the module is supported in island, and module is
   responsible to exit island from the callback if necessary.

   This parameter is queries as CAPI_SET_PARAM with a valid capi_port_info_t. Module has to return
   the callback handlers corresponding to the specific port thats being queried.

    @msgpayload{fwk_extn_prop_ipc_msg_callback_info_t}
    @table{weak__fwk__extn__prop__ext__io__callback__hanlders__t}
    */
#define FWK_EXTN_PROPERTY_ID_IPC_MSG_CALLBACK_INFO 0x0A003BAD

/*==============================================================================
   Type definitions
==============================================================================*/

/* Structure for above property */
typedef struct fwk_extn_prop_ipc_msg_callback_info_t fwk_extn_prop_ipc_msg_callback_info_t;

/** handler for ctrl command or ctrl command response opcodes */
typedef ar_result_t (*fwk_extn_ipc_port_ctrl_msg_handler_fptr_t)(void         *callback_ptr,
                                                                 uint32_t      is_ext_input,
                                                                 uint32_t      port_index,
                                                                 gpr_packet_t *pkt_ptr);

/** Flags sent by the container along with the data msg packet. */
typedef enum fwk_extn_ipc_port_data_msg_flags_t
{
   FWK_EXT_IPC_PORT_FLAG_UNSET = 0,

   FWK_EXT_IPC_PORT_FLAG_FLUSH = 1 << 0,

   FWK_EXT_IPC_PORT_FLAG_DESTROY_BUFS = 1 << 1,

   FWK_EXT_IPC_PORT_FLAG_UNDERRUN   = 1 << 2,

   FWK_EXT_IPC_PORT_FLAG_OVERRUN    = 1 << 3,

} fwk_extn_ipc_port_data_msg_flags_t;

/** handler for data cmd or data cmd response opcodes */
typedef ar_result_t (*fwk_extn_ipc_port_data_msg_handler_fptr_t)(
   void         *callback_ptr,
   uint32_t      is_ext_input,
   uint32_t      port_index,
   gpr_packet_t *pkt_ptr,
   uint32_t      ipc_data_msg_flag_mask /* refer to: fwk_extn_ipc_port_data_msg_flags_t*/);

typedef enum fwk_extn_ipc_port_trigger_t
{
   /* indicates module is not ready to process and needs a buffer, hence container needs to wait for on that port and do
      not call module process yet. */
   FWK_EXTN_IPC_PORT_BUFFER_NEEDED = 0,

   /* indicates module is ready to process and does not need a buffer, hence container skip waiting on that port and can
      call module process. */
   FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED = 1 << 0,

   /* indicates module is not ready to process and needs a buffer optionally, hence container optionaly waits on that
      port and do not call process yet. */
   FWK_EXTN_IPC_PORT_BUFFER_NEEDED_OPTIONALLY = 1 << 1,

   /* indicates module is ready to process and needs a buffer optionally, hence container can optionaly waits on that
      port and also call process. */
   FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED_OPTIONALLY = 1 << 2,
} fwk_extn_ipc_port_trigger_t;

/** @weakgroup fwk_extn_prop_ipc_msg_callback_info_t
@{ */
struct fwk_extn_prop_ipc_msg_callback_info_t
{
   fwk_extn_ipc_port_trigger_t *ext_port_trigger_shared_ptr;
   /**< This pointer allocated by the module and shared with the container to inform the trigger for the given
    * ipc/external port.

     supported values:
           0 - FWK_EXTN_IPC_PORT_BUFFER_NEEDED
                  - container considers the module needs data, hence adds the external port data queue to the wait mask
                  - IPC TX module sets this trigger whenever it ends up not having an external output buffer, which
                    usually is set immediately after sending external IPC output buffer.
                  - IPC RX sets this whenever it doesnt have sufficient data to generate output.

           1 - FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED
                  - container considers the module has data and trigger is considered satisfied. And doesnt wait on the
                    data queue.
                  - IPC TX module sets this trigger if it has an empty external output buffer to process.
                  - IPC RX module set this trigger if its has required amount of data at its external input.

            2 - FWK_EXTN_IPC_PORT_BUFFER_NEEDED_OPTIONALLY
                  - container considers the modules trigger is optional and waits on the data queue, but the trigger
                    will not trigger the container processing.
                  - IPC RX module sets this when the IPC port goes to at gap i.e if a EOS is propagated. This should
                    be the default trigger IPC RX port at start, once it gets a data buffer it starts setting '0' or '1'
                    As soon as the port gets started it should it should be marked optional.

            4 - FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED_OPTIONALLY
                  - it means that module doesn't need a buffer for processing and it has sufficient input to process.
                    And allows container to wait optionally on the port. This trigger is possible only in the case where
                    Flushing EOS or DFG is received and stuck inside the module. Module is ready to process in that case,
                    but at the same time container can start listenting on the input optionally. So if new input data is
                    received before modules gets chance it process, it can drop the flushing EOS buffered internally.
      >*/

   fwk_extn_ipc_port_ctrl_msg_handler_fptr_t ctrl_msg_handler; // SPF_CMD_PRIVATE_CTRL_MSG
   /**< Module's ctrl cmds or ctrl cmd rsp opcode handlers. After handling the ipc msgs, module is expected to call
    * either free() or end() on the packets to return response for the . If the msg type is rsp cmd, module can only
    * call free() if it called end() error will be reported. */

   fwk_extn_ipc_port_data_msg_handler_fptr_t data_msg_handler; // SPF_CMD_PRIVATE_DATA_MSG
   /**< Module's data cmds or data cmd rsp opcode handlers. After handling the ipc msgs, module is expected to call
    * either free() or end() on the packets to return response for the . If the msg type is rsp cmd, module can only
    * call free() if it called end() error will be reported. */

   void * callback_handle_ptr;
   /** This is the capi modules capi callback handler that container sends as an argument to the ipc port data/ctrl msg
    * handler callback functions. */
};
/** @} */ /* end_weakgroup fwk_extn_prop_ipc_msg_callback_info_t */

/** ID of the parameter used configure the number of IPC buffers and buffer size
    based on upstream and downstream container frame size property.

    @msgpayload{fwk_extn_param_id_ipc_buffer_info_t}
    @table{weak__fwk__extn__param__id__ipc__buffer__info__t}
 */
#define FWK_EXTN_PARAM_ID_IPC_BUFFER_INFO                   0x0A00100F

typedef struct fwk_extn_param_id_ipc_buffer_info_t fwk_extn_param_id_ipc_buffer_info_t;

/** @weakgroup fwk_extn_param_id_ipc_buffer_info_t
@{ */
struct fwk_extn_param_id_ipc_buffer_info_t
{
   uint32_t num_reg_bufs;
   /**< number of ipc buffers that IPC module needs to create.
        This depends upon the media format and upstream and downstream
        frame length property info. */

   uint32_t num_reg_prebufs;
   /**< number of ipc prebuffers required to be pushed by TX to account for upstream
        processing jitter in realtime path.*/
};
/** @} */ /* end_weakgroup weak_fwk_extn_param_id_ipc_buffer_info_t */

/** Opcode used by the container to port property related information to peer container in a remote
   proc domain through GPR */

/** SPF msg opcode used to push the IPC modules related opcodes into the container command queue. This msg opcode is
 used to push all the GPR opcodes/packets intended for IPC ports into a single container workloop handler. The
 subsequent opcodes listed below indicate the actual GPR opcode and indicate purpose of each of the GPR packets. */
#define SPF_MSG_CMD_IPC_PORT_FWK_EXTN_GPR                       0x01001064

/**  This GPR opcode is used between IPC out and input ports to exchange port level info like state, rt/rt related info
 */
#define SPF_IPC_FWK_EXTN_GPR_CMD_PEER_PORT_PROPERTY_UPDATE      0x01001065

/* Upstream sends this opcode to downstream to indicate upstream IPC output port is stopped. */
#define SPF_IPC_FWK_EXTN_GPR_CMD_UPSTREAM_IPC_TX_STOPPED_ACK    0x01001066

/** downstream informs upstream of its frame length through this opcode for IPC modules*/
#define SPF_IPC_FWK_EXTN_GPR_CMD_INFORM_ICB_INFO                0x01001067

/** @} */ /* end_addtogroup capi_fwk_extns_ipc_port_handler */

#endif /* _CAPI_FWK_EXTNS_IPC_PORT_HANDLER_H_ */
