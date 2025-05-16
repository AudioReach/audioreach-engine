/**
 * \file ar_msg.h
 * \brief 
 *  	 This file contains a utility for generating diagnostic messages.
 *   	 This file defines macros for printing debug messages on the target or in simulation.
 * 
 * \copyright
 *    Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *    SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _AR_MSG_H
#define _AR_MSG_H

#include "ar_defs.h"
#include <assert.h>

//***************************************************************************
// Debug message enable flags
//***************************************************************************


//***************************************************************************
// Define MSG() macros
//***************************************************************************

/** If the target supports diag logging use diag msg utils. Currently only hexagon targets support diag.
 *
 *  If the target doesn't support diag logging use ar logging. AudioReach logging utils
 *  can be used on sim/arm/win32. ar logging also has support for cosim depending
 *  upon the compiler flags.
 *
 *  NOTE:
 *  Techinically diag logging can also be featurized under platform specific ar osal source files but
 *  the downside is Qshrink will not work. Because the Qshrink compresses the messages at compile
 *  time by looking for MSG_1, MSG_2 .. tags and it will not compress the ar_osal_log() prints.
 *  In future, if Qshrink supports ar_osal_log() then diag utils can be featurized in ar_osal_log.c
 *  itself.
 *
 */

/**
 * Maps AR_MSG -> Diag macros MSG(), MSG_!, MSG_2 ..
 */
#include "msg.h"
#include "msgcfg.h"

// Legacy debug priority messages
#define DBG_LOW_PRIO MSG_LEGACY_LOW     /**< Low priority debug message. */
#define DBG_MED_PRIO MSG_LEGACY_MED     /**< Medium priority debug message. */
#define DBG_HIGH_PRIO MSG_LEGACY_HIGH   /**< High priority debug message. */
#define DBG_ERROR_PRIO MSG_LEGACY_ERROR /**< Error priority debug message. */
#define DBG_FATAL_PRIO MSG_LEGACY_FATAL /**< Fatal priority debug message. */

#if defined(USES_AUDIO_IN_ISLAND)
#define AR_MSG_ISLAND(xx_ss_mask, xx_fmt, ...) MICRO_MSG(MSG_SSID_QDSP6, xx_ss_mask, xx_fmt, ##__VA_ARGS__)
#else
#define AR_MSG_ISLAND AR_MSG
#endif


// determining the process domain based on the DSP mode information in the build env
#ifdef MDSPMODE
#define PROC_DOMAIN  " MDSP: "
#endif

#ifdef ADSPMODE
#define PROC_DOMAIN  " ADSP: "
#endif

#ifdef CDSPMODE
#define PROC_DOMAIN  " CDSP: "
#endif

#ifdef SDSPMODE
#define PROC_DOMAIN  " SDSP: "
#endif

#ifdef GPDSP0_IMG
#define PROC_DOMAIN  " GDSP_0: "
#endif

#ifdef GPDSP1_IMG
#define PROC_DOMAIN  " GDSP_1: "
#endif

#ifndef PROC_DOMAIN

#ifdef ADSP1_IMG
#define PROC_DOMAIN  " ADSP1: "
#elif ADSP2_IMG
#define PROC_DOMAIN  " ADSP2: "
#else
#define PROC_DOMAIN " ADSP: "
#endif

#endif

/* Utility to get variable argument
 * Slide the variable arguments to get the string '_N' to be appended */
#define AR_VA_NUM_ARGS_IMPL(a, b, c, d, e, f, g, h, i, j, _N, ...) _N
#define AR_VA_NUM_ARGS(...) AR_VA_NUM_ARGS_IMPL(, ##__VA_ARGS__, _9, _8, _7, _6, _5, _4, _3, _2, _1, )
/* Concatenates string x with y*/
#define AR_TOKENPASTE(x, y) x##y

/*Macro parses the text to MSG with _N as suffix, where N = {1, 2, 3, .... ,9} */
#define AR_MSG_x(_N) AR_TOKENPASTE(MSG, _N)

#ifndef MSG_SSID_DFLT
#define MSG_SSID_DFLT MSG_SSID_QDSP6
#endif // MSG_SSID_DFLT

/* This Logging macro supports variable arguments*/
#define AR_MSG(xx_ss_mask, xx_fmt, ...)                                                                                \
   AR_MSG_x(AR_VA_NUM_ARGS(__VA_ARGS__))(MSG_SSID_QDSP6, xx_ss_mask, PROC_DOMAIN xx_fmt, ##__VA_ARGS__)

#endif // _AR_MSG_H
