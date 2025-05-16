/**
 * \file amdb_api.h
 * \brief
 *    API header for AMDB.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _AMDB_API_H_
#define _AMDB_API_H_

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#include "ar_defs.h"

/*# @h2xml_title1           {Audio Module Data Base (AMDB) APIs}
    @h2xml_title_agile_rev  {Audio Module Data Base (AMDB) APIs}
    @h2xml_title_date       {October 31, 2018} */

/*# @h2xmlx_xmlNumberFormat {int} */


#define AMDB_MODULE_REG_STRUCT_V1 1

/** @ingroup spf_mods_amdb
    Interface type is the Common Audio Processor Interface. */
#define AMDB_INTERFACE_TYPE_CAPI 2

/** @ingroup spf_mods_amdb
    Interface version is the Common Audio Processor Interface version 3. */
#define AMDB_INTERFACE_VERSION_CAPI_V3 3

/** @ingroup spf_mods_amdb
    Module is generic, such as a preprocessor, postprocessor, and so on. */
#define AMDB_MODULE_TYPE_GENERIC 2

/** @ingroup spf_mods_amdb
    Module is a decoder. */
#define AMDB_MODULE_TYPE_DECODER 3

/** @ingroup spf_mods_amdb
    Module is an encoder. */
#define AMDB_MODULE_TYPE_ENCODER 4

/** @ingroup spf_mods_amdb
    Module is a converter. */
#define AMDB_MODULE_TYPE_CONVERTER 5

/** @ingroup spf_mods_amdb
    Module is a packetizer. */
#define AMDB_MODULE_TYPE_PACKETIZER 6

/** @ingroup spf_mods_amdb
    Module is a de-packetizer. @newpage */
#define AMDB_MODULE_TYPE_DEPACKETIZER 7

/** @ingroup spf_mods_amdb
    Module is a detector (such as DTMF, VoiceUI). @newpage */
#define AMDB_MODULE_TYPE_DETECTOR 8

/** @ingroup spf_mods_amdb
    Module is a generator (such as DTMF, Ultrasound). @newpage */
#define AMDB_MODULE_TYPE_GENERATOR 9

/** @ingroup spf_mods_amdb
    Module is a pre or post-processor (such as filters). @newpage */
#define AMDB_MODULE_TYPE_PP 10

/** @ingroup spf_mods_amdb
    Module is a an end point (software or hardware end point, such as I2S source or sink). @newpage */
#define AMDB_MODULE_TYPE_END_POINT 11


/** @ingroup spf_mods_amdb
    Payload for #AMDB_CMD_REGISTER_MODULES.
    This structure contains module-specific registration.

    When interface_type = CAPI and interface_version = 3, immediately
    following this structure is %amdb_capi_module_reg_info_t.
 */
#include "spf_begin_pack.h"
struct amdb_module_reg_info_t
{
   uint16_t interface_type;
   /**< Type of the module interface.

        @values #AMDB_INTERFACE_TYPE_CAPI */

   uint16_t interface_version;
   /**< Version of the module interface.

        @values #AMDB_INTERFACE_VERSION_CAPI_V3 */
}
#include "spf_end_pack.h"
;
typedef struct amdb_module_reg_info_t amdb_module_reg_info_t;


/** @ingroup spf_mods_amdb
    Payload for #AMDB_CMD_REGISTER_MODULES.
    This structure contains CAPI-specific registration data.
*/
#include "spf_begin_pack.h"
struct amdb_capi_module_reg_info_t
{
   uint32_t module_type;
   /**< Type of module.

        @valuesbul
        - #AMDB_MODULE_TYPE_GENERIC
        - #AMDB_MODULE_TYPE_DECODER
        - #AMDB_MODULE_TYPE_ENCODER
        - #AMDB_MODULE_TYPE_CONVERTER
        - #AMDB_MODULE_TYPE_PACKETIZER
        - #AMDB_MODULE_TYPE_DEPACKETIZER
        - #AMDB_MODULE_TYPE_DETECTOR
        - #AMDB_MODULE_TYPE_GENERATOR
        - #AMDB_MODULE_TYPE_PP
        - #AMDB_MODULE_TYPE_END_POINT
        @tablebulletend */

   uint32_t module_id;
   /**< ID for the module. */

   uint16_t file_name_len;
   /**< Length of the SO filename (in bytes).

        The file_name element in the structure is of length file_name_len. */

   uint16_t tag_len;
   /**< Length of tag in bytes. This tag identifies the entry point function.

        The tag element in the structure is of length tag_len bytes. */

   uint32_t error_code;
   /**< Result of the module registration.

        The SPF must be able to write to this memory. Memory is populated only
        for out-of-band data. @newpagetable */
}
#include "spf_end_pack.h"
;
typedef struct amdb_capi_module_reg_info_t amdb_capi_module_reg_info_t;


/** @ingroup spf_mods_amdb
    Instance ID for the AMDB module. This module supports the following command
    IDs:
    - #AMDB_CMD_REGISTER_MODULES
    - #AMDB_CMD_DEREGISTER_MODULES
    - #AMDB_CMD_LOAD_MODULES
    - #AMDB_CMD_UNLOAD_MODULES

    @par Calibration header field
    Opcode -- AMDB_MODULE_INSTANCE_ID
 */
#define AMDB_MODULE_INSTANCE_ID 0x00000003

/** @ingroup spf_mods_amdb
    Registers a module with the AMDB to enable the module for dynamic loading.

    @msgpayload
    amdb_module_registration_t \n
    @indent{12pt} amdb_module_reg_info_t \n
    @indent{12pt} amdb_capi_module_reg_info_t

    @detdesc
    When amdb_capi_module_reg_info_t follows amdb_module_reg_info_t, the
    following variable-size arrays are present. Their size depends upon the
    variables present in %amdb_capi_module_reg_info_t.
    - @codeinline{uint8_t file_name[0];} @vertspace{-6}
      - Name of the SO file of length file_name_len bytes @vertspace{-4}
      - Total size of this variable array is file_name_len * sizeof (uint8_t)
        @vertspace{-3}
    - @codeinline{uint8_t tag[0];} @vertspace{-6}
      - Tag name of length tag_len @vertspace{-4}
      - For CAPIv2, the tag is a prefix for the entry point functions:
        @vertspace{-6}
         - tag + _get_static_properties @vertspace{-4}
         - tag + _init @vertspace{-3}
      - Total size of this variable array is tag_len * sizeof (uint8_t)
        @vertspace{-3}
    - @codeinline{uint8_t alignment[0];} @vertspace{-6}
      - For 4-byte alignment @vertspace{-2}

    @returns
    GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

    @dependencies
    When registering offloaded modules on a satellite DSP using the out-of-band
    command, the client must have used
    #APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS to perform memory mapping for the
    satellite.
 */
#define AMDB_CMD_REGISTER_MODULES 0x0100101E

/** @ingroup spf_mods_amdb
    Payload for #AMDB_CMD_REGISTER_MODULES.
    The overall payload can be both in-band and out-of-band.
*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct amdb_module_registration_t
{
   uint32_t proc_domain;
   /**< ID for the processor domain in which the AMDB is running.

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

   uint32_t num_modules;
   /**< Number of modules being registered. */

   uint32_t struct_version;
   /**< Version of the %amdb_module_reg_info_t structure. */

   amdb_module_reg_info_t reg_info[0];
   /**< Array of registration information for num_modules (of type
        reg_info[num_modules]).

        Each element in the array (including filename and tag) must be 4-byte
        aligned. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct amdb_module_registration_t amdb_module_registration_t;


/** @ingroup spf_mods_amdb
    Deregisters one or more registered modules.

    @msgpayload
    amdb_module_deregistration_t

    @returns
    GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

    @dependencies
    Modules must have been previous registered with #AMDB_CMD_REGISTER_MODULES.
    @par
    When deregistering offloaded modules from a satellite DSP using the
    out-of-band command, the client must have used
    #APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS to perform memory mapping for the
    satellite.
 */
#define AMDB_CMD_DEREGISTER_MODULES 0x0100101F

/** @ingroup spf_mods_amdb
    Payload for #AMDB_CMD_DEREGISTER_MODULES.
    This payload can be both in-band and out-of-band.
*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct amdb_module_deregistration_t
{
   uint32_t proc_domain;
   /**< ID for the processor domain in which the AMDB is running.

        @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_ADSP
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROC_DOMAIN_ID_GDSP_0
        - #APM_PROC_DOMAIN_ID_GDSP_1
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend */

   uint32_t num_modules;
   /**< Number of modules being registered. */

   uint32_t module_id[0];
   /**< Array of module IDs for num_modules (module_id[num_modules]). */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct amdb_module_deregistration_t amdb_module_deregistration_t;

/** @ingroup spf_mods_amdb
    Payload for #AMDB_CMD_LOAD_MODULES, #AMDB_CMD_RSP_LOAD_MODULES, and
    #AMDB_CMD_UNLOAD_MODULES.
    This structure contains module-specific information for loading and
    unloading modules.
 */
#include "spf_begin_pack.h"
struct amdb_load_unload_info_t
{
   uint32_t module_id;
   /**< ID for the module. */

   uint32_t handle_lsw;
   /**< Lower 32 bits of the 64-bit handle to the module. */

   uint32_t handle_msw;
   /**< Upper 32 bits of the 64-bit handle to the module. */
}
#include "spf_end_pack.h"
;
typedef struct amdb_load_unload_info_t amdb_load_unload_info_t;


/** @ingroup spf_mods_amdb
    Loads one or more modules if they were successfully registered with the
    AMDB.

    @msgpayload
    amdb_module_load_unload_t \n
    @indent{12pt} amdb_load_unload_info_t

    @returns
    #AMDB_CMD_RSP_LOAD_MODULES

    @dependencies
    A module must have been registered before loading.
    @par
    For loading offloaded modules on a satellite DSP using an
    out-of-band command, the client must have used
    #APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS to perform memory mapping for the
    satellite. @newpage
 */
#define AMDB_CMD_LOAD_MODULES 0x01001020

/** @ingroup spf_mods_amdb
    Payload for #AMDB_CMD_LOAD_MODULES, #AMDB_CMD_RSP_LOAD_MODULES, and
    #AMDB_CMD_UNLOAD_MODULES.

    The overall payload can be for both in-band and out-of-band data.
    - For an in-band payload, the incoming payload is copied, modified (if
      necessary), and sent in the response.
    - For an out-of-band payload, the incoming payload is sent in the response.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct amdb_module_load_unload_t
{
   uint32_t proc_domain;
   /**< ID for the processor domain in which the AMDB is running.

        @valuesbul
        - #APM_PROC_DOMAIN_ID_MDSP
        - #APM_PROC_DOMAIN_ID_ADSP
        - #APM_PROC_DOMAIN_ID_ADSP_1
        - #APM_PROC_DOMAIN_ID_ADSP_2
        - #APM_PROC_DOMAIN_ID_SDSP
        - #APM_PROC_DOMAIN_ID_CDSP
        - #APM_PROC_DOMAIN_ID_GDSP_0
        - #APM_PROC_DOMAIN_ID_GDSP_1
        - #APM_PROP_ID_DONT_CARE (Default) @tablebulletend */

   uint32_t error_code;
   /**< Result of module loading. For an out-of-band payload, this block
        is updated directly. */

   uint32_t num_modules;
   /**< Number of modules being registered. */

   amdb_load_unload_info_t load_unload_info[0];
   /**< Array of load or unload information for num_modules (of type
        load_unload_info[num_modules]). */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct amdb_module_load_unload_t amdb_module_load_unload_t;


/** @ingroup spf_mods_amdb
    Sent in response to an #AMDB_CMD_LOAD_MODULES command.

    If a module was not registered with the AMDB, handle_lsw and handle_msw
    is populated with 0 in the response payload for that module.

    @msgpayload
    amdb_module_load_unload_t \n
    @indent{12pt} amdb_load_unload_info_t

    @returns
    None.
 */
#define AMDB_CMD_RSP_LOAD_MODULES 0x02001008

/** @ingroup spf_mods_amdb
    Unloads a module that was loaded via the #AMDB_CMD_LOAD_MODULES command.

    @msgpayload
    amdb_module_load_unload_t \n
    @indent{12pt} amdb_load_unload_info_t

    @returns
    GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

    @dependencies
    For unloading offloaded modules on a satellite DSP using an out-of-band
    command, the client must have used
    #APM_CMD_SHARED_SATELLITE_MEM_MAP_REGIONS to perform memory mapping for the
    satellite. @newpage
 */
#define AMDB_CMD_UNLOAD_MODULES 0x01001022



/** @ingroup spf_mods_amdb
    Identifier for the parameter that gets the module version info.

    @msgpayload
    amdb_param_id_amdb_module_version_info_t \n
    @indent{12pt} amdb_param_id_amdb_module_version_info_t

    @returns
    GPR_IBASIC_RSP_RESULT (see AudioReach SPF Generic Packet Router (GPR) API Reference).

    @newpage

*/
#define AMDB_PARAM_ID_MODULE_VERSION_INFO 0x080013D6

#define AMDB_MODULE_INFO_OK 0
#define AMDB_MODULE_INFO_ERR_FILE_NOT_FOUND 1
#define AMDB_MODULE_INFO_ERR_DYNAMIC_LINK 2
#define AMDB_MODULE_INFO_ERR_GENERAL AR_NON_GUID(0xFFFFFFFF)

/** @ingroup spf_mods_amdb
Payload for #AMDB_PARAM_ID_MODULE_VERSION_INFO

The client is reponsible for filling out the proc_domain, num_modules, and module_id fields. Please note that
since only one proc_domain is given it is the responsibility of the CLIENT to iterate through all of the PDs to
look for the module of intrest.
*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct amdb_param_id_module_version_info_t
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
	uint32_t num_modules;
	/**< Number of modules in the array. */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct amdb_param_id_module_version_info_t amdb_param_id_module_version_info_t;



/** @ingroup spf_mods_amdb
*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct amdb_module_build_ts_info_t
{
	uint16_t year;
    /**< Year that the build was produced */
    uint16_t month;
    /**< month the build was produced
         @h2xmle_range       {0..12} */
    uint16_t day;
    /**< day the build was produced
         @h2xmle_range       {0..31} */
    uint16_t hour;
    /**< hour the build was produced
         @h2xmle_range       {0..24} */
    uint16_t minute;
    /**< minute the build was produced
         @h2xmle_range       {0..60} */
    uint16_t second;
    /**< second the build was produced
         @h2xmle_range       {0..60} */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct amdb_module_build_ts_info_t amdb_module_build_ts_info_t;

/** @ingroup spf_mods_amdb
*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct amdb_module_version_info_payload_t
{
	uint32_t module_id;
	/**< Requested module id, to be filled in by the client. */
	uint32_t error_code;
	/**< Indicates whether an error has occurred loading the module

    @valuesbul
    - #AMDB_MODULE_INFO_OK
    - #AMDB_MODULE_INFO_ERR_FILE_NOT_FOUND
    - #AMDB_MODULE_INFO_ERR_DYNAMIC_LINK
    - #AMDB_MODULE_INFO_ERR_GENERAL @tablebulletend  */
	uint32_t is_present;
	/**< 1 if and only if the module is present and not stubbed in the AMDB. */
	uint32_t module_version_major;
	/**< Major version of the requested module. 0 indicates unknown version */
	uint32_t module_version_minor;
	/**< Minor version of the requested module. 0 indicates unknown version */

    amdb_module_build_ts_info_t build_ts;
    /**< Indicates the time at which the build for the given module was created. */

}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct amdb_module_version_info_payload_t amdb_module_version_info_payload_t;


#endif //_AMDB_API_H_
