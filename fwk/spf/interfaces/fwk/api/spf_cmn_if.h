#ifndef _SPF_MSG_H_
#define _SPF_MSG_H_

/**
 * \file spf_msg.h
 * \brief
 *    This file defines messages structures, IDs and payloads for spf
 *    messages.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/** @weakgroup weakf_spf_msg_overview
  The spf message structure is used throughout the spf framework for
  internal communication. A spf message contains an opcode and a pointer
  to a payload.

  All non-APR payloads begin with a common header structure, which includes
  fields for a return queue, response handle, destination handle, token,
  response result, payload size, and the start of the payload (to ensure the
  payload starts at an 8-byte aligned boundary). Many opcodes will require
  additional, specialized header fields: these can be defined in another
  header structure which can be placed in the common header's payload field.

  The opcodes are globally defined. The recommended way to process a spf
  message is to use a function table with each entry being the message handler
  corresponding to the opcode. To reduce the function table size, the number
  of opcodes is tightly limited.

  Handles are defined to provide an association between a port's data queue and
  its container's command queue. Each handle contains a pointer to a queue and
  a pointer to a command handle. The command handle contains a command
  queue and the a thread ID. This two-layered approach is used to avoid
  duplicate (and possibly outdated) copies of container information within the
  container's port handles.

  A handle may have a NULL command handle pointer if the handle does not
  correspond to a container. Similarly, a container may store its command
  queue in its own handle. Since this handle would not be tied to a port, the
  queue pointer can be NULL in this case.

  Internal messages can be sent through the command path (command queue) or
  data path (port's data queue). Message utility functions are provided to
  accomplish either using handles. Messages that should be sent through the
  data path include exchanges of data buffers as well as any notifications
  that must be synchronized with the data itself, including media format and
  eos messages. Other messages should be sent through the command path.

  As much as possible, command path messages should be sent to containers from
  APM. To maintain centralization, container to container messaging is
  discouraged.

  Many messages require a response in the form of an acknowledgement, an
  indication of success or failure, or possibly to provide more specialized
  information such as query results. The common behavior for a response is to
  push the same message header and payload to the response handle, modifying the
  response result as necessary. The sender will then receive the response,
  validate the token value to match the response to the sent message, and check
  the response result.

  If more customized responses are needed, other approaches are possible and
  can be handled with opcode-specific behavior.
*/

/*-------------------------------------------------------------------------
Include Files
-------------------------------------------------------------------------*/
#include "posal.h"
#include "ar_guids.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/*-------------------------------------------------------------------------
Preprocessor Definitions and Constants
-------------------------------------------------------------------------*/
/*
  Macro that determines the actual required message size (payload +
  spf header) if req_buf_size bytes are requested for the payload.
 */
#define GET_SPF_MSG_REQ_SIZE(req_buf_size) ((req_buf_size) + sizeof(spf_msg_header_t) - sizeof(uint64_t))

/*-------------------------------------------------------------------------
Type Declarations
-------------------------------------------------------------------------*/

typedef union spf_msg_token_t spf_msg_token_t;

union spf_msg_token_t
{
   uint32_t token_data;
   /**< Token to be given in the acknowledgment. The token should be unique
         per sent message and is used to match the response to the sent
         message. */

   void *token_ptr;
   /**< Token to be given in the acknowledgment. The token should be unique
         per sent message and is used to match the response to the sent
         message. */
};

/** Contains the command queue and thread id. Multiple port handles and
    response handles can reference the same command handle.
 */
typedef struct spf_cmd_handle_t
{
   posal_thread_t thread_id;
   posal_queue_t *cmd_q_ptr;
   posal_queue_t *sys_cmd_q_ptr;
} spf_cmd_handle_t;

/** A unified handle to push to either data path or control path. Associates a
    data queue or response queue with a command queue.
 */
typedef struct spf_handle_t
{
   spf_cmd_handle_t *cmd_handle_ptr;
   /**< Command handle, for pushing to the control path.*/

   posal_queue_t *q_ptr;
   /**< The queue pointer. */
} spf_handle_t;

/** @addtogroup spf_msg_datatypes
@{ */

/** Message structure for all spf messages.
 */
typedef struct spf_msg_t
{
   void *payload_ptr;
   /**< Payload buffer pointer. This field starts at the
        No alignment requirements other than native for architecture. */

   union
   {
      uint32_t msg_opcode;
      /**< Opcode to help distinguish the payload. */
      void *unused_ptr;
      /**< Pointer added to keep structure consistent with posal_queue_element_t and
       * posal_bufmgr_node_t structures in 64 bit systems. This field should never be used. */
   };
} spf_msg_t;

/** Common header structure for the payload of spf command messages.
 */
typedef struct spf_msg_header_t
{
   posal_queue_t *return_q_ptr;
   /**< Queue to which this message header must be returned. */

   spf_handle_t *rsp_handle_ptr;
   /**< Handle to which the acknowledgment should be sent. NULL indicates that
        no response is required. */

   spf_handle_t *dst_handle_ptr;
   /**< Handle of the destination to which this message is sent. For messages
        sent to specific ports, this can be a port handle. If no recipient
        handle is needed, this can be set to NULL. */

   spf_msg_token_t token;
   /**< Token to be given in the acknowledgment. The token should be unique
        per sent message and is used to match the response to the sent
        message. */

   uint32_t rsp_result;
   /**< Response result that the receiver sends back to the sender. Often
        used to indicate success or failure of command processing. */

   uint32_t payload_size;
   /**< Size of payload in bytes. */

   uint64_t payload_start[1];
   /**< Placeholder for the opcode specific payload. Note that this payload
        will likely begin with an opcode specific header. */

} spf_msg_header_t;

/** GPR command */
#define SPF_MSG_CMD_GPR 0x0100102A

/** Micro Socket command */
#define SPF_MSG_CMD_UQSI 0x01001048

/** @} */ /* end_spf_msg_datatypes */

/** Heap ID mask indicating that the heap ID belongs to a container */
#define CONTAINER_HEAP_ID_MASK AR_NON_GUID(0xFFFFF000)

/** Heap ID mask indicating that the heap ID belongs to a CAPI Module */
#define CAPI_MODULE_INSTANCES_HEAP_ID_MASK AR_NON_GUID(0x00000FC0)

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef _SPF_MSG_H_
