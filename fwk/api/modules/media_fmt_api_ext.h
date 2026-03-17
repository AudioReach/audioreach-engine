#ifndef MEDIA_FMT_API_EXT_H
#define MEDIA_FMT_API_EXT_H
/**
 * \file media_fmt_api.h
 * \brief
 *  	 This file contains media format IDs and definitions
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Not a contribution.

 * Portions copied from The Apple Lossless Audio Codec (ALAC) ALACMagicCookieDescription.txt, under Apache-2.0 license.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 *   http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 */

#include "ar_defs.h"

#ifdef __cplusplus
extern "C"
{
#endif /*__cplusplus*/

/** @h2xml_title1           {Media Format APIs}
    @h2xml_title_agile_rev  {Media Format APIs}
    @h2xml_title_date       {August 13, 2018} */
/**
   @h2xmlx_xmlNumberFormat {int}
*/

/********************************************* Vocoder Rates ************************************************/

/** Enumerations for various vocoder rates */

/** Enumeration for invalid vocoder Rate **/
#define VOCODER_INVALID_RATE          0

/** Enumeration for 1/8 vocoder rate.*/
#define VOCODER_ONE_EIGHTH_RATE       1

/** Enumeration for 1/4 vocoder rate. */
#define VOCODER_QUARTER_RATE       2

/** Enumeration for 1/2 vocoder rate. */
#define VOCODER_HALF_RATE             3

/** Enumeration for full vocoder rate. */
#define VOCODER_FULL_RATE             4

/*************************************************** AAC Media Format *************************************************/
/**
 * Media format ID for AAC, which identifies both MPEG2 and
 * MPEG4 AAC formats.
 */
#define MEDIA_FMT_ID_AAC                  0x09001001

/** Enumeration for the audio data transport stream AAC format. */
#define AAC_FORMAT_FLAG_ADTS 0

/** Enumeration for the low overhead audio stream AAC format. */
#define AAC_FORMAT_FLAG_LOAS 1

/** Enumeration for the audio data interchange format AAC format. */
#define AAC_FORMAT_FLAG_ADIF 2

/** Enumeration for the raw AAC format. */
#define AAC_FORMAT_FLAG_RAW 3

/** Enumeration for the low overhead audio transport multiplex AAC format. */
#define AAC_FORMAT_FLAG_LATM 4

/** Enumeration for the AAC low complexity audio object type:
    - When used for aac_fmt_flag in payload_media_fmt_aac_t, this parameter
      specifies that the AOT in the AAC stream is LC.
    - When used for enc_mode in payload_enc_cfg_aac_t, this parameter specifies
      that the Encoding mode to be used is LC.
*/
#define AAC_AOT_LC 2

/** Enumeration for the AAC spectral band replication AOT:
    - When used for aac_fmt_flag in payload_media_fmt_aac_t, this parameter
      specifies that the AOT in the AAC stream is SBR.
    - When used for enc_mode in payload_enc_cfg_aac_t, this parameter specifies
      that the Encoding mode to be used is AAC+.
*/
#define AAC_AOT_SBR 5

/** Enumeration for the AAC parametric stereo AOT:
    - When used for aac_fmt_flag in payload_media_fmt_aac_t, this parameter
      specifies that the AOT in the AAC stream is PS.
    - When used for enc_mode in payload_enc_cfg_aac_t, this parameter specifies
      that the Encoding mode to be used is eAAC+.
*/
#define AAC_AOT_PS 29

/** Enumeration for the bit-sliced arithmetic coding AOT:
    - When used for aac_fmt_flag in payload_media_fmt_aac_t, this parameter
      specifies that the AOT in the AAC stream is BSAC.
    - This parameter must not be used for enc_mode in
      payload_enc_cfg_aac_t. BSAC encoding is not supported.
*/
#define AAC_AOT_BSAC 22

/** AOT=17 is the ER AAC-LC format, which currently is not  supported. */

typedef struct payload_media_fmt_aac_t payload_media_fmt_aac_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload of the AAC decoder format block parameters in the
                       MEDIA_FMT_ID_AAC media format. If the total size of PCE > 0 bits, the PCE information follows the
                       structure. In this case, the client must pad the additional bits for 32-bit alignment}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct payload_media_fmt_aac_t
{
   uint16_t aac_fmt_flag;
   /**< @h2xmle_description {Bit stream format option.}
        @h2xmle_rangeList   {"AAC_FORMAT_FLAG_ADTS"=0,
                             "AAC_FORMAT_FLAG_LOAS"=1,
                             "AAC_FORMAT_FLAG_ADIF"=2,
                             "AAC_FORMAT_FLAG_RAW"=3,
                             "AAC_FORMAT_FLAG_LATM"=4}
        @h2xmle_policy      {Basic} */

   uint16_t audio_obj_type;
   /**< @h2xmle_description {Audio Object Type (AOT) present in the AAC stream.}
        @h2xmle_default     {AAC_AOT_LC}
        @h2xmle_rangeList   {"AAC_AOT_LC"=2,
                             "AAC_AOT_SBR"=5,
                             "AAC_AOT_BSAC"=22,
                             "AAC_AOT_PS"=29}
        @h2xmle_policy      {Basic} */

   uint16_t num_channels;
   /**< @h2xmle_description {Number of channels present in the AAC stream.}
        @h2xmle_rangeList   {"PCE"=0,
                             "Mono"=1,
                             "Stereo"=2,
                             "5.1 content"=6}
        @h2xmle_policy      {Basic} */

   uint16_t total_size_of_PCE_bits;
   /**< @h2xmle_description {For RAW formats and if channel_config=0 (PCE), the client can send
                             the bit stream containing PCE immediately following this structure
                             (in band)
                             If this field is set to 0, the PCE information is assumed to be
                             available in the audio bit stream and not in band.
                             If this field is greater than 0, the PCE information follows this
                             structure. Additional bits might be required for 32-bit alignment.}
        @h2xmle_range       {0...0xFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t sample_rate;
   /**< @h2xmle_description {Number of samples per second. This field must be equal to the sample
                             rate of the AAC-LC decoder output.
                             - For MP4 or 3GP containers, this sample rate is indicated by the
                               samplingFrequencyIndex field in the AudioSpecificConfig element.
                             - For ADTS format, this sample rate is indicated by the
                               samplingFrequencyIndex in the ADTS fixed header.
                             - For ADIF format, this sample rate is indicated by the
                               samplingFrequencyIndex in the program_config_element present in the
                               ADIF header.}
    @h2xmle_default     {0}
        @h2xmle_rangeList   {INVALID_VALUE=0, 8000, 11025, 12000, 16000, 22050, 24000, 32000,
                             44100, 48000, 64000, 88200, 96000}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;

typedef struct aac_enc_cfg_t aac_enc_cfg_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload for configuring the AAC encoder module}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct aac_enc_cfg_t
{
   uint16_t enc_mode;
   /**<
        @h2xmle_description {AAC encoding mode}
        @h2xmle_default     {AAC_AOT_LC}
        @h2xmle_rangeList   { "AAC_AOT_LC"=2;
                              "AAC_AOT_SBR"=5;
                              "AAC_AOT_PS"=29 }
        @h2xmle_policy      {Basic} */

   uint16_t aac_fmt_flag;
   /**<
        @h2xmle_description {AAC format flag}
        @h2xmle_default     {AAC_FORMAT_FLAG_ADTS}
        @h2xmle_rangeList   { "AAC_FORMAT_FLAG_ADTS"=0;
                              "AAC_FORMAT_FLAG_LOAS"=1;
                              "AAC_FORMAT_FLAG_RAW" =3;
                              "AAC_FORMAT_FLAG_LATM"=4 }
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;

/*************************************************** AC3/EAC3 Media Format ********************************************/
/**
 * Media format ID for AC3 streams
 */
#define MEDIA_FMT_ID_AC3    0x09001018

/**
 * Media format ID for EAC3 streams
 */
#define MEDIA_FMT_ID_EAC3   0x09001017

/*************************************************** DTS Media Format ********************************************/
#define MEDIA_FMT_ID_DTS    0x09001016

/*************************************************** ATRAC Media Format ***********************************************/
#define MEDIA_FMT_ID_ATRAC  0x09001012

/*********************************************** DTHD Media Format ******************************************/
#define MEDIA_FMT_ID_DTHD   0x09001014

/*************************************************** MAT Media Format *************************************************/
#define MEDIA_FMT_ID_MAT    0x09001015

/*************************************************** ALAC Media Format ************************************************/
/**
 * Media format ID for ALAC streams
 */
#define MEDIA_FMT_ID_ALAC                 0x09001002

typedef struct payload_media_fmt_alac_t payload_media_fmt_alac_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload of the ALAC decoder format block parameters in the
                          MEDIA_FMT_ID_ALAC media format}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct payload_media_fmt_alac_t
{
  uint32_t frame_length;
  /**< @h2xmle_description {Frames per packet when no explicit frames per packet setting is
   present in the packet header. The encoder frames per packet can be explicitly set, but for maximum
   compatibility, use the default encoder setting of 4096.}
       @h2xmle_policy      {Basic} */

  uint8_t compatible_version;
  /**< @h2xmle_description {Specifies the compatible version.}
       @h2xmle_rangeList   {"0" = 0}
       @h2xmle_policy      {Basic} */

  uint8_t bit_depth;
  /**< @h2xmle_description {Bit depth of the source PCM data.}
       @h2xmle_default     {16}
       @h2xmle_rangeList   {"16" = 16,
                            "24" = 24}
       @h2xmle_policy      {Basic} */

  uint8_t pb;
  /**< @h2xmle_description {Tuning parameter. (Currently not used.)}
       @h2xmle_rangeList   {"40" = 40}
       @h2xmle_default     {40}
       @h2xmle_policy      {Basic} */

  uint8_t mb;
  /**< @h2xmle_description {Tuning parameter. (Currently not used.)}
       @h2xmle_rangeList   {"10" = 10}
       @h2xmle_default     {10}
       @h2xmle_policy      {Basic} */

  uint8_t kb;
  /**< @h2xmle_description {Tuning parameter. (Currently not used.)}
       @h2xmle_rangeList   {"14" = 14}
       @h2xmle_default     {14}
       @h2xmle_policy      {Basic} */

  uint8_t num_channels;
  /**< @h2xmle_description {Number of channels for multichannel decoding.}
       @h2xmle_rangeList   {INVALID_VALUE=0,1...8}
       @h2xmle_policy      {Basic} */

  uint16_t max_run;
  /**< @h2xmle_description {Currently not used}
       @h2xmle_rangeList   {"255" = 255}
       @h2xmle_default     {255}
       @h2xmle_policy      {Basic} */

  uint32_t max_frame_bytes;
  /**< @h2xmle_description {Maximum size of an Apple Lossless packet within the encoded stream.}
       @h2xmle_range       {0..0xFFFFFFFF}
       @h2xmle_policy      {Basic} */

  uint32_t avg_bit_rate;
  /**< @h2xmle_description {Average bit rate in bits per second of the Apple Lossless stream.}
       @h2xmle_range       {0..0xFFFFFFFF}
       @h2xmle_policy      {Basic} */

  uint32_t sample_rate;
  /**< @h2xmle_description {Number of samples per second in Hertz.}
       @h2xmle_rangeList   {INVALID_VALUE=0, "8000"=8000, "11025"=11025, "12000"=12000,
                       "16000"=16000, "22050"=22050, "24000"=24000, "32000"=32000,
                            "44100"=44100, "48000"=48000, "64000"=64000, "88200"=88200,
             "96000"=96000, "128000"=128000, "176400"=176400, "192000"=192000}
       @h2xmle_policy      {Basic} */

  /* following channel layout of the bitstream is defined in ALACAudioTypes.h)*/
  uint32_t channel_layout_tag;
  /**< @h2xmle_description {Indicates whether channel layout information is present in the
                            bit stream.0 means, Channel layout information is not present.
                            Constants are extracted when channel layout information is present}
       @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
/*************************************************** APE Media Format *************************************************/
/**
 * Media format ID for APE streams
 */
#define MEDIA_FMT_ID_APE                  0x09001003

typedef struct payload_media_fmt_ape_t payload_media_fmt_ape_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload of the APE decoder format block parameters in the
                          MEDIA_FMT_ID_APE media format}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct payload_media_fmt_ape_t
{
    uint16_t           version;
    /**< @h2xmle_description {Specifies the compatible version.}
         @h2xmle_rangeList   {"0" = 0}
         @h2xmle_policy      {Basic} */

    uint16_t           compression_level;
    /**< @h2xmle_description {Compression level present in the encoded packet.
                              The nBlocksPerFrame parameter value is based on the compression level.}
         @h2xmle_rangeList   {"COMPRESSION_LEVEL_FAST"       = 1000,
                              "COMPRESSION_LEVEL_NORMAL"     = 2000,
                              "COMPRESSION_LEVEL_HIGH"       = 3000,
                              "COMPRESSION_LEVEL_EXTRA_HIGH" = 4000,
                              "COMPRESSION_LEVEL_INSANE"     = 5000,
                              INVALID_VALUE=0}
         @h2xmle_policy      {Basic} */

    uint32_t           format_flags;
    /**< @h2xmle_description {Reserved for future use.}
         @h2xmle_policy      {Basic} */

    uint32_t           blocks_per_frame;
    /**< @h2xmle_description {Number of audio blocks (channel samples) in one frame present in the
                              encoded packet header.}
         @h2xmle_rangeList   {"Any value present in the APE header" = 0}
         @h2xmle_policy      {Basic} */

    uint32_t           final_frame_blocks;
    /**< @h2xmle_description {Number of audio blocks (channel samples) in the final frame present
                              in the encoded packet header}
         @h2xmle_rangeList   {"Any value present in the APE header" = 0}
         @h2xmle_policy      {Basic} */

    uint32_t           total_frames;
    /**< @h2xmle_description {Total number of frames.}
         @h2xmle_rangeList   {"Any value present in the APE header" = 0}
         @h2xmle_policy      {Basic}  */

    uint16_t           bit_width;
    /**< @h2xmle_description {Bit width of the source PCM data.}
         @h2xmle_default     {16}
         @h2xmle_rangeList   {"16" = 16,"24" = 24}
         @h2xmle_policy      {Basic} */

    uint16_t           num_channels;
    /**< @h2xmle_description {Number of channels for decoding}
         @h2xmle_rangeList   {"Mono" = 1,"Stereo"= 2,INVALID_VALUE=0}
         @h2xmle_policy      {Basic} */

    uint32_t           sample_rate;
    /**< @h2xmle_description {Number of samples per second.}
        @h2xmle_rangeList   {INVALID_VALUE=0, "8000"=8000, "11025"=11025, "12000"=12000,
                             "16000"=16000, "22050"=22050, "24000"=24000, "32000"=32000,
                                  "44100"=44100, "48000"=48000, "64000"=64000, "88200"=88200,
              "96000"=96000, "128000"=128000, "176400"=176400, "192000"=192000}
         @h2xmle_policy      {Basic} */

    uint32_t           is_seek_table_present;
    /**< @h2xmle_description {Flag used to indicate the presence of the seek table. This flag is
                              not part of the APE header.}
         @h2xmle_rangeList   {"Not present" = 0,
                                  "Present" = 1}
         @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;

/*************************************************** FLAC Media Format ************************************************/
/**
 * Media format ID for FLAC streams
 */
#define MEDIA_FMT_ID_FLAC                 0x09001004

typedef struct payload_media_fmt_flac_t payload_media_fmt_flac_t;

/** @h2xmlp_subStruct
    @h2xmlp_description {Payload of the FLAC decoder format block structure parameters in the
   MEDIA_FMT_ID_FLAC media format..\n }
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct payload_media_fmt_flac_t
{
    uint16_t          num_channels;
    /**< @h2xmle_description {Number of channels present in the Flac stream.}
          @h2xmle_default     {1}
         @h2xmle_rangeList       {"Mono"=1,
                              "Stereo"=2}
         @h2xmle_policy      {Basic} */


    uint16_t          sample_size;
    /**< @h2xmle_description {Bits per sample}
         @h2xmle_default     {16}
         @h2xmle_rangeList   {16,24}
         @h2xmle_policy      {Basic} */

    uint16_t          min_blk_size;
    /**< @h2xmle_description {Minimum block size (in samples) used in the stream.
                              It must be lessthan or equal to max_blk_size.}
         @h2xmle_policy      {Basic} */

    uint16_t          max_blk_size;
    /**< @h2xmle_description {Maximum block size (in samples) used in the stream.
                              If the minimum block size equals the maximum block size,
                              a fixed block size stream is implied}
         @h2xmle_policy      {Basic} */


    uint32_t          sample_rate;
    /**< @h2xmle_description {Supported sampling rates}
  @h2xmle_default     {0}
          @h2xmle_rangeList   {INVALID_VALUE=0,0,8000, 11025, 12000, 16000, 22050, 24000, 32000,
                             44100, 48000, 64000, 88200, 96000, 128000, 176400, 192000}
         @h2xmle_policy      {Basic} */

    uint32_t          min_frame_size;
    /**< @h2xmle_description {Minimum frame size. This should be greater than 0}
         @h2xmle_policy      {Basic} */

    uint32_t          max_frame_size;
    /**< @h2xmle_description {Maximum frame size. This should be greater than 0}
         @h2xmle_policy      {Basic} */

}
#include "spf_end_pack.h"
;
/*************************************************** Vorbis Media Format **********************************************/
/**
 * Media format ID for Vorbis streams
 */
#define MEDIA_FMT_ID_VORBIS               0x09001005

typedef struct payload_media_fmt_vorbis_t payload_media_fmt_vorbis_t;

/** @h2xmlp_subStruct
    @h2xmlp_description {Payload of the VORBIS decoder format block structure parameters in the
   MEDIA_FMT_ID_VORBIS media format. \n }
    @h2xmlp_toolPolicy  {Calibration} */
#include "spf_begin_pack.h"
struct payload_media_fmt_vorbis_t
{
    uint32_t          bit_stream_fmt;
    /**< @h2xmle_description {Transcoded bit stream containing the size of the frame
                          as the first word in each frame.}
                                        @h2xmle_default     {1}
         @h2xmle_rangeList       {"Transcoded Bit Stream"=1}
         @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;

/*************************************************** WMAPro Media Format **********************************************/
/**
 * Media format ID for the WMAPRO streams.
 */
#define MEDIA_FMT_ID_WMAPRO               0x09001006

typedef struct payload_media_fmt_wmapro_t payload_media_fmt_wmapro_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload of the WMAPRO decoder format block parameters in the
                          MEDIA_FMT_ID_WMAPRO media format}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct payload_media_fmt_wmapro_t
{
    uint16_t          fmt_tag;
  /**< @h2xmle_description {WMA format type.}
       @h2xmle_rangeList   {"WMA 9 Pro"           = 0x162,
                            "WMA 9 Pro Lossless"  = 0x163,
                            "WMA 10 Pro"          = 0x166,
                            "WMA 10 Pro Lossless" = 0x167,
                            INVALID_VALUE=0}
       @h2xmle_policy      {Basic} */

    uint16_t          num_channels;
  /**< @h2xmle_description {Number of channels encoded in the input stream.}
       @h2xmle_rangeList   {INVALID_VALUE=0,1...8}
       @h2xmle_policy      {Basic} */

    uint32_t          sample_rate;
  /**< @h2xmle_description {Number of samples per second in Hertz.}
       @h2xmle_rangeList   {INVALID_VALUE=0, 11025, 16000, 22050, 32000,
                             44100, 48000, 88200, 96000}
       @h2xmle_policy      {Basic} */

    uint32_t          avg_bytes_per_sec;
  /**< @h2xmle_description {Bitrate expressed as the average bytes per second}
       @h2xmle_rangeList   {INVALID_VALUE=0,2000..96000}
       @h2xmle_policy      {Basic} */

    uint16_t          blk_align;
  /**< @h2xmle_description {Size of the bit stream packet size. WMA Pro files have a payload of one
                            block per bit stream packet.}
       @h2xmle_rangeList   {"13376"=13376}
       @h2xmle_default     {13376}
       @h2xmle_policy      {Basic} */

    uint16_t          bits_per_sample;
  /**< @h2xmle_description {Number of bits per sample in the encoded WMA stream.}
       @h2xmle_default     {16}
       @h2xmle_rangeList   {"16" = 16,
                            "24" = 24}
       @h2xmle_policy      {Basic} */

    uint32_t          channel_mask;
  /**< @h2xmle_description {Bit-packed double word (32-bits) that indicates the recommended
                            speaker positions for each source channel.}
       @h2xmle_policy      {Basic} */

    uint16_t          enc_options;
  /**< @h2xmle_description {Bit-packed word with values that indicate whether certain features of
                            the bit stream are used.}
       @h2xmle_rangeList   {"ENCOPT3_PURE_LOSSLESS"          = 0x0001,
                            "ENCOPT3_FRM_SIZE_MOD"           = 0x0006,
                            "ENCOPT3_SUBFRM_DIV"             = 0x0038,
                            "ENCOPT3_WRITE_FRAMESIZE_IN_HDR" = 0x0040,
                            "ENCOPT3_GENERATE_DRC_PARAMS"    = 0x0080,
                            "ENCOPT3_RTMBITS"                = 0x0100,
                            INVALID_VALUE=0}
       @h2xmle_policy      {Basic} */

    uint16_t          advanced_enc_option;
  /**< @h2xmle_description {Advanced encoding option. This field is ignored.}
       @h2xmle_policy      {Basic} */

    uint32_t          advanced_enc_options2;
  /**< @h2xmle_description {Advanced encoding option 2. This field is ignored.}
       @h2xmle_policy      {Basic} */

}
#include "spf_end_pack.h"
;

/*************************************************** WMAStd Media Format **********************************************/
/**
 * Media format ID for the WMASTD streams.
 */
#define MEDIA_FMT_ID_WMASTD               0x09001007

typedef struct payload_media_fmt_wmastd_t payload_media_fmt_wmastd_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload of the WMASTD decoder format block parameters in the
                          MEDIA_FMT_ID_WMASTD media format}
    @h2xmlp_toolPolicy  {Calibration} */


#include "spf_begin_pack.h"
struct payload_media_fmt_wmastd_t
{
    uint16_t          fmt_tag;
    /**< @h2xmle_description {WMA format tag.}
         @h2xmle_rangeList   {"WMA v9 standard"     = 0x161}
         @h2xmle_default     {0x161}
         @h2xmle_policy      {Basic} */

    uint16_t          num_channels;
    /**< @h2xmle_description {Number of channels in the stream.}
         @h2xmle_rangeList   {"Mono" = 1, "Stereo" = 2, INVALID_VALUE=0}
         @h2xmle_policy      {Basic} */

    uint32_t          sample_rate;
    /**< @h2xmle_description {Number of samples per second in Hertz.}
	     @h2xmle_rangeList   {INVALID_VALUE=0, "8000"=8000, "11025"=11025,
	                          "16000"=16000, "22050"=22050, "32000"=32000,
                                  "44100"=44100, "48000"=48000}
         @h2xmle_policy      {Basic} */

    uint32_t          avg_bytes_per_sec;
    /**< @h2xmle_description {Bitrate expressed as the average bytes per second}
         @h2xmle_policy      {Basic} */

    uint16_t          blk_align;
    /**< @h2xmle_description {Block align. All WMA files with a maximum packet size of 13376
                          are supported.}
         @h2xmle_policy      {Basic} */

    uint16_t          bits_per_sample;
    /**< @h2xmle_description {Number of bits per sample in the output}
         @h2xmle_rangeList   {"16" = 16}
         @h2xmle_default     {16}
         @h2xmle_policy      {Basic} */

    uint32_t          channel_mask;
    /**< @h2xmle_description {Channel mask.}
         @h2xmle_rangeList  {"Stereo (front left/front right)" = 3,
                          "Mono  (centre)" = 4, INVALID_VALUE=0}
         @h2xmle_policy      {Basic} */

    uint16_t          enc_options;
    /**< @h2xmle_description {Options used during encoding.}
         @h2xmle_policy      {Basic} */

    uint16_t          reserved;
    /**< @h2xmle_description {Used for alignment; must be set to 0.}
         @h2xmle_policy      {Basic} */

}
#include "spf_end_pack.h"
;

/*************************************************** SBC Media Format *************************************************/
/**
 * Media format ID for the SBC
 */
#define MEDIA_FMT_ID_SBC                                 0x09001008


/** Enumeration for SBC channel Mono mode. */
#define MEDIA_FMT_SBC_CHANNEL_MODE_MONO                     1

/** Enumeration for SBC channel Stereo mode. */
#define MEDIA_FMT_SBC_CHANNEL_MODE_STEREO                   2

/** Enumeration for SBC channel Dual Mono mode. */
#define MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO                8

/** Enumeration for SBC channel Joint Stereo mode. */
#define MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO             9

/** Enumeration for SBC bit allocation method = loudness. */
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS            0

/** Enumeration for SBC bit allocation method = SNR.  */
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR                 1

/* SBC encoder configuration structure. */
typedef struct sbc_enc_cfg_t sbc_enc_cfg_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload for configuring the SBC encoder module} */

#include "spf_begin_pack.h"
struct sbc_enc_cfg_t
{
    uint32_t          num_subbands;
   /**<
    @h2xmle_description {Number of subbands}
    @h2xmle_default     {4}
    @h2xmle_rangeList   { "4" = 4,
                          "8" = 8}
    @h2xmle_policy      {Basic} */

    uint32_t          blk_len;
   /**<
    @h2xmle_description {Size of the encoded block in samples}
    @h2xmle_default     {4}
    @h2xmle_rangeList   { "4"  = 4,
                          "8"  = 8,
                          "12" = 12,
                          "16" = 16}
    @h2xmle_policy      {Basic} */

    int32_t          channel_mode;
   /**<
    @h2xmle_description {Mode used to allocate bits between channels.

                        -Native mode indicates that encoding must be performed with the number
                          of channels at the input.

                        -If postprocessing outputs one-channel data, Mono mode is used. If
                          postprocessing outputs two-channel data, Stereo mode is used.

                        -The number of channels must not change during encoding. }
    @h2xmle_default     {-1}
    @h2xmle_rangeList   { "PARAM_VAL_NATIVE" = -1,
	                      "MEDIA_FMT_SBC_CHANNEL_MODE_MONO" = 1,
	                      "MEDIA_FMT_SBC_CHANNEL_MODE_STEREO" = 2,
                          "MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO" = 8,
                          "MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO" = 9}
    @h2xmle_policy      {Basic} */

    uint32_t          alloc_method;
   /**<
    @h2xmle_description {Encoder bit allocation method.}
    @h2xmle_default     {0}
    @h2xmle_rangeList   { "MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS"=0,
                          "MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR"=1}
    @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
/*************************************************** MP3 Media Format *************************************************/
/**
 * Media format ID for MP3 streams
 */
#define MEDIA_FMT_ID_MP3                  0x09001009
/*************************************************** MP2 Media Format *************************************************/

/** @ingroup asmstrm_mediafmt_mp2
      Media format ID for MP2. Following are decoder-specific details:
      - Sampling rates -- 16 to 48 kHz
      - Channels -- 2
      - Bitwidth -- 16/24 bits per sample
      - Bitrates -- 8 to 384 kbps
      - Minimum buffer required per frame -- 1728 bytes
      - External bit stream support -- No

      @par MP2 format block
      No format block is needed. All information is contained in the bit stream.
  */
#define MEDIA_FMT_ID_MP2 0x09001029

/*************************************************** AMRWB+ Media Format **********************************************/
/**
 * Media format ID for the amrwbplus streams
 */
#define MEDIA_FMT_ID_AMRWBPLUS            0x0900100A

/** @ingroup MEDIA_FMT_ID_AMRWBPLUS
    Media format ID for the AMR-WB Plus format block.

    @par AMR-WB Plus format block (payload_media_fmt_amrwbplus_t)
    @table{weak__payload__amrwbplus__fmt__blk__t}
*/
typedef struct payload_media_fmt_amrwbplus_t payload_media_fmt_amrwbplus_t;
/** @h2xmlp_subStruct
28  @h2xmlp_description {Payload of the AMRWBPLUS decoder format block parameters in the
29                       MEDIA_FMT_ID_AMRWBPLUS media format}
30  @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"

struct payload_media_fmt_amrwbplus_t
{
    uint32_t          amr_frame_fmt;
    /**< @h2xmle_description {Transport Interface Format (TIF)}
         @h2xmle_rangeList   {"contains a 2-byte header for each frame within the superframe" = 6,
                               "contains one header per superframe"= 0}
         @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
/*************************************************** AMRNB Media Format *************************************************/
/**
 * Media format ID for AMRNB streams
 */
#define MEDIA_FMT_ID_EAMR                0x0900100B

#define MEDIA_FMT_ID_AMRNB_IF2           0x0900100C

#define MEDIA_FMT_ID_AMRNB_MODEM         0x0900100D

#define MEDIA_FMT_ID_AMRNB_FS            0x0900100E


/** Enumeration for 4.75 kbps AMR-NB Encoding mode. */
#define AMRNB_FS_ENCODE_MODE_MR475                0

/** Enumeration for 5.15 kbps AMR-NB Encoding mode. */
#define AMRNB_FS_ENCODE_MODE_MR515                1

/** Enumeration for 5.90 kbps AMR-NB Encoding mode. */
#define AMRNB_FS_ENCODE_MODE_MMR59                2

/** Enumeration for 6.70 kbps AMR-NB Encoding mode. */
#define AMRNB_FS_ENCODE_MODE_MMR67                3

/** Enumeration for 7.40 kbps AMR-NB Encoding mode. */
#define AMRNB_FS_ENCODE_MODE_MMR74                4

/** Enumeration for 7.95 kbps AMR-NB Encoding mode. */
#define AMRNB_FS_ENCODE_MODE_MMR795               5

/** Enumeration for 10.20 kbps AMR-NB Encoding mode. */
#define AMRNB_FS_ENCODE_MODE_MMR102               6

/** Enumeration for 12.20 kbps AMR-NB Encoding mode. */
#define AMRNB_FS_ENCODE_MODE_MMR122               7

/** Enumeration for disabling AMR-NB DTX mode. */
#define AMRNB_FS_DTX_MODE_OFF                     0

/** Enumeration for enabling AMR-NB DTX mode VAD1. */
#define AMRNB_FS_DTX_MODE_VAD1                    1

/** Enumeration for enabling AMR-NB DTX mode VAD2. */
#define AMRNB_FS_DTX_MODE_VAD2                    2

/** Enumeration for enabling AMR-NB automatic DTX mode. */
#define AMRNB_FS_DTX_MODE_AUTO                    3


typedef struct amrnb_enc_cfg_t amrnb_enc_cfg_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload for configuring the AMRNB encoder module}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct amrnb_enc_cfg_t
{
    uint16_t          enc_mode;
    /**< @h2xmle_description {AMR-NB encoding rate.}
         @h2xmle_default     {0}
         @h2xmle_rangeList   {"AMRNB_FS_ENCODE_MODE_MR475"=0,
                              "AMRNB_FS_ENCODE_MODE_MR515"=1,
                              "AMRNB_FS_ENCODE_MODE_MMR59"=2,
                              "AMRNB_FS_ENCODE_MODE_MMR67"=3,
                              "AMRNB_FS_ENCODE_MODE_MMR74"=4,
                              "AMRNB_FS_ENCODE_MODE_MMR795"=5,
                              "AMRNB_FS_ENCODE_MODE_MMR102"=6,
                              "AMRNB_FS_ENCODE_MODE_MMR122"=7}
         @h2xmle_policy      {Basic} */
    uint16_t          dtx_mode;
    /**< @h2xmle_description {Specifies whether DTX mode is enabled}
             @h2xmle_default     {0}
             @h2xmle_rangeList   {"Enable"=1,
                                  "Disable"=0}
             @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;

typedef struct payload_media_fmt_eamr_t payload_media_fmt_eamr_t;

/** @h2xmlp_subStruct
    @h2xmlp_description {Payload of the EAMR decoder format block structure to indicate the decoding format}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct payload_media_fmt_eamr_t
{
    uint32_t          media_format_id;
    /**< @h2xmle_description {Configures the decoding format of the AMRNB decoder}
         @h2xmle_default     {MEDIA_FMT_ID_AMRNB_MODEM}
         @h2xmle_rangeList   {"MEDIA_FMT_ID_EAMR"=0x0900100B,
                              "MEDIA_FMT_ID_AMRNB_IF2"=0x0900100C,
                              "MEDIA_FMT_ID_AMRNB_MODEM"=0x0900100D,
                              "MEDIA_FMT_ID_AMRNB_FS"=0x0900100E}
         @h2xmle_policy      {Basic} */
}

#include "spf_end_pack.h"
;

/*********************************************** EVRC Media format *********************************************************/
/**
 * Media format ID for EVRC streams
 */
#define MEDIA_FMT_ID_EVRC 			     0x0900100F
#define MEDIA_FMT_ID_EVRC_FS             0x09001024


typedef struct evrc_enc_cfg_t evrc_enc_cfg_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload for configuring the EVRC encoder module}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct evrc_enc_cfg_t
{
	uint16_t          max_rate;
    /**< @h2xmle_description {Maximum allowed encoder frame rate.}
         @h2xmle_default     {VOCODER_INVALID_RATE}
         @h2xmle_rangeList   {"VOCODER_ONE_EIGHTH_RATE"=1,
                              "VOCODER_ONE_FOURTH_RATE"=2,
                              "VOCODER_HALF_RATE"=3,
                              "VOCODER_FULL_RATE"=4,
                              "VOCODER_INVALID_RATE"=0}
         @h2xmle_policy      {Basic} */
	uint16_t          min_rate;
    /**< @h2xmle_description {Maximum allowed encoder frame rate.}
         @h2xmle_default     {VOCODER_INVALID_RATE}
         @h2xmle_rangeList   {"VOCODER_ONE_EIGHTH_RATE"=1,
                              "VOCODER_ONE_FOURTH_RATE"=2,
                              "VOCODER_HALF_RATE"=3,
                              "VOCODER_FULL_RATE"=4,
                              "VOCODER_INVALID_RATE"=0}
         @h2xmle_policy      {Basic} */
	uint16_t          rate_mod_cmd;
	/**< @h2xmle_description {Rate modulation command
	                         If bit 0=1, rate control is enabled.
	                         If bit 1=1, the maximum number of consecutive full rate frames is
	                         limited with numbers supplied in bits 2 to 10.
	                         If bit 1=0, the minimum number of non-full rate frames in between
	                         two full rate frames is forced to the number supplied in bits 2 to 10
	                         In both cases of bit 1, if necessary, half rate is used to substitute full rate.
	                         Bits 15 to 10 are reserved and must all be set to 0.}
	     @h2xmle_default     {0}
	     @h2xmle_policy      {Basic} */
	   uint16_t         reserved;
/**< @h2xmle_description {Clients must set this field to 0.}
	   @h2xmle_rangeList {"0"=0}
	   @h2xmle_visibility	{hide} */
}
#include "spf_end_pack.h"
;

/************************************************************************************************************************/

/************************************************ HR Media format *****************************************************/

/**
 * Media format ID for HR streams
 */
#define MEDIA_FMT_ID_HR              0x09001019


/************************************************************************************************************************/

/************************************************ FR Media format *****************************************************/

/**
 * Media format ID for FR streams
 */
#define MEDIA_FMT_ID_FR              0x0900101B
#define MEDIA_FMT_ID_FR_FS           0x0900101C


/************************************************************************************************************************/

/************************************************ EFR Media format *****************************************************/

/**
 * Media format ID for EFR streams
 */
#define MEDIA_FMT_ID_EFR              0x0900101A


/************************************************************************************************************************/

/************************************************ G711 Media format *****************************************************/

/**
 * Media format ID for G711 streams
 */
#define MEDIA_FMT_ID_G711_ALAW              0x09001032
#define MEDIA_FMT_ID_G711_MLAW              0x09001033
#define MEDIA_FMT_ID_G711_ALAW_V2           0x09001034
#define MEDIA_FMT_ID_G711_MLAW_V2           0x09001035
#define MEDIA_FMT_ID_G711_ALAW_FS           0x09001036
#define MEDIA_FMT_ID_G711_MLAW_FS           0x09001037

typedef struct payload_media_fmt_g711_t payload_media_fmt_g711_t;

/** @h2xmlp_subStruct
    @h2xmlp_description {Payload of the G711 decoder format block structure}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct payload_media_fmt_g711_t
{
    uint32_t          sample_rate;
    /**< @h2xmle_description {Sampling rate}
  	  	 @h2xmle_default     {8000}
         @h2xmle_rangeList   {"8000" = 8000, "16000" = 16000}
         @h2xmle_policy      {Basic} */
}

#include "spf_end_pack.h"
;

/************************************************ G722 Media format *****************************************************/

/**
 * Media format ID for G722 streams
 */
#define MEDIA_FMT_ID_G722              0x09001013

/************************************************************************************************************************/


/************************************************ G729AB Media format *****************************************************/

/**
 * Media format ID for G729AB streams
 */
#define MEDIA_FMT_ID_G729AB              0x09001010


/*********************************************** LDAC Media format *********************************************************/
/**
 * Media format ID for LDAC streams
 */
#define MEDIA_FMT_ID_LDAC 			     0x0900101D

/** Enumeration for Stereo mode on the LDAC channel. */
#define LDAC_CHANNEL_MODE_STEREO         1

/** Enumeration for Dual Channel mode on the LDAC channel. */
#define LDAC_CHANNEL_MODE_DUAL_CHANNEL   2

/** Enumeration for Mono mode on the LDAC channel. */
#define LDAC_CHANNEL_MODE_MONO           4

/* ldac encoder configuration structure. */
typedef struct ldac_enc_cfg_t ldac_enc_cfg_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload for configuring the ldac encoder module, optional param} */

#include "spf_begin_pack.h"
struct ldac_enc_cfg_t
{
    uint32_t          channel_mode;
   /**<
    @h2xmle_description {Mode used to allocate bits between channels.
                        The number of channels must not change during encoding }
    @h2xmle_default     {1}
    @h2xmle_rangeList   { "LDAC_CHANNEL_MODE_STEREO" = 1,
	                      "LDAC_CHANNEL_MODE_DUAL_CHANNEL" = 2,
                          "LDAC_CHANNEL_MODE_MONO" = 4}
    @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;

/************************************************ V13K Media format *****************************************************/
#define MEDIA_FMT_ID_V13K                             0x09001025
#define MEDIA_FMT_ID_V13K_FS                          0x09001026


/** Enumeration for 14.4 kbps V13K Encoding mode. */
#define V13K_FS_ENCODE_MODE_MR1440                0

/** Enumeration for 12.2 kbps V13K Encoding mode. */
#define V13K_FS_ENCODE_MODE_MR1220                1

/** Enumeration for 11.2 kbps V13K Encoding mode. */
#define V13K_FS_ENCODE_MODE_MR1120                2

/** Enumeration for 9.0 kbps V13K Encoding mode. */
#define V13K_FS_ENCODE_MODE_MR90                  3

/** Enumeration for 7.2 kbps V13K Encoding mode. */
#define V13K_FS_ENCODE_MODE_MR720                 4


typedef struct v13k_enc_cfg_t v13k_enc_cfg_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload for configuring the V13K encoder module}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct v13k_enc_cfg_t
{
	uint16_t          max_rate;
    /**< @h2xmle_description {Maximum allowed encoder frame rate.}
         @h2xmle_default     {VOCODER_FULL_RATE}
         @h2xmle_rangeList   {"VOCODER_ONE_EIGHTH_RATE"=1,
                              "VOCODER_ONE_FOURTH_RATE"=2,
                              "VOCODER_HALF_RATE"=3,
                              "VOCODER_FULL_RATE"=4,
                              "VOCODER_INVALID_RATE"=0}
         @h2xmle_policy      {Basic} */
	uint16_t          min_rate;
    /**< @h2xmle_description {Minimum allowed encoder frame rate.}
         @h2xmle_default     {VOCODER_ONE_EIGHTH_RATE}
         @h2xmle_rangeList   {"VOCODER_ONE_EIGHTH_RATE"=1,
                              "VOCODER_ONE_FOURTH_RATE"=2,
                              "VOCODER_HALF_RATE"=3,
                              "VOCODER_FULL_RATE"=4,
                              "VOCODER_INVALID_RATE"=0}
         @h2xmle_policy      {Basic} */
	uint16_t          reduced_rate_cmd;
	/**< @h2xmle_description {Reduced rate command, used to change the average bit-rate
	                         of the vocoder}
	     @h2xmle_rangeList   {"V13K_FS_ENCODE_MODE_MR1440"=0,
                              "V13K_FS_ENCODE_MODE_MR1220"=1,
                              "V13K_FS_ENCODE_MODE_MR1120"=2,
                              "V13K_FS_ENCODE_MODE_MR90"=3,
                              "V13K_FS_ENCODE_MODE_MR720"=4}
	     @h2xmle_default     {V13K_FS_ENCODE_MODE_MR1440}
	     @h2xmle_policy      {Basic} */
	uint16_t          rate_mod_cmd;
	/**< @h2xmle_description {Rate modulation command
	                         If bit 0=1, rate control is enabled.
	                         If bit 1=1, the maximum number of consecutive full rate frames is
	                         limited with numbers supplied in bits 2 to 10.
	                         If bit 1=0, the minimum number of non-full rate frames in between
	                         two full rate frames is forced to the number supplied in bits 2 to 10
	                         In both cases of bit 1, if necessary, half rate is used to substitute full rate.
	                         Bits 15 to 10 are reserved and must all be set to 0.}
	     @h2xmle_default     {0}
	     @h2xmle_range       {0..0x03FF}
	     @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;

/************************************************ AMRWB Media format *****************************************************/

/**
 * Media format ID for AMRWB streams
 */
#define MEDIA_FMT_ID_AMRWB              0x09001027
#define MEDIA_FMT_ID_AMRWB_FS           0x09001028


/** Enumeration for 6.60 kbps AMR-WB Encoding mode. */
#define AMRWB_FS_ENCODE_MODE_MR66                 0

/** Enumeration for 8.85 kbps AMR-WB Encoding mode. */
#define AMRWB_FS_ENCODE_MODE_MR885                1

/** Enumeration for 12.65 kbps AMR-WB Encoding mode. */
#define AMRWB_FS_ENCODE_MODE_MR1265               2

/** Enumeration for 14.25 kbps AMR-WB Encoding mode. */
#define AMRWB_FS_ENCODE_MODE_MR1425               3

/** Enumeration for 15.85 kbps AMR-WB Encoding mode. */
#define AMRWB_FS_ENCODE_MODE_MR1585               4

/** Enumeration for 18.25 kbps AMR-WB Encoding mode. */
#define AMRWB_FS_ENCODE_MODE_MR1825               5

/** Enumeration for 19.85 kbps AMR-WB Encoding mode. */
#define AMRWB_FS_ENCODE_MODE_MR1985               6

/** Enumeration for 23.05 kbps AMR-WB Encoding mode. */
#define AMRWB_FS_ENCODE_MODE_MR2305               7

/** Enumeration for 23.85 kbps AMR-WB Encoding mode. */
#define AMRWB_FS_ENCODE_MODE_MR2385               8

typedef struct amrwb_enc_cfg_t amrwb_enc_cfg_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload for configuring the AMRWB encoder module}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct amrwb_enc_cfg_t
{
    uint16_t          enc_mode;
    /**< @h2xmle_description {AMR-WB encoding rate.}
         @h2xmle_default     {0}
         @h2xmle_rangeList   {"AMRWB_FS_ENCODE_MODE_MR66"=0,
                              "AMRWB_FS_ENCODE_MODE_MR885"=1,
                              "AMRWB_FS_ENCODE_MODE_MR1265"=2,
                              "AMRWB_FS_ENCODE_MODE_MR1425"=3,
                              "AMRWB_FS_ENCODE_MODE_MR1585"=4,
                              "AMRWB_FS_ENCODE_MODE_MR1825"=5,
                              "AMRWB_FS_ENCODE_MODE_MR1985"=6,
                              "AMRWB_FS_ENCODE_MODE_MR2305"=7,
                              "AMRWB_FS_ENCODE_MODE_MR2385"=8}
         @h2xmle_policy      {Basic} */
    uint16_t          dtx_mode;
    /**< @h2xmle_description {Specifies whether DTX mode is enabled}
             @h2xmle_default     {0}
             @h2xmle_rangeList   {"Enable"=1,
                                  "Disable"=0}
             @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;

/************************************************************************************************************************/

/*********************************************** CELT Media format
 * *********************************************************/
/**
 * Media format ID for the CELT
 */
#define MEDIA_FMT_ID_CELT               0x0900102A

/* CELT encoder configuration structure. */
typedef struct celt_enc_cfg_t celt_enc_cfg_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload for configuring the CELT encoder module} */

#include "spf_begin_pack.h"
struct celt_enc_cfg_t
{

   uint16_t frame_size;
   /**<
      @h2xmle_description {Frame size used for encoding.}
       @h2xmle_default     {256}
       @h2xmle_rangeList   {"64" = 64,
                            "128" = 128,
                            "256" = 256,
                            "512" = 512}
       @h2xmle_policy      {Basic} */

   uint16_t complexity;
   /**<
      @h2xmle_description {Complexity of the algorithm.
       - Currently, this parameter is used to enable or disable the pitch
        analysis. In the future, it can be scaled to add more levels of
        complexity.
       - value less than 2 - Disable pitch analysis
       - Value > 2 - Enable pitch analysis}
       @h2xmle_default     {3}
       @h2xmle_range       {0..10}
       @h2xmle_policy      {Basic} */

   uint16_t prediction_mode;
   /**<
      @h2xmle_description {Switch variable for the prediction feature.
      - This parameter is used to choose between the level of interframe
         predictions allowed while encoding.
        - 0 -- Independent frames
        - 1 -- Short-term interframe prediction allowed
        - 2 -- Long-term prediction allowed}
       @h2xmle_default     {2}
       @h2xmle_rangeList   {"0" = 0,
                          "1" = 1,
                     "2" = 2}
       @h2xmle_policy      {Basic} */

   uint16_t vbr_flag;
   /**<
      @h2xmle_description {Variable bit rate flag.
       Currently, this parameter is disabled. In the future, it can be
         enabled if use cases require it.}
       @h2xmle_default     {0}
       @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
/************************************************ FOURGV Media format *****************************************************/

/**
 * Media format ID for FOURGV streams
 */
#define MEDIA_FMT_ID_EVRCB_FS                           0x0900102B
#define MEDIA_FMT_ID_EVRCWB_FS                          0x0900102C
#define MEDIA_FMT_ID_4GV_NB_MODEM                       0x0900102D
#define MEDIA_FMT_ID_4GV_WB_MODEM                       0x0900102E
#define MEDIA_FMT_ID_4GV_NW_MODEM                       0x0900102F
#define MEDIA_FMT_ID_4GV_NW                             0x09001030
#define MEDIA_FMT_ID_EVRC_NW_2K                         0x09001031


/** Enumeration for 9.3 kbps EVRC-B Encoding mode. */
#define EVRCB_FS_ENCODE_MODE_MR930        0

/** Enumeration for 8.5 kbps EVRC-B Encoding mode. */
#define EVRCB_FS_ENCODE_MODE_MR850        1

/** Enumeration for 7.5 kbps EVRC-B Encoding mode. */
#define EVRCB_FS_ENCODE_MODE_MR750        2

/** Enumeration for 7.0 kbps EVRC-B Encoding mode. */
#define EVRCB_FS_ENCODE_MODE_MR700        3

/** Enumeration for 6.6 kbps EVRC-B Encoding mode. */
#define EVRCB_FS_ENCODE_MODE_MR660        4

/** Enumeration for 6.2 kbps EVRC-B Encoding mode. */
#define EVRCB_FS_ENCODE_MODE_MR620        5

/** Enumeration for 5.8 kbps EVRC-B Encoding mode. */
#define EVRCB_FS_ENCODE_MODE_MR580        6

/** Enumeration for 4.8 kbps EVRC-B Encoding mode. */
#define EVRCB_FS_ENCODE_MODE_MR480        7

/* EVCR-B encoder configuration structure. */
//typedef struct evrcb_enc_cfg_t evrcb_enc_cfg_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload for configuring the EVRC-B encoder module}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct evrcb_enc_cfg_t
{
	uint16_t          max_rate;
    /**< @h2xmle_description {Maximum allowed encoder frame rate.}
         @h2xmle_default     {EVRCB_FS_ENCODE_MODE_MR930}
         @h2xmle_rangeList   {"EVRCB_FS_ENCODE_MODE_MR930"=0,
                              "EVRCB_FS_ENCODE_MODE_MR850"=1,
                              "EVRCB_FS_ENCODE_MODE_MR750"=2,
                              "EVRCB_FS_ENCODE_MODE_MR700"=3,
                              "EVRCB_FS_ENCODE_MODE_MR660"=4,
                              "EVRCB_FS_ENCODE_MODE_MR620"=5,
                              "EVRCB_FS_ENCODE_MODE_MR580"=6,
                              "EVRCB_FS_ENCODE_MODE_MR480"=7}
         @h2xmle_policy      {Basic} */
	uint16_t          min_rate;
    /**< @h2xmle_description {Minimum allowed encoder frame rate.}
         @h2xmle_default     {EVRCB_FS_ENCODE_MODE_MR480}
         @h2xmle_rangeList   {"EVRCB_FS_ENCODE_MODE_MR930"=0,
                              "EVRCB_FS_ENCODE_MODE_MR850"=1,
                              "EVRCB_FS_ENCODE_MODE_MR750"=2,
                              "EVRCB_FS_ENCODE_MODE_MR700"=3,
                              "EVRCB_FS_ENCODE_MODE_MR660"=4,
                              "EVRCB_FS_ENCODE_MODE_MR620"=5,
                              "EVRCB_FS_ENCODE_MODE_MR580"=6,
                              "EVRCB_FS_ENCODE_MODE_MR480"=7}
         @h2xmle_policy      {Basic} */
    uint16_t          dtx_mode;
    /**< @h2xmle_description {Specifies whether DTX mode is enabled}
             @h2xmle_default     {0}
             @h2xmle_rangeList   {"Enable"=1,
                                  "Disable"=0}
             @h2xmle_policy      {Basic} */

    uint16_t          reduced_rate_level;
    /**< @h2xmle_description {Reduced rate level for the average encoding rate}
         @h2xmle_default     {EVRCB_FS_ENCODE_MODE_MR480}
         @h2xmle_rangeList   {"EVRCB_FS_ENCODE_MODE_MR930"=0,
                              "EVRCB_FS_ENCODE_MODE_MR850"=1,
                              "EVRCB_FS_ENCODE_MODE_MR750"=2,
                              "EVRCB_FS_ENCODE_MODE_MR700"=3,
                              "EVRCB_FS_ENCODE_MODE_MR660"=4,
                              "EVRCB_FS_ENCODE_MODE_MR620"=5,
                              "EVRCB_FS_ENCODE_MODE_MR580"=6,
                              "EVRCB_FS_ENCODE_MODE_MR480"=7}
 	 	@h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;

/** Enumeration for 8.5 kbps EVRC-WB Encoding mode.  */
#define EVRCWB_FS_ENCODE_MODE_MR850        0

/** Enumeration for 9.3 kbps EVRC-WB Encoding mode. */
#define EVRCWB_FS_ENCODE_MODE_MR930        4

/** Enumeration for 4.8 kbps EVRC-WB Encoding mode. */
#define EVRCWB_FS_ENCODE_MODE_MR480        7

/* EVRC-WB encoder configuration structure. */
typedef struct evrcwb_enc_cfg_t evrcwb_enc_cfg_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload for configuring the EVRC-WB encoder module}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"

struct evrcwb_enc_cfg_t
{
	uint16_t          max_rate;
    /**< @h2xmle_description {Maximum allowed encoder frame rate.}
         @h2xmle_default     {VOCODER_FULL_RATE}
         @h2xmle_rangeList   {"VOCODER_ONE_EIGHTH_RATE"=1,
                              "VOCODER_ONE_FOURTH_RATE"=2,
                              "VOCODER_HALF_RATE"=3,
                              "VOCODER_FULL_RATE"=4,
                              "VOCODER_INVALID_RATE"=0}
         @h2xmle_policy      {Basic} */
	uint16_t          min_rate;
    /**< @h2xmle_description {Minimum allowed encoder frame rate.}
         @h2xmle_default     {VOCODER_ONE_EIGHTH_RATE}
         @h2xmle_rangeList   {"VOCODER_ONE_EIGHTH_RATE"=1,
                              "VOCODER_ONE_FOURTH_RATE"=2,
                              "VOCODER_HALF_RATE"=3,
                              "VOCODER_FULL_RATE"=4,
                              "VOCODER_INVALID_RATE"=0}
         @h2xmle_policy      {Basic} */
    uint16_t          dtx_enable;
    /**< @h2xmle_description {Specifies whether DTX mode is enabled}
             @h2xmle_default     {0}
             @h2xmle_rangeList   {"Enable"=1,
                                  "Disable"=0}
             @h2xmle_policy      {Basic} */
	uint16_t          reduced_rate_level;
	/**< @h2xmle_description {Reduced rate level for the average encoding rate.}
         @h2xmle_default     {EVRCWB_FS_ENCODE_MODE_MR850}
         @h2xmle_rangeList   {"EVRCWB_FS_ENCODE_MODE_MR850"=0,
                              "EVRCWB_FS_ENCODE_MODE_MR930"=4,
                              "EVRCWB_FS_ENCODE_MODE_MR480"=7}
         @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;

/*********************************************** EVS Media format *********************************************************/
/**
 * Media format ID for EVRC streams
 */
#define MEDIA_FMT_ID_EVS             0x09001023

/** Enumeration for 06.60 kbps, AMR-WB IO (WB). */
#define EVS_CODEC_MODE_0660               0

/** Enumeration for 08.85 kbps, AMR-WB IO (WB). */
#define EVS_CODEC_MODE_0885               1

/** Enumeration for 12.65 kbps, AMR-WB IO (WB). */
#define EVS_CODEC_MODE_1265               2

/** Enumeration for 14.25 kbps, AMR-WB IO (WB). */
#define EVS_CODEC_MODE_1425               3

/** Enumeration for 15.85 kbps, AMR-WB IO (WB). */
#define EVS_CODEC_MODE_1585               4

/** Enumeration for 18.25 kbps, AMR-WB IO (WB). */
#define EVS_CODEC_MODE_1825               5

/** Enumeration for 19.85 kbps, AMR-WB IO (WB). */
#define EVS_CODEC_MODE_1985               6

/** Enumeration for 23.05 kbps, AMR-WB IO (WB). */
#define EVS_CODEC_MODE_2305               7

/** Enumeration for 23.85 kbps, AMR-WB IO (WB). */
#define EVS_CODEC_MODE_2385               8

/** Enumeration for 05.90 kbps, EVS (NB, WB). */
#define EVS_CODEC_MODE_0590               9

/** Enumeration for 07.20 kbps, EVS (NB, WB). */
#define EVS_CODEC_MODE_0720               10

/** Enumeration for 08.00 kbps, EVS (NB, WB). */
#define EVS_CODEC_MODE_0800               11

/** Enumeration for 09.60 kbps, EVS (NB, WB, SWB). */
#define EVS_CODEC_MODE_0960               12

/** Enumeration for 13.20 kbps, EVS (NB, WB, SWB). */
#define EVS_CODEC_MODE_1320               13

/** Enumeration for 16.40 kbps, EVS (NB, WB, SWB, FB). */
#define EVS_CODEC_MODE_1640               14

/** Enumeration for 24.40 kbps, EVS (NB, WB, SWB, FB) */
#define EVS_CODEC_MODE_2440               15

/** Enumeration for 32.00 kbps, EVS (WB, SWB, FB). */
#define EVS_CODEC_MODE_3200               16

/** Enumeration for 48.00 kbps, EVS (WB, SWB, FB). */
#define EVS_CODEC_MODE_4800               17

/** Enumeration for 64.00 kbps, EVS (WB, SWB, FB). */
#define EVS_CODEC_MODE_6400               18

/** Enumeration for 96.00 kbps, EVS (WB, SWB, FB). */
#define EVS_CODEC_MODE_9600               19

/** Enumeration for 128.00 kbps, EVS (WB, SWB, FB). */
#define EVS_CODEC_MODE_12800               20


typedef struct evs_enc_cfg_t evs_enc_cfg_t;
/** @h2xmlp_subStruct
    @h2xmlp_description {Payload for configuring the EVS encoder module}
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct evs_enc_cfg_t
{
   uint32_t bit_rate;
   /**< @h2xmle_description {EVS encoding bit rate}
        @h2xmle_rangeList   {"EVS_CODEC_MODE_0660"=0,
        					 "EVS_CODEC_MODE_0885"=1,
        					 "EVS_CODEC_MODE_1265"=2,
        					 "EVS_CODEC_MODE_1425"=3,
        					 "EVS_CODEC_MODE_1585"=4,
        					 "EVS_CODEC_MODE_1825"=5,
        					 "EVS_CODEC_MODE_1985"=6,
        					 "EVS_CODEC_MODE_2305"=7,
        					 "EVS_CODEC_MODE_2385"=8,
        					 "EVS_CODEC_MODE_0590"=9,
        					 "EVS_CODEC_MODE_0720"=10,
        					 "EVS_CODEC_MODE_0800"=11,
        					 "EVS_CODEC_MODE_0960"=12,
        					 "EVS_CODEC_MODE_1320"=13,
        					 "EVS_CODEC_MODE_1640"=14,
        					 "EVS_CODEC_MODE_2440"=15,
        					 "EVS_CODEC_MODE_3200"=16,
        					 "EVS_CODEC_MODE_4800"=17,
        					 "EVS_CODEC_MODE_6400"=18,
        					 "EVS_CODEC_MODE_9600"=19,
        					 "EVS_CODEC_MODE_12800"=20}
        @h2xmle_default     {13}
        @h2xmle_policy      {Basic} */

   uint32_t bandwidth;
   /**< @h2xmle_description {EVS encoder bandwidth}
        @h2xmle_rangeList   {"SAMPLE_RATE_8K"=0,
        					 "SAMPLE_RATE_16K"=1,
        					 "SAMPLE_RATE_32K"=2,
        					 "SAMPLE_RATE_48K"=3}
        @h2xmle_default     {2}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;

/*************************************************** OPUS Media Format ************************************************/
/**
 * Media format ID for OPUS streams
 */
#define MEDIA_FMT_ID_OPUS                 0x09001039

typedef struct payload_media_fmt_opus_t payload_media_fmt_opus_t;

/** @h2xmlp_subStruct
    @h2xmlp_description {Payload of the OPUS decoder format block structure parameters in the
                         MEDIA_FMT_ID_OPUS media format..\n }
    @h2xmlp_toolPolicy  {Calibration} */

#include "spf_begin_pack.h"
struct payload_media_fmt_opus_t
{
   uint16_t        bitstream_format;
   /**< @h2xmle_description {OggOpus container format or raw fmt with
                          4bytes len appended.}
        @h2xmle_default     {1}
        @h2xmle_range       {0...1}
        @h2xmle_policy      {Basic} */

   uint16_t        payload_type;
   /**< @h2xmle_description {payload_type indicates if unset config data is available in OGG 0th page,
                if set, following elements of this structure are valid.}
        @h2xmle_default     {1}
        @h2xmle_range       {0...1}
        @h2xmle_policy      {Basic} */


   uint8_t          version;
   /**< @h2xmle_description {Specifies the compatible version.}
         @h2xmle_default     {1}
        @h2xmle_rangeList   {"1" = 1}
        @h2xmle_policy      {Basic} */

   uint8_t          num_channels;
   /**< @h2xmle_description {Number of channels present in the Opus stream.}
         @h2xmle_default     {1}
        @h2xmle_rangeList       {"Mono"=1,
                              "Stereo"=2,
                              "LinearSurround"=3,
                              "Quadraphonic"=4,
                              "5dot0Surround"=5,
                              "5dot1Surround"=6,
                              "6dot1Surround"=7,
                              "7dot1Surround"=8}
        @h2xmle_policy      {Basic} */


   uint16_t          pre_skip;
   /**< @h2xmle_description {Number of Samples at 48kHz to discard from decoder output when
                          starting playback.}
        @h2xmle_default     {0}
        @h2xmle_range   {0...0xFFFF}
        @h2xmle_policy      {Basic} */


   uint32_t          sample_rate;
   /**< @h2xmle_description {Supported sampling rates}
        @h2xmle_default     {0}
        @h2xmle_rangeList   {INVALID_VALUE=0,0,8000, 12000, 16000, 24000, 48000}
        @h2xmle_policy      {Basic} */


   uint16_t          output_gain;
   /**< @h2xmle_description {The gain applied when decoding, to acheive desired playback volume
                          in Q7.8 format.}
        @h2xmle_default     {0x100}
        @h2xmle_rangeList   {0...0xFF}
        @h2xmle_policy      {Basic} */


   uint8_t          mapping_family;
   /**< @h2xmle_description {Mapping family defines the order and meaning of channels}
        @h2xmle_default     {0}
        @h2xmle_rangeList   {"0" = 0,
                          "1" = 1,
                          "255" = 255}
        @h2xmle_policy      {Basic} */


   uint8_t          stream_count;
   /**< @h2xmle_description {Number of streams present in Opus Stream
   This member is valid only if mapping_family is not 0}
        @h2xmle_default     {0x1}
        @h2xmle_range   {1...0xFF}
        @h2xmle_policy      {Basic} */


   uint8_t          coupled_count;
   /**< @h2xmle_description {Number of streams which needs to be configured as Stereo
                          This member is valid only if mapping_family is not 0}
        @h2xmle_default     {0x0}
        @h2xmle_range   {0...0xFF}
        @h2xmle_policy      {Basic} */


   uint8_t          channel_map[8];
   /**< @h2xmle_description {This contains one octet per output channel, it
                          tells which decoded channel is to be used for each speaker location
                          This member is valid only if mapping_family is not 0}
        @h2xmle_default     {0x0}
        @h2xmle_range   {0...0xFF}
        @h2xmle_policy      {Basic} */

   uint8_t         reserved[3];
   /**< @h2xmle_description {Clients must set this field to 0.}
          @h2xmle_rangeList {"0"=0}
          @h2xmle_visibility   {hide} */

}
#include "spf_end_pack.h"
;

/*************************************************** ASR Media Format ************************************************/
/**
 * Media format ID for ASR text output
 */
#define MEDIA_FMT_ID_ASR                 0x0900103A

/** Enumeration for Language code en_US. */
#define LANG_CODE_en_US               0

/** Enumeration for Language code zh_CN. */
#define LANG_CODE_zh_CN               1

/** Enumeration for Language code hi_IN. */
#define LANG_CODE_hi_IN               2

/** Enumeration for Language code es_US. */
#define LANG_CODE_es_US               3

/** Enumeration for Language code ko_KR. */
#define LANG_CODE_ko_KR               4

/** Enumeration for Language code ja_JP. */
#define LANG_CODE_ja_JP               5

typedef struct payload_media_fmt_asr_t payload_media_fmt_asr_t;

/** @h2xmlp_subStruct
    @h2xmlp_description {Payload of the ASR text format structure parameters in the
                         MEDIA_FMT_ID_ASR media format..\n }
    @h2xmlp_toolPolicy  {Calibration} */

/* if the media mf type MEDIA_FMT_ID_ASR, the expected data is in the form of capi_asr_data_out_port_t */

#include "spf_begin_pack.h"
struct payload_media_fmt_asr_t
{
   uint32_t        max_text_output_size;
   /**< @h2xmle_description {ASR output text stream max chunk size}
        @h2xmle_default     {2048}
        @h2xmle_range       {1024...2048}
        @h2xmle_policy      {Basic} */

   uint32_t        language_code;
   /**< @h2xmle_description {langugage code of text output}
        @h2xmle_rangeList   {"LANG_CODE_en_US"=0,
        					 "LANG_CODE_zh_CN"=1,
        					 "LANG_CODE_hi_IN"=2,
        					 "LANG_CODE_es_US"=3,
        					 "LANG_CODE_ko_KR"=4,
        					 "LANG_CODE_ja_JP"=5}
        @h2xmle_default     {0}
        @h2xmle_policy      {Basic} */

}
#include "spf_end_pack.h"
;

/************************************************************************************************************************/

#ifdef __cplusplus
}
#endif /*__cplusplus*/


#endif /* MEDIA_FMT_API_EXT_H */
