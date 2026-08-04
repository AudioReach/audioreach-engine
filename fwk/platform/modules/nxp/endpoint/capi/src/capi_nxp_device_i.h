/* ========================================================================
  @file capi_nxp_device_i.h
  @brief This file contains CAPI includes of nxp Device Module

  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
  SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/

#ifndef _CAPI_NXP_DEVICE_I_H
#define _CAPI_NXP_DEVICE_I_H

/*=====================================================================
  Includes
 ======================================================================*/
#include "capi_nxp_device.h"
#include "nxp_device_api.h"
#include "capi_cmn.h"
#include <zephyr/drivers/dai.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/*=====================================================================
  Macros
 ======================================================================*/
#ifndef SIZE_OF_ARRAY
#define SIZE_OF_ARRAY(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef min
#define min(A,B) ((A) < (B) ? (A) : (B))
#endif

 /* Number of CAPI Framework extension needed */
#define NXP_DEVICE_NUM_FRAMEWORK_EXTENSIONS 1

/* Number of milliseconds in a second*/
#define NUM_MS_PER_SEC 1000

#define ALIGN_UP(val, align) (((val) + (align) - 1) & ~((align) - 1))

#define DT_DRV_COMPAT nxp_dai_sai

typedef enum nxp_device_state
{
   NXP_DEVICE_INTERFACE_STOP = 0,
   NXP_DEVICE_INTERFACE_START,
   NXP_DEVICE_READY
} nxp_device_interface_state_t;

typedef enum nxp_device_data_flow_state
{
   DF_STOPPED = 0,
   DF_STARTED = 1,
   DF_STOPPING = 2,
}nxp_device_data_flow_state;

typedef struct i2s_port_state
{
   // interrupt statistics
   int32_t *i2s_intr_cnt_ptr;
   uint32_t  fifo_ovf_cnt;
   uint32_t  ahb_err_cnt;

   // Signal Miss config - For debugging purpose only
   bool_t   is_periodic_signal_miss;
   uint64_t intr_count_pre;  // interrupt count to introduce a pre process signal miss (cntr delay)
   uint64_t intr_count_post; // interrupt count to introduce a post process signal miss (module proc delay)

   //dma_device_handle dma_dev_hdl;

   // callback signal ptr for the interrupt
   void *trigger_signal_ptr;

   /* status of current trigger shared between hwep module and client
    * to differentiate between data and signal triggers.
    * This flag will be set in interrupt callback context to indicate
    * it is a signal trigger
    */
   bool_t signal_trigger_status;

   //uint32_t i2s_ctl; /* Content of I2S control status register */

   // H.W delay in us.
   //uint32_t hw_delay_us;
   /* This field is used to indicate if the frame duration normalization is allowed or not.
    * If the frame duration is outside the range, the frame duration will be normalized to
    * the closest supported range. 

   bool_t   allow_frame_duration_normalization;
   uint32_t min_normalized_frame_dur_us;
   uint32_t max_normalized_frame_dur_us;
   uint32_t normalized_num_dma_buffers;*/
} i2s_port_state_t;

typedef struct i2s_drv_state
{
   i2s_port_state_t i2s_intf_state;
   /* Pointer to individual interface state object*/

} i2s_drv_state_t;

typedef struct capi_nxp_device {
   /* v-table pointer */
   capi_t vtbl;

   /* Heap id, used to allocate memory */
   capi_heap_id_t heap_mem;

   /* Call back info for event raising */
   capi_event_callback_info_t cb_info;

   //struct capi_dai_data dai_data;

   void *user_cb_data;

   i2s_drv_state_t i2s_driver;

   /* Instance ID of this module */
   uint32_t iid;

   uint32_t direction;

   uint32_t log_id;
   /* Unique log ID provided by framework */

   uint32_t log_id_reserved_mask;
   /* bits reserved for this module to use from the LSB*/

    /* pointer to scratch data buffer used in process function */
    int8_t *out_data_buffer;

    /* Size of the scratch data buffer */
    uint32_t out_data_buffer_size;

   void *dma_src_addr; /**< Buffer base address */

   void *dma_dest_addr; //Address of DMA buffer in system heap

   uint32_t dma_buf_size; /**< Runtime buffer size in bytes (period multiple) */

   uint32_t dma_chan_index;

   struct dma_config *z_config;

   int dai_dir; //Different from the CAPI direction

} capi_nxp_device_t;

struct sai_params {
   uint32_t reserved0;

   /* MCLK */
   uint16_t reserved1;
   uint16_t mclk_id;
   uint32_t mclk_direction;

   uint32_t mclk_rate; /* MCLK frequency in Hz */
   uint32_t fsync_rate;
   uint32_t bclk_rate;

   /* TDM */
   uint32_t tdm_slots;
   uint32_t rx_slots;
   uint32_t tx_slots;
   uint16_t tdm_slot_width;
   uint16_t reserved2;  /* alignment */
};

/*------------------------------------------------------------------------
 * VTBL function declarations
 * -----------------------------------------------------------------------*/

capi_vtbl_t *capi_nxp_device_get_vtbl();

capi_err_t capi_nxp_device_process(capi_t *_pif,
                                    capi_stream_data_t *input[],
                                    capi_stream_data_t *output[]);

capi_err_t capi_nxp_device_end(capi_t *_pif);

capi_err_t capi_nxp_device_get_param(capi_t *_pif,
                                      uint32_t param_id,
                                      const capi_port_info_t *port_info_ptr,
                                      capi_buf_t *params_ptr);

capi_err_t capi_nxp_device_set_param(capi_t *_pif,
                                      uint32_t param_id,
                                      const capi_port_info_t *port_info_ptr,
                                      capi_buf_t *params_ptr);

capi_err_t capi_nxp_device_get_properties(capi_t *_pif,
                                           capi_proplist_t *proplist_ptr);

capi_err_t capi_nxp_device_set_properties(capi_t *_pif,
                                           capi_proplist_t *proplist_ptr);

bool_t capi_nxp_device_update_dataflow_state(capi_stream_data_t *input,
                                              nxp_device_data_flow_state *current_state,
                                              bool is_data_sufficient);

bool_t capi_nxp_device_check_data_sufficiency(capi_stream_data_t *input,
                                               capi_buf_t *scratch_buf,
                                               uint32_t total_bytes,
                                               bool_t packetized,
                                               bool_t is_capi_in_media_fmt_set,
                                               uint32_t expected_data_len,
                                               uint16_t num_channels,
                                               bool_t *need_to_underrun);

capi_err_t capi_nxp_device_dai_init(capi_nxp_device_t *me_ptr, uint32_t dir);

capi_err_t capi_nxp_device_init_param(capi_nxp_device_t *me_ptr);

capi_err_t capi_nxp_device_init_dma_buffer(capi_nxp_device_t *me_ptr, uint32_t *pb, uint32_t *pc);

capi_err_t capi_nxp_device_set_dma_config(capi_nxp_device_t *me_ptr, uint32_t period_bytes, uint32_t period_count);

void capi_nxp_device_reset(capi_nxp_device_t *me_ptr);

void capi_nxp_device_stop(capi_nxp_device_t *me_ptr);

void capi_nxp_device_dma_callback(const struct device *dev, void *user_data,
                uint32_t channel, int status);

capi_err_t capi_nxp_device_process_get_properties(capi_nxp_device_t *me_ptr, capi_proplist_t *proplist_ptr);

capi_err_t capi_nxp_device_process_set_properties(capi_nxp_device_t *me_ptr, capi_proplist_t *proplist_ptr);

capi_err_t capi_nxp_device_process_sink(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

#endif /* _CAPI_NXP_DEVICE_I_H */