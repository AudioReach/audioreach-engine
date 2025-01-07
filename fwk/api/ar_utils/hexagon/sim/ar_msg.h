/**
 * \file ar_msg.h
 * \brief 
 *  	 This file contains a utility for generating diagnostic messages.
 *  	This file defines macros for printing debug messages on the target or in simulation
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
#include "msg.h"
#include "msgcfg.h"

// user pd
#if !defined(ROOTPD_AVS_INTEGRATION)
#include "cosim_log_island_i.h"
#else // root pd
#include "cosim_log_rpd_island_i.h"
#endif

/** Enable (1) or disable (0) the debug messagef */
#define ar_log_debugmsg_enable 1

/** Variable for setting priority threshold for the debug message in simulator. */
extern uint8_t ar_logs_debugmsg_lowest_prio;

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


// Legacy debug priority messages
#define DBG_LOW_PRIO MSG_LEGACY_LOW     /**< Low priority debug message. */
#define DBG_MED_PRIO MSG_LEGACY_MED     /**< Medium priority debug message. */
#define DBG_HIGH_PRIO MSG_LEGACY_HIGH   /**< High priority debug message. */
#define DBG_ERROR_PRIO MSG_LEGACY_ERROR /**< Error priority debug message. */
#define DBG_FATAL_PRIO MSG_LEGACY_FATAL /**< Fatal priority debug message. */

#ifdef __FILENAME__
#define LOCAL_FILE_NAME AR_NON_GUID(__FILENAME__)
#elif __FILE__
#define LOCAL_FILE_NAME AR_NON_GUID(__FILE__)
#endif

#ifndef LOCAL_FILE_NAME
#define LOCAL_FILE_NAME AR_NON_GUID("")
#endif

// user pd
#if !defined(ROOTPD_AVS_INTEGRATION)

#define AR_MSG_DOMAIN_ID  2
#define PROC_DOMAIN  " ADSP: "

#undef AR_MSG_LOG
#define AR_MSG_LOG(num_args, priority, xx_fmt, ...)                                                                    \
   do                                                                                                                  \
   {                                                                                                                   \
      if (ar_log_debugmsg_enable && (priority >= ar_logs_debugmsg_lowest_prio))                                        \
      {                                                                                                                \
         static const char xx_ar_log_format[] = (xx_fmt);                                                              \
         static const char xx_ar_log_file[]   = LOCAL_FILE_NAME;                                                       \
                                                                                                                       \
         static const ar_dbg_log_msg_t xx_ar_log_msg = {.msg_header.b.msg_line     = __LINE__,                         \
                                                        .msg_header.b.msg_priority = priority,                         \
                                                        .msg_header.b.domain_id    = AR_MSG_DOMAIN_ID,                 \
                                                        .msg_header.b.num_words    = 2 + num_args,                     \
                                                        .msg_format_ptr            = (char *)xx_ar_log_format,         \
                                                        .msg_filename_ptr          = xx_ar_log_file };                 \
         uint32_t time_ms = ar_timer_get_time_in_ms();                                                                 \
         ar_dbg_log(&xx_ar_log_msg, time_ms, ##__VA_ARGS__);                                                           \
         if (DBG_FATAL_PRIO == (priority))                                                                             \
         {                                                                                                             \
            assert(0);                                                                                                 \
         }                                                                                                             \
      }                                                                                                                \
   } while (0)

#undef AR_MSG_ISLAND_LOG
#define AR_MSG_ISLAND_LOG(num_args, priority, xx_fmt, ...)                                                             \
   do                                                                                                                  \
   {                                                                                                                   \
      if (ar_log_debugmsg_enable && (priority >= ar_logs_debugmsg_lowest_prio))                                        \
      {                                                                                                                \
         static const char xx_ar_log_format[] = (xx_fmt);                                                              \
         static const char xx_ar_log_file[]   = LOCAL_FILE_NAME;                                                       \
                                                                                                                       \
         static const ar_dbg_log_msg_t xx_ar_log_msg = {.msg_header.b.msg_line     = __LINE__,                         \
                                                        .msg_header.b.msg_priority = priority,                         \
                                                        .msg_header.b.domain_id    = AR_MSG_DOMAIN_ID,                 \
                                                        .msg_header.b.num_words    = 2 + num_args,                     \
                                                        .msg_format_ptr            = (char *)xx_ar_log_format,         \
                                                        .msg_filename_ptr          = xx_ar_log_file };                 \
         uint32_t time_ms = ar_timer_get_time_in_ms();                                                                 \
         ar_dbg_log(&xx_ar_log_msg, time_ms, ##__VA_ARGS__);                                                           \
         if (DBG_FATAL_PRIO == (priority))                                                                             \
         {                                                                                                             \
            assert(0);                                                                                                 \
         }                                                                                                             \
      }                                                                                                                \
   } while (0)

#endif

// rootpd
#if defined(ROOTPD_AVS_INTEGRATION)

#define AR_MSG_DOMAIN_ID 1
#define PROC_DOMAIN  " MDSP: "

#undef AR_MSG_LOG
#define AR_MSG_LOG(num_args, priority, xx_fmt, ...)                                                                    \
   do                                                                                                                  \
   {                                                                                                                   \
      if (ar_log_debugmsg_enable && (priority >= ar_logs_debugmsg_lowest_prio))                                        \
      {                                                                                                                \
         static const char                               xx_ar_log_format[] QSR_ATTR = (xx_fmt);                       \
         static const char                               xx_ar_log_file[] QSR_ATTR   = LOCAL_FILE_NAME;                \
         static const ar_rpd_dbg_log_msg_t xx_ar_log_msg QSR_ATTR = { .msg_header.b.msg_line     = __LINE__,           \
                                                                      .msg_header.b.msg_priority = priority,           \
                                                                      .msg_header.b.domain_id    = AR_MSG_DOMAIN_ID,   \
                                                                      .msg_header.b.num_words    = 2 + num_args,       \
                                                                      .msg_format_ptr   = (char *)xx_ar_log_format,    \
                                                                      .msg_filename_ptr = xx_ar_log_file };            \
         uint32_t time_ms = ar_timer_get_time_in_ms();                          \
         ar_rpd_dbg_log(&xx_ar_log_msg, time_ms, ##__VA_ARGS__);                                                        \
         if (DBG_FATAL_PRIO == (priority))                                                                             \
         {                                                                                                             \
            assert(0);                                                                                                 \
         }                                                                                                             \
      }                                                                                                                \
   } while (0)

#endif

#undef DBG_MSG_CAST
#define DBG_MSG_CAST (uint32_t)

#define MSG(xx_ss_id, xx_ss_mask, xx_fmt) AR_MSG_LOG(0, xx_ss_mask, xx_fmt)

#define MSG_1(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1) AR_MSG_LOG(1, xx_ss_mask, xx_fmt, DBG_MSG_CAST xx_arg1)

#define MSG_2(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2)                                                          \
   AR_MSG_LOG(2, xx_ss_mask, xx_fmt, DBG_MSG_CAST xx_arg1, DBG_MSG_CAST xx_arg2)

#define MSG_3(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3)                                                 \
   AR_MSG_LOG(3, xx_ss_mask, xx_fmt, DBG_MSG_CAST xx_arg1, DBG_MSG_CAST xx_arg2, DBG_MSG_CAST xx_arg3)

#define MSG_4(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4)                                        \
   AR_MSG_LOG(4,                                                                                                       \
              xx_ss_mask,                                                                                              \
              xx_fmt,                                                                                                  \
              DBG_MSG_CAST xx_arg1,                                                                                    \
              DBG_MSG_CAST xx_arg2,                                                                                    \
              DBG_MSG_CAST xx_arg3,                                                                                    \
              DBG_MSG_CAST xx_arg4)

#define MSG_5(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4, xx_arg5)                               \
   AR_MSG_LOG(5,                                                                                                       \
              xx_ss_mask,                                                                                              \
              xx_fmt,                                                                                                  \
              DBG_MSG_CAST xx_arg1,                                                                                    \
              DBG_MSG_CAST xx_arg2,                                                                                    \
              DBG_MSG_CAST xx_arg3,                                                                                    \
              DBG_MSG_CAST xx_arg4,                                                                                    \
              DBG_MSG_CAST xx_arg5)

#define MSG_6(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4, xx_arg5, xx_arg6)                      \
   AR_MSG_LOG(6,                                                                                                       \
              xx_ss_mask,                                                                                              \
              xx_fmt,                                                                                                  \
              DBG_MSG_CAST xx_arg1,                                                                                    \
              DBG_MSG_CAST xx_arg2,                                                                                    \
              DBG_MSG_CAST xx_arg3,                                                                                    \
              DBG_MSG_CAST xx_arg4,                                                                                    \
              DBG_MSG_CAST xx_arg5,                                                                                    \
              DBG_MSG_CAST xx_arg6)

#define MSG_7(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4, xx_arg5, xx_arg6, xx_arg7)             \
   AR_MSG_LOG(7,                                                                                                       \
              xx_ss_mask,                                                                                              \
              xx_fmt,                                                                                                  \
              DBG_MSG_CAST xx_arg1,                                                                                    \
              DBG_MSG_CAST xx_arg2,                                                                                    \
              DBG_MSG_CAST xx_arg3,                                                                                    \
              DBG_MSG_CAST xx_arg4,                                                                                    \
              DBG_MSG_CAST xx_arg5,                                                                                    \
              DBG_MSG_CAST xx_arg6,                                                                                    \
              DBG_MSG_CAST xx_arg7)

#define MSG_8(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4, xx_arg5, xx_arg6, xx_arg7, xx_arg8)    \
   AR_MSG_LOG(8,                                                                                                       \
              xx_ss_mask,                                                                                              \
              xx_fmt,                                                                                                  \
              DBG_MSG_CAST xx_arg1,                                                                                    \
              DBG_MSG_CAST xx_arg2,                                                                                    \
              DBG_MSG_CAST xx_arg3,                                                                                    \
              DBG_MSG_CAST xx_arg4,                                                                                    \
              DBG_MSG_CAST xx_arg5,                                                                                    \
              DBG_MSG_CAST xx_arg6,                                                                                    \
              DBG_MSG_CAST xx_arg7,                                                                                    \
              DBG_MSG_CAST xx_arg8)

#define MSG_9(                                                                                                         \
   xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4, xx_arg5, xx_arg6, xx_arg7, xx_arg8, xx_arg9)      \
   AR_MSG_LOG(9,                                                                                                       \
              xx_ss_mask,                                                                                              \
              xx_fmt,                                                                                                  \
              DBG_MSG_CAST xx_arg1,                                                                                    \
              DBG_MSG_CAST xx_arg2,                                                                                    \
              DBG_MSG_CAST xx_arg3,                                                                                    \
              DBG_MSG_CAST xx_arg4,                                                                                    \
              DBG_MSG_CAST xx_arg5,                                                                                    \
              DBG_MSG_CAST xx_arg6,                                                                                    \
              DBG_MSG_CAST xx_arg7,                                                                                    \
              DBG_MSG_CAST xx_arg8,                                                                                    \
              DBG_MSG_CAST xx_arg9)

#define ISLAND_MSG(xx_ss_id, xx_ss_mask, xx_fmt) AR_MSG_ISLAND_LOG(0, xx_ss_mask, xx_fmt)

#define ISLAND_MSG_1(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1)                                                            \
   AR_MSG_ISLAND_LOG(1, xx_ss_mask, xx_fmt, DBG_MSG_CAST xx_arg1)

#define ISLAND_MSG_2(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2)                                                   \
   AR_MSG_ISLAND_LOG(2, xx_ss_mask, xx_fmt, DBG_MSG_CAST xx_arg1, DBG_MSG_CAST xx_arg2)

#define ISLAND_MSG_3(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3)                                          \
   AR_MSG_ISLAND_LOG(3, xx_ss_mask, xx_fmt, DBG_MSG_CAST xx_arg1, DBG_MSG_CAST xx_arg2, DBG_MSG_CAST xx_arg3)

#define ISLAND_MSG_4(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4)                                 \
   AR_MSG_ISLAND_LOG(4,                                                                                                \
                     xx_ss_mask,                                                                                       \
                     xx_fmt,                                                                                           \
                     DBG_MSG_CAST xx_arg1,                                                                             \
                     DBG_MSG_CAST xx_arg2,                                                                             \
                     DBG_MSG_CAST xx_arg3,                                                                             \
                     DBG_MSG_CAST xx_arg4)

#define ISLAND_MSG_5(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4, xx_arg5)                        \
   AR_MSG_ISLAND_LOG(5,                                                                                                \
                     xx_ss_mask,                                                                                       \
                     xx_fmt,                                                                                           \
                     DBG_MSG_CAST xx_arg1,                                                                             \
                     DBG_MSG_CAST xx_arg2,                                                                             \
                     DBG_MSG_CAST xx_arg3,                                                                             \
                     DBG_MSG_CAST xx_arg4,                                                                             \
                     DBG_MSG_CAST xx_arg5)

#define ISLAND_MSG_6(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4, xx_arg5, xx_arg6)               \
   AR_MSG_ISLAND_LOG(6,                                                                                                \
                     xx_ss_mask,                                                                                       \
                     xx_fmt,                                                                                           \
                     DBG_MSG_CAST xx_arg1,                                                                             \
                     DBG_MSG_CAST xx_arg2,                                                                             \
                     DBG_MSG_CAST xx_arg3,                                                                             \
                     DBG_MSG_CAST xx_arg4,                                                                             \
                     DBG_MSG_CAST xx_arg5,                                                                             \
                     DBG_MSG_CAST xx_arg6)

#define ISLAND_MSG_7(xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4, xx_arg5, xx_arg6, xx_arg7)      \
   AR_MSG_ISLAND_LOG(7,                                                                                                \
                     xx_ss_mask,                                                                                       \
                     xx_fmt,                                                                                           \
                     DBG_MSG_CAST xx_arg1,                                                                             \
                     DBG_MSG_CAST xx_arg2,                                                                             \
                     DBG_MSG_CAST xx_arg3,                                                                             \
                     DBG_MSG_CAST xx_arg4,                                                                             \
                     DBG_MSG_CAST xx_arg5,                                                                             \
                     DBG_MSG_CAST xx_arg6,                                                                             \
                     DBG_MSG_CAST xx_arg7)

#define ISLAND_MSG_8(                                                                                                  \
   xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4, xx_arg5, xx_arg6, xx_arg7, xx_arg8)               \
   AR_MSG_ISLAND_LOG(8,                                                                                                \
                     xx_ss_mask,                                                                                       \
                     xx_fmt,                                                                                           \
                     DBG_MSG_CAST xx_arg1,                                                                             \
                     DBG_MSG_CAST xx_arg2,                                                                             \
                     DBG_MSG_CAST xx_arg3,                                                                             \
                     DBG_MSG_CAST xx_arg4,                                                                             \
                     DBG_MSG_CAST xx_arg5,                                                                             \
                     DBG_MSG_CAST xx_arg6,                                                                             \
                     DBG_MSG_CAST xx_arg7,                                                                             \
                     DBG_MSG_CAST xx_arg8)

#define ISLAND_MSG_9(                                                                                                  \
   xx_ss_id, xx_ss_mask, xx_fmt, xx_arg1, xx_arg2, xx_arg3, xx_arg4, xx_arg5, xx_arg6, xx_arg7, xx_arg8, xx_arg9)      \
   AR_MSG_ISLAND_LOG(9,                                                                                                \
                     xx_ss_mask,                                                                                       \
                     xx_fmt,                                                                                           \
                     DBG_MSG_CAST xx_arg1,                                                                             \
                     DBG_MSG_CAST xx_arg2,                                                                             \
                     DBG_MSG_CAST xx_arg3,                                                                             \
                     DBG_MSG_CAST xx_arg4,                                                                             \
                     DBG_MSG_CAST xx_arg5,                                                                             \
                     DBG_MSG_CAST xx_arg6,                                                                             \
                     DBG_MSG_CAST xx_arg7,                                                                             \
                     DBG_MSG_CAST xx_arg8,                                                                             \
                     DBG_MSG_CAST xx_arg9)

/* Supporting macros for MM_MSG() logging utility.
 * MM_MSG is supported on both target and simulation builds. */

/* Utility to get variable argument
 * Slide the variable arguments to get the string '_N' to be appended */
#define AR_VA_NUM_ARGS_IMPL(a, b, c, d, e, f, g, h, i, j, _N, ...) _N
#define AR_VA_NUM_ARGS(...) AR_VA_NUM_ARGS_IMPL(, ##__VA_ARGS__, _9, _8, _7, _6, _5, _4, _3, _2, _1, )
/* Concatenates string x with y*/
#define AR_TOKENPASTE(x, y) x##y

/*Macro parses the text to MSG with _N as suffix, where N = {1, 2, 3, .... ,9} */
#define AR_MSG_x(_N) AR_TOKENPASTE(MSG, _N)

/*Macro parses the text to MSG with _N as suffix, where N = {1, 2, 3, .... ,9} */
#define AR_MSG_ISLAND_x(_N) AR_TOKENPASTE(ISLAND_MSG, _N)

#ifndef MSG_SSID_QDSP6
#define MSG_SSID_QDSP6
#endif // AR_MSG_DFLT_MSG_SSID

#if defined(ISLAND_TEST)
#undef AR_MSG_ISLAND
#define AR_MSG_ISLAND AR_NON_GUID(_AR_MSG_ISLAND_)
#else
#undef AR_MSG_ISLAND
#define AR_MSG_ISLAND AR_NON_GUID(AR_MSG_I)
#endif

/* This Logging macro supports variable arguments*/
#define AR_MSG(xx_ss_mask, xx_fmt, ...)                                                                                \
   AR_MSG_x(AR_VA_NUM_ARGS(__VA_ARGS__))(MSG_SSID_QDSP6, xx_ss_mask, PROC_DOMAIN xx_fmt, ##__VA_ARGS__)

/* This Logging macro supports variable arguments*/
#define AR_MSG_I(xx_ss_mask, xx_fmt, ...)                                                                                \
   AR_MSG_x(AR_VA_NUM_ARGS(__VA_ARGS__))(MSG_SSID_QDSP6, xx_ss_mask, PROC_DOMAIN xx_fmt, ##__VA_ARGS__)

/* This Logging macro supports variable arguments*/
#define _AR_MSG_ISLAND_(xx_ss_mask, xx_fmt, ...)                                                                         \
   AR_MSG_ISLAND_x(AR_VA_NUM_ARGS(__VA_ARGS__))(MSG_SSID_QDSP6, xx_ss_mask, PROC_DOMAIN xx_fmt, ##__VA_ARGS__)
#endif // #ifndef _AR_MSG_H
