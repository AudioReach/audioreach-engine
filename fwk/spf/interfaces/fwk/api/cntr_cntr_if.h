#ifndef _CONTAINER_CONTAINER_IF_H_
#define _CONTAINER_CONTAINER_IF_H_

/**
 * \file cntr_cntr_if.h
 *
 * \brief
 *     This file defines container APIs between containers.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "module_cmn_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/**< payload for SPF_MSG_CMD_INFORM_ICB_INFO. as always spf_msg_header_t precedes this. */
typedef struct spf_msg_cmd_inform_icb_info_t
{
   uint32_t downstream_frame_len_samples;
   /**< frame length of downstream in samples, valid only if sample rate is nonzero */

   uint32_t downstream_sample_rate;
   /**< sample rate of downstream */

   uint32_t downstream_frame_len_us;
   /**< frame length of downstream in microseconds, used only if sample rate is zero. */

   uint32_t downstream_period_us;
   /**< period at which downstream operates in,  in microseconds, used instead of frame length if non-zero. */

   uint32_t downstream_sid;
   /**< downstream scenario ID. */

   bool_t downstream_consumes_variable_input;
   /**< Flag to indicate if downstream consumes variable input */

   bool_t downstream_is_self_real_time;
   /**< Flag to indicate if downstream processing is real time (timer driven) */

   bool_t downstream_propagated_downstream_real_time;
   /**< Flag to indicate if processing downstream to the downstream is real time (timer driven) */

   bool_t downstream_set_single_buffer_mode;
   /**< Flag to indicate to operate the upstream in single buffer mode. This flag will
    *   will be used by the downstream to coomunicate thto upstream */

} spf_msg_cmd_inform_icb_info_t;
/** downstream informs upstream of its frame length through this opcode */
#define SPF_MSG_CMD_INFORM_ICB_INFO 0x01001039

/**
 * Payload structure for SPF_MSG_CTRL_PORT_POLLING_MSG and SPF_MSG_CMD_CTRL_PORT_TRIGGER_MSG
 *
 * this payload is preceded by spf_msg_header_t
 */
typedef struct spf_msg_ctrl_port_msg_t
{
   bool_t is_intra_cntr;
   /**<  Boolean to indicate if the IMCL is intra-container (within the container) */
   void *src_intra_cntr_port_hdl;
   /**<  Control Port handle of the source - valid if is_intra_cntr = TRUE; NULL otherwise */
   void *dst_intra_cntr_port_hdl;
   /**<  Control Port handle of the destination - valid if is_intra_cntr = TRUE; NULL otherwise */
   uint32_t actual_size;
   /**< Number of valid bytes in the buffer. */
   uint32_t max_size;

   uint64_t data_buf[1];
   /**< for inline data this is the location of the first data sample. */
} spf_msg_ctrl_port_msg_t;

/** Control port msg opcode, only trigger based ctrl msgs are received by the command handler. */
#define SPF_MSG_CMD_CTRL_PORT_TRIGGER_MSG 0x0100103A

struct spf_msg_peer_port_property_info_t
{
   uint32_t property_type;  /**< topo_port_property_type_t */
   uint32_t property_value; /**< 32 bit payload per property: assume for now that property cannot exceed 32 bits. */
};

typedef struct spf_msg_peer_port_property_info_t spf_msg_peer_port_property_info_t;

/**
 * Payload structure for SPF_MSG_CMD_PEER_PORT_PROPERTY_UPDATE & SPF_IPC_FWK_EXTN_GPR_CMD_PEER_PORT_PROPERTY_UPDATE
 *
 * this payload is preceded by spf_msg_header_t
 */
typedef struct spf_msg_peer_port_property_update_t
{
   uint32_t                          num_properties;
   spf_msg_peer_port_property_info_t payload[1];
} spf_msg_peer_port_property_update_t;

/** Control port message handling through polling mechanism. */
#define SPF_MSG_CTRL_PORT_POLLING_MSG 0x01001041

/**
 * Downstream informs stop to upstream (using SPF_MSG_CMD_PEER_PORT_PROPERTY_UPDATE).
 * Later upstream sends this message to downstream as an ack.
 * Without sending upstream stopping, downstream cannot flush.
 *
 * Note: this ack is not done synchronously.
 *
 * no special payload. just spf_msg_header_t
 */
#define SPF_MSG_CMD_UPSTREAM_STOPPED_ACK 0x01001040

/** Opcode used by the container to send the is data real time flag to peer container. */
#define SPF_MSG_CMD_PEER_PORT_PROPERTY_UPDATE 0x0100103F

#define DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK AR_NON_GUID(0x80000000)
#define DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT 31

#define DATA_BUFFER_FLAG_OUT_OF_BAND_MASK AR_NON_GUID(0x40000000)
#define DATA_BUFFER_FLAG_OUT_OF_BAND_SHIFT 30

#define DATA_BUFFER_FLAG_PREBUFFER_MARK_MASK AR_NON_GUID(0x20000000)
#define DATA_BUFFER_FLAG_PREBUFFER_MARK_SHIFT 29

#define DATA_BUFFER_FLAG_TIMESTAMP_CONTINUE_MASK AR_NON_GUID(0x10000000)
#define DATA_BUFFER_FLAG_TIMESTAMP_CONTINUE_SHIFT 28

#define DATA_BUFFER_FLAG_EOF_MASK AR_NON_GUID(0x08000000)
#define DATA_BUFFER_FLAG_EOF_SHIFT 27
/**
 * Payload structure for SPF_MSG_DATA_BUFFER
 *
 * this payload is preceded by spf_msg_header_t
 */
typedef struct spf_msg_data_buffer_t spf_msg_data_buffer_t;
struct spf_msg_data_buffer_t
{
   int64_t timestamp;
   /**< Timestamp in microseconds. Negative timestamps are possible due to filter delays. */
   uint32_t flags;
   /**< flag whose bits describe the data.
    * SPF_MSG_DATA_BUFFER_FLAG_TIMESTAMP_VALID - timestamp valid - 1 = valid; 0 = invalid
    * SPF_MSG_DATA_BUFFER_FLAG_OUT_OF_BAND - data is out of band - 1 = out-of-band; 0 = inline. */
   uint32_t actual_size;
   /**< Number of valid bytes in the buffer. */
   uint32_t max_size;
   /**< Total number of bytes allocated in the data region. */
   module_cmn_md_list_t *metadata_list_ptr;
   /**< linked list of metadata. */
   union
   {
      uint64_t data_buf[1];
      /**< for inline data this is the location of the first data sample. */
      void *data_ptr;
      /**< for out-of-band data, this points to the location of the first data sample. */
   };
};

#define GET_SPF_INLINE_DATABUF_REQ_SIZE(req_size)                                                                      \
   (GET_SPF_MSG_REQ_SIZE(req_size + sizeof(spf_msg_data_buffer_t) - sizeof(uint64_t)))
#define GET_SPF_INLINE_CTRLBUF_REQ_SIZE(req_size)                                                                      \
   (GET_SPF_MSG_REQ_SIZE(req_size + sizeof(spf_msg_ctrl_port_msg_t) - sizeof(uint64_t)))
#define GET_SPF_METADATA_BUF_REQ_SIZE(req_size) (req_size + sizeof(spf_metadata_t) - sizeof(uint64_t))

/** Data buffer */
#define SPF_MSG_DATA_BUFFER 0x04001008

/**
 * Payload structure for SPF_MSG_DATA_BUFFER_V2
 *
 * this payload is preceded by spf_msg_header_t
 */

typedef struct spf_msg_single_buf_v2_t spf_msg_single_buf_v2_t;

struct spf_msg_single_buf_v2_t
{
   int8_t *data_ptr;
   /**<  for out-of-band data, this points to the location of the first data sample. The
    * size of this buffer is max_size mentioned in data buffer message v2.   */

   uint32_t max_size;
   /**< Total number of bytes allocated for each buffer in the data region. */

   uint32_t actual_size;
   /**<  for the buffer above this is the number of valid bytes in it.   */
};

typedef struct spf_msg_data_buffer_v2_t spf_msg_data_buffer_v2_t;

struct spf_msg_data_buffer_v2_t
{
   int64_t timestamp;
   /**< Timestamp in microseconds. Negative timestamps are possible due to filter delays. */

   uint32_t flags;
   /**< flag whose bits describe the data.
    * SPF_MSG_DATA_BUFFER_FLAG_TIMESTAMP_VALID - timestamp valid - 1 = valid; 0 = invalid
    * SPF_MSG_DATA_BUFFER_FLAG_OUT_OF_BAND - data is out of band - 1 = out-of-band; 0 = inline. */

   module_cmn_md_list_t *metadata_list_ptr;
   /**< linked list of metadata. */
   uint8_t bufs_num;
   /**< Number of spf_msg_single_buf_v2_t that will be following this message. This should correspond
    * to number of buffers implied in media format */

#if 0
spf_msg_single_buf_v2_t buf_info[bufs_num];
/**< list of out-of-band data pointers and lengths in data region based on
 * number of buffers from media format in bufs_num. */
#endif
};

#define GET_SPF_DATABUF_V2_REQ_SIZE(bufs_num, buf_size_per_data_buf)                                                   \
   (GET_SPF_MSG_REQ_SIZE(sizeof(spf_msg_data_buffer_v2_t) +                                                            \
                         bufs_num * (sizeof(spf_msg_single_buf_v2_t) + buf_size_per_data_buf)))

#define SPF_MSG_DATA_BUFFER_V2 0x0400100C

/** Media format can be sent in data as well as control path.
 * This is why it's defined in this common header */

/**
 * Data format indicating overall format of the data.
 *
 * Note the difference in data format 'data_format_t' exposed by CAPIv2:
 * the CAPI v2 enum has valid 'zero' value (CAPI_FIXED_POINT).
 * But spf_data_format_t starts with valid value of 1. 0 is invalid or unknown value.
 */
typedef enum spf_data_format_t
{
   SPF_UNKNOWN_DATA_FORMAT            = 0,
   SPF_FIXED_POINT                    = 1 << 0, /**< Maps to DATA_FORMAT_FIXED_POINT */
   SPF_FLOATING_POINT                 = 1 << 1,
   SPF_RAW_COMPRESSED                 = 1 << 2, /**< DATA_FORMAT_RAW_COMPRESSED */
   SPF_IEC61937_PACKETIZED            = 1 << 3, /**< DATA_FORMAT_IEC61937_PACKETIZED */
   SPF_DSD_DOP_PACKETIZED             = 1 << 4,
   SPF_COMPR_OVER_PCM_PACKETIZED      = 1 << 5,
   SPF_GENERIC_COMPRESSED             = 1 << 6, /**< DATA_FORMAT_GENERIC_COMPRESSED */
   SPF_IEC60958_PACKETIZED            = 1 << 7, /**< DATA_FORMAT_IEC60958_PACKETIZED */
   SPF_IEC60958_PACKETIZED_NON_LINEAR = 1 << 8, /**< DATA_FORMAT_IEC60958_PACKETIZED_NON_LINEAR */
   SPF_DEINTERLEAVED_RAW_COMPRESSED   = 1 << 9,
} spf_data_format_t;

#ifdef PROD_SPECIFIC_MAX_CH
   #define SPF_MAX_CHANNELS PROD_SPECIFIC_MAX_CH
   #else
   #define SPF_MAX_CHANNELS 32
#endif

typedef enum spf_interleaving_t
{
   SPF_INTERLEAVING_UNKNOWN = 0,
   SPF_INTERLEAVED,
   SPF_DEINTERLEAVED_PACKED
} spf_interleaving_t;

/**
 * Standard media format for fixed point, floating point (except SPF_RAW_COMPRESSED) etc
 *
 * Data is always deinterleaved, little endian, signed.
 *
 * 16 bit data is always in 16 bit words
 * 24 and 32 bit data is always in 32 bit words.
 *
 * Currently packed mode (24 bit in 24 bit word)is not supported.
 *    To support sample_word_size of 24 and Q format of Q23 are required to be added.
 *
 *    Internal to spf, it's always 16 or 32 bits. Q format decides actual bits per sample (24 bit or not).
 *
 *    See comment in topo_utils.h above TOPO_QFORMAT_TO_BIT_WIDTH
 */
typedef struct spf_std_media_format_t spf_std_media_format_t;
struct spf_std_media_format_t
{
   uint32_t sample_rate;
   /**< sample rate in Hz */
   uint16_t bits_per_sample;
   /** bits per sample or sample word size 16, 32 */
   uint16_t q_format;
   /**< Q format of the fixed point data (applicable to PCM only), 15, 27 (for 24 bits per sample), 31*/
   uint32_t num_channels;
   /**< Number of channels*/
   spf_interleaving_t interleaving;
   /*set to 0 if interleaving, 1 for deinterleaving packed*/
   uint8_t channel_map[SPF_MAX_CHANNELS];
   /**< Channel mapping */
};

/*For PCM, if sample rate is zero, frame_len_us used, else frame_len_samples is used
 * For raw compr and deint raw compr data frame_len_bytes is used,
 * and we also populate the other fields to be sent downstream*/
typedef struct spf_msg_frame_length_t
{
   uint32_t sample_rate;
   /**< Sample rate in Hz. */

   uint32_t frame_len_samples;
   /**< Frame length in samples. */

   uint32_t frame_len_us;
   /**< Frame length in microseconds. */

   uint32_t frame_len_bytes;
   /**< Frame length in bytes.
    * For raw compr and deint raw compr data,
    * this represents the total size of the buffer- including all channels */
} spf_msg_frame_length_t;

typedef struct spf_msg_media_format_flags_t
{
   uint32_t does_upstream_req_prebuf : 1; /**< TRUE if upstream outputs variable data.
                                                                     If upstream doesn't send a pre-buffer, then
                                             downstream has to create its own */
} spf_msg_media_format_flags_t;

/**
 * Payload structure for SPF_MSG_DATA_MEDIA_FORMAT
 * Also used by SPF_MSG_CMD_MEDIA_FORMAT
 *
 * this payload is preceded by spf_msg_header_t
 *
 * dst_handle_ptr in the spf_msg_header_t points to the ext in port to which this message is to be routed.
 *
 * This payload also contains upstream frame len related fields
 * And flags that need to be propagated along with media format
 *
 */
typedef struct spf_msg_media_format_t spf_msg_media_format_t;
struct spf_msg_media_format_t
{
   spf_data_format_t df;
   /**< data format */
   uint32_t fmt_id;
   /**< GUID of the data format PCM, AAC etc */
   uint32_t actual_size;

   spf_msg_frame_length_t upstream_frame_len;
   /**< frame length of upstream container, can be in us or samples */

   spf_msg_media_format_flags_t flags;
   /**< Flags sent as part of media fmt msg */

   /**< size of the following payload (depends on df, fmt_id).*/
   uint64_t payload_start[1];
   /**< points to spf_std_media_format_t for PCM_PACKETIZED, and directly to payload in case of RAW compressed*/
};

#define GET_SPF_STD_MEDIA_FMT_SIZE                                                                                     \
   (GET_SPF_MSG_REQ_SIZE(sizeof(spf_msg_media_format_t) - sizeof(uint64_t) + sizeof(spf_std_media_format_t)))
#define GET_SPF_RAW_MEDIA_FMT_SIZE(req_size)                                                                           \
   (GET_SPF_MSG_REQ_SIZE(sizeof(spf_msg_media_format_t) - sizeof(uint64_t) + req_size))

#define SPF_IS_PACKETIZED(data_format)                                                                                 \
   ((data_format & (SPF_IEC61937_PACKETIZED | SPF_DSD_DOP_PACKETIZED | SPF_COMPR_OVER_PCM_PACKETIZED |                 \
                    SPF_GENERIC_COMPRESSED | SPF_IEC60958_PACKETIZED | SPF_IEC60958_PACKETIZED_NON_LINEAR)) != 0)

#define SPF_IS_PACKETIZED_OR_PCM(data_format)                                                                          \
   ((data_format & (SPF_FIXED_POINT | SPF_FLOATING_POINT | SPF_IEC61937_PACKETIZED | SPF_DSD_DOP_PACKETIZED |          \
                    SPF_COMPR_OVER_PCM_PACKETIZED | SPF_GENERIC_COMPRESSED | SPF_IEC60958_PACKETIZED |                 \
                    SPF_IEC60958_PACKETIZED_NON_LINEAR)) != 0)

#define SPF_IS_RAW_COMPR_DATA_FMT(data_format)                                                                         \
   ((data_format & (SPF_RAW_COMPRESSED | SPF_DEINTERLEAVED_RAW_COMPRESSED)) != 0)

#define SPF_IS_PCM_DATA_FORMAT(data_format) ((data_format & (SPF_FIXED_POINT | SPF_FLOATING_POINT)) != 0)

/** Media format message
 *  payload spf_msg_media_format_t*/
#define SPF_MSG_DATA_MEDIA_FORMAT 0x04001009

/** Media format in Control path
 *  payload spf_msg_media_format_t */
#define SPF_MSG_CMD_MEDIA_FORMAT 0x01001038

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef _CONTAINER_CONTAINER_IF_H_
