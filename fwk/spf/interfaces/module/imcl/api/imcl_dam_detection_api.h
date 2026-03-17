#ifndef IMCL_DAM_DETECTION_API_H
#define IMCL_DAM_DETECTION_API_H

/**
  @file imcl_dam_detection_api.h

  @brief defines the Intent IDs for communication over Inter-Module Control
  Links (IMCL) betweeen Detection Modules (like Voice Wakeup) and DAM module
*/
/*==========================================================================
 * Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * =========================================================================*/
#include "imcl_fwk_intent_api.h"

#ifdef INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*==============================================================================
   Constants
==============================================================================*/
#define MIN_INCOMING_IMCL_PARAM_SIZE_SVA_DAM                                                                           \
   (sizeof(vw_imcl_header_t) + sizeof(intf_extn_param_id_imcl_incoming_data_t))

/**< Header - Any IMCL message going out of / coming in to the
      Voice Wakeup Module (Voice Activation) will have the
      following header followed by the actual payload.
      The peers have to parse the header accordingly*/
typedef struct vw_imcl_header_t
{
   // specific purpose understandable to the IMCL peers only
   uint32_t opcode;

   // Size (in bytes) for the payload specific to the intent.
   uint32_t actual_data_len;
} vw_imcl_header_t;

/*==============================================================================
  Intent ID -  INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL
==============================================================================*/
/**< Intent defines the payload structure of the IMCL message.
Voice Activation and Dam modules supports the following functionalities –
1. Channel resizing – This as an input intent ID exposed by Dam module, which allows resizing the channel buffers based
on Voice Activation module’s buffering requirement.
2. Output port data flow control - This as an input intent ID exposed by Dam module. This intent is used to open/close
the Dam output ports gates by Voice Activation module.
3. Best channel output – This intent allows detection engines to send the best channel indices to Dam module. Upon
receiving the best channel indices, Dam module outputs only the best channels from the given output port.
4. FTRT data availably info - This as an output intent ID exposed by Dam module. This intent is used by Dam module to
send the unread data length [FTRT data] length present in the channel buffers to the Voice Activation module. */

/* Parameter used by the  IMC client[Detection engines] to resize the buffers associated
 *  with an output port stream.*/
#define PARAM_ID_AUDIO_DAM_RESIZE 0x0800105C

/* Register as a resize client */
#define AUDIO_DAM_BUFFER_REGISTER_RESIZE 1

/* De-Register an existing resize client */
#define AUDIO_DAM_BUFFER_DEREGISTER_RESIZE 0

/*==============================================================================
   Type Definitions
==============================================================================*/

/* Structure definition for Parameter */
typedef struct param_id_audio_dam_buffer_resize_t param_id_audio_dam_buffer_resize_t;

/** @h2xmlp_parameter   {"PARAM_ID_AUDIO_DAM_RESIZE",
                         PARAM_ID_AUDIO_DAM_RESIZE}
    @h2xmlp_description {Resizes a dam circular buffers based on the requirements of a
                         client module[detection engines].}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct param_id_audio_dam_buffer_resize_t
{
   uint32_t resize_in_us;
   /**< @h2xmle_description {Increase/decrease the circular buffer by given length in micro seconds.}
        @h2xmle_range       {0..4294967295}
        @h2xmle_default     {0} */
}
#include "spf_end_pack.h"
;

/*parmeter used by the IMC client to trigger island entry exit based on the client processing time.
client will send this param and enable DAM to control the island entry on the IMCL messages*/
#define PARAM_ID_AUDIO_DAM_ALLOW_DCM_ISLAND_ENTRY 0x08001AE0

/* Structure definition for Parameter */
typedef struct param_id_audio_dam_allow_dcm_island_entry_t param_id_audio_dam_allow_dcm_island_entry_t;

/** @h2xmlp_parameter   {"PARAM_ID_AUDIO_DAM_ALLOW_DCM_ISLAND_ENTRY",
                         PARAM_ID_AUDIO_DAM_ALLOW_DCM_ISLAND_ENTRY}
    @h2xmlp_description {Enables DAM module to be able to raise votes for island entry.}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct param_id_audio_dam_allow_dcm_island_entry_t
{
   uint32_t process_done;
   /**< @h2xmle_description {set the flag to indicate if island entry should be triggered.}
        @h2xmle_range       {0..1}
        @h2xmle_default     {0} */
}
#include "spf_end_pack.h"
;

/*==============================================================================
   Constants
==============================================================================*/

/* This param is sent to IMC client[Detection engines] by Dam buffer as a response to Read adjust.
   Un read data length allows the HLOS client to create the first read buffer to read the history
   data in FTRT mode.*/
#define PARAM_ID_AUDIO_DAM_UNREAD_DATA_LENGTH 0x0800105D

/*==============================================================================
   Type Definitions
==============================================================================*/

typedef struct param_id_audio_dam_unread_bytes_t param_id_audio_dam_unread_bytes_t;

/** @h2xmlp_parameter    {"PARAM_ID_AUDIO_DAM_UNREAD_DATA_LENGTH",
                           PARAM_ID_AUDIO_DAM_UNREAD_DATA_LENGTH}
    @h2xmlp_description  {get unread length which are buffered in the history buffer.}
   @h2xmlp_toolPolicy    {Calibration} */

#include "spf_begin_pack.h"
struct param_id_audio_dam_unread_bytes_t
{
   uint32_t unread_in_us;
   /**< @h2xmle_description {get unread data length(in micro secs)from circular buffer.}
         @h2xmle_range      {0..4294967295}
         @h2xmle_default    {0} */
}
#include "spf_end_pack.h"
;

/*==============================================================================
   Constants
==============================================================================*/

/* This is a Set param ID to control the data flow at the output port of Dam module.
 * This paramter is set by the detection module to open/close the Dam gate. During open,
   this parameter allows to adjust the read pointer and also provide best channel index
   to the dam module. */
#define PARAM_ID_AUDIO_DAM_DATA_FLOW_CTRL 0x0800105E

/*==============================================================================
   Type Definitions
==============================================================================*/

typedef struct param_id_audio_dam_data_flow_ctrl_t param_id_audio_dam_data_flow_ctrl_t;

/** @h2xmlp_parameter    {"PARAM_ID_AUDIO_DAM_DATA_FLOW_CTRL",
                           PARAM_ID_AUDIO_DAM_DATA_FLOW_CTRL}
    @h2xmlp_description  {Controls the data flow from a output port.}
   @h2xmlp_toolPolicy    {Calibration} */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_audio_dam_data_flow_ctrl_t
{
   uint32_t is_gate_open;
   /**< @h2xmle_description { If greater than '0' then the gate is opened, if set to '0' then the
                              gate is closed and stops draining the data.}
         @h2xmle_range       {0..4294967295}
         @h2xmle_default     {0} */

   uint32_t read_offset_in_us;
   /**< @h2xmle_description { Relative offset of the read pointer (in micro secs) before the current write pointer.
                              This is valid only if is_gate_open = TRUE }
        @h2xmle_default     {0} */

   uint32_t num_best_channels;
   /**< @h2xmle_description { number of best channels detected. }
         @h2xmle_range       {0..4294967295}
         @h2xmle_default     {0} */

   uint32_t best_ch_ids[0];
   /**< @h2xmle_description { List of best channel IDs. }
        @h2xmle_default     {0}
        @h2xmle_variableArraySize  { "num_channels" }*/
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

/*==============================================================================
   Constants
==============================================================================*/

/* This is a Set param ID to set the best channel indices to the Dam module*/
#define PARAM_ID_AUDIO_DAM_OUTPUT_CH_CFG 0x08001067

/*==============================================================================
   Type Definitions
==============================================================================*/

typedef struct param_id_audio_dam_output_ch_cfg_t param_id_audio_dam_output_ch_cfg_t;

/** @h2xmlp_parameter    {"PARAM_ID_AUDIO_DAM_OUTPUT_CH_CFG",
                           PARAM_ID_AUDIO_DAM_OUTPUT_CH_CFG}
    @h2xmlp_description  { Channel map configuration for the Dam output port associated with this control port. }
   @h2xmlp_toolPolicy    {Calibration} */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_audio_dam_output_ch_cfg_t
{
   uint32_t num_channels;
   /**< @h2xmle_description { number of channels of associated output put.}
         @h2xmle_range       {0..4294967295}
         @h2xmle_default     {0} */

   uint32_t channels_ids[0];
   /**< @h2xmle_description { List of channel IDs. }
        @h2xmle_default     {0}
        @h2xmle_variableArraySize  { "num_channels" } */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

/*==============================================================================
   Constants
==============================================================================*/

/* This is an optional parameter. All the detections engines are not required to implement
   this parameter.s

   Currently this param ID is used,
    1.  To inform Dam that IMCL peer is Acoustic Activity Detection. */
#define PARAM_ID_AUDIO_DAM_IMCL_PEER_INFO 0x08001395

/*==============================================================================
   Type Definitions
==============================================================================*/

typedef struct param_id_audio_dam_imcl_peer_info_t param_id_audio_dam_imcl_peer_info_t;

/** @h2xmlp_parameter    {"PARAM_ID_AUDIO_DAM_IMCL_PEER_INFO",
                           PARAM_ID_AUDIO_DAM_IMCL_PEER_INFO}
    @h2xmlp_description  { Peer module info. }*/

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_audio_dam_imcl_peer_info_t
{
   uint32_t is_aad_usecase;
   /**< @h2xmle_description { Indicate is the connected Peer is AAD module.}
         @h2xmle_range       {0..1}
         @h2xmle_default     {0} */

   uint32_t preferred_heap_id;
   /**< @h2xmle_description { Preferred Posal Heap id for creating circular buffer memory in Dam. }
        @h2xmle_range       {0..0xF}
        @h2xmle_default     {0} */

   uint32_t is_preferred_heap_id_valid;
   /**< @h2xmle_description { Indicates if a the preferred heap ID is valid. }
        @h2xmle_range       { 0..0xF }
        @h2xmle_default     { 0 } */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

/* This is a Set param ID to start streaming with batches of dam buffer size for an output port.
 * Audio DAM gate opens after receiving this set-param and it sends out data on the opened port.
 * It can function as regular gate open and close as well as can function to accumualte batch
 * size amount of data. The structure is similar to param_id_audio_dam_data_flow_ctrl_t with the
 * exception that this now supports three modes : regular gate open, gate close as well as new
 * batch streaming */
#define PARAM_ID_AUDIO_DAM_DATA_FLOW_CTRL_V2 0x0800151A

/*==============================================================================
   Type Definitions
==============================================================================*/
typedef enum
{
   AUDIO_DAM_GATE_CLOSE = 0,
   /* Close gate for the corresponding gate to stop data flow */

   AUDIO_DAM_GATE_OPEN = 1,
   /* Open gate for the corresponding gate and start data flow
    * starting with read-offset mentioned in read_offset_in_us
    * which is relative offset of the read pointer (in micro secs)
    * before the current write pointer.*/

   AUDIO_DAM_BATCH_STREAM = 2,
   /* Open gate for corresponding gate and send out data in batches
    * i.e. only after batch amount of data is accumulated in the dam
    * buffer - repeatedly till gate close is requested. The batch
    * size should be mentioned as read_offset_in_us for this case */

   AUDIO_DAM_DRAIN_HISTORY = 3,
   /* Open gate for corresponding gate and send out history data
    * i.e. only the amount of data mentioned in read offset if
    * accumulated in the dam buffer - and close gate immediately.
    * The history size should be mentioned as read_offset_in_us
    * for this case. No flow occurs if reqd data is not present
    * in the buffer */

   AUDIO_DAM_BATCH_STREAM_WITH_ISLAND_DUTY_CYCLING = 4,
   /* Open gate for corresponding gate and send out data in batches
    * i.e. only after batch amount of data is accumulated in the dam
    * buffer - repeatedly till gate close is requested. The batch
    * size should be mentioned as read_offset_in_us for this case, DAM
    * will exit island when the buffer is full, enter when the DAM is buffering
    * and Downstream module is done processing, wait for PARAM_ID_AUDIO_DAM_ALLOW_DCM_ISLAND_ENTRY */

   AUDIO_DAM_BATCH_INVALID = 0xFFFFFFFF
   /* Invalid option for gate_open */
} audio_dam_gate_ctrl_op_t;

typedef struct param_id_audio_dam_data_flow_ctrl_v2_t param_id_audio_dam_data_flow_ctrl_v2_t;

/** @h2xmlp_parameter    {"PARAM_ID_AUDIO_DAM_DATA_FLOW_CTRL_V2",
                           PARAM_ID_AUDIO_DAM_DATA_FLOW_CTRL_V2}
    @h2xmlp_description  {Controls the data flow from a output port.} */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_audio_dam_data_flow_ctrl_v2_t
{
   audio_dam_gate_ctrl_op_t gate_ctrl;
   /**< @h2xmle_description { Gate operation to be performed on corresponding port }
         @h2xmle_rangeEnum  { audio_dam_gate_ctrl_op_t }
         @h2xmle_default    {0} */

   uint32_t read_offset_in_us;
   /**< @h2xmle_description { This is valid only if gate_ctrl is non-zero. Usage based on gate_ctrl field. }
        @h2xmle_default     {0} */

   uint32_t num_best_channels;
   /**< @h2xmle_description { number of best channels detected. }
         @h2xmle_range       {0..4294967295}
         @h2xmle_default     {0} */

   uint32_t best_ch_ids[0];
   /**< @h2xmle_description { List of best channel IDs. }
        @h2xmle_default     {0}
        @h2xmle_variableArraySize  { "num_channels" }*/
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

/*==============================================================================
   Constants
==============================================================================*/

/* This is an optional parameter. All the detections engines are not required to implement
   this parameters.

   This parameter indiciates that Dam's if it needs to operate in virtual writer mode. In this mode,
   Dam is expected to only read data from a virtual buffer and route it to the outputs. Dam gets the
   buffer address and writer pointer handle, which allows dam to adjust the read pointers behind the
   virtual write position and start draining the FTRT data.

   Currently this param ID is used by AAD to send preroll buffer info to AAD. In virutal writer mode,
   PRB DMA driver allocates a circular buffer and writes data in to the buffer. Dam just needs to maintain
   the read position and drain data from the virtual buffer whenever gate is opened by AAD.

   Media format of the data written by the virtual writer is expected to be same as the propagated
   input media format. */
#define PARAM_ID_AUDIO_DAM_VIRTUAL_WRITER_INFO            0x08001A73

/*==============================================================================
   Type Definitions
==============================================================================*/


/** Client is expected to pass this structure to imcl_dam_get_writer_ptr_fn_t. And the function populates the
 * write position and returns to the client. */
typedef struct virt_wr_position_info_t
{
   uint32_t latest_write_addr;
   /**< @h2xmle_description {Address of the latest written sample.}
         @h2xmle_range       {0..0xFFFFFFFF}
         @h2xmle_default     {0} */

   int64_t latest_write_sample_ts;
   /**< @h2xmle_description { Timestamp associated with the latest written sample. }
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_default     {0} */

   uint32_t   is_ts_valid;
   /**< @h2xmle_description {is above TS valid}
         @h2xmle_range       {0..0xFFFFFFFF}
         @h2xmle_default     {0} */

}virt_wr_position_info_t;

/** @h2xmlp_parameter    {"PARAM_ID_AUDIO_DAM_IMCL_VIRTUAL_WRITER_INFO",
                           PARAM_ID_AUDIO_DAM_IMCL_VIRTUAL_WRITER_INFO}
    @h2xmlp_description  { Virtual Writer's buffer info. }*/
typedef ar_result_t (*imcl_dam_get_writer_ptr_fn_t) (uint32_t writer_handle /**in param*/, virt_wr_position_info_t *ret_ptr /**in/out param*/);

typedef struct param_id_audio_dam_imcl_virtual_writer_info_t param_id_audio_dam_imcl_virtual_writer_info_t;

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_audio_dam_imcl_virtual_writer_info_t
{
   uint32_t         enable;
   /**< @h2xmle_description { Buffer base address. }
         @h2xmle_range       {0..0xFFFFFFFF}
         @h2xmle_default     {0} */

   uint32_t circular_buffer_base_address;
   /**< @h2xmle_description { Buffer base address. }
         @h2xmle_range       {0..0xFFFFFFFF}
         @h2xmle_default     {0} */

   uint32_t circular_buffer_size_in_us;
   /**< @h2xmle_description { Size of the circular buffer in microseconds.}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_default     {0} */

   uint32_t circular_buffer_size_in_bytes;
   /**< @h2xmle_description { Size of the circular buffer in bytes. This should be basically equal to,
    *                            circular_buffer_size_in_bytes = circular_buffer_size_in_us*num_channels*(sample_word_size/8)}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_default     {0} */

   imcl_dam_get_writer_ptr_fn_t get_writer_ptr_fn;
   /**< @h2xmle_description { callback function pointer to get the writer address.}
        @h2xmle_range       { 0..0xFFFFFFFF }
        @h2xmle_default     { 0 } */

   uint32_t writer_handle;
   /**< @h2xmle_description { callback function pointer to get the writer address.}
        @h2xmle_range       { 0..0xFFFFFFFF }
        @h2xmle_default     { 0 } */

   uint32_t num_channels;
   /**< @h2xmle_description { Number of channels written by the writer. }
         @h2xmle_range       {0..0xFFFFFFFF}
         @h2xmle_default     {0} */

   uint32_t bits_per_sample;
   /**< @h2xmle_description { word size of each sample.
                              Note that 24 bit samples are always packed in 32 bit.
                              so for 24 bit bytes per samples is '4' }
         @h2xmle_rangeList  {32, 24, 16}
         @h2xmle_default     {16} */

   uint32_t q_factor;
   /**< @h2xmle_description { Number of fractional bits in the fixed point representation of the data. }
         @h2xmle_range       {0..0xFFFFFFFF}
         @h2xmle_default     {0} */

   uint32_t sampling_rate;
   /**< @h2xmle_description { sample rate of the mic data. }
         @h2xmle_range       {0..0xFFFFFFFF}
         @h2xmle_default     {0} */

   uint32_t data_is_signed;
   /**< @h2xmle_description { Specifies whether data is signed.
                               *
                                       - 1 -- Signed
                                       - 0 -- Unsigned
                             }
         @h2xmle_range       {0..0xFFFFFFFF}
         @h2xmle_default     {0}  */

}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif // ifdef INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL

#endif /* #ifndef IMCL_DAM_DETECTION_API_H*/
