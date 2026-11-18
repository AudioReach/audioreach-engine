/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/*========================================================================
Edit History


when       who     what, where, why
--------   ---     -------------------------------------------------------
7/15/26   vasharma    Added file for opensource resampler based MFC

========================================================================== */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#ifndef MFC_OSRC_CONVERTER_H
#define MFC_OSRC_CONVERTER_H
#include "audio_basic_op_ext.h"
#include "ar_error_codes.h"
#include "capi.h"
#include "capi_cmn.h"
#include "shared_lib_api.h"
#include "shared_aud_cmn_lib.h"
#include "common_enc_dec_api.h"
#include "ChannelMixerLib.h"
#include "osrc_rs_lib_wrapper.h"
#include "spf_interleaver.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*--------------------------------------------------------------*/
/* Macro definitions                                            */
/* -------------------------------------------------------------*/
//#define USE_Q6_SPECIFIC_CODE
//#define MFC_OSRC_LIB_DEBUG 1
#define MIID_UNKNOWN 0

#define CNV_MSG_PREFIX "MFC_OSRC:[%lX] "
#define CNV_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, CNV_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

#define CNV_IIR_RS_MIN_FRAME_SIZE_US 1000
#define MEM_ALIGN_EIGHT_BYTE         8

/*----------------------------------------------------------------------------
 * Global Data Definitions
 * -------------------------------------------------------------------------*/
static const uint32_t MAX_Q23                         = ((1 << PCM_Q_FACTOR_23) - 1);
static const uint32_t MIN_Q23                         = (-(1 << PCM_Q_FACTOR_23));
static const uint32_t MAX_Q27                         = ((1 << PCM_Q_FACTOR_27) - 1);
static const uint32_t MIN_Q27                         = (-(1 << PCM_Q_FACTOR_27));
static const uint32_t MAX_Q31                         = 0x7FFFFFFF;
static const uint32_t MIN_Q31                         = 0x80000000;
#define NUMBER_OF_PROCESS               11
#define MAX_NUMBER_OF_BUFFERING_PROCESS 7
#define NUMBER_OF_CHANNEL_MAPS          2
#define NUMBER_OF_REMAP_BUFFERS         4
#define MAX_BW_IDX                      3
// All the kpps values are scaled based on sample word size
// Indices to WS map is as follows,
// 0 - > 16 bit
// 1 - > 24 bit
// 2 - > 32 bit
static const uint32_t endianness_kpps_per_ch[MAX_BW_IDX] = { 230, 180, 130 };
static const uint32_t intl_dintl_kpps_per_ch[MAX_BW_IDX] = { 110, 640, 110 };
static const uint32_t byte_cnv_kpps_per_ch[MAX_BW_IDX]   = { 100, 300, 120 };
static const uint32_t ch_mixer_kpps_per_ch[MAX_BW_IDX]   = { 450, 500, 500 };
static const uint32_t memcpy_kpps_per_ch[MAX_BW_IDX]     = { 100, 100, 100 };
static const uint32_t fw_kpps                            = 100; // just a little extra kpps for logic

static const uint32_t base_sample_rate = 48000;

#define MFC_CEIL(x, y) (((x) + (y)-1) / (y))

// Enlists possible combinations of media_fmt for a given buf
// BW- bit width, W - word size, Q - q-factor
typedef enum mfc_mf_combo_t {
   MFC_INVALID_CMBO = 0,
   MFC_BW16_W16_Q15,
   MFC_BW24_W24_Q23,
   MFC_BW24_W32_Q23,
   MFC_BW24_W32_Q27,
   MFC_BW24_W32_Q31,
   MFC_BW32_W32_Q31,
   MFC_BW32_W32_FLOAT,
   MFC_BW64_W64_DOUBLE,
} mfc_mf_combo_t;

typedef enum mfc_endian_t {
   MFC_UNKNOWN_ENDIAN = PARAM_VAL_INVALID,
   MFC_LITTLE_ENDIAN  = PCM_LITTLE_ENDIAN,
   MFC_BIG_ENDIAN     = PCM_BIG_ENDIAN
} mfc_endian_t;

typedef enum mfc_alignment_t {
   MFC_UNKNOWN_ALIGNMENT = PARAM_VAL_INVALID,
   MFC_LSB_ALIGNED       = PCM_LSB_ALIGNED,
   MFC_MSB_ALIGNED       = PCM_MSB_ALIGNED
} mfc_alignment_t;

/* PC handles unpacked data, in the CAPI_DEINTERLEVED_UNPACKED_V2 format,
   i.e it reads/updates only first chs lengths */
typedef enum mfc_interleaving_t
{
   MFC_UNKNOWN_INTERLEAVING      = PARAM_VAL_INVALID,
   MFC_INTERLEAVED               = PCM_INTERLEAVED,
   MFC_DEINTERLEAVED_PACKED      = PCM_DEINTERLEAVED_PACKED,
   MFC_DEINTERLEAVED_UNPACKED_V2 = PCM_DEINTERLEAVED_UNPACKED
} mfc_interleaving_t;

// Enlisting data format to the pc module
typedef enum mfc_data_format_t {
   MFC_FIXED_FORMAT = 0,
   MFC_FLOATING_FORMAT,
   MFC_INVALID_FORMAT,
} mfc_data_format_t;

// Media Format used for
typedef struct mfc_media_fmt_t
{
   uint32_t          sampling_rate;
   mfc_endian_t       endianness;
   mfc_interleaving_t interleaving;
   mfc_mf_combo_t     byte_combo;
   mfc_alignment_t    alignment;
   mfc_data_format_t  data_format;
   uint16_t          bit_width;
   uint16_t          word_size;
   uint16_t          q_factor;
   uint16_t          num_channels;
   uint16_t *        channel_type;
} mfc_media_fmt_t;

// Process modules which can be present in the PCM pipeline
// UP - comes before channel map module
// Down - comes after channel map module
typedef union mfc_process_flags_t
{
   struct
   {
      uint32_t ENDIANNESS_PRE : 1;
      uint32_t DATA_CNV_FLOAT_TO_FIXED : 1;
      uint32_t INT_DEINT_PRE : 1;
      uint32_t BYTE_CNV_PRE : 1;
      uint32_t RESAMPLER_PRE : 1;
      uint32_t CHANNEL_MIXER : 1;
      uint32_t RESAMPLER_POST : 1;
      uint32_t BYTE_CNV_POST : 1;
      uint32_t INT_DEINT_POST : 1;
      uint32_t DATA_CNV_FIXED_TO_FLOAT : 1;
      uint32_t ENDIANNESS_POST : 1;
   };
   uint32_t word;
} mfc_process_flags_t;

typedef enum mfc_proc_flags_t {
   ENDIANNESS_PRE = 0,
   DATA_CNV_FLOAT_TO_FIXED,
   INT_DEINT_PRE,
   BYTE_CNV_PRE,
   RESAMPLER_PRE,
   CHANNEL_MIXER,
   RESAMPLER_POST,
   BYTE_CNV_POST,
   INT_DEINT_POST,
   DATA_CNV_FIXED_TO_FLOAT,
   ENDIANNESS_POST,
} mfc_proc_flags_t;

// OSResampler is always FIR-based, no type selection needed

typedef enum mfc_rs_position_flag_t { RESAMPLER_POSITION_PRE, RESAMPLER_POSITION_POST } mfc_rs_position_flag_t;

typedef enum pcm_mf_cnv_type_t { PCM_CNV = 0, PCM_ENC, PCM_DEC, MFC } pcm_mf_cnv_type_t;
// This structure is used as a v-table for process calls for modules in the pcm pipeline
typedef struct mfc_proc_info_t
{
   // Pointer to the process function
   ar_result_t (*process)(void *          me_ptr,
                          capi_buf_t *    input_buf_ptr,
                          capi_buf_t *    output_buf_ptr,
                          mfc_media_fmt_t *input_media_fmt_ptr,
                          mfc_media_fmt_t *output_media_fmt_ptr);

   // Pointer to the output buffer.
   // We point this to the de-interleaved-unpacked buffers part of the mfc_osrc_lib_t struct, because
   // 1.Actual buffers are not available in the init phase
   // 2.Actual buffer ptr values may change
   // 3.We might have to re-map the buffers run time since most of the process functions take deint-unpacked fmt
   capi_buf_t *output_buffer_ptr;

   // Output media format for the module. Input media fmt is obtained form the previous process's output media fmt
   mfc_media_fmt_t output_media_fmt;

} mfc_proc_info_t;

typedef struct mfc_core_lib_t
{
   // Memory pointer to the memory required for channel maps and remap buffers
   void *pc_mem_ptr;

   // Max of input and output number of channels
   uint16_t max_num_channels;

   // Input Media format
   mfc_media_fmt_t input_media_fmt;

   // Output media format
   mfc_media_fmt_t output_media_fmt;

   // Flags to hold information on which module is enabled in the pipeline
   mfc_process_flags_t flags;

   // Number of modules with cannot do processing in-place
   uint32_t no_of_buffering_stages;

   // Process info v-table required to call process of each module
   mfc_proc_info_t pc_proc_info[NUMBER_OF_PROCESS];

   // Temporary de-interleaved-unpacked buffer ptr holder for fwk input
   capi_buf_t *remap_input_buf_ptr;

   // Temporary de-interleaved-unpacked buffer ptr holder for scratch buffer 1
   capi_buf_t *remap_scratch_buf1_ptr;

   // Temporary de-interleaved-unpacked buffer ptr holder for scratch buffer 2
   capi_buf_t *remap_scratch_buf2_ptr;

   // Temporary de-interleaved-unpacked buffer ptr holder for fwk output buffer
   capi_buf_t *remap_output_buf_ptr;
} mfc_core_lib_t;

// Main library structure for pcm converter
typedef struct mfc_osrc_lib_t
{
   mfc_core_lib_t core_lib;

   // log id used for printing
   uint32_t miid;

   // Channel mixer library structure
   ChMixerStateStruct *ch_lib_ptr;

   // OSResampler wrapper instance
   osrc_resampler_lib_t *osrc_rs_lib_ptr;

   // previous resampler position
   mfc_rs_position_flag_t resampler_used;

   uint32_t heap_id;

   // Frame-size state (mirrors original mfc_osrc_lib_t; used by mfc_osrc_set_cntr_frame_size /
   // mfc_osrc_set_consume_partial_input for DM and threshold behavior)
   uint32_t cntr_frame_size_us;
   uint32_t frame_size_us;
   bool_t   consume_partial_input;
} mfc_osrc_lib_t;

// Process function

ar_result_t mfc_osrc_endianness_process(void *          me_ptr,
                                  capi_buf_t *    input_buf_ptr,
                                  capi_buf_t *    output_buf_ptr,
                                  mfc_media_fmt_t *input_media_fmt_ptr,
                                  mfc_media_fmt_t *output_media_fmt_ptr);

ar_result_t mfc_osrc_interleaving_process(void *          me_ptr,
                                    capi_buf_t *    input_buf_ptr,
                                    capi_buf_t *    output_buf_ptr,
                                    mfc_media_fmt_t *input_media_fmt_ptr,
                                    mfc_media_fmt_t *output_media_fmt_ptr);

ar_result_t mfc_osrc_byte_morph_process(void *          me_ptr,
                                  capi_buf_t *    input_buf_ptr,
                                  capi_buf_t *    output_buf_ptr,
                                  mfc_media_fmt_t *input_media_fmt_ptr,
                                  mfc_media_fmt_t *output_media_fmt_ptr);

ar_result_t mfc_osrc_channel_mix_process(void *          me_ptr,
                                   capi_buf_t *    input_buf_ptr,
                                   capi_buf_t *    output_buf_ptr,
                                   mfc_media_fmt_t *input_media_fmt_ptr,
                                   mfc_media_fmt_t *output_media_fmt_ptr);

ar_result_t mfc_osrc_resampler_process(void *          me_ptr,
                                     capi_buf_t *    input_buf_ptr,
                                     capi_buf_t *    output_buf_ptr,
                                     mfc_media_fmt_t *input_media_fmt_ptr,
                                     mfc_media_fmt_t *output_media_fmt_ptr);

ar_result_t mfc_osrc_float_to_fixed_conv_process(void *          me_ptr,
                                           capi_buf_t *    input_buf_ptr,
                                           capi_buf_t *    output_buf_ptr,
                                           mfc_media_fmt_t *input_media_fmt_ptr,
                                           mfc_media_fmt_t *output_media_fmt_ptr);

ar_result_t mfc_osrc_fixed_to_float_conv_process(void *          me_ptr,
                                           capi_buf_t *    input_buf_ptr,
                                           capi_buf_t *    output_buf_ptr,
                                           mfc_media_fmt_t *input_media_fmt_ptr,
                                           mfc_media_fmt_t *output_media_fmt_ptr);

ar_result_t mfc_osrc_intlv_16_out(capi_buf_t *input_buf_ptr,
                            capi_buf_t *output_buf_ptr,
                            uint16_t    word_size_in,
                            uint16_t    q_factor_in);

ar_result_t mfc_osrc_intlv_24_out(capi_buf_t *input_buf_ptr,
                            capi_buf_t *output_buf_ptr,
                            uint16_t    word_size_in,
                            uint16_t    q_factor_in);

ar_result_t mfc_osrc_intlv_32_out(capi_buf_t *input_buf_ptr,
                            capi_buf_t *output_buf_ptr,
                            uint16_t    word_size_in,
                            uint16_t    q_factor_in,
                            uint16_t    q_factor_out);

/** Generates DEINTERLEAVED UNPACKED V1 output i.e updates all the chs data len's*/
ar_result_t mfc_osrc_deintlv_16_out(capi_buf_t *input_buf_ptr,
                              capi_buf_t *output_buf_ptr,
                              uint16_t    num_channels,
                              uint16_t    word_size_in,
                              uint16_t    q_factor_in);

/** Generates DEINTERLEAVED UNPACKED V1 output i.e updates all the chs data len's*/
ar_result_t mfc_osrc_deintlv_24_out(capi_buf_t *input_buf_ptr,
                              capi_buf_t *output_buf_ptr,
                              uint16_t    num_channels,
                              uint16_t    word_size_in,
                              uint16_t    q_factor_in);

/** Generates DEINTERLEAVED UNPACKED V1 output i.e updates all the chs data len's*/
ar_result_t mfc_osrc_deintlv_32_out(capi_buf_t *input_buf_ptr,
                              capi_buf_t *output_buf_ptr,
                              uint16_t    num_channels,
                              uint16_t    word_size_in,
                              uint16_t    q_factor_in,
                              uint16_t    q_factor_out);

/** Generates DEINTERLEAVED UNPACKED V2 output i.e updates only first chs data len's*/
ar_result_t mfc_osrc_deintlv_unpacked_v2_16_out(capi_buf_t *input_buf_ptr,
                                          capi_buf_t *output_buf_ptr,
                                          uint16_t    num_channels,
                                          uint16_t    word_size_in,
                                          uint16_t    q_factor_in);

/** Generates DEINTERLEAVED UNPACKED V2 output i.e updates only first chs data len's*/
ar_result_t mfc_osrc_deintlv_unpacked_v2_24_out(capi_buf_t *input_buf_ptr,
                                          capi_buf_t *output_buf_ptr,
                                          uint16_t    num_channels,
                                          uint16_t    word_size_in,
                                          uint16_t    q_factor_in);

/** Generates DEINTERLEAVED UNPACKED V2 output i.e updates only first chs data len's*/
ar_result_t mfc_osrc_deintlv_unpacked_v2_32_out(capi_buf_t *input_buf_ptr,
                                          capi_buf_t *output_buf_ptr,
                                          uint16_t    num_channels,
                                          uint16_t    word_size_in,
                                          uint16_t    q_factor_in,
                                          uint16_t    q_factor_out);

ar_result_t mfc_osrc_change_endianness(int8_t   *src_ptr,
                                 int8_t   *dest_ptr,
                                 uint32_t  src_actual_len,
                                 uint32_t *dest_actual_len,
                                 uint32_t  word_size);
/* -----------------------------------------------------------------------
 ** API Func
 ** ----------------------------------------------------------------------- */
bool_t mfc_osrc_is_floating_point_data_format_supported();
bool_t mfc_osrc_is_ch_mixer_needed(mfc_osrc_lib_t *      pc_ptr,
                             int16_t *       coef_set_ptr,
                             mfc_media_fmt_t *input_media_fmt_ptr,
                             mfc_media_fmt_t *output_media_fmt_ptr);

ar_result_t mfc_osrc_init(mfc_osrc_lib_t *      pc_ptr,
                    mfc_media_fmt_t *input_media_fmt_ptr,
                    mfc_media_fmt_t *output_media_fmt_ptr,
                    void *          coef_set_ptr,
                    uint32_t        heap_id,
                    bool_t *        lib_enable,
                    uint32_t        miid,
                    uint8_t         type);

void mfc_osrc_deinit(mfc_osrc_lib_t *pc_ptr);

void mfc_osrc_get_kpps_and_bw(mfc_osrc_lib_t *pc_ptr, uint32_t *kpps, uint32_t *bw);

uint32_t mfc_osrc_get_algo_delay(mfc_osrc_lib_t *pc_ptr);

mfc_mf_combo_t mfc_osrc_classify_mf(mfc_media_fmt_t *mf_ptr);

ar_result_t mfc_osrc_process(mfc_osrc_lib_t *  me_ptr,
                       capi_buf_t *input_buf_ptr,
                       capi_buf_t *output_buf_ptr,
                       capi_buf_t *scratch_buf_ptr_1,
                       capi_buf_t *scratch_buf_ptr_2);

uint32_t mfc_osrc_get_fixed_out_samples(mfc_osrc_lib_t *pc_ptr, uint32_t req_out_samples);

void mfc_osrc_set_cntr_frame_size(mfc_osrc_lib_t *pc_ptr, uint32_t cntr_frame_size_us);

void mfc_osrc_set_consume_partial_input(mfc_osrc_lib_t *pc_ptr, bool_t consume_partial_input);

#ifdef __cplusplus
}
#endif /*__cplusplus*/
#endif // MFC_OSRC_CONVERTER_H
