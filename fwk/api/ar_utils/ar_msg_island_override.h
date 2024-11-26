/**
 * \file ar_msg_island_override.h
 * \brief
 *       This file contains a utility for generating diagnostic messages.
 *       This file defines macros for printing debug messages on the target or in simulation.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// clang-format off
/*
$Header: //components/rel/avs.fwk/1.0.2/api/ar_utils/generic/ar_msg_island_override.h#1 $
*/
// clang-format on

#ifndef _AR_MSG_ISLAND_OVERRIDE_H
#define _AR_MSG_ISLAND_OVERRIDE_H

#ifdef USES_AUDIO_IN_ISLAND
#undef AR_MSG
#define AR_MSG AR_NON_GUID(AR_MSG_ISLAND)
#endif


#endif // #ifndef _AR_MSG_ISLAND_OVERRIDE_H
