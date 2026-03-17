/**
@file irm_prof_drv_q6.cpp

@brief Profiler driver and Dev cfg file for IRM q6 processor.

================================================================================
Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
==============================================================================*/
// clang-format off
// clang-format on

#include "irm_dev_cfg.h"
#include "irm_api.h"
#include "irm_i.h"
#include "spf_macros.h"
#include "spf_svc_calib.h"
#include "amssheap.h"
#include "irm_sysmon_util.h"
#include "irm_prev_metric_info.h"
#include "posal_mem_prof.h"
#include "private_irm_api.h"
#include "posal_thread_profiling.h"
#include "posal_island.h"

//#define IRM_DEBUG 1

typedef struct irm_q6_profiler_info_t
{
   bool_t                     processor_init_done; // if sysmon register was done
   irm_metric_id_q6_hw_info_t q6_hw_info;          // q6 number hardware into

} irm_q6_profiler_info_t;

irm_system_capabilities_t g_irm_cmn_capabilities = { .processor_type               = IRM_PROCESSOR_TYPE_HEXAGON,
                                                     .min_profiling_period_us      = IRM_MIN_PROFILING_PERIOD_1MS,
                                                     .min_profile_per_report       = IRM_MIN_PROFILES_PER_REPORT_1,
                                                     .max_num_containers_supported = IRM_MAX_NUM_CONTAINERS_SUPPORTED,
                                                     .max_module_supported         = IRM_MAX_NUM_MODULES_SUPPORTED };
#if defined(SIM)
uint32_t g_irm_processor_metric_capabilities[] = { IRM_METRIC_ID_PROCESSOR_CYCLES, IRM_BASIC_METRIC_ID_CURRENT_CLOCK,
                                                   IRM_METRIC_ID_HEAP_INFO,        IRM_METRIC_ID_PACKET_COUNT,
                                                   IRM_METRIC_ID_MEM_TRANSACTIONS, IRM_METRIC_ID_Q6_HW_INFO,
                                                   IRM_METRIC_ID_TIMESTAMP

};
#else
uint32_t g_irm_processor_metric_capabilities[] = { IRM_METRIC_ID_PROCESSOR_CYCLES,
                                                   IRM_BASIC_METRIC_ID_CURRENT_CLOCK,
                                                   IRM_METRIC_ID_HEAP_INFO,
                                                   IRM_METRIC_ID_PACKET_COUNT,
                                                   IRM_METRIC_ID_MEM_TRANSACTIONS

};
#endif

#if defined(SIM)
uint32_t g_irm_container_metric_capabilities[] = { IRM_METRIC_ID_PROCESSOR_CYCLES,
                                                   IRM_METRIC_ID_PACKET_COUNT,
                                                   IRM_METRIC_ID_HEAP_INFO,
                                                   IRM_METRIC_ID_STACK_INFO };
#else
uint32_t g_irm_container_metric_capabilities[]  = { IRM_METRIC_ID_PROCESSOR_CYCLES,
                                                   IRM_METRIC_ID_PACKET_COUNT,
                                                   IRM_METRIC_ID_HEAP_INFO };
#endif

#if defined(SIM)
uint32_t g_irm_module_metric_capabilities[] = { IRM_METRIC_ID_PROCESSOR_CYCLES,
                                                IRM_METRIC_ID_PACKET_COUNT,
                                                IRM_METRIC_ID_HEAP_INFO };
#else
uint32_t g_irm_module_metric_capabilities[]     = { IRM_METRIC_ID_PROCESSOR_CYCLES,
                                                IRM_METRIC_ID_PACKET_COUNT,
                                                IRM_METRIC_ID_HEAP_INFO };
#endif

#if defined(SIM)
uint32_t g_irm_pool_metric_capabilities[] = { IRM_METRIC_ID_HEAP_INFO };
#else
uint32_t g_irm_pool_metric_capabilities[]       = { IRM_METRIC_ID_HEAP_INFO };
#endif

#if defined(SIM)
uint32_t g_irm_static_mod_metric_capabilities[] = { IRM_METRIC_ID_PROCESSOR_CYCLES,
                                                    IRM_METRIC_ID_PACKET_COUNT,
                                                    IRM_METRIC_ID_HEAP_INFO,
                                                    IRM_METRIC_ID_STACK_INFO };
#else
uint32_t g_irm_static_mod_metric_capabilities[] = { IRM_METRIC_ID_PROCESSOR_CYCLES,
                                                    IRM_METRIC_ID_PACKET_COUNT,
                                                    IRM_METRIC_ID_HEAP_INFO };
#endif

irm_capability_node_t g_capability_list[] = {
   { .block_id       = IRM_BLOCK_ID_PROCESSOR,
     .num_metrics    = sizeof(g_irm_processor_metric_capabilities) / sizeof(uint32_t),
     .capability_ptr = (uint32_t *)&g_irm_processor_metric_capabilities },
   { .block_id       = IRM_BLOCK_ID_CONTAINER,
     .num_metrics    = sizeof(g_irm_container_metric_capabilities) / sizeof(uint32_t),
     .capability_ptr = (uint32_t *)&g_irm_container_metric_capabilities },
   { .block_id       = IRM_BLOCK_ID_MODULE,
     .num_metrics    = sizeof(g_irm_module_metric_capabilities) / sizeof(uint32_t),
     .capability_ptr = (uint32_t *)&g_irm_module_metric_capabilities },
   { .block_id       = IRM_BLOCK_ID_POOL,
     .num_metrics    = sizeof(g_irm_pool_metric_capabilities) / sizeof(uint32_t),
     .capability_ptr = (uint32_t *)&g_irm_pool_metric_capabilities },
   { .block_id       = IRM_BLOCK_ID_STATIC_MODULE,
     .num_metrics    = sizeof(g_irm_static_mod_metric_capabilities) / sizeof(uint32_t),
     .capability_ptr = (uint32_t *)&g_irm_static_mod_metric_capabilities },
};

irm_capability_node_t *g_capability_list_ptr   = &g_capability_list[0];
uint32_t               g_num_capability_blocks = sizeof(g_capability_list) / sizeof(irm_capability_node_t);

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
bool_t irm_is_supported_metric(uint32_t block_id, uint32_t metric_id)
{
   uint32_t *capabilities_ptr = NULL;
   uint32_t  num_elements     = 0;
   if (block_id == IRM_BLOCK_ID_PROCESSOR)
   {
      capabilities_ptr = g_irm_processor_metric_capabilities;
      num_elements     = sizeof(g_irm_processor_metric_capabilities) / sizeof(uint32_t);
   }
   else if (block_id == IRM_BLOCK_ID_CONTAINER)
   {
      capabilities_ptr = (uint32_t *)&g_irm_container_metric_capabilities;
      num_elements     = sizeof(g_irm_container_metric_capabilities) / sizeof(uint32_t);
   }
   else if (block_id == IRM_BLOCK_ID_MODULE)
   {
      capabilities_ptr = (uint32_t *)&g_irm_module_metric_capabilities;
      num_elements     = sizeof(g_irm_module_metric_capabilities) / sizeof(uint32_t);
   }
   else if (block_id == IRM_BLOCK_ID_POOL)
   {
      capabilities_ptr = (uint32_t *)&g_irm_pool_metric_capabilities;
      num_elements     = sizeof(g_irm_pool_metric_capabilities) / sizeof(uint32_t);
   }
   else if (block_id == IRM_BLOCK_ID_STATIC_MODULE)
   {
      capabilities_ptr = (uint32_t *)&g_irm_static_mod_metric_capabilities;
      num_elements     = sizeof(g_irm_static_mod_metric_capabilities) / sizeof(uint32_t);
   }
   else
   {
      return FALSE;
   }

   for (uint32_t i = 0; i < num_elements; i++)
   {
      if (metric_id == capabilities_ptr[i])
      {
         AR_MSG(DBG_HIGH_PRIO, "IRM: is supported metric id = 0x%X", capabilities_ptr[i]);
         return TRUE;
      }
   }
   return FALSE;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
void irm_get_supported_metric_arr(uint32_t block_id, uint32_t **metric_id_list, uint32_t *num_elements)
{

   if (block_id == IRM_BLOCK_ID_PROCESSOR)
   {
      *metric_id_list = g_irm_processor_metric_capabilities;
      *num_elements   = sizeof(g_irm_processor_metric_capabilities) / sizeof(uint32_t);
   }
   else if (block_id == IRM_BLOCK_ID_CONTAINER)
   {
      *metric_id_list = (uint32_t *)&g_irm_container_metric_capabilities;
      *num_elements   = sizeof(g_irm_container_metric_capabilities) / sizeof(uint32_t);
   }
   else if (block_id == IRM_BLOCK_ID_MODULE)
   {
      *metric_id_list = (uint32_t *)&g_irm_module_metric_capabilities;
      *num_elements   = sizeof(g_irm_module_metric_capabilities) / sizeof(uint32_t);
   }
   else if (block_id == IRM_BLOCK_ID_POOL)
   {
      *metric_id_list = (uint32_t *)&g_irm_pool_metric_capabilities;
      *num_elements   = sizeof(g_irm_pool_metric_capabilities) / sizeof(uint32_t);
   }
   else if (block_id == IRM_BLOCK_ID_STATIC_MODULE)
   {
      *metric_id_list = (uint32_t *)&g_irm_static_mod_metric_capabilities;
      *num_elements   = sizeof(g_irm_static_mod_metric_capabilities) / sizeof(uint32_t);
   }
   else
   {
      AR_MSG(DBG_HIGH_PRIO, "IRM: Illigal block id= 0x%X", block_id);
   }
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
#if 0
void irm_print_sysmon_values(sysmon_audio_query_t *current_query_ptr)
{
   AR_MSG(DBG_HIGH_PRIO,
          "IRM: sysclocktick %lu, pcycles %lu, busVoteinAb %lu, busVoteinIb %lu, "
          "coreClkVoteinkHz %lu",
          current_query_ptr->sysclocktick,
          current_query_ptr->pcycles,
          current_query_ptr->busVoteinAb,
          current_query_ptr->busVoteinIb,
          current_query_ptr->coreClkVoteinkHz);

   AR_MSG(DBG_HIGH_PRIO,
          "avsheap_total_bytes %lu, avsheap_available_total = %lu, avsheap_available_max %lu, pktcnt %lu, axi_rd_cnt "
          "%lu, "
          "axi_wr_cnt %lu",
          current_query_ptr->avsheap_total_bytes,
          current_query_ptr->avsheap_available_total,
          current_query_ptr->avsheap_available_max,
          current_query_ptr->pktcnt,
          current_query_ptr->axi_rd_cnt,
          current_query_ptr->axi_wr_cnt);
}
#endif

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/

// Queries the os for the packet count for blocks where the block is a thread (static modules, containers)
static void irm_fill_thread_packet_metric(irm_report_metric_payload_t *report_metric_payload_ptr,
                                          irm_node_obj_t *             metric_obj_ptr,
                                          int64_t                      thread_id,
                                          uint32_t                     instance_id)
{
   irm_metric_id_packet_count_t *  payload_ptr = (irm_metric_id_packet_count_t *)(report_metric_payload_ptr + 1);
   irm_prev_metric_packet_count_t *prev_ptr =
      (irm_prev_metric_packet_count_t *)metric_obj_ptr->metric_info.prev_statistic_ptr;

   uint64_t pktcount = 0;

#if defined(AVS_USES_QURT_PROF)
   pktcount = qurt_thread_pktcount_get(thread_id);
#endif

   if (!metric_obj_ptr->is_first_time)
   {
      payload_ptr->packet_count = pktcount - prev_ptr->packet_count;
   }
   else
   {
      report_metric_payload_ptr->is_valid      = 0;
      report_metric_payload_ptr->frame_size_ms = 0;
      metric_obj_ptr->is_first_time            = FALSE;
   }
   prev_ptr->packet_count = pktcount;
#if IRM_DEBUG
   AR_MSG(DBG_HIGH_PRIO, "IRM: IID = 0x%X, packet_count = %lu", instance_id, payload_ptr->packet_count);
#endif
}

static void irm_fill_thread_cycles_metric(irm_report_metric_payload_t *report_metric_payload_ptr,
                                          irm_node_obj_t *             metric_obj_ptr,
                                          int64_t                      thread_id,
                                          uint32_t                     instance_id)
{
   irm_metric_id_processor_cycles_t *payload_ptr = (irm_metric_id_processor_cycles_t *)(report_metric_payload_ptr + 1);
   irm_prev_metric_processor_cycles_t *prev_ptr =
      (irm_prev_metric_processor_cycles_t *)metric_obj_ptr->metric_info.prev_statistic_ptr;

   uint64_t total_pcycles = 0;
#if defined(AVS_USES_QURT_PROF)
   uint64_t pcycles[IRM_MAX_NUM_HW_THREADS] = { 0 };
   qurt_profile_get_threadid_pcycles(thread_id, &pcycles[0]);
   for (uint32_t hw_thread = 0; hw_thread < IRM_MAX_NUM_HW_THREADS; hw_thread++)
   {
      total_pcycles += pcycles[hw_thread];
   }
#endif
   if (!metric_obj_ptr->is_first_time)
   {
      payload_ptr->processor_cycles = total_pcycles - prev_ptr->processor_cycles;
   }
   else
   {
      report_metric_payload_ptr->is_valid      = 0;
      report_metric_payload_ptr->frame_size_ms = 0;
      metric_obj_ptr->is_first_time            = FALSE;
   }
   prev_ptr->processor_cycles = total_pcycles;
#if IRM_DEBUG
   AR_MSG(DBG_HIGH_PRIO, "IRM: IID = 0x%X, pcyles = %lu", instance_id, payload_ptr->processor_cycles);
#endif
}

static ar_result_t irm_fill_thread_stack_metric(irm_report_metric_payload_t *report_metric_payload_ptr,
                                                irm_node_obj_t *             metric_obj_ptr,
                                                int64_t                      thread_id,
                                                uint32_t                     instance_id)
{
   ar_result_t                 result      = AR_EOK;
   irm_metric_id_stack_info_t *payload_ptr = (irm_metric_id_stack_info_t *)(report_metric_payload_ptr + 1);

   // Thread profiling is only availible on sim
#ifdef SIM

   result =
      posal_thread_profiling_get_stack_info(thread_id, &payload_ptr->current_stack_usage, &payload_ptr->stack_size);

#endif

   if (payload_ptr->current_stack_usage > payload_ptr->stack_size)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "IRM: WARNING: IID = 0x%X, stack usage of %lu is greater than stack size = %lu",
             instance_id,
             payload_ptr->current_stack_usage,
             payload_ptr->stack_size);
   }

#if IRM_DEBUG
   AR_MSG(DBG_HIGH_PRIO,
          "IRM: IID = 0x%X, stack usage = %lu stack size = %lu",
          instance_id,
          payload_ptr->current_stack_usage,
          payload_ptr->stack_size);
#endif

   return result;
}

static ar_result_t irm_fill_processor_metric(irm_t *               irm_ptr,
                                             irm_node_obj_t *      metric_obj_ptr,
                                             sysmon_audio_query_t *current_query_ptr,
                                             uint32_t              frame_size_ms,
                                             uint64_t *            idle_pcycles_ptr)
{
   ar_result_t result = AR_EOK;
   if (NULL == metric_obj_ptr)
   {
      result = AR_EFAILED;
      return result;
   }
   irm_q6_profiler_info_t *q6_info_ptr       = (irm_q6_profiler_info_t *)irm_ptr->core.profiler_handle_ptr;
   irm_report_metric_t *   report_metric_ptr = (irm_report_metric_t *)metric_obj_ptr->metric_info.metric_payload_ptr;
   report_metric_ptr->num_metric_payloads    = irm_ptr->core.timer_tick_counter + 1;
   report_metric_ptr++;

   uint32_t metric_size = irm_get_metric_payload_size(metric_obj_ptr->id);

   irm_report_metric_payload_t *report_metric_payload_ptr =
      (irm_report_metric_payload_t *)(((uint8_t *)report_metric_ptr) +
                                      (irm_ptr->core.timer_tick_counter *
                                       (sizeof(irm_report_metric_payload_t) + metric_size)));

   report_metric_payload_ptr->is_valid      = 1;
   report_metric_payload_ptr->frame_size_ms = frame_size_ms;
   report_metric_payload_ptr->payload_size  = metric_size;

   switch (metric_obj_ptr->id)
   {
      case IRM_METRIC_ID_PROCESSOR_CYCLES:
      {
         irm_metric_id_processor_cycles_t *payload_ptr =
            (irm_metric_id_processor_cycles_t *)(report_metric_payload_ptr + 1);
         irm_prev_metric_processor_cycles_t *prev_ptr =
            (irm_prev_metric_processor_cycles_t *)metric_obj_ptr->metric_info.prev_statistic_ptr;

         payload_ptr->processor_cycles = (uint32_t)(current_query_ptr->pcycles - prev_ptr->processor_cycles);
         irm_calculate_non_idle_cycles(&q6_info_ptr->q6_hw_info, &payload_ptr->processor_cycles, idle_pcycles_ptr);
#if IRM_DEBUG
         AR_MSG(DBG_HIGH_PRIO, "IRM: pcyles = %lu", payload_ptr->processor_cycles);
#endif
         prev_ptr->processor_cycles = current_query_ptr->pcycles;
         break;
      }
      case IRM_BASIC_METRIC_ID_CURRENT_CLOCK:
      {
         irm_metric_id_current_clock_t *payload_ptr = (irm_metric_id_current_clock_t *)(report_metric_payload_ptr + 1);
         payload_ptr->current_clock                 = (uint32_t)current_query_ptr->coreClkVoteinkHz;

         break;
      }
      case IRM_METRIC_ID_HEAP_INFO:
      {
         irm_metric_id_heap_info_t *payload_ptr = (irm_metric_id_heap_info_t *)(report_metric_payload_ptr + 1);
         payload_ptr->num_heap_id               = IRM_MAX_NUM_HEAP_ID;
         irm_per_heap_id_info_payload_t *per_heap_id_payload_ptr = (irm_per_heap_id_info_payload_t *)(payload_ptr + 1);

         // Handle Default Memory Type (POSAL_MEM_TYPE_DEFAULT)
         per_heap_id_payload_ptr[POSAL_MEM_TYPE_DEFAULT].heap_id = POSAL_MEM_TYPE_DEFAULT;
         per_heap_id_payload_ptr[POSAL_MEM_TYPE_DEFAULT].current_heap_usage =
            (uint32_t)(current_query_ptr->avsheap_total_bytes - current_query_ptr->avsheap_available_total);
         per_heap_id_payload_ptr[POSAL_MEM_TYPE_DEFAULT].max_allowed_heap_size =
            (uint32_t)(current_query_ptr->avsheap_total_bytes);

#if IRM_DEBUG
         AR_MSG(DBG_HIGH_PRIO,
                "IRM: heap_type: %lu, current_heap_usage: %lu, max_allowed_heap_size: %lu",
                POSAL_MEM_TYPE_DEFAULT,
                per_heap_id_payload_ptr[POSAL_MEM_TYPE_DEFAULT].current_heap_usage,
                per_heap_id_payload_ptr[POSAL_MEM_TYPE_DEFAULT].max_allowed_heap_size);
#endif

         // Handle Non-default Memory Types (island memory)
         for (uint32_t i = POSAL_MEM_TYPE_DEFAULT + 1; i < POSAL_MEM_TYPE_NUM_SUPPORTED; i++)
         {
            POSAL_HEAP_ID heap_id = posal_get_heap_id(i);
            if (POSAL_HEAP_DEFAULT == heap_id)
            {
               continue;
            }

            if (POSAL_MEM_TYPE_LOW_POWER_2 == i)
            {
#if IRM_DEBUG
               AR_MSG(DBG_HIGH_PRIO, "IRM: warning.. heap_type: %lu aggregation not yet supported", i);
#endif
               continue;
            }

            per_heap_id_payload_ptr[i].heap_id = i;
#if defined(AVS_USES_ISLAND_MEM_PROF)
            per_heap_id_payload_ptr[i].current_heap_usage = posal_island_get_current_mem_usage_v2(GET_ACTUAL_HEAP_ID(posal_get_heap_id(i)));
            per_heap_id_payload_ptr[i].max_allowed_heap_size =
               posal_island_get_max_allowed_mem_usage_v2(GET_ACTUAL_HEAP_ID(posal_get_heap_id(i)));
#else
            per_heap_id_payload_ptr[i].current_heap_usage    = posal_globalstate.avs_stats[heap_id].curr_heap;
            per_heap_id_payload_ptr[i].max_allowed_heap_size = posal_globalstate.avs_stats[heap_id].curr_heap;
#endif

#if IRM_DEBUG
            AR_MSG(DBG_HIGH_PRIO,
                   "IRM: heap_type: %lu, current_heap_usage: %lu, max_allowed_heap_size: %lu",
                   i,
                   per_heap_id_payload_ptr[i].current_heap_usage,
                   per_heap_id_payload_ptr[i].max_allowed_heap_size);
#endif
         }
         break;
      }
      case IRM_METRIC_ID_PACKET_COUNT:
      {
         irm_metric_id_packet_count_t *  payload_ptr = (irm_metric_id_packet_count_t *)(report_metric_payload_ptr + 1);
         irm_prev_metric_packet_count_t *prev_ptr =
            (irm_prev_metric_packet_count_t *)metric_obj_ptr->metric_info.prev_statistic_ptr;
         payload_ptr->packet_count = (uint32_t)(current_query_ptr->pktcnt - prev_ptr->packet_count);

#if IRM_DEBUG
         AR_MSG(DBG_HIGH_PRIO,
                "IRM: packet_count = %lu, current query = %lu, prev query = %lu",
                payload_ptr->packet_count,
                current_query_ptr->pktcnt,
                (uint32_t)prev_ptr->packet_count);
#endif

         prev_ptr->packet_count = current_query_ptr->pktcnt;
         break;
      }
      case IRM_METRIC_ID_MEM_TRANSACTIONS:
      {
         irm_metric_id_mem_transactions_t *payload_ptr =
            (irm_metric_id_mem_transactions_t *)(report_metric_payload_ptr + 1);
         irm_prev_metric_mem_transactions_t *prev_ptr =
            (irm_prev_metric_mem_transactions_t *)metric_obj_ptr->metric_info.prev_statistic_ptr;

         payload_ptr->read_mem_transactions_bytes  = (uint32_t)(current_query_ptr->axi_rd_cnt - prev_ptr->axi_rd_cnt);
         payload_ptr->write_mem_transactions_bytes = (uint32_t)(current_query_ptr->axi_wr_cnt - prev_ptr->axi_wr_cnt);

#if IRM_DEBUG
         AR_MSG(DBG_HIGH_PRIO,
                "IRM: mem read transactions = %lu, mem write transactions = %lu",
                payload_ptr->read_mem_transactions_bytes,
                payload_ptr->write_mem_transactions_bytes);

#endif
         prev_ptr->axi_rd_cnt = current_query_ptr->axi_rd_cnt;
         prev_ptr->axi_wr_cnt = current_query_ptr->axi_wr_cnt;
         break;
      }
      case IRM_METRIC_ID_Q6_HW_INFO:
      {
         irm_metric_id_q6_hw_info_t *payload_ptr = (irm_metric_id_q6_hw_info_t *)(report_metric_payload_ptr + 1);
         payload_ptr->num_hw_threads             = q6_info_ptr->q6_hw_info.num_hw_threads;
         payload_ptr->q6_arch_version            = q6_info_ptr->q6_hw_info.q6_arch_version;
#if IRM_DEBUG
         AR_MSG(DBG_HIGH_PRIO,
                "IRM: Q6 Hw info, num_hw_threads = %lu, q6_arch_version = 0x%X",
                payload_ptr->num_hw_threads,
                payload_ptr->q6_arch_version);
#endif
         break;
      }
      case IRM_METRIC_ID_TIMESTAMP:
      {
         irm_metric_id_timestamp_t *payload_ptr = (irm_metric_id_timestamp_t *)(report_metric_payload_ptr + 1);
         uint64_t                   ts          = posal_timer_get_time_in_msec();
         payload_ptr->timestamp_ms_lsw          = 0xFFFFFFFF & ts;
         payload_ptr->timestamp_ms_msw          = 0xFFFFFFFF & ts >> 32;
#if IRM_DEBUG
         AR_MSG(DBG_HIGH_PRIO, "IRM: time stamp = %lu", (uint32_t)ts);
#endif
         break;
      }
      default:
      {
         break;
      }
   }
   return result;
}

static void irm_fill_cntr_or_module_heap_metric(irm_node_obj_t *             instance_obj_ptr,
                                                irm_report_metric_payload_t *report_metric_payload_ptr)
{
   irm_metric_id_heap_info_t *payload_ptr = (irm_metric_id_heap_info_t *)(report_metric_payload_ptr + 1);
   payload_ptr->num_heap_id               = IRM_MAX_NUM_HEAP_ID;
   irm_per_heap_id_info_payload_t *per_heap_id_payload_ptr = (irm_per_heap_id_info_payload_t *)(payload_ptr + 1);

   uint32_t regular_heap_usage = 0;
   uint32_t island_heap_usage  = 0;

   for (uint32_t heap_idx = 0; heap_idx < POSAL_HEAP_MGR_HEAP_INDEX_END; heap_idx++)
   {
      uint32_t      heap_usage = 0;
      POSAL_HEAP_ID heap_id    = instance_obj_ptr->heap_id | heap_idx;
      posal_mem_prof_query(heap_id, &heap_usage);
      // Internally there may be more heaps but they are classified into island and non-island.
      if (POSAL_IS_ISLAND_HEAP_ID(heap_id))
      {
         island_heap_usage += heap_usage;
      }
      else
      {
         regular_heap_usage += heap_usage;
      }
   }

   // TODO:pbm create a def in irm_api for tools
   per_heap_id_payload_ptr[0].heap_id               = 0;
   per_heap_id_payload_ptr[0].current_heap_usage    = regular_heap_usage;
   per_heap_id_payload_ptr[0].max_allowed_heap_size = 0;
   per_heap_id_payload_ptr[1].heap_id               = 1;
   per_heap_id_payload_ptr[1].current_heap_usage    = island_heap_usage;
   per_heap_id_payload_ptr[1].max_allowed_heap_size = 0;
#if IRM_DEBUG
   AR_MSG(DBG_HIGH_PRIO,
          "IRM: IID = 0x%X, orig heap id = 0x%X, heap_id0 = %lu, current_heap_usage0 = %lu,  heap_id1 = %lu, "
          "current_heap_usage1 = %lu",
          instance_obj_ptr->id,
          instance_obj_ptr->heap_id,
          per_heap_id_payload_ptr[0].heap_id,
          per_heap_id_payload_ptr[0].current_heap_usage,
          per_heap_id_payload_ptr[1].heap_id,
          per_heap_id_payload_ptr[1].current_heap_usage);
#endif
}

static void irm_fill_pool_heap_metric(irm_node_obj_t *             instance_obj_ptr,
                                      irm_report_metric_payload_t *report_metric_payload_ptr)
{
   irm_metric_id_heap_info_t *payload_ptr = (irm_metric_id_heap_info_t *)(report_metric_payload_ptr + 1);
   payload_ptr->num_heap_id               = IRM_MAX_NUM_HEAP_ID;
   irm_per_heap_id_info_payload_t *per_heap_id_payload_ptr = (irm_per_heap_id_info_payload_t *)(payload_ptr + 1);

   uint32_t pool_used = 0;

   switch (instance_obj_ptr->id)
   {
      case IRM_POOL_ID_LIST:
         pool_used = posal_bufpool_profile_all_mem_usage();
   }

   // TODO:pbm create a def in irm_api for tools
   per_heap_id_payload_ptr[0].heap_id               = 0;
   per_heap_id_payload_ptr[0].current_heap_usage    = pool_used;
   per_heap_id_payload_ptr[0].max_allowed_heap_size = 0;
   per_heap_id_payload_ptr[1].heap_id               = 1;
   per_heap_id_payload_ptr[1].current_heap_usage    = 0;
   per_heap_id_payload_ptr[1].max_allowed_heap_size = 0;
#if IRM_DEBUG
   AR_MSG(DBG_HIGH_PRIO,
          "IRM: IID = 0x%X, orig heap id = 0x%X, heap_id0 = %lu, current_heap_usage0 = %lu,  heap_id1 = %lu, "
          "current_heap_usage1 = %lu",
          instance_obj_ptr->id,
          instance_obj_ptr->heap_id,
          per_heap_id_payload_ptr[0].heap_id,
          per_heap_id_payload_ptr[0].current_heap_usage,
          per_heap_id_payload_ptr[1].heap_id,
          per_heap_id_payload_ptr[1].current_heap_usage);
#endif
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
static ar_result_t irm_fill_container_metrics(irm_t *         irm_ptr,
                                              irm_node_obj_t *instance_obj_ptr,
                                              irm_node_obj_t *metric_obj_ptr,
                                              uint32_t        frame_size_ms,
                                              uint32_t        block_id)
{
   ar_result_t result = AR_EOK;

   irm_report_metric_t *report_metric_ptr = (irm_report_metric_t *)metric_obj_ptr->metric_info.metric_payload_ptr;
   report_metric_ptr->num_metric_payloads = irm_ptr->core.timer_tick_counter + 1;
   report_metric_ptr++;

   uint32_t metric_size = irm_get_metric_payload_size(metric_obj_ptr->id);

   irm_report_metric_payload_t *report_metric_payload_ptr =
      (irm_report_metric_payload_t *)(((uint8_t *)report_metric_ptr) +
                                      (irm_ptr->core.timer_tick_counter *
                                       (sizeof(irm_report_metric_payload_t) + metric_size)));

   report_metric_payload_ptr->is_valid      = 1;
   report_metric_payload_ptr->frame_size_ms = frame_size_ms;
   report_metric_payload_ptr->payload_size  = metric_size;

   if (NULL == instance_obj_ptr->handle_ptr)
   {
      // Packet cannot be filled with valid data if there is no handle
#if IRM_DEBUG
      AR_MSG(DBG_HIGH_PRIO, "IRM: early return due to null handle ptr");
#endif
      report_metric_payload_ptr->is_valid = 0;
      return result;
   }
   int64_t thread_id = posal_thread_get_tid_v2(instance_obj_ptr->handle_ptr->cmd_handle_ptr->thread_id);

   switch (metric_obj_ptr->id)
   {
      case IRM_METRIC_ID_PROCESSOR_CYCLES:
      {
         irm_fill_thread_cycles_metric(report_metric_payload_ptr, metric_obj_ptr, thread_id, instance_obj_ptr->id);
         break;
      }
      case IRM_METRIC_ID_PACKET_COUNT:
      {
         irm_fill_thread_packet_metric(report_metric_payload_ptr, metric_obj_ptr, thread_id, instance_obj_ptr->id);
         break;
      }
      case IRM_METRIC_ID_HEAP_INFO:
      {
         irm_fill_cntr_or_module_heap_metric(instance_obj_ptr, report_metric_payload_ptr);
         break;
      }
      case IRM_METRIC_ID_STACK_INFO:
      {
         result =
            irm_fill_thread_stack_metric(report_metric_payload_ptr, metric_obj_ptr, thread_id, instance_obj_ptr->id);
         break;
      }
      default:
      {
         break;
      }
   }
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
static ar_result_t irm_fill_static_module_metrics(irm_t *         irm_ptr,
                                                  irm_node_obj_t *instance_obj_ptr,
                                                  irm_node_obj_t *metric_obj_ptr,
                                                  uint32_t        frame_size_ms,
                                                  uint32_t        block_id)
{
   ar_result_t result = AR_EOK;

   irm_report_metric_t *report_metric_ptr = (irm_report_metric_t *)metric_obj_ptr->metric_info.metric_payload_ptr;
   report_metric_ptr->num_metric_payloads = irm_ptr->core.timer_tick_counter + 1;
   report_metric_ptr++;

   uint32_t metric_size = irm_get_metric_payload_size(metric_obj_ptr->id);

   irm_report_metric_payload_t *report_metric_payload_ptr =
      (irm_report_metric_payload_t *)(((uint8_t *)report_metric_ptr) +
                                      (irm_ptr->core.timer_tick_counter *
                                       (sizeof(irm_report_metric_payload_t) + metric_size)));

   report_metric_payload_ptr->is_valid      = 1;
   report_metric_payload_ptr->frame_size_ms = frame_size_ms;
   report_metric_payload_ptr->payload_size  = metric_size;

   if (NULL == instance_obj_ptr->static_module_info_ptr)
   {
      // Packet cannot be filled with valid data if there is no static module info
#if IRM_DEBUG
      AR_MSG(DBG_HIGH_PRIO, "IRM: early return due to null static module info ptr");
#endif
      report_metric_payload_ptr->is_valid = 0;
      return result;
   }

   int64_t thread_id = instance_obj_ptr->static_module_info_ptr->tid;

   switch (metric_obj_ptr->id)
   {
      case IRM_METRIC_ID_PROCESSOR_CYCLES:
      {
         irm_fill_thread_cycles_metric(report_metric_payload_ptr, metric_obj_ptr, thread_id, instance_obj_ptr->id);
         break;
      }
      case IRM_METRIC_ID_PACKET_COUNT:
      {
         irm_fill_thread_packet_metric(report_metric_payload_ptr, metric_obj_ptr, thread_id, instance_obj_ptr->id);

         break;
      }
      case IRM_METRIC_ID_HEAP_INFO:
      {
         irm_fill_cntr_or_module_heap_metric(instance_obj_ptr, report_metric_payload_ptr);
         break;
      }
      case IRM_METRIC_ID_STACK_INFO:
      {
         result =
            irm_fill_thread_stack_metric(report_metric_payload_ptr, metric_obj_ptr, thread_id, instance_obj_ptr->id);
         break;
      }
      default:
      {
         break;
      }
   }
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
static ar_result_t irm_fill_module_metrics(irm_t *         irm_ptr,
                                           irm_node_obj_t *instance_obj_ptr,
                                           irm_node_obj_t *metric_obj_ptr,
                                           uint32_t        frame_size_ms,
                                           uint32_t        block_id)
{
   ar_result_t result = AR_EOK;

   irm_report_metric_t *report_metric_ptr = (irm_report_metric_t *)metric_obj_ptr->metric_info.metric_payload_ptr;
   report_metric_ptr->num_metric_payloads = irm_ptr->core.timer_tick_counter + 1;
   report_metric_ptr++;

   uint32_t metric_size = irm_get_metric_payload_size(metric_obj_ptr->id);

   irm_report_metric_payload_t *report_metric_payload_ptr =
      (irm_report_metric_payload_t *)(((uint8_t *)report_metric_ptr) +
                                      (irm_ptr->core.timer_tick_counter *
                                       (sizeof(irm_report_metric_payload_t) + metric_size)));

   report_metric_payload_ptr->is_valid      = 1;
   report_metric_payload_ptr->frame_size_ms = frame_size_ms;
   report_metric_payload_ptr->payload_size  = metric_size;

   if (NULL == instance_obj_ptr->handle_ptr)
   {
      // Packet cannot be filled with valid data if there is no handle
#if IRM_DEBUG
      AR_MSG(DBG_HIGH_PRIO, "IRM: early return due to null handle ptr");
#endif
      report_metric_payload_ptr->is_valid = 0;
      return result;
   }

   switch (metric_obj_ptr->id)
   {
      case IRM_METRIC_ID_PROCESSOR_CYCLES:
      {
         irm_metric_id_processor_cycles_t *payload_ptr =
            (irm_metric_id_processor_cycles_t *)(report_metric_payload_ptr + 1);
         irm_prev_metric_processor_cycles_t *prev_ptr =
            (irm_prev_metric_processor_cycles_t *)metric_obj_ptr->metric_info.prev_statistic_ptr;

         uint64_t total_pcycles = 0;

         if ((NULL != metric_obj_ptr->metric_info.current_mod_statistics_ptr))
         {
            total_pcycles = *((uint64_t *)metric_obj_ptr->metric_info.current_mod_statistics_ptr);
         }
         else
         {
            metric_obj_ptr->is_first_time = TRUE;
         }

         if (!metric_obj_ptr->is_first_time)
         {
            payload_ptr->processor_cycles = total_pcycles - prev_ptr->processor_cycles;
         }
         else
         {
            report_metric_payload_ptr->is_valid      = 0;
            report_metric_payload_ptr->frame_size_ms = 0;
            metric_obj_ptr->is_first_time            = FALSE;
         }
         prev_ptr->processor_cycles = total_pcycles;
#if IRM_DEBUG
         AR_MSG(DBG_HIGH_PRIO,
                "IRM: IID = 0x%X, handle = 0x%X, pcyles = %lu",
                instance_obj_ptr->id,
                instance_obj_ptr->handle_ptr,
                payload_ptr->processor_cycles);
#endif
         break;
      }
      case IRM_METRIC_ID_PACKET_COUNT:
      {
         irm_metric_id_packet_count_t *  payload_ptr = (irm_metric_id_packet_count_t *)(report_metric_payload_ptr + 1);
         irm_prev_metric_packet_count_t *prev_ptr =
            (irm_prev_metric_packet_count_t *)metric_obj_ptr->metric_info.prev_statistic_ptr;

         uint64_t pktcount = 0;

         if (NULL != metric_obj_ptr->metric_info.current_mod_statistics_ptr)
         {
            pktcount = *((uint64_t *)metric_obj_ptr->metric_info.current_mod_statistics_ptr);
         }
         else
         {
            metric_obj_ptr->is_first_time = TRUE;
         }

         if (!metric_obj_ptr->is_first_time)
         {
            payload_ptr->packet_count = pktcount - prev_ptr->packet_count;
         }
         else
         {
            report_metric_payload_ptr->is_valid      = 0;
            report_metric_payload_ptr->frame_size_ms = 0;
            metric_obj_ptr->is_first_time            = FALSE;
         }
         prev_ptr->packet_count = pktcount;
#if IRM_DEBUG
         AR_MSG(DBG_HIGH_PRIO,
                "IRM: IID = 0x%X, handle = 0x%X, packet_count = %lu",
                instance_obj_ptr->id,
                instance_obj_ptr->handle_ptr,
                payload_ptr->packet_count);
#endif
         break;
      }
      case IRM_METRIC_ID_HEAP_INFO:
      {
         irm_fill_cntr_or_module_heap_metric(instance_obj_ptr, report_metric_payload_ptr);
         break;
      }
      default:
      {
         break;
      }
   }
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
static ar_result_t irm_fill_pool_metrics(irm_t *         irm_ptr,
                                         irm_node_obj_t *instance_obj_ptr,
                                         irm_node_obj_t *metric_obj_ptr,
                                         uint32_t        frame_size_ms)
{
   ar_result_t result = AR_EOK;

   irm_report_metric_t *report_metric_ptr = (irm_report_metric_t *)metric_obj_ptr->metric_info.metric_payload_ptr;
   report_metric_ptr->num_metric_payloads = irm_ptr->core.timer_tick_counter + 1;
   report_metric_ptr++;

   uint32_t metric_size = irm_get_metric_payload_size(metric_obj_ptr->id);

   irm_report_metric_payload_t *report_metric_payload_ptr =
      (irm_report_metric_payload_t *)(((uint8_t *)report_metric_ptr) +
                                      (irm_ptr->core.timer_tick_counter *
                                       (sizeof(irm_report_metric_payload_t) + metric_size)));

   report_metric_payload_ptr->is_valid      = 1;
   report_metric_payload_ptr->frame_size_ms = frame_size_ms;
   report_metric_payload_ptr->payload_size  = metric_size;

   irm_fill_pool_heap_metric(instance_obj_ptr, report_metric_payload_ptr);
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
static ar_result_t irm_handle_processor_metrics(irm_t *irm_ptr, irm_node_obj_t *block_obj_ptr, uint32_t frame_size_ms)
{
   spf_list_node_t *    instance_node_ptr = NULL;
   irm_node_obj_t *     instance_obj_ptr  = NULL;
   spf_list_node_t *    metric_node_ptr   = NULL;
   sysmon_audio_query_t current_query;
   uint64_t             idle_pcycles[IRM_MAX_NUM_HW_THREADS] = { 0 };

   int32_t sysmon_result = irm_query_processor_metrics(&current_query, &idle_pcycles[0]);
   if (0 != sysmon_result)
   {
      // NOTE: there is a known compatibility issue where sysmon cannont debug on a production device due to a QPSI
      // review. If the sysmon query is returning non-0 then you'll need to check with CE about their build/device and
      // loop in sysmon team.
      AR_MSG(DBG_ERROR_PRIO, "IRM: non-0 return value from sysmon: %d", sysmon_result);
      return AR_EFAILED;
   }

   instance_node_ptr = block_obj_ptr->head_node_ptr;
   if (NULL == instance_node_ptr || NULL == instance_node_ptr->obj_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Null instance node");
      return AR_EFAILED;
   }

   instance_obj_ptr = (irm_node_obj_t *)instance_node_ptr->obj_ptr;
   metric_node_ptr  = instance_obj_ptr->head_node_ptr;

   for (; NULL != metric_node_ptr; metric_node_ptr = metric_node_ptr->next_ptr)
   {
      irm_node_obj_t *metric_obj_ptr = (irm_node_obj_t *)metric_node_ptr->obj_ptr;
      irm_fill_processor_metric(irm_ptr, metric_obj_ptr, &current_query, frame_size_ms, &idle_pcycles[0]);
   }
   return AR_EOK;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
static ar_result_t irm_handle_container_metrics(irm_t *irm_ptr, irm_node_obj_t *block_obj_ptr, uint32_t frame_size_ms)
{
   ar_result_t result = AR_EOK;

   spf_list_node_t *cntr_node_ptr = block_obj_ptr->head_node_ptr;

   // For each container,
   for (; NULL != cntr_node_ptr; LIST_ADVANCE(cntr_node_ptr))
   {
      irm_node_obj_t *instance_obj_ptr = (irm_node_obj_t *)cntr_node_ptr->obj_ptr;
      if (NULL != instance_obj_ptr)
      {
         spf_list_node_t *metric_node_ptr = instance_obj_ptr->head_node_ptr;

         // For each metric,
         for (; NULL != metric_node_ptr; LIST_ADVANCE(metric_node_ptr))
         {
            irm_node_obj_t *metric_obj_ptr = (irm_node_obj_t *)metric_node_ptr->obj_ptr;
            if (NULL != metric_obj_ptr)
            {
               result = irm_fill_container_metrics(irm_ptr,
                                                   instance_obj_ptr,
                                                   metric_obj_ptr,
                                                   frame_size_ms,
                                                   block_obj_ptr->id);
            }
         }
      }
   }
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
static ar_result_t irm_handle_static_module_metrics(irm_t *         irm_ptr,
                                                    irm_node_obj_t *block_obj_ptr,
                                                    uint32_t        frame_size_ms)
{
   ar_result_t result = AR_EOK;

   spf_list_node_t *cntr_node_ptr = block_obj_ptr->head_node_ptr;

   // For each container,
   for (; NULL != cntr_node_ptr; LIST_ADVANCE(cntr_node_ptr))
   {
      irm_node_obj_t *instance_obj_ptr = (irm_node_obj_t *)cntr_node_ptr->obj_ptr;
      if (NULL != instance_obj_ptr)
      {
         spf_list_node_t *metric_node_ptr = instance_obj_ptr->head_node_ptr;

         // For each metric,
         for (; NULL != metric_node_ptr; LIST_ADVANCE(metric_node_ptr))
         {
            irm_node_obj_t *metric_obj_ptr = (irm_node_obj_t *)metric_node_ptr->obj_ptr;
            if (NULL != metric_obj_ptr)
            {
               result = irm_fill_static_module_metrics(irm_ptr,
                                                       instance_obj_ptr,
                                                       metric_obj_ptr,
                                                       frame_size_ms,
                                                       block_obj_ptr->id);
            }
         }
      }
   }
   return result;
}

static ar_result_t irm_handle_pool_metrics(irm_t *irm_ptr, irm_node_obj_t *block_obj_ptr, uint32_t frame_size_ms)
{
   spf_list_node_t *instance_node_ptr = NULL;
   irm_node_obj_t * instance_obj_ptr  = NULL;
   spf_list_node_t *metric_node_ptr   = NULL;

   instance_node_ptr = block_obj_ptr->head_node_ptr;
   if (NULL == instance_node_ptr || NULL == instance_node_ptr->obj_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Null instance node");
      return AR_EFAILED;
   }

   instance_obj_ptr = (irm_node_obj_t *)instance_node_ptr->obj_ptr;
   metric_node_ptr  = instance_obj_ptr->head_node_ptr;

   for (; NULL != metric_node_ptr; metric_node_ptr = metric_node_ptr->next_ptr)
   {
      irm_node_obj_t *metric_obj_ptr = (irm_node_obj_t *)metric_node_ptr->obj_ptr;
      // irm_fill_processor_metric(irm_ptr, metric_obj_ptr, &current_query, frame_size_ms, &idle_pcycles[0]);
      irm_fill_pool_metrics(irm_ptr, instance_obj_ptr, metric_obj_ptr, frame_size_ms);
   }
   return AR_EOK;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
static ar_result_t irm_handle_module_metrics(irm_t *irm_ptr, irm_node_obj_t *block_obj_ptr, uint32_t frame_size_ms)
{
   ar_result_t result = AR_EOK;

   spf_list_node_t *module_node_ptr = block_obj_ptr->head_node_ptr;

   // For each module,
   for (; NULL != module_node_ptr; LIST_ADVANCE(module_node_ptr))
   {
      irm_node_obj_t *instance_obj_ptr = (irm_node_obj_t *)module_node_ptr->obj_ptr;
      if (NULL != instance_obj_ptr)
      {

         bool_t any_nonheap_metric = FALSE;

         // read heap metric first, as it doesn't need mutex.
         for (spf_list_node_t *metric_node_ptr = instance_obj_ptr->head_node_ptr; NULL != metric_node_ptr;
              LIST_ADVANCE(metric_node_ptr))
         {
            irm_node_obj_t *metric_obj_ptr = (irm_node_obj_t *)metric_node_ptr->obj_ptr;
            if (NULL != metric_obj_ptr)
            {
               if (IRM_METRIC_ID_HEAP_INFO == metric_obj_ptr->id)
               {
                  result = irm_fill_module_metrics(irm_ptr,
                                                   instance_obj_ptr,
                                                   metric_obj_ptr,
                                                   frame_size_ms,
                                                   block_obj_ptr->id);
               }
               else if (!any_nonheap_metric)
               {
                  any_nonheap_metric = TRUE;
               }
            }
         }

         if (!any_nonheap_metric)
         {
            continue;
         }

         bool_t is_mutex_valid =
            (NULL != instance_obj_ptr->mod_mutex_ptr) && ((NULL != *instance_obj_ptr->mod_mutex_ptr));

         // If the mutex exists we have to hang onto it for the duration of collecting all the metrics
         // to avoid having the calculations be incorrect because the module updates between different metrics.
         // Packets and pcycles need to be read in one-go. Even though heap doesn't need mutex, we simply lock it
         if (is_mutex_valid)
         {
            posal_mutex_lock(*instance_obj_ptr->mod_mutex_ptr);
         }

         for (spf_list_node_t *metric_node_ptr = instance_obj_ptr->head_node_ptr; NULL != metric_node_ptr;
              LIST_ADVANCE(metric_node_ptr))
         {
            irm_node_obj_t *metric_obj_ptr = (irm_node_obj_t *)metric_node_ptr->obj_ptr;
            if (NULL != metric_obj_ptr)
            {
               if (IRM_METRIC_ID_HEAP_INFO == metric_obj_ptr->id)
               {
                  continue;
               }

               if (is_mutex_valid)
               {
                  result = irm_fill_module_metrics(irm_ptr,
                                                   instance_obj_ptr,
                                                   metric_obj_ptr,
                                                   frame_size_ms,
                                                   block_obj_ptr->id);
               }
               else
               {
                  // Fix invalid packet
                  AR_MSG(DBG_MED_PRIO,
                         "IRM: IID = 0x%X, handle = 0x%X, fix packet due to invalid mutex",
                         instance_obj_ptr->id,
                         instance_obj_ptr->handle_ptr);
                  irm_report_metric_t *report_metric_ptr =
                     (irm_report_metric_t *)metric_obj_ptr->metric_info.metric_payload_ptr;
                  report_metric_ptr->num_metric_payloads = irm_ptr->core.timer_tick_counter + 1;
                  report_metric_ptr++;
                  uint32_t                     metric_size = irm_get_metric_payload_size(metric_obj_ptr->id);
                  irm_report_metric_payload_t *report_metric_payload_ptr =
                     (irm_report_metric_payload_t *)(((uint8_t *)report_metric_ptr) +
                                                     (irm_ptr->core.timer_tick_counter *
                                                      (sizeof(irm_report_metric_payload_t) + metric_size)));
                  report_metric_payload_ptr->is_valid = 0;
               }
            }
         }

         if (is_mutex_valid)
         {
            posal_mutex_unlock(*instance_obj_ptr->mod_mutex_ptr);
         }
      }
   }
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
static void irm_reset_profile_enable_state()
{
   irm_wrapper_profile_enable(IRM_BLOCK_ID_PROCESSOR, 0);
   irm_wrapper_profile_enable(IRM_BLOCK_ID_CONTAINER, 0);
   irm_wrapper_profile_enable(IRM_BLOCK_ID_MODULE, 0);
   irm_wrapper_profile_enable(IRM_BLOCK_ID_POOL, 0);
   irm_wrapper_profile_enable(IRM_BLOCK_ID_STATIC_MODULE, 0);
}
/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_profiler_init(irm_t *irm_ptr)
{
   irm_q6_profiler_info_t *q6_info_ptr    = NULL;
   spf_list_node_t *       block_node_ptr = NULL;

   if (NULL == irm_ptr->core.profiler_handle_ptr)
   {
      irm_ptr->core.profiler_handle_ptr = posal_memory_malloc(sizeof(irm_q6_profiler_info_t), irm_ptr->heap_id);
      if (NULL == irm_ptr->core.profiler_handle_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "IRM: failed to allocate memory for profiler");
         return AR_ENOMEMORY;
      }
      memset(irm_ptr->core.profiler_handle_ptr, 0, sizeof(irm_q6_profiler_info_t));
      AR_MSG(DBG_HIGH_PRIO, "IRM: profiler memory allocated");
   }

   // Disable all profilers here, and based on what blocks are present, enable only those profilers
   irm_reset_profile_enable_state();

   q6_info_ptr    = (irm_q6_profiler_info_t *)irm_ptr->core.profiler_handle_ptr;
   block_node_ptr = irm_ptr->core.block_head_node_ptr;

   for (; NULL != block_node_ptr; block_node_ptr = block_node_ptr->next_ptr)
   {
      irm_node_obj_t *block_obj_ptr = (irm_node_obj_t *)block_node_ptr->obj_ptr;
      if (NULL == block_obj_ptr)
      {
         continue;
      }
      switch (block_obj_ptr->id)
      {
         case IRM_BLOCK_ID_PROCESSOR:
         case IRM_BLOCK_ID_CONTAINER:
         case IRM_BLOCK_ID_MODULE:
         case IRM_BLOCK_ID_POOL:
         case IRM_BLOCK_ID_STATIC_MODULE:
         {
            if (0 != irm_wrapper_profile_enable(block_obj_ptr->id, 1))
            {
               AR_MSG(DBG_ERROR_PRIO, "IRM: Profile enable fail, block id = %lu", block_obj_ptr->id);
            }
            // Collect the q6 hw info, this is used later
            irm_get_q6_hw_info(&q6_info_ptr->q6_hw_info);
            break;
         }
         default:
         {
            AR_MSG(DBG_HIGH_PRIO, "IRM: Block ID %lu not supported", block_obj_ptr->id);
            break;
         }
      }
   }
   return AR_EOK;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
void irm_profiler_deinit(irm_t *irm_ptr)
{
   if (NULL != irm_ptr->core.profiler_handle_ptr)
   {
      irm_reset_profile_enable_state();
      posal_memory_free(irm_ptr->core.profiler_handle_ptr);
      AR_MSG(DBG_HIGH_PRIO, "IRM: profiler memory deallocated");
   }
   irm_ptr->core.profiler_handle_ptr = NULL;
}
/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_collect_and_fill_info(irm_t *irm_ptr, uint32_t frame_size_ms)
{
   ar_result_t result = AR_EOK;
   if (!irm_ptr || !irm_ptr->core.profiler_handle_ptr)
   {
      return AR_EFAILED;
   }
   spf_list_node_t *block_node_ptr = irm_ptr->core.block_head_node_ptr;
   for (; NULL != block_node_ptr; block_node_ptr = block_node_ptr->next_ptr)
   {
      irm_node_obj_t *block_obj_ptr = (irm_node_obj_t *)block_node_ptr->obj_ptr;
      if (NULL != block_obj_ptr)
      {
         switch (block_obj_ptr->id)
         {
            case IRM_BLOCK_ID_PROCESSOR:
            {
               result |= irm_handle_processor_metrics(irm_ptr, block_obj_ptr, frame_size_ms);
               break;
            }
            case IRM_BLOCK_ID_CONTAINER:
            {
               result |= irm_handle_container_metrics(irm_ptr, block_obj_ptr, frame_size_ms);
               break;
            }
            case IRM_BLOCK_ID_STATIC_MODULE:
            {
               result |= irm_handle_static_module_metrics(irm_ptr, block_obj_ptr, frame_size_ms);
               break;
            }

            case IRM_BLOCK_ID_MODULE:
            {
               result |= irm_handle_module_metrics(irm_ptr, block_obj_ptr, frame_size_ms);
               break;
            }
            case IRM_BLOCK_ID_POOL:
            {
               result |= irm_handle_pool_metrics(irm_ptr, block_obj_ptr, frame_size_ms);
               break;
            }
            default:
            {
               AR_MSG(DBG_ERROR_PRIO, "IRM: Block ID %lu not supported", block_obj_ptr->id);
               break;
            }
         }
      }
   }
   return result;
}
