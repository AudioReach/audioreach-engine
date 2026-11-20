#ifndef MFC_OSRC_API_H
#define MFC_OSRC_API_H

/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/*------------------------------------------------------------------------------
   Includes
------------------------------------------------------------------------------*/
#include "mfc_cmn_api.h"

/*# @h2xml_title1          {OSRC Media Format Converter (MFC_OSRC) Module API}
    @h2xml_title_agile_rev {OSRC Media Format Converter (MFC_OSRC) Module API}
    @h2xml_title_date      {2026} */

/*---------------------------------------------------------------------------
 * PARAM_ID_MFC_OSRC_RESAMPLER_CFG
 *
 * OSResampler-specific quality configuration for MODULE_ID_MFC_OSRC.
 *
 * Use this parameter to tune the windowed-sinc resampler quality (filter
 * length, interpolation, windowing function).  It has no equivalent in
 * MODULE_ID_MFC, which uses the Qualcomm FIR/IIR resampler.
 *
 * The common resampler type/HW/dynamic fields are still accepted via
 * PARAM_ID_MFC_RESAMPLER_CFG (inherited from mfc_cmn_api.h) for backward
 * compatibility, but those fields are ignored by MODULE_ID_MFC_OSRC.
 *--------------------------------------------------------------------------*/

/** @ingroup ar_spf_mod_mfc_osrc_mod
    Identifier for the OSResampler quality configuration parameter.

    @msgpayload
    param_id_mfc_osrc_resampler_cfg_t
*/

#define PARAM_ID_MFC_OSRC_RESAMPLER_CFG   0x08001C1D
#define CAPI_MFC_OSRC_STACK_SIZE          4096
/*# @h2xmlp_parameter   {"PARAM_ID_MFC_OSRC_RESAMPLER_CFG",
                          PARAM_ID_MFC_OSRC_RESAMPLER_CFG}
    @h2xmlp_description {Configures the OSResampler quality settings for
                         MODULE_ID_MFC_OSRC. The OSResampler uses windowed sinc
                         interpolation filters. Quality is controlled by the
                         number of filter taps and the number of filters, 
                         selected via a quality preset. \n
                         Higher quality presets provide better audio fidelity
                         at the cost of increased CPU load and memory usage. \n
                         The resampler is always a software FIR resampler;
                         IIR and hardware resampler options are not available
                         in this module — use PARAM_ID_MFC_RESAMPLER_CFG for
                         those settings (accepted for backward compatibility
                         but IIR/HW fields are ignored). \n
                         This parameter has no equivalent in MODULE_ID_MFC.}
    @h2xmlp_toolPolicy  {Calibration; RTC} */ //TODO: keep RTC_get and check tool policy; ask aditya/patrick

#include "spf_begin_pack.h"
struct param_id_mfc_osrc_resampler_cfg_t
{
   uint32_t quality_preset;
   /**< Selects the OSResampler quality preset.

        @valuesbul
        - 0 -- Preset 0: 48 taps, 48 filters, ~16 KB RAM (stereo). (Default)
               Recommended for embedded DSP with tight memory constraints.
        - 1 -- Preset 1: 156 taps, 320 filters, ~219 KB RAM (stereo).
               Good quality/performance balance for embedded systems.
        - 2 -- Preset 2: 380 taps, 380 filters, ~620 KB RAM (stereo).
               High quality, suitable for high-performance DSPs.
        - 3 -- Preset 3: 988 taps, 988 filters, ~3956 KB RAM (stereo).
               Maximum quality.

        Note: RAM usage scales linearly with the number of channels. */

   /*#< @h2xmle_description {Selects the OSResampler quality preset. \n
                             Preset 0 (48 taps, 48 filters, ~16 KB stereo RAM)
                             is recommended for embedded DSP with tight memory
                             constraints. \n
                             Preset 1 (156 taps, 320 filters, ~219 KB stereo RAM)
                             provides a good quality/performance balance. \n
                             Preset 2 (380 taps, 380 filters, ~620 KB stereo RAM)
                             is a high-quality setting. \n
                             Preset 3 (988 taps, 988 filters, ~3956 KB stereo RAM)
                             provides maximum quality. \n
                             Note: RAM usage scales linearly with the number of channels.}
        @h2xmle_rangeList   {"Preset 0: 48 taps, 48 filters (16 KB stereo)"=0;
                             "Preset 1: 156 taps, 320 filters (219 KB stereo)"=1;
                             "Preset 2: 380 taps, 380 filters (620 KB stereo)"=2;
                             "Preset 3: 988 taps, 988 filters (3956 KB stereo)"=3}
        @h2xmle_default     {0} */

   uint32_t use_interpolation;
   /**< Enables sub-sample interpolation between adjacent sinc filters.

        @valuesbul
        - 0 -- Disabled: use nearest filter only (faster, less accurate).
        - 1 -- Enabled (Default): interpolate between adjacent filters.
               Recommended for best quality.

        Note: For fixed-ratio conversions where the filter count reduces to an
        exact integer (e.g., 44.1 kHz to 48 kHz), interpolation is
        automatically disabled regardless of this setting. */

   /*#< @h2xmle_description {Enables sub-sample interpolation between adjacent
                             sinc filters. \n
                             When enabled, the resampler interpolates between
                             the two nearest sinc filters to compute each output
                             sample, improving quality at approximately doubled
                             CPU load. \n
                             Recommended to keep enabled for best quality. \n
                             Note: For fixed-ratio conversions where the number
                             of filters reduces to an exact integer (e.g.,
                             44.1 kHz to 48 kHz), interpolation is automatically
                             disabled regardless of this setting.}
        @h2xmle_rangeList   {"Disabled"=0;
                             "Enabled"=1}
        @h2xmle_default     {1} */

   uint32_t use_blackman_harris;
   /**< Selects the windowing function for the sinc filters.

        @valuesbul
        - 0 -- Hann window: steeper transition band, poorer stopband rejection.
        - 1 -- Blackman-Harris window (Default): better stopband rejection,
               recommended for most applications. */

   /*#< @h2xmle_description {Selects the windowing function used to generate
                             the sinc interpolation filters. \n
                             Blackman-Harris (4-term) provides better stopband
                             rejection (lower aliasing) and is recommended for
                             most use cases. \n
                             Hann (raised cosine) has a steeper transition band
                             which may be preferable for very short filters
                             (low quality presets) where transition sharpness
                             is more important than stopband rejection.}
        @h2xmle_rangeList   {"Hann window (steeper transition)"=0;
                             "Blackman-Harris window (better stopband)"=1}
        @h2xmle_default     {1} */
}
#include "spf_end_pack.h"
;
typedef struct param_id_mfc_osrc_resampler_cfg_t param_id_mfc_osrc_resampler_cfg_t;

/*---------------------------------------------------------------------------
 * MODULE_ID_MFC_OSRC
 *--------------------------------------------------------------------------*/

/** @ingroup ar_spf_mod_mfc_osrc_mod
    Identifier for the OSRC Media Format Converter module.

    This module provides the same functionality as MODULE_ID_MFC but uses
    the open-source windowed sinc resampler (OSResampler) as the resampling
    engine instead of the Qualcomm FIR/IIR resampler.

    @subhead4{Supported parameter IDs}
    - #PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT   (from mfc_cmn_api.h) @lstsp1
    - #PARAM_ID_MFC_RESAMPLER_CFG         (from mfc_cmn_api.h; IIR/HW fields ignored) @lstsp1
    - #PARAM_ID_MFC_OSRC_RESAMPLER_CFG   (OSResampler quality controls) @lstsp1
    - #PARAM_ID_CHMIXER_COEFF

    @subhead4{Supported input media format}
    - Data Format          : #DATA_FORMAT_FIXED_POINT, #DATA_FORMAT_FLOATING_POINT @lstsp1
    - fmt_id               : Don't care @lstsp1
    - Sample Rates         : >0 to 384 kHz @lstsp1
    - Number of channels   : 1 to 128 @lstsp1
    - Bits per sample      : 16, 32 (24-bit stored in 32-bit word) @lstsp1
    - Q format             : 15, 27, 31 @lstsp1
    - Interleaving         : De-interleaved unpacked @lstsp1
    - Signed/unsigned      : Signed

    @subhead4{Resampler characteristics}
    - Algorithm            : Windowed sinc interpolation (OSResampler) @lstsp1
    - Type                 : Software FIR only (no IIR, no hardware resampler) @lstsp1
    - Algorithmic delay    : numTaps/2 input samples (depends on quality preset) @lstsp1
    - Fractional SRC       : Supported via DM extension (FWK_EXTN_DM) @lstsp1
    - Fixed-ratio opt.     : Automatic filter count reduction for integer ratios
*/

#define MODULE_ID_MFC_OSRC 0x07001192

/*# @h2xmlm_module             {"MODULE_ID_MFC_OSRC", MODULE_ID_MFC_OSRC}
    @h2xmlm_displayName        {"MFC_OSRC"}
    @h2xmlm_modSearchKeys      {resampler, channel mixer, byte converter, osrc, Audio, Voice}
    @h2xmlm_description        {ID for the OSRC Media Format Converter module.
                                This module provides the same functionality as
                                MODULE_ID_MFC (sample rate conversion, channel
                                mixing, bit width conversion, endianness
                                conversion, float data format support) but uses
                                the open-source windowed sinc resampler
                                (OSResampler) as the resampling engine instead
                                of the Qualcomm FIR/IIR resampler. \n
                                The OSResampler is a pure software FIR resampler
                                with configurable quality presets. It does not
                                support IIR resampling or hardware acceleration.}
    @h2xmlm_dataMaxInputPorts  {1}
    @h2xmlm_dataInputPorts     {IN=MFC_DATA_INPUT_PORT}
    @h2xmlm_dataOutputPorts    {OUT=MFC_DATA_OUTPUT_PORT}
    @h2xmlm_dataMaxOutputPorts {1}
    @h2xmlm_supportedContTypes {APM_CONTAINER_TYPE_SC, APM_CONTAINER_TYPE_GC}
    @h2xmlm_isOffloadable      {true}
    @h2xmlm_stackSize          {CAPI_MFC_OSRC_STACK_SIZE} 

    @{                      <-- Start of the Module -->
    @h2xml_Select           {param_id_mfc_resampler_cfg_t}
    @h2xmlm_InsertParameter
    @h2xml_Select           {param_id_mfc_osrc_resampler_cfg_t}
    @h2xmlm_InsertParameter
    @h2xml_Select           {param_id_mfc_output_media_fmt_t}
    @h2xmlm_InsertParameter
    @h2xml_Select           {param_id_chmixer_coeff_t}
    @h2xmlm_InsertParameter
    @h2xml_Select           {chmixer_coeff_t}
    @h2xmlm_InsertStructure
    @}                      <-- End of the Module --> */

#endif /* MFC_OSRC_API_H */
