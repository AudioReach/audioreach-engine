/**
 * \file ar_msg.h
 * \brief
 *  	 This file contains a utility for generating diagnostic messages.
 *  	 This file defines macros for printing debug messages on the target or in simulation.
 *
 * \copyright
 *    Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *    SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _AR_MSG_H
#define _AR_MSG_H

#include "ar_defs.h"
#include "ar_guids.h"
#include <assert.h>

/** Enable (1) or disable (0) the debug messagef */
#define ar_log_debugmsg_enable 1

// use ar osal for non diag usecases
#include "ar_osal_log.h"

// Undefine earlier definitions of diag declerations
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
#undef AR_MSG_ISLAND
#undef AR_MSG

// Undefine priority messages
#undef DBG_LOW_PRIO
#undef DBG_MED_PRIO
#undef DBG_HIGH_PRIO
#undef DBG_ERROR_PRIO
#undef DBG_FATAL_PRIO
#undef MSG_LEGACY_ERROR
#undef MSG_LEGACY_HIGH
#undef MSG_LEGACY_MED
#undef MSG_LEGACY_LOW


// Legacy debug priority messages
#define DBG_LOW_PRIO AR_HIGH_PRIO     /**< Low priority debug message. */
#define DBG_MED_PRIO AR_HIGH_PRIO     /**< Medium priority debug message. */
#define DBG_HIGH_PRIO AR_HIGH_PRIO   /**< High priority debug message. */
#define DBG_ERROR_PRIO AR_ERROR_PRIO /**< Error priority debug message. */
#define DBG_FATAL_PRIO AR_HIGH_PRIO /**< Fatal priority debug message. */
#define MSG_LEGACY_ERROR DBG_ERROR_PRIO
#define MSG_LEGACY_HIGH DBG_HIGH_PRIO
#define MSG_LEGACY_MED DBG_MED_PRIO
#define MSG_LEGACY_LOW DBG_LOW_PRIO


#define AR_MSG_ISLAND AR_MSG

/* Undefine earlier definitions of MSG macros, and define for SIM */
#define MSG AR_MSG_LOG
#define MSG_1 AR_MSG_LOG
#define MSG_2 AR_MSG_LOG
#define MSG_3 AR_MSG_LOG
#define MSG_4 AR_MSG_LOG
#define MSG_5 AR_MSG_LOG
#define MSG_6 AR_MSG_LOG
#define MSG_7 AR_MSG_LOG
#define MSG_8 AR_MSG_LOG
#define MSG_9 AR_MSG_LOG
#define MSG_FATAL AR_MSG_LOG

#undef  AR_MSG_TAG
#define AR_MSG_TAG  ( "spf_on_arm" )

#ifdef __FILENAME__
#define LOCAL_FILE_NAME AR_NON_GUID(__FILENAME__)
#elif defined(__FILE_NAME__)
#define LOCAL_FILE_NAME AR_NON_GUID(__FILE_NAME__)
#elif defined(__FILE__)
#define LOCAL_FILE_NAME AR_NON_GUID(__FILE__)
#endif

#undef AR_MSG_LOG

#if defined (ARSPF_PLATFORM_LRH)
#define AR_MSG_LOG( xx_ss_sid, xx_ss_mask, xx_fmt, ...) \
   do { \
      if (ar_log_debugmsg_enable) \
      { \
         static const char *msg_tag = AR_MSG_TAG; \
         static const uint32_t line_no = __LINE__; \
         ar_log( xx_ss_mask, msg_tag, __FUNCTION__, line_no, xx_fmt, ##__VA_ARGS__); \
         if (DBG_FATAL_PRIO == (xx_ss_mask)) \
         { \
            assert(0); \
         } \
      } \
   } while(0)
#elif defined (ARSPF_PLATFORM_QNX)
#define AR_MSG_LOG( xx_ss_sid, xx_ss_mask, xx_fmt, ...) \
   do { \
      if (ar_log_debugmsg_enable) \
      { \
         static const char *msg_tag = AR_MSG_TAG; \
         static const uint32_t line_no = __LINE__; \
         ar_log( xx_ss_mask, msg_tag, __FUNCTION__, line_no, xx_fmt, ##__VA_ARGS__); \
         if (DBG_FATAL_PRIO == (xx_ss_mask)) \
         { \
            assert(0); \
         } \
      } \
   } while(0)
#else
#define AR_MSG_LOG( xx_ss_sid, xx_ss_mask, xx_fmt, ...) \
   do { \
      if (ar_log_debugmsg_enable) \
      { \
         static const char *msg_tag = AR_MSG_TAG; \
         static const char filename[] = LOCAL_FILE_NAME; \
         static const uint32_t line_no = __LINE__; \
         ar_log( xx_ss_mask, msg_tag, filename, __FUNCTION__, line_no, xx_fmt, ##__VA_ARGS__); \
         if (DBG_FATAL_PRIO == (xx_ss_mask)) \
         { \
            assert(0); \
         } \
      } \
   } while(0)
#endif //if defined (ARSPF_PLATFORM_LRH)

#define AR_MSG_DFLT_MSG_SSID MSG_SSID_QDSP6

/* This Logging macro supports variable arguments*/
#define AR_MSG(xx_ss_mask, xx_fmt, ...)  \
   AR_MSG_LOG( AR_MSG_DFLT_MSG_SSID, xx_ss_mask, xx_fmt, ##__VA_ARGS__ )


#endif // #ifndef _AR_MSG_H
