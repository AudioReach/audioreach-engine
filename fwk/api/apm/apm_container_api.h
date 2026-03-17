#ifndef _APM_CONTAINER_API_H_
#define _APM_CONTAINER_API_H_
/**
 * \file apm_container_api.h
 * \brief
 *    This file contains APM container commands and events structures definitions.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_defs.h"
#include "apm_graph_properties.h"

#ifdef __cplusplus
extern "C"
{
#endif /*__cplusplus*/

/*====================================================================================================================*/
/*====================================================================================================================*/

/** @ingroup spf_apm_container_props
    Identifier for the parameter that configures the container properties.

    @msgpayload
    apm_param_id_container_cfg_t \n
    @indent{12pt} apm_container_cfg_t \n
    @indent{12pt} apm_container_type_t
*/
#define APM_PARAM_ID_CONTAINER_CONFIG 0x08001000

/*# h2xmlp_parameter   {"Container Configuration",
                         APM_PARAM_ID_CONTAINER_CONFIG}
    h2xmlp_description {ID for the parameter that configures the container
                        properties.}
    h2xmlp_toolPolicy  {RTC; Calibration} */

/** @ingroup spf_apm_container_props
    Payload for #APM_PARAM_ID_CONTAINER_CONFIG.

    Immediately following this structure is a variable length array of
    structure objects corresponding to each container configuration and
    properties. The length of the array is determined by number of containers
    being configured.
 */
#include "spf_begin_pack.h"
struct apm_param_id_container_cfg_t
{
   uint32_t num_container;
   /**< Number of containers being configured. */

   /*#< h2xmle_description {Number of containers being configured.}
        h2xmle_range       {0..0xFFFFFFFF}
        h2xmle_default     {0} */
}
#include "spf_end_pack.h"
;
typedef struct apm_param_id_container_cfg_t apm_param_id_container_cfg_t;

/** @ingroup spf_apm_container_props
    Payload for #APM_PARAM_ID_CONTAINER_CONFIG. This structure contains
    container configuration parameters.

    Immediately following this structure is a variable length array of
    container property structures. The length of the array is determined by the
    number of properties being configured.
 */
#include "spf_begin_pack.h"
struct apm_container_cfg_t
{
   uint32_t container_id;
   /**< Unique identifier for the container. */

   /*#< h2xmle_description {Unique ID for the container.}
        h2xmle_range       {0..0xFFFFFFFF}
        h2xmle_default     {0} */

   uint32_t num_prop;
   /**< Number of properties for this sub-graph. */

   /*#< h2xmle_description {Number of properties for this sub-graph.}
        h2xmle_range       {0..0xFFFFFFFF}
        h2xmle_default     {0} */
}
#include "spf_end_pack.h"
;
typedef struct apm_container_cfg_t apm_container_cfg_t;

/** @ingroup spf_apm_container_props
    Payload for #APM_PARAM_ID_CONTAINER_CONFIG and
    #APM_CONTAINER_PROP_ID_CONTAINER_TYPE.
 */
#include "spf_begin_pack.h"
struct apm_container_type_t
{
   uint32_t type;
   /**< Type of container.

        @values #containerCap */

   /*#< @h2xmle_rangeEnum   {containerCap}
        @h2xmle_default     {APM_PROP_ID_INVALID}
        @h2xmle_description {Type of container.} */
}
#include "spf_end_pack.h"
;
typedef struct apm_container_type_t apm_container_type_t;

/*# h2xmlgp_configType  {"CONT_CFG"}
    @h2xmlgp_config      {"CONT_CFG", "APM_PARAM_ID_CONTAINER_CONFIG",
                           APM_PARAM_ID_CONTAINER_CONFIG}
    @h2xmlgp_description {Parameter ID used when configuring container
                          properties.}
    @{                   <-- Start of container_cfg --> */

/*--------------------------------------------------------------------------------------------------------------------*/
/** @ingroup spf_apm_container_props
    Container property identifier for the container type.

    @msgpayload
    apm_cont_prop_id_type_t \n
    @indent{12pt} apm_container_type_t
*/
#define APM_CONTAINER_PROP_ID_CONTAINER_TYPE 0x08001011

/*# @h2xmlp_property    {"Container Type",
                          APM_CONTAINER_PROP_ID_CONTAINER_TYPE}
    @h2xmlp_description {Container property ID for the container type.} */

/*# @h2xmlp_insertSubStruct {apm_container_type_t} */

/** @ingroup spf_apm_container_props
    Payload for #APM_CONTAINER_PROP_ID_CONTAINER_TYPE.
    Immediately following this structure is a single instance of
    %apm_container_type_t.
*/
#include "spf_begin_pack.h"
struct apm_cont_prop_id_type_t
{
   uint32_t version;
   /**< Version of the property ID payload for the container type. */

   /*#< @h2xmle_range       {1..0xFFFFFFFF}
        @h2xmle_default     {1}
        @h2xmle_description {Version of the property ID payload for the
                             container type.} */
   /* version can never be incremented to 2 because it will change below
      var length array. */

   apm_container_type_t type_id;
   /**< Identifier for the container type. */

   /*#< @h2xmle_range             {0..0xFFFFFFFF}
        @h2xmle_default           {APM_PROP_ID_INVALID}
        @h2xmle_description       {ID for the container property.}
        @h2xmle_variableArraySize {version} */
   /* This is not meant to be a variable size array, but due to backward
      compatibility with previous struct, which based based on container
      capability ID, we had to do this. */
}
#include "spf_end_pack.h"
;
typedef struct apm_cont_prop_id_type_t apm_cont_prop_id_type_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/** @ingroup spf_apm_container_props
    Container property identifier for the graph position.

    @msgpayload
    apm_cont_prop_id_graph_pos_t
*/
#define APM_CONTAINER_PROP_ID_GRAPH_POS 0x08001012

/** @ingroup spf_apm_container_props
    Position of the container graph in the stream. */
#define APM_CONT_GRAPH_POS_STREAM 0x1

/** @ingroup spf_apm_container_props
    Position of the container graph, per stream per device. */
#define APM_CONT_GRAPH_POS_PER_STR_PER_DEV 0x2

/** @ingroup spf_apm_container_props
    Position of the container graph on a stream device. */
#define APM_CONT_GRAPH_POS_STR_DEV 0x3

/** @ingroup spf_apm_container_props
    Position of the container graph on a global device. @newpage */
#define APM_CONT_GRAPH_POS_GLOBAL_DEV 0x4

/*# @h2xmlp_property    {"Graph Position", APM_CONTAINER_PROP_ID_GRAPH_POS}
    @h2xmlp_description {Container property ID for the graph position.} */

/** @ingroup spf_apm_container_props
    Payload for #APM_CONTAINER_PROP_ID_GRAPH_POS.
 */
#include "spf_begin_pack.h"
struct apm_cont_prop_id_graph_pos_t
{
   uint32_t graph_pos;
   /**< Position of the container in the graph.

        @valuesbul
        - #APM_CONT_GRAPH_POS_STREAM
        - #APM_CONT_GRAPH_POS_PER_STR_PER_DEV
        - #APM_CONT_GRAPH_POS_STR_DEV
        - #APM_CONT_GRAPH_POS_GLOBAL_DEV
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend */

   /*#< @h2xmle_rangeList   {"DONT_CARE"=0xFFFFFFFF,
                             "STREAM"=APM_CONT_GRAPH_POS_STREAM,
                             "PSPD"=APM_CONT_GRAPH_POS_PER_STR_PER_DEV,
                             "STR_DEV"=APM_CONT_GRAPH_POS_STR_DEV,
                             "GLOBAL DEV"=APM_CONT_GRAPH_POS_GLOBAL_DEV}
        @h2xmle_default     {0xFFFFFFFF}
        @h2xmle_description {Position of the container in the graph.} */
}
#include "spf_end_pack.h"
;
typedef struct apm_cont_prop_id_graph_pos_t apm_cont_prop_id_graph_pos_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/** @ingroup spf_apm_container_props
    Container property identifier for the stack size.

    @msgpayload
    apm_cont_prop_id_stack_size_t
*/
#define APM_CONTAINER_PROP_ID_STACK_SIZE 0x08001013

/*# @h2xmlp_property    {"Stack Size", APM_CONTAINER_PROP_ID_STACK_SIZE}
    @h2xmlp_description {Container property ID for the stack size.} */

/** @ingroup spf_apm_container_props
    Payload for #APM_CONTAINER_PROP_ID_STACK_SIZE.
 */
#include "spf_begin_pack.h"
struct apm_cont_prop_id_stack_size_t
{
   uint32_t stack_size;
   /**< Stack size of this container. */

   /*#< @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_default     {0xFFFFFFFF}
        @h2xmle_description {Stack size of this container.} */
}
#include "spf_end_pack.h"
;
typedef struct apm_cont_prop_id_stack_size_t apm_cont_prop_id_stack_size_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/** @ingroup spf_apm_container_props
    Container property identifier for the processor domain.

    @msgpayload
    apm_cont_prop_id_proc_domain_t
*/
#define APM_CONTAINER_PROP_ID_PROC_DOMAIN 0x08001014

/*# @h2xmlp_property    {"Proc Domain", APM_CONTAINER_PROP_ID_PROC_DOMAIN}
    @h2xmlp_description {Container property ID for the processor domain.} */

/** @ingroup spf_apm_container_props
    Payload for #APM_CONTAINER_PROP_ID_PROC_DOMAIN.
 */
#include "spf_begin_pack.h"
struct apm_cont_prop_id_proc_domain_t
{
   uint32_t proc_domain;
   /**< Processor domain of the container.

        @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_ADSP
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2
        - #APM_PROC_DOMAIN_ID_APPS
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROC_DOMAIN_ID_GDSP_0
        - #APM_PROC_DOMAIN_ID_GDSP_1
        - #APM_PROC_DOMAIN_ID_APPS_2
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend */

   /*#< @h2xmle_rangeList   {"DONT_CARE"=0xFFFFFFFF,
                             "mDSP"=APM_PROC_DOMAIN_ID_MDSP,
                             "aDSP"=APM_PROC_DOMAIN_ID_ADSP,
                             "aDSP_1"=APM_PROC_DOMAIN_ID_ADSP_1,
                             "aDSP_2"=APM_PROC_DOMAIN_ID_ADSP_2,
                             "APPS"=APM_PROC_DOMAIN_ID_APPS,
                             "sDSP"=APM_PROC_DOMAIN_ID_SDSP,
                             "cDSP"=APM_PROC_DOMAIN_ID_CDSP,
                             "gDSP0"=APM_PROC_DOMAIN_ID_GDSP_0,
                             "gDSP1"=APM_PROC_DOMAIN_ID_GDSP_1,
                             "APPS2"=APM_PROC_DOMAIN_ID_APPS_2}
        @h2xmle_default     {0xFFFFFFFF}
        @h2xmle_description {Processor domain of the container.} */
}
#include "spf_end_pack.h"
;
typedef struct apm_cont_prop_id_proc_domain_t apm_cont_prop_id_proc_domain_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/** @ingroup spf_apm_container_props
    Container property identifier for the parent container ID.

    @msgpayload
    apm_cont_prop_id_parent_container_t
*/
#define APM_CONTAINER_PROP_ID_PARENT_CONTAINER_ID 0x080010CB

/*# @h2xmlp_property    {"Parent Container ID",
                          APM_CONTAINER_PROP_ID_PARENT_CONTAINER_ID}
    @h2xmlp_description {Container property ID for the parent container ID.} */

/** @ingroup spf_apm_container_props
    Payload for #APM_CONTAINER_PROP_ID_PARENT_CONTAINER_ID.
 */
#include "spf_begin_pack.h"
struct apm_cont_prop_id_parent_container_t
{
   uint32_t parent_container_id;
   /**< Unique identifier for the offload container in the master DSP with which
        the satellite DSP containers are associated. @newpagetable */

   /*#< @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_default     {0xFFFFFFFF }
        @h2xmle_description {Unique ID for the offload container in the master
                             DSP with which the satellite DSP containers are
                             associated.} */
}
#include "spf_end_pack.h"
;
typedef struct apm_cont_prop_id_parent_container_t apm_cont_prop_id_parent_container_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/** @ingroup spf_apm_container_props
    Container property identifier for the heap ID.

    @msgpayload
    apm_cont_prop_id_heap_id_t
*/
#define APM_CONTAINER_PROP_ID_HEAP_ID 0x08001174

/** @ingroup spf_apm_container_props
    Default heap ID for the container. */
#define APM_CONT_HEAP_DEFAULT 0x1

/** @ingroup spf_apm_container_props
    Low Power Island (LPI) heap ID for the container. */
#define APM_CONT_HEAP_LOW_POWER 0x2

/*# @h2xmlp_property    {"Container Heap", APM_CONTAINER_PROP_ID_HEAP_ID}
    @h2xmlp_description {Container property ID for the heap ID.} */

/** @ingroup spf_apm_container_props
    Payload for #APM_CONTAINER_PROP_ID_HEAP_ID.
 */
#include "spf_begin_pack.h"
struct apm_cont_prop_id_heap_id_t
{
   uint32_t heap_id;
   /**< Notifies the container about the heap it is use. The container remaps
        this ID to the actual heap ID.

        @valuesbul
        - #APM_CONT_HEAP_DEFAULT
        - #APM_CONT_HEAP_LOW_POWER @tablebulletend */

   /*#< @h2xmle_rangeList   {"Default"=APM_CONT_HEAP_DEFAULT,
                             "Low Power"=APM_CONT_HEAP_LOW_POWER}
        @h2xmle_default     {APM_CONT_HEAP_DEFAULT}
        @h2xmle_description {Notifies the container about the heap it is to
                             use. The container remaps this ID to the actual
                             heap ID.} */
}
#include "spf_end_pack.h"
;
typedef struct apm_cont_prop_id_heap_id_t apm_cont_prop_id_heap_id_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/** @ingroup spf_apm_container_props
    Container property identifier for the thread priority

    @msgpayload
    apm_cont_prop_id_heap_id_t
*/
#define APM_CONTAINER_PROP_ID_THREAD_PRIORITY                  0x08001A9E

/*# @h2xmlp_property    {"Thread Priority", APM_CONTAINER_PROP_ID_THREAD_PRIORITY}
    @h2xmlp_description {Container property ID for the thread priority.} */

/** Value of container thread priority that is ignored.*/
#define APM_CONT_PRIO_IGNORE    AR_NON_GUID(0x80000000)

/** @ingroup spf_apm_container_props
    Payload for #APM_CONTAINER_PROP_ID_THREAD_PRIORITY.
 */
#include "spf_begin_pack.h"
struct apm_cont_prop_id_thread_priority_t
{
   int32_t priority;
   /**< Sets the priority of the thread(s) that container uses. This is a platform dependent value.

    */

   /*#< @h2xmle_range       {0x80000000..0x7FFFFFFF}
        @h2xmle_default     {0x80000000}
        @h2xmle_description {Sets the priority of the thread(s) that container uses for data processing.
              This is a platform dependent value. } */
}
#include "spf_end_pack.h"
;
typedef struct apm_cont_prop_id_thread_priority_t apm_cont_prop_id_thread_priority_t;


/*--------------------------------------------------------------------------------------------------------------------*/
/** @ingroup spf_apm_container_props
    Container property identifier for the thread scheduling policy

    @msgpayload
    apm_cont_prop_id_heap_id_t
*/
#define APM_CONTAINER_PROP_ID_THREAD_SCHED_POLICY                  0x08001A9F

/*# @h2xmlp_property    {"Thread Scheduling Policy", APM_CONTAINER_PROP_ID_THREAD_SCHED_POLICY}
    @h2xmlp_description {Container property ID for the thread scheduling policy.} */

/** Value of container thread scheduling policy that is ignored.*/
#define APM_CONT_SCHED_POLICY_IGNORE    AR_NON_GUID(0xFFFFFFFF)

/** @ingroup spf_apm_container_props
    Payload for #APM_CONTAINER_PROP_ID_THREAD_SCHED_POLICY.
 */
#include "spf_begin_pack.h"
struct apm_cont_prop_id_thread_sched_policy_t
{
   uint32_t sched_policy;
   /**< Sets the scheduling policy of the thread(s) that container uses. This is a platform dependent value.
       Support available only in certain platform (E.g. Linux)
    */

   /*#< @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_default     {0xFFFFFFFF}
        @h2xmle_description {Sets the scheduling policy of the thread(s) that container uses for data processing.
        This is a platform dependent value. Support available only in certain platform (E.g. Linux).
        In Linux Red hat, the values are SCHED_OTHER 0, SCHED_FIFO 1, SCHED_RR 2.} */
}
#include "spf_end_pack.h"
;
typedef struct apm_cont_prop_id_thread_sched_policy_t apm_cont_prop_id_thread_sched_policy_t;


/*--------------------------------------------------------------------------------------------------------------------*/
/** @ingroup spf_apm_container_props
    Container property identifier for the thread scheduling policy

    @msgpayload
    apm_cont_prop_id_heap_id_t
*/
#define APM_CONTAINER_PROP_ID_THREAD_CORE_AFFINITY                  0x08001AA0

/*# @h2xmlp_property    {"Thread Core Affinity Mask", APM_CONTAINER_PROP_ID_THREAD_CORE_AFFINITY}
    @h2xmlp_description {Container property ID for the thread core affinity mask} */

/** Value of container core scheduling policy that is ignored.*/
#define APM_CONT_CORE_AFFINITY_IGNORE    0

/** @ingroup spf_apm_container_props
    Payload for #APM_CONTAINER_PROP_ID_THREAD_CORE_AFFINITY.
 */
#include "spf_begin_pack.h"
struct apm_cont_prop_id_thread_core_affinity_t
{
   uint32_t core_affinity;
   /**< Sets the core affinity mask of the thread(s) that container uses. This is a platform dependent value.
    * Support available only in certain platform (E.g. Linux)
    */

   /*#< @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_default     {APM_CONT_CORE_AFFINITY_IGNORE}
        @h2xmle_description {Sets the core affinity mask of the thread(s) that container uses for data processing.
           This is a platform dependent value. Support available only on certain platform (E.g. Linux).
           Value of zero is ignored and platform default is used.
         } */
}
#include "spf_end_pack.h"
;
typedef struct apm_cont_prop_id_thread_core_affinity_t apm_cont_prop_id_thread_core_affinity_t;

/** @ingroup spf_apm_container_props
    Container property identifier for the peer heap ID.

    @msgpayload
    apm_cont_prop_id_peer_heap_id_t
*/
#define APM_CONTAINER_PROP_ID_PEER_HEAP_ID 0x0800124D

/*# @h2xmlp_property    {"Peer Container Heap",
                          APM_CONTAINER_PROP_ID_PEER_HEAP_ID}
    @h2xmlp_description {Container property ID for the peer heap ID.} */

/** @ingroup spf_apm_container_props
    Payload for #APM_CONTAINER_PROP_ID_PEER_HEAP_ID.
 */
#include "spf_begin_pack.h"
struct apm_cont_prop_id_peer_heap_id_t
{
   uint32_t heap_id;
   /**< Notifies the container about its peer container's heap ID.

        @valuesbul
        - #APM_CONT_HEAP_DEFAULT
        - #APM_CONT_HEAP_LOW_POWER @tablebulletend */

   /*#< @h2xmle_rangeList   {"Default"=APM_CONT_HEAP_DEFAULT,
                             "Low Power"=APM_CONT_HEAP_LOW_POWER}
        @h2xmle_default     {APM_CONT_HEAP_DEFAULT}
        @h2xmle_description {Notifies the container about its peer container's
                             heap ID.} */
}
#include "spf_end_pack.h"
;
typedef struct apm_cont_prop_id_peer_heap_id_t apm_cont_prop_id_peer_heap_id_t;

/*--------------------------------------------------------------------------------------------------------------------*/
/** @ingroup spf_apm_container_props
    Container property identifier for the processing frame size.

    @msgpayload
    apm_cont_prop_id_frame_size_t
*/
#define APM_CONTAINER_PROP_ID_FRAME_SIZE 0x08001A9B

/** @ingroup spf_apm_container_props
    Default mode for the container frame size property. */
#define APM_CONTAINER_PROP_FRAME_SIZE_DEFAULT 0

/** @ingroup spf_apm_container_props
    In-Time mode for the container frame size property. */
#define APM_CONTAINER_PROP_FRAME_SIZE_TIME 1

/** @ingroup spf_apm_container_props
    Payload for #APM_CONTAINER_PROP_ID_FRAME_SIZE.
 */

/** @h2xmlp_subStruct */
#include "spf_begin_pack.h"
struct apm_cont_prop_id_frame_size_time_t
{
    uint32_t frame_size_us;
    /**< Processing frame size of the container in time (microseconds).*/

    /*#< @h2xmle_range   {1...100000}
     @h2xmle_default     {5000}
     @h2xmle_description {Processing frame size of the container in time (microseconds).} */
}
#include "spf_end_pack.h"
;
typedef struct apm_cont_prop_id_frame_size_time_t apm_cont_prop_id_frame_size_time_t;

/*# @h2xmlp_property    {"Processing frame size of the container",
 APM_CONTAINER_PROP_ID_FRAME_SIZE}
 @h2xmlp_description {Container property to set the processing frame size for PCM or PCM Packetized media format.
 If container has a module with threshold then that will be used to determine the container processing frame size
 instead. OLC (offload container) will not use this property.} */
#include "spf_begin_pack.h"
struct apm_cont_prop_id_frame_size_t
{
   uint32_t mode;
   /**< Specifies whether the container frame size configuration is default or in-time.*/

   /*#< @h2xmle_rangeList   {"DEFAULT" = 0; "IN_TIME" = 1}
        @h2xmle_default     {APM_CONTAINER_PROP_FRAME_SIZE_DEFAULT}
        @h2xmle_description {Container frame size configuration mode.} */
#ifdef __H2XML__
    uint8_t bytes[0];
    /*#< @h2xmle_elementType {rawData} */
#endif
}
#include "spf_end_pack.h"
;

typedef struct apm_cont_prop_id_frame_size_t apm_cont_prop_id_frame_size_t;


/**
 * case1:
 *  mode: #APM_CONTAINER_PROP_FRAME_SIZE_TIME
 *  frame_size_us: 5000
 *  usecase_sampling_rate: 48000
 *     Container frame size in samples = floor((usecase_sampling_rate * frame_size_us) / 10^6)
 *                                     = 240 samples
 *     Container frame size in time    = floor((10^6 * frame_size_samples) / usecase_sampling_rate)
 *                                     = 5000 us
 *  usecase_sampling_rate: 44100
 *     Container frame size in samples = floor((usecase_sampling_rate * frame_size_us) / 10^6)
 *                                     = 220 samples
 *     Container frame size in time    = floor((10^6 * frame_size_samples) / usecase_sampling_rate)
 *                                     = 4988 us
 *
 * case2:
 *  mode: #APM_CONTAINER_PROP_FRAME_SIZE_TIME
 *  frame_size_us: 5334
 *  usecase_sampling_rate: 48000
 *     Container frame size in samples = floor((usecase_sampling_rate * frame_size_us) / 10^6)
 *                                     = 256 samples
 *     Container frame size in time    = floor((10^6 * frame_size_samples) / usecase_sampling_rate)
 *                                     = 5333 us
 *
 * case3:
 *  mode: #APM_CONTAINER_PROP_FRAME_SIZE_TIME
 *  frame_size_us: 10000
 *  usecase_sampling_rate: 44100
 *     Container frame size in samples = floor((usecase_sampling_rate * frame_size_us) / 10^6)
 *                                     = 441 samples
 *     Container frame size in time    = floor((10^6 * frame_size_samples) / usecase_sampling_rate)
 *                                     = 10000 us
 *
 * case4:
 *  mode: APM_CONTAINER_PROP_FRAME_SIZE_DEFAULT
 *  Container frame size is decided based on the subgraph performance mode.
 */

/*--------------------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------------------*/
/*# @}                   <-- End of container_cfg -->*/

/*====================================================================================================================*/
/*====================================================================================================================*/

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* _APM_CONTAINER_API_H_ */
