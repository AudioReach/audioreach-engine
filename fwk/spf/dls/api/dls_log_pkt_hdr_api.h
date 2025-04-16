#ifndef _DLS_LOG_PKT_HDR_API_H_
#define _DLS_LOG_PKT_HDR_API_H_
/**
 * \file dls_log_pkt_hdr_api.h
 * \brief
 *   This file contains data structures for log packet header.
 *
 * \copyright
 *    Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *    SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*------------------------------------------------------------------------------
 *  Include Files
 *----------------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*------------------------------------------------------------------------------
 *  Global Definitions/Declarations
 *----------------------------------------------------------------------------*/
#define DLS_PACKED(x) x __attribute__((__packed__))

// TODO: Support the list of buffers to commit and return
/* Overall log packet structure
  For PCM data, the containers are in following order:
    DLS_HEADER
    VERSION
    DATA_FORMAT_TAG
    DLS_CMN_HEADER
    DLS_PCM_HEADER
    DLS_PCM_BUFFER_DATA

  For bitsream data,
    DLS_HEADER
    VERSION
    DATA_FORMAT_TAG
    DLS_CMN_HEADER
    DLS_BITSTREAM_HEADER
    DLS_BITSTREAM_BUFFER_DATA
*/

typedef enum
{
   AUDIOLOG_PCM_DATA_FORMAT = 4,
   AUDIOLOG_BS_DATA_FORMAT  = 5
} dls_audiolog_tag_t;

typedef enum
{
   AUDIOLOG_FIXED_POINT_DATA_FORMAT    = 0,
   AUDIOLOG_FLOATING_POINT_DATA_FORMAT = 1
} dls_audiolog_data_format_t;

/** Audio log PCM data format container. */
typedef DLS_PACKED(struct)
{
   uint32_t sampling_rate;
   /**< PCM sampling rate (8000 Hz, 48000 Hz, etc.). */

   uint16_t num_channels;
   /**< Number of channels in the PCM stream. */

   uint8_t bits_per_sample;
   /**< Bits per sample for the PCM data. */

   bool_t interleaved : 8;
   /**< Specifies whether the data is interleaved. */

   uint8_t q_factor;
   /**< qfactor information. */

   dls_audiolog_data_format_t data_format : 8;
   /**< Data format information for this container. */

   uint8_t channel_mapping[1];
   /**< Variable Array of channel mappings. @newpagetable */
}
dls_header_pcm_data_t;

/** Audio log header for bitstream data. */
typedef DLS_PACKED(struct)
{
   uint32_t media_fmt_id;
   /**< Media format ID for the audio/voice encoder/decoder. */
}
dls_header_bitstream_data_t;

/** Common header structure for PCM/bitstream data. */
typedef DLS_PACKED(struct)
{
   uint32_t log_session_id;
   /**< Log session ID. */

   uint32_t log_tap_id;
   /**< GUID for the tap point. */

   uint32_t log_ts_lsw;
   /*!< The lower 32-bits of the timestamp (in microseconds) for the log packet */

   uint32_t log_ts_msw;
   /*!< The upper 32-bits of the timestamp (in microseconds) for the log packet */

   uint32_t log_seg_number;
   /**< Log segment number */

   uint32_t log_segment_size;
   /**< Size in bytes of the payload, excluding the header. */

   uint32_t log_fragment_offset;
   /**< Fragment offset*/
}
dls_header_cmn_t;

/*! log_hdr_type ,includes length, code and timestamp */
typedef DLS_PACKED(struct)
{
   uint16_t len;
   /*!< Specifies the length, in bytes of the
                entry, including this header. */

   uint16_t code;
   /*!<  Specifies the log code for the entry as
                enumerated above. Note: This is
                specified as word to guarantee size. */

   uint32_t ts_lsw;
   /*!< The lower 32-bits of the system timestamp for the log entry. The
                   upper 48 bits represent elapsed time since
                   6 Jan 1980 00:00:00 in 1.25 ms units. The
                   low order 16 bits represent elapsed time
                   since the last 1.25 ms tick in 1/32 chip
                   units (this 16 bit counter wraps at the
                   value 49152). */
   uint32_t ts_msw;
   /*!< The upper 32-bits of the system timestamp for the log entry */
}
dls_log_hdr_type;

/** Complete log packet structure for logging PCM data. */
typedef DLS_PACKED(struct)
{
   dls_log_hdr_type hdr;
   /**< Length of the packet in bytes, including this header, the QXDM Pro log
        code, and system timestamp. Each log packet must begin with a
        member of type log_hdr_type. */

   uint16_t version;
   /** < Version information of the header.
         POSAL_PINE_VERSION_V1
         POSAL_PINE_VERSION_V2
         POSAL_PINE_VERSION_V3 */

   uint16_t data_format_tag;
   /**< Tag information for this container.
        AUDIOLOG_BITSTREAM_DATA_FORMAT
        AUDIOLOG_PCM_DATA_FORMAT */

   dls_header_cmn_t log_header_cmn;
   /**< Audio log header for common info */

   dls_header_pcm_data_t log_header_pcm;
   /**< Audio log header for PCM data. */

   uint8_t payload[1];
   /**< Logged packet data. */
}
dls_log_pkt_pcm_data_t;

/** Complete log packet structure for logging bitstream data. */
typedef DLS_PACKED(struct)
{
   dls_log_hdr_type hdr;
   /**< Length of the packet in bytes including this header, the QXDM Pro log
        code, and system timestamp. Each log packet must begin with a
        member of type log_hdr_type. */

   uint16_t version;
   /** < Version information of the header.
         POSAL_PINE_VERSION_V1
         POSAL_PINE_VERSION_V2
         POSAL_PINE_VERSION_V3 */

   uint16_t data_format_tag;
   /**< Tag information for this container.
        AUDIOLOG_BITSTREAM_DATA_FORMAT
        AUDIOLOG_PCM_DATA_FORMAT */

   dls_header_cmn_t log_header_cmn;
   /**< Audio log header for common info */

   dls_header_bitstream_data_t log_header_bs;
   /**< Audio log header for bitstream data. */

   uint8_t payload[1];
   /**< logged packet data. */
}
dls_log_pkt_bitstream_data_t;

/** Complete log packet structure for logging raw data. */
typedef DLS_PACKED(struct)
{
   dls_log_hdr_type hdr;
   /**< Length of the packet in bytes including this header, the QXDM Pro log
        code, and system timestamp. Each log packet must begin with a
        member of type log_hdr_type. */

   uint8_t payload[1];
   /**< logged bitstream data. */
}
dls_log_pkt_raw_data_t;

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif // #ifdef _DLS_LOG_PKT_HDR_API_H_