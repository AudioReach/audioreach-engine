#ifndef _VCPM_CONFIG_API_H_
#define _VCPM_CONFIG_API_H_

/**
* \file vcpm_config_api.h
* \brief
*     This file contains declarations of the VCPM config API.
*
*
* \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
*/
#include "ar_defs.h"
#include "apm_graph_properties.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus */


/** Instance identifier for the VCPM static module. */
#define VCPM_MODULE_INSTANCE_ID 0x00000004

/**
    @h2xmlm_module       {"VCPM_MODULE_INSTANCE_ID",VCPM_MODULE_INSTANCE_ID}
    @h2xmlm_displayName  {"VCPM"}

    @h2xmlm_description  {Voice Call proxy Manager Module \n
  -  This module supports the following parameter IDs: \n
  - #VCPM_PARAM_ID_SCALE_FACTOR \n
  - #VCPM_PARAM_ID_SAFETY_MARGIN \n }
    @{                   <-- Start of the Module -->
   */
   

/*==============================================================================
   Type definitions
==============================================================================*/

/** @ingroup spf_vcpm_param_id_scale_factor_t
    Identifier for the parameter that sets the scale factor parameters.
    Note that this parameter can be utilized by master and satellite domains as well.
    Offloaded containers would be applying the scale factor before voting for the respective resource.

    @msgpayload
    spf_vcpm_param_id_scale_factor_t
*/
#define VCPM_PARAM_ID_SCALE_FACTOR 0x08001B33

#include "spf_begin_pack.h"
/** @h2xmlp_subStruct */
struct vcpm_param_id_scale_factor_payload_t
{
   uint32_t   proc_id;
   /**< @h2xmle_description {Proc ID for which this param is applicable}
           @h2xmle_default     {APM_PROC_DOMAIN_ID_INVALID}
           @h2xmle_rangeList   {"INVALID_VALUE"=APM_PROC_DOMAIN_ID_INVALID,
                                 "ADSP"=APM_PROC_DOMAIN_ID_ADSP,
                                 "MDSP"=APM_PROC_DOMAIN_ID_MDSP,
                                 "CDSP"=APM_PROC_DOMAIN_ID_CDSP}
           @h2xmle_policy      {Basic} 
           */

   uint32_t kpps_sf;

   /**< @h2xmle_description { Scale factor to apply to the aggregated kpps of the offload path. KPPS SF is defined as follows:
                               10-100: final kpps =  ( kpps_scale_factor / 10 ) * initial kpps
                               So, a scale factor of 10 translates to unity kpps scaling. A scale factor of 50 translates to
                               kpps scaling of 5x.}
        @h2xmle_default     {10}
        @h2xmle_range   {10...100}
        @h2xmle_copySrc      {kpps_scale_factor}
        @h2xmle_policy      {Basic} 
        */

   uint32_t bus_bw_sf;
     /**< @h2xmle_description { Scale factor to apply to the aggregated BW. BW SF is defined as follows:
                               10-100: final BW = ( BW_scale_factor / 10 )* initial BW
                               So, a scale factor of 10 translates to unity BW scaling. A scale factor of 50 translates to
                               BW scaling of 5x.}
        @h2xmle_default     {10}
        @h2xmle_range   {10...100}
        @h2xmle_copySrc      {bw_scale_factor}
        @h2xmle_policy      {Basic} 
        */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_id_scale_factor_payload_t vcpm_param_id_scale_factor_payload_t;

typedef struct vcpm_param_id_scale_factor_t vcpm_param_id_scale_factor_t;
/** @h2xmlp_parameter   {"VCPM_PARAM_ID_SCALE_FACTOR",
                          VCPM_PARAM_ID_SCALE_FACTOR}
    @h2xmlp_description { Identifier for the parameter that sets the scale factor info.
                          Note that this parameter can be utilized by master and satellite domains as well.}
    @h2xmlp_copySrc     {0x080011AE}
    @h2xmlp_toolPolicy  { Calibration}*/

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct vcpm_param_id_scale_factor_t
{
   uint32_t num_configs;
   /**< @h2xmle_description {number of  scale factor configs for this SubGraph. 
                             Each proc domain should not have more than one config.}
         @h2xmle_default     {0}
         @h2xmle_range   {0...32}
         @h2xmle_policy      {Basic} 
         */
   vcpm_param_id_scale_factor_payload_t scale_factor[0];
   /**< @h2xmle_description {Specifies the scale factor configuration per domain. }
          @h2xmle_variableArraySize {num_configs}
          @h2xmle_default      {0} */

}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

/** @ingroup spf_vcpm_param_id_safety_margin
    Identifier for the parameter that sets the safety margin.
    This shall be considered as an aggregated margin for  the containers in the SubGraph.
    It will be absorbed by the VCPM sitting in the master domain to calculate the packet delivery timelines.

    @msgpayload
    vcpm_param_id_safety_margin_t
*/
#define VCPM_PARAM_ID_SAFETY_MARGIN 0x08001B34

/** @ingroup vcpm_param_safety_margin_t
    Payload for #VCPM_PARAM_ID_SAFETY_MARGIN.
 */
 
typedef struct vcpm_param_id_safety_margin_t vcpm_param_id_safety_margin_t;
 /** @h2xmlp_parameter   {"VCPM_PARAM_ID_SAFETY_MARGIN",
                          VCPM_PARAM_ID_SAFETY_MARGIN}
    @h2xmlp_description { Identifier for the parameter that sets the Safety margin info.
                         }
    @h2xmlp_toolPolicy  { Calibration} */
    
#include "spf_begin_pack.h"
struct vcpm_param_id_safety_margin_t
{
   uint32_t safety_margin_us;
     /*< @h2xmle_description { Aggregated Safety margin for the containers part of this SG.
                               Please note that margin needs be tuned very carefully and should mainly account for corner/concurrency scenarios.}
        @h2xmle_default     {0}
        @h2xmle_range   {0...20000}
        @h2xmle_policy      {Basic} 
        */
}
#include "spf_end_pack.h"
;


#ifdef __cplusplus
}
#endif /*__cplusplus*/
/** @}                   <-- End of the Module -->*/

#endif /* #ifdef _VCPM_CONFIG_API_H_ */
