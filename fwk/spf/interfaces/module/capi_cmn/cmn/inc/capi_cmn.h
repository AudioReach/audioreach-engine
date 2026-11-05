#ifndef CAPI_CMN_H
#define CAPI_CMN_H
/**
 * \file capi_cmn.h
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "capi.h"
#include "shared_lib_api.h"
#include "posal_types.h"
#include "module_cmn_api.h"
#include "common_enc_dec_api.h"

/*=====================================================================
  Macros
 ======================================================================*/
#define CAPI_MF_V2_MIN_SIZE (sizeof(capi_standard_data_format_v2_t) + sizeof(capi_set_get_media_format_t))
#define CAPI_RAW_COMPRESSED_MF_MIN_SIZE                                                                                \
   (sizeof(capi_raw_compressed_data_format_t) + sizeof(capi_set_get_media_format_t))
#define CAPI_ALIGN_4_BYTE(x) (((x) + 3) & (0xFFFFFFFC))
#define CAPI_CMN_MAX_IN_PORTS 1
#define CAPI_CMN_MAX_OUT_PORTS 1
#define CAPI_ALIGN_8_BYTE(x) ((((uintptr_t)(x) + 7) >> 3) << 3)
#define CAPI_CMN_IS_PCM_FORMAT(data_format) ((CAPI_FIXED_POINT == data_format) || (CAPI_FLOATING_POINT == data_format))
#define CAPI_CMN_INT32_SIZE_IN_BYTES 4
#define CAPI_CMN_IS_POW_OF_2(x) (!((x) & ((x)-1)))

#define CAPI_CMN_ISLAND_VOTE_EXIT 1
#define CAPI_CMN_ISLAND_VOTE_ENTRY 0

//24 hours in microseconds = 86400000000 (1000*1000*60*60*24)
#define CAPI_CMN_UNDERRUN_TIME_THRESH_US 86400000000
#define CAPI_CMN_STEADY_STATE_UNDERRUN_TIME_THRESH_US (1000*10)

#define CAPI_CMN_UNDERRUN_INFO_RESET(underrun_info) \
   underrun_info.underrun_counter = 0; \
   underrun_info.prev_time = 0;

#define CAPI_CMN_DBG_MSG 1

#define TIMESTAMP_NUM_FRACTIONAL_BITS 10
#define TIMESTAMP_FRACTIONAL_BIT_MASK 0x3FF
#define SIZE_OF_AN_ARRAY(a) (sizeof(a) / sizeof((a)[0]))

#define CAPI_CMN_MSG_PREFIX "CAPI CMN:[%lX] "
#define CAPI_CMN_MSG(ID, xx_ss_mask, xx_fmt, ...)\
         AR_MSG(xx_ss_mask, CAPI_CMN_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

#define CAPI_CMN_MSG_ISLAND(LOGID, PRIO, XX_FMT, ...)                                                                  \
   AR_MSG_ISLAND(PRIO, CAPI_CMN_MSG_PREFIX XX_FMT, LOGID, ##__VA_ARGS__);

// number of channels that can be represented in a word mask
#define CAPI_CMN_CHANNELS_PER_MASK              32
#define CAPI_CMN_CHANNELS_PER_MASK_MINUS_ONE    (CAPI_CMN_CHANNELS_PER_MASK - 1)

// optimal approach for mod operation of x with CHANNELS_PER_MASK
#define CAPI_CMN_MOD_WITH_32(var1) (var1 & CAPI_CMN_CHANNELS_PER_MASK_MINUS_ONE)

// optimal approach for divide operation of x with CHANNELS_PER_MASK
#define CAPI_CMN_DIVIDE_WITH_32(var1) (var1 >> 5)

#define CAPI_CMN_TOTAL_BITS_NEEDED_FOR_INDEX_CFG   PCM_MAX_CHANNEL_MAP_V2

// would need PCM_MAX_CHANNEL_MAP_V2 + 1 bits to store channel type config since
// bit 0 in 1st group is always reserved
#define CAPI_CMN_TOTAL_BITS_NEEDED_FOR_TYPE_CFG   (PCM_MAX_CHANNEL_MAP_V2 + 1)

// Adds the value of (CHANNELS_PER_MASK_MINUS_ONE) to the num_ch to account for rounding up to nearest higher integer value.
// The result is division of the sum by CHANNELS_PER_MASK, which determines the channel group.
#define CAPI_CMN_GET_MAX_CHANNEL_GROUPS_NEEDED(num_ch) ((num_ch + (CAPI_CMN_CHANNELS_PER_MASK_MINUS_ONE)) / CAPI_CMN_CHANNELS_PER_MASK)

// maximum valid groups for channel index array
#define CAPI_CMN_MAX_CHANNEL_INDEX_GROUPS CAPI_CMN_GET_MAX_CHANNEL_GROUPS_NEEDED(CAPI_CMN_TOTAL_BITS_NEEDED_FOR_INDEX_CFG)
const static uint32_t capi_cmn_max_ch_idx_group = CAPI_CMN_MAX_CHANNEL_INDEX_GROUPS;

// maximum valid groups for channel type array
#define CAPI_CMN_MAX_CHANNEL_MAP_GROUPS CAPI_CMN_GET_MAX_CHANNEL_GROUPS_NEEDED(CAPI_CMN_TOTAL_BITS_NEEDED_FOR_TYPE_CFG)
const static uint32_t capi_cmn_max_ch_type_group = CAPI_CMN_MAX_CHANNEL_MAP_GROUPS;

#define CAPI_CMN_CONVERT_TO_32B_MASK(var)   (uint32_t)((uint32_t)1<<var)

//checks if pos bit is set in val
//val and pos should be 32 bit variables.
#define CAPI_CMN_IS_BIT_SET_AT_POS_IN_32B_VAL(val,pos) ((val & (1 << pos)))

#define CAPI_CMN_SET_MASK_32B 0xFFFFFFFF

#ifdef __cplusplus
extern "C" {
#endif

/*=====================================================================
  Structure definitions
 ======================================================================*/
typedef struct capi_media_fmt_v1_t
{
   capi_set_get_media_format_t header;
   capi_standard_data_format_t format;
} capi_media_fmt_v1_t;

typedef struct capi_media_fmt_v2_t
{
   capi_set_get_media_format_t    header;
   capi_standard_data_format_v2_t format;
   uint16_t                       channel_type[CAPI_MAX_CHANNELS_V2];
} capi_media_fmt_v2_t;

typedef struct capi_cmn_raw_media_fmt_t
{
   capi_set_get_media_format_t       header;
   capi_raw_compressed_data_format_t format;
} capi_cmn_raw_media_fmt_t;
typedef struct capi_cmn_deinterleaved_raw_media_fmt_t
{
   capi_set_get_media_format_t                     header;
   capi_deinterleaved_raw_compressed_data_format_t format;
} capi_cmn_deinterleaved_raw_media_fmt_t;

typedef struct capi_basic_prop_t
{
   uint32_t  init_memory_req;
   uint32_t  stack_size;
   uint32_t  num_fwk_extns;
   uint32_t *fwk_extn_ids_arr;
   bool_t    is_inplace;
   bool_t    req_data_buffering;
   uint32_t  max_metadata_size;
} capi_basic_prop_t;

typedef struct capi_cmn_underrun_info_t
{
   uint64_t prev_time;  //Last time stamp when underrun msg was printed
   uint32_t underrun_counter; //This counts the no. of underruns since last it was printed
} capi_cmn_underrun_info_t;

/*=====================================================================
  Utility functions to retrieve certain bits from a flag
 ======================================================================*/
static inline uint32_t capi_get_bits(uint32_t x, uint32_t mask, uint32_t shift)
{
   return (x & mask) >> shift;
}
static inline void capi_set_bits(uint32_t *x_ptr, uint32_t val, uint32_t mask, uint32_t shift)
{
   val    = (val << shift) & mask;
   *x_ptr = (*x_ptr & ~mask) | val;
}
/*=====================================================================
  Utility function defines
 ======================================================================*/

static inline void pick_config_or_input(uint32_t *val, int32_t config_val, uint32_t inp_val)
{
   *val = (PARAM_VAL_NATIVE != config_val) ? config_val : inp_val;
}

static inline void pick_if_not_unset(int16_t *current_value, int16_t config_value)
{
   if (PARAM_VAL_UNSET != config_value)
   {
      *current_value = config_value;
   }
}

static inline intf_extn_data_port_state_t intf_extn_data_port_op_to_port_state(intf_extn_data_port_opcode_t opcode)
{
   switch (opcode)
   {
      case INTF_EXTN_DATA_PORT_OPEN:
      {
         return DATA_PORT_STATE_OPENED;
      }
      case INTF_EXTN_DATA_PORT_START:
      {
         return DATA_PORT_STATE_STARTED;
      }
      case INTF_EXTN_DATA_PORT_STOP:
      {
         return DATA_PORT_STATE_STOPPED;
      }
      case INTF_EXTN_DATA_PORT_CLOSE:
      {
         return DATA_PORT_STATE_CLOSED;
      }
      case INTF_EXTN_DATA_PORT_SUSPEND:
      {
         return DATA_PORT_STATE_SUSPENDED;
      }
      default:
      {
         break;
      }
   }
   return DATA_PORT_STATE_INVALID;
}

static inline void pcm_to_capi_interleaved_with_native_param(capi_interleaving_t *capi_value,
                                                             int16_t              cfg_value,
                                                             capi_interleaving_t  inp_value)
{
   switch (cfg_value)
   {
      case PCM_INTERLEAVED:
      {
         *capi_value = CAPI_INTERLEAVED;
         break;
      }
      case PCM_DEINTERLEAVED_PACKED:
      {
         *capi_value = CAPI_DEINTERLEAVED_PACKED;
         break;
      }
      case PCM_DEINTERLEAVED_UNPACKED:
      {
         *capi_value = CAPI_DEINTERLEAVED_UNPACKED;
         break;
      }
      case PARAM_VAL_NATIVE:
      {
         *capi_value = inp_value;
         break;
      }
      default:
         break;
   }
}

/** This function is used to map interleaved fromat value from PCM_* to CAPI_*.
 *
 *  Important: Use this function only if module supports CAPI_DEINTERLEAVED_UNPACKED_V2. If module
 *             doesnt support unpacked v2, use pcm_to_capi_interleaved_with_native_param()
 */
static inline void pcm_to_capi_interleaved_with_native_param_v2(capi_interleaving_t *capi_value,
                                                                int16_t              cfg_value,
                                                                capi_interleaving_t  inp_value)
{
   switch (cfg_value)
   {
      case PCM_INTERLEAVED:
      {
         *capi_value = CAPI_INTERLEAVED;
         break;
      }
      case PCM_DEINTERLEAVED_PACKED:
      {
         *capi_value = CAPI_DEINTERLEAVED_PACKED;
         break;
      }
      /** Note that here unpacked is mapped to CAPI unpacked V2. */
      case PCM_DEINTERLEAVED_UNPACKED:
      {
         *capi_value = CAPI_DEINTERLEAVED_UNPACKED_V2;
         break;
      }
      case PARAM_VAL_NATIVE:
      {
         *capi_value = inp_value;
         break;
      }
      default:
         break;
   }
}

capi_err_t capi_cmn_set_basic_properties(capi_proplist_t *           proplist_ptr,
                                         capi_heap_id_t *            heap_mem_ptr,
                                         capi_event_callback_info_t *cb_info_ptr,
                                         bool_t                      check_port_info);

capi_err_t capi_cmn_get_basic_properties(capi_proplist_t *proplist_ptr, capi_basic_prop_t *mod_prop_ptr);

capi_err_t capi_cmn_update_algo_delay_event(capi_event_callback_info_t *cb_info_ptr, uint32_t delay_in_us);

capi_err_t capi_cmn_update_kpps_event(capi_event_callback_info_t *cb_info_ptr, uint32_t kpps);

capi_err_t capi_cmn_update_bandwidth_event(capi_event_callback_info_t *cb_info_ptr,
                                           uint32_t                    code_bandwidth,
                                           uint32_t                    data_bandwidth);

capi_err_t capi_cmn_update_hw_acc_proc_delay_event(capi_event_callback_info_t *cb_info_ptr, uint32_t delay_in_us);

capi_err_t capi_cmn_update_process_check_event(capi_event_callback_info_t *cb_info_ptr, uint32_t process_check);

capi_err_t capi_cmn_update_port_data_threshold_event(capi_event_callback_info_t *cb_info_ptr,
                                                     uint32_t                    threshold_bytes,
                                                     bool_t                      is_input_port,
                                                     uint32_t                    port_index);

capi_err_t capi_cmn_handle_get_port_threshold(capi_prop_t *prop_ptr, uint32_t threshold);

capi_err_t capi_cmn_output_media_fmt_event_v1(capi_event_callback_info_t *cb_info_ptr,
                                              capi_media_fmt_v1_t *       out_media_fmt,
                                              bool_t                      is_input_port,
                                              uint32_t                    port_index);

capi_err_t capi_cmn_output_media_fmt_event_v2(capi_event_callback_info_t *cb_info_ptr,
                                              capi_media_fmt_v2_t *       out_media_fmt,
                                              bool_t                      is_input_port,
                                              uint32_t                    port_index);

capi_err_t capi_cmn_raw_output_media_fmt_event(capi_event_callback_info_t *cb_info_ptr,
                                               capi_cmn_raw_media_fmt_t *  out_media_fmt,
                                               bool_t                      is_input_port,
                                               uint32_t                    port_index);
capi_err_t capi_cmn_deinterleaved_raw_media_fmt_event(capi_event_callback_info_t *            cb_info_ptr,
                                                      capi_cmn_deinterleaved_raw_media_fmt_t *out_media_fmt,
                                                      bool_t                                  is_input_port,
                                                      uint32_t                                port_index);

capi_err_t capi_cmn_raise_data_to_dsp_svc_event(capi_event_callback_info_t *cb_info_ptr,
                                                uint32_t                    event_id,
                                                capi_buf_t *                event_buf);

capi_err_t capi_cmn_init_media_fmt_v1(capi_media_fmt_v1_t *media_fmt_ptr);

capi_err_t capi_cmn_init_media_fmt_v2(capi_media_fmt_v2_t *media_fmt_ptr);

capi_err_t capi_cmn_handle_get_output_media_fmt_v1(capi_prop_t *prop_ptr, capi_media_fmt_v1_t *media_fmt_ptr);

capi_err_t capi_cmn_handle_get_output_media_fmt_v2(capi_prop_t *prop_ptr, capi_media_fmt_v2_t *media_fmt_ptr);
static inline capi_err_t capi_cmn_validate_client_pcm_general_media_format(const payload_media_fmt_pcm_t *pcm_fmt_ptr);
capi_err_t capi_cmn_validate_client_pcm_float_media_format(const payload_media_fmt_pcm_t *pcm_fmt_ptr);
capi_err_t capi_cmn_validate_client_pcm_media_format(const payload_media_fmt_pcm_t *pcm_fmt_ptr);
capi_err_t capi_cmn_validate_client_pcm_output_cfg(const payload_pcm_output_format_cfg_t *pcm_cfg_ptr);
capi_err_t capi_cmn_validate_client_pcm_float_output_cfg(const payload_pcm_output_format_cfg_t *pcm_cfg_ptr);
bool_t capi_cmn_media_fmt_equal(capi_media_fmt_v2_t *media_fmt_1_ptr, capi_media_fmt_v2_t *media_fmt_2_ptr);
capi_err_t capi_cmn_data_fmt_map(uint32_t *in_format, capi_media_fmt_v2_t *media_fmt);

capi_err_t capi_cmn_raise_dm_disable_event(capi_event_callback_info_t *cb_info,
                                           uint32_t                    module_log_id,
                                           uint32_t                    disable);
capi_err_t capi_cmn_raise_dynamic_inplace_event(capi_event_callback_info_t *cb_info_ptr, bool_t is_inplace);

capi_err_t capi_cmn_populate_trigger_ts_payload(capi_buf_t *             params_ptr,
                                                stm_latest_trigger_ts_t *ts_struct_ptr,
                                                stm_get_ts_fn_ptr_t      func_ptr,
                                                void *                   cntxt_ptr);

capi_err_t capi_cmn_gapless_remove_zeroes(uint32_t *            bytes_to_remove_per_channel_ptr,
                                          capi_media_fmt_v2_t * out_mf_ptr,
                                          capi_stream_data_t *  output[],
                                          bool_t                initial,
                                          module_cmn_md_list_t *metadata_list_ptr);

capi_err_t capi_cmn_dec_handle_metadata(capi_stream_data_v2_t *                in_stream_ptr,
                                        capi_stream_data_v2_t *                out_stream_ptr,
                                        intf_extn_param_id_metadata_handler_t *metadata_handler_ptr,
                                        module_cmn_md_list_t **                internal_md_list_pptr,
                                        uint32_t *                             in_len_before_process,
                                        capi_media_fmt_v2_t *                  out_media_fmt_ptr,
                                        uint32_t                               dec_algo_delay,
                                        capi_err_t                             process_result);

void capi_cmn_dec_update_buffer_end_md(capi_stream_data_v2_t *in_stream_ptr,
                                       capi_stream_data_v2_t *out_stream_ptr,
                                       capi_err_t *           agg_process_result,
                                       bool_t *               error_recovery_done);

capi_err_t capi_cmn_raise_island_vote_event(capi_event_callback_info_t *cb_info_ptr, bool_t island_vote);
capi_err_t capi_cmn_raise_deinterleaved_unpacked_v2_supported_event(capi_event_callback_info_t *cb_info_ptr);

/**
* unlike capi_cmn_div_num this function is optimal in code-size but doesn't optimize power of 2 divisions
*/
static inline uint32_t capi_cmn_divide(uint32_t num, uint32_t den)
{
   return (num / den);
}

/**
 * Returns needed size for capi v2 media format for passed in number of channels. Useful for
 * get properties validation.
 */
static inline uint32_t capi_cmn_media_fmt_v2_required_size(uint32_t num_channels)
{
   return sizeof(capi_media_fmt_v1_t) + (num_channels * sizeof(capi_channel_type_t));
}

#define CAPI_CMN_CEIL(x, y) (((x) + (y)-1) / (y))
#define CAPI_CMN_BITS_TO_BYTES(x) ((x) >> 3)
#define CAPI_CMN_BYTES_TO_BITS(x) ((x) << 3)
static const uint32_t NUM_US_PER_SEC = 1000000L;
static const uint32_t NUM_NS_PER_SEC = 1000000000L;
static const uint32_t NUM_NS_PER_US  = 1000;
#define CAPI_ZERO_IF_NULL(ptr) ((NULL == ptr) ? 0 : *ptr)

static inline uint32_t capi_cmn_us_to_samples(uint64_t time_us, uint64_t sample_rate)
{
   return CAPI_CMN_CEIL((time_us * sample_rate), NUM_US_PER_SEC);
}

static inline uint32_t capi_cmn_us_to_bytes_per_ch(uint64_t time_us, uint32_t sample_rate, uint16_t bits_per_sample)
{
   uint32_t bytes = 0;
   uint32_t b     = CAPI_CMN_BITS_TO_BYTES(bits_per_sample);

   uint32_t samples = capi_cmn_us_to_samples(time_us, sample_rate);

   bytes = samples * b;

   return (uint32_t)bytes;
}

static inline uint32_t capi_cmn_us_to_bytes(uint64_t time_us,
                                            uint32_t sample_rate,
                                            uint16_t bits_per_sample,
                                            uint32_t num_channels)
{
   uint32_t bytes = capi_cmn_us_to_bytes_per_ch(time_us, sample_rate, bits_per_sample);
   bytes          = bytes * num_channels;
   return bytes;
}

static inline uint32_t capi_cmn_bytes_to_samples_per_ch(uint32_t bytes, uint16_t bits_per_sample, uint32_t num_channels)
{
   uint32_t samples = 0;
   if ((num_channels != 0) && (bits_per_sample != 0))
   {
      uint32_t bytes_for_all_ch = ((uint64_t)num_channels * CAPI_CMN_BITS_TO_BYTES(bits_per_sample));

      samples = capi_cmn_divide(bytes, bytes_for_all_ch);
   }

   return samples;
}

static inline uint64_t capi_cmn_bytes_to_us_optimized(uint32_t  bytes,
                                                      uint32_t  num_channels,
                                                      uint32_t  sample_rate,
                                                      uint8_t   bits_per_sample,
                                                      uint64_t *fract_time_ptr)
{

   uint64_t time_us         = 0;
   uint64_t time_pns        = 0;
   uint32_t time_per_sample = 0;
   bool_t   use_optimized   = FALSE;
   use_optimized            = CAPI_CMN_IS_POW_OF_2(num_channels) && CAPI_CMN_IS_POW_OF_2(bits_per_sample);

   if (use_optimized)
   {
      typedef struct sr_multipliers_t
      {
         uint32_t sample_rate;     ///< Sampling Rate
         uint32_t time_per_sample; ///< 1 / (Sampling Rate) microsecs (Q25 format)
      } sr_multipliers_t;

      const sr_multipliers_t sr_multipliers[] = {
         { 48000, 699050667U },  { 44100, 760871474U },  { 8000, 4194304000U },  { 11025, 3043485895U },
         { 12000, 2796202667U }, { 16000, 2097152000U }, { 22050, 1521742948U }, { 24000, 1398101334U },
         { 32000, 1048576000U }, { 88200, 380435737U },  { 96000, 349525334U },  { 144000, 233016889U },
         { 176400, 190217869U }, { 192000, 174762667U }, { 352800, 95108935U },  { 384000, 87381334U },
      };

      for (uint16_t i = 0; i < SIZE_OF_AN_ARRAY(sr_multipliers); i++)
      {
         if (sr_multipliers[i].sample_rate == sample_rate)
         {
            time_per_sample = sr_multipliers[i].time_per_sample;
            // uint64_t temp2  = 0; //((uint64_t)(0x1) << 24); // rounding

            uint64_t temp1_bytes_us_q25 = (uint64_t)bytes * (uint64_t)time_per_sample;

            // -3 for bits to byte conversion, 25 is from Q25
            uint32_t right_shifts =
               s32_get_lsb_s32((uint32_t)num_channels) + (s32_get_lsb_s32((uint32_t)bits_per_sample) - 3) + 25;

            /* Time in pns is computed in three steps ideally,
                3 steps:
                  1. Compute temp1_bytes_pns_q25 = = temp1_bytes_us_q25 << TIMESTAMP_NUM_FRACTIONAL_BITS;
                  2. Compute right_shifts
                  3. compute time_pns = (temp1_bytes_pns_q25 >> right_shifts);

               Instead of compute temp1_bytes_pns_q25 we can combine step 1 and 3 into a single right shift operation on
               temp1_bytes_us_q25. Note that right_shifts is always greater than TIMESTAMP_NUM_FRACTIONAL_BITS i.e 10
            */
            time_pns = (temp1_bytes_us_q25 >> (right_shifts - TIMESTAMP_NUM_FRACTIONAL_BITS));

            goto _update_fract_ts_ptr;
         }
      }
   }
   {
      /* If use_optimized is false, use bps to compute time_pns */
      uint64_t total_byte_us_per_s = ((uint64_t)bytes * NUM_US_PER_SEC);
      total_byte_us_per_s          = total_byte_us_per_s << TIMESTAMP_NUM_FRACTIONAL_BITS;
      uint64_t bps                 = ((uint64_t)num_channels * sample_rate * CAPI_CMN_BITS_TO_BYTES(bits_per_sample));

#ifdef FLOATING_POINT_DEFINED
      double floating_time_pns = (((double)total_byte_us_per_s) / bps);
      time_pns                 = (uint64_t)floating_time_pns;
#else

      time_pns = (total_byte_us_per_s / bps);
#endif
   }
_update_fract_ts_ptr:
   /* Cnvt pns to micro seconds */
   if (fract_time_ptr)
   {
      time_pns += *fract_time_ptr;
      *fract_time_ptr = time_pns & TIMESTAMP_FRACTIONAL_BIT_MASK;
   }
   time_us = time_pns >> TIMESTAMP_NUM_FRACTIONAL_BITS;
   return time_us;
}

/**
 * includes previous fractional time, and calculates new fractional time in fract_time_ptr
 * Unit of *fract_time_ptr is ns
 *
 * Must be called only for PCM or packetized
 */
static inline uint64_t capi_cmn_bytes_to_us(uint32_t  bytes,
                                            uint32_t  sample_rate,
                                            uint16_t  bits_per_sample,
                                            uint32_t  num_channels,
                                            uint64_t *fract_time_ptr)
{
   uint64_t time_us = 0;
   if ((num_channels != 0) && (sample_rate != 0) && (bits_per_sample != 0))
   {
      time_us = capi_cmn_bytes_to_us_optimized(bytes, num_channels, sample_rate, bits_per_sample, fract_time_ptr);
   }
   return time_us;
}

/* Determine if the sampling rate is fractional.*/
static inline bool_t is_sample_rate_fractional(uint32_t sample_rate)
{
   return (sample_rate % 1000) > 0 ? TRUE : FALSE;
}

/**
 * returns p/q where q has high chance of being power of 2
    * unlike capi_cmn_divide this function is optimized for power of 2 div at the expense of code size.
 */
static inline uint32_t capi_cmn_div_num(uint32_t num, uint32_t den)
{
   if (CAPI_CMN_IS_POW_OF_2(den))
   {
      return s32_shr_s32_sat(num, s32_get_lsb_s32(den));
   }
      return capi_cmn_divide(num, den);
}
/* returns the number of set bis in num */
static inline uint32_t capi_cmn_count_set_bits(uint32_t num)
{
   uint32_t count = 0;
   while (num)
   {
      num &= (num - 1);
      count++;
   }
   return count;
}

/*utility to calculate mask array size dependent on group mask*/
static inline uint32_t capi_cmn_multi_ch_per_config_increment_size(uint32_t group_mask, uint32_t per_config_size)
{
   return (per_config_size + (capi_cmn_count_set_bits(group_mask) * CAPI_CMN_INT32_SIZE_IN_BYTES));
} 

//tests index(0 to 31) bit in var1 and result in position of idx'th bit among the set bits from lsb
static inline uint32_t capi_cmn_count_set_bits_in_lower_n_bits(uint32_t var, uint32_t n)
{
   uint32_t set_bits_before_N = var & ((1 << n) - 1);
   uint32_t pos_of_Nth_bit = capi_cmn_count_set_bits(set_bits_before_N);
   return pos_of_Nth_bit;
}

static inline void capi_cmn_crash(void)
{
#if defined(SIM)
   *((volatile uint32_t *)0) = 0;
#else
   return;
#endif
}

void capi_cmn_check_print_underrun(capi_cmn_underrun_info_t *underrun_info_ptr, uint32_t iid);

void capi_cmn_check_print_underrun_multiple_threshold(capi_cmn_underrun_info_t *underrun_info_ptr,
                                                      uint32_t                  iid,
                                                      bool                      need_to_reduce_underrun_print,
                                                      bool_t                    marker_eos,
                                                      bool_t                    is_capi_in_media_fmt_set);

capi_err_t capi_cmn_check_payload_validation(uint32_t miid,
		                                     uint32_t ch_type_group_mask,
	                                         uint32_t per_cfg_payload_size,
										     uint32_t count,
										     uint32_t param_size,
										     uint32_t *config_size_ptr,
										     uint32_t *required_size_ptr);

bool_t capi_cmn_check_v2_channel_mask_duplication(uint32_t  miid,
		                                          uint32_t  config,
		                                          uint32_t  channel_group_mask,
		                                          uint32_t* temp_mask_list_ptr,
		                                          uint32_t* current_channel_mask_arr_ptr,
												  uint32_t* check_channel_mask_arr_ptr,
												  uint32_t* offset_ptr,
												  uint32_t  per_cfg_base_payload_size);

capi_err_t capi_cmn_check_and_update_intf_extn_status(uint32_t    num_supported_extensions,
                                                      uint32_t   *module_supported_extns_list,
                                                      capi_buf_t *payload_ptr);


capi_err_t capi_cmn_intf_extn_event_module_input_buffer_reuse(uint32_t                    log_id,
                                                              capi_event_callback_info_t *cb_info_ptr,
                                                              uint32_t                    port_index,
                                                              bool_t                      is_enable,
                                                              uint32_t                    buffer_mgr_cb_handle,
                                                              intf_extn_get_module_input_buf_func_t get_input_buf_fn);


capi_err_t capi_cmn_intf_extn_event_module_output_buffer_reuse(uint32_t                    log_id,
                                                              capi_event_callback_info_t *cb_info_ptr,
                                                              uint32_t                    port_index,
                                                              bool_t                      is_enable,
                                                              uint32_t                    buffer_mgr_cb_handle,
                                                              intf_extn_return_module_output_buf_func_t return_output_buf_fn);

capi_err_t capi_cmn_intf_extn_event_module_port_buffer_reuse_v2(uint32_t                    log_id,
                                                              capi_event_callback_info_t *cb_info_ptr,
                                                              uint32_t                    port_index,
                                                              bool_t                      is_input,
                                                              intf_extn_event_id_module_buffer_access_enable_v2_t *cfg_ptr);

#ifdef AVS_BUILD_SOS
#include "spf_dyn_loading_func_mapping.h"

#endif

#ifdef __cplusplus
}
#endif
#endif // CAPI_CMN_H
