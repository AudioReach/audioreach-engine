/**
 * \file posal_timer.c
 * \brief
 *   This file contains utilities of Timers , such as   timer create, start/restart,delete.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "posal.h"
#include <signal.h>           /* Definition of SIGEV_* constants */
#include <time.h>
#include <unistd.h> // for usleep
#include "posal_internal.h"
#include "posal_target_i.h"
#include <ar_osal_timer.h>

/*--------------------------------------------------------------*/
/* Macro / Global definitions                                            */
/* -------------------------------------------------------------*/
#define ATS_TIMER_MAX_DURATION 0
#define TIMER_SIGNAL_MARGIN 300
#define TIMER_SLEEP_MARGIN 200

/* =======================================================================
 **                          Function Definitions
 ** ======================================================================= */

/**
  Deletes an existing timer.

  @datatypes
  posal_timer_t

  @param[in] pTimer   Pointer to the POSAL timer object.

  @return
  Indication of success (0) or failure (nonzero).

  @dependencies
  The timer object must be created using posal_timer_create().
 */
ar_result_t posal_timer_destroy(posal_timer_t *pp_obj)
{
   return posal_timer_destroy_v2(pp_obj);
}

ar_result_t posal_timer_destroy_v2(posal_timer_t *pp_obj)
{
   posal_timer_info_t *p_timer = NULL;
   int nStatus = AR_EOK;

   if (!pp_obj || !*pp_obj)
      return AR_EBADPARAM;

   p_timer = (posal_timer_info_t *)(*pp_obj);
   if (!p_timer->timer_obj)
      return AR_EBADPARAM;

   timer_t *timerId = (timer_t *)p_timer->timer_obj;

   nStatus = timer_delete(*timerId);
   if (nStatus != 0) {
      AR_MSG(DBG_ERROR_PRIO, "Failed to destroy timer");
      return AR_EFAILED;
   }

   free(timerId);
   free(p_timer);
   *pp_obj = NULL;

   return nStatus;
}

static void posal_timer_expire_cb(union sigval sv)
{
   posal_timer_info_t *p_timer = (posal_timer_info_t *)sv.sival_ptr;
   posal_channel_internal_t *p_channel = (posal_channel_internal_t *)p_timer->pChannel;
   posal_signal_set_target_inline(&p_channel->anysig, p_timer->timer_sigmask);
#ifdef DEBUG_POSAL_TIMER
      prev_trigger_count = trigger_counter;
      trigger_counter++;
      is_timer_triggered = TRUE;
#endif // DEBUG_POSAL_TIMER
}

/**
  Creates a timer in the default deferrable timer group.

  @datatypes
  posal_timer_t \n
  #posal_timer_duration_t \n
  #posal_timer_src_t \n
  posal_signal_t

  @param[in] pTimer      Pointer to the POSAL timer object.
  @param[in] timerType   One of the following:
                          - #POSAL_TIMER_ONESHOT_DURATION
                          - #POSAL_TIMER_PERIODIC
                          - #POSAL_TIMER_ONESHOT_ABSOLUTE
                            @tablebulletend
  @param[in] clockSource Clock source is #POSAL_TIMER_USER.
  @param[in] pSignal     Pointer to the signal to be generated when the timer
                         expires.

  @detdesc
  The caller must allocate the memory for the timer structure and pass the
  pointer to this function.
  @par
  After calling this function, call the appropriate start functions based on
  the type of timer.

  @return
  An indication of success (0) or failure (nonzero).

  @dependencies
  This function must be called before arming the timer. @newpage
 */
int32_t posal_timer_create(posal_timer_t *        pp_timer,
                           posal_timer_duration_t timerType,
                           posal_timer_src_t      clockSource,
                           posal_signal_t         p_signal,
                           POSAL_HEAP_ID          heap_id)
{
   return posal_timer_create_v2(pp_timer,
                                timerType,
                                clockSource,
                                POSAL_TIMER_NOTIFY_OBJ_TYPE_SIGNAL,
                                p_signal,
                                heap_id);
}

int32_t posal_timer_create_v2(posal_timer_t *                        pp_timer,
                              posal_timer_duration_t                 timerType,
                              posal_timer_src_t                      clockSource,
                              posal_timer_client_notification_type_t notification_type,
                              void *                                 client_info_ptr,
                              POSAL_HEAP_ID                          heap_id)
{
   int nStatus = 0;
   struct sigevent timer_event = {0};

   if ((NULL == pp_timer) || (NULL == client_info_ptr) ||
       (notification_type >= MAX_SUPPORTED_POSAL_TIMER_NOTIFY_OBJ_TYPES))
   {
      AR_MSG(DBG_ERROR_PRIO, "Bad inputarguments for timer creation");
      return AR_EBADPARAM;
   }

   if (POSAL_TIMER_USER != clockSource)
   {
      AR_MSG(DBG_ERROR_PRIO, "Only USER TIMER supported for now");
      return AR_EBADPARAM;
   }

   //Allocate posal_timer_info
   posal_timer_info_t *p_timer = NULL;
   if (NULL == (p_timer = (posal_timer_info_t *)posal_memory_malloc(sizeof(posal_timer_info_t), heap_id)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Memory allocation failure");
      return AR_ENOMEMORY;
   }
   memset(p_timer, 0, sizeof(posal_timer_info_t));

   //Allocate timer id
   timer_t *timerId = NULL;
   if (NULL == (timerId = (posal_timer_info_t *)posal_memory_malloc(sizeof(timer_t), heap_id)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Memory allocation failure");
      posal_memory_free(p_timer);
      posal_memory_free(timerId);
      return AR_ENOMEMORY;
   }
   memset(timerId, 0, sizeof(timer_t));

   // init timer structure
   p_timer->istimerCreated = FALSE;
   p_timer->uTimerType     = (uint32_t)timerType;
   p_timer->duration       = ATS_TIMER_MAX_DURATION;
   p_timer->heap_id        = (POSAL_HEAP_ID)heap_id;
   p_timer->timer_obj      = timerId; //save pointer to timer

   //Set signal for timer
   posal_signal_t p_signal = NULL;
   if (POSAL_TIMER_NOTIFY_OBJ_TYPE_SIGNAL == notification_type)
   {
      p_signal = client_info_ptr;
      if (NULL == (p_timer->pChannel = posal_signal_get_channel(p_signal)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Signal does not belong to any channel");
         posal_memory_free(p_timer);
         posal_memory_free(timerId);
         return AR_EFAILED;
      }
      p_timer->timer_sigmask = posal_signal_get_channel_bit(p_signal);
   }
   //Create linux timer
   timer_event.sigev_notify = SIGEV_THREAD;
   timer_event.sigev_notify_function = &posal_timer_expire_cb;
   timer_event.sigev_value.sival_ptr = p_timer; //Save the timer info for callback function

   nStatus = timer_create(CLOCK_REALTIME, &timer_event, timerId);
   if (nStatus != 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to create timer");
      posal_memory_free(p_timer);
      posal_memory_free(timerId);
      return AR_EFAILED;
   }

   p_timer->istimerCreated = TRUE;
   *pp_timer = (posal_timer_t *)p_timer;

   return AR_EOK;
}
/**
  Gets the duration of the specified timer.

  @datatypes
  posal_timer_t

  @param[in] pTimer   Pointer to the POSAL timer object.

  @return
  Duration of the timer, in microseconds.

  @dependencies
  The timer object must be created using posal_timer_create().
  @newpage
 */
uint64_t posal_timer_get_duration(posal_timer_t p_obj)
{
   return 0;
}

/**
  Creates a synchronous sleep timer. Control returns to the callee
  after the timer expires.

  @param[in] llMicrosec  Duration the timer will sleep.

  @return
  Always returns 0. The return type is int for backwards compatibility.

  @dependencies
  None.
 */
int32_t posal_timer_sleep(int64_t llMicrosec)
{
#ifdef DEBUG_POSAL_TIMER
   AR_MSG(DBG_LOW_PRIO, "wait until sleep timer expires");
#endif // DEBUG_POSAL_TIMER

   usleep((useconds_t)llMicrosec);

   return 0;
}

/**
  Gets the wall clock time.

  @return
  Returns the wall clock time in microseconds.

  @dependencies
  None.
 */
uint64_t posal_timer_get_time(void)
{
   return ar_timer_get_time_in_us();
}

/**
  Gets the wall clock time in milliseconds.

  Converts ticks into milliseconds,
      1 tick = 1/19.2MHz seconds

  MilliSeconds = Ticks * 10ULL/192000ULL. Compiler uses magic multiply functions
  to
  resolve this repeated fractional binary. performance is 10 cycles.

  @return
  Returns the wall clock time in milliseconds.

  @dependencies
  None. @newpage
 */
uint64_t posal_timer_get_time_in_msec(void)
{
   return  ar_timer_get_time_in_ms();
}

/**
  Restarts the duration-based one-shot timer.

  @datatypes
  posal_timer_t

  @param[in] pTimer    Pointer to the POSAL timer object.
  @param[in] duration  Duration of the timer, in microseconds.

  @return
  An indication of success (0) or failure (nonzero).

  @dependencies
  The timer must be created using posal_timer_create(). @newpage
 */
int32_t posal_timer_oneshot_start_duration(posal_timer_t p_obj, int64_t duration)
{
   return AR_EOK;
}

/**
  Starts the oneshot absolute timer.

  @datatypes
  posal_timer_t

  @param[in] pTimer    Pointer to the POSAL timer object.
  @param[in] duration  Duration of the timer, in microseconds.

  @return
  An indication of success (0) or failure (nonzero).

  @dependencies
  The timer must be created using posal_timer_create().
 */
int32_t posal_timer_oneshot_start_absolute(posal_timer_t p_obj, int64_t time)
{
   int nStatus = AR_EOK;
   posal_timer_info_t *p_timer = (posal_timer_info_t *)p_obj;
   timer_t *timer = (timer_t*) p_timer->timer_obj;
   struct itimerspec its = {0};

   // Set the timer delay and interval
   its.it_interval.tv_sec = 0;
   its.it_interval.tv_nsec = 0;

   //Indicates when the timer will fire next
   its.it_value.tv_sec = time / 1000000;
   its.it_value.tv_nsec = (time % 1000000) * 1000;

   nStatus = timer_settime(*timer, TIMER_ABSTIME, &its, NULL);
   if (nStatus != 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to start oneshot absolute timer for time: %lld", time);
      return AR_EFAILED;
   }

   return nStatus;
}

/**
  Starts the periodic timer.

  @datatypes
  posal_timer_t

  @param[in] pTimer    Pointer to the POSAL timer object.
  @param[in] duration  Duration of the timer, in microseconds.

  @return
  An indication of success (0) or failure (nonzero).

  @dependencies
  The timer must be created using posal_timer_create().
 */
int32_t posal_timer_periodic_start(posal_timer_t p_obj, int64_t duration)
{
   int nStatus = 0;
   posal_timer_info_t *p_timer = (posal_timer_info_t *)p_obj;
   timer_t *timer = (timer_t*) p_timer->timer_obj;
   struct itimerspec its = {0};

   //Set the timer delay and interval
   its.it_interval.tv_sec = 0;
   its.it_interval.tv_nsec = duration * 1000; //Convert duration to nanoseconds
   its.it_value.tv_sec = 0;
   its.it_value.tv_nsec = duration * 1000; //Indicates when the timer will fire next

   //Start the timer
   nStatus = timer_settime(*timer, 0, &its, NULL);
   if (nStatus != 0)
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to start timer");
      return AR_EFAILED;
   }

   return AR_EOK;
}

/**
  Starts the periodic timer after the specified duration.

  @datatypes
  posal_timer_t

  @param[in] pTimer    Pointer to the POSAL timer object.
  @param[in] duration  Duration of the timer, in microseconds.

  @return
  An indication of success (0) or failure (nonzero).

  @dependencies
  The timer must be created using posal_timer_create().
 */
int32_t posal_timer_periodic_start_with_offset(posal_timer_t p_obj, int64_t periodic_duration, int64_t start_offset)
{
   return AR_EOK;
}

/**
  Cancels the duration-based one-shot timer.

  @datatypes
  posal_timer_t

  @param[in] pTimer   Pointer to the POSAL timer object.

  @return
  An indication of success (0) or failure (nonzero).

  @dependencies
  The timer must be created using posal_timer_create(). @newpage
 */
int32_t posal_timer_stop(posal_timer_t p_obj)
{
   return AR_EOK;
}

/**
  Gets the remaining duration of the timer.

  @datatypes
  posal_timer_t

  @param[in] pTimer   Pointer to the POSAL timer object.

  @return
  Remaining timer duration in usec.
 */
uint64_t posal_timer_get_remaining_duration(posal_timer_t p_obj)
{
   uint64_t  duration = 0;

   return duration;
}

/**
   Gets the HW tick

   @param[in] none

   @return
   HW tick.
*/

uint64_t posal_timer_get_hw_ticks(void)
{
   return AR_ENOTIMPL;
}


uint64_t posal_timer_time_to_tick(uint64_t time_us)
{
   return AR_ENOTIMPL;
}
