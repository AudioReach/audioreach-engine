# Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

#Include directories
include_directories(
                    cmn/common/utils/inc/
                    cmn/api
                    cmn/common/internal_api/imcl/api
                    ${LIB_ROOT}/fwk/spf/utils/list/inc
                    ${LIB_ROOT}/fwk/spf/interfaces/module/capi_cmn/ctrl_port/inc
                    ${LIB_ROOT}/fwk/spf/containers/cmn/topologies/topo_utils/inc
                    ${LIB_ROOT}/fwk/spf/utils/lpi_pool/inc
                    ${LIB_ROOT}/fwk/spf/containers/cmn/graph_utils/inc
                   )

#Add the sub directories
add_subdirectory(cmn/build cmn)

if(CONFIG_ENCODER)
	add_subdirectory(examples/encoder/build)
endif()
if(CONFIG_ECHO_CANCELLATION)
	add_subdirectory(examples/echo_cancellation/build)
endif()
if(CONFIG_EXAMPLE_GAIN)
    add_subdirectory(examples/gain/build)
endif()
if(CONFIG_CHMIXER)
    add_subdirectory(processing/channel_mixer/build)
endif()
if(CONFIG_MSIIR)
    add_subdirectory(processing/filters/multi_stage_iir/build)
endif()
if(CONFIG_PCM_CNV)
    add_subdirectory(cmn/pcm_mf_cnv/build)
    add_subdirectory(processing/resamplers/dynamic_resampler/build)
    add_subdirectory(processing/resamplers/iir_resampler/build)
endif()
if(CONFIG_GAIN)
    add_subdirectory(processing/volume_control/capi/gain/build)
endif()
if(CONFIG_SOFT_VOL)
    add_subdirectory(processing/volume_control/capi/soft_vol/build)
endif()
if(CONFIG_FIR_FILTER)
    add_subdirectory(processing/filters/fir/build)
endif()
if(CONFIG_POPLESS_EQUALIZER)
    add_subdirectory(processing/PoplessEqualizer/build)
endif()
if(CONFIG_SAL)
    add_subdirectory(cmn/simple_accumulator_limiter/build)
	add_subdirectory(processing/gain_control/limiter/build)
endif()
