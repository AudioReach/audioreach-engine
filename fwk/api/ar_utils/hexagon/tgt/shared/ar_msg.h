/**
 * \file ar_msg.h
 * \brief 
 *  	 This file contains a utility for generating diagnostic messages.
 *  	 This file defines macros for printing debug messages on the target or in simulation.
 * 
 * \copyright
 *    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *    SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _AR_MSG_H
#define _AR_MSG_H

#include "ar_guids.h"
#include "HAP_farf.h"

//map elite to Diag mask
#define DBG_LOW_PRIO   MSG_LEGACY_LOW    /**< Low priority debug message. */
#define DBG_MED_PRIO   MSG_LEGACY_MED    /**< Medium priority debug message. */
#define DBG_HIGH_PRIO  MSG_LEGACY_HIGH   /**< High priority debug message. */
#define DBG_ERROR_PRIO MSG_LEGACY_ERROR  /**< Error priority debug message. */
#define DBG_FATAL_PRIO MSG_LEGACY_FATAL  /**< Fatal priority debug message. */

#ifdef __FILENAME__
#define LOCAL_FILE_NAME AR_NON_GUID(__FILENAME__)
#elif __FILE__
#define LOCAL_FILE_NAME AR_NON_GUID(__FILE__)
#endif

#define AR_MSG_ISLAND AR_MSG   //for now - pending discussion to add hap_debug_v2_uimage

/*@{*/ /* start group MSG_MASK definitions for legacy MSG macros */
/*!
  If the message mask is MSG_LEGACY_LOW client would only see the message if
  run time mask for low  is turned on
*/
#define MSG_LEGACY_LOW MSG_MASK_0

/*!
  If the message mask is MSG_LEGACY_MED client would only see the message if
  run time mask for med  is turned on for the subsystem id
*/
#define MSG_LEGACY_MED MSG_MASK_1

/*!
  If the message mask is MSG_LEGACY_HIGH client would only see the message if
  run time mask for high  is turned on for the subsystem id
*/
#define MSG_LEGACY_HIGH MSG_MASK_2

/*!
  If the message mask is MSG_LEGACY_ERROR client would only see the message if
  run time mask for error is turned on for the subsystem id
*/
#define MSG_LEGACY_ERROR MSG_MASK_3

/*!
  If the message mask is MSG_LEGACY_FATAL client would only see the message if
  run time mask for fatal is turned on for the subsystem id
*/
#define MSG_LEGACY_FATAL MSG_MASK_4
/*@}*/ /* end group MSG_MASK definitions for legacy MSG macros */
/*@{*/ /* start group MSG_MASK definitions for MSG 2.0 macros */
#define MSG_MASK_0 (0x00000001)
#define MSG_MASK_1 (0x00000002)
#define MSG_MASK_2 (0x00000004)
#define MSG_MASK_3 (0x00000008)
#define MSG_MASK_4 (0x00000010)
/*@}*/ /* end group MSG_MASK definitions for MSG 2.0 macros */

static inline uint16 diag_to_farf_mask_map(uint16 xx_ss_mask) {
    switch(xx_ss_mask) {
        case MSG_LEGACY_LOW:
            return HAP_LEVEL_LOW;
        case MSG_LEGACY_MED:
            return HAP_LEVEL_MEDIUM;
        case MSG_LEGACY_HIGH:
            return HAP_LEVEL_HIGH;
        case MSG_LEGACY_ERROR:
            return HAP_LEVEL_ERROR;
        case MSG_LEGACY_FATAL:
            return HAP_LEVEL_FATAL;
        default:
            return HAP_LEVEL_ERROR;
    }
}

#define MSG_FARF(xx_ss_mask, xx_fmt...) \
    do { \
        if(0 == (HAP_debug_v2)) { \
            _HAP_debug_v2( \
                diag_to_farf_mask_map(xx_ss_mask), \
                LOCAL_FILE_NAME, \
                __LINE__, \
                xx_fmt); \
        } else { \
            HAP_debug_v2( \
                diag_to_farf_mask_map(xx_ss_mask), \
                LOCAL_FILE_NAME, \
                __LINE__, \
                xx_fmt); \
        } \
    } while (0)

#undef MSG
#undef MSG_1
#undef MSG_2
#undef MSG_3
#undef MSG_4
#undef MSG_5
#undef MSG_6
#undef MSG_7
#undef MSG_8
#undef MSG_9
#undef MSG_FATAL
#undef MSG_SPRINTF_1
#undef MSG_SPRINTF_2
#undef MSG_SPRINTF_3
#undef MSG_SPRINTF_4
#undef MSG_SPRINTF_5
#undef MSG_SPRINTF_6
#undef MSG_SPRINTF_7
#undef MSG_SPRINTF_8
#undef MSG_SPRINTF_FMT_VAR_3

// MSG APIs
#define MSG(xx_ss_id, xx_ss_mask, xx_fmt) \
                    MSG_FARF(xx_ss_mask,xx_fmt)
#define MSG_1(xx_ss_id, xx_ss_mask, xx_fmt,...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_2(xx_ss_id, xx_ss_mask, xx_fmt,...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_3(xx_ss_id, xx_ss_mask, xx_fmt,...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_4(xx_ss_id, xx_ss_mask, xx_fmt,...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_5(xx_ss_id, xx_ss_mask, xx_fmt,...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_6(xx_ss_id, xx_ss_mask, xx_fmt,...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_7(xx_ss_id, xx_ss_mask, xx_fmt,...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_8(xx_ss_id, xx_ss_mask, xx_fmt,...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_9(xx_ss_id, xx_ss_mask, xx_fmt,...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)

// MSG_sprintf APIs
#define MSG_SPRINTF_1(xx_ss_id, xx_ss_mask, xx_fmt, ...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_SPRINTF_2(xx_ss_id, xx_ss_mask, xx_fmt, ...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_SPRINTF_3(xx_ss_id, xx_ss_mask, xx_fmt, ...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_SPRINTF_4(xx_ss_id, xx_ss_mask, xx_fmt, ...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_SPRINTF_5(xx_ss_id, xx_ss_mask, xx_fmt, ...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_SPRINTF_6(xx_ss_id, xx_ss_mask, xx_fmt, ...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_SPRINTF_7(xx_ss_id, xx_ss_mask, xx_fmt, ...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)
#define MSG_SPRINTF_8(xx_ss_id, xx_ss_mask, xx_fmt, ...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)

#define MSG_SPRINTF_FMT_VAR_3(xx_ss_id, xx_ss_mask, xx_fmt, ...) \
                    MSG_FARF(xx_ss_mask,xx_fmt,##__VA_ARGS__)

/* Supporting macros for AR_MSG() logging utility.
 * AR_MSG is supported on both target and simulation builds. */

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
   AR_MSG_x(AR_VA_NUM_ARGS(__VA_ARGS__))(MSG_SSID_DFLT, xx_ss_mask, xx_fmt, ##__VA_ARGS__)

#endif // #ifndef _AR_MSG_H
