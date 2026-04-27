/* ========================================================================
  @file capi_alsa_device_i.h
  @brief This file contains CAPI includes of AlSA Device Module

  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
  SPDX-License-Identifier: BSD-3-Clause-Clear
==============================================================================*/

#ifndef _CAPI_ALSA_DEVICE_I_H
#define _CAPI_ALSA_DEVICE_I_H

/*=====================================================================
  Includes
 ======================================================================*/
#include "capi_alsa_device.h"
#include "alsa_device_api.h"
#include "capi_cmn.h"
#include "alsa_device_driver.h"

/*=====================================================================
  Macros
 ======================================================================*/
#ifndef SIZE_OF_ARRAY
#define SIZE_OF_ARRAY(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef min
#define min(A,B) ((A) < (B) ? (A) : (B))
#endif

 /* Number of CAPI Framework extension needed
Note: this module is not defined as Signal Triggered Module */
#define ALSA_DEVICE_NUM_FRAMEWORK_EXTENSIONS_SOURCE 1
#define ALSA_DEVICE_NUM_FRAMEWORK_EXTENSIONS_SINK 0

/* Number of milliseconds in a second*/
#define NUM_MS_PER_SEC 1000

typedef enum alsa_device_state
{
   ALSA_DEVICE_INTERFACE_STOP = 0,
   ALSA_DEVICE_INTERFACE_START
} alsa_device_interface_state_t;

typedef enum alsa_device_data_flow_state
{
   DF_STOPPED = 0,
   DF_STARTED = 1,
   DF_STOPPING = 2,
}alsa_device_data_flow_state;

typedef struct capi_alsa_device
{
   /* v-table pointer */
   capi_t vtbl;

   /* Heap id, used to allocate memory */
   capi_heap_id_t heap_mem;

   /* Call back info for event raising */
   capi_event_callback_info_t cb_info;

   /* pointer to scratch data buffer used in process function */
   int8_t *out_data_buffer;

   /* Size of the scratch data buffer */
   uint32_t out_data_buffer_size;

   /* media fmt_struct */
   capi_media_fmt_v2_t gen_cntr_alsa_device_media_fmt;

   /* Instance ID of this module */
   uint32_t iid;

   uint32_t direction;

   bool_t need_to_underrun;

   capi_cmn_underrun_info_t underrun_info;

   /*bool to check if the input mf is received*/
   bool_t is_capi_in_media_fmt_set;

   /*enum maintaining data flow state for sink side*/
   alsa_device_data_flow_state df_state;

   /* port state  */
   alsa_device_interface_state_t state;
   bool_t ep_mf_received;
   uint16_t bit_width;
   uint32_t bytes_per_channel;
   uint16_t q_format_shift_factor;
   uint32_t sample_rate;
   uint32_t num_channels;
   uint32_t data_format;
   bool_t frame_size_cfg_received;
   uint16_t frame_size_ms;
   uint32_t int_samples_per_period;

   //H.W delay in us.
   uint32_t hw_delay_us;

   alsa_device_driver_t alsa_device_driver;

   void *   signal_ptr; // Signal ptr for STM
   uint32_t enable_stm;

   /* Thread management for source direction */
   posal_thread_t dma_wait_thread;    // Thread handle for DMA waiting
   bool_t is_thread_running;          // Thread running state
   bool_t exit_thread;                // Thread exit flag

   /* Buffer to store the data read form ALSA driver in wait thread */
   int8_t *read_buffer;               // Buffer to hold one period of captured data
   uint32_t read_buffer_size;         // Size of read buffer in bytes
   bool_t data_ready;                 // Flag: data available in read buffer
} capi_alsa_device_t;

/*------------------------------------------------------------------------
 * VTBL function declarations
 * -----------------------------------------------------------------------*/

capi_vtbl_t *capi_alsa_device_get_vtbl();

capi_err_t capi_alsa_device_process(capi_t *_pif,
                                    capi_stream_data_t *input[],
                                    capi_stream_data_t *output[]);

capi_err_t capi_alsa_device_end(capi_t *_pif);

capi_err_t capi_alsa_device_get_param(capi_t *_pif,
                                      uint32_t param_id,
                                      const capi_port_info_t *port_info_ptr,
                                      capi_buf_t *params_ptr);

capi_err_t capi_alsa_device_set_param(capi_t *_pif,
                                      uint32_t param_id,
                                      const capi_port_info_t *port_info_ptr,
                                      capi_buf_t *params_ptr);

capi_err_t capi_alsa_device_get_properties(capi_t *_pif,
                                           capi_proplist_t *proplist_ptr);

capi_err_t capi_alsa_device_set_properties(capi_t *_pif,
                                           capi_proplist_t *proplist_ptr);

bool_t capi_alsa_device_update_dataflow_state(capi_stream_data_t *input,
                                              alsa_device_data_flow_state *current_state,
                                              bool is_data_sufficient);

bool_t capi_alsa_device_check_data_sufficiency(capi_stream_data_t *input,
                                               capi_buf_t *scratch_buf,
                                               uint32_t total_bytes,
                                               bool_t packetized,
                                               bool_t is_capi_in_media_fmt_set,
                                               uint32_t expected_data_len,
                                               uint16_t num_channels,
                                               bool_t *need_to_underrun);

// Perform alsa device media format configuration
ar_result_t capi_alsa_device_set_hw_ep_mf_cfg(param_id_hw_ep_mf_t *alsa_device_cfg_ptr,
                                              capi_t *_pif);

// Perform alsa device operating frame size configuration
ar_result_t capi_alsa_device_set_frame_size_cfg(param_id_frame_size_factor_t *alsa_device_cfg_ptr,
                                                capi_t *_pif);

#endif /* _CAPI_ALSA_DEVICE_I_H */
