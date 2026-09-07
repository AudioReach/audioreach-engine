/* ========================================================================
  @file capi_nxp_device_utils.c
  @brief This file contains library implementation of nxp device Module

  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
  SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/

/*=====================================================================
  Includes
 ======================================================================*/
#include "capi_nxp_device_i.h"
#include <fsl_sai.h>
#include <zephyr/cache.h>

#define PLATFORM_DCACHE_ALIGN 32

/* 16-bit interleaved stereo sine: 48 frames x (L,R) = 96 int16 = 192 bytes.
 * Mono source sample duplicated into L and R slots for the 2-slot SAI frame. */
#define BUF_SIZE 96
static int16_t sine_buf[BUF_SIZE] = {
	0x0000, 0x0000, 0x10b5, 0x10b5, 0x2120, 0x2120, 0x30fb, 0x30fb,
	0x4000, 0x4000, 0x4deb, 0x4deb, 0x5a82, 0x5a82, 0x658c, 0x658c,
	0x6ed9, 0x6ed9, 0x7641, 0x7641, 0x7ba3, 0x7ba3, 0x7ee7, 0x7ee7,
	0x7fff, 0x7fff, 0x7ee7, 0x7ee7, 0x7ba3, 0x7ba3, 0x7641, 0x7641,
	0x6ed9, 0x6ed9, 0x658c, 0x658c, 0x5a82, 0x5a82, 0x4deb, 0x4deb,
	0x4000, 0x4000, 0x30fb, 0x30fb, 0x2120, 0x2120, 0x10b5, 0x10b5,
	0x0000, 0x0000, 0xef4a, 0xef4a, 0xdedf, 0xdedf, 0xcf04, 0xcf04,
	0xc000, 0xc000, 0xb214, 0xb214, 0xa57d, 0xa57d, 0x9a73, 0x9a73,
	0x9126, 0x9126, 0x89be, 0x89be, 0x845c, 0x845c, 0x8118, 0x8118,
	0x8000, 0x8000, 0x8118, 0x8118, 0x845c, 0x845c, 0x89be, 0x89be,
	0x9126, 0x9126, 0x9a73, 0x9a73, 0xa57d, 0xa57d, 0xb214, 0xb214,
	0xc000, 0xc000, 0xcf04, 0xcf04, 0xdedf, 0xdedf, 0xef4a, 0xef4a,
};

extern char _end[], _heap_sentry[];
#define heapmem ((uint8_t *)ALIGN_UP((uintptr_t)_end, PLATFORM_DCACHE_ALIGN))

//Heap for dma_buffer
static struct k_heap kernel_heap;

//const struct device *dai_devs[] = { DT_FOREACH_STATUS_OKAY(DT_NODELABEL(nxp_dai_sai), DEVICE_DT_GET(DT_NODELABEL(nxp_dai_sai))) };
const struct device *dma_device = DEVICE_DT_GET(DT_NODELABEL(sdma3));
const struct device *dai_dev = DEVICE_DT_GET(DT_NODELABEL(sai3));

/*------------------------------------------------------------------------
  Function name: capi_nxp_device_common_init
  DESCRIPTION: Initialize the CAPIv2 nxp_device module and library.
  This function can allocate memory.
  -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_common_init(capi_t *_pif, capi_proplist_t *init_set_properties, uint32_t dir)
{
   capi_err_t capi_result = CAPI_EOK;
   //AR_MSG(DBG_ERROR_PRIO,
   //         "CAPI_NXP_DEVICE: Init enter");

   if (NULL == _pif || NULL == init_set_properties)
   {
      AR_MSG(DBG_ERROR_PRIO,
            "CAPI_NXP_DEVICE: Init received bad pointer, 0x%lx",
            (uint32_t)_pif);
      return CAPI_EBADPARAM;
   }

   capi_nxp_device_t *me_ptr = (capi_nxp_device_t *)_pif;
   memset((void *)me_ptr, 0, sizeof(capi_nxp_device_t));

   // Allocate vtbl
   me_ptr->vtbl.vtbl_ptr = capi_nxp_device_get_vtbl();

   // Cache direction
   me_ptr->direction = 0; //sink
   //me_ptr->i2s_media_fmt.format.data_interleaving = CAPI_DEINTERLEAVED_UNPACKED;

   capi_result = capi_nxp_device_process_set_properties(me_ptr, init_set_properties);
   capi_result ^= (capi_result & CAPI_EUNSUPPORTED); // ignore unsupported
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: init set properties failed");
      return capi_result;
   }

   //me_ptr->drift_info.heap_id = (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id;

   capi_result = capi_nxp_device_dai_init(me_ptr, DAI_DIR_TX);
   if (capi_result < 0) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Failed to initialize dai");
      return CAPI_EFAILED;
   }

   capi_result = capi_nxp_device_init_param(me_ptr);
   if (capi_result < 0) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Failed to initialize device parameters");
      return CAPI_EFAILED;
   }
   
   AR_MSG(DBG_ERROR_PRIO, "rc: %d", capi_result);
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_nxp_device_dai_init
  DESCRIPTION: Initialize the CAPIv2 nxp_device dai.
  -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_dai_init(capi_nxp_device_t *me_ptr, uint32_t dir)
{
   capi_err_t capi_result = CAPI_EOK;
   int ret = 0;
   struct dai_config cfg;
   struct sai_params *sai_cfg_params;
   const void* cfg_params;

   if (NULL == me_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
            "CAPI_NXP_DEVICE: Init received null property");
      return CAPI_EBADPARAM;
   }

   ret = dai_config_get(dai_dev, &cfg, DAI_DIR_TX);
   if (ret != 0) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Failed to get dai configuration rc: %d", ret);
      capi_result = CAPI_EFAILED;
      goto end;
   }

   if (dai_probe(dai_dev)) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Failed to probe dai");
      capi_result = CAPI_EFAILED;
      goto end;
   }

   sai_cfg_params = posal_memory_malloc(sizeof(struct sai_params), (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
   if (NULL == sai_cfg_params) {
      AR_MSG(DBG_ERROR_PRIO,
            "CAPI_NXP_DEVICE: Failed to allocate sai configuration pointer");
      capi_result = CAPI_ENOMEMORY;
      goto end;
   }

   me_ptr->dai_dir = dir;

   //Set the configuration
   cfg.dai_index = 3;
   //cfg.format = 1;
   cfg.format = DAI_PROTO_I2S;
   cfg.options = 1;
   cfg.rate = 48000;
   cfg.type = DAI_IMX_SAI;
   cfg.channels  = 2;
   cfg.word_size = 16;
   // Fill cfg_params
   sai_cfg_params->mclk_id = 0;
   sai_cfg_params->mclk_direction = 0;
   sai_cfg_params->mclk_rate = 12288000;
   sai_cfg_params->fsync_rate = 48000;
   sai_cfg_params->bclk_rate = 1536000; //(48000 x 16 x 2)
   sai_cfg_params->tdm_slots = 2;
   sai_cfg_params->rx_slots = 0x3;
   sai_cfg_params->tx_slots = 0x3;
   sai_cfg_params->tdm_slot_width = 16;
   cfg_params = sai_cfg_params;
   ret = dai_config_set(dai_dev, &cfg, cfg_params);
   if (ret) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: dai_config_set failed %d", ret);
      return CAPI_EFAILED;
   }

end:
   if (sai_cfg_params)
      posal_memory_free(sai_cfg_params);

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_nxp_device_init_param
  DESCRIPTION: Sets either a parameter value or a parameter structure containing
  multiple parameters. In the event of a failure, the appropriate error code is
  returned.
  -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_init_param(capi_nxp_device_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   uint32_t period_bytes = 0;
   uint32_t period_count = 0;

   capi_result = capi_nxp_device_init_dma_buffer(me_ptr, &period_bytes, &period_count);
   if (capi_result < 0) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Failed to initialize dma buffer");
      return CAPI_EFAILED;
   }

   capi_result = capi_nxp_device_set_dma_config(me_ptr, period_bytes, period_count);
   if (capi_result < 0) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Failed to set dma config");
      return CAPI_EFAILED;
   }

   return capi_result;
}

capi_err_t capi_nxp_device_init_dma_buffer(capi_nxp_device_t *me_ptr, uint32_t *pb, uint32_t *pc)
{
   capi_err_t capi_result = CAPI_EOK;
   //struct capi_dai_data dai_data = me_ptr->dai_data;
   struct dai_config cfg;
   int ret = 0;
   uint32_t channels = 2;   /* 2 slots on the wire (WM8960 stereo frame) */
   uint32_t sample_bytes, frame_size, period_bytes, period_count;
   uint32_t align = 0, addr_align, buf_size;
   size_t heap_size = 0;

   ret = dai_config_get(dai_dev, &cfg, DAI_DIR_TX);
   if (ret) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Failed to get dai configuration");
      return CAPI_EFAILED;
   }

   //Get the DMA_ATTR_BUFFER_ADDRESS_ALIGNMENT
   ret = dma_get_attribute(dma_device, DMA_ATTR_BUFFER_ADDRESS_ALIGNMENT, &addr_align); //128
   if (ret < 0) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Failed to get dma address align");
      return CAPI_EFAILED;
   }

   //Get the DMA_ATTR_BUFFER_SIZE_ALIGNMENT
   ret = dma_get_attribute(dma_device, DMA_ATTR_BUFFER_SIZE_ALIGNMENT, &align); // 4
   if (ret < 0) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Failed to get dma align");
      return CAPI_EFAILED;
   }

   //Calculate frame size, period size, and period count from the values in cfg
   sample_bytes = 2;   /* 16-bit sample */
   frame_size = sample_bytes * channels;
   period_bytes = 192; /* 48 samples x 2 bytes x 2 slots (1ms @ 48kHz) */
   period_count = 2;
   buf_size = ALIGN_UP(period_count * period_bytes, align);
   *pb = period_bytes;
   *pc = period_count;
   me_ptr->dma_buf_size = buf_size;

   AR_MSG(DBG_ERROR_PRIO, "dma_buf_size %u", buf_size);
   //If dma buffer exists, change the dma_buffer size to the period size
   if (me_ptr->dma_dest_addr) {
      //Check if the dma_buffer is the same size as the period size, otherwise resize it

      //Use sys_heap_realloc?
      AR_MSG(DBG_ERROR_PRIO, "empty block called");
   } else {
      /* heap_size must cover: sys_heap metadata + alignment padding + buf_size
       * sys_heap overhead  ~ 2 * CHUNK_UNIT (64 bytes on 32-bit)
       * alignment padding  = addr_align - 1 (up to 127 bytes for 128-byte align)
       * Use buf_size * 2 as a safe upper bound.
       */
      heap_size = (size_t)(buf_size * 2);
      AR_MSG(DBG_ERROR_PRIO,
             "sys_heap_init: base=%p size=%u buf_size=%u addr_align=%u",
             heapmem, heap_size, buf_size, addr_align);
      sys_heap_init(&kernel_heap.heap, heapmem, heap_size);

      me_ptr->dma_src_addr = sys_heap_aligned_alloc(&kernel_heap.heap, addr_align, buf_size);
      AR_MSG(DBG_ERROR_PRIO,
             "sys_heap_aligned_alloc: ptr=%p align=%u size=%u %s",
             me_ptr->dma_src_addr, addr_align, buf_size,
             me_ptr->dma_src_addr ? "OK" : "FAILED - NULL returned");
      if (NULL == me_ptr->dma_src_addr) {
         AR_MSG(DBG_ERROR_PRIO,
               "CAPI_NXP_DEVICE: Failed to allocate DMA destination buffer");
         return CAPI_ENOMEMORY;
      }
   }
   AR_MSG(DBG_ERROR_PRIO, "init_dma_buffer exit");
   return capi_result;
}

capi_err_t capi_nxp_device_set_dma_config(capi_nxp_device_t *me_ptr, uint32_t period_bytes, uint32_t period_count)
{
   capi_err_t capi_result = CAPI_EOK;
   struct dma_config *dma_cfg = NULL;
   struct dma_block_config *dma_block_cfg = NULL;
   struct dma_block_config *prev = NULL;
   int ret, channel;
   uint32_t fifo, burst_elems, max_block_count, buf_size;

   const struct dai_properties *props = dai_get_properties(dai_dev, DAI_DIR_TX, 0);
   uint32_t hs = props->dma_hs_id;
   burst_elems = props->fifo_depth;
   fifo = props->fifo_address;
   channel = hs & GENMASK(7, 0);

   AR_MSG(DBG_ERROR_PRIO, "channel %d", channel);
   
   ret = dma_get_attribute(dma_device, DMA_ATTR_MAX_BLOCK_COUNT, &max_block_count); //2
   if (ret < 0 || !max_block_count) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Failed to get block count");
      capi_result = CAPI_EFAILED;
      goto end;
   }

   if (max_block_count < period_count) {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: unsupported period count %d, max_block_count %d", period_count, max_block_count);
      buf_size = period_count * period_bytes;
      do {
         if (IS_ALIGNED(buf_size, max_block_count)) {
            period_count = max_block_count;
            period_bytes = buf_size / period_count;
            break;
         } else {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: alignment error for buf_size = %d, block count = %d",
                 buf_size, max_block_count);
         }
      } while (--max_block_count > 0);
   }

   dma_cfg = posal_memory_malloc(sizeof(struct dma_config), (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
   if (NULL == dma_cfg) {
      AR_MSG(DBG_ERROR_PRIO,
            "CAPI_NXP_DEVICE: Failed to allocate dma configuration pointer");
      capi_result = CAPI_ENOMEMORY;
      goto end;
   }


   dma_cfg->channel_direction = MEMORY_TO_PERIPHERAL;
   dma_cfg->source_data_size = 4;
   dma_cfg->dest_data_size = dma_cfg->source_data_size;
   dma_cfg->source_burst_length = 4;
   dma_cfg->dest_burst_length = dma_cfg->source_burst_length;
   dma_cfg->cyclic = 1;
   dma_cfg->user_data = me_ptr;
   dma_cfg->dma_callback = &capi_nxp_device_dma_callback; //Set to dma callback function
   dma_cfg->block_count = max_block_count;
   dma_cfg->dma_slot = (hs & GENMASK(15, 8)) >> 8;

   dma_block_cfg = posal_memory_malloc(sizeof(struct dma_block_config) * dma_cfg->block_count, (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
   if (NULL == dma_block_cfg) {
      AR_MSG(DBG_ERROR_PRIO,
            "CAPI_NXP_DEVICE: Failed to allocate DMA blocks");
      capi_result = CAPI_ENOMEMORY;
      goto end;
   }

   dma_cfg->head_block = dma_block_cfg;
   for (int i = 0; i < dma_cfg->block_count; i++) {
      dma_block_cfg->dest_scatter_en = 0;
      dma_block_cfg->block_size = period_bytes;
      dma_block_cfg->source_address = (uint32_t)me_ptr->dma_src_addr + i * period_bytes;
      dma_block_cfg->dest_address = fifo;
      dma_block_cfg->source_addr_adj = DMA_ADDR_ADJ_DECREMENT;
      dma_block_cfg->dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
      AR_MSG(DBG_ERROR_PRIO,
             "DMA BD[%d]: block_cfg=%p src=0x%08x dst=0x%08x size=%u src_adj=%d dst_adj=%d",
             i, dma_block_cfg,
             dma_block_cfg->source_address,
             dma_block_cfg->dest_address,
             dma_block_cfg->block_size,
             dma_block_cfg->source_addr_adj,
             dma_block_cfg->dest_addr_adj);
      prev = dma_block_cfg;
      prev->next_block = ++dma_block_cfg;
   }
   if (prev)
      prev->next_block = dma_cfg->head_block;

   me_ptr->z_config = dma_cfg;

   channel = dma_request_channel(dma_device, &channel);
   if (channel < 0) {
      AR_MSG(DBG_ERROR_PRIO,
            "CAPI_NXP_DEVICE: DMA request channel failed");
      capi_result = CAPI_EFAILED;
      goto end;
   }
   me_ptr->dma_chan_index = channel;

   ret = dma_config(dma_device, me_ptr->dma_chan_index, dma_cfg);
   if (ret < 0) {
      AR_MSG(DBG_ERROR_PRIO,
            "CAPI_NXP_DEVICE: dma_config failed");
      capi_result = CAPI_EFAILED;
      goto end;
   }

   end:
   if (capi_result != 0) {
      if (dma_block_cfg)
         posal_memory_free(dma_block_cfg);
      if (dma_cfg)
         posal_memory_free(dma_cfg);

      if (me_ptr->dma_src_addr)
         sys_heap_free(&kernel_heap.heap, me_ptr->dma_src_addr);
   }
   AR_MSG(DBG_ERROR_PRIO, "set_dma_config exit");

   return capi_result;
}

void capi_nxp_device_reset(capi_nxp_device_t *me_ptr)
{
   if (me_ptr->z_config->head_block)
      posal_memory_free(me_ptr->z_config->head_block);

   if (me_ptr->z_config)
      posal_memory_free(me_ptr->z_config);

   if (me_ptr->dma_src_addr)
      sys_heap_free(&kernel_heap.heap, me_ptr->dma_src_addr);
}

void capi_nxp_device_stop(capi_nxp_device_t *me_ptr)
{
   //Stop the DMA and the DAI
   dma_stop(dma_device, me_ptr->dma_chan_index);
   dai_trigger(dai_dev, DAI_DIR_TX, DAI_TRIGGER_STOP);
}

/*---------------------------------------------------------------------
  Function name: capi_nxp_device_dma_callback
  DESCRIPTION: Will be called by the DMA driver when it needs more data
  -----------------------------------------------------------------------*/

void capi_nxp_device_dma_callback(const struct device *dev, void *user_data,
                uint32_t channel, int status) {
   capi_nxp_device_t *me_ptr;
   static uint32_t cb_count = 0;
   cb_count++;

   //AR_MSG(DBG_ERROR_PRIO, "NXP CB [#%u]: ch=%u status=%d", cb_count, channel, status);

   if (user_data)
      me_ptr = (capi_nxp_device_t *) user_data;
   else {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: User data is NULL");
      return;
   }

   /* --- SDMA register state --- */
   /*{
      SDMAARM_Type *sdma = (SDMAARM_Type *)0x30E00000;
      AR_MSG(DBG_ERROR_PRIO,
             "NXP CB [#%u] SDMA: CHNENBL[5]=0x%08x EVTOVR=0x%08x "
             "HOSTOVR=0x%08x STOP_STAT=0x%08x INTR=0x%08x CHNPRI[1]=%u",
             cb_count,
             sdma->CHNENBL[5],
             sdma->EVTOVR,
             sdma->HOSTOVR,
             sdma->STOP_STAT,
             sdma->INTR,
             sdma->SDMA_CHNPRI[1]);
   }*/

   /* --- SAI FIFO watermark vs burst size --- */
   {
      I2S_Type *sai = (I2S_Type *)0x30c30000;
      uint32_t tcsr = sai->TCSR;
      uint32_t tcr1 = sai->TCR1;  /* TX FIFO watermark in TCR1[2:0] */
      AR_MSG(DBG_ERROR_PRIO,
             "NXP CB [#%u] SAI: TCSR=0x%08x TCR1=0x%08x "
             "FRF=%d FEF=%d TE=%d FRDE=%d",
             cb_count, tcsr, tcr1,
             !!(tcsr & I2S_TCSR_FRF_MASK),
             !!(tcsr & I2S_TCSR_FEF_MASK),
             !!(tcsr & I2S_TCSR_TE_MASK),
             !!(tcsr & I2S_TCSR_FRDE_MASK));
   }

   /* --- BD source addresses (cache coherency check) --- */
   /*{
      struct dma_block_config *bd0 = me_ptr->z_config->head_block;
      struct dma_block_config *bd1 = bd0 ? bd0->next_block : NULL;
      AR_MSG(DBG_ERROR_PRIO,
             "NXP CB [#%u] BD: bd[0].src=0x%08x bd[0].dst=0x%08x "
             "bd[1].src=0x%08x bd[1].dst=0x%08x",
             cb_count,
             bd0 ? bd0->source_address : 0,
             bd0 ? bd0->dest_address   : 0,
             bd1 ? bd1->source_address : 0,
             bd1 ? bd1->dest_address   : 0);
   }*/

   /* Trigger the waiting thread to tell the container we need more data */
   if (me_ptr->i2s_driver.i2s_intf_state.trigger_signal_ptr) {
      AR_MSG(DBG_ERROR_PRIO, "NXP CB [#%u]: sending signal ptr=%p",
             cb_count, me_ptr->i2s_driver.i2s_intf_state.trigger_signal_ptr);
      posal_signal_send((posal_signal_t)me_ptr->i2s_driver.i2s_intf_state.trigger_signal_ptr);
   } else {
      AR_MSG(DBG_ERROR_PRIO, "NXP CB [#%u]: trigger_signal_ptr is NULL", cb_count);
   }
}

/*---------------------------------------------------------------------
  Function name: capi_nxp_device_process_sink
  DESCRIPTION: Processes an input buffer and generates an output buffer.
  -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_process_sink(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t capi_result = CAPI_EOK;
   capi_nxp_device_t *me_ptr = (capi_nxp_device_t *)_pif;
   struct dma_status stat;
   int ret = 0;
   uint16_t port = 0;
   uint16_t i = 0;
   uint32_t free_bytes = 0;
   uint32_t buf_data_len = 0;

   static uint32_t sink_count = 0;
   sink_count++;

   //AR_MSG(DBG_ERROR_PRIO, "process_sink [#%u]", sink_count);

   //Get data sizes and read/write buffer positions from the DMA
   ret = dma_get_status(dma_device, me_ptr->dma_chan_index, &stat);
   if (ret != 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "dma_get_status failed rc: %d", ret);
      capi_result = CAPI_EFAILED;
      return capi_result;
   }

   /* Periodic SAI register dump every 10 calls to catch stall */
   if ((sink_count % 10) == 1) {
      I2S_Type *sai_base = (I2S_Type *)0x30c30000;
      uint32_t tcsr = sai_base->TCSR;
      uint32_t tcr3 = sai_base->TCR3;
      AR_MSG(DBG_ERROR_PRIO,
             "SINK [#%u] SAI: TCSR=0x%08x TCR3=0x%08x "
             "TE=%d FEF=%d FWF=%d FRF=%d FRDE=%d",
             sink_count, tcsr, tcr3,
             !!(tcsr & I2S_TCSR_TE_MASK),
             !!(tcsr & I2S_TCSR_FEF_MASK),
             !!(tcsr & I2S_TCSR_FWF_MASK),
             !!(tcsr & I2S_TCSR_FRF_MASK),
             !!(tcsr & I2S_TCSR_FRDE_MASK));
   }

   free_bytes = stat.free; //The amount of free bytes in the output DMA buffer
   buf_data_len = input[port]->buf_ptr[i].actual_data_len;

   //AR_MSG(DBG_ERROR_PRIO, "free_bytes: %u, buf_data_len: %u, stat.wr_pos:%u", free_bytes, buf_data_len, stat.write_position);

   /* Reload gating: only copy + flush + reload when at least one full period
    * (192 bytes) of space is free in the DMA ring. */
   if (free_bytes >= 192) {
      /* Write sine data to the correct ring buffer slot using write_position.
       * stat.write_position is the offset within dma_src_addr where the
       * CPU should write next — this ensures each BD gets fresh data.
       */
      uint8_t *write_addr = (uint8_t *)me_ptr->dma_src_addr + stat.write_position;
      memcpy(write_addr, sine_buf, 192);
      /* Flush the CPU-written data so SDMA reads fresh bytes from DRAM (dcache is enabled) */
      sys_cache_data_flush_range(write_addr, 192);
      /*AR_MSG(DBG_ERROR_PRIO,
             "process_sink: wrote 192 bytes to %p (base=%p wr_pos=%u)",
             write_addr, me_ptr->dma_src_addr, stat.write_position);*/

      dma_reload(dma_device, me_ptr->dma_chan_index, 0, 0, 192);
   } else {
      AR_MSG(DBG_ERROR_PRIO, "process_sink: skip reload, free_bytes %u < 192", free_bytes);
   }

   AR_MSG(DBG_ERROR_PRIO, "process_sink exit");
   
   return capi_result;
}


capi_err_t capi_nxp_device_process_get_properties(capi_nxp_device_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   uint32_t fwk_extn_ids[1] = { 0 };
   fwk_extn_ids[0]          = FWK_EXTN_STM;

   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req = sizeof(capi_nxp_device_t);
   mod_prop.stack_size = NXP_DEVICE_STACK_SIZE;
   mod_prop.num_fwk_extns = NXP_DEVICE_NUM_FRAMEWORK_EXTENSIONS;
   mod_prop.fwk_extn_ids_arr = fwk_extn_ids;
   mod_prop.is_inplace = 0;       // NA
   mod_prop.req_data_buffering = 0; // NA
   mod_prop.max_metadata_size = 0;  // NA

   capi_result = capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Get common basic properties failed with result %lu", capi_result);
      return capi_result;
   }

   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;
   for (uint32_t i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;
      switch (prop_ptr[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_STACK_SIZE:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            break;
         }
         case CAPI_PORT_DATA_THRESHOLD:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: null ptr while querying threshold");
               return CAPI_EBADPARAM;
            }

            // based on int samples per period calculation (frame size in bytes)
            /* mono graph input: 48 samples x 2 bytes/sample x 1 channel = 96 bytes (1ms period) */
            uint32_t threshold_in_bytes = 48 * 2 * 1;

            capi_result = capi_cmn_handle_get_port_threshold(&prop_ptr[i], threshold_in_bytes);
            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         {
            //capi_result = capi_hw_intf_cmn_update_intf_extn_status(payload_ptr);
            break;
         }
         default:
         {
            AR_MSG(DBG_HIGH_PRIO, "CAPI_NXP_DEVICE: Skipped Get Property for 0x%x. Not supported.", prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      }
   }
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_nxp_device_process_set_properties
  DESCRIPTION: Function to set the properties for the nxp_device module
 * -----------------------------------------------------------------------*/
capi_err_t capi_nxp_device_process_set_properties(capi_nxp_device_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   int ret = 0;
   //struct dma_status stat;
   //uint32_t free_bytes;
   uint32_t i = 0;
   //AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Set properties enter");

   if (NULL == me_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Set property received null property.");
      return CAPI_EBADPARAM;
   }

   capi_result = capi_cmn_set_basic_properties(proplist_ptr, &me_ptr->heap_mem, &me_ptr->cb_info, FALSE);

   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Set basic properties failed with result %lu", capi_result);
      return capi_result;
   }

   
   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;
      switch (prop_ptr[i].id)
      {
         case CAPI_HEAP_ID:
         case CAPI_EVENT_CALLBACK_INFO:
         case CAPI_ALGORITHMIC_RESET:
         {
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            capi_result = CAPI_EOK;

            break;
         }
         case CAPI_PORT_NUM_INFO:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_port_num_info_t))
            {
               capi_port_num_info_t *data_ptr = (capi_port_num_info_t *)payload_ptr->data_ptr;
               if (!(data_ptr->num_input_ports == 1 && data_ptr->num_output_ports == 0) &&
               !(data_ptr->num_input_ports == 0 && data_ptr->num_output_ports == 1))
               {
               AR_MSG(DBG_ERROR_PRIO,
                     "CAPI_NXP_DEVICE: Invalid number of input = %d, number of output ports = %d.",
                     data_ptr->num_input_ports,
                     data_ptr->num_output_ports);
               CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
               }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Bad param size %lu", payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_CUSTOM_PROPERTY:
         {
            capi_custom_property_t *cust_prop_ptr    = (capi_custom_property_t *)payload_ptr->data_ptr;
            void *                  cust_payload_ptr = (void *)(cust_prop_ptr + 1);

            switch (cust_prop_ptr->secondary_prop_id)
            {
               case FWK_EXTN_PROPERTY_ID_STM_TRIGGER:
               {
                  if (payload_ptr->actual_data_len < sizeof(capi_custom_property_t) + sizeof(capi_prop_stm_trigger_t))
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "CAPI_NXP_DEVICE: Insufficient payload size for stm trigger %d",
                            payload_ptr->actual_data_len);
                     return CAPI_EBADPARAM;
                  }

                  //AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Setting STM trigger");

                  // Get end point info
                  capi_prop_stm_trigger_t *trig_ptr                    = (capi_prop_stm_trigger_t *)cust_payload_ptr;
                  me_ptr->i2s_driver.i2s_intf_state.trigger_signal_ptr = trig_ptr->signal_ptr;
                  me_ptr->i2s_driver.i2s_intf_state.i2s_intr_cnt_ptr   = trig_ptr->raised_intr_counter_ptr;

                  break;
               }
               case FWK_EXTN_PROPERTY_ID_STM_CTRL:
               {
                  //AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Setting STM CTRL enter");
                  if (payload_ptr->actual_data_len < sizeof(capi_prop_stm_ctrl_t) + sizeof(capi_custom_property_t))
                  {
                     /* Insufficient payload size */
                     AR_MSG(DBG_ERROR_PRIO, "invalid payload size");
                     capi_result |= CAPI_ENEEDMORE;
                     break;
                  }

                  capi_prop_stm_ctrl_t *ctr_ptr = (capi_prop_stm_ctrl_t *)cust_payload_ptr;

                  if (ctr_ptr->enable){

                     //AR_MSG(DBG_ERROR_PRIO, "Start DMA");
                     ret = dma_start(dma_device, me_ptr->dma_chan_index);
                     if (ret < 0) {
                        AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: DMA start failed rc: %d", ret);
                        return CAPI_EFAILED;
                     }

                     //Start the DAI
                     dai_trigger(dai_dev, DAI_DIR_TX, DAI_TRIGGER_START);

                     /* SAI TCSR status after trigger
                     * SAI3 base = 0x30c30000 on i.MX8MP
                     * TCSR offset = 0x00, TCR3 offset = 0x0C
                     */
                     /*{
                        I2S_Type *sai_base = (I2S_Type *)0x30c30000;
                        uint32_t tcsr = sai_base->TCSR;
                        uint32_t tcr3 = sai_base->TCR3;
                        AR_MSG(DBG_ERROR_PRIO,
                              "SAI post-start: TCSR=0x%08x TCR3=0x%08x "
                              "TE=%d FEF=%d FWF=%d FRF=%d FRDE=%d",
                              tcsr, tcr3,
                              !!(tcsr & I2S_TCSR_TE_MASK),
                              !!(tcsr & I2S_TCSR_FEF_MASK),
                              !!(tcsr & I2S_TCSR_FWF_MASK),
                              !!(tcsr & I2S_TCSR_FRF_MASK),
                              !!(tcsr & I2S_TCSR_FRDE_MASK));
                     }*/

                     AR_MSG(DBG_ERROR_PRIO, "DMA & DAI start done");
                  }
                  else{
                     AR_MSG(DBG_ERROR_PRIO, "disable: True");
                  }
                  break;
               }
               default:
               {
                  AR_MSG(DBG_HIGH_PRIO, "CAPI_NXP_DEVICE: Unknown Custom Property[%d]", cust_prop_ptr->secondary_prop_id);
                  capi_result |= CAPI_EUNSUPPORTED;
                  break;
               }
            } // inner switch - CUSTOM Properties
            break;
         }
         case CAPI_MODULE_INSTANCE_ID:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_module_instance_id_t))
            {
               capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
               me_ptr->iid = data_ptr->module_instance_id;
               AR_MSG(DBG_LOW_PRIO,
                     "CAPI_NXP_DEVICE: This module-id 0x%08lX, instance-id 0x%08lX",
                     data_ptr->module_id,
                     me_ptr->iid);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                     "CAPI_NXP_DEVICE: Set, Param id 0x%lx Bad param size %lu",
                     (uint32_t)prop_ptr[i].id,
                     payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_LOGGING_INFO:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_logging_info_t))
            {
               capi_logging_info_t *data_ptr = (capi_logging_info_t *)payload_ptr->data_ptr;
               me_ptr->log_id                = data_ptr->log_id;
               me_ptr->log_id_reserved_mask  = data_ptr->log_id_mask;
               AR_MSG(DBG_LOW_PRIO,
                     "CAPI_NXP_DEVICE: log-id 0x%08lX, mask 0x%08lX",
                     me_ptr->log_id,
                     me_ptr->log_id_reserved_mask);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                     "CAPI_NXP_DEVICE: Set, Param id 0x%lx Bad param size %lu",
                     (uint32_t)prop_ptr[i].id,
                     payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         default:
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI_NXP_DEVICE: Skipping set prop, unsupported param[%d]", prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
            continue;
         }
      } /* Outer switch - Generic CAPI Properties */
   } /* Loop all properties */

   return capi_result;
}