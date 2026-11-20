/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     -------------------------------------
 * 7/15/26   vasharma    Added file for opensource resampler based MFC
   ========================================================================= */

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/

#ifndef CAPI_MFC_OSRC_UTILS_H
#define CAPI_MFC_OSRC_UTILS_H

#ifndef CAPI_UNIT_TEST
/* For shared libraries. */
#include "shared_lib_api.h"
#include "topo_utils.h"
#else
#include "capi_fwk_extns_pcm.h"
#include "capi_fwk_extns_frame_duration.h"
#endif
#include "capi_cmn.h"
#include "audpp_util.h"
#include "capi.h"
#include "audio_basic_op_ext.h"
#include "common_enc_dec_api.h"
#include "mfc_osrc_api.h"
#include "mfc_osrc_converter.h"

#define CAPI_MFC_OSRC_MAX_IN_PORTS 1
#define CAPI_MFC_OSRC_MAX_OUT_PORTS 1
#define PCM_TRAILING_DELAY 0
#define PCM_DECODER_DELAY 0
#define PRI_IN_PORT_INDEX 0
#define PRI_OUT_PORT_INDEX 0
#define PCM_ENCODER_CONFIG_MAX_FRAME_SIZE_IN_SAMPLES 38400
#define PCM_ENCODER_CONFIG_MAX_FRAME_SIZE_IN_MICROSECOND 100000
#define PCM_ENCODER_CONFIG_FRAME_SIZE_IN_SAMPLES 1
#define PCM_ENCODER_CONFIG_FRAME_SIZE_IN_MICROSECONDS 2
#ifdef CAPI_UNIT_TEST
static inline uint32_t tu_get_unit_frame_size(uint32_t sample_rate)
{
   // Returns 1 sample as a minimum value
   if (sample_rate < 1000)
   {
      return 1;
   }
   return (sample_rate / 1000);
}

#endif

//#define MFC_OSRC_DEBUG
//#define MFC_OSRC_profiling


#define ALIGN_4_BYTES(a) ((a + 3) & (0xFFFFFFFC))

const uint32_t MFC_OSRC_BW                 = 1 * 1024 * 1024;
const uint32_t MFC_OSRC_CAPI_KPPS          = 50;

typedef enum capi_mfc_osrc_result_t { CAPI_PCM_INVALID = 0, CAPI_PCM_VALID, CAPI_PCM_ERROR } capi_mfc_osrc_result_t;

typedef enum capi_mfc_osrc_perf_mode_t {
   CAPI_PCM_PERF_MODE_INVALID     = 0,
   CAPI_PCM_PERF_MODE_LOW_POWER   = 1, // should be same as #define APM_SG_PERF_MODE_LOW_POWER  0x1
   CAPI_PCM_PERF_MODE_LOW_LATENCY = 2  // should be same as #define APM_SG_PERF_MODE_LOW_LATENCY 0x2
} capi_mfc_osrc_perf_mode_t;

typedef struct capi_mfc_osrc_events_config_t
{
   uint32_t kpps;
   uint32_t delay_in_us;
   uint32_t code_bw;
   uint32_t data_bw;
} capi_mfc_osrc_events_config_t;

typedef struct capi_mfc_osrc_chmixer_coef_set_t
{
   uint16_t  num_in_ch;
   uint16_t *in_ch_map;
   uint16_t  num_out_ch;
   uint16_t *out_ch_map;
   int16_t * coef_ptr;
} capi_mfc_osrc_chmixer_coef_set_t;

typedef struct capi_pc_configured_mf_t
{
   data_format_t                   data_format;
   uint32_t                        sampling_rate;
   payload_pcm_output_format_cfg_t fmt;
   uint16_t                        channel_type[CAPI_MAX_CHANNELS_V2];
} capi_pc_configured_mf_t;

typedef struct capi_mfc_osrc_chmixer_config
{
   uint32_t                            num_coef_sets;
   capi_mfc_osrc_chmixer_coef_set_t *coef_sets_ptr;
} capi_mfc_osrc_chmixer_config;

typedef struct capi_mfc_osrc_dm_info_t
{
   // mode
   uint16_t dm_mode;
   // flag to indicate if dm is enabled
   uint16_t is_dm_disabled;
   // for fixed output mode
   uint32_t req_out_samples;
   uint32_t expected_in_samples;
   // for fixed input mode
   uint32_t req_in_samples;
   uint32_t expected_out_samples;

   uint32_t should_consume_partial_input;
} capi_mfc_osrc_dm_info_t;

typedef struct capi_mfc_osrc_profiler
{
   uint64_t start_cycles;
   uint64_t end_cycles;
   uint64_t total_cycles;
   uint64_t sample_count;
   uint64_t average_kpps;
   uint64_t peak_kpps;
   uint32_t sample_rate;
} capi_mfc_osrc_profiler_t;

typedef struct capi_mfc_osrc_t
{
   capi_t                                 vtbl;
   capi_event_callback_info_t             cb_info;
   capi_heap_id_t                         heap_info;
   capi_media_fmt_v2_t                    in_media_fmt[CAPI_MFC_OSRC_MAX_IN_PORTS];
   capi_media_fmt_v2_t                    out_media_fmt[CAPI_MFC_OSRC_MAX_OUT_PORTS];
   fwk_extn_pcm_param_id_media_fmt_extn_t extn_in_media_fmt[CAPI_MFC_OSRC_MAX_IN_PORTS]; //TODO: check and clean
   fwk_extn_pcm_param_id_media_fmt_extn_t extn_out_media_fmt[CAPI_MFC_OSRC_MAX_OUT_PORTS]; //TODO: check and clean
   capi_buf_t                             scratch_buf_1[CAPI_MFC_OSRC_MAX_OUT_PORTS];
   capi_buf_t                             scratch_buf_2[CAPI_MFC_OSRC_MAX_OUT_PORTS];
   capi_pc_configured_mf_t                configured_media_fmt[CAPI_MFC_OSRC_MAX_OUT_PORTS];
   capi_mfc_osrc_chmixer_config         config;
   uint32_t                               coef_payload_size;
   uint32_t                               inp_buf_size_per_ch;
   uint32_t                               out_buf_size_per_ch;
   uint32_t                               max_data_len_per_ch;
   capi_mfc_osrc_events_config_t        events_config;
   capi_mfc_osrc_perf_mode_t            perf_mode;
   
   #ifdef MFC_OSRC_profiling
   capi_mfc_osrc_profiler_t        kpps_profile_data; 
   #endif

   uint32_t                               frame_size_us;
   mfc_osrc_lib_t                               pc[CAPI_MFC_OSRC_MAX_IN_PORTS]; /* osrc mfc_osrc_lib_t from mfc_osrc_converter.h */
   bool_t                                 lib_enable;
   bool_t                                 input_media_fmt_received; // indicates ip mf is received, lib init done and out mf is raised
   uint32_t                               initial_silence_in_samples; //TODO: clean
   uint32_t                               trailing_silence_in_samples; //TODO: clean
   uint32_t                               miid;

   // port states
   // intf_extn_data_port_state_t in_port_state;
   // intf_extn_data_port_state_t out_port_state;

   // fixed input or output mode (if configured) and related data
   capi_mfc_osrc_dm_info_t dm_info;

   /* OSResampler quality parameters — cached from PARAM_ID_MFC_RESAMPLER_CFG.
    * Applied to osrc_pc_lib_t before every lib reinit so they survive across
    * media-format changes. Defaults match OSRC_RS_DEFAULT_* in osrc_pc_converter.h. */
   param_id_mfc_osrc_resampler_cfg_t osrc_resampler_cfg;

   /* Last output media format successfully raised to the framework.
    * Used by capi_mfc_osrc_check_and_raise_output_media_format_event to
    * suppress redundant raises that would otherwise trigger a downstream
    * gen_cntr_handle_port_data_thresh_change mid-inner-loop, breaking the
    * write-buffer return path in non-STM upstream containers. */
   capi_media_fmt_v2_t                    out_media_fmt_raised[CAPI_MFC_OSRC_MAX_OUT_PORTS];
   bool_t                                 out_media_fmt_raised_valid[CAPI_MFC_OSRC_MAX_OUT_PORTS];
} capi_mfc_osrc_t; 

bool_t mfc_osrc_is_supported_media_type(const capi_media_fmt_v2_t *format_ptr);

bool_t mfc_osrc_mfc_is_supported_media_type(const capi_media_fmt_v2_t *format_ptr);

void capi_mfc_osrc_mfc_set_output_bps_qf(capi_pc_configured_mf_t *media_fmt_ptr);

uint32_t   capi_mfc_osrc_mfc_set_extn_inp_mf_bitwidth(uint32_t q_factor);
void       capi_mfc_osrc_mfc_deinit(capi_mfc_osrc_t *me_ptr);
capi_err_t capi_mfc_osrc_check_ch_type(capi_mfc_osrc_t *me_ptr,
                                         const uint16_t *   channel_type,
                                         const uint32_t     array_size);

capi_err_t capi_mfc_osrc_handle_input_media_fmt(capi_mfc_osrc_t *  me_ptr,
                                                  capi_media_fmt_v2_t *temp_media_fmt_ptr,
                                                  uint32_t             port);
capi_err_t capi_mfc_osrc_check_and_raise_output_media_format_event(capi_mfc_osrc_t *me_ptr);

void       capi_mfc_osrc_print_media_fmt(capi_mfc_osrc_t *me_ptr, mfc_media_fmt_t *fmt_ptr, uint32_t port);
capi_err_t capi_mfc_osrc_get_fixed_out_samples(capi_mfc_osrc_t *me_ptr);
capi_err_t capi_mfc_osrc_get_max_in_samples(capi_mfc_osrc_t *me_ptr, uint32_t max_out_samples);

ar_result_t capi_mfc_osrc_lib_init(capi_mfc_osrc_t *me_ptr, bool_t rs_reinit);
capi_err_t  capi_mfc_osrc_allocate_scratch_buffer(capi_mfc_osrc_t *me_ptr, uint32_t max_data_len_per_ch);
bool_t mfc_osrc_is_supported_out_media_type(const payload_pcm_output_format_cfg_t *format_ptr, uint32 data_format);
capi_mfc_osrc_result_t mfc_osrc_is_supported_extn_media_type(
   const fwk_extn_pcm_param_id_media_fmt_extn_t *format_ptr);
capi_err_t capi_mfc_osrc_process_set_properties(capi_mfc_osrc_t *me_ptr, capi_proplist_t *proplist_ptr);
capi_err_t capi_mfc_osrc_process_get_properties(capi_mfc_osrc_t *    me_ptr,
                                                  capi_proplist_t *      proplist_ptr);
capi_err_t capi_mfc_osrc_update_and_raise_events(capi_mfc_osrc_t *me_ptr);
capi_err_t capi_mfc_osrc_raise_process_check_event(capi_mfc_osrc_t *me_ptr);
capi_err_t capi_mfc_osrc_raise_process_event(capi_mfc_osrc_t *me_ptr);

capi_err_t capi_mfc_osrc_handle_data_port_op(capi_mfc_osrc_t *me_ptr, capi_buf_t *params_ptr);

capi_err_t capi_mfc_osrc_realloc_scratch_buf(capi_mfc_osrc_t *me_ptr, capi_buf_t *sb_ptr, uint32_t max_data_len);

capi_err_t capi_mfc_osrc_allocate_scratch_buffer(capi_mfc_osrc_t *me_ptr, uint32_t max_data_len_per_ch);

void capi_mfc_osrc_apply_quality_cfg(capi_mfc_osrc_t *me_ptr);


void capi_mfc_osrc_reset_buffer_len(capi_mfc_osrc_t *me_ptr,
                                      capi_buf_t *       src_buf_ptr,
                                      capi_buf_t *       dest_buf_ptr,
                                      uint32_t           port);

void capi_mfc_osrc_update_dm_disable(capi_mfc_osrc_t *me_ptr);

capi_err_t capi_mfc_osrc_raise_dm_event(capi_mfc_osrc_t *                 me_ptr,
                                          fwk_extn_dm_param_id_req_samples_t *req_samples_ptr,
                                          uint32_t                            event_id);


static inline bool_t capi_mfc_osrc_is_dm_enabled(capi_mfc_osrc_t *me_ptr)
{
   return !me_ptr->dm_info.is_dm_disabled;
}

static inline mfc_interleaving_t capi_interleaved_to_mfc(capi_interleaving_t capi_value)
{
   mfc_interleaving_t pc_interleaved = MFC_UNKNOWN_INTERLEAVING;
   if (CAPI_INTERLEAVED == capi_value)
   {
      pc_interleaved = MFC_INTERLEAVED;
   }
   else if (CAPI_DEINTERLEAVED_PACKED == capi_value)
   {
      pc_interleaved = MFC_DEINTERLEAVED_PACKED;
   }
   else if(CAPI_DEINTERLEAVED_UNPACKED_V2 == capi_value)
   {
      // input media format will only be set with V2, hence no need to V1.
      pc_interleaved = MFC_DEINTERLEAVED_UNPACKED_V2;
   }
   return pc_interleaved;
}

static inline mfc_data_format_t capi_dataformat_to_mfc(data_format_t capi_value)
{
   if (CAPI_FIXED_POINT == capi_value)
   {
      return MFC_FIXED_FORMAT;
   }
   else if (CAPI_FLOATING_POINT == capi_value)
   {
      return MFC_FLOATING_FORMAT;
   }
   return MFC_INVALID_FORMAT;
}

#ifdef MFC_OSRC_profiling
void capi_mfc_osrc_profiling(capi_mfc_osrc_t *me_ptr, uint32_t num_samples);
void capi_mfc_osrc_print_kpps(capi_mfc_osrc_t *me_ptr);
#endif

#endif // CAPI_MFC_OSRC_UTILS_H
