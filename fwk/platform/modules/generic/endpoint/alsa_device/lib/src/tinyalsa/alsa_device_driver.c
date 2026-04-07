/* ========================================================================
  @file alsa_device_driver.c
  @brief This file contains tinyalsa based driver implementation of ALSA device.

  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
  SPDX-License-Identifier: BSD-3-Clause-Clear
==============================================================================*/

#include "alsa_device_driver_i.h"
#include "alsa_device_driver.h"

/* Number of milliseconds in a second*/
#define NUM_MS_PER_SEC 1000

ar_result_t alsa_device_driver_set_cfg(alsa_device_driver_t *alsa_device_driver_ptr, param_id_hw_ep_mf_t *alsa_device_cfg_ptr)
{
   if ((NULL == alsa_device_driver_ptr) || (NULL == alsa_device_cfg_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "ALSA_DEVICE_DRIVER: Pointer to alsa device handle/cfg_ptr pointer is null or driver is not in INIT state");
      return AR_EFAILED;
   }

   struct pcm_config *config = &(alsa_device_driver_ptr->config);

   config->channels = alsa_device_cfg_ptr->num_channels;
   config->rate = alsa_device_cfg_ptr->sample_rate;
   if (alsa_device_cfg_ptr->bit_width == 32)
      config->format = PCM_FORMAT_S32_LE;
   else if (alsa_device_cfg_ptr->bit_width == 24)
      config->format = PCM_FORMAT_S24_3LE;
   else if (alsa_device_cfg_ptr->bit_width == 16)
      config->format = PCM_FORMAT_S16_LE;

   AR_MSG(DBG_HIGH_PRIO,
          "ALSA_DEVICE_DRIVER: set_cfg called with configuration: channels: %d, sample_rate: %d, bit_width: %d\n",
          alsa_device_cfg_ptr->num_channels,
          alsa_device_cfg_ptr->sample_rate,
          alsa_device_cfg_ptr->bit_width);

   return AR_EOK;
}

ar_result_t alsa_device_driver_init(alsa_device_driver_t *alsa_device_driver_ptr)
{
   alsa_device_driver_ptr->card_id = 0;
   alsa_device_driver_ptr->device_id = 0;

   //  Initialize PCM config
   struct pcm_config *config = &(alsa_device_driver_ptr->config);
   config->channels = 0;
   config->rate = 0;
   config->format = PCM_FORMAT_INVALID;
   config->start_threshold = 0;
   config->stop_threshold = 0;
   config->silence_threshold = 0;
   config->period_count = 0;
   config->period_size = 0;

   return AR_EOK;
}

ar_result_t alsa_device_driver_open(alsa_device_driver_t *alsa_device_driver_ptr, uint32_t direction)
{
   uint32_t flags = 0;
   if (direction == ALSA_DEVICE_SINK)
   {
      flags = PCM_OUT;
   }
   else // ALSA_DEVICE_SOURCE
   {
      flags = PCM_IN;
   }

   AR_MSG(DBG_HIGH_PRIO, "ALSA_DEVICE_DRIVER: pcm_open called with config: sr: %d, ch: %d, fmt: %d period_cnt: %d, period_sz: %d\n",
          alsa_device_driver_ptr->config.rate,
          alsa_device_driver_ptr->config.channels,
          alsa_device_driver_ptr->config.format,
          alsa_device_driver_ptr->config.period_count,
          alsa_device_driver_ptr->config.period_size);

   alsa_device_driver_ptr->pcm = pcm_open(alsa_device_driver_ptr->card_id, alsa_device_driver_ptr->device_id, flags, &(alsa_device_driver_ptr->config));
   if (!alsa_device_driver_ptr->pcm || !pcm_is_ready(alsa_device_driver_ptr->pcm))
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: Unable to open PCM device %u (%s)\n",
             alsa_device_driver_ptr->device_id,
             pcm_get_error(alsa_device_driver_ptr->pcm));
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO, "ALSA_DEVICE_DRIVER: pcm open for card_id: %d, device_id: %u success.\n",
          alsa_device_driver_ptr->card_id,
          alsa_device_driver_ptr->device_id);

   // Log the ACTUAL config after pcm_open and warn if hardware changed it
   AR_MSG(DBG_HIGH_PRIO, "ALSA_DEVICE_DRIVER: AFTER pcm_open - period_size=%d, rate=%d, channels=%d",
          alsa_device_driver_ptr->config.period_size,
          alsa_device_driver_ptr->config.rate,
          alsa_device_driver_ptr->config.channels);

   return AR_EOK;
}

ar_result_t alsa_device_driver_start(alsa_device_driver_t *alsa_device_driver_ptr)
{
   if (pcm_start(alsa_device_driver_ptr->pcm) < 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: pcm start failed with error: %s\n",
             pcm_get_error(alsa_device_driver_ptr->pcm));
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO, "ALSA_DEVICE_DRIVER: pcm start success.\n");

   return AR_EOK;
}

ar_result_t alsa_device_driver_prepare(alsa_device_driver_t *alsa_device_driver_ptr)
{
   if (pcm_prepare(alsa_device_driver_ptr->pcm) < 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: pcm prepare failed with error: %s\n",
             pcm_get_error(alsa_device_driver_ptr->pcm));
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO, "ALSA_DEVICE_DRIVER: pcm prepare success.\n");

   return AR_EOK;
}

ar_result_t alsa_device_driver_read(alsa_device_driver_t *alsa_device_driver_ptr,
                                    int8_t *buffer_ptr,
                                    uint32_t num_bytes)
{
   if (!alsa_device_driver_ptr || !alsa_device_driver_ptr->pcm || !buffer_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: Invalid parameters for read");
      return AR_EBADPARAM;
   }

   if (pcm_read(alsa_device_driver_ptr->pcm, buffer_ptr, num_bytes) < 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: pcm_read failed: %s",
             pcm_get_error(alsa_device_driver_ptr->pcm));
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO, "ALSA_DEVICE_DRIVER: pcm_read success, bytes: %d", num_bytes);
   return AR_EOK;
}

ar_result_t alsa_device_driver_write(alsa_device_driver_t *alsa_device_driver_ptr, int8_t *buffer_ptr, uint32_t num_bytes)
{
   if (pcm_write(alsa_device_driver_ptr->pcm, buffer_ptr, num_bytes) < 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: pcm write failed with error: %s\n",
             pcm_get_error(alsa_device_driver_ptr->pcm));
      return AR_EFAILED;
   }

   //AR_MSG(DBG_HIGH_PRIO, "ALSA_DEVICE_DRIVER: pcm write success \n");

   return AR_EOK;
}

ar_result_t alsa_device_driver_stop(alsa_device_driver_t *alsa_device_driver_ptr)
{
   if (pcm_stop(alsa_device_driver_ptr->pcm) < 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: pcm stop failed with error: %s\n",
             pcm_get_error(alsa_device_driver_ptr->pcm));
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO, "ALSA_DEVICE_DRIVER: pcm stop success.\n");

   return AR_EOK;
}

ar_result_t alsa_device_driver_close(alsa_device_driver_t *alsa_device_driver_ptr)
{
   if (pcm_close(alsa_device_driver_ptr->pcm) < 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: pcm close failed with error: %s\n",
             pcm_get_error(alsa_device_driver_ptr->pcm));
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO, "ALSA_DEVICE_DRIVER: pcm close success.\n");

   return AR_EOK;
}

ar_result_t alsa_device_driver_set_intf_cfg(param_id_alsa_device_intf_cfg_t *alsa_device_cfg_ptr,
                                            alsa_device_driver_t *alsa_device_driver_ptr)
{
   if ((NULL == alsa_device_driver_ptr) || (NULL == alsa_device_cfg_ptr))
   {
     AR_MSG(DBG_ERROR_PRIO,
         "ALSA_DEVICE_DRIVER: Pointer to alsa device handle/cfg_ptr pointer is null or driver is not in INIT state");
     return AR_EFAILED;
   }

   // set the intf conf.
   alsa_device_driver_ptr->card_id = alsa_device_cfg_ptr->card_id;
   alsa_device_driver_ptr->device_id = alsa_device_cfg_ptr->device_id;
   alsa_device_driver_ptr->config.period_count = alsa_device_cfg_ptr->period_count;
   alsa_device_driver_ptr->config.start_threshold = alsa_device_cfg_ptr->start_threshold;
   alsa_device_driver_ptr->config.stop_threshold = alsa_device_cfg_ptr->stop_threshold;
   alsa_device_driver_ptr->config.silence_threshold = alsa_device_cfg_ptr->silence_threshold;

   AR_MSG(DBG_HIGH_PRIO,
          "ALSA_DEVICE_DRIVER: set_intf_cfg called with period cnt: %d, card_id: %d, device_id %d, start threshold %d, stop threshold %d, silence threshold %d \n",
          alsa_device_driver_ptr->config.period_count,
          alsa_device_driver_ptr->card_id,
          alsa_device_driver_ptr->device_id,
          alsa_device_driver_ptr->config.start_threshold,
          alsa_device_driver_ptr->config.stop_threshold,
          alsa_device_driver_ptr->config.silence_threshold);

   return AR_EOK;
}

ar_result_t alsa_device_driver_set_frame_size_cfg(param_id_frame_size_factor_t *alsa_device_cfg_ptr,
                                                  alsa_device_driver_t *alsa_device_driver_ptr)
{
   if ((NULL == alsa_device_driver_ptr) || (NULL == alsa_device_cfg_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "ALSA_DEVICE_DRIVER: Pointer to alsa device handle/cfg_ptr pointer is null or driver is not in INIT state");
      return AR_EFAILED;
   }

   struct pcm_config *config = &(alsa_device_driver_ptr->config);

   // Compute period_size from frame size factor(in ms).
   alsa_device_driver_ptr->config.period_size =
      (alsa_device_driver_ptr->config.rate / NUM_MS_PER_SEC) *
      alsa_device_cfg_ptr->frame_size_factor;

   AR_MSG(DBG_HIGH_PRIO,
          "ALSA_DEVICE_DRIVER: set_frame_size_cfg: frame_size=%d ms, period_size=%d frames @ %d Hz",
          alsa_device_cfg_ptr->frame_size_factor,
          alsa_device_driver_ptr->config.period_size,
          alsa_device_driver_ptr->config.rate);

   return AR_EOK;
}

ar_result_t alsa_device_driver_wait(alsa_device_driver_t *alsa_device_driver_ptr)
{
   // Enhanced parameter validation
   if (!alsa_device_driver_ptr || !alsa_device_driver_ptr->pcm)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: Invalid parameters for wait");
      return AR_EBADPARAM;
   }

   struct pcm_config *config = &alsa_device_driver_ptr->config;

   if (config->rate == 0 || config->period_size == 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: Invalid PCM config - rate: %d, period_size: %d",
             config->rate, config->period_size);
      return AR_EBADPARAM;
   }

   // Calculate timeout dynamically: (period_size / sample_rate) * 1000 * 2
   // 2x safety factor (one period to fill + one for safety)
   uint32_t timeout_ms = (config->period_size * 1000 * 2) / config->rate;

   // Wait for frames to be available
   int ret = pcm_wait(alsa_device_driver_ptr->pcm, timeout_ms);

   if (ret == 0)
   {
      // Timeout - no frames available within timeout period
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: pcm_wait timeout after %d ms", timeout_ms);
      return AR_ETIMEOUT;
   }
   else if (ret < 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "ALSA_DEVICE_DRIVER: pcm_wait error %d", ret);
      return AR_EFAILED;
   }

   return AR_EOK;
}
