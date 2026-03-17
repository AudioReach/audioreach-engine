#ifndef _IRM_API_H_
#define _IRM_API_H_
/**
 * \file irm_api.h
 * \brief
 *      API header for Integrated Resource Monitor (IRM).
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*----------------------------------------------------------------------------------------------------------------------
 Include files
 ---------------------------------------------------------------------------------------------------------------------*/
#include "ar_defs.h"
#include "apm_graph_properties.h"

/** @addtogroup spf_mods_irm
@{ */

/** Minor version of the Integrated Resource Monitor (IRM) module. */
#define IRM_MINOR_VERSION 2

// clang-format off

/** Type of IRM processor is invalid or unknown. */
#define IRM_PROCESSOR_TYPE_UNSUPPORTED          0

/** IRM processor type is a Hexagon processor. */
#define IRM_PROCESSOR_TYPE_HEXAGON              1

/** IRM processor type is an Arm processor. */
#define IRM_PROCESSOR_TYPE_ARM                  2

/** IRM processor type is an x86 processor. */
#define IRM_PROCESSOR_TYPE_x86                  3


/** ID for the IRM block is invalid or unknown. */
#define IRM_BLOCK_ID_UNSUPPORTED                0

/** IRM block is a processor. */
#define IRM_BLOCK_ID_PROCESSOR                  1

/** IRM block is a container.
 * - The heap numbers reported by containers do not
 *   include the heap allocated by modules placed in the container.
 *   In order to get the overall use case heap, we need to add.
 * - Packet count, processor cycles reported by the container includes
 *   module contribution */
#define IRM_BLOCK_ID_CONTAINER                  2

/** IRM block is a module. @newpage */
#define IRM_BLOCK_ID_MODULE                     3

/** IRM block is a bufpool.
 *  Block used to measure the heap usage in the fwk pools @newpage */
#define IRM_BLOCK_ID_POOL 4

/** IRM block is a static module, eg VCPM, APM, etc. @newpage */
#define IRM_BLOCK_ID_STATIC_MODULE 5

#define IRM_POOL_ID_LIST 0

/*--------------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------------*/

/** Identifier for the Integrated Resource Monitor (IRM) module.

    This module supports the following metric and parameter IDs:
     - #IRM_METRIC_ID_PROCESSOR_CYCLES
     - #IRM_BASIC_METRIC_ID_CURRENT_CLOCK
     - #IRM_METRIC_ID_HEAP_INFO
     - #IRM_METRIC_ID_PACKET_COUNT
     - #IRM_METRIC_ID_MEM_TRANSACTIONS
     - #PARAM_ID_IRM_VERSION
     - #PARAM_ID_IRM_SYSTEM_CAPABILITIES
     - #PARAM_ID_IRM_METRIC_CAPABILITIES
     - #PARAM_ID_IRM_PROFILING_PARAMS
     - #PARAM_ID_IRM_ENABLE_DISABLE_PROFILING
     - #PARAM_ID_IRM_REPORT_METRICS
 */
#define IRM_MODULE_INSTANCE_ID 6
/*--------------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------------*/

// clang-format on
/*--------------------------------------------------------------------------------------------------------------------*/

/** Number of elapsed processor cycles during the given frame period
    (frame_size_ms).

    Processor cycles can be used to calculate the processor load in cycles per
    second (CPS), where:
    - CPS = (cycles * 1000 / frame_size_ms);
    - MCPS = CPS/1000000

    @msgpayload{irm_metric_id_processor_cycles_t}
    @table{weak__irm__metric__id__processor__cycles__t}
 */
#define IRM_METRIC_ID_PROCESSOR_CYCLES 0x0800120B


/** @weakgroup weak_irm_metric_id_processor_cycles_t
@{ */
#include "spf_begin_pack.h"
struct irm_metric_id_processor_cycles_t
{
   uint32_t processor_cycles;
   /**< Number of processor cycles that elapsed during the frame size time
        (in Hertz). */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_metric_id_processor_cycles_t */
typedef struct irm_metric_id_processor_cycles_t irm_metric_id_processor_cycles_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/** Current system clock rate.

    @msgpayload{irm_metric_id_current_clock_t}
    @table{weak__irm__metric__id__current__clock__t} @newpage
 */
#define IRM_BASIC_METRIC_ID_CURRENT_CLOCK 0x0800120C


/** @weakgroup weak_irm_metric_id_current_clock_t
@{ */
#include "spf_begin_pack.h"
struct irm_metric_id_current_clock_t
{
   uint32_t current_clock;
   /**< Current system clock rate (in kilohertz). */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_metric_id_current_clock_t */
typedef struct irm_metric_id_current_clock_t irm_metric_id_current_clock_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/** Information related to the heap memory usage during the frame size period
    (#irm_report_metric_payload_t::frame_size_ms), including:
    - Current heap usage (in bytes)
    - Peak heap usage (in bytes)
    - Maximum allowed heap allocation

    @msgpayload{irm_metric_id_heap_info_t}
    @tablens{weak__irm__metric__id__heap__info__t}

    @par Heap payload (irm_per_heap_id_info_payload_t)
    @table{weak__irm__per__heap__id__info__payload__t}
 */
#define IRM_METRIC_ID_HEAP_INFO 0x0800120D


/** @weakgroup weak_irm_metric_id_heap_info_t
@{ */
/**
  Contains information about heap statistics per heap ID. This structure is
  followed by a variable array structure where the number of elements =
  num_heap_id.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct irm_metric_id_heap_info_t
{
   uint32_t num_heap_id;
   /**< Number of heap IDs. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_metric_id_heap_info_t */
typedef struct irm_metric_id_heap_info_t irm_metric_id_heap_info_t;


/** @weakgroup weak_irm_per_heap_id_info_payload_t
@{ */
#include "spf_begin_pack.h"
struct irm_per_heap_id_info_payload_t
{
   uint32_t heap_id;
   /**< Heap ID. 0 - non-island. 1 -> island.*/

   uint32_t current_heap_usage;
   /**< Current heap usage (in bytes). */

   uint32_t max_allowed_heap_size;
   /**< Maximum possible heap usage (in bytes). The heap usage percentage is
        calculated as follows: \n
        @indent{12pt} heap usage = current_heap_usage/max_allowed_heap_size */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_per_heap_id_info_payload_t */
typedef struct irm_per_heap_id_info_payload_t irm_per_heap_id_info_payload_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/** Number of packets executed during the frame size period.

    This macro is used to calculate the packets per second per hardware thread
    statistics, where:
    - PPS = packet_count * 1000/(frame_size_ms)
    - MPPS = PPS/1000000

    @msgpayload{irm_metric_id_packet_count_t}
    @table{weak__irm__metric__id__packet__count__t} @newpage
 */
#define IRM_METRIC_ID_PACKET_COUNT 0x0800120E


/** @weakgroup weak_irm_metric_id_packet_count_t
@{ */
#include "spf_begin_pack.h"
struct irm_metric_id_packet_count_t
{
   uint32_t packet_count;
   /**< Number of packets executed in the frame size period
        (#irm_report_metric_payload_t::frame_size_ms). */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_metric_id_packet_count_t */
typedef struct irm_metric_id_packet_count_t irm_metric_id_packet_count_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/** Number of memory read and writes that occurred during the frame size period
    (#irm_report_metric_payload_t::frame_size_ms).

    This macro is used to calculate the memory bandwidth usage:
    - read_bw = read_memory_transactions_bytes * 1000 / frame_size_ms
      (in bytes per second)
    - write_bw = write_memory_transactions_bytes * 1000 / frame_size_ms
      (in bytes per second)

    @msgpayload{irm_metric_id_mem_transactions_t}
    @table{weak__irm__metric__id__mem__transactions__t}
 */
#define IRM_METRIC_ID_MEM_TRANSACTIONS 0x0800120F


/** @weakgroup weak_irm_metric_id_mem_transactions_t
@{ */
#include "spf_begin_pack.h"
struct irm_metric_id_mem_transactions_t
{
   uint32_t read_mem_transactions_bytes;
   /**< Number of read memory transactions (in bytes). */

   uint32_t write_mem_transactions_bytes;
   /**< Number of write memory transactions (in bytes). */
}
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_metric_id_mem_transactions_t */
typedef struct irm_metric_id_mem_transactions_t irm_metric_id_mem_transactions_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------------*/

/** Identifier for the parameter that gets the IRM version.

    @msgpayload{param_id_irm_version_t}
    @table{weak__param__id__irm__version__t} @newpage
*/
#define PARAM_ID_IRM_VERSION 0x0800119E


/** @weakgroup weak_param_id_irm_version_t
@{ */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_irm_version_t
{
   uint32_t version;
   /**< Version of the IRM. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_param_id_irm_version_t */
typedef struct param_id_irm_version_t param_id_irm_version_t;


/*--------------------------------------------------------------------------------------------------------------------*/
/* IRM PRIVATE METRICS BEGIN                                                                                        */
/*--------------------------------------------------------------------------------------------------------------------*/
#ifndef DOXYGEN_SHOULD_SKIP_THIS

#define IRM_METRIC_ID_Q6_HW_INFO 0x08001322
struct irm_metric_id_q6_hw_info_t
{
   uint32_t num_hw_threads;   /* Number of hardware threads present */
   uint32_t q6_arch_version;  /* Q6 architecture version */
}
#include "spf_end_pack.h"
;
typedef struct irm_metric_id_q6_hw_info_t irm_metric_id_q6_hw_info_t;
/*--------------------------------------------------------------------------------------------------------------------*/
#define IRM_METRIC_ID_TIMESTAMP 0x08001323
#include "spf_begin_pack.h"
struct irm_metric_id_timestamp_t
{
   uint32_t timestamp_ms_lsw;  /* Time stamp during which the profiling was done in ms*/
   uint32_t timestamp_ms_msw;  /* Time stamp during which the profiling was done in ms*/
}
#include "spf_end_pack.h"
;
typedef struct irm_metric_id_timestamp_t irm_metric_id_timestamp_t;

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
/*--------------------------------------------------------------------------------------------------------------------*/
/* IRM PRIVATE METRICS END                                                                                          */
/*--------------------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------------*/

/** Identifier for the parameter that gets the IRM system capabilities.

    @msgpayload{param_id_irm_system_capabilities_t}
    @tablens{weak__param__id__irm__system__capabilities__t}

    @par System capabilities (irm_system_capabilities_t)
    @table{weak__irm__system__capabilities__t} @newpage
*/
#define PARAM_ID_IRM_SYSTEM_CAPABILITIES 0x0800108B


/** @weakgroup weak_param_id_irm_system_capabilities_t
@{ */
/** Contains information about system capabilities per processor domain ID.
    This structure is followed by a variable array structure
    (%irm_system_capabilities_t) where the number of elements =
    num_proc_domain.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_irm_system_capabilities_t
{
   uint32_t num_proc_domain;
   /**< Number of processor domain IDs. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_param_id_irm_system_capabilities_t */
typedef struct param_id_irm_system_capabilities_t param_id_irm_system_capabilities_t;


/** @weakgroup weak_irm_system_capabilities_t
@{ */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct irm_system_capabilities_t
{
   uint32_t proc_domain;
   /**< ID for the processor domain.

        @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_ADSP
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend */

   uint32_t processor_type;
   /**< Type of processor the IRM is running.

        @valuesbul
        - #IRM_PROCESSOR_TYPE_UNSUPPORTED
        - #IRM_PROCESSOR_TYPE_HEXAGON
        - #IRM_PROCESSOR_TYPE_ARM
        - #IRM_PROCESSOR_TYPE_x86 @tablebulletend */

   uint32_t min_profiling_period_us;
   /**< Minimum number of profiling periods in which statistics are collected.

        @values 1000..0xFFFFFFFF (microseconds) */

   uint32_t min_profile_per_report;
   /**< Minimum number of times profiling is to be done before it must be
        reported.

        @values 1..0xFFFFFFFF (microseconds) */

   uint32_t max_num_containers_supported;
   /**< Maximum number of threads supported by the IRM that is to be
        profiled. */

   uint32_t max_module_supported;
   /**< Maximum number of modules supported by the IRM that is to be
        profiled. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_system_capabilities_t */
typedef struct irm_system_capabilities_t irm_system_capabilities_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------------*/

/** Identifier for the parameter that gets IRM metric capabilities.

    @msgpayload{param_id_irm_metric_capabilities_t}
    @tablens{weak__param__id__irm__metric__capabilities__t}

    @par IRM metric capabilities (irm_metric_capabilities_t)
    @tablens{weak__irm__metric__capabilities__t}

    @par Metric identifiers (irm_metric_capability_block_t)
    @tablens{weak__irm__metric__capability__block__t} @newpage
*/
#define PARAM_ID_IRM_METRIC_CAPABILITIES 0x080010BC


/** @weakgroup weak_param_id_irm_metric_capabilities_t
@{ */
/** Contains metric capabilities information per processor domain. This
    structure is followed by a variable array structure
   (%irm_metric_capabilities_t) where the number of elements =
   num_proc_domain.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_irm_metric_capabilities_t
{
   uint32_t num_proc_domain;
   /**< Number of processor domain IDs. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_param_id_irm_metric_capabilities_t */
typedef struct param_id_irm_metric_capabilities_t param_id_irm_metric_capabilities_t;


/** @weakgroup weak_irm_metric_capabilities_t
@{ */
/** Contains metric capabilities information per block. This structure is
    followed by a variable array structure (%irm_metric_capability_block_t)
    where the number of elements = num_blocks.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct irm_metric_capabilities_t
{
   uint32_t proc_domain;
   /**< ID for the processor domain.

        @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_ADSP
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend */

   uint32_t num_blocks;
   /**< Number of blocks in the array. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_metric_capabilities_t */
typedef struct irm_metric_capabilities_t irm_metric_capabilities_t;


/** @weakgroup weak_irm_metric_capability_block_t
@{ */
/**
  Contains metric IDs. This structure is followed by a variable array of metric
  IDs where the number of elements = num_metric_ids.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct irm_metric_capability_block_t
{
   uint32_t block_id;
   /**< Valid block IDs.

        @valuesbul
        - #IRM_BLOCK_ID_UNSUPPORTED
        - #IRM_BLOCK_ID_PROCESSOR
        - #IRM_BLOCK_ID_CONTAINER
        - #IRM_BLOCK_ID_MODULE @tablebulletend */

   uint32_t num_metric_ids;
   /**< Number of metric IDs. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_metric_capability_block_t */
typedef struct irm_metric_capability_block_t irm_metric_capability_block_t;


/*--------------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------------*/

/** Identifier for the parameter that gets the IRM profiling parameters.

    @msgpayload{param_id_irm_profiling_params_t}
    @table{weak__param__id__irm__profiling__params__t} @newpage
*/
#define PARAM_ID_IRM_PROFILING_PARAMS 0x0800119A


/** @weakgroup weak_param_id_irm_profiling_params_t
@{ */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_irm_profiling_params_t
{
   uint32_t proc_domain;
   /**< ID for the processor domain.

        @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_ADSP
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend */

   uint32_t profiling_period_us;
   /**< Profiling period in which statistics is to be collected (in
        microseconds). */

   uint32_t num_profiles_per_report;
   /**< Number of profiles to be done before they are to be reported. The
        report period is calculated as follows:

        @indent{12pt} report_period = profiling_period_us * num_profiles_per_report. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_param_id_irm_profiling_params_t */
typedef struct param_id_irm_profiling_params_t param_id_irm_profiling_params_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------------*/
/** Identifier for the parameter that enables or disables profiling metrics on
    specific IRM blocks and instances.

    @msgpayload{param_id_enable_disable_metrics_t}
    @tablens{weak__param__id__enable__disable__metrics__t}

    @par Block configuration {irm_enable_disable_block_t}
    @tablens{weak__irm__enable__disable__block__t} @newpage
*/
#define PARAM_ID_IRM_ENABLE_DISABLE_PROFILING 0x080011AA


/** @weakgroup weak_param_id_enable_disable_metrics_t
@{ */
/** Contains the enable and disable information per block. This structure is
    followed by a variable array structure (%irm_enable_disable_block_t) where
    the number of elements = num_blocks.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_enable_disable_metrics_t
{
   uint32_t is_enable;
   /**< Indicates whether the entire variable array is enabled.

        @valuesbul
        - 0x0 -- Disabled
        - 0x1 -- Enabled @tablebulletend */

   uint32_t proc_domain;
   /**< ID for the processor domain.

        @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_ADSP
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend */

   uint32_t num_blocks;
   /**< Number of blocks.

        If is_enable = 0 and num_blocks = 0, the IRM disables all configured
        block IDs, instance IDs, and metric IDs as specified by irm_enable_disable_block_t */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_param_id_enable_disable_metrics_t */
typedef struct param_id_enable_disable_metrics_t param_id_enable_disable_metrics_t;


/** @weakgroup weak_irm_enable_disable_block_t
@{ */
/** Contains metrics IDs. This structure is followed by a variable array where
    the number of elements = num_metric_ids.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct irm_enable_disable_block_t
{
   uint32_t block_id;
   /**< Valid block IDs.

        @valuesbul
        - #IRM_BLOCK_ID_UNSUPPORTED
        - #IRM_BLOCK_ID_PROCESSOR
        - #IRM_BLOCK_ID_CONTAINER
        - #IRM_BLOCK_ID_MODULE @tablebulletend */

   uint32_t instance_id;
   /**< Instance ID for the block. */

   uint32_t num_metric_ids;
   /**< Number of metric IDs.

        When is_enable = 0 and num_metric_ids = 0, the IRM disables all metric
        IDs for the given instance ID/block ID. Metric ID is of type uint32_t*/
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_enable_disable_block_t */
typedef struct irm_enable_disable_block_t irm_enable_disable_block_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------------*/

/** Identifier for the parameter that reports the profiled information of all
    enabled metrics for each instance of provided block IDs.

    @msgpayload{param_id_report_metrics_t}
    @tablens{weak__param__id__report__metrics__t}

    @par Metrics block (irm_report_metrics_block_t)
    @tablens{weak__irm__report__metrics__block__t}

    @par Metrics data (irm_report_metric_t)
    @tablens{weak__irm__report__metric__t} @newpage

    @par Collected metric information (irm_report_metric_payload_t)
    @tablens{weak__irm__report__metric__payload__t}
*/
#define PARAM_ID_IRM_REPORT_METRICS 0x08001210


/** @weakgroup weak_param_id_report_metrics_t
@{ */
/** Contains collected metric information per block. This structure is
    followed by a variable array structure (%irm_report_metrics_block_t) where
    the number of elements = num_blocks.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_report_metrics_t
{
   uint32_t routing_id;
   /**< Routing ID for the block. Currently, this field is not used. */

   uint32_t proc_domain;
   /**< ID for the processor domain.

        @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_ADSP
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend */

   uint32_t num_blocks;
   /**< Number of blocks that are collected. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_param_id_report_metrics_t */
typedef struct param_id_report_metrics_t param_id_report_metrics_t;


/** @weakgroup weak_irm_report_metrics_block_t
@{ */
/** Contains information about a block ID, instance ID, and the number of
    metric IDs. This structure is followed by a variable array structure where
    the number of elements = num_metric_ids.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct irm_report_metrics_block_t
{
   uint32_t block_id;
   /**< Valid block IDs.

        @valuesbul
        - #IRM_BLOCK_ID_UNSUPPORTED
        - #IRM_BLOCK_ID_PROCESSOR
        - #IRM_BLOCK_ID_CONTAINER
        - #IRM_BLOCK_ID_MODULE @tablebulletend */

   uint32_t instance_id;
   /**< Instance ID for the block */

   uint32_t num_metric_ids;
   /**< Number of metric IDs. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_report_metrics_block_t */
typedef struct irm_report_metrics_block_t irm_report_metrics_block_t;


/** @weakgroup weak_irm_report_metric_t
@{ */
/** Contains information about a metric ID and the number of metric payloads.
    This structure is followed by a variable array structure
    (%irm_report_metric_payload_t) where the number of elements =
    num_metric_payloads.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct irm_report_metric_t
{
   uint32_t metric_id;
   /**< ID for the metric information. */

   uint32_t num_metric_payloads;
   /**< Number of payloads present for the given metric ID. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_report_metric_t */
typedef struct irm_report_metric_t irm_report_metric_t;


/** @weakgroup weak_irm_report_metric_payload_t
@{ */
/** Contains collected metric information per profiling period. This structure
    is followed by the actual payload (size = payload_size) that contains the
    metric information.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct irm_report_metric_payload_t
{
   uint32_t is_valid;
   /**< Indicates whether the payload information is valid.

        @valuesbul
        - 0 -- Disable
        - 1 -- Enable @tablebulletend */

   uint32_t frame_size_ms;
   /**< Frame size (in milliseconds) during which the metric data was
        accumulated. */

   uint32_t payload_size;
   /**< Size of the metric payload. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_report_metric_payload_t */
typedef struct irm_report_metric_payload_t irm_report_metric_payload_t;


//TODO guid + fix name for irm prefixing
/*--------------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------------*/
/** Identifier for the parameter that enables selected profiling metrics on
    seleted IRM blocks for all instances.

*/
#define IRM_PARAM_ID_ENABLE_ALL_PROFILING 0x080014EC

/** @weakgroup weak_param_id_enable_all_t
@{ */
/** Contains the enable and disable information per block. This structure is
    followed by a variable array structure (%irm_enable_all_block_t) where
    the number of elements = num_blocks.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_enable_all_t
{
   uint32_t proc_domain;
   /**< ID for the processor domain.

        @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_ADSP
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend */

   uint32_t is_enable;
   /**< Indicates whether to enable or disable all.

        @valuesbul
        - 0x0 -- Disabled
        - 0x1 -- Enabled @tablebulletend */
   uint32_t num_blocks;
   /**< Number of blocks. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_param_id_enable_all_t */
typedef struct param_id_enable_all_t param_id_enable_all_t;

/** @weakgroup weak_irm_enable_all_block_t
@{ */
/** Contains metrics IDs. This structure is followed by a variable array where
    the number of elements = num_metric_ids.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct irm_enable_all_block_t
{
   uint32_t block_id;
   /**< Valid block IDs.

        @valuesbul
        - #IRM_BLOCK_ID_UNSUPPORTED
        - #IRM_BLOCK_ID_PROCESSOR
        - #IRM_BLOCK_ID_CONTAINER
        - #IRM_BLOCK_ID_MODULE @tablebulletend */

   uint32_t num_metric_ids;
   /**< Number of metric IDs immediately following the payload*/
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/** @} */ /* end_weakgroup weak_irm_enable_all_block_t */
typedef struct irm_enable_all_block_t irm_enable_all_block_t;
/*--------------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------------*/

/** @} */ /* end_addtogroup spf_mods_irm */

#endif //_IRM_API_H_
