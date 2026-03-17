/**
 * \file capi_mux_demux_utils.h
 *
 * \brief
 *        CAPI utility for mux demux module.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef CAPI_MUX_DEMUX_UTILS_H_
#define CAPI_MUX_DEMUX_UTILS_H_

#include "capi_cmn.h"
#include "capi_intf_extn_data_port_operation.h"
#include "mux_demux_api.h"
#include "posal_intrinsics.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

// #define MUX_DEMUX_TX_DEBUG_INFO

#ifdef SIM
   // Interleaved data is not really supported in Mux, this macro is enabled for profiling automotive use graphs for the fwk MPPS overheads.
   // this macro allows mux demux to accept interleaved data and output interleaved data. Module doesnt really do muxing, it just routes the
   // data from first input to all the outputs.
   #define MUX_DEMUX_INTERLEAVED_DATA_WORKAROUND
#endif

/** Maximum value of a signed 28-bit integer*/
static const int32_t MAX_28 = 0x7FFFFFF;
/** Minimum value of a signed 28-bit integer*/
static const int32_t MIN_28 = -134217728;  //0x8000000

/*----------------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------------*/
#define CHECK_ERR_AND_RETURN(result, error_msg, ...)                                                                   \
   {                                                                                                                   \
      if (CAPI_FAILED(result))                                                                                         \
      {                                                                                                                \
         AR_MSG(DBG_ERROR_PRIO, error_msg, ##__VA_ARGS__);                                                             \
         return result;                                                                                                \
      }                                                                                                                \
   }

#define ONE_Q15_32BIT (1 << 15)

// output media format skeleton
#ifdef PROD_SPECIFIC_MAX_CH
static const capi_media_fmt_v2_t MUX_DEMUX_MEDIA_FMT_V2 = { { {
                                                               CAPI_FIXED_POINT,
                                                            } },
                                                            {
                                                               CAPI_MEDIA_FORMAT_MINOR_VERSION,
                                                               MEDIA_FMT_ID_PCM,
                                                               0, // num_channels
                                                               0, // bits per sample
                                                               0, // q factor
                                                               0, // sampling_rate
                                                               1, // data_is_signed
                                                               CAPI_DEINTERLEAVED_UNPACKED_V2,
                                                            },
                                                            { 1,   2,   3,   4,   5,   6,   7,   8,   9,   10,
                                                              11,  12,  13,  14,  15,  16,  17,  18,  19,  20,
                                                              21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
                                                              31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
                                                              41,  42,  43,  44,  45,  46,  47,  48,  49,  50,
                                                              51,  52,  53,  54,  55,  56,  57,  58,  59,  60,
                                                              61,  62,  63,  64,  65,  66,  67,  68,  69,  70,
                                                              71,  72,  73,  74,  75,  76,  77,  78,  79,  80,
                                                              81,  82,  83,  84,  85,  86,  87,  88,  89,  90,
                                                              91,  92,  93,  94,  95,  96,  97,  98,  99,  100,
                                                              101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
                                                              111, 112, 113, 114, 115, 116, 117, 118, 119, 120,
                                                              121, 122, 123, 124, 125, 126, 127, 128 } };

#else
static const capi_media_fmt_v2_t MUX_DEMUX_MEDIA_FMT_V2 = { { {
                                                               CAPI_FIXED_POINT,
                                                            } },
                                                            {
                                                               CAPI_MEDIA_FORMAT_MINOR_VERSION,
                                                               MEDIA_FMT_ID_PCM,
                                                               0, // num_channels
                                                               0, // bits per sample
                                                               0, // q factor
                                                               0, // sampling_rate
                                                               1, // data_is_signed
                                                               CAPI_DEINTERLEAVED_UNPACKED_V2,
                                                            },
                                                            { 1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
                                                              12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
                                                              23, 24, 25, 26, 27, 28, 29, 30, 31, 32 } };
#endif

static inline uint32_t min_of_two(uint32_t a, uint32_t b)
{
   return (a < b) ? a : b;
}

static inline uint32_t max_of_two(uint32_t a, uint32_t b)
{
   return (a < b) ? b : a;
}

static inline uint32_t bytes_to_samples(uint32_t bytes, uint32_t bits_per_sample)
{
   return (bits_per_sample == 16) ? bytes / 2 : bytes / 4;
}

static inline uint32_t samples_to_bytes(uint32_t samples, uint32_t bits_per_sample)
{
   return (bits_per_sample == 16) ? samples * 2 : samples * 4;
}

/* -----------------------------------------------------------------------
 ** Type Declarations
 ** ----------------------------------------------------------------------- */
/*minimal media format structure for input port */
typedef struct mux_demux_input_media_fmt_t
{
   /*media format received from fwk*/
   uint32_t sample_rate;
   uint32_t bits_per_sample;
   uint32_t q_factor;

   /*true iff port is a candidate of operating media format and has sample rate same as operating sample rate.*/
   bool_t is_valid;
} mux_demux_input_media_fmt_t;

/*minimal media format structure for output port*/
typedef struct mux_demux_output_media_fmt_t
{
   /*based on the connections. */
   uint32_t num_channels;

   /*based on the client configuration. */
   uint32_t num_max_channels;

   /*based on the client setting, if not received then based on the first valid connected input stream. */
   uint32_t bits_per_sample;
   uint32_t q_factor;

   /*based on client setting, if not received then default {1,2,3,4,5,6,....32}. */
   uint16_t channel_type[CAPI_MAX_CHANNELS_V2];

   /* = sample_rate*10 + num_channels*10 + q factor; calculated after raising output media format event.
    * This sum is used to avoid raising media format event even when it is not changed.*/
   uint32_t media_fmt_sum;

#ifdef MUX_DEMUX_INTERLEAVED_DATA_WORKAROUND
   uint32_t data_interleaving;
#endif

} mux_demux_output_media_fmt_t;

/*Connection from an input stream channel to an output stream channel.
 * this structure is per output stream channel. */
typedef struct mux_demux_input_connection_t
{
   /*input port id of the connected input port*/
   uint32_t input_port_index;

   /*channel position of the connected channel.*/
   uint32_t input_channel_index;

   /*weight of the connected input channel.*/
   int32_t coeff_q15;
} mux_demux_input_connection_t;

/*Connection for an output stream channel */
typedef struct mux_demux_channel_connection_t
{
   /*number of input channels connected to this output channel*/
   uint32_t num_of_connected_input_channels;

   /*connection config from each input channel */
   mux_demux_input_connection_t *input_connections_ptr;
} mux_demux_channel_connection_t;

/*Input Port information structure */
typedef struct mux_demux_input_port_info_t
{
   uint32_t                    port_id;
   uint32_t                    port_index;
   intf_extn_data_port_state_t port_state;

   mux_demux_input_media_fmt_t fmt;

   /* flag to indicate if this input port is a candidate for operating media format.
    * following condition must be true for this flag.
    *   1. There is at least one connection with any output port which is in "started" state.
    *   2. A valid media format has been received.
    *   3. Port is started.
    */
   bool_t b_operating_fmt_candidate;

   /* Flag to indicate if eos is propagated from this input port or not.
    */
   bool_t is_eos_moved;

   /*array of num_of_output_ports elements,
    * each element denotes if in-out ports is connected or not */
   bool_t *is_output_connected;
} mux_demux_input_port_info_t;

/*Output Port information structure */
typedef struct mux_demux_output_port_info_t
{
   uint32_t                    port_id;
   uint32_t                    port_index;
   intf_extn_data_port_state_t port_state;

   mux_demux_output_media_fmt_t fmt;

   /*array of fmt.num_channels elements.
    * each element is the list of input stream channels connected with an output stream channel*/
   mux_demux_channel_connection_t *channel_connection_ptr;
} mux_demux_output_port_info_t;

#ifdef SIM
typedef struct capi_mux_demux_tgp_t
{
   fwk_extn_param_id_trigger_policy_cb_fn_t tg_policy_cb;
} capi_mux_demux_tgp_t;
#endif

/*mux demux capiv2 handle structure */
typedef struct capi_mux_demux_t
{
   const capi_vtbl_t *        vtbl;
   uint32_t                   heap_id;
   capi_event_callback_info_t cb_info;

   intf_extn_param_id_metadata_handler_t metadata_handler;

   uint32_t operating_sample_rate;

   uint32_t                     num_of_input_ports;
   mux_demux_input_port_info_t *input_port_info_ptr; // input array index is same as port index

   uint32_t                      num_of_output_ports;
   mux_demux_output_port_info_t *output_port_info_ptr; // output array index can be different from port index

   /*cached configuration */
   param_id_mux_demux_config_t *cached_config_ptr;

   uint32_t miid;

   uint32_t enable_ts_propagation; // Flag to enable/disable TS propagation from input to output buffer

#ifdef MUX_DEMUX_INTERLEAVED_DATA_WORKAROUND
   capi_interleaving_t data_interleaving;
#endif

#ifdef SIM
   capi_mux_demux_tgp_t tgp;
#endif
} capi_mux_demux_t;

uint32_t capi_mux_demux_get_input_port_index_from_port_id(capi_mux_demux_t           *me_ptr,
                                                          uint32_t                     port_id);
uint32_t capi_mux_demux_get_output_arr_index_from_port_id(capi_mux_demux_t           *me_ptr,
                                                           uint32_t                      port_id);

/* caching out config even if port was not opened,
   accessing the config based on arr_index instead of port
   this update was only for output ports only.
*/
uint32_t capi_mux_demux_get_output_arr_index_from_port_index(capi_mux_demux_t           *me_ptr,
                                                             uint32_t                    port_index);

void capi_mux_demux_raise_out_port_media_format_event(capi_mux_demux_t *me_ptr, uint32_t out_port_index);

void       capi_mux_demux_cleanup_port_config(capi_mux_demux_t *me_ptr);
capi_err_t capi_mux_demux_alloc_port_config(capi_mux_demux_t *me_ptr);

capi_err_t capi_mux_demux_port_operation(capi_mux_demux_t *                     me_ptr,
                                         const intf_extn_data_port_operation_t *port_config_ptr);

capi_err_t capi_mux_demux_update_connection(capi_mux_demux_t *me_ptr);

void capi_mux_demux_update_operating_fmt(capi_mux_demux_t *me_ptr);

capi_err_t capi_mux_demux_handle_metadata(capi_mux_demux_t *  me_ptr,
                                          capi_stream_data_t *input[],
                                          capi_stream_data_t *output[]);

capi_err_t capi_mux_demux_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

capi_err_t capi_mux_demux_end(capi_t *_pif);

capi_err_t capi_mux_demux_set_param(capi_t *                _pif,
                                    uint32_t                param_id,
                                    const capi_port_info_t *port_info_ptr,
                                    capi_buf_t *            params_ptr);

capi_err_t capi_mux_demux_get_param(capi_t *                _pif,
                                    uint32_t                param_id,
                                    const capi_port_info_t *port_info_ptr,
                                    capi_buf_t *            params_ptr);

capi_err_t capi_mux_demux_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

capi_err_t capi_mux_demux_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

capi_err_t capi_mux_demux_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);
#if defined(__cplusplus)
}
#endif // __cplusplus

#endif /* CAPI_MUX_DEMUX_UTILS_H_ */
