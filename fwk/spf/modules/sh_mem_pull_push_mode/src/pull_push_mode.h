/* ======================================================================== */
/**
  @file capi_i2s.h
  @brief This file contains function declarations internal to CAPI
         Pull and Push mode module

  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
  SPDX-License-Identifier: BSD-3-Clause-Clear
==============================================================================*/
#ifndef PULL_MODE_H
#define PULL_MODE_H

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#ifndef CAPI_STANDALONE
/* For shared libraries. */
#include "shared_lib_api.h"
#else
#include "capi_util.h"
#endif
#include "posal_timer.h"
#include "sh_mem_pull_push_mode_api.h"
#include "ar_error_codes.h"
#include "capi_types.h"
#include "capi_cmn.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

#define CAPI_PM_KPPS 1325

#define PULL_MODE 0

#define PUSH_MODE 1

#define MAX_EVENT_CLIENTS 4

/* debug message */
#define MIID_UNKNOWN 0
#define PULL_PUSH_MSG_PREFIX "CAPI PM:[%lX] "
#define PULL_PUSH_MSG(ID, xx_ss_mask, xx_fmt, ...)\
         AR_MSG(xx_ss_mask, PULL_PUSH_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

/*------------------------------------------------------------------------
 * Structure definitions
 * ----------------------------------------------------------------------*/
#define QFORMAT_TO_BIT_WIDTH(q) ((PCM_Q_FACTOR_15 == q) ? 16 : ( (PCM_Q_FACTOR_27 == q ) ? 24 : 32) )

typedef struct pm_media_fmt_t
{
   uint32_t fmt_id;
   uint32_t sample_rate;
   uint16_t num_channels;
   uint16_t bit_width; //16,24,32
   uint16_t is_signed;
   uint16_t Q_format;
   uint32_t endianness;
   uint32_t bits_per_sample; //sample word size, 16, 32
   uint32_t alignment;
   capi_interleaving_t data_interleaving;
   uint8_t  channel_map[CAPI_MAX_CHANNELS_V2];
} pm_media_fmt_t;

typedef struct event_client_info_t
{
   uint64_t   dest_addr;
   uint32_t   token;
   uint32_t   event_id;
}event_client_info_t;

typedef struct pull_push_mode_watermark_level_t
{
   uint32_t    watermark_level_bytes;
   /**< Watermark level in bytes*/
}pull_push_mode_watermark_level_t;

typedef struct pull_push_mode_t
{
   uint32_t                                 miid;
   uint32_t                                 mode;
   uint32_t                                 mem_map_client;
   uint32_t                                 num_water_mark_levels;
   uint32_t                                 circ_buf_mem_map_handle;
   uint32_t                                 pos_buf_mem_map_handle;
   uint32_t                                 shared_circ_buf_size;
   uint32_t                                 num_clients_registered;
   event_client_info_t                      event_client_info[MAX_EVENT_CLIENTS];
   uint8_t                                 *shared_circ_buf_start_ptr;
   sh_mem_pull_push_mode_position_buffer_t *shared_pos_buf_ptr;
   pull_push_mode_watermark_level_t        *water_mark_levels_ptr;
   pm_media_fmt_t                           media_fmt;     /**< input media fmt */
   pm_media_fmt_t                           cfg_media_fmt; /**< configured media fmt for push mode */
   capi_buf_t                               scratch_buf[CAPI_MAX_CHANNELS_V2];
   posal_thread_prio_t                      ist_priority;
   bool_t                                   is_disabled;
   bool_t                                   is_mod_buf_access_enabled;
   void                                    *curr_shared_buf_ptr;
   uint32_t                                 next_read_index;
} pull_push_mode_t;

typedef struct capi_pm_media_fmt_t
{
   capi_set_get_media_format_t main;
   capi_standard_data_format_t std;
} capi_pm_media_fmt_t;

typedef struct capi_pm_t
{
   /* v-table pointer */
   capi_t vtbl;

   /* Heap id, used to allocate memory */
   capi_heap_id_t heap_mem;

   /* Call back info for event raising */
   capi_event_callback_info_t cb_info;

   pull_push_mode_t pull_push_mode_info;

   // container frame duration.
   uint32_t frame_dur_us;
} capi_pm_t;

/*------------------------------------------------------------------------
 * Function declarations
 * ----------------------------------------------------------------------*/
static inline void get_frame_size(uint32_t sample_rate, uint32_t *num_samples_ptr)
{
   // Returns 1 sample as a minimum value
   if (sample_rate < 1000)
   {
      *num_samples_ptr = 1;
      return;
   }
   *num_samples_ptr = (sample_rate / 1000);
}

capi_err_t pull_push_mode_init(pull_push_mode_t *pm_ptr, sh_mem_pull_push_mode_cfg_t *init_ptr);

capi_err_t pull_push_mode_check_buf_size(pull_push_mode_t *pm_ptr, pm_media_fmt_t *media_fmt_ptr);

capi_err_t pull_push_mode_set_inp_media_fmt(pull_push_mode_t *pm_ptr,
                                            media_format_t   *media_fmt,
                                            pm_media_fmt_t   *dst_media_fmt_ptr);

bool_t pull_push_check_media_fmt_validity(pull_push_mode_t *pm_ptr);

capi_err_t pull_push_mode_set_fwk_ext_inp_media_fmt(pull_push_mode_t                       *pm_ptr,
                                                    fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr);

capi_err_t capi_pm_raise_event_to_clients(capi_pm_t *me, uint32_t event_id, void *payload_ptr, uint32_t payload_len);

void pull_push_mode_deinit(pull_push_mode_t *pm_ptr);

capi_err_t pull_mode_read_input(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);
capi_err_t push_mode_write_output(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

capi_err_t pull_push_mode_watermark_levels_init(pull_push_mode_t *pm_ptr,
                                                uint32_t          num_water_mark_levels,
                                                event_cfg_sh_mem_pull_push_mode_watermark_level_t *water_mark_levels,
                                                uint32_t                                           heap_id);

capi_err_t pull_module_buf_mgr_extn_return_output_buf(uint32_t    handle,
                                           uint32_t    port_index,
                                           uint32_t   *num_bufs,
                                           capi_buf_t *buffer_ptr);

capi_err_t push_module_buf_mgr_extn_get_input_buf(uint32_t    handle,
                                           uint32_t    port_index,
                                           uint32_t   *num_bufs,
                                           capi_buf_t *buffer_ptr);

capi_err_t pull_push_mode_check_send_watermark_event_util_(capi_pm_t *capi_ptr, uint32_t startLevel, uint32_t endLevel);

static inline capi_err_t pull_push_mode_check_send_watermark_event(capi_pm_t *me_ptr,
                                                                   uint32_t   startLevel,
                                                                   uint32_t   endLevel)
{
   if (me_ptr->pull_push_mode_info.num_water_mark_levels == 0)
   {
      return CAPI_EOK;
   }

   return pull_push_mode_check_send_watermark_event_util_(me_ptr, startLevel, endLevel);
}

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* PULL_MODE_H */
